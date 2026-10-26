/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the voices. Runs in the audio interrupt: integers only, no division per frame.
 *
 * Each of the eight tracks plays one engine (its sound picks it):
 *   PULSE   pulse waves 12 / 25 / 50 / 75 % or PWM, band-limited (polyBLEP), a detuned second oscillator
 *   WAVE    a 32-step 4-bit wave table (Game Boy style), stepped as the real thing
 *   NOISE   a 15-bit LFSR (NES style), LONG or METAL (93 steps), clocked by the note
 *   FM      six operators, 32 algorithms: msfa (Dexed's core) as Felucca ported it (fm6_core.c), two voices
 *   KIT     16 synthesised drums, two voices
 *   SAMPLE  a break (16 slices on the white keys) or a sample of the user's, sliced or played on the notes
 * Then, for every track: the ADSR envelope (ENV page), the track's effect (a filter, drive, crush,
 * decimate, ring, tremolo or wah, with its mix), the LFO (pitch, volume, the effect's A, pan or shape).
 * Control (envelopes, pitch, LFO) runs once per chunk of 16 frames; FM renders 32 frames at a time. */
#include "fm6_core.c"

static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9): f * 2^32 / 44100; lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static const uint32_t SEMI_Q16[25] = {          /* 2^(k/12), k = 0..24, Q16 */
    65536u, 69433u, 73562u, 77936u, 82570u, 87480u, 92682u, 98193u, 104032u, 110218u, 116772u, 123715u,
    131072u, 138866u, 147123u, 155872u, 165140u, 174959u, 185364u, 196386u, 208064u, 220436u, 233544u, 247431u, 262144u};
static const uint32_t SEMI_INV_Q16[25] = {      /* 2^(-k/12), Q16 */
    65536u, 61858u, 58386u, 55109u, 52016u, 49097u, 46341u, 43740u, 41285u, 38968u, 36781u, 34716u,
    32768u, 30929u, 29193u, 27554u, 26008u, 24548u, 23170u, 21870u, 20643u, 19484u, 18390u, 17358u, 16384u};
static uint32_t semi_ratio(int32_t k)           /* 2^(k/12), Q16, k = -36..+36 */
{
    k = clampi(k, -36, 36);
    if (k >= 0)
        return k > 24 ? SEMI_Q16[k - 12] << 1 : SEMI_Q16[k];
    k = -k;
    return k > 24 ? SEMI_INV_Q16[k - 12] >> 1 : SEMI_INV_Q16[k];
}

