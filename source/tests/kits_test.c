/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA's drum kits on this computer (the KIT engine: firmware/src/kawa_drums.c, firmware/src/kawa_kits.h).
 * Every drum of every kit is hit once (drum_on), rendered (drum_chunk, then the track's TONE as kawa_snd.c
 * applies it) and measured: the peak, the loudness (the loudest 50 ms), the decay (-20 / -40 dB), the
 * spectral centroid, the flatness (noise or tone), the energy in 8 bands. Then the kits are compared: per key,
 * the distance between the feature vectors of two kits, and a matrix of the kits (the mean over the 16 keys).
 * Out: build/kits/kit_<n>.wav (the 16 drums, 0.7 s each) and build/kits/beat_<n>.wav (a short beat).
 * It fails (exit 1) on a hard clip, DC, a silent drum, a NaN, a voice that never stops, a choke that does not
 * stop, or two kits too alike.
 *   tests/run_kits_test.sh
 * The knobs per kit (REC: the recommended DRIVE and TONE of each kit's preset in kawa_data.c); -DKT_TODAY uses
 * the presets as they were (for a comparison). */
#define main kawa_host_main
#include "../firmware/src/kawa.c"
#undef main
#include <math.h>
#include <time.h>

#define KT_FS 44100u
#define KT_HIT (KT_FS * 7u / 10u)              /* 0.7 s a drum */
#define KT_MAX (KT_FS * 20u)                   /* a voice must have stopped by then (the presets' knobs) */
#define KT_NB 8
#define KT_NF (5 + KT_NB)                      /* the feature vector */
#define KT_FFT 2048

typedef struct { int8_t tune, decay, drive, crush, ton; } kt_knob_t;
#ifdef KT_TODAY
static const kt_knob_t REC[NKIT] = {{0, 0, 1, 0, 13}, {0, 0, 3, 0, 12}, {0, 0, 2, 0, 15}, {0, 0, 1, 0, 11},
                                    {0, 0, 4, 2, 9}, {0, 0, 0, 0, 15}, {0, 0, 1, 0, 14}, {0, 0, 0, 0, 14}};
#else
static const kt_knob_t REC[NKIT] = {{0, 0, 0, 0, 15}, {0, 0, 1, 0, 13}, {0, 0, 1, 0, 15}, {0, 0, 0, 0, 13},
                                    {0, 0, 1, 0, 11}, {0, 0, 0, 0, 15}, {0, 0, 0, 0, 15}, {0, 0, 0, 0, 15}};
#endif
static const char *const KIT_EN[NKIT] = {"ACOUSTIC", "HIP HOP", "TECHNO", "DUB REGGAE", "LO-FI", "GAME BOY",
                                         "SNES", "PERCU"};

typedef struct {
    double peak, loud, crest, dc, t20, t40, cent, flat, tpk, band[KT_NB];
    int clip, x20, x40;                         /* x: the decay extrapolated (the drum rings past 0.7 s) */
    uint32_t stop;                              /* frames until both voices are off (0: never, within KT_MAX) */
    double f[KT_NF];
} kt_feat_t;
static kt_feat_t F[NKIT][NDRUM];
static int32_t kt_buf[KT_MAX];
static int kt_fail;

static void kt_bad(const char *what, uint32_t k, uint32_t d)
{
    printf("FAIL  kit %u %-10s key %2u %-3s: %s\n", k, KIT_EN[k], d + 1u, KIT_CODE3[k][d], what);
    kt_fail = 1;
}

/* one track (0) on the KIT engine with these knobs; a hit of drum d, n frames into y (the track's TONE after
 * the kit, as kawa_snd.c's track_chunk). Returns when both voices stopped (frames; 0 = not within max) */
static uint32_t kt_render(uint32_t k, uint32_t d, const kt_knob_t *kn, uint32_t fx, int32_t fxv, int32_t *y,
                          uint32_t n, uint32_t max)
{
    int8_t *p = prj.snd[0].p;
    int32_t lp = 0, ton;
    uint32_t i, j, stop = 0;
    memset(drum, 0, sizeof drum);
    memset(drum_next, 0, sizeof drum_next);
    memset(&prj.snd[0], 0, sizeof prj.snd[0]);
    prj.snd[0].engine = E_KIT;
    p[P_KIT] = (int8_t)k;
    p[P_TUNE] = kn->tune;
    p[P_DECAY] = kn->decay;
    p[P_DRIVE] = kn->drive;
    p[P_KCRUSH] = kn->crush;
    p[P_KTON] = kn->ton;
    ton = TON_A[clampi(p[P_KTON], 0, 15)];
    drum_on(0, d, fx == FX_PITCH ? fxv : 0, fx == FX_CRUSH ? fxv : -1);
    for (i = 0; i < max; i += CHUNK) {
        int32_t raw[CHUNK];
        for (j = 0; j < CHUNK; j++)
            raw[j] = 0;
        drum_chunk(&drum[0][0], raw);
        drum_chunk(&drum[0][1], raw);
        if (ton < 32767)
            for (j = 0; j < CHUNK; j++) { lp += ((raw[j] - lp) * ton) >> 15; raw[j] = lp; }
        for (j = 0; j < CHUNK && i + j < n; j++)
            y[i + j] = raw[j];
        if (!drum[0][0].on && !drum[0][1].on) {
            stop = i + CHUNK;
            break;
        }
    }
    for (i += CHUNK; i < n; i++)
        y[i] = 0;
    return stop;
}

/* ---- the measures */
static void kt_fft(double *re, double *im, int n)
{
    int i, j, len;
    for (i = 1, j = 0; i < n; i++) {
        int b = n >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j ^= b;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2.0 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (j = 0; j < len / 2; j++) {
                double ur = re[i + j], ui = im[i + j];
                double vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci;
                double vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
                double t;
                re[i + j] = ur + vr; im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
                t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

static const double BAND_HZ[KT_NB + 1] = {0, 90, 220, 500, 1200, 3000, 6000, 11000, 22051};

static void kt_measure(kt_feat_t *m, const int32_t *y, uint32_t n)
{
    static double re[KT_FFT], im[KT_FFT], pw[KT_FFT / 2 + 1];
    double sum = 0, env[1024], emax = 0, etot = 0, num = 0, lg = 0, ar = 0, loud2 = 0;
    uint32_t i, j, w = KT_FS / 200u, ne = n / w, ipk = 0, e40 = 0, e20 = 0, ie = 0, nb = 0;
    int b;
    memset(m->band, 0, sizeof m->band);
    m->peak = 0;
    m->clip = 0;
    for (i = 0; i < n; i++) {
        double a = fabs((double)y[i]);
        sum += y[i];
        if (a > m->peak) { m->peak = a; ipk = i; }
        if (a >= 32767.0) m->clip++;
        if (i >= 2 && y[i] == y[i - 1] && y[i] == y[i - 2] && a >= 31500.0) m->clip++;   /* a flat top */
    }
    m->tpk = ipk * 1000.0 / KT_FS;
    for (i = 0; i < ne && i < 1024u; i++) {    /* 5 ms RMS frames */
        double s2 = 0;
        for (j = 0; j < w; j++)
            s2 += (double)y[i * w + j] * y[i * w + j];
        env[i] = sqrt(s2 / w);
        if (env[i] > emax) { emax = env[i]; ie = i; }
    }
    for (i = 0; i + 10u <= ne && i < 1024u; i++) {   /* the loudest 50 ms */
        double s2 = 0;
        for (j = 0; j < 10u; j++)
            s2 += env[i + j] * env[i + j];
        if (s2 / 10 > loud2) loud2 = s2 / 10;
    }
    m->loud = 20.0 * log10(sqrt(loud2) / 32768.0 + 1e-12);
    m->crest = 20.0 * log10(m->peak / (sqrt(loud2) + 1e-9));
    for (i = ie; i < ne && i < 1024u; i++) {
        if (env[i] > emax * 0.1) e20 = i;      /* -20 dB */
        if (env[i] > emax * 0.01) e40 = i;     /* -40 dB */
    }
    m->x20 = m->x40 = 0;
    m->t20 = (e20 + 1) * 5.0;
    m->t40 = (e40 + 1) * 5.0;
    if (e40 + 1u >= ne) {                      /* still ringing: the slope of the last 200 ms */
        double a = env[ne - 41u] + 1e-9, z = env[ne - 1u] + 1e-9, slope = 20.0 * log10(z / a) / 200.0;   /* dB/ms */
        double lvl = 20.0 * log10(z / emax);
        m->x40 = 1;
        m->t40 = slope < -1e-4 ? ne * 5.0 + (-40.0 - lvl) / slope : 60000.0;
        if (m->t40 > 60000.0) m->t40 = 60000.0;
        if (e20 + 1u >= ne) {
            m->x20 = 1;
            m->t20 = slope < -1e-4 ? ne * 5.0 + (-20.0 - lvl) / slope : 60000.0;
            if (m->t20 > 60000.0) m->t20 = 60000.0;
        }
    }
    m->dc = m->peak > 0 ? fabs(sum / n) / m->peak : 0;
    memset(pw, 0, sizeof pw);                  /* the spectrum: 2048-point Hann frames, hop 512, power summed */
    for (i = 0; i + KT_FFT <= n; i += KT_FFT / 4u) {
        for (j = 0; j < KT_FFT; j++) {
            re[j] = y[i + j] * (0.5 - 0.5 * cos(2.0 * M_PI * j / KT_FFT));
            im[j] = 0;
        }
        kt_fft(re, im, KT_FFT);
        for (j = 0; j <= KT_FFT / 2u; j++)
            pw[j] += re[j] * re[j] + im[j] * im[j];
    }
    for (j = 1; j <= KT_FFT / 2u; j++) {
        double hz = (double)j * KT_FS / KT_FFT;
        etot += pw[j];
        num += hz * pw[j];
        for (b = 0; b < KT_NB; b++)
            if (hz >= BAND_HZ[b] && hz < BAND_HZ[b + 1]) m->band[b] += pw[j];
        if (hz >= 100.0 && hz <= 16000.0) {
            lg += log(pw[j] + 1e-3);
            ar += pw[j];
            nb++;
        }
    }
    m->cent = etot > 0 ? num / etot : 0;
    m->flat = ar > 0 ? exp(lg / nb) / (ar / nb) : 0;
    for (b = 0; b < KT_NB; b++)
        m->band[b] = 10.0 * log10(m->band[b] / (etot + 1e-30) + 1e-4);   /* dB of the total, floor -40 */
    m->f[0] = log2(m->t40 < 5.0 ? 5.0 : m->t40 > 8000.0 ? 8000.0 : m->t40);   /* a doubling: 1 */
    m->f[1] = log2(m->cent + 1.0);              /* an octave: 1 */
    m->f[2] = m->flat * 3.0;
    m->f[3] = m->crest / 6.0;
    m->f[4] = log2(1.0 + m->tpk);
    for (b = 0; b < KT_NB; b++)
        m->f[5 + b] = m->band[b] / 10.0;        /* 10 dB: 1 */
}

static double kt_dist(const kt_feat_t *a, const kt_feat_t *b)
{
    double s = 0;
    int i;
    for (i = 0; i < KT_NF; i++)
        s += (a->f[i] - b->f[i]) * (a->f[i] - b->f[i]);
    return sqrt(s);
}

/* ---- WAV (mono, 16 bits) */
static void kt_wav(const char *name, const int32_t *y, uint32_t n)
{
    FILE *f = fopen(name, "wb");
    uint32_t i, bytes = n * 2u, v[] = {16, 0x00010001u, KT_FS, KT_FS * 2u, 0x00100002u};
    if (!f) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)(y[i] > 32767 ? 32767 : y[i] < -32767 ? -32767 : y[i]);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

/* a beat of two bars on the kit (both voices, as the sequencer plays them), the presets' knobs */
static uint32_t kt_beat(uint32_t k, int32_t *y)
{
    /* 32 sixteenths: a drum per step and its trick (the keys: 1 kick, 3 snare, 5 closed hat, 6 open hat) */
    static const uint8_t KICK[32] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0};
    static const uint8_t SNR[32] = {0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 4};
    static const uint8_t HAT[32] = {5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 6, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 7, 8, 9, 6, 0};
    static const uint8_t PER[32] = {0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 10, 0, 0, 0, 13, 0, 0, 14, 0, 0, 15, 0, 0, 0, 0, 16, 0};
    const kt_knob_t *kn = &REC[k];
    uint32_t step = KT_FS * 60u / (100u * 4u), n = 32u * step + KT_FS, i, j, s, tt;
    int32_t lp[4] = {0, 0, 0, 0}, ton = TON_A[clampi(kn->ton, 0, 15)];
    memset(drum, 0, sizeof drum);
    memset(drum_next, 0, sizeof drum_next);
    for (tt = 0; tt < 4u; tt++) {              /* four tracks, one per lane */
        int8_t *p = prj.snd[tt].p;
        memset(&prj.snd[tt], 0, sizeof prj.snd[tt]);
        prj.snd[tt].engine = E_KIT;
        p[P_KIT] = (int8_t)k; p[P_TUNE] = kn->tune; p[P_DECAY] = kn->decay; p[P_DRIVE] = kn->drive;
        p[P_KCRUSH] = kn->crush; p[P_KTON] = kn->ton;
    }
    for (i = 0; i < n; i += CHUNK) {
        if (i % step < CHUNK && i / step < 32u) {
            s = i / step;
            if (KICK[s]) drum_on(0, KICK[s] - 1u, 0, -1);
            if (SNR[s]) drum_on(1, SNR[s] - 1u, 0, -1);
            if (HAT[s]) drum_on(2, HAT[s] - 1u, 0, -1);
            if (PER[s]) drum_on(3, PER[s] - 1u, 0, -1);
        }
        for (j = 0; j < CHUNK; j++)
            y[i + j] = 0;
        for (tt = 0; tt < 4u; tt++) {
            int32_t raw[CHUNK];
            for (j = 0; j < CHUNK; j++)
                raw[j] = 0;
            drum_chunk(&drum[tt][0], raw);
            drum_chunk(&drum[tt][1], raw);
            for (j = 0; j < CHUNK; j++) {
                if (ton < 32767) { lp[tt] += ((raw[j] - lp[tt]) * ton) >> 15; raw[j] = lp[tt]; }
                y[i + j] += (raw[j] * (tt == 0u ? 18 : tt == 1u ? 16 : tt == 2u ? 11 : 13)) >> 5;   /* a mix */
            }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    static int32_t all[NDRUM * KT_HIT];
    static const kt_knob_t EXTREME[] = {{12, 8, 15, 7, 0}, {-12, -8, 15, 0, 15}, {12, -8, 0, 3, 15},
                                        {-12, 8, 8, 0, 7}, {0, 0, 15, 7, 15}};
    double D[NKIT][NKIT], dmin = 1e9, kmin = 1e9, wmin = 1e9, cost[NKIT][NDRUM], cmax = 0;
    uint32_t k, d, k2, i, e, kmin_a = 0, kmin_b = 0;
    char name[256];
    (void)argc; (void)argv;
    if (system("mkdir -p build/kits") != 0) { puts("cannot make build/kits"); return 1; }
    /* ---- every drum, the presets' knobs */
    for (k = 0; k < NKIT; k++) {
        printf("\nKIT %u  %-10s (%s / %s / %s)  DRIVE %d  CRUSH %d  TONE %d\n", k, KIT_EN[k], L_KITS[k], L_KITS[NKIT + k],
               L_KITS[2u * NKIT + k], REC[k].drive, REC[k].crush, REC[k].ton);
        printf("  key code   peak  dBFS  loud50 crest   t20ms   t40ms  centroid  flat   tpk   dc%%  stop_ms\n");
        for (d = 0; d < NDRUM; d++) {
            kt_feat_t *m = &F[k][d];
            m->stop = kt_render(k, d, &REC[k], FX_NONE, 0, kt_buf, KT_HIT, KT_MAX);
            memcpy(all + d * KT_HIT, kt_buf, KT_HIT * sizeof(int32_t));
            kt_measure(m, kt_buf, KT_HIT);
            printf("  %3u  %-3s  %6.0f %5.1f %6.1f %5.1f %6.0f%s %6.0f%s %8.0f  %4.2f %5.1f %5.2f  %7.0f\n", d + 1u,
                   KIT_CODE3[k][d], m->peak, 20.0 * log10(m->peak / 32768.0 + 1e-12), m->loud, m->crest, m->t20,
                   m->x20 ? "~" : " ", m->t40, m->x40 ? "~" : " ", m->cent, m->flat, m->tpk, m->dc * 100.0,
                   m->stop * 1000.0 / KT_FS);
            for (i = 0; i < KT_NF; i++)
                if (m->f[i] != m->f[i]) kt_bad("NaN in its measures", k, d);
            if (m->clip) kt_bad("hard clip (samples at the ceiling or flat tops)", k, d);
            if (m->peak < 3000.0) kt_bad("silent (peak under 3000)", k, d);
            if (m->peak < 9000.0 || m->loud < -32.0) kt_bad("too quiet next to the others", k, d);
            if (m->dc > 0.02) kt_bad("DC (mean over 2 % of the peak)", k, d);
            if (!m->stop) kt_bad("the voice never stops", k, d);
        }
        snprintf(name, sizeof name, "build/kits/kit_%u.wav", k);
        kt_wav(name, all, NDRUM * KT_HIT);
        kt_wav((snprintf(name, sizeof name, "build/kits/beat_%u.wav", k), name), all, kt_beat(k, all));
    }
    /* ---- the kits, compared */
    printf("\nDISTANCE per key between two kits (feature vectors; 0 = the same drum)\n  key   min  (pair)    mean\n");
    for (d = 0; d < NDRUM; d++) {
        double mn = 1e9, sum = 0;
        uint32_t a = 0, b = 0, np = 0;
        for (k = 0; k < NKIT; k++)
            for (k2 = k + 1u; k2 < NKIT; k2++) {
                double x = kt_dist(&F[k][d], &F[k2][d]);
                sum += x;
                np++;
                if (x < mn) { mn = x; a = k; b = k2; }
            }
        printf("  %3u %5.2f  (%u-%u %s/%s)  %5.2f\n", d + 1u, mn, a, b, KIT_CODE3[a][d], KIT_CODE3[b][d], sum / np);
        if (mn < dmin) dmin = mn;
        if (mn < 0.6) {
            printf("FAIL  key %u: kits %u and %u play nearly the same drum (%.2f < 0.6)\n", d + 1u, a, b, mn);
            kt_fail = 1;
        }
    }
    printf("\nKIT MATRIX: mean distance over the 16 keys\n       ");
    for (k = 0; k < NKIT; k++)
        printf("  %-6.6s", KIT_EN[k]);
    printf("\n");
    for (k = 0; k < NKIT; k++) {
        printf("  %-5.5s", KIT_EN[k]);
        for (k2 = 0; k2 < NKIT; k2++) {
            double s = 0;
            for (d = 0; d < NDRUM; d++)
                s += kt_dist(&F[k][d], &F[k2][d]);
            D[k][k2] = s / NDRUM;
            printf("  %6.2f", D[k][k2]);
            if (k2 > k && D[k][k2] < kmin) { kmin = D[k][k2]; kmin_a = k; kmin_b = k2; }
        }
        printf("\n");
    }
    printf("  closest kits: %s and %s (%.2f)\n", KIT_EN[kmin_a], KIT_EN[kmin_b], kmin);
    if (kmin < 2.0) {
        printf("FAIL  kits %u and %u are too alike (mean %.2f < 2.0)\n", kmin_a, kmin_b, kmin);
        kt_fail = 1;
    }
    for (k = 0; k < NKIT; k++)                 /* inside a kit: no two keys the same */
        for (d = 0; d < NDRUM; d++)
            for (k2 = d + 1u; k2 < NDRUM; k2++) {
                double x = kt_dist(&F[k][d], &F[k][k2]);
                if (x < wmin) wmin = x;
                if (x < 0.5) {
                    printf("FAIL  kit %u: keys %u and %u are nearly the same (%.2f)\n", k, d + 1u, k2 + 1u, x);
                    kt_fail = 1;
                }
            }
    printf("  closest keys inside a kit: %.2f; closest same key across kits: %.2f\n", wmin, dmin);
    /* ---- the knobs at their ends, the tricks: no wrap-around, no DC, the hits still end */
    for (e = 0; e < sizeof EXTREME / sizeof EXTREME[0]; e++)
        for (k = 0; k < NKIT; k++)
            for (d = 0; d < NDRUM; d++) {
                kt_feat_t m;
                uint32_t stop = kt_render(k, d, &EXTREME[e], e == 2u ? FX_PITCH : e == 3u ? FX_CRUSH : FX_NONE,
                                          e == 2u ? 12 : e == 3u ? 5 : 0, kt_buf, KT_HIT, KT_MAX);
                int32_t mx = 0;
                for (i = 0; i < KT_HIT; i++)
                    mx = abs(kt_buf[i]) > mx ? abs(kt_buf[i]) : mx;
                kt_measure(&m, kt_buf, KT_HIT);
                if (mx > 32768) kt_bad("over full scale with the knobs at their ends", k, d);
                if (m.dc > 0.05 && mx > 2000) {
                    snprintf(name, sizeof name, "DC with the knobs at their ends (set %u: %.1f %%)", e, m.dc * 100.0);
                    kt_bad(name, k, d);
                }
                if (EXTREME[e].decay <= 0 && !stop) kt_bad("never stops with the knobs at their ends", k, d);
                for (i = 0; i < KT_NF; i++)
                    if (m.f[i] != m.f[i]) kt_bad("NaN with the knobs at their ends", k, d);
            }
    /* ---- CUT (drum_choke) and the same drum again: the old hit stops within 15 ms */
    for (k = 0; k < NKIT; k++)
        for (d = 0; d < NDRUM; d++) {
            const kt_knob_t *kn = &REC[k];
            uint32_t n;
            kt_render(k, d, kn, FX_NONE, 0, kt_buf, 0, KT_FS / 20u);   /* 50 ms in */
            if (drum[0][0].on) {
                drum_choke(&drum[0][0]);
                for (n = 0; n < KT_FS * 15u / 1000u && drum[0][0].on; n += CHUNK) {
                    int32_t raw[CHUNK] = {0};
                    drum_chunk(&drum[0][0], raw);
                }
                if (drum[0][0].on) kt_bad("CUT does not stop it within 15 ms", k, d);
            }
            kt_render(k, d, kn, FX_NONE, 0, kt_buf, 0, KT_FS / 20u);
            if (drum[0][0].on) {
                drum_on(0, d, 0, -1);     /* again: the first voice chokes */
                for (n = 0; n < KT_FS * 15u / 1000u && drum[0][0].on; n += CHUNK) {
                    int32_t raw[CHUNK] = {0};
                    drum_chunk(&drum[0][0], raw);
                    drum_chunk(&drum[0][1], raw);
                }
                if (drum[0][0].on) kt_bad("the same drum again does not choke the last hit", k, d);
            }
        }
    /* ---- the cost on this computer (relative: the worst drums) */
    for (k = 0; k < NKIT; k++)
        for (d = 0; d < NDRUM; d++) {
            clock_t c0 = clock();
            for (i = 0; i < 40u; i++)
                kt_render(k, d, &REC[k], FX_NONE, 0, kt_buf, KT_FS / 4u, KT_FS / 4u);
            cost[k][d] = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e9 / (40.0 * KT_FS / 4.0);
            if (cost[k][d] > cmax) cmax = cost[k][d];
        }
    printf("\nCOST on this computer (ns a frame, the hit's first 250 ms, both voices' calls; the 5 worst)\n");
    for (i = 0; i < 5u; i++) {
        double best = -1;
        uint32_t bk = 0, bd = 0;
        for (k = 0; k < NKIT; k++)
            for (d = 0; d < NDRUM; d++)
                if (cost[k][d] > best) { best = cost[k][d]; bk = k; bd = d; }
        printf("  %-10s key %2u %-3s %6.1f ns\n", KIT_EN[bk], bd + 1u, KIT_CODE3[bk][bd], best);
        cost[bk][bd] = -1;
    }
    printf("\nWAV: build/kits/kit_<n>.wav (the 16 drums, 0.7 s each), build/kits/beat_<n>.wav (two bars at 100 BPM)\n");
    printf("%s\n", kt_fail ? "kits_test: FAILED" : "kits_test: passed");
    return kt_fail;
}
