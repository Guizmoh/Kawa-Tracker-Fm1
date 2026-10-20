/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: the sound. Integers only, no 64-bit division (the chip has no FPU).
 *
 *   core 1   the voices: two oscillators (8 engines each), sub, noise, the filter, three envelopes, the
 *            two LFOs and the matrix, at a control rate of KS_CB samples      -> vbus (mono)
 *   core 0   the audio interrupt: drive, crush, chorus, delay, reverb, glue, the limiter -> L R
 *
 * With the second core running (KS_DUAL, fm1_cpu1_start) core 1 renders the next block while core 0 puts the
 * effects on this one (one block of latency, 5.8 ms). Without it (the emulator, or until the start of the
 * second core is checked on the FM-1) core 0 does both, one after the other: the same code, the same sound. */
#define KS_CB 16u                               /* samples per control block */
#define NVOICE 4u
#define CMB_N 1024u                             /* comb filter line per voice */

#if KS_DUAL
#define KS_SYNC() fm1_csync()                   /* (what one core wrote, the other sees: fm1_cpu1.h) */
#else
#define KS_SYNC() RING_PUBLISH()
#endif
#define BLOCK_TICKS (HALF_FRAMES * (FM1_TICKS_PER_US * 1000000u / FS))   /* 5.8 ms in 24 MHz ticks */
#define BLOCK_TICKS100 (BLOCK_TICKS / 100u)
static volatile uint8_t ks_shed;                /* the load governor's step (0: the whole sound) */
static volatile uint32_t ks_late;               /* blocks cut short (never past the block: the watchdog) */

/* ---- what the main loop sends to the voices (one writer, one reader) */
enum { EV_ON, EV_OFF, EV_PANIC, EV_SYNC, EV_SEQ };   /* EV_SEQ: a 1 play (b 1: from the bar), 0 stop */
static volatile uint32_t evq[64], ev_w, ev_r;
static void ev_push(uint32_t t, uint32_t a, uint32_t b)
{
    if (ev_w - ev_r >= 64u) return;
    evq[ev_w % 64u] = t | a << 8 | b << 16;
    KS_SYNC();
    ev_w++;
}

/* ---- shared with the main loop */
static int8_t patch[NP_MAX];                    /* the sound (the main loop writes, the voices read) */
typedef struct { uint8_t note[16], fl[16], len, rate, swing, pad; } seqpat_t;   /* 36 B */
enum { SF_ON = 1, SF_ACC = 2, SF_SLIDE = 4 };
#define SQ_NPAT 16u
#define PF_MAGIC 0x5Bu
#define MC_MAGIC 0x4Du
static struct {                                 /* stored (the store's second sector): what plays, whatever the patch */
    uint8_t magic, sum, cur, ar_on, ar_mode, ar_rate, ar_oct, ar_gate;
    seqpat_t pat[SQ_NPAT];                      /* (sum: up to here) */
    uint8_t mc_magic, mc_dst[16];               /* MACROS: four targets each (a destination, an amount) */
    int8_t mc_amt[16];
} perf;
static volatile uint8_t mc_val[4];              /* the four macros now, 0..127 (K1..K4 in the MACROS window; not stored) */
/* FX LIVE (not stored, all off at the start): DJ filter (- low pass, + high pass) and its resonance, PUMP (the
 * sidechain dip on the tempo) and its rate, STUTTER (the last slice played again and again), TAPE (slower, to a stop) */
static volatile int8_t lfx_flt;
static volatile uint8_t lfx_fres = 40, lfx_pump, lfx_prate, lfx_stut, lfx_tape;
static volatile uint32_t ks_bpm10 = 1400;       /* tempo x 10 */
static volatile int32_t ks_wheel, ks_press, ks_bend;   /* Q15 */
static volatile uint32_t ks_beats;              /* Q24 beats, kept modulo 48 beats (every division fits) */
static volatile uint8_t ks_sounding;
static int16_t scope[512];                      /* the output, one sample in 4 (the PLAY page draws it) */
static volatile uint32_t scope_w;
static volatile uint32_t lfo_ph_show[2];        /* where the LFOs are (the LFO page's dot) */
static volatile uint8_t env_st_show[3];         /* the newest voice's envelopes: stage, level Q15 (the ENV dots) */
static volatile int32_t env_lv_show[3], cut_show; /* ... and its cutoff after the modulation (Q8 units) */
static volatile int32_t step_val;               /* the STEP effect: its level now (Q15), its step */
static volatile uint8_t step_pos;
static volatile uint8_t ks_straight;            /* LFO GRID: round divisions only */
static volatile int32_t gmod[ND];               /* the global destinations' modulation (Q8 units), once a block */
static volatile int32_t meter_pk, meter_gr;     /* the output's peak and the glue's gain (Q15), for MASTER */
#define BEATS_WRAP (48u << 24)

/* ---- helpers */
static inline int32_t sin32(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = KS_SIN[i], f = (int32_t)((ph >> 6) & 0xFFFFu);
    return a + (((KS_SIN[i + 1u] - a) * f) >> 16);
}
static inline int32_t mul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
static inline int32_t sat15(int32_t x)          /* soft clip: x - 4/27 x^3, flat beyond +-1.5 */
{
    int32_t x2;
    if (x > 49152) return 32767;
    if (x < -49152) return -32767;
    x2 = (x * (x >> 1)) >> 14;
    return x - mul15(mul15(x, x2), 4855);
}
static inline int32_t clip(int32_t x, int32_t m) { return x > m ? m : x < -m ? -m : x; }
static uint32_t ks_rng = 0x9E3779B9u;
static inline uint32_t rnd(void)
{
    ks_rng ^= ks_rng << 13;
    ks_rng ^= ks_rng >> 17;
    ks_rng ^= ks_rng << 5;
    return ks_rng;
}
static uint32_t pitch_inc(int32_t q8)           /* note in 1/256 semitone -> phase step */
{
    int32_t q6, d;
    uint32_t o;
    if (q8 < 0) q8 = 0;
    if (q8 > 131 * 256) q8 = 131 * 256;
    q6 = q8 >> 2;
    if (q6 >= 120 * 64) return KS_PITCH[q6 - 120 * 64];
    d = 120 * 64 - q6;
    o = ((uint32_t)d + 767u) / 768u;
    return KS_PITCH[q6 + (int32_t)o * 768 - 120 * 64] >> o;
}
static inline uint32_t blep_rcp(uint32_t inc) { return (1u << 30) / ((inc >> 12) | 1u); }
static inline int32_t blep(uint32_t t, uint32_t inc, uint32_t rcp)   /* the PolyBLEP of a step of -2 at t = 0 */
{
    int32_t x;
    if (t < inc) {
        x = 32768 - (int32_t)(((t >> 12) * rcp) >> 15);
        return -((x * x) >> 15);
    }
    if (-t < inc) {
        x = 32768 - (int32_t)((((0u - t) >> 12) * rcp) >> 15);
        return (x * x) >> 15;
    }
    return 0;
}
static inline int32_t saw_bl(uint32_t ph, uint32_t inc, uint32_t rcp) { return (int32_t)(ph >> 16) - 32768 - blep(ph, inc, rcp); }
static inline int32_t sqr_bl(uint32_t ph, uint32_t pw, uint32_t inc, uint32_t rcp)   /* -1 then +1 from pw */
{
    return (ph < pw ? -32767 : 32767) - blep(ph, inc, rcp) + blep(ph - pw, inc, rcp);
}
static inline int32_t tri32(uint32_t ph) { return (int32_t)((ph ^ (uint32_t)((int32_t)ph >> 31)) >> 15) - 32768; }
static inline int32_t lfold(int32_t x)          /* a triangle fold of any x (Q15) back into +-1 */
{
    uint32_t f = (uint32_t)((x + 32768) * 2) & 0xFFFFu;
    int32_t d = (int32_t)f - 32768;
    return 32767 - 2 * (d < 0 ? -d : d);
}
static inline int32_t lvl15(int32_t v) { return v <= 0 ? 0 : v * v * 2; }   /* 0..127 -> Q15, squared */

/* ================================================================ the oscillators */
typedef struct {
    uint32_t ph[7], ph2, f[3];
    int32_t fb1, fb2;
} osc_t;
typedef struct {
    uint8_t eng, sync;                          /* sync: restart with the master (X MODE SYNC: B with A) */
    int32_t p1, p2, p3;                         /* 0..32767 after the modulation */
    uint32_t inc, mph, minc, mrq;               /* the master's phase and step; its step / ours, Q8 */
} oscp_t;
/* hard sync, in the loops that read ph: when the master turns, ph starts again where the master is in its turn */
#define SYNC_STEP() do { if (sy) { uint32_t m0 = mp; mp += q->minc; if (mp < m0) ph = (mp >> 8) * q->mrq - inc; } } while (0)

static const int32_t SUPER_DET[7] = {0, -7210, 7210, -4121, 4121, -1279, 1279};   /* JP-8000 spread, Q16 */
static const uint16_t VOX_F[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {270, 1850, 2600}, {450, 800, 2830}, {325, 700, 2530}};
static const uint16_t VOX_A[5][3] = {{32767, 16384, 8192}, {32767, 13107, 6554}, {32767, 9830, 6554}, {32767, 14746, 3277}, {32767, 8192, 2621}};