static uint32_t pitch_inc(int32_t p)            /* p: semitones Q8 (MIDI note * 256) -> phase step per frame */
{
    uint32_t semi, base;
    p = clampi(p, 0, 131 * 256 + 255);
    semi = (uint32_t)p >> 8;
    base = INC_C9[semi % 12u] >> (10u - semi / 12u);
    return (uint32_t)(((uint64_t)base * FINE[((uint32_t)p & 255u) >> 2]) >> 30);
}

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static int32_t blep(uint32_t t, uint32_t dt)    /* polyBLEP for a rising edge of height 2, Q15; t: phase since it */
{
    uint32_t tq = t >> 16, dq = dt >> 16;
    int32_t x;
    if (!dq)
        return 0;
    if (tq < dq) {
        x = (int32_t)((tq << 15) / dq);
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    if (tq > 65535u - dq) {
        x = -(int32_t)(((65536u - tq) << 15) / dq);
        return ((x * x) >> 15) + 2 * x + 32768;
    }
    return 0;
}

static int32_t sat(int32_t x)                   /* a soft knee from 16384, flat at 32767 */
{
    int32_t a = x < 0 ? -x : x;
    if (a > 16384) {
        a = 16384 + ((a - 16384) >> 1);
        if (a > 32767)
            a = 32767;
        x = x < 0 ? -a : a;
    }
    return x;
}

static int32_t crush_mask(int32_t c) { return c > 0 ? ~((1 << (2 * c)) - 1) : -1; }

/* what the live layer tells the voices (kawa_fx.c sets it once per block) */
static int32_t lv_transpose;                    /* semitones: OCTAVE +1 / -1 */
static uint8_t lv_uni;                          /* 1 unison, 2 unison an octave down */
static uint8_t lv_mute;                         /* tracks silenced, bit per track */
static uint8_t lv_mask = 0xFF;                  /* the tracks the live layer's pitch effects reach (the target's) */
static uint32_t lfo_inc[11];                    /* the LFO's rates per chunk, from the tempo (kawa_seq.c) */

/* ---- the effect of a track or of the master */
typedef struct {
    int32_t lp, bp, hold, tone;
    uint32_t n, ph;
} fxs_t;
static void fx_insert(fxs_t *s, int32_t type, int32_t a, int32_t b, int32_t mix, int32_t *x, uint32_t n)
{
    uint32_t i, rinc = 0;
    int32_t wet, f = 0, q = 0, m = clampi(mix, 0, 15) * 2185, bq;
    if (type <= FXT_OFF || type >= FXT_N || !m)
        return;
    a = clampi(a, 0, 15);
    b = clampi(b, 0, 15);
    bq = b * 2185;                              /* b / 15, Q15 (no division per frame) */
    if (type == FXT_RING)
        rinc = pitch_inc((40 + a * 5) * 256);
    if (type == FXT_LPF || type == FXT_HPF || type == FXT_BPF) {
        f = SVF_F[30 + a * 6];
        q = 30000 - b * 1800;
    } else if (type == FXT_WAH) {
        s->ph += lfo_inc[clampi(4 + a / 4, 0, 10)];
        f = SVF_F[clampi(64 + ((sine(s->ph) * (b + 4) * 2) >> 15), 30, 110)];
        q = 9000;
    }
    for (i = 0; i < n; i++) {
        int32_t d = x[i], y;
        switch (type) {
        case FXT_LPF: case FXT_HPF: case FXT_BPF: case FXT_WAH: {
            int32_t hp;
            s->lp += (int32_t)(((int64_t)f * s->bp) >> 15);   /* (64-bit products: a loud KIT or SAMPLE track */
            s->lp = clampi(s->lp, -400000, 400000);            /* would wrap in 32 and lock the filter) */
            hp = d - s->lp - (int32_t)(((int64_t)q * s->bp) >> 15);
            s->bp += (int32_t)(((int64_t)f * hp) >> 15);
            s->bp = clampi(s->bp, -400000, 400000);
            y = type == FXT_LPF ? s->lp : type == FXT_HPF ? hp : s->bp;
            break;
        }
        case FXT_DIST:
            y = sat(d * (1 + a * 2));
            s->tone += ((y - s->tone) * TON_A[b]) >> 15;
            y = (s->tone * 3) >> 2;
            break;
        case FXT_CRUSH:
            y = d & ~((1 << clampi(a, 0, 14)) - 1);
            break;
        case FXT_DECIM:
            if (++s->n > (uint32_t)a) { s->n = 0; s->hold = d; }
            y = s->hold;
            break;
        case FXT_RING:
            s->ph += rinc;
            y = (int32_t)(((int64_t)d * sine(s->ph)) >> 15);
            break;
        case FXT_TREM:
            if ((i & 15u) == 0u) s->ph += lfo_inc[clampi(4 + a / 4, 0, 10)] ;
            y = (int32_t)(((int64_t)d * (32767 - ((((32767 - sine(s->ph)) >> 1) * bq) >> 15))) >> 15);
            break;
        default:
            y = d;
            break;
        }
        wet = y;
        x[i] = d + (int32_t)(((int64_t)(wet - d) * m) >> 15);
    }
}

/* ---- every track's common state: the envelope, pitch, tricks, LFO, effect */
typedef struct {
    uint8_t gate, stage, note, vol, shape, crush, vib, arp_n, arp_i;
    int8_t arp_off[4];
    int32_t env;                                /* the ADSR, Q16 */
    int32_t amp;                                /* the gain now, Q15 (ramps over a chunk) */
    int32_t pitch, ptgt, glide, sweep, bend;    /* Q8 semitones */
    uint32_t vph, arp_cnt, arp_len;
    uint32_t ph, ph2, pwm, lfsr;                /* PULSE, WAVE, NOISE */
    int32_t lp, dc;
    uint32_t lfo_ph, lfo_rnd;
    int32_t lfo;                                /* the LFO now, -32767..32767 x depth / 15 */
    int32_t volq;                               /* the note's volume, Q16 (VSL slides it) */
    int8_t vslide, npan;                        /* VSL: volume a row; PAN: this note's pan */
    uint8_t tb_run, tb_i, tb_note;              /* the table: running, its step, the note it started from */
    int8_t tb_tr, tb_vsl;                       /* its step's semitones, its volume slide */
    uint32_t tb_cnt;                            /* frames into its step */
    fxs_t fx;
} trk_t;
static uint32_t vsl_k = 272u;                   /* a VSL unit per chunk: 65536 x CHUNK / the row's frames (seq_tempo) */
static trk_t tk[NTRK];

/* ---- FM: one track (fm_trk), two voices (the new note and the last one's release). The patch is the
 * project's (prj.fmp: its operators, edited in EDIT) through the track's knobs; the main loop builds it */
static fm6_note_t fmn[2];
static uint8_t fm_cur, fm_pos, fm_seen;
static int32_t fm_vpit[2];                     /* a voice's pitch once it releases (Q8 semitones) */
static int32_t fm_buf[CTL];
static uint8_t fm_patch[156];
static volatile uint8_t fm_gen;
static uint8_t fm_alg, fm_fb;
static fm6_lfo_t fm_lfo;
static int32_t fm_dt[6];

static void fm_build(uint32_t t)                /* main loop: the FM track's patch from its operators and knobs */
{
    const int8_t *p = prj.snd[t].p;
    uint8_t d[156];
    uint32_t k, car, alg;
    for (k = 0; k < 156u; k++)
        d[k] = prj.fmp[k];
    alg = p[P_FMALG] ? (uint32_t)(p[P_FMALG] - 1) : d[FP_ALG] & 31u;
    d[FP_ALG] = (uint8_t)(alg & 31u);
    car = fm6_carriers(alg & 31u);
    for (k = 0; k < 6u; k++) {
        uint8_t *op = d + k * FP_OP;
        uint32_t r;
        if ((car >> k) & 1u)
            continue;                           /* the modulators: ratio and envelope speed */
        op[FP_FC] = (uint8_t)clampi(op[FP_FC] + p[P_FMRAT], 0, 31);
        for (r = 0; r < 4u; r++)
            op[FP_R1 + r] = (uint8_t)clampi(op[FP_R1 + r] - p[P_FMMEG] * 6, 1, 99);
    }
    if (p[P_FMVIB]) {
        d[FP_LPMD] = (uint8_t)clampi(p[P_FMVIB] * 6, 0, 99);
        d[FP_LPMS] = 4;
        if (d[FP_LFS] < 30u) d[FP_LFS] = 34;
    }
    fm_alg = (uint8_t)(alg & 31u);
    fm_fb = (uint8_t)clampi((d[FP_FB] & 7) + p[P_FMFB], 0, 7);
    for (k = 0; k < 6u; k++)                    /* DETUNE: the carriers apart, up to ~25 cents */
        fm_dt[k] = ((car >> k) & 1u) ? (int32_t)((k & 1u) ? 1 : -1) * p[P_FMDET] * 23000 : 0;
    for (k = 0; k < 156u; k++)                  /* (byte by byte: the interrupt sees at worst one block half new) */
        fm_patch[k] = d[k];
    fm_trk = (uint8_t)t;
    fm_gen++;
}

#include "kawa_drums.c"

/* ---- SAMPLE: a break or a user sample, two voices so that a new note fades the last one out */
typedef struct {
    uint8_t on, rev, crush;
    const uint8_t *d;
    uint32_t pos, step, step0, lo, hi;          /* Q16, in the sample's frames; step0: without the table's pitch */
    int32_t g, gtgt, gstep, fade_at, rel, drive, gain;   /* gain: the edit's NORMALISE, Q8 */
} smpv_t;
static smpv_t smp[NTRK][2];
static uint8_t smp_next[NTRK];

static int smp_source(uint32_t src, const uint8_t **d, uint32_t *len, uint32_t *rate)
{
    if (src < BRK_N) {
        *d = BRK_DATA[src];
        *len = BRK_LEN;
        *rate = BRK_SR;
        return 1;
    }
    src -= BRK_N;
    if (src < NUSR && usr_data[src] && usr_hdr[src].len > 64u) {
        *d = usr_data[src];
        *len = usr_hdr[src].len;
        *rate = usr_hdr[src].rate;
        return 1;
    }
    return 0;
}

static int src_is_break(int32_t s) { return s >= 0 && s < (int32_t)BRK_N; }

static uint32_t sed_at(uint32_t x, uint32_t len) { return (uint32_t)(((uint64_t)x * len) >> 16); }   /* 65536ths */

static void smp_retune(smpv_t *v, int32_t tr)   /* the table's semitones over the note (no division) */
{
    v->step = tr ? (uint32_t)(((uint64_t)v->step0 * semi_ratio(tr)) >> 16) : v->step0;
}

static int32_t smp_extra;                       /* semitones added to the next smp_on (a black key on a slice track) */
static void smp_on(uint32_t t, uint32_t note, const tricks_t *k)
{
    const int8_t *p = prj.snd[t].p;
    const sedit_t *se = &prj.sed[t];
    smpv_t *v = &smp[t][smp_next[t]], *o = &smp[t][smp_next[t] ^ 1u];
    const uint8_t *d;
    uint32_t len, rate, s0, s1, a, b, ratio, q = DUR_Q[clampi(p[P_LEN], 0, 11)];
    int32_t sv, semis = p[P_STUNE] + ((lv_mask >> t & 1u) ? lv_transpose : 0) + (trick(k, FX_PITCH, &sv) ? sv : 0) + smp_extra;
    smp_extra = 0;
    if (!smp_source((uint32_t)clampi(p[P_SRC], 0, BRK_N + NUSR - 1), &d, &len, &rate))
        return;
    if (o->on)
        o->gtgt = 0, o->gstep = -(32767 / 96);  /* ~2 ms */
    smp_next[t] ^= 1u;
    a = sed_at(se->start, len);                 /* the edit: where the sound starts and ends (65535: its end) */
    b = se->end >= 65535u ? len : sed_at(se->end, len);
    if (a + 64u > b)
        a = b > 64u ? b - 64u : 0u;
    if (p[P_SMODE] == 0) {                      /* slices: 16 across it, or where the edit put them */
        uint32_t sl = note & 15u, w;
        if (se->manual) {
            s0 = sed_at(se->sl[sl], len);
            w = sl < 15u ? sed_at(se->sl[sl + 1u], len) : b;
            w = w > s0 ? (w - s0) << 4 : 0u;    /* (as 16 of them: the LENGTH below is in 64ths) */
        } else {
            w = b - a;
            s0 = a + ((sl * w) >> 4);
        }
        if (s0 < a) s0 = a;
        s1 = q ? s0 + ((q * w) >> 6) : b;       /* LENGTH: quarters of a slice */
    } else {                                    /* notes: from the start, C4 = as recorded */
        s0 = a;
        s1 = b;
        semis += (int32_t)note - 60;
    }
    if (s1 > b)
        s1 = b;
    if (s1 > len - 2u)                          /* (the interpolation reads one frame on) */
        s1 = len - 2u;
    if (s0 > s1)
        s0 = s1;
    if (trick(k, FX_OFS, &sv) && s1 > s0)       /* OFS: further in */
        s0 += (uint32_t)clampi(sv, 0, 15) * ((s1 - s0) >> 4);
    v->d = d;
    v->lo = s0 << 16;
    v->hi = s1 << 16;
    v->rev = (uint8_t)((trick(k, FX_REV, &sv) ? 1u : 0u) ^ (se->rev ? 1u : 0u));   /* REV: the other way */
    v->pos = v->rev ? v->hi : v->lo;
    v->gain = se->norm ? clampi(se->gain, 64, 4096) : 256;
    ratio = semi_ratio(semis);
    v->step = (((rate << 16) / 44100u) * (ratio >> 4)) >> 12;   /* the sample's rate, times the pitch */
    if (p[P_FIT] && src_is_break(p[P_SRC]))     /* FIT: a break follows the tempo */
        v->step = (uint32_t)(((uint64_t)v->step * (((uint32_t)prj.bpm << 16) / BRK_BPM)) >> 16);
    v->step0 = v->step;
    smp_retune(v, tk[t].tb_tr);
    v->g = 0;
    v->gtgt = 32767;
    v->gstep = 32767 / 40;
    v->rel = 2;
    v->fade_at = 0;
    v->drive = 1 + p[P_SDRIVE];
    v->crush = (uint8_t)(trick(k, FX_CRUSH, &sv) ? sv : p[P_SCRUSH]);
    v->on = 1;
}

static void smp_chunk(smpv_t *v, int32_t *out)
{
    const uint8_t *d = v->d;
    uint32_t i, remain;
    int32_t m = crush_mask(v->crush);
    if (!v->on)
        return;
    remain = v->rev ? (v->pos - v->lo) : (v->hi - v->pos);
    if (!v->fade_at && remain < (uint32_t)(v->rel * 260 + 32) * v->step) {   /* the end fades */
        v->fade_at = 1;
        v->gtgt = 0;
        v->gstep = -(32767 / (v->rel * 260 + 32) + 1);
    }
    for (i = 0; i < CHUNK; i++) {
        uint32_t k = v->pos >> 16, f = (v->pos >> 2) & 0x3FFFu;
        int32_t x0 = BRK_ULAW[d[k]], x1 = BRK_ULAW[d[k + 1u]], s;
        s = x0 + (((x1 - x0) * (int32_t)f) >> 14);
        if (v->gain != 256)                     /* NORMALISE (the edit) */
            s = clampi((s * v->gain) >> 8, -32767, 32767);
        s = sat(s * v->drive) & m;
        v->g += v->gstep;
        if ((v->gstep > 0 && v->g > v->gtgt) || (v->gstep < 0 && v->g < v->gtgt))
            v->g = v->gtgt;
        out[i] += (s * v->g) >> 15;
        if (v->rev) {
            if (v->pos <= v->lo + v->step) { v->on = 0; break; }
            v->pos -= v->step;
        } else {
            v->pos += v->step;
            if (v->pos >= v->hi) { v->on = 0; break; }
        }
    }
    if (v->g <= 0 && v->gtgt == 0)
        v->on = 0;
}

/* ---- a note on track t (k: the cell's two tricks) */
static const tricks_t NO_TRICKS = {FX_NONE, FX_NONE, 0, 0};
static uint8_t tb_inner;                        /* a table's HIT plays the note: the table goes on */
static void tb_apply(uint32_t t, int hit);
static void voice_on(uint32_t t, uint32_t note, uint32_t vol, const tricks_t *k, uint32_t arp_len)
{
    trk_t *v = &tk[t];
    const int8_t *p = prj.snd[t].p;
    const table_t *tb = &prj.tbl[t];
    uint32_t e = prj.snd[t].engine, nm = note_mode(t), i, table = 0;
    int32_t sv, pt = (int32_t)note << 8, old_pit = v->pitch;   /* (old_pit: an FM release's note) */
    if (note == NOTE_OFF) {
        v->gate = 0;
        if (v->stage && v->stage < 4u) v->stage = 4;
        if (e == E_FM && t == fm_trk) {
            fm6_note_t *n = &fmn[fm_cur];
            if (n->live && n->down) fm6_note_key(n, fm_patch, 0);
        }
        return;
    }
    if (!tb_inner) {                            /* a new note: its table from the first step */
        v->tb_run = 0;
        v->tb_tr = 0;
        v->tb_vsl = 0;
        tb_now[t] = NTSTEP;
        if (tb->on) {
            table = 1;
            v->tb_run = 1;
            v->tb_i = 0;
            v->tb_cnt = 0;
            v->tb_note = (uint8_t)note;
            v->tb_tr = (int8_t)clampi(tb->s[0].tr, -48, 48);
            if (tb->s[0].vol) vol = tb->s[0].vol & 15u;
        }
    }
    trig_ms[t] = fm1_ms;
    v->vol = (uint8_t)vol;
    v->volq = (int32_t)vol << 16;
    v->vslide = (int8_t)(trick(k, FX_VSL, &sv) ? sv : 0);
    v->npan = (int8_t)(trick(k, FX_PAN, &sv) ? sv : 0);
    if (p[P_MLFRT]) {                           /* LFO KEY: the LFO starts again with each note */
        v->lfo_ph = 0;
        v->lfo_rnd = xr(&rnd_isr);
    }
    v->bend = 0;
    v->arp_n = 0;
    v->shape = (uint8_t)p[P_SHAPE];
    v->crush = (uint8_t)p[P_CRUSH];
    v->vib = (uint8_t)(e == E_FM ? 0 : p[P_VIB]);
    if (nm == NM_NOTE) {
        int32_t gv;
        int fg = trick(k, FX_GLIDE, &gv);
        int glide = (fg || (e <= E_NOISE && p[P_GLIDE])) && v->stage && v->stage < 4u;
        if (glide) {
            v->ptgt = pt;
            v->glide = fg ? 1 + gv * gv / 6 : 1 + p[P_GLIDE] * p[P_GLIDE] / 6;
        } else {
            v->pitch = v->ptgt = pt;
            v->glide = 0;
            v->sweep = e <= E_NOISE ? p[P_SWEEP] * 512 : 0;
            if (v->env < 6000 && e <= E_WAVE)
                v->ph = v->ph2 = 0;             /* the same start every time (chip punch), when it cannot click */
        }
        if (trick(k, FX_ARP, &sv) && sv >= 0 && sv < 14) {
            v->arp_n = (uint8_t)CHORDS[sv].n;
            for (i = 0; i < 4u; i++)
                v->arp_off[i] = CHORDS[sv].off[i];
            v->arp_i = 0;
            v->arp_cnt = 0;
            v->arp_len = arp_len ? arp_len * 3u / (uint32_t)clampi(e <= E_NOISE ? p[P_ARPV] : 3, 1, 8) : 700u;
        }
        if (trick(k, FX_VIB, &sv)) v->vib = (uint8_t)sv;
        if (trick(k, FX_BEND, &sv)) v->bend = sv;
        if (trick(k, FX_SHAPE, &sv)) v->shape = (uint8_t)sv;
        if (trick(k, FX_CRUSH, &sv)) v->crush = (uint8_t)sv;
        if (trick(k, FX_PITCH, &sv)) v->pitch = v->ptgt = pt + sv * 256;
        if (!v->lfsr) v->lfsr = 1;
        if (e == E_FM && t == fm_trk && !(v->glide && v->stage)) {   /* a new FM voice; the last one releases */
            fm6_note_t *old = &fmn[fm_cur], *n;
            const uint8_t *fp = fm_patch;
            if (old->live && old->down) fm6_note_key(old, fp, 0);
            fm_vpit[fm_cur] = old_pit;          /* the note it was playing (before this one) */
            fm_cur ^= 1u;
            n = &fmn[fm_cur];
            fm6_note_init(n, fp, (int32_t)note + fp[FP_TRNSP] - 24, (int32_t)(vol * 8u + 7u), 1);
            fm6_lfo_key(&fm_lfo);
        }
    } else if (e == E_KIT)
        drum_on(t, cell_idx(note) & 15u, (trick(k, FX_PITCH, &sv) ? sv : 0) + v->tb_tr + (cell_idx(note) >= 16u ? 5 : 0), trick(k, FX_CRUSH, &sv) ? sv : -1);   /* (17..32: the same drum, tuned up) */
    else {
        if (nm == NM_SLICE && cell_idx(note) >= 16u) smp_extra = 5;
        smp_on(t, nm == NM_SLICE ? cell_idx(note) & 15u : note, k);
    }
    if (e == E_SAMPLE && nm == NM_NOTE)
        smp_on(t, note, k);
    v->note = (uint8_t)note;
    if (!(v->glide && v->stage) || v->stage == 4u) {
        v->stage = 1;
        if (p[P_ATT] == 0) v->env = 65535, v->stage = 2;
    }
    v->gate = 1;
    if (table)
        tb_apply(t, 0);                         /* the first step's trick (its pitch and volume are in) */
}

static void voice_off(uint32_t t)
{
    trk_t *v = &tk[t];
    v->gate = 0;
    if (v->stage && v->stage < 4u)
        v->stage = 4;
    if (prj.snd[t].engine == E_FM && t == fm_trk) {
        fm6_note_t *n = &fmn[fm_cur];
        if (n->live && n->down) fm6_note_key(n, fm_patch, 0);
    }
}

static void voice_cut(uint32_t t)                /* CUT: the note stops now, whatever plays it */
{
    voice_off(t);
    drum_choke(&drum[t][0]);
    drum_choke(&drum[t][1]);
    smp[t][0].gtgt = smp[t][1].gtgt = 0;
    smp[t][0].gstep = smp[t][1].gstep = -(32767 / 120);
}

static void tb_stop(uint32_t t)
{
    tk[t].tb_run = 0;
    tk[t].tb_vsl = 0;
    tb_now[t] = NTSTEP;
}

static void voices_silence(void)                 /* (the stop: the tables stop too, no HIT starts a note again) */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        voice_off(t);
        tb_stop(t);
    }
}