/* render n samples of one oscillator; pm (or 0): phase modulation per sample */
static void osc_render(osc_t *o, const oscp_t *q, int32_t *out, const int32_t *pm, uint32_t n)
{
    uint32_t i, inc = q->inc, rcp = blep_rcp(inc), ph = o->ph[0], mp = q->mph, sy = q->sync;
    switch (q->eng) {
    case E_ANALOG: {                            /* sine > triangle > saw > square, a width, a drive */
        int32_t w = q->p1 * 3, seg = w >> 15, k = w & 32767, g = 32768 + q->p3 * 7;
        uint32_t pw = (uint32_t)(16384 + ((q->p2 * 14745) >> 15)) << 17;
        if (seg > 2) { seg = 2; k = 32767; }
        for (i = 0; i < n; i++) {
            int32_t a, b, x;
            uint32_t p;
            SYNC_STEP();
            ph += inc;
            p = ph + (pm ? (uint32_t)pm[i] : 0u);
            switch (seg) {
            case 0: a = sin32(p - 0x40000000u); b = tri32(p); break;
            case 1: a = tri32(p); b = saw_bl(p, inc, rcp); break;
            default: a = saw_bl(p, inc, rcp); b = sqr_bl(p, pw, inc, rcp); break;
            }
            x = a + (((b - a) * k) >> 15);
            out[i] = q->p3 ? sat15(mul15(x, g) >> 0) : x;
        }
        break;
    }
    case E_SUPER: {                             /* 2, 3, 5 or 7 saws, detuned around the note */
        uint32_t nv = SUPER_N[q->p3 >> 13], cap = ks_shed >= 3u ? 2u : ks_shed >= 2u ? 3u : 7u, inc_k[7], rc[7], v0;
        if (nv > cap) nv = cap;
        v0 = nv == 2u ? 1u : 0u;
        int32_t s = (int32_t)(((int64_t)q->p1 * q->p1) >> 15), cg = 32767 - (q->p2 >> 1), sg = nv == 2u ? 32767 : 6000 + ((q->p2 * 26767) >> 15), norm;
        norm = nv == 2u ? 19000 : nv == 3u ? 15000 : nv == 5u ? 11500 : 9800;
        for (i = v0; i < v0 + nv; i++) {
            int32_t d = (int32_t)(((int64_t)SUPER_DET[i] * s) >> 15);
            inc_k[i] = inc + (uint32_t)(((int64_t)inc * d) >> 16);
            rc[i] = blep_rcp(inc_k[i]);
        }
        for (i = 0; i < n; i++) {
            int32_t acc = 0;
            uint32_t k;
            for (k = v0; k < v0 + nv; k++) {
                uint32_t p;
                o->ph[k] += inc_k[k];
                p = o->ph[k] + (pm ? (uint32_t)pm[i] : 0u);
                acc += mul15(saw_bl(p, inc_k[k], rc[k]), k == 0u ? cg : sg);
            }
            out[i] = mul15(acc, norm) << 1;
        }
        return;
    }
    case E_WAVE: {                              /* 8 frames, read with a warp of the phase */
        uint32_t pos = (uint32_t)q->p1 * 7u, fr = pos >> 15, fk = pos & 32767u, mip = inc > 34000000u;
        const int16_t *t0, *t1;
        uint32_t K = 0, r1 = 0, r2 = 0;
        if (fr > 6u) { fr = 6u; fk = 32767u; }
        t0 = KS_WT + (((mip * 8u + ((uint32_t)q->p2 >> 12)) * 8u + fr) << 8);
        t1 = t0 + 256;
        if (q->p3) {
            uint32_t k16 = 32768u - (((uint32_t)q->p3 * 29491u) >> 15);   /* the knee: 0.5 .. 0.05 */
            K = k16 << 16;
            r1 = (32768u << 16) / k16;
            r2 = (32768u << 16) / (65536u - k16);
        }
        for (i = 0; i < n; i++) {
            uint32_t p, ix, f;
            int32_t a, b;
            SYNC_STEP();
            ph += inc;
            p = ph + (pm ? (uint32_t)pm[i] : 0u);
            if (K) p = p < K ? (uint32_t)(((uint64_t)p * r1) >> 16) : 0x80000000u + (uint32_t)(((uint64_t)(p - K) * r2) >> 16);
            ix = p >> 24;
            f = (p >> 9) & 32767u;
            a = t0[ix] + (((t0[(ix + 1u) & 255u] - t0[ix]) * (int32_t)f) >> 15);
            b = t1[ix] + (((t1[(ix + 1u) & 255u] - t1[ix]) * (int32_t)f) >> 15);
            out[i] = a + (((b - a) * (int32_t)fk) >> 15);
        }
        break;
    }
    case E_FM: {                                /* a carrier, a modulator at a ratio, its feedback */
        int32_t ik = (int32_t)((((q->p1 * q->p1) >> 15) * 6366) >> 10), fk = (q->p3 * 978) >> 10;
        uint32_t im = (uint32_t)(((uint64_t)inc * RATIO_Q8[q->p2 >> 11]) >> 8), pm2 = o->ph2;
        for (i = 0; i < n; i++) {
            int32_t m;
            pm2 += im;
            m = sin32(pm2 + (uint32_t)((((o->fb1 + o->fb2) >> 1) * fk)));
            o->fb2 = o->fb1;
            o->fb1 = m;
            SYNC_STEP();
            ph += inc;
            out[i] = sin32(ph + (pm ? (uint32_t)pm[i] : 0u) + (uint32_t)((int64_t)m * ik));
        }
        o->ph2 = pm2;
        break;
    }
    case E_PHASE: {                             /* Casio CZ: a cosine read through a bent phase, or a windowed resonance */
        uint32_t ty = ((uint32_t)q->p2 * 6u) >> 15, k16, r1, r2, K, R;
        int32_t body = q->p3 >> 1;
        k16 = ty == 2u ? 65536u - (((uint32_t)q->p1 * 63000u) >> 15) : 32768u - (((uint32_t)q->p1 * 31500u) >> 15);
        K = ty == 2u && k16 >= 65536u ? 0xFFFFFFFFu : k16 << 16;
        r1 = ty == 2u ? 0xFFFFFFFFu / k16 : (32768u << 16) / k16;
        r2 = (32768u << 16) / (65536u - (k16 >= 65536u ? 65535u : k16));
        R = 256u + (((uint32_t)q->p1 * 3840u) >> 15);   /* the resonance ratio, Q8: 1 .. 16 */
        for (i = 0; i < n; i++) {
            uint32_t p, t2;
            int32_t x, w;
            ph += inc;
            p = ph + (pm ? (uint32_t)pm[i] : 0u);
            switch (ty) {
            case 0:
                t2 = p < K ? (uint32_t)(((uint64_t)p * r1) >> 16) : 0x80000000u + (uint32_t)(((uint64_t)(p - K) * r2) >> 16);
                x = -sin32(t2 + 0x40000000u);
                break;
            case 1: {
                uint32_t u = p & 0x7FFFFFFFu;
                t2 = u < K ? (uint32_t)(((uint64_t)u * r1) >> 16) : 0x80000000u;
                if (t2 > 0x80000000u) t2 = 0x80000000u;
                x = -sin32((p & 0x80000000u) + t2 + 0x40000000u);
                break;
            }
            case 2:
                t2 = p < K ? (uint32_t)(((uint64_t)p * r1) >> 16) : 0u;
                x = -sin32(t2 + 0x40000000u);
                break;
            default:
                w = ty == 3u ? 32767 - (int32_t)(p >> 17)
                  : ty == 4u ? (p < 0x80000000u ? (int32_t)(p >> 16) : (int32_t)((~p) >> 16)) : (p < 0x80000000u ? 32767 : (int32_t)((~p) >> 16));
                x = -mul15(sin32((uint32_t)(((uint64_t)p * R) >> 8) + 0x40000000u), w);
                break;
            }
            out[i] = x + mul15(sin32(p) - x, body);
        }
        break;
    }
    case E_FOLD: {                              /* a sine (or triangle) driven into a folder, with a bias */
        int32_t g = 4096 + ((q->p1 * 9) >> 3), bias = (q->p2 - 16384) >> 1, tk = q->p3;
        for (i = 0; i < n; i++) {
            int32_t s;
            uint32_t p;
            ph += inc;
            p = ph + (pm ? (uint32_t)pm[i] : 0u);
            s = sin32(p);
            s += mul15(tri32(p + 0x40000000u) - s, tk);
            out[i] = clip(lfold(((s * g) >> 12) + bias), 32767);
        }
        break;
    }
    case E_SYNC: {                              /* a slave at 1..8 x reset by the note's phase */
        uint32_t R = 4096u + (((uint32_t)q->p1 * 7u) >> 3), is = (uint32_t)(((uint64_t)inc * R) >> 12), rs = blep_rcp(is), ps = o->ph2;
        int32_t k = q->p2, soft = q->p3;
        for (i = 0; i < n; i++) {
            uint32_t old = ph;
            int32_t a, b, x;
            ph += inc + (pm ? (uint32_t)pm[i] : 0u);
            ps += is;
            if (ph < old) ps = (uint32_t)(((uint64_t)ph * R) >> 12);
            a = saw_bl(ps, is, rs);
            b = ps < 0x80000000u ? -32767 : 32767;
            x = a + mul15(b - a, k);
            out[i] = x - mul15(x, mul15((int32_t)(ph >> 17), soft));
        }
        o->ph2 = ps;
        break;
    }
    default: {                                  /* VOCAL: three formant bursts reset at every period */
        uint32_t v = (uint32_t)q->p1 * 4u, vi = v >> 15, vk = v & 32767u, fi[3], k;
        int32_t a[3], sh = 2048 + ((q->p2 * 6144) >> 15), wd = q->p3;
        if (vi > 3u) { vi = 3u; vk = 32767u; }
        for (k = 0; k < 3u; k++) {
            int32_t f = VOX_F[vi][k] + ((((int32_t)VOX_F[vi + 1u][k] - VOX_F[vi][k]) * (int32_t)vk) >> 15);
            f = (f * sh) >> 12;
            if (f > 6000) f = 6000;
            fi[k] = (uint32_t)f * 97391u;
            a[k] = VOX_A[vi][k] + ((((int32_t)VOX_A[vi + 1u][k] - VOX_A[vi][k]) * (int32_t)vk) >> 15);
        }
        for (i = 0; i < n; i++) {
            uint32_t old = ph;
            int32_t w, w2, s;
            ph += inc + (pm ? (uint32_t)pm[i] : 0u);
            if (ph < old) o->f[0] = o->f[1] = o->f[2] = 0;
            o->f[0] += fi[0]; o->f[1] += fi[1]; o->f[2] += fi[2];
            w = 32767 - (int32_t)(ph >> 17);
            w2 = mul15(w, w);
            w = w2 + mul15(mul15(w2, w2) - w2, wd);
            s = mul15(sin32(o->f[0]), a[0]) + mul15(sin32(o->f[1]), a[1]) + mul15(sin32(o->f[2]), a[2]);
            out[i] = clip(mul15(s, w) + (sin32(ph) >> 2), 40000);
        }
        break;
    }
    }
    o->ph[0] = ph;
}