static void voice_kill(uint32_t t)               /* the engine changed (t NTRK: all tracks): the old voices go now, */
{                                                /* nothing would play them out (the gain fades in a chunk) */
    uint32_t a = t < NTRK ? t : 0u, b = t < NTRK ? t + 1u : NTRK;
    for (t = a; t < b; t++) {
        tk[t].gate = 0;
        tk[t].stage = 0;
        tk[t].env = 0;
        tk[t].tb_tr = 0;
        tb_stop(t);
        drum[t][0].on = drum[t][1].on = 0;
        smp[t][0].on = smp[t][1].on = 0;
        if (t == fm_trk)
            fmn[0].live = fmn[1].live = 0;
    }
}

/* ---- the table of a track (TABLE): from each note, a step every SPEED ticks; each step its pitch over the
 * note, its volume, its trick. HIT plays the note again (only while it is held: an OFF, a stop end it) */
static void tb_apply(uint32_t t, int hit)
{
    trk_t *v = &tk[t];
    const tstep_t *s = &prj.tbl[t].s[v->tb_i % NTSTEP];
    uint32_t e = prj.snd[t].engine;
    int32_t fv = s->fxv;
    v->tb_tr = (int8_t)clampi(s->tr, -48, 48);
    tb_now[t] = v->tb_i;
    if (s->vol) {
        v->vol = (uint8_t)(s->vol & 15u);
        v->volq = (int32_t)v->vol << 16;
    }
    if (e == E_SAMPLE) {                        /* (the other engines take the pitch in track_chunk) */
        smpv_t *sv = &smp[t][smp_next[t] ^ 1u];
        if (sv->on) smp_retune(sv, v->tb_tr);
    }
    switch (s->fx) {
    case TB_HIT:
        if (hit && v->gate) {
            tb_inner = 1;
            voice_on(t, v->tb_note, v->vol, &NO_TRICKS, 0);
            tb_inner = 0;
        }
        break;
    case TB_CUT: voice_cut(t); break;
    case TB_VIB: v->vib = (uint8_t)clampi(fv, 0, 15); break;
    case TB_BEND: v->bend = clampi(fv, -15, 15); break;
    case TB_SHAPE: v->shape = (uint8_t)clampi(fv, 0, 7); break;
    case TB_CRUSH:
        v->crush = (uint8_t)clampi(fv, 0, 7);
        if (e == E_SAMPLE) smp[t][smp_next[t] ^ 1u].crush = v->crush;
        if (e == E_KIT) drum[t][drum_next[t] ^ 1u].crush = v->crush;
        break;
    case TB_VSL: v->tb_vsl = (int8_t)clampi(fv, -15, 15); break;
    case TB_PAN: v->npan = (int8_t)clampi(fv, -7, 7); break;
    }
}

static void tb_chunk(uint32_t t)                /* once a chunk, before the track renders */
{
    trk_t *v = &tk[t];
    const table_t *tb = &prj.tbl[t];
    const tstep_t *s;
    uint32_t len, n, next;
    if (!v->tb_run)
        return;
    if (!tb->on) {                              /* turned off while it ran */
        tb_stop(t);
        v->tb_tr = 0;
        return;
    }
    n = g_ticklen * (uint32_t)clampi(tb->speed, 1, 24);
    v->tb_cnt += CHUNK;
    if (v->tb_cnt < n)
        return;
    v->tb_cnt -= n;                             /* (a step lasts at least a tick: 367 frames, never two a chunk) */
    if (v->tb_cnt >= n)
        v->tb_cnt = 0;
    len = (uint32_t)clampi(tb->len, 1, NTSTEP);
    s = &tb->s[v->tb_i % NTSTEP];
    if (s->fx == TB_HOP && (uint32_t)s->fxv < len)
        next = (uint32_t)s->fxv;
    else if (v->tb_i + 1u < len)
        next = v->tb_i + 1u;
    else if (tb->loop < len)
        next = tb->loop;
    else {
        v->tb_run = 0;                          /* the end, no loop: the last step stays */
        return;
    }
    v->tb_i = (uint8_t)next;
    tb_apply(t, 1);
}