/* ================================================================ envelopes, LFOs */
typedef struct {
    uint8_t st;                                 /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    int32_t lv;                                 /* Q30 */
} env_t;
static void env_gate(env_t *e, int on) { e->st = on ? 1u : (e->st ? 4u : 0u); }
static int32_t env_run(env_t *e, uint32_t a, uint32_t d, int32_t s, uint32_t r)   /* s: Q30; returns Q15 */
{
    switch (e->st) {
    case 1:
        e->lv += (int32_t)KS_ENV_ATK[a];
        if (e->lv >= (1 << 30)) { e->lv = 1 << 30; e->st = 2; }
        break;
    case 2:
        e->lv = s + (int32_t)(((int64_t)(e->lv - s) * KS_ENV_DEC[d]) >> 24);
        if (e->lv - s < (1 << 16) && e->lv - s > -(1 << 16)) { e->lv = s; e->st = 3; }
        break;
    case 3:
        e->lv = s;
        break;
    case 4:
        e->lv = (int32_t)(((int64_t)e->lv * KS_ENV_DEC[r]) >> 24);
        if (e->lv < (1 << 18)) { e->lv = 0; e->st = 0; }
        break;
    default:
        e->lv = 0;
    }
    return e->lv >> 15;
}

typedef struct {
    uint32_t ph, b0, last;
    int32_t out, sh, sh0;
} lfo_t;
static lfo_t lfo[2];
static uint32_t lfo_key[2];                     /* a note asked the LFOs to start again */
static int32_t lfo_shape(lfo_t *l, uint32_t ph, uint32_t shape)
{
    switch (shape) {
    case 0: return sin32(ph);
    case 1: return tri32(ph + 0x40000000u);
    case 2: return 32767 - (int32_t)(ph >> 16);
    case 3: return (int32_t)(ph >> 16) - 32768;
    case 4: return ph < 0x80000000u ? 32767 : -32767;
    case 5: return l->sh;
    case 6: {                                   /* WUB: open at the start of the cycle, closing with a curve */
        int32_t t = 32767 - (int32_t)(ph >> 17);
        return (mul15(t, t) << 1) - 32767;
    }
    case 7: return (int32_t)(ph >> 30) * 21845 - 32767;
    default: return l->sh0 + mul15(l->sh - l->sh0, (int32_t)(ph >> 17));
    }
}
static void lfo_run(uint32_t k, uint32_t beats)
{
    lfo_t *l = &lfo[k];
    const int8_t *p = patch + k * LFO_2;
    uint32_t ph, off = (uint32_t)(uint8_t)p[L1_PH] << 25;
    int32_t raw, smo = p[L1_SMO];
    if (lfo_key[k]) {
        lfo_key[k] = 0;
        if (p[L1_TRIG]) { l->b0 = beats; l->ph = 0; }
    }
    if (p[L1_SYNC]) {
        uint32_t b = beats - (p[L1_TRIG] ? l->b0 : 0u);
        if (b >= BEATS_WRAP) b += BEATS_WRAP;   /* (wrapped below the start) */
        int32_t dv = clampi(p[L1_DIV] + (gmod[D_L1RATE + k] >> 8) * 15 / 127, 0, 15);
        if (ks_straight) dv = DIV_NEAR[dv];
        ph = (uint32_t)((uint64_t)(b >> 8) * DIV_CPB[dv]);
        l->ph = ph;
    } else
        ph = l->ph += KS_LFO_INC[clampi(p[L1_RATE] + (gmod[D_L1RATE + k] >> 8), 0, 127)];
    ph += off;
    if (ph < l->last && l->last - ph > 0x40000000u) {   /* a new cycle: a new random value */
        l->sh0 = l->sh;
        l->sh = ((int32_t)(rnd() >> 17) - 16384) * 2;
    }
    l->last = ph;
    lfo_ph_show[k] = ph;
    raw = lfo_shape(l, ph, (uint32_t)p[L1_SHP]);
    l->out = smo ? l->out + mul15(raw - l->out, 32767 - ((smo * 255) >> 0)) : raw;
}

/* ================================================================ the voices */
typedef struct {
    uint8_t on, note, vel, gate, slide;         /* slide: the SEQ BASS glides into this note */
    int32_t pitch, target;                      /* Q8 semitones (glide) */
    osc_t oa, ob;
    uint32_t phs;
    env_t ea, ef, em;
    int32_t y1, y2, y3, y4, g, amp;             /* the ladder, its g (Q16), the amp (Q15), both smoothed */
    int32_t b1, l1, b2, l2, b3, l3;             /* the vowel filter */
    int16_t cmb[CMB_N];
    uint32_t cw;
    int32_t drift_a, drift_b, rand, ncol;
    uint32_t age;
} voice_t;
static voice_t vo[NVOICE];
static uint8_t held[16], nheld;                 /* the keys held, the last on top (mono) */
static uint32_t vage;
static uint8_t ks_slide;                        /* (SEQ BASS) the next note_on slides: no new attack, a glide */

static void voice_start(voice_t *v, uint32_t note, uint32_t vel, int legato)
{
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->target = (int32_t)note << 8;
    v->slide = ks_slide && v->on && v->gate;
    if (!v->on || (!patch[V_GLIDE] && !v->slide) || (patch[V_MODE] == VM_LEGATO && !legato && !v->slide))
        v->pitch = v->target;               /* (MONO and POLY glide always, LEGATO only between held keys) */
    v->age = ++vage;
    if (legato && (patch[V_MODE] == VM_LEGATO || v->slide) && v->on && v->gate) return;   /* legato: no new attack */
    if (patch[X_RESET] && (!v->on || !v->ea.lv)) {
        uint32_t k;
        for (k = 0; k < 7u; k++) v->oa.ph[k] = v->ob.ph[k] = k * 0x24924924u;
        v->oa.ph[0] = v->ob.ph[0] = 0;
        v->oa.ph2 = v->ob.ph2 = 0;
        v->phs = 0;
    }
    if (!v->on) { v->y1 = v->y2 = v->y3 = v->y4 = 0; v->b1 = v->l1 = v->b2 = v->l2 = v->b3 = v->l3 = 0; }
    v->on = 1;
    v->gate = 1;
    v->rand = ((int32_t)(rnd() >> 17) - 16384) * 2;
    env_gate(&v->ea, 1);
    env_gate(&v->ef, 1);
    env_gate(&v->em, 1);
    lfo_key[0] = lfo_key[1] = 1;
}
static void voice_release(voice_t *v)
{
    v->gate = 0;
    env_gate(&v->ea, 0);
    env_gate(&v->ef, 0);
    env_gate(&v->em, 0);
}

static void note_on(uint32_t n, uint32_t vel)
{
    uint32_t i, best = 0;
    if (patch[V_MODE] != VM_POLY) {
        int legato = nheld > 0;
        for (i = 0; i < nheld && held[i] != n; i++) {}
        if (i < nheld) { for (; i + 1u < nheld; i++) held[i] = held[i + 1u]; nheld--; }
        if (nheld < 16u) held[nheld++] = (uint8_t)n;
        for (i = 1; i < NVOICE; i++) if (vo[i].on) voice_release(&vo[i]);
        voice_start(&vo[0], n, vel, legato && vo[0].gate);
        return;
    }
    for (i = 0; i < NVOICE; i++) if (vo[i].on && vo[i].note == n) { best = i; goto go; }
    for (i = 0; i < NVOICE; i++) if (!vo[i].on) { best = i; goto go; }
    for (i = 1; i < NVOICE; i++) if (vo[i].age < vo[best].age) best = i;   /* the oldest gives way */
go:
    voice_start(&vo[best], n, vel, 0);
}
static void note_off(uint32_t n)
{
    uint32_t i;
    if (patch[V_MODE] != VM_POLY) {
        for (i = 0; i < nheld && held[i] != n; i++) {}
        if (i >= nheld) return;
        for (; i + 1u < nheld; i++) held[i] = held[i + 1u];
        nheld--;
        if (vo[0].note != n) return;
        if (nheld) {                            /* back to the key still held, gliding (legato) */
            vo[0].target = (int32_t)held[nheld - 1u] << 8;
            vo[0].note = held[nheld - 1u];
            if (!patch[V_GLIDE]) vo[0].pitch = vo[0].target;
        } else
            voice_release(&vo[0]);
        return;
    }
    for (i = 0; i < NVOICE; i++)
        if (vo[i].on && vo[i].gate && vo[i].note == n) voice_release(&vo[i]);
}