/* ---- the LFO of a track, once a chunk: -32767..32767 x depth / 15 */
static void lfo_chunk(trk_t *v, const int8_t *p)
{
    int32_t w;
    uint32_t o = v->lfo_ph;
    if (!p[P_LFOD] || !p[P_LFOA]) {
        v->lfo = 0;
        return;
    }
    v->lfo_ph += lfo_inc[clampi(p[P_LFOR], 0, 10)];
    switch (p[P_LFOW]) {
    case 1: w = (int32_t)(v->lfo_ph >> 16); w = (w < 32768 ? w : 65535 - w) * 2 - 32767; break;
    case 2: w = v->lfo_ph < 0x80000000u ? 32767 : -32767; break;
    case 3: w = (int32_t)(v->lfo_ph >> 16) - 32768; break;
    case 4:
        if (v->lfo_ph < o) v->lfo_rnd = xr(&rnd_isr);
        w = (int32_t)(v->lfo_rnd >> 17) - 16384;
        w *= 2;
        break;
    default: w = sine(v->lfo_ph); break;
    }
    v->lfo = (w * p[P_LFOA]) / 15;
}

/* ---- one track, one chunk: the engine, the envelope, the effect -> out (mono) */
static void track_chunk(uint32_t t, int32_t *out)
{
    trk_t *v = &tk[t];
    const int8_t *p = prj.snd[t].p;
    uint32_t e = prj.snd[t].engine, i, dest = (uint32_t)p[P_LFOD];
    int32_t raw[CHUNK], pit, gain, tgt, da, amp, fxa = p[P_FXA];
    int32_t ton = TON_A[clampi(e == E_KIT ? p[P_KTON] : e == E_SAMPLE ? p[P_STON] : p[P_TON], 0, 15)];
    int active = v->stage != 0 || v->amp != 0;
    lfo_chunk(v, p);
    switch (v->stage) {                         /* ADSR, once a chunk */
    case 1:
        v->env += ENV_RATE[p[P_ATT] & 15];
        if (v->env >= 65535) { v->env = 65535; v->stage = 2; }
        break;
    case 2:
        tgt = p[P_SUS] * 4369;
        v->env -= ENV_RATE[p[P_DEC] & 15] >> 1;
        if (v->env <= tgt) { v->env = tgt; v->stage = tgt ? 3 : 0; }
        break;
    case 4:
        v->env -= ENV_RATE[p[P_REL] & 15] >> 1;
        if (v->env <= 0) { v->env = 0; v->stage = 0; }
        break;
    }
    env_now[t] = (uint16_t)v->env;
    env_stage[t] = v->stage;
    for (i = 0; i < CHUNK; i++)
        raw[i] = 0;
    /* pitch: glide, bend, sweep, vibrato, arpeggio, the live octave, the LFO */
    if (v->glide) {
        if (v->pitch < v->ptgt) { v->pitch += v->glide; if (v->pitch > v->ptgt) v->pitch = v->ptgt; }
        else if (v->pitch > v->ptgt) { v->pitch -= v->glide; if (v->pitch < v->ptgt) v->pitch = v->ptgt; }
    }
    v->pitch += v->bend;
    v->pitch = clampi(v->pitch, -48 * 256, 160 * 256);   /* (a long BEND, a table's, stays in range) */
    v->sweep = (int32_t)(((int64_t)v->sweep * 64880) >> 16);   /* tau ~35 ms */
    pit = v->pitch + v->sweep + ((lv_mask >> t & 1u) ? lv_transpose * 256 : 0) + v->tb_tr * 256;
    if (v->vib) {
        v->vph += 9350000u;                     /* ~6 Hz */
        pit += (sine(v->vph) * v->vib * 14) >> 15;
    }
    if (v->arp_n) {
        v->arp_cnt += CHUNK;
        if (v->arp_cnt >= v->arp_len) {
            v->arp_cnt -= v->arp_len;
            if (++v->arp_i >= v->arp_n) v->arp_i = 0;
        }
        pit += v->arp_off[v->arp_i] * 256;
    }
    if (dest == LFD_PITCH)
        pit += (v->lfo * p[P_LFOA] * 4) >> 15;
    if (dest == LFD_FILT)
        fxa = clampi(fxa + ((v->lfo * 8) >> 15), 0, 15);
    if (p[P_MENVA] || p[P_MVELA])               /* MODULATION: the envelope and the velocity open the effect */
        fxa = clampi(fxa + ((v->env * p[P_MENVA]) >> 16) + ((v->vol * p[P_MVELA]) >> 4), 0, 15);
    if (p[P_MENVP])                             /* ... and bend the pitch */
        pit += (v->env * p[P_MENVP]) >> 8;
    if (v->vslide || v->tb_vsl) {               /* VSL: the volume slides over the row (a table's: on) */
        v->volq = clampi(v->volq + (v->vslide + v->tb_vsl) * (int32_t)vsl_k, 0, 15 << 16);
        v->vol = (uint8_t)(v->volq >> 16);
    }
    if (e <= E_NOISE && active) {
        uint32_t inc, inc2 = 0, duty = 0x80000000u, ph = v->ph, ph2 = v->ph2;
        int32_t m = crush_mask(v->crush), lp = v->lp, det = p[P_DETUNE];
        int uni = (lv_mask >> t & 1u) ? lv_uni : 0;
        if (e == E_NOISE)
            pit += 48 * 256;                    /* the noise clock runs four octaves over the note */
        inc = pitch_inc(pit);
        if (e != E_NOISE && (uni || det))
            inc2 = uni == 2 ? inc >> 1 : inc + (inc >> 7) + (uint32_t)(((uint64_t)inc * (uint32_t)det) >> 11);
        if (e == E_PULSE) {
            static const uint32_t DUTY[4] = {0x20000000u, 0x40000000u, 0x80000000u, 0xC0000000u};
            if (v->shape < 4u && dest != LFD_SHAPE)
                duty = DUTY[v->shape];
            else {
                v->pwm += 1400000u;             /* ~1 Hz */
                duty = 0x80000000u + (uint32_t)((dest == LFD_SHAPE ? v->lfo : sine(v->pwm)) * 40000);
            }
        }
        for (i = 0; i < CHUNK; i++) {
            int32_t s;
            if (e == E_PULSE) {
                s = (ph < duty ? 32767 : -32767) + blep(ph, inc) - blep(ph - duty, inc);
                ph += inc;
                if (inc2) {
                    s += (ph2 < duty ? 32767 : -32767) + blep(ph2, inc2) - blep(ph2 - duty, inc2);
                    s = (int32_t)(((int64_t)s * 23170) >> 15);
                    ph2 += inc2;
                }
            } else if (e == E_WAVE) {
                const uint8_t *w = WAVES[v->shape & 7u];
                s = (w[ph >> 27] * 2 - 15) * 2184;
                ph += inc;
                if (inc2) {
                    s = ((s + (w[ph2 >> 27] * 2 - 15) * 2184) * 23170) >> 15;
                    ph2 += inc2;
                }
            } else {
                uint32_t o = ph;
                ph += inc;
                if (ph < o) {                   /* one LFSR step per wrap of the noise clock */
                    uint32_t l = v->lfsr, b = (l ^ (l >> (v->shape ? 6u : 1u))) & 1u;
                    v->lfsr = (l >> 1) | (b << 14);
                }
                s = (v->lfsr & 1u) ? 26000 : -26000;
            }
            s &= m;
            lp += (int32_t)(((int64_t)(s - lp) * ton) >> 15);
            v->dc += (lp * 256 - v->dc) >> 11;  /* ~3 Hz high-pass, as the chips' output capacitors */
            raw[i] = (lp - (v->dc >> 8)) >> 1;
        }
        v->ph = ph;
        v->ph2 = ph2;
        v->lp = lp;
    } else if (e == E_FM) {
        const uint8_t *fp = fm_patch;
        if (t != fm_trk) {                      /* (a second FM track cannot happen: the editor refuses it) */
            active = 0;
        } else {
            if (fm_seen != fm_gen) {            /* a new patch: its LFO */
                fm_seen = fm_gen;
                fm6_lfo_reset(&fm_lfo, fp);
            }
            if (fm_pos == 0u) {                 /* a new block of 32 */
                int32_t lv, ld, lf, k;
                uint32_t j;
                int32_t bri = (p[P_FMBRI] + (dest == LFD_SHAPE ? (v->lfo * 10) >> 15 : 0)) * 48;
                for (j = 0; j < (uint32_t)CTL; j++)
                    fm_buf[j] = 0;
                lv = fm6_lfo_sample(&fm_lfo);
                ld = fm6_lfo_delay(&fm_lfo);
                for (k = 0; k < 2; k++) {
                    fm6_note_t *n = &fmn[k];
                    int32_t bus[CTL], vp = k == (int32_t)fm_cur ? pit : pit - v->pitch + fm_vpit[k];
                    lf = fm6_note_logfreq((vp >> 4) + (fp[FP_TRNSP] - 24) * 16, 0);   /* (the release: its own note) */
                    if (cpu_shed && k != fm_cur)
                        n->live = 0;            /* (the governor: no release voice) */
                    if (!n->live)
                        continue;
                    if (fm6_note_compute(n, fp, bus, lv, ld, lf, fm_alg, fm_fb, fm_dt, bri << 16)) {
                        for (j = 0; j < (uint32_t)CTL; j++) {
                            int32_t x = bus[j] - n->dc;
                            n->dc += x >> 10;   /* FM puts sidebands at 0 Hz */
                            fm_buf[j] += x >> 10;
                        }
                    }
                    if (!n->down && fm6_note_done(n, fp, fm_alg))
                        n->live = 0;
                }
            }
            for (i = 0; i < CHUNK; i++)
                raw[i] = fm_buf[fm_pos + i];
            fm_pos = (uint8_t)(fm_pos ^ CHUNK);
            if (!fmn[0].live && !fmn[1].live && !v->stage)
                active = 0;
        }
    } else if (e == E_KIT) {
        if (cpu_shed >= 3u) drum[t][drum_next[t]].on = 0;   /* (the governor: the older voice goes) */
        drum_chunk(&drum[t][0], raw);
        drum_chunk(&drum[t][1], raw);
        if (ton < 32767)
            for (i = 0; i < CHUNK; i++) { v->lp += (int32_t)(((int64_t)(raw[i] - v->lp) * ton) >> 15); raw[i] = v->lp; }
        active = 1;
    } else {
        if (cpu_shed >= 3u) smp[t][smp_next[t]].on = 0;
        smp_chunk(&smp[t][0], raw);
        smp_chunk(&smp[t][1], raw);
        if (ton < 32767)
            for (i = 0; i < CHUNK; i++) { v->lp += (int32_t)(((int64_t)(raw[i] - v->lp) * ton) >> 15); raw[i] = v->lp; }
        active = 1;
    }
    if (!active && !v->amp)
        return;
    /* the envelope and the note's volume, ramped over the chunk */
    gain = (int32_t)(((uint32_t)v->env * (uint32_t)v->env) >> 17);
    tgt = (int32_t)(((int64_t)gain * ((v->volq * 18) >> 16)) >> 8);   /* vol 15 -> ~1.05 x gain */
    tgt = (int32_t)(((int64_t)tgt * ((p[P_VOL] & 15) * 5461)) >> 16);   /* MIX VOLUME: 12 = x 1 (no division) */
    if (dest == LFD_VOL)
        tgt = (int32_t)(((int64_t)tgt * (32767 - ((32767 - v->lfo) >> 1) * 1)) >> 15);
    if (lv_mute >> t & 1u)
        tgt = 0;
    amp = v->amp;
    da = (tgt - amp) / (int32_t)CHUNK;
    for (i = 0; i < CHUNK; i++) {
        out[i] = (int32_t)(((int64_t)raw[i] * amp) >> 15);
        amp += da;
    }
    v->amp = tgt;
    if (cpu_shed < 2u)                          /* (the load governor: kawa_seq.c) */
        fx_insert(&v->fx, p[P_FXT], fxa, p[P_FXB], p[P_FXM], out, CHUNK);
}