/* the modulation of one voice for one block: param units x 256 (or Q8 semitones for the pitch) */
static void route(int32_t *m, const int32_t *src);
static void voice_mods(voice_t *v, int32_t *m, int32_t fenv, int32_t menv)
{
    int32_t src[NSRC];
    src[SR_OFF] = 0;
    src[SR_LFO1] = lfo[0].out;
    src[SR_LFO2] = lfo[1].out;
    src[SR_FENV] = fenv;
    src[SR_MENV] = menv;
    src[SR_VEL] = v->vel * 258;
    src[SR_WHEEL] = ks_wheel;
    src[SR_PRESS] = ks_press;
    src[SR_NOTE] = clip(((int32_t)v->note - 60) * 512, 32767);
    src[SR_RAND] = v->rand;
    src[SR_STEP] = step_val;
    route(m, src);
    m[D_CUT] += ((patch[F_ENV] + (m[D_FENV] >> 8)) * fenv) >> 6;
    m[D_CUT] += (int32_t)((((int64_t)(v->pitch - 60 * 256) * 276) >> 8) * patch[F_KEY] >> 7);
    m[D_CUT] += (patch[F_VEL] * (v->vel - 64)) >> 1;
}
/* the matrix, the two LFOs, the STEP effect and the mod envelope into m (param units x 256, Q8 semitones) */
static void route(int32_t *m, const int32_t *src)
{
    int32_t wob;
    uint32_t k;
    for (k = 0; k < ND; k++) m[k] = 0;
    for (k = 0; k < 4u; k++) {                  /* the matrix, then the LFOs (their depth can be a destination) */
        const int8_t *s = patch + M1_SRC + 3u * k;
        if (s[0] && s[1]) {
            int32_t a = s[2];
            if (IS_PITCH(s[1])) m[s[1]] += (int32_t)(((int64_t)(a < 0 ? -a * a : a * a) * 3 * src[s[0]]) >> 15);
            else m[s[1]] += (a * src[s[0]]) >> 6;
        }
    }
    wob = m[D_WOB] >> 8;
    for (k = 0; k < 2u; k++) {
        const int8_t *p = patch + k * LFO_2;
        int32_t d = clampi(p[L1_DEP] + (k == 0u ? wob : m[D_L2DEP] >> 8), -64, 64), dst = p[L1_DST];   /* (LFO 1 can move LFO 2) */
        if (!dst || !d) continue;
        if (IS_PITCH(dst)) m[dst] += (int32_t)(((int64_t)(d < 0 ? -d * d : d * d) * 3 * lfo[k].out) >> 15);
        else m[dst] += (d * lfo[k].out) >> 6;
    }
    if (patch[ST_ON] && patch[ST_DST] && patch[ST_DEP]) {   /* the STEP effect's own destination */
        int32_t a = patch[ST_DEP];
        if (IS_PITCH(patch[ST_DST])) m[patch[ST_DST]] += (int32_t)(((int64_t)(a < 0 ? -a * a : a * a) * 3 * step_val) >> 15);
        else m[patch[ST_DST]] += (a * step_val) >> 6;
    }
    if (patch[ME_DST] && patch[ME_AMT]) {
        int32_t a = patch[ME_AMT];
        if (IS_PITCH(patch[ME_DST])) m[patch[ME_DST]] += (int32_t)(((int64_t)(a < 0 ? -a * a : a * a) * 3 * src[SR_MENV]) >> 15);
        else m[patch[ME_DST]] += (a * src[SR_MENV]) >> 6;
    }
    if (perf.mc_magic == MC_MAGIC)              /* the MACROS: each knob onto its four targets */
        for (k = 0; k < 16u; k++) {
            int32_t mv = mc_val[k >> 2], a = perf.mc_amt[k], d = perf.mc_dst[k];
            if (!mv || !a || !d || d >= ND) continue;
            if (IS_PITCH(d)) m[d] += (int32_t)(((int64_t)(a < 0 ? -a * a : a * a) * 3 * (mv * 258)) >> 15);
            else m[d] += (a * mv * 258) >> 6;
        }
}
/* the global destinations (the effects, the rates, the gate): once a block, the newest note's envelopes */
static void fx_mods(void)
{
    int32_t src[NSRC], m[ND];
    uint32_t k;
    voice_t *v = 0;
    for (k = 0; k < NVOICE; k++) if (vo[k].on && vo[k].age == vage) v = &vo[k];
    src[SR_OFF] = 0;
    src[SR_LFO1] = lfo[0].out;
    src[SR_LFO2] = lfo[1].out;
    src[SR_FENV] = v ? v->ef.lv >> 15 : 0;
    src[SR_MENV] = v ? v->em.lv >> 15 : 0;
    src[SR_VEL] = v ? v->vel * 258 : 0;
    src[SR_WHEEL] = ks_wheel;
    src[SR_PRESS] = ks_press;
    src[SR_NOTE] = v ? clip(((int32_t)v->note - 60) * 512, 32767) : 0;
    src[SR_RAND] = v ? v->rand : 0;
    src[SR_STEP] = step_val;
    route(m, src);
    for (k = D_G0; k < ND; k++) gmod[k] = m[k];
}
static int32_t fpv(uint32_t id, uint32_t d)     /* a parameter of the effects, modulated */
{
    return clampi(patch[id] + (gmod[d] >> 8), PD[id].lo, PD[id].hi);
}
static inline int32_t pv(int32_t base, int32_t mod) { return clampi(base * 256 + mod, 0, 127 * 256); }   /* -> Q8 units */
static inline int32_t q8to15(int32_t q8) { return q8 + (q8 >> 7); }

static const int8_t FT_MIX[5][5] = {{0, 0, 0, 0, 1}, {0, 0, 1, 0, 0}, {0, 2, -2, 0, 0}, {1, -4, 6, -4, 1}, {1, -2, 2, 0, 0}};

/* one voice, one control block, added into acc */
static void voice_block(voice_t *v, int32_t *acc)
{
    int32_t m[ND], fenv, menv, amp, p, bend, bufa[KS_CB], bufb[KS_CB], pmb[KS_CB], s[KS_CB], cut, res, drv, la, lb, lsub, ln, i;
    oscp_t qa, qb;
    const int8_t *P = patch;
    uint32_t inc_sub, rsub, ft = (uint32_t)P[F_TYPE];
    int32_t amp_t, g_t, dg, da, gain;

    fenv = env_run(&v->ef, (uint32_t)P[FE_A], (uint32_t)P[FE_D], P[FE_S] << 23, (uint32_t)P[FE_R]);
    menv = env_run(&v->em, (uint32_t)P[ME_A], (uint32_t)P[ME_D], 0, (uint32_t)P[ME_D]);
    amp = env_run(&v->ea, (uint32_t)P[AE_A], (uint32_t)P[AE_D], P[AE_S] << 23, (uint32_t)P[AE_R]);
    if (!v->ea.st && !v->ea.lv) { v->on = 0; v->amp = 0; return; }
    if (P[V_GLIDE] || v->slide)                 /* (a SEQ slide without GLIDE: ~60 ms) */
        v->pitch = v->target + (int32_t)(((int64_t)(v->pitch - v->target) * KS_ENV_DEC[(P[V_GLIDE] ? P[V_GLIDE] : 34) >> 1]) >> 24);
    else v->pitch = v->target;
    voice_mods(v, m, fenv, menv);
    if (P[X_DRIFT]) {                           /* slow wandering of each oscillator */
        v->drift_a = clip(v->drift_a + ((int32_t)(rnd() >> 24) - 128) - (v->drift_a >> 6), 4000);
        v->drift_b = clip(v->drift_b + ((int32_t)(rnd() >> 24) - 128) - (v->drift_b >> 6), 4000);
    }
    bend = (int32_t)(((int64_t)ks_bend * P[V_BEND]) >> 7);
    p = v->pitch + m[D_PITCH] + bend;

    qa.eng = (uint8_t)P[A_ENG];
    qa.p1 = q8to15(pv(P[A_P1], m[D_AP1]));
    qa.p2 = q8to15(pv(P[A_P2], m[D_AP2]));
    qa.p3 = q8to15(pv(P[A_P3], m[D_AP3]));
    qa.inc = pitch_inc(p + m[D_APIT] + ((P[A_OCT] * 12 + P[A_SEMI]) << 8) + P[A_FINE] * 256 / 100 + ((v->drift_a * P[X_DRIFT]) >> 9));
    qb.eng = (uint8_t)P[B_ENG];
    qb.p1 = q8to15(pv(P[B_P1], m[D_BP1]));
    qb.p2 = q8to15(pv(P[B_P2], m[D_BP2]));
    qb.p3 = q8to15(pv(P[B_P3], m[D_BP3]));
    qb.inc = pitch_inc(p + m[D_BPIT] + ((P[B_OCT] * 12 + P[B_SEMI]) << 8) + P[B_FINE] * 256 / 100 + ((v->drift_b * P[X_DRIFT]) >> 9)
                       + (P[X_MODE] == 3 ? (pv(P[X_AMT], m[D_XMOD]) >> 8) * 6144 / 127 : 0));   /* (SYNC: X-MOD = +0..24 semitones) */
    qa.sync = 0;
    qb.sync = P[X_MODE] == 3;
    if (qb.sync) { qb.mph = v->oa.ph[0]; qb.minc = qa.inc; qb.mrq = qb.inc / ((qa.inc >> 8) + 1u); }
    inc_sub = pitch_inc(p - (2 - P[S_OCT]) * 12 * 256);
    la = lvl15(pv(P[A_LVL], m[D_ALVL]) >> 8);
    lb = lvl15(pv(P[B_LVL], m[D_BLVL]) >> 8);
    lsub = lvl15(pv(P[S_LVL], m[D_SUB]) >> 8);
    ln = lvl15(pv(P[N_LVL], m[D_NOISE]) >> 8);

    /* the oscillators: B first (it can modulate A) */
    if (lb || P[X_MODE]) osc_render(&v->ob, &qb, bufb, 0, KS_CB);
    else for (i = 0; i < (int32_t)KS_CB; i++) bufb[i] = 0;
    if (P[X_MODE] == 2) {
        int32_t xk = (pv(P[X_AMT], m[D_XMOD]) >> 8) * 650;
        for (i = 0; i < (int32_t)KS_CB; i++) pmb[i] = (int32_t)(uint32_t)((int64_t)bufb[i] * xk);
    }
    if (la || P[X_MODE] == 1) osc_render(&v->oa, &qa, bufa, P[X_MODE] == 2 ? pmb : 0, KS_CB);
    else {
        for (i = 0; i < (int32_t)KS_CB; i++) bufa[i] = 0;
        if (qb.sync) v->oa.ph[0] += qa.inc * KS_CB;   /* (A silent still leads B) */
    }
    {
        int32_t ncoef = 32767 - (pv(P[N_COL], m[D_NCOL]) >> 8) * 250, rm = P[X_MODE] == 1;
        for (i = 0; i < (int32_t)KS_CB; i++) {
            int32_t x = mul15(bufa[i], la), b = rm ? mul15(bufa[i], bufb[i]) : bufb[i];
            x += mul15(b, lb);
            if (ln) {
                v->ncol += mul15((int32_t)(rnd() >> 17) - 16384 - v->ncol, ncoef);
                x += mul15(v->ncol << 1, ln);
            }
            s[i] = x >> 1;
        }
    }

    /* the filter */
    cut = pv(P[F_CUT], m[D_CUT]);
    if (v->age == vage) {                      /* (the newest voice: what the screen shows) */
        env_st_show[0] = v->ea.st; env_lv_show[0] = v->ea.lv >> 15;
        env_st_show[1] = v->ef.st; env_lv_show[1] = v->ef.lv >> 15;
        env_st_show[2] = v->em.st; env_lv_show[2] = v->em.lv >> 15;
        cut_show = cut;
    }
    res = pv(P[F_RES], m[D_RES]) >> 8;
    drv = pv(P[F_DRV], m[D_DRV]) >> 8;
    gain = 4096 + drv * 160;
    if (ft <= FT_NOTCH) {
        const int8_t *c = FT_MIX[ft];
        int32_t k = res * 125, comp = ft <= FT_LP12 ? 4096 + (k >> 1) : 4096, x0 = cut >> 7;
        g_t = KS_CUT_G[x0] + (((KS_CUT_G[x0 + 1] - KS_CUT_G[x0]) * (cut & 127)) >> 7);
        if (!v->g) v->g = g_t;
        dg = (g_t - v->g) / (int32_t)KS_CB;
        for (i = 0; i < (int32_t)KS_CB; i++) {
            int32_t x = (int32_t)(((int64_t)s[i] * gain) >> 12), u;   /* (64-bit: the macros can push the mix past 2^17) */
            if (drv) x = sat15(x);
            v->g += dg;
            u = sat15(((x * comp) >> 12) - (int32_t)(((int64_t)v->y4 * k) >> 12));
            v->y1 += (int32_t)(((int64_t)(u - v->y1) * v->g) >> 16);
            v->y2 += (int32_t)(((int64_t)(v->y1 - v->y2) * v->g) >> 16);
            v->y3 += (int32_t)(((int64_t)(v->y2 - v->y3) * v->g) >> 16);
            v->y4 += (int32_t)(((int64_t)(v->y3 - v->y4) * v->g) >> 16);
            s[i] = (c[0] * u + c[1] * v->y1 + c[2] * v->y2 + c[3] * v->y3 + c[4] * v->y4) << 1;
        }
        v->g = g_t;
    } else if (ft == FT_VOWEL) {                /* two formants and a third, the cutoff walks A E I O U */
        uint32_t w = (uint32_t)q8to15(cut) * 4u, vi = w >> 15, vk = w & 32767u, k;
        int32_t f[3], a[3], q = 32768 - res * 230;
        if (vi > 3u) { vi = 3u; vk = 32767u; }
        for (k = 0; k < 3u; k++) {
            int32_t hz = VOX_F[vi][k] + ((((int32_t)VOX_F[vi + 1u][k] - VOX_F[vi][k]) * (int32_t)vk) >> 15);
            f[k] = sin32((uint32_t)hz * 48696u) << 1;
            a[k] = VOX_A[vi][k] + ((((int32_t)VOX_A[vi + 1u][k] - VOX_A[vi][k]) * (int32_t)vk) >> 15);
        }
        for (i = 0; i < (int32_t)KS_CB; i++) {
            int32_t x = (int32_t)(((int64_t)s[i] * gain) >> 12), h;
            if (drv) x = sat15(x);
            h = x - v->l1 - mul15(v->b1, q); v->b1 = clip(v->b1 + mul15(f[0], h), 200000); v->l1 += mul15(f[0], v->b1);
            h = x - v->l2 - mul15(v->b2, q); v->b2 = clip(v->b2 + mul15(f[1], h), 200000); v->l2 += mul15(f[1], v->b2);
            h = x - v->l3 - mul15(v->b3, q); v->b3 = clip(v->b3 + mul15(f[2], h), 200000); v->l3 += mul15(f[2], v->b3);
            v->l1 = clip(v->l1, 200000);
            s[i] = (mul15(v->b1, a[0]) + mul15(v->b2, a[1]) + mul15(v->b3, a[2])) * 3 / 2 + (v->l1 >> 2);
        }
    } else {                                    /* COMB: a line tuned by the cutoff (with KEY TRACK: the note) */
        int32_t hz = KS_CUT_HZ[cut >> 7], L, fb = res * 254;
        if (hz < 44) hz = 44;
        L = (int32_t)((44100u << 8) / (uint32_t)hz);
        if (L > (int32_t)(CMB_N - 2u) << 8) L = (int32_t)(CMB_N - 2u) << 8;
        if (L < 2 << 8) L = 2 << 8;
        for (i = 0; i < (int32_t)KS_CB; i++) {
            int32_t x = (int32_t)(((int64_t)s[i] * gain) >> 12), d0, d1, y;
            uint32_t r = (v->cw - (uint32_t)(L >> 8)) & (CMB_N - 1u);
            if (drv) x = sat15(x);
            d0 = v->cmb[r];
            d1 = v->cmb[(r - 1u) & (CMB_N - 1u)];
            y = x + mul15(d0 + (((d1 - d0) * (L & 255)) >> 8), fb);
            y = sat15(y);
            v->cmb[v->cw & (CMB_N - 1u)] = (int16_t)y;
            v->cw++;
            s[i] = (x + y);
        }
    }

    /* the sub (direct or into the filter: here after it, so it stays clean), the amp */
    rsub = blep_rcp(inc_sub);
    amp_t = mul15(amp, 32767 - mul15(P[V_VEL] * 258, 32767 - v->vel * 258));
    amp_t = clip(amp_t + (m[D_AMP] >> 1), 32767);
    if (amp_t < 0) amp_t = 0;
    da = (amp_t - v->amp) / (int32_t)KS_CB;
    for (i = 0; i < (int32_t)KS_CB; i++) {
        int32_t x = s[i], sb;
        v->phs += inc_sub;
        if (lsub) {
            switch (P[S_WAVE]) {
            case 0: sb = sin32(v->phs); break;
            case 1: sb = tri32(v->phs + 0x40000000u); break;
            case 2: sb = sqr_bl(v->phs, 0x80000000u, inc_sub, rsub) * 3 / 4; break;
            default: sb = sqr_bl(v->phs, 0x40000000u, inc_sub, rsub) * 3 / 4; break;
            }
            x += mul15(sb, lsub);
        }
        v->amp += da;
        acc[i] += mul15(x, v->amp);
    }
    v->amp = amp_t;
}

/* the STEP effect: the pattern's level at this step of the beat grid, smoothed (once a control block) */
static void step_run(uint32_t beats)
{
    static int32_t sv;
    uint32_t spb = STEP_PB[clampi(patch[ST_RATE], 0, 5)], n = (uint32_t)(((uint64_t)(beats >> 8) * spb) >> 16), pos = n & 15u;
    int32_t raw, smo = patch[ST_SMO];
    char c;
    if (!patch[ST_ON]) { step_val = 32767; sv = 32767; return; }
    if (patch[ST_PAT] == 15) {                  /* RANDOM: a hash of the step and the bar */
        uint32_t h = (n >> 4) * 2654435761u ^ pos * 40503u;
        h ^= h >> 13;
        raw = (int32_t)(((h * 0x9E3779B1u) >> 28) & 15u) * 2184;
    } else {
        c = STEP_PAT[patch[ST_PAT] & 15][pos];
        raw = (c <= '9' ? c - '0' : c - 'A' + 10) * 2184;
    }
    sv = smo ? sv + mul15(raw - sv, 32767 - smo * 255) : raw;
    step_val = sv;
    step_pos = (uint8_t)pos;
}


/* ================================================================ SEQ BASS and ARP (the voices' core, at each KS_CB)
 * SEQ BASS: 16 patterns of up to 16 steps, each a note (C1 + 0..48), on or a rest, an accent, a slide into the next
 * (no new attack, a glide). While it plays the keys transpose it (C4 = as written) and do not sound.
 * ARP: the keys held (or HOLD) played one at a time on the tempo: UP, DOWN, UP-DN, RANDOM, ORDER, over 1..4
 * octaves. Both run on ks_beats: on the MIDI clock with the LFOs. */
static volatile uint8_t sq_play, sq_rec;        /* playing (the voices say), step record (the main loop says) */
static volatile uint8_t sq_show = 0xFF;         /* the step playing (the screen) */
static int32_t sq_tr;                           /* the keys' transposition */
static uint8_t sq_note = 0xFF, sq_slide, ar_held[16], ar_n, ar_was;
static uint32_t sq_tick = 0xFFFFFFFFu, sq_step, sq_gate, ar_i;
static void sq_off(void) { if (sq_note != 0xFF) note_off(sq_note); sq_note = 0xFF; sq_slide = 0; }
static void sq_sound(uint32_t n, uint32_t vel, int slide_in, uint32_t gate)   /* (a slide: the new one, then the old one off) */
{
    uint8_t old = sq_note;
    ks_slide = (uint8_t)slide_in;
    note_on(n, vel);
    ks_slide = 0;
    if (old != 0xFF && old != n) note_off(old);
    sq_note = (uint8_t)n;
    sq_gate = gate;
}
static void ar_add(uint32_t n) { uint32_t i; for (i = 0; i < ar_n; i++) if (ar_held[i] == n) return; if (ar_n < 16u) ar_held[ar_n++] = (uint8_t)n; }
static void ar_del(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < ar_n && ar_held[i] != n; i++) {}
    if (i >= ar_n) return;
    for (; i + 1u < ar_n; i++) ar_held[i] = ar_held[i + 1u];
    ar_n--;
}
static void seq_step_on(void)
{
    if (sq_play) {
        const seqpat_t *p = &perf.pat[perf.cur % SQ_NPAT];
        uint32_t len = p->len >= 1u && p->len <= 16u ? p->len : 16u, f;
        sq_step = (sq_step + 1u) % len;
        sq_show = (uint8_t)sq_step;
        f = p->fl[sq_step];
        if (f & SF_ON) {
            int slide = sq_slide;
            sq_sound((uint32_t)clampi(24 + p->note[sq_step] + sq_tr, 0, 127), f & SF_ACC ? 127u : 90u, slide, 0x800000u);
            sq_slide = (uint8_t)(f & SF_SLIDE);
        } else sq_off();
        return;
    }
    {                                           /* ARP */
        uint8_t so[16];
        uint32_t i, j, n = ar_n, oct = perf.ar_oct % 4u + 1u, tot = n * oct, k, nt;
        for (i = 0; i < n; i++) so[i] = ar_held[i];
        if (perf.ar_mode != 4u)                 /* (ORDER: as played; the others from the lowest) */
            for (i = 1; i < n; i++) for (j = i; j > 0 && so[j - 1u] > so[j]; j--) { uint8_t t = so[j]; so[j] = so[j - 1u]; so[j - 1u] = t; }
        switch (perf.ar_mode) {
        case 1: k = tot - 1u - ar_i % tot; break;
        case 2: { uint32_t per = tot > 1u ? 2u * tot - 2u : 1u, q = ar_i % per; k = q < tot ? q : per - q; break; }
        case 3: k = (rnd() >> 8) % tot; break;
        default: k = ar_i % tot; break;
        }
        ar_i++;
        nt = so[k % n] + 12u * (k / n);
        sq_show = (uint8_t)(k & 15u);
        sq_sound(nt > 127u ? 127u : nt, 100u, 0, (uint32_t)clampi(perf.ar_gate, 1, 10) * 0x199999u);
        sq_slide = perf.ar_gate >= 10u;
    }
}
static void seq_clock(uint32_t beats)
{
    const seqpat_t *p = &perf.pat[perf.cur % SQ_NPAT];
    uint32_t rate = sq_play ? p->rate : perf.ar_rate, sw = sq_play ? p->swing : 0u, tick, fr;
    uint64_t t = (uint64_t)beats * STEP_PB[rate % 6u];
    if (ar_was && !perf.ar_on) ar_n = 0;        /* (the ARP turned off: what it held is let go) */
    ar_was = perf.ar_on;
    if (!sq_play && !(perf.ar_on && ar_n)) {
        if (sq_note != 0xFF) sq_off();
        sq_tick = 0xFFFFFFFFu;
        sq_show = 0xFF;
        ar_i = 0;
        return;
    }
    tick = (uint32_t)(t >> 24);
    fr = (uint32_t)t & 0xFFFFFFu;
    if (sw && (tick & 1u)) {                    /* swing: the odd steps late by up to half a step */
        uint32_t s = (sw > 50u ? 50u : sw) * 167772u;
        if (fr < s) { tick--; fr += 0x1000000u; } else fr -= s;
    }
    if (tick != sq_tick) { sq_tick = tick; seq_step_on(); }
    if (sq_note != 0xFF && !sq_slide && fr >= sq_gate) { note_off(sq_note); sq_note = 0xFF; }
}

/* all the voices for n samples (core 1's job) -> dst, mono */
static void voices_render(int32_t *dst, uint32_t n, uint32_t t0, uint32_t budget)
{
    uint32_t i, k, binc, any = 0, lim = ks_shed >= 3u ? 1u : ks_shed >= 2u ? 2u : NVOICE, on = 0;
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % 64u], t = e & 255u, a = (e >> 8) & 255u, b = e >> 16;
        ev_r++;
        if (t == EV_ON) {
            if (sq_play && !sq_rec) sq_tr = (int32_t)a - 60;   /* (the SEQ plays: the keys transpose it) */
            else if (perf.ar_on && !sq_play) ar_add(a);
            else note_on(a, b);
        } else if (t == EV_OFF) {
            if (sq_play && !sq_rec) {}
            else if (perf.ar_on && !sq_play) ar_del(a);
            else note_off(a);
        } else if (t == EV_SEQ) {
            sq_off();
            sq_play = (uint8_t)a;
            sq_step = 0xFFFFFFFFu;
            sq_tick = 0xFFFFFFFFu;
            if (a && b) ks_beats = 0;           /* (the internal clock: from the bar, the LFOs too) */
        } else if (t == EV_PANIC) { for (k = 0; k < NVOICE; k++) { vo[k].on = 0; vo[k].ea.st = 0; vo[k].ea.lv = 0; } nheld = 0; ar_n = 0; sq_note = 0xFF; }
        else if (t == EV_SYNC) ks_beats = a ? b * 699051u : 0u;   /* (the MIDI clock: its tick, 1/24 beat) */
    }
    for (k = 0; k < NVOICE; k++) on += vo[k].on;
    while (on > lim) {                          /* the governor took voices away: the oldest go */
        uint32_t old = NVOICE;
        for (k = 0; k < NVOICE; k++) if (vo[k].on && (old == NVOICE || vo[k].age < vo[old].age)) old = k;
        vo[old].on = 0;
        vo[old].ea.st = 0;
        vo[old].ea.lv = 0;
        on--;
    }
    binc = (ks_bpm10 * 664858u) >> 16;          /* beats per block, Q24: bpm10 x 16 x 2^24 / (600 x 44100) */
    for (i = 0; i < n; i += KS_CB) {
        int32_t *acc = dst + i;
        if (i && fm1_ticks() - t0 > budget) {   /* out of time: the rest of the block stays silent */
            for (k = i; k < n; k++) dst[k] = 0;
            ks_late++;
            break;
        }
        ks_beats = (ks_beats + binc) % BEATS_WRAP;
        lfo_run(0, ks_beats);
        lfo_run(1, ks_beats);
        step_run(ks_beats);
        seq_clock(ks_beats);
        for (k = 0; k < KS_CB; k++) acc[k] = 0;
        for (k = 0; k < NVOICE; k++)
            if (vo[k].on) { voice_block(&vo[k], acc); any = 1; }
        if (patch[ST_ON] && fpv(ST_GATE, D_GATE)) {   /* the gate: the steps cut the sound, ramped over the block */
            static int32_t g0 = 32767;
            int32_t g1 = 32767 - (((32767 - step_val) * fpv(ST_GATE, D_GATE)) >> 7), dg = (g1 - g0) / (int32_t)KS_CB;
            for (k = 0; k < KS_CB; k++) { g0 += dg; acc[k] = mul15(acc[k], g0); }
            g0 = g1;
        }
    }
    ks_sounding = (uint8_t)any;
    fx_mods();
}

/* ================================================================ the effects (core 0) */
#define DL_N 32768u
#define CH_N 2048u
static int16_t dl_buf[2][DL_N] POOL;
#define LV_N 32768u                             /* FX LIVE: the last 0.74 s of the voices (STUTTER, TAPE) */
static int16_t lv_buf[LV_N] POOL;
static struct {
    uint32_t w, st_start, st_len, st_pos, tp_lag, tp_fade;
    uint8_t st_on, st_div, tp_on;
    int32_t dj_l[2], dj_b[2];
} lv;
static int16_t ch_buf[CH_N];
static const uint16_t RV_LEN[8] = {1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379}, AP_LEN[4] = {556, 441, 579, 464};
static int16_t rv_c[1116 + 1188 + 1277 + 1356 + 1139 + 1211 + 1300 + 1379] POOL;
static int16_t rv_a[556 + 441 + 579 + 464];
static struct {
    int32_t dco[2], dc_y, cr_hold, cr_n, hp_ch, dl_lp[2], dl_hp[2], dl_t, rv_lp[8], rv_hp, sub_lp, env, gain;
    uint32_t dl_w, ch_w, chph, rv_i[8], ap_i[4];
    int32_t cp_env, cp_g, ph_x[2][4], ph_y[2][4], ph_last[2], sp_hp, sp_lp;   /* compressor, phaser, the spring's band */
    uint32_t cp_n, phph, dl_wow;
} fx;
static volatile int32_t meter_cp = 32767;       /* the compressor's gain (the COMPRESSOR view) */
/* PUMP: the dip on the tempo, deepest at the beat, a curve back up (rate 0 1/4, 1 1/8, 2 1/2, 3 a bar); Q15 gain */
static int32_t pump_gain(uint32_t pos, uint32_t rate, int32_t depth)
{
    static const uint8_t SH[4] = {0, 1, 1, 2};
    uint32_t fr = (rate == 1u ? pos << 1 : rate ? pos >> SH[rate & 3u] : pos) & 0xFFFFFFu;
    int32_t u = 32767 - (int32_t)(fr >> 9), g = 32767 - mul15(mul15(u, u), depth * 258);
    if ((fr >> 16) < 5u) g = 32767 - (32767 - g) * (int32_t)(fr >> 8) / 1280;   /* (a 2 % slope in, no click) */
    return g;
}
/* the reverb's types: its combs' length (Q8), feedback and damping added; SPRING: a band into short combs */
static const uint16_t RV_SC[4] = {150, 256, 205, 86};
static const int16_t RV_FBA[4] = {-1800, 0, 1500, 4200}, RV_DA[4] = {-2000, 0, -7000, 3000};

static void fx_render(const int32_t *in, int32_t *out, uint32_t n)
{
    const int8_t *P = patch;
    uint32_t i, k;
    int32_t da = fpv(DR_AMT, D_DRAMT), cr = fpv(CR_RATE, D_CRRATE), chr = fpv(CH_RATE, D_CHRATE), mv = fpv(MS_VOL, D_VOL);
    int32_t dr_g = 256 + da * da * 3 / 4, dr_mix = fpv(DR_MIX, D_DRMIX) * 258, cr_mix = fpv(CR_MIX, D_CRMIX) * 258;
    int32_t bits = 16 - ((fpv(CR_BITS, D_CRBITS) * 14) >> 7), hold = 1 + ((cr * 31) >> 7);
    int32_t ch_rate = 4870 + chr * chr * 30, ch_dep = fpv(CH_DEP, D_CHDEP) * 2, ch_mix = fpv(CH_MIX, D_CHMIX) * 258;
    int32_t dl_fb = fpv(DL_FB, D_DLFB) * 250, dl_lpk = 3000 + fpv(DL_TONE, D_DLTONE) * 233, dl_mix = fpv(DL_MIX, D_DLMIX) * 258, dl_t;
    int32_t rv_fb = 22000 + fpv(RV_SIZE, D_RVSIZE) * 80, rv_d = fpv(RV_DAMP, D_RVDAMP) * 200, rv_mix = fpv(RV_MIX, D_RVMIX) * 258;
    int32_t vol = (mv * mv * 419) >> 10, boost = fpv(MS_SUB, D_SUBB) * 200, glue = fpv(MS_GLUE, D_GLUE);
    int32_t thr = 32767 - glue * 180, makeup = 4096 + glue * 24;
    int32_t dj_f = 0, dj_q = 0, dj_mix = 0;
    uint32_t pb = ks_beats, pinc = (ks_bpm10 * 41546u) >> 16;   /* (the beat, Q24, and its step a sample: PUMP) */
    int32_t cp_amt = fpv(CP_AMT, D_CPAMT), cp_thr = 32767 - cp_amt * 200, cp_r = 2 + cp_amt / 16, cp_mk = 32768 + cp_amt * 150;
    int32_t cp_atk = 32768 / (1 + P[CP_ATK] * 11), cp_rel = 1048576 / (441 + P[CP_REL] * 270), cp_pump = P[CP_ON] ? P[CP_PUMP] : 0;
    int32_t ph_dep = fpv(PH_DEP, D_PHDEP) * 160, ph_fb = P[PH_FB] * 200, ph_mix = fpv(PH_MIX, D_PHMIX) * 258;
    uint32_t ph_inc = KS_LFO_INC[fpv(PH_RATE, D_PHRATE)] / KS_CB, rvt = (uint32_t)P[RV_TYPE] & 3u, dlt = (uint32_t)P[DL_TYPE] & 3u;
    int32_t rv_fb2 = clampi(rv_fb + RV_FBA[rvt], 0, 31800), rv_d2 = clampi(rv_d + RV_DA[rvt], 0, 32000);
    {                                           /* the delay time from the tempo */
        uint32_t cpb = DIV_CPB[P[DL_TIME] & 15], spb = 26460000u / (ks_bpm10 | 1u), t = (spb << 8) / (cpb >> 8);
        dl_t = (int32_t)(t > DL_N - 4u ? DL_N - 4u : t) << 8;
        if (!fx.dl_t) fx.dl_t = dl_t;
    }
    {                                           /* the glue: a level each block, a gain to follow it */
        int32_t pk = 0, tg = 32767;
        for (i = 0; i < n; i++) { int32_t a = in[i] < 0 ? -in[i] : in[i]; if (a > pk) pk = a; }
        fx.env = pk > fx.env ? fx.env + ((pk - fx.env) >> 1) : fx.env - (fx.env >> 5);
        if (glue && fx.env > thr) tg = (((thr + ((fx.env - thr) >> 2)) >> 2) << 15) / (fx.env >> 2);
        if (!fx.gain) fx.gain = 32767;
        fx.gain += (tg - fx.gain) >> 2;
        meter_gr = fx.gain;
        meter_pk -= meter_pk >> 3;              /* (the meter falls back) */
    }
    {                                           /* FX LIVE: the STUTTER slice from the tempo */
        uint32_t spb = 26460000u / (ks_bpm10 | 1u), len = lfx_stut ? spb >> (lfx_stut - 1u) : 0u;
        if (len > LV_N - 256u) len = LV_N - 256u;
        if (lv.st_on && (!lfx_stut || lfx_stut != lv.st_div)) lv.st_on = 0;   /* (another length: from the same end) */
        if (lfx_stut && !lv.st_on) { lv.st_on = 1; lv.st_div = lfx_stut; lv.st_len = len < 64u ? 64u : len; lv.st_start = lv.w - lv.st_len; lv.st_pos = 0; }
    }
    if (lfx_flt || lfx_pump) {                  /* the DJ filter's coefficients and the PUMP's place on the beat */
        int32_t v = lfx_flt < 0 ? -lfx_flt : lfx_flt;
        dj_f = lfx_flt < 0 ? (64 - v) * (64 - v) * 6 + 360 : v * v * 6 + 120;
        if (dj_f > 24000) dj_f = 24000;
        dj_q = 46000 - lfx_fres * 280;
        dj_mix = v * 4096 > 32767 ? 32767 : v * 4096;
    }
    for (i = 0; i < n; i++) {
        int32_t x = in[i] * 7 / 8, L, R, w;
        if (lv.st_on) {                         /* STUTTER: the slice again, a short fade at its ends; nothing written */
            uint32_t q = lv.st_pos, e = lv.st_len - q, g = q < e ? q : e;
            int32_t y = lv_buf[(lv.st_start + q) & (LV_N - 1u)];
            x = g < 64u ? (y * (int32_t)g) >> 6 : y;
            if (++lv.st_pos >= lv.st_len) lv.st_pos = 0;
        } else {
            lv_buf[lv.w & (LV_N - 1u)] = (int16_t)sat15(x);
            lv.w++;
            if (lfx_tape || lv.tp_fade) {       /* TAPE: read slower and slower; let go, back to now through a fade */
                uint32_t sp = lfx_tape ? 65536u - (uint32_t)lfx_tape * 516u : 65536u, r, f, pos;
                int32_t a, b, y;
                if (!lv.tp_on && lfx_tape) { lv.tp_on = 1; lv.tp_lag = 0; }
                lv.tp_lag += 65536u - sp;       /* (how far behind now, Q16: it grows while slower) */
                if (lv.tp_lag > (LV_N - 512u) << 16) { lv.tp_lag = (LV_N - 512u) << 16; sp = 0; }   /* (as far as it keeps: stopped) */
                pos = (lv.w << 16) - lv.tp_lag - (2u << 16);
                r = pos >> 16; f = (pos >> 1) & 32767u;
                a = lv_buf[r & (LV_N - 1u)]; b = lv_buf[(r + 1u) & (LV_N - 1u)];
                y = a + (((b - a) * (int32_t)f) >> 15);
                if (sp < 16384u) y = (int32_t)(((int64_t)y * sp) >> 14);   /* (the stop fades out) */
                if (lfx_tape) x = y;
                else { x = y + (((x - y) * (int32_t)(256u - lv.tp_fade)) >> 8); lv.tp_fade--; }
            }
            if (!lfx_tape && lv.tp_on) { lv.tp_on = 0; lv.tp_fade = 256u; }
        }
        if (P[DR_ON]) {                         /* drive */
            int32_t y = (int32_t)(((int64_t)x * dr_g) >> 8);
            switch (P[DR_TYPE]) {
            case 0: y = sat15(y); break;
            case 1: y = sat15(y + (int32_t)(((int64_t)y * y) >> 17)); break;
            case 2: y = clip(y, 26000) * 5 / 4; break;
            default: y = lfold(y); break;
            }
            x += mul15(y - x, dr_mix);
            fx.dc_y += (x * 256 - fx.dc_y) >> 11;  /* (the tube's DC away) */
            x -= fx.dc_y >> 8;
        }
        if (P[CR_ON]) {                         /* crush: bits and rate */
            if (++fx.cr_n >= hold) {
                int32_t sh = 16 - bits;
                fx.cr_n = 0;
                fx.cr_hold = sh > 0 ? (clip(x, 32767) >> sh) * (1 << sh) : x;
            }
            x += mul15(fx.cr_hold - x, cr_mix);
        }
        if (P[CP_ON]) {                         /* the compressor: a peak follower, the gain every 16 samples, slid */
            int32_t a = x < 0 ? -x : x;
            if (a > 131071) a = 131071;
            if (a > fx.cp_env) fx.cp_env += (int32_t)(((int64_t)(a - fx.cp_env) * cp_atk) >> 15);
            else fx.cp_env -= (int32_t)(((int64_t)(fx.cp_env - a) * cp_rel) >> 20) + 1;
            if (fx.cp_env < 0) fx.cp_env = 0;
            if (!fx.cp_g) fx.cp_g = 32767;
            if (!(fx.cp_n++ & 15u)) {
                int32_t e = fx.cp_env > 1 ? fx.cp_env : 1, gt = 32767;
                if (e > cp_thr && e >= 4) gt = (((cp_thr + (e - cp_thr) / cp_r) >> 2) << 15) / (e >> 2);   /* (32-bit: e < 2^17) */
                fx.cp_g += (gt - fx.cp_g) >> 1;
                meter_cp = fx.cp_g;
            }
            x = (int32_t)(((int64_t)mul15(x, fx.cp_g) * cp_mk) >> 15);
        } else meter_cp = 32767;
        x = clip(x, 120000);
        L = R = x;
        if (P[CH_ON] && !ks_shed) {             /* chorus on what is above ~150 Hz (the sub stays mono) */
            int32_t hp, t0, t1;
            uint32_t r;
            fx.hp_ch += (x - fx.hp_ch) >> 6;
            hp = x - fx.hp_ch;
            ch_buf[fx.ch_w & (CH_N - 1u)] = (int16_t)clip(hp, 32767);
            fx.chph += (uint32_t)ch_rate;
            t0 = (441 << 8) + tri32(fx.chph) * ch_dep / 128;
            t1 = (441 << 8) + tri32(fx.chph + 0x80000000u) * ch_dep / 128;
            r = (fx.ch_w - (uint32_t)(t0 >> 8)) & (CH_N - 1u);
            w = ch_buf[r] + (((ch_buf[(r - 1u) & (CH_N - 1u)] - ch_buf[r]) * (t0 & 255)) >> 8);
            L += mul15(w, ch_mix);
            r = (fx.ch_w - (uint32_t)(t1 >> 8)) & (CH_N - 1u);
            w = ch_buf[r] + (((ch_buf[(r - 1u) & (CH_N - 1u)] - ch_buf[r]) * (t1 & 255)) >> 8);
            R += mul15(w, ch_mix);
            fx.ch_w++;
        }
        if (P[PH_ON] && !ks_shed) {             /* the phaser: four all-passes a side, swept by its LFO, a feedback */
            uint32_t c;
            fx.phph += ph_inc;
            for (c = 0; c < 2u; c++) {
                int32_t lfo = (tri32(fx.phph + (c ? 0x40000000u : 0u)) + 32768) >> 1;   /* (0..32767) */
                int32_t a = 4900 + mul15(lfo, ph_dep), y = clip(x + mul15(fx.ph_last[c], ph_fb), 120000);
                if (a > 31000) a = 31000;
                for (k = 0; k < 4u; k++) {      /* y = a (x - y1) + x1 */
                    int32_t o = fx.ph_x[c][k] + mul15(a, y - fx.ph_y[c][k]);
                    fx.ph_x[c][k] = y;
                    fx.ph_y[c][k] = o = clip(o, 150000);
                    y = o;
                }
                fx.ph_last[c] = y;
                if (c) R += mul15((y - x) >> 1, ph_mix); else L += mul15((y - x) >> 1, ph_mix);
            }
        }
        if (P[DL_ON]) {                         /* the delay: PING-PONG, TAPE (wow, saturated, darker), DIGITAL, DUB */
            int32_t o[2], t = fx.dl_t, lpk = dlt == 2u ? 32767 : dlt == 3u ? dl_lpk * 2 / 3 : dlt == 1u ? dl_lpk * 3 / 4 : dl_lpk;
            int ping = dlt == 0u || dlt == 3u;
            fx.dl_t += (dl_t - fx.dl_t) >> 11;
            if (dlt == 1u) {                    /* TAPE: wow, about 0.4 Hz, a little flutter on top */
                fx.dl_wow += 39000u;
                t += (int32_t)(((int64_t)sin32(fx.dl_wow) * (fx.dl_t >> 9)) >> 15) + (sin32(fx.dl_wow * 13u) >> 9);
                if (t < 512) t = 512;
            }
            for (k = 0; k < 2u; k++) {
                uint32_t r = (fx.dl_w - (uint32_t)(t >> 8)) & (DL_N - 1u);
                int32_t a = dl_buf[k][r], b = dl_buf[k][(r - 1u) & (DL_N - 1u)];
                o[k] = a + (((b - a) * (t & 255)) >> 8);
            }
            for (k = 0; k < 2u; k++) {
                int32_t fbk = mul15(o[ping ? k ^ 1u : k], dl_fb), ink = ping ? (k == 0u ? (x >> 1) : 0) : (x >> 1), w2;
                if (dlt == 3u) fbk += fbk >> 3;
                fx.dl_lp[k] += mul15(fbk - fx.dl_lp[k], lpk);
                fx.dl_hp[k] += (fx.dl_lp[k] - fx.dl_hp[k]) >> (dlt == 2u ? 12 : dlt == 3u ? 6 : 7);
                w2 = ink + fx.dl_lp[k] - fx.dl_hp[k];
                dl_buf[k][fx.dl_w & (DL_N - 1u)] = (int16_t)(dlt == 2u ? clip(w2, 32767) : sat15(dlt == 1u ? w2 + (w2 >> 2) : w2));
            }
            fx.dl_w++;
            L += mul15(o[0], dl_mix);
            R += mul15(o[1], dl_mix);
        }
        if (P[RV_ON] && !ks_shed) {             /* a small Freeverb: 4 combs and 2 all-passes a side */
            int32_t rin, acc[2] = {0, 0};
            uint32_t base = 0;
            fx.rv_hp += (x - fx.rv_hp) >> 5;
            rin = (x - fx.rv_hp) >> 3;
            if (rvt == 3u) {                    /* SPRING: a band (about 300 Hz .. 3 kHz) into the short combs */
                fx.sp_hp += (rin - fx.sp_hp) >> 4;
                fx.sp_lp += ((rin - fx.sp_hp) - fx.sp_lp) >> 1;
                rin = fx.sp_lp << 1;
            }
            for (k = 0; k < 8u; k++) {
                int16_t *c = rv_c + base;
                uint32_t len = (RV_LEN[k] * RV_SC[rvt]) >> 8;
                int32_t y;
                if (fx.rv_i[k] >= len) fx.rv_i[k] = 0;
                y = c[fx.rv_i[k]];
                fx.rv_lp[k] = y + mul15(fx.rv_lp[k] - y, rv_d2);
                c[fx.rv_i[k]] = (int16_t)clip(rin + mul15(fx.rv_lp[k], rv_fb2), 32767);
                if (++fx.rv_i[k] >= len) fx.rv_i[k] = 0;
                acc[k >> 2] += y;
                base += RV_LEN[k];
            }
            base = 0;
            for (k = 0; k < 4u; k++) {
                int16_t *a = rv_a + base;
                uint32_t len = (AP_LEN[k] * (rvt == 3u ? 128u : RV_SC[rvt])) >> 8;
                int32_t b, in2 = acc[k >> 1];
                if (fx.ap_i[k] >= len) fx.ap_i[k] = 0;
                b = a[fx.ap_i[k]];
                a[fx.ap_i[k]] = (int16_t)clip(in2 + (b >> 1), 32767);
                acc[k >> 1] = b - in2;
                if (++fx.ap_i[k] >= len) fx.ap_i[k] = 0;
                base += AP_LEN[k];
            }
            L += mul15(acc[0], rv_mix);
            R += mul15(acc[1], rv_mix);
        }
        if (boost) {                            /* sub boost (mono, both sides) */
            fx.sub_lp += (x * 256 - fx.sub_lp) >> 8;
            L += mul15(fx.sub_lp >> 8, boost);
            R += mul15(fx.sub_lp >> 8, boost);
        }
        if (lfx_flt) {                          /* the DJ filter: a state-variable pair, - low pass, + high pass */
            int32_t c[2] = {L, R};
            for (k = 0; k < 2u; k++) {
                int32_t lo, hi, o;
                lv.dj_l[k] += mul15(dj_f, lv.dj_b[k]);
                hi = clip(c[k], 120000) - lv.dj_l[k] - mul15(dj_q, lv.dj_b[k]);
                lv.dj_b[k] = clip(lv.dj_b[k] + mul15(dj_f, hi), 200000);
                lo = lv.dj_l[k] = clip(lv.dj_l[k], 200000);
                o = lfx_flt < 0 ? lo : hi;
                c[k] += mul15(o - c[k], dj_mix);
            }
            L = c[0]; R = c[1];
        }
        if (lfx_pump || cp_pump) {              /* PUMP (FX LIVE, the compressor's): the dip on the tempo */
            uint32_t pos = pb + pinc * i;
            int32_t g = 32767;
            if (lfx_pump) g = pump_gain(pos, lfx_prate, lfx_pump);
            if (cp_pump) g = mul15(g, pump_gain(pos, (uint32_t)P[CP_PRATE], cp_pump));
            L = mul15(L, g); R = mul15(R, g);
        }
        L = mul15(L, fx.gain);
        R = mul15(R, fx.gain);
        { int32_t a = L < 0 ? -L : L; if (a > meter_pk) meter_pk = a; }
        fx.dco[0] += (L * 256 - fx.dco[0]) >> 12;   /* (no DC out; Q8, so it settles at 0) */
        fx.dco[1] += (R * 256 - fx.dco[1]) >> 12;
        L -= fx.dco[0] >> 8;
        R -= fx.dco[1] >> 8;
        L = sat15((int32_t)(((int64_t)L * vol * makeup) >> 24));
        R = sat15((int32_t)(((int64_t)R * vol * makeup) >> 24));
        out[2u * i] += L;
        out[2u * i + 1u] += R;
        if (!(i & 3u)) scope[scope_w++ & 511u] = (int16_t)((L + R) >> 1);
    }
}

/* ================================================================ the two cores */
#ifndef KS_DUAL
#define KS_DUAL 0
#endif
static int32_t vbus[2][HALF_FRAMES], vzero[HALF_FRAMES];
static volatile uint32_t c1_req, c1_done, c1_park_req, c1_parked;
static volatile uint8_t dual_on;                /* the second core runs (fm1_cpu1_start said yes) */
static volatile uint8_t c1_failed;              /* core 1 stopped answering: back on one core (the UI saves it) */
static volatile uint32_t ks_load[2];            /* % of a block, per core (the SYSTEM page) */

/* the load governor (as KAWA's, after Felucca's shed_check): two blocks in a row over 70 % and the sound thins
 * out a step: 1 the reverb and the chorus, 2 two voices and 3 saws a SUPER, 3 one voice and 2 saws. Two seconds
 * under 45 %: a step back. The sound thins out instead of crackling, and the watchdog never sees a block that
 * does not end (voices_render stops at its budget) */
static void governor(uint32_t pct)
{
    static uint8_t hist;
    static uint32_t calm;
    hist = (uint8_t)(hist << 1 | (pct > 70u));
    if ((hist & 3u) == 3u && ks_shed < 3u) { ks_shed++; hist = 0; calm = 0; }
    if (pct < 45u) { if (++calm > 344u && ks_shed) { ks_shed--; calm = 0; } }   /* 344 blocks: 2 s */
    else calm = 0;
}

#if KS_DUAL
static void core1_main(void)                    /* core 1: the voices of the next block, again and again */
{
    for (;;) {
        uint32_t r, t0;
        if (c1_park_req) { FL_FAR(fm1_cpu1_park)(&c1_park_req, &c1_parked); continue; }   /* (the flash is busy: in RAM) */
        KS_SYNC();
        r = c1_req;
        if (r == c1_done) continue;
        t0 = fm1_ticks();
        voices_render(vbus[r & 1u], HALF_FRAMES, t0, BLOCK_TICKS * 9u / 10u);
        ks_load[1] = (fm1_ticks() - t0) / BLOCK_TICKS100;
        KS_SYNC();
        c1_done = r;
    }
}
/* the main loop, around a flash erase or write: core 1 waits in RAM (or, if it does not answer, stops) */
static void c1_pause(void)
{
    uint32_t t0 = fm1_ticks();
    if (!dual_on) return;
    c1_park_req = 1;
    KS_SYNC();
    while (!c1_parked && fm1_ticks() - t0 < 20000u * FM1_TICKS_PER_US) {}
    if (!c1_parked) { fm1_cpu1_stop(); dual_on = 0; c1_park_req = 0; }
}
static void c1_resume(void) { c1_park_req = 0; KS_SYNC(); }
static void app_flash_busy(void)                /* an update, UBOOT, a crash: core 1 stops for good */
{
    if (!dual_on) return;
    c1_pause();
    fm1_cpu1_stop();
    dual_on = 0;
    c1_park_req = 0;
}
#else
static void c1_pause(void) {}
static void c1_resume(void) {}
static void app_flash_busy(void) {}
static void fm1_cpu1_stop(void) {}
#endif

static void app_audio(int32_t *out, uint32_t n)
{
    static uint32_t miss;
    uint32_t t0 = fm1_ticks(), l1 = 0;
    if (dual_on && !c1_park_req) {              /* core 1 did this block's voices during the last one */
        uint32_t cur = c1_req;
        while (c1_done != cur && fm1_ticks() - t0 < BLOCK_TICKS / 2u) { KS_SYNC(); }
        t0 = fm1_ticks();
        if (c1_done == cur) {
            c1_req = cur + 1u;                  /* core 1 starts the next block now */
            KS_SYNC();
            fx_render(vbus[cur & 1u], out, n);
            miss = 0;
        } else if (++miss < 3u) {
            ks_late++;                          /* (core 1 late: a silent block, it goes on) */
            fx_render(vzero, out, n);
        } else {                                /* three blocks without core 1: it is stuck, one core from here */
            fm1_cpu1_stop();
            dual_on = 0;
            c1_failed = 1;
            miss = 0;
            voices_render(vbus[0], n, t0, BLOCK_TICKS * 4u / 10u);
            fx_render(vbus[0], out, n);
        }
        l1 = ks_load[1];
    } else if (dual_on)
        fx_render(vzero, out, n);               /* core 1 waits for the flash: silence, as on core 0 */
    else {
        voices_render(vbus[0], n, t0, BLOCK_TICKS * 7u / 10u);
        fx_render(vbus[0], out, n);
    }
    ks_load[0] = (fm1_ticks() - t0) / BLOCK_TICKS100;
    governor(ks_load[0] > l1 ? ks_load[0] : l1);
}
