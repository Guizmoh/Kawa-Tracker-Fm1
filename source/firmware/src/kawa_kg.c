/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the live effects of K-GLITCH (Omnipotard, GPL-3.0-only: its kg_dd.c, kg_beat.c, kg_filter.c), on KAWA's live
 * bus. Included by kawa_fx.c; the audio interrupt, after the capture of each frame (fx_live).
 *
 * Two more banks on the LIVE page, one effect at a time (the last pad pressed), each shaped by its INTENSITY (KNOB 2):
 *   DESTROY  LOOP DESTROY (1/2 .. 1/32 of a bar), STUTTER X (2x .. 8x a beat), GLITCH (crush, rate, chunks of the past,
 *            noise), SCRATCH (a hand on the record), REVERSE X, PITCH MAD (a staircase, jumps, a dive), GATE X,
 *            FILTER CHAOS, BEAT REPEAT, BEAT SKIP, MICRO STUTTER, TIME STRETCH, RING MOD, FUZZ, RANDOMIZER (one of
 *            the first 14 each 16th), TOTAL CHAOS (the same, crushed, a ring on some 16ths)
 *   BEAT     programs over the bar, on the transport: RND STUTTER, RND BAR, RND FILL, ROLL UP, SKIP 8TH, STAMMER (3-3-2),
 *            GATE 8 / 16 / 3-3-2 / RAMP, REV BEAT, REV OFFBEAT, SCRATCH BAR, SCRATCH CUT, TAPE STOP, HALF TIME
 * The scratches move a "record" the way a hand does (smooth strokes that stop and turn back) under a crossfader, read
 * with a cubic interpolation; the sound follows the record's speed as a cartridge does: duller and quieter when slow,
 * silent when still. POCKET's two SCRATCH pads play it too (gentle, then wild).
 * And the FILTER page (FX, third page): LP, HP, BP, NOTCH (a trapezoidal state-variable filter, A. Simper's), DRIVE,
 * COMB+ / COMB-, METAL, VOWEL, PHASER, RESONATOR, its cutoff a note (the keys tune it), MOVE an LFO of a bar.
 *
 * The past they read is KAWA's capture (cap: the bus before its effects, 0.74 s), its two sides as one; a segment an
 * effect loops is kept from the capture's writer (kg_owns). Every switch fades over 64 frames; the gains stay at most 1,
 * the output is clamped. No division a frame (the FM-1 divides slowly): what needs one is worked out once a chunk. */

enum { DD_LOOP, DD_STUT, DD_GLITCH, DD_SCRATCH, DD_REV, DD_PITCH, DD_GATE, DD_FILTER,
       DD_BREP, DD_BSKIP, DD_MICRO, DD_STRETCH, DD_RING, DD_FUZZ, DD_RAND, DD_TOTAL,
       DD_RSTUT, DD_RBAR, DD_RFILL, DD_ROLL, DD_SKIP8, DD_STAMR, DD_GATE8, DD_GATE16, DD_GATE332, DD_GRAMP,
       DD_REVB, DD_REVOFF, DD_SCRBAR, DD_SCRCUT, DD_TSTOP, DD_HALFT, DD_N };
#define KG_LANE (LF_N + 1u)                       /* the LIVE lane: LF_N + 1 + DD_* is a K-GLITCH effect (33 .. 64) */
#define DD_W 2048u                                /* PITCH and STRETCH: the grain window (46 ms) */
#define KB_OWN 10000u                             /* a record held from the writer: at most */

typedef struct {
    int8_t cur, sub;          /* the effect running (DD_*, -1 none); what plays this 16th (RANDOMIZER, TOTAL: picked) */
    int32_t g;                /* its wet gain, 0..32767 */
    int32_t xf;               /* a sub-effect's fade in, 0..64 */
    uint32_t t;               /* frames since it started */
    uint32_t s16;             /* the 16th last seen */
    uint32_t S, L, L0, k;     /* a segment of the capture: start, length (L0: as captured), position */
    uint8_t rev, own, mode;   /* this pass backwards; the segment is held from the writer; SKIP / TOTAL: this 16th's way */
    uint32_t off;             /* LOOP: offset into the fragment */
    int32_t rep;              /* BREP: this repeat's level, Q15 */
    uint32_t ri;              /* SCRATCH, STRETCH, HALF, STOP: a read position */
    int32_t rf, sp;           /* .. its fraction (Q16) and speed (Q16) */
    uint32_t ph, dph, ph2;    /* RING's carrier; TOTAL's ring */
    uint32_t pp;              /* PITCH: the delay phase, Q16 over DD_W; STRETCH: the grains' phase */
    int32_t ratio;            /* PITCH: Q16; STRETCH: the speed, Q16 */
    uint32_t ga, gb;          /* STRETCH: the two grains' anchors */
    int8_t semi;              /* PITCH: the step it is on */
    uint32_t mask, i32;       /* GATE: the bar's pattern (bit per 16th or 32nd), the slot last seen */
    int32_t gg;               /* the gates: the gain, Q15 */
    int32_t cut, cut_to, reso;   /* FILTER CHAOS: the cutoff (the filter's index), where it goes, the resonance */
    int32_t hl, hr;           /* GLITCH: the held frame */
    uint32_t hn, corrupt, csrc, noise;
    uint32_t rnd;
    uint32_t bseed, rec, slen, sk, D, u24, du;        /* BEAT and SCRATCH: the bar's seed, the record, the 16th */
    int32_t acc, lpy, ps, dl, dr, ll, lr;
    uint8_t op, arg, open, newop, qi, from8, to8;
    uint32_t fmul;            /* STUT: 13000 / L, Q8 (once a chunk) */
    uint32_t I;               /* the intensity now, 0..100 (once a chunk) */
} kd_t;
static kd_t kd = {.cur = -1, .sub = -1, .rnd = 0x51A3E1B7u};
static uint8_t kd_int[DD_N] = {[0 ... DD_N - 1] = 50};   /* main: each effect's INTENSITY, 0..100 */
static uint8_t kd_sint;                           /* ISR: an intensity that wins (+ 1; POCKET's scratches), 0 none */
static volatile int8_t kg_hold = -1;              /* main: the DESTROY / BEAT pad held, -1 none */
static volatile int8_t kg_on = -1;                /* ISR: the effect sounding now (the screens) */
static uint8_t kg_anyown;
static int32_t kd_fs[2][2];                       /* FILTER CHAOS: its states */

static uint32_t kg_rand(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int32_t kg_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the capture, its two sides as one (the bus's scale) */
static int32_t ring_at(uint32_t i)
{
    uint32_t w = cap[i & CAPM];
    return ((int32_t)(int16_t)(w & 0xFFFFu) + (int32_t)(int16_t)(w >> 16)) >> 1;
}
static uint32_t dd_beat(void) { return g_rowlen * 4u; }
static uint32_t dd_barpos(void) { return sq_play ? ((uint32_t)(sq_row >> 2) & 3u) * dd_beat() + g_beatpos : kd.t; }
static uint32_t dd_cap(uint32_t L) { return L < 64u ? 64u : L > CAPN / 2u - 1024u ? CAPN / 2u - 1024u : L; }
static int32_t dd_rd(uint32_t i, int32_t f)        /* the capture at i + f / 65536, linear */
{
    int32_t a = ring_at(i), b = ring_at(i + 1u);
    return a + (((b - a) * (f >> 1)) >> 15);
}
static int32_t dd_declick(int32_t y, uint32_t k, uint32_t L)
{
    return k < 64u ? (y * (int32_t)k) >> 6 : L - k < 64u ? (y * (int32_t)(L - k)) >> 6 : y;
}
/* the capture's writer leaves a held segment alone once it has been written */
static int kg_owns(uint32_t w)
{
    uint32_t d;
    if (!kg_anyown)
        return 0;
    d = w - kd.S;
    return d >= kd.L0 && (d & CAPM) < kd.L0;
}
static int32_t dd_semi(int32_t s)                  /* 2^(s/12), Q16, s -24..24 */
{
    static const uint32_t R[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
    int32_t o = 0;
    while (s < 0) s += 12, o--;
    while (s >= 12) s -= 12, o++;
    return o >= 0 ? (int32_t)(R[s] << o) : (int32_t)(R[s] >> -o);
}

/* ---- the BEAT bank and the SCRATCH (K-GLITCH's kg_beat.c): an operation on each 16th of the bar */
enum { BO_DRY, BO_MUTE, BO_REP, BO_PREV, BO_REV, BO_HALF, BO_STOP, BO_SCR, BO_PREP, BO_RREV };
#define BO(op, arg) ((uint32_t)(op) | (uint32_t)(arg) << 4)
#define B_CONT 0x100u                              /* the operation goes on from the 16th before */
#define B_CAP 0x200u                               /* the record is this 16th's sound */
static const uint8_t KB_DIV[10] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};   /* REP: a beat / DIV */
enum { KD1, KD2, KD3, KD4, KD6, KD8, KD12, KD16, KD24, KD32 };
enum { KS_BABY, KS_CHIRP, KS_TEAR, KS_TRANS, KS_SCRIB, KS_DRAG, KS_BACK, KS_N };
#define SQ(t, o) ((uint8_t)((t) << 1 | (o)))       /* a stroke: to a place (eighths of its depth), the fader open */
static const uint8_t KB_PAT[KS_N][4] = {
    {SQ(6, 1), SQ(0, 1), SQ(6, 1), SQ(0, 1)},      /* BABY: forward and back, open */
    {SQ(8, 1), SQ(0, 0), SQ(8, 1), SQ(0, 0)},      /* CHIRP: forward open, back shut */
    {SQ(4, 1), SQ(4, 1), SQ(8, 1), SQ(0, 1)},      /* TEAR: the forward stroke in two, a stop between */
    {SQ(4, 1), SQ(8, 0), SQ(4, 1), SQ(0, 0)},      /* TRANSFORMER: the fader cutting the strokes */
    {SQ(2, 1), SQ(0, 1), SQ(2, 1), SQ(0, 1)},      /* SCRIBBLE: tiny fast strokes */
    {SQ(2, 1), SQ(4, 1), SQ(6, 1), SQ(8, 1)},      /* DRAG: slowly forward */
    {SQ(8, 0), SQ(0, 1), SQ(8, 0), SQ(0, 1)}};     /* BACKWARD: forward shut, back open */

static uint32_t kb_hash(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    return a ^ (a >> 16);
}
static int32_t kb_abs(int32_t v) { return v < 0 ? -v : v; }
static int dd_isop(int32_t e) { return e == DD_SCRATCH || (e >= DD_RSTUT && e < DD_N); }
static int dd_isgate(int32_t e) { return e >= DD_GATE8 && e <= DD_GRAMP; }

/* the capture at i + f / 65536, cubic (Hermite): a moving record without the linear's grit */
static int32_t dd_cub(uint32_t i, int32_t f)
{
    int32_t xm = ring_at(i - 1u), x0 = ring_at(i), x1 = ring_at(i + 1u), x2 = ring_at(i + 2u), t = (f >> 4) & 4095;
    int32_t c1 = (x1 - xm) >> 1, c2 = xm - ((5 * x0) >> 1) + 2 * x1 - (x2 >> 1), c3 = ((x2 - xm) >> 1) + ((3 * (x0 - x1)) >> 1);
    int32_t y = ((((((c3 * t) >> 12) + c2) * t) >> 12) + c1) * t;
    return kg_clamp((y >> 12) + x0, -65535, 65535);
}
/* the record's sound at speed v (Q15, 32768 = normal, either way): a low-pass and a level that follow the speed */
static int32_t kb_cart(int32_t m, int32_t v)
{
    int32_t a, g;
    if (v < 0) v = -v;
    a = kg_clamp((v * 5) >> 3, 700, 30000);
    kd.lpy += (((m - kd.lpy) >> 4) * (a >> 3)) >> 8;
    g = v > 36000 ? 36000 : v;
    return ((kd.lpy >> 4) * (g >> 3)) >> 8;
}
/* the last attack in the capture's last beat: where a DJ would put the needle */
static uint32_t kb_cue(uint32_t base)
{
    uint32_t span = dd_beat(), p, best, e[4] = {0, 0, 0, 0}, j = 0;
    int32_t top = 0;
    if (span > CAPN / 2u) span = CAPN / 2u;
    best = base - span / 2u;
    for (p = base - span; (int32_t)(base - 512u - p) > 0; p += 64u, j++) {
        uint32_t now = (uint32_t)(kb_abs(ring_at(p)) + kb_abs(ring_at(p + 16u)) + kb_abs(ring_at(p + 32u)) + kb_abs(ring_at(p + 48u)));
        int32_t rise = (int32_t)now - (int32_t)((e[0] + e[1] + e[2] + e[3]) >> 2);
        if (j >= 4u && rise > top) top = rise, best = p - 64u;
        e[j & 3u] = now;
    }
    return best;
}

/* the operation of the effect e on the bar's 16th s */
static uint32_t beat_op(int32_t e, uint32_t s, uint32_t I)
{
    uint32_t h = kb_hash(kd.bseed ^ (s * 0x9E3779B9u)), r = h % 100u, b = s >> 2, q = s & 3u;
    switch (e) {
    case DD_SCRATCH: {
        static const uint8_t SM[4] = {KS_CHIRP, KS_TEAR, KS_TRANS, KS_BACK};
        uint32_t p = I < 25u ? KS_BABY : I < 50u ? (s & 1u ? KS_BABY : KS_CHIRP) : I < 75u ? SM[(h >> 8) % 4u] : (h >> 8) % KS_N;
        return BO(BO_SCR, p);
    }
    case DD_RSTUT: {                                   /* decided per 8th */
        static const uint8_t ST[6] = {KD32, KD16, KD16, KD12, KD24, KD8};
        uint32_t hp = kb_hash(kd.bseed ^ ((s >> 1) * 0x85EBCA6Bu));
        if (s < 2u || hp % 100u >= 15u + I * 6u / 10u)
            return BO(BO_DRY, 0);
        return BO(BO_REP, ST[(hp >> 8) % 6u]) | (s & 1u ? B_CONT : 0u);
    }
    case DD_RBAR: {
        uint32_t dry = 70u - I * 6u / 10u;
        if (r < dry || (s == 0u && I < 50u))
            return BO(BO_DRY, 0);
        switch ((h >> 8) % 8u) {
        case 0: return BO(BO_REP, KD32);
        case 1: return BO(BO_REP, KD16);
        case 2: return BO(BO_PREV, 1);
        case 3: return BO(BO_REV, 1);
        case 4: return BO(BO_MUTE, 0);
        case 5: return BO(BO_HALF, 1);
        case 6: return BO(BO_REP, KD24);
        default: return BO(BO_PREP, (h >> 12) % 2u ? 7 : 12);
        }
    }
    case DD_RFILL:
        if (s < (I < 50u ? 12u : 8u))
            return BO(BO_DRY, 0);
        if (s == 15u)
            return (h >> 8) % 2u ? BO(BO_REP, KD32) : BO(BO_MUTE, 0);
        switch ((h >> 8) % 5u) {
        case 0: return BO(BO_REP, KD16);
        case 1: return BO(BO_REP, KD8);
        case 2: return BO(BO_REV, 1);
        case 3: return BO(BO_PREV, 1);
        default: return BO(BO_PREP, 5u + (h >> 12) % 8u);
        }
    case DD_ROLL:                                      /* the downbeat: a quarter, 8ths, 16ths, 32nds */
        if (b == 0u)
            return BO(BO_DRY, 0) | (s == 0u ? B_CAP : 0u);
        return BO(BO_PREP, b) | (q ? B_CONT : 0u);
    case DD_SKIP8:
        if (q < 2u)
            return BO(BO_DRY, 0);
        return I < 50u ? BO(BO_MUTE, 0) : BO(BO_PREV, 2);
    case DD_STAMR: {                                   /* hits on 0, 3, 6 of each half bar */
        static const uint8_t BACK[8] = {0, 1, 2, 0, 1, 2, 0, 1};
        uint32_t k = s & 7u;
        if (!BACK[k] || (I < 40u && k != 1u && k != 4u && k != 7u))
            return BO(BO_DRY, 0);
        return I > 80u ? BO(BO_REP, KD32) : BO(BO_PREV, BACK[k]);
    }
    case DD_REVB:
        if (I < 50u && !(b & 1u))
            return BO(BO_DRY, 0);
        return BO(BO_REV, 4) | (q ? B_CONT : 0u);
    case DD_REVOFF:
        if (I >= 60u && b == 3u)
            return BO(BO_REV, 4) | (q ? B_CONT : 0u);
        if (q < 2u)
            return BO(BO_DRY, 0);
        return BO(BO_REV, 2) | (q == 3u ? B_CONT : 0u);
    case DD_SCRBAR: {                                  /* silence, three short scratches, the downbeat backwards */
        uint32_t mute = I > 66u ? 2u : 4u, n = I > 66u ? 5u : 3u, k;
        if (s < mute)
            return BO(BO_MUTE, 0) | (s == 0u ? B_CAP : 0u);
        if (s >= 12u)
            return BO(BO_RREV, 0) | (s > 12u ? B_CONT : 0u);
        k = (s - mute) / 2u;
        if ((s - mute) & 1u || k >= n)
            return BO(BO_MUTE, 0);
        {
            static const uint8_t SB[5] = {KS_CHIRP, KS_TEAR, KS_TRANS, KS_BABY, KS_BACK};
            return BO(BO_SCR, I < 34u ? KS_CHIRP : SB[k % 5u]);
        }
    }
    case DD_SCRCUT:
        if (!(b & 1u))
            return BO(BO_DRY, 0) | (q == 0u ? B_CAP : 0u);
        return BO(BO_SCR, I < 50u ? (q & 1u ? KS_BACK : KS_TRANS) : (h >> 8) % 2u ? KS_SCRIB : KS_TRANS);
    case DD_TSTOP: {
        uint32_t st = I < 50u ? 12u : 8u;
        if (s < st)
            return BO(BO_DRY, 0);
        return BO(BO_STOP, 16u - st) | (s > st ? B_CONT : 0u);
    }
    case DD_HALFT:
        if (I < 50u)
            return BO(BO_HALF, 4) | (q ? B_CONT : 0u);
        return BO(BO_HALF, 2) | (q & 1u ? B_CONT : 0u);
    default:                                           /* the gates: the sound as it is, gated (beat_gate) */
        return BO(BO_DRY, 0);
    }
}

/* the gates, from the place in the bar: open or shut (once a chunk) */
static uint32_t beat_gate(int32_t e, uint32_t p, uint32_t I)
{
    uint32_t beat = dd_beat() + 1u, duty = 25u + I * 65u / 100u, cyc, ph;
    switch (e) {
    case DD_GATE8:
        cyc = beat / 2u;
        break;
    case DD_GATE16:
        cyc = beat / 4u;
        if (I > 70u && kb_hash(kd.bseed ^ (p / cyc) * 0x27D4EB2Fu) % 100u < I - 70u)
            return 0;
        break;
    case DD_GATE332: {                                 /* 3 + 3 + 2 sixteenths each half bar */
        static const uint8_t ST[8] = {0, 0, 0, 3, 3, 3, 6, 6}, LN[8] = {3, 3, 3, 3, 3, 3, 2, 2};
        uint32_t sl = beat / 4u, hb = p % (beat * 2u), k = hb / sl;
        if (k > 7u) k = 7u;
        return hb - ST[k] * sl < LN[k] * sl * duty / 100u;
    }
    default:                                           /* GATE RAMP: quarters, 8ths, 16ths, 32nds */
        cyc = beat >> ((p / beat) & 3u);
        duty = 30u + I * 40u / 100u;
        break;
    }
    ph = p % cyc;
    return ph < cyc * duty / 100u;
}

/* a new 16th of the bar (s), the capture's index base: the operation set up */
static void beat_slot(uint32_t s, uint32_t base)
{
    uint32_t I = kd.I, code, op, arg, beat = dd_beat(), slen = beat / 4u;
    if (s == 0u)
        kd.bseed = kg_rand(&kd.rnd);                   /* a new bar: new random ones */
    code = beat_op(kd.sub, s, I);
    op = code & 15u;
    arg = code >> 4 & 15u;
    kd.slen = slen;
    kd.sk = 0;
    if (code & B_CAP) {                                /* the record: this 16th on (held from the writer) */
        kd.rec = base;
        kd.S = base;
        kd.L0 = kd.sub == DD_ROLL ? (beat / 2u < KB_OWN ? beat / 2u : KB_OWN) : (beat < KB_OWN ? beat : KB_OWN);
        kd.own = 1;
    }
    if ((code & B_CONT) && op == kd.op)
        return;
    kd.op = (uint8_t)op;
    kd.arg = (uint8_t)arg;
    kd.newop = 1;                                      /* (the jump declicked) */
    kd.open = op != BO_MUTE;
    kd.k = 0;
    switch (op) {
    case BO_REP:
        kd.S = base;                                   /* (the first pass is the live sound) */
        kd.L = dd_cap(beat / KB_DIV[arg % 10u]);
        if (kd.sub != DD_ROLL && kd.sub != DD_SCRBAR) kd.own = 0;
        break;
    case BO_PREV:
        kd.S = base - arg * slen;
        kd.L = dd_cap(slen);
        break;
    case BO_REV:
        kd.L = dd_cap(arg * slen);
        kd.S = base - kd.L;
        break;
    case BO_RREV:
        kd.S = kd.rec;
        kd.L = kd.L0 > 64u ? kd.L0 : 64u;
        break;
    case BO_HALF:
        kd.ri = base, kd.rf = 0, kd.sp = 32768;
        break;
    case BO_STOP:                                      /* to a standstill over 3/4 of the span */
        kd.ri = base, kd.rf = 0, kd.sp = 65536;
        kd.acc = (int32_t)(65536u / (arg * slen * 3u / 4u + 1u)) + 1;
        kd.lpy = 0;
        break;
    case BO_PREP: {                                    /* a repeat of the record (ROLL) or of the 16th before, pitched */
        int32_t semi = kd.sub == DD_ROLL ? (I > 50u ? (int32_t)(arg * (I > 80u ? 4u : 2u)) : 0) : (int32_t)arg;
        if (kd.sub == DD_ROLL) {
            kd.S = kd.rec;
            kd.L = dd_cap(beat >> arg);
            if (kd.L > kd.L0 && kd.own) kd.L = kd.L0;
        } else {
            kd.S = base - slen;
            kd.L = dd_cap(slen);
        }
        kd.ri = kd.S, kd.rf = 0;
        kd.sp = dd_semi(semi);
        break;
    }
    case BO_SCR:
        kd.D = slen / 2u;
        kd.qi = 0;
        kd.u24 = 0;
        kd.du = (1u << 24) / (slen / 4u + 1u);
        kd.from8 = kd.to8;                             /* (from where the last stroke left the record) */
        kd.to8 = (uint8_t)(KB_PAT[arg % KS_N][0] >> 1);
        kd.open = KB_PAT[arg % KS_N][0] & 1u;
        break;
    default:
        break;
    }
}

/* the operation's frame: the dry x, y; idx the capture's index of this frame */
static void beat_sample(int32_t x, int32_t y, uint32_t idx, int32_t *wl, int32_t *wr)
{
    int32_t m = 0;
    kd.sk++;
    switch (kd.op) {
    case BO_DRY:
        *wl = x, *wr = y;
        goto gate;
    case BO_MUTE:
        break;
    case BO_REP:
    case BO_PREV:
        m = dd_declick(ring_at(kd.S + kd.k), kd.k, kd.L);
        if (++kd.k >= kd.L) kd.k = 0;
        break;
    case BO_REV:
    case BO_RREV:
        m = dd_declick(ring_at(kd.S + kd.L - 1u - kd.k), kd.k, kd.L);
        if (++kd.k >= kd.L) {
            kd.k = 0;
            if (kd.op == BO_REV) kd.S = idx + 1u - kd.L;   /* (on: the next stretch, just played, backwards) */
        }
        break;
    case BO_HALF: {
        int32_t f = kd.rf + kd.sp;
        m = dd_cub(kd.ri, kd.rf);
        kd.ri += (uint32_t)(f >> 16), kd.rf = f & 0xFFFF;
        if (idx - kd.ri > CAPN - 4096u) kd.ri = idx - kd.slen;
        break;
    }
    case BO_PREP: {
        int32_t f = kd.rf + kd.sp;
        uint32_t o = kd.ri - kd.S;
        m = dd_declick(dd_rd(kd.ri, kd.rf), o < kd.L ? o : 0u, kd.L);
        kd.ri += (uint32_t)(f >> 16), kd.rf = f & 0xFFFF;
        if (kd.ri - kd.S >= kd.L) kd.ri -= kd.L;
        if ((int32_t)(idx - kd.ri) < 4) kd.ri = kd.S;  /* (never ahead of the writer) */
        break;
    }
    case BO_STOP: {
        int32_t f = kd.rf + kd.sp;
        m = kb_cart(dd_cub(kd.ri, kd.rf), kd.sp >> 1);
        kd.ri += (uint32_t)(f >> 16), kd.rf = f & 0xFFFF;
        kd.sp = kd.sp > kd.acc ? kd.sp - kd.acc : 0;
        break;
    }
    case BO_SCR: {                                     /* the hand: a smooth stroke to the next place each quarter */
        uint32_t u = kd.u24 >> 9, sm = (((u * u) >> 15) * ((3u << 15) - 2u * u)) >> 15;   /* smoothstep, Q15 */
        int32_t a = (int32_t)(kd.from8 * kd.D / 8u), d = (int32_t)((kd.to8 - kd.from8) * (int32_t)kd.D / 8), off, v;
        uint32_t ri;
        off = (a << 15) + d * (int32_t)sm;
        v = off - kd.ps;
        kd.ps = off;
        ri = kd.rec + (uint32_t)(off >> 15);
        if ((int32_t)(idx - ri) < 8) ri = idx - 8u;    /* (the record ends at now) */
        m = kb_cart(dd_cub(ri, (off & 0x7FFF) << 1), v);
        kd.u24 += kd.du;
        if (kd.u24 >= 1u << 24) {                      /* the next stroke */
            uint8_t p;
            kd.u24 = 0;
            kd.from8 = kd.to8;
            if (kd.qi < 3u) kd.qi++;
            p = KB_PAT[kd.arg % KS_N][kd.qi];
            kd.to8 = (uint8_t)(p >> 1);
            kd.open = p & 1u;
        }
        break;
    }
    default:
        break;
    }
    *wl = *wr = m;
gate:
    *wl = ((*wl >> 3) * (kd.gg >> 3)) >> 9;
    *wr = ((*wr >> 3) * (kd.gg >> 3)) >> 9;
    if (kd.newop) {                                    /* a new operation: from where the last one was, quickly */
        kd.newop = 0;
        kd.dl = kd.ll - *wl, kd.dr = kd.lr - *wr;
    }
    *wl += kd.dl, *wr += kd.dr;
    kd.dl -= kd.dl >> 5, kd.dr -= kd.dr >> 5;
    kd.ll = *wl, kd.lr = *wr;
}

/* the effect taken over at index base: its record (the last attack) and seed */
static void beat_engage(uint32_t base)
{
    uint32_t beat = dd_beat(), L;
    kd.bseed = kg_rand(&kd.rnd);
    kd.op = BO_DRY;
    kd.open = 1;
    kd.newop = 0;
    kd.dl = kd.dr = 0;
    kd.ll = kd.lr = 0;
    kd.lpy = 0;
    kd.ps = 0;
    kd.from8 = kd.to8 = 0;
    kd.rec = kb_cue(base);
    L = kd.sub == DD_SCRATCH ? beat / 4u : beat;
    kd.S = kd.rec;
    kd.L0 = L < KB_OWN ? L : KB_OWN;
    kd.own = kd.sub == DD_SCRATCH || kd.sub == DD_SCRBAR || kd.sub == DD_SCRCUT || kd.sub == DD_ROLL;
}

/* ---- the DESTROY bank (K-GLITCH's kg_dd.c) */
/* the effect e takes over from the frame at index base */
static void dd_engage(int32_t e, uint32_t base)
{
    uint32_t beat = dd_beat(), I = kd.I, pos = dd_barpos(), L;
    kd.sub = (int8_t)e;
    kd.xf = 0;
    kd.own = 0;
    kd.rev = 0;
    kd.off = 0;
    kd.rep = 32767;
    kd.gg = 32767;
    switch (e) {
    case DD_LOOP: {
        static const uint8_t DIV[5] = {2, 4, 8, 16, 32};   /* of a bar */
        L = dd_cap(beat * 4u / DIV[I * 5u / 101u]);
        kd.k = pos % L;
        kd.S = base - kd.k;                     /* the last division: the first pass is the live sound */
        kd.L = kd.L0 = L;
        kd.own = 1;
        break;
    }
    case DD_STUT: {
        static const uint8_t DIV[4] = {2, 3, 4, 8};        /* repeats a beat */
        L = dd_cap(beat / DIV[I * 4u / 101u]);
        kd.k = pos % L;
        kd.S = base - kd.k;
        kd.L = kd.L0 = L;
        kd.own = 1;
        break;
    }
    case DD_BREP:
        L = dd_cap(beat >> (I * 3u / 101u));
        kd.k = 0;
        kd.S = base - L;                        /* what just played, again */
        kd.L = kd.L0 = L;
        kd.own = 1;
        break;
    case DD_REV:
        L = dd_cap(beat >> (I * 3u / 101u));
        kd.k = pos % L;
        kd.S = base - kd.k - L;                 /* the segment before, backwards */
        kd.L = kd.L0 = L;
        kd.rev = 1;
        break;
    case DD_PITCH:
        kd.pp = 0;
        kd.semi = 0;
        kd.ratio = 65536;
        break;
    case DD_STRETCH:
        kd.ri = base - DD_W;
        kd.rf = 0;
        kd.pp = 0;
        kd.ga = kd.ri - DD_W / 2u;
        kd.gb = kd.ri - DD_W / 2u;
        kd.ratio = I < 50u ? 49152 - (int32_t)I * 16384 / 50 : 32768 - ((int32_t)I - 50) * 16384 / 50;   /* 3/4 .. 1/4 */
        break;
    case DD_GATE:
        kd.i32 = 0xFFFFFFFFu;
        break;
    case DD_FILTER:
        memset(kd_fs, 0, sizeof kd_fs);
        kd.cut = kd.cut_to = 100 << 8;
        kd.reso = 40 + (int32_t)I * 80 / 100;
        break;
    case DD_RING:
        kd.ph = 0;
        kd.dph = (uint32_t)(30u << (I * 6u / 100u)) * (0xFFFFFFFFu / FS);   /* 30 Hz .. ~2 kHz */
        break;
    case DD_GLITCH:
        kd.hn = 0;
        kd.corrupt = kd.noise = 0;
        break;
    default:
        if (dd_isop(e))
            beat_engage(base);
        break;
    }
}

/* each 16th: what changes on the grid (s: the 16th of the bar) */
static void dd_tick(uint32_t base, uint32_t s)
{
    uint32_t I = kd.I, r = kg_rand(&kd.rnd), step = dd_beat() / 4u;
    if (kd.cur == DD_RAND || kd.cur == DD_TOTAL) {      /* a new effect each 16th */
        dd_engage((int32_t)(r % 14u), base);   /* (any of the first 14) */
        kd.mode = (uint8_t)(kd.cur == DD_TOTAL && (r >> 8) % 3u == 0u);   /* TOTAL: a ring modulator on top */
        r = kg_rand(&kd.rnd);
    }
    switch (kd.sub) {
    case DD_BSKIP:                                     /* drop it, repeat the one before, or play it */
        kd.mode = r % 100u < I * 6u / 10u ? (I > 50u && (r >> 8) % 2u ? 2u : 1u) : 0u;
        if (kd.mode == 2u) {
            kd.S = base - step;
            kd.k = 0;
            kd.L = kd.L0 = dd_cap(step);
        }
        break;
    case DD_FILTER:
        kd.cut_to = (int32_t)(36u + r % (40u + I * 50u / 100u)) << 8;
        break;
    case DD_PITCH:
        if (I < 34u) {                                 /* a staircase up an octave */
            static const int8_t ST[8] = {0, 2, 4, 5, 7, 9, 11, 12};
            kd.semi = ST[(kd.t / (step + 1u)) % 8u];
        } else if (I < 67u) {                          /* random jumps */
            static const int8_t J[7] = {-12, -7, -5, 0, 5, 7, 12};
            kd.semi = J[r % 7u];
        } else {                                       /* a dive: an octave up down to two down, again */
            kd.semi = (int8_t)(kd.semi <= -24 ? 12 : kd.semi - 3);
        }
        kd.ratio = dd_semi(kd.semi);
        break;
    case DD_MICRO: {
        uint32_t G = 2205u - I * 1764u / 100u;         /* 50 .. 10 ms */
        kd.S = base - G;
        kd.k = 0;
        kd.L = kd.L0 = G;
        kd.own = 1;
        break;
    }
    case DD_GLITCH:
        if (r % 100u < 20u + I / 2u) {                  /* a chunk of the past instead of now */
            kd.corrupt = 256u + (r >> 8) % (256u + I * 16u);
            kd.csrc = base - 2048u - (r >> 16) % (CAPN / 2u);
        }
        if (I > 60u && (r >> 4) % 100u < I - 60u)
            kd.noise = 64u + (r >> 12) % 256u;
        break;
    default:
        break;
    }
    if (dd_isop(kd.sub))                               /* BEAT, SCRATCH: the 16th's operation */
        beat_slot(s, base);
}

/* the wet frame of the effect playing, for the dry x, y; idx the capture's index of this frame */
static void dd_sample(int32_t x, int32_t y, uint32_t idx, int32_t *wl, int32_t *wr)
{
    uint32_t I = kd.I;
    int32_t m;
    switch (kd.sub) {
    case DD_LOOP: {
        uint32_t p = kd.off + kd.k;
        if (p >= kd.L) p -= kd.L;
        m = dd_declick(ring_at(kd.S + (kd.rev ? kd.L - 1u - p : p)), kd.k, kd.L);
        if (++kd.k >= kd.L) {
            uint32_t r = kg_rand(&kd.rnd);
            kd.k = 0;
            kd.rev = (uint8_t)(I > 60u && r % 2u);
            kd.off = I > 85u ? ((r >> 8) % 4u) * (kd.L / 4u) : 0u;
        }
        *wl = *wr = m;
        return;
    }
    case DD_STUT:
    case DD_BREP:
    case DD_MICRO:
        m = dd_declick(ring_at(kd.S + kd.k), kd.k, kd.L);
        if (kd.sub == DD_STUT)                        /* each repeat fades a little */
            m = ((m >> 2) * ((65536 - (int32_t)((kd.k * kd.fmul) >> 8)) >> 2)) >> 12;
        else if (kd.sub == DD_BREP)
            m = (m * kd.rep) >> 15;
        if (++kd.k >= kd.L) {
            kd.k = 0;
            if (kd.sub == DD_STUT && I > 75u) {       /* bursts */
                uint32_t r = kg_rand(&kd.rnd) % 3u;
                kd.L = dd_cap(kd.L0 >> r);
            } else if (kd.sub == DD_BREP) {
                kd.rep = kd.rep * (int32_t)(500u - I) / 500;
                if (kd.rep < 16384) kd.rep = 16384;   /* (never below half) */
                if (I > 70u && kd.L > kd.L0 / 8u)
                    kd.L = dd_cap(kd.L / 2u);
            }
        }
        *wl = *wr = m;
        return;
    case DD_REV:
        m = dd_declick(ring_at(kd.S + (kd.rev ? kd.L - 1u - kd.k : kd.k)), kd.k, kd.L);
        if (++kd.k >= kd.L) {
            kd.k = 0;
            kd.S += kd.L;                             /* the next segment (already written: one behind) */
            kd.rev = (uint8_t)(I <= 70u || kg_rand(&kd.rnd) % 3u != 0u);
        }
        *wl = *wr = m;
        return;
    case DD_PITCH: {                                   /* two taps sliding through a delay, crossfaded */
        uint32_t W16 = DD_W << 16, pa = kd.pp, pb = (kd.pp + W16 / 2u) & (W16 - 1u);
        int32_t a = ring_at(idx - 64u - (pa >> 16)), b = ring_at(idx - 64u - (pb >> 16));
        int32_t wa = (int32_t)((pa >> 16) < DD_W / 2u ? (pa >> 16) : DD_W - (pa >> 16));
        m = (a * wa + b * ((int32_t)DD_W / 2 - wa)) >> 10;
        kd.pp = (uint32_t)((int32_t)kd.pp + (65536 - kd.ratio)) & (W16 - 1u);
        *wl = *wr = m;
        return;
    }
    case DD_STRETCH: {                                 /* two grains at normal speed from a point that moves slower */
        uint32_t q = kd.pp, qb = (q + DD_W / 2u) & (DD_W - 1u), back;
        int32_t wa = (int32_t)(q < DD_W / 2u ? q : DD_W - q), f;
        if (q == 0u) kd.ga = kd.ri - DD_W / 2u;
        if (qb == 0u) kd.gb = kd.ri - DD_W / 2u;
        m = (ring_at(kd.ga + q) * wa + ring_at(kd.gb + qb) * ((int32_t)DD_W / 2 - wa)) >> 10;
        kd.pp = (q + 1u) & (DD_W - 1u);
        f = kd.rf + kd.ratio;
        kd.ri += (uint32_t)(f >> 16);
        kd.rf = f & 0xFFFF;
        back = idx - kd.ri;
        if (back > CAPN - 4u * DD_W) kd.ri = idx - 2u * DD_W;   /* too far behind: back near now */
        *wl = *wr = m;
        return;
    }
    case DD_GATE:
    case DD_BSKIP:
        if (kd.sub == DD_BSKIP && kd.mode == 2u) {     /* the 16th before, again */
            m = dd_declick(ring_at(kd.S + kd.k), kd.k, kd.L);
            if (++kd.k >= kd.L) kd.k = 0;
            *wl = *wr = m;
            return;
        }
        *wl = ((x >> 3) * (kd.gg >> 3)) >> 9;
        *wr = ((y >> 3) * (kd.gg >> 3)) >> 9;
        return;
    case DD_FILTER: {                                  /* (its coefficients: once a chunk, kg_begin) */
        *wl = x, *wr = y;
        return;
    }
    case DD_RING: {
        int32_t s = sine(kd.ph);
        kd.ph += kd.dph;
        *wl = ((x >> 2) * (s >> 2)) >> 11;
        *wr = ((y >> 2) * (s >> 2)) >> 11;
        return;
    }
    case DD_FUZZ: {
        int32_t d = 256 + (int32_t)I * 40, v[2] = {x, y}, j;
        for (j = 0; j < 2; j++) {
            int32_t u = (kg_clamp(v[j], -65535, 65535) * (d >> 2)) >> 6;
            if (u > 14000) u = 14000 + ((u - 14000) >> 3);
            if (u < -14000) u = -14000 + ((u + 14000) >> 3);
            v[j] = (kg_clamp(u, -20000, 20000) * 26214) >> 15;   /* (x 0.8: about as loud as the dry sound) */
        }
        *wl = v[0], *wr = v[1];
        return;
    }
    case DD_GLITCH: {
        int32_t a = x, b = y, sh = 4 + (int32_t)I / 10;
        if (kd.corrupt) {                              /* a chunk of the past */
            a = b = ring_at(kd.csrc++);
            kd.corrupt--;
        }
        if (kd.noise) {
            kd.noise--;
            a = b = (int32_t)(kg_rand(&kd.rnd) >> 18) - 8192;   /* (a quarter of full scale) */
        }
        if (kd.hn == 0u) kd.hl = a, kd.hr = b;          /* (hold: 1 + I / 6 frames) */
        if (++kd.hn > I / 6u) kd.hn = 0;
        *wl = (kd.hl >> sh) << sh;
        *wr = (kd.hr >> sh) << sh;
        return;
    }
    default:
        if (dd_isop(kd.sub)) {
            beat_sample(x, y, idx, wl, wr);
            return;
        }
        *wl = x, *wr = y;
        return;
    }
}

/* ---- the FILTER page (K-GLITCH's kg_filter.c) */
enum { KF_OFF, KF_LP, KF_HP, KF_BP, KF_NOTCH, KF_DRIVE, KF_COMBP, KF_COMBN, KF_METAL, KF_VOWEL, KF_PHASER, KF_RESON, KF_N };
#define KF_BUF 2048u
static int16_t kf_buf[KF_BUF];                    /* COMB: left 0..1023, right 1024..2047; METAL: four of 512 */
static uint8_t kf_type, kf_note = 72, kf_res = 64, kf_move;   /* main: TYPE, FREQ (a MIDI note), RESO 0..127, MOVE 0..100 */
static volatile uint8_t kf_note_vis;              /* ISR: the cutoff now, a note (the page draws it) */
typedef struct { int32_t a1, a2, a3, k; } kf_c;
static struct {
    int8_t cur;                                    /* the type playing (fading to TYPE) */
    int32_t g;                                     /* its wet gain 0..32767 */
    int32_t s[2][3][2];                            /* per side: up to three filters' two states */
    int32_t ap[2][4][2];                           /* PHASER: per side, four stages: x[n-1], y[n-1] */
    int32_t fbk[2];
    uint32_t w;                                    /* the comb buffer's write position */
    kf_c c, c2, c3;                                /* this chunk's coefficients */
    uint32_t cd, md[4];
    int32_t cfb, cgain, mfb, pha, res;
} kf = {.cur = KF_OFF};

/* the trapezoidal state-variable filter (A. Simper's), cut 0..127 << 8 (30 Hz .. 16 kHz), reso 0..127 */
static void kf_coef_idx(kf_c *c, int32_t cut, int32_t reso)   /* cut: the filter's index, 0..127 << 8 */
{
    int32_t i, g, den;
    cut = kg_clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    c->k = 8192 - reso * 7600 / 127;              /* the damping, 2.0 .. ~0.15 (Q12) */
    g = SVF_G[i];
    if (i < 127)                                   /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + c->k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static void kf_coef(kf_c *c, int32_t note8, int32_t reso)   /* note8: a MIDI note << 8 (the index: 30 Hz at 0, 0.86 semitone a step) */
{
    kf_coef_idx(c, ((note8 - 5734) * 298) >> 8, reso);
}
static int32_t kf_svf(const kf_c *c, int32_t in, int32_t *st, int32_t *bp)   /* the low-pass; *bp the band-pass */
{
    int32_t v3 = in - st[1];
    int32_t v1 = (c->a1 * st[0] + c->a2 * v3) >> 13;
    int32_t v2 = st[1] + ((c->a2 * st[0] + c->a3 * v3) >> 13);
    st[0] = kg_clamp(2 * v1 - st[0], -150000, 150000);
    st[1] = kg_clamp(2 * v2 - st[1], -150000, 150000);
    *bp = v1;
    return v2;
}
static int32_t kf_softclip(int32_t x)              /* tanh(x / 16384) */
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
static uint32_t kf_period(int32_t note8)           /* frames a period of note8 (MIDI << 8) */
{
    uint32_t inc = pitch_inc(note8);
    return inc ? 0xFFFFFFFFu / inc : 1023u;
}

/* once a chunk: the type, the cutoff with its LFO, the coefficients */
static void kf_begin(void)
{
    static const int16_t VF1[5] = {78 * 256 + 26, 72 * 256 + 154, 61 * 256 + 0, 73 * 256 + 230, 62 * 256 + 205};   /* A E I O U */
    static const int16_t VF2[5] = {85 * 256 + 26, 93 * 256 + 179, 97 * 256 + 128, 79 * 256 + 128, 80 * 256 + 26};
    static const uint16_t MR[4] = {32768, 23921, 19005, 13435};   /* METAL: the combs' lengths, Q15 (inharmonic) */
    int32_t want = kf_type < KF_N ? kf_type : KF_OFF, res = kf_res, note8;
    if (kf.cur == KF_OFF && want == KF_OFF)
        return;
    if (kf.cur == KF_OFF) {                        /* a new type: from silent states */
        kf.cur = (int8_t)want;
        kf.g = 0;
        memset(kf.s, 0, sizeof kf.s);
        memset(kf.ap, 0, sizeof kf.ap);
        kf.fbk[0] = kf.fbk[1] = 0;
        memset(kf_buf, 0, sizeof kf_buf);
    }
    note8 = (int32_t)kf_note << 8;
    if (kf_move) {                                 /* MOVE: +-3 octaves at 100 %, a cycle a bar */
        uint32_t bar = dd_beat() * 4u + 1u, ph = (uint32_t)(((uint64_t)(dd_barpos() % bar) << 32) / bar);
        note8 += (sine(ph) * (int32_t)kf_move * 36 / 100) >> 7;
    }
    note8 = kg_clamp(note8, 12 << 8, 124 << 8);
    kf_note_vis = (uint8_t)(note8 >> 8);
    kf.res = res;
    switch (kf.cur) {
    case KF_DRIVE:
        kf_coef(&kf.c, note8, 40 + res * 80 / 127);
        break;
    case KF_VOWEL: {
        int32_t p = (note8 >> 8) * 4 * 256 / 127, v = p >> 8, f = p & 255;
        if (v > 3) v = 3, f = 255;
        kf_coef(&kf.c, VF1[v] + (((VF1[v + 1] - VF1[v]) * f) >> 8), 70 + res * 50 / 127);
        kf_coef(&kf.c2, VF2[v] + (((VF2[v + 1] - VF2[v]) * f) >> 8), 70 + res * 50 / 127);
        break;
    }
    case KF_RESON:
        kf_coef(&kf.c, note8, 118 + res * 9 / 127);
        kf_coef(&kf.c2, note8 + (12 << 8), 118 + res * 9 / 127);
        kf_coef(&kf.c3, note8 + (19 << 8), 118 + res * 9 / 127);
        break;
    default:
        kf_coef(&kf.c, note8, res);
        break;
    }
    kf.cd = 8u;
    kf.cfb = kf.mfb = kf.pha = 0;
    kf.cgain = 32768;
    if (kf.cur == KF_COMBP || kf.cur == KF_COMBN) {
        uint32_t cd = kf_period(note8);
        while (cd > 1023u) cd >>= 1;                  /* (below 43 Hz: octaves up, still in tune) */
        kf.cd = cd < 8u ? 8u : cd;
        kf.cfb = (12000 + res * 19000 / 127) * (kf.cur == KF_COMBN ? -1 : 1);
        kf.cgain = 32768 - (kf.cfb < 0 ? -kf.cfb : kf.cfb) / 2;
    } else if (kf.cur == KF_METAL) {
        uint32_t pp = kf_period(note8), k;
        for (k = 0; k < 4u; k++) {
            uint32_t d = (pp * MR[k]) >> 15;
            while (d > 511u) d >>= 1;
            kf.md[k] = d < 6u ? 6u : d;
        }
        kf.mfb = 20000 + res * 11000 / 127;
    } else if (kf.cur == KF_PHASER)
        kf.pha = kg_clamp(30000 - ((note8 - (24 << 8)) * 27000 / (100 << 8)), 1500, 31000);
}

/* a frame through the filter */
static void kf_frame(int32_t *pl, int32_t *pr)
{
    int32_t x[2] = {*pl, *pr}, y[2] = {*pl, *pr}, target = (int32_t)kf_type == kf.cur ? 32767 : 0, ch, nch = 2;
    if (kf.g < target) kf.g = kf.g + 512 > target ? target : kf.g + 512;
    else if (kf.g > target) kf.g = kf.g - 512 < target ? target : kf.g - 512;
    if (cpu_shed >= 3u && kf.cur != KF_METAL) {   /* (the load governor at its last step: one side, half the work) */
        x[0] = x[1] = (x[0] + x[1]) >> 1;
        nch = 1;
    }
    for (ch = 0; ch < nch; ch++) {
        int32_t in = x[ch], bp, lp, o = in;
        switch (kf.cur) {
        case KF_LP: o = kf_svf(&kf.c, in, kf.s[ch][0], &bp); break;
        case KF_HP: lp = kf_svf(&kf.c, in, kf.s[ch][0], &bp); o = in - ((kf.c.k * bp) >> 12) - lp; break;
        case KF_BP: kf_svf(&kf.c, in, kf.s[ch][0], &bp); o = (bp * 3) >> 1; break;
        case KF_NOTCH: kf_svf(&kf.c, in, kf.s[ch][0], &bp); o = in - ((kf.c.k * bp) >> 12); break;
        case KF_DRIVE: o = kf_svf(&kf.c, kf_softclip(in * 3) >> 1, kf.s[ch][0], &bp); break;
        case KF_COMBP:
        case KF_COMBN: {
            uint32_t base = (uint32_t)ch * 1024u;
            int32_t z = kf_buf[base + ((kf.w - kf.cd) & 1023u)] * 2;
            o = in + ((z * kf.cfb) >> 15);
            kf_buf[base + (kf.w & 1023u)] = (int16_t)kg_clamp(o >> 1, -32768, 32767);
            o = ((o >> 2) * kf.cgain) >> 13;
            break;
        }
        case KF_METAL: {
            uint32_t k;
            int32_t z[4];
            if (ch) break;                     /* (both sides at once, on the left's turn) */
            for (k = 0; k < 4u; k++) {
                uint32_t b = k * 512u;
                z[k] = ((x[0] + x[1]) >> 2) + ((kf_buf[b + ((kf.w - kf.md[k]) & 511u)] * 2 * kf.mfb) >> 15);
                kf_buf[b + (kf.w & 511u)] = (int16_t)kg_clamp(z[k] >> 1, -32768, 32767);
            }
            y[0] = (x[0] * 6 + (z[0] + z[2] - z[1]) * 4) >> 4;   /* (each side its own mix of the four: wide) */
            y[1] = (x[1] * 6 + (z[1] + z[3] - z[2]) * 4) >> 4;
            break;
        }
        case KF_VOWEL: {
            int32_t b2;
            kf_svf(&kf.c, in, kf.s[ch][0], &bp);
            kf_svf(&kf.c2, in, kf.s[ch][1], &b2);
            o = bp * 2 + ((b2 * 3) >> 1);
            break;
        }
        case KF_PHASER: {
            int32_t v = in + ((kf.fbk[ch] * kf.res) >> 8), k;
            for (k = 0; k < 4; k++) {          /* y = a (x - y[n-1]) + x[n-1] */
                int32_t yy = ((kf.pha * (v - kf.ap[ch][k][1])) >> 15) + kf.ap[ch][k][0];
                kf.ap[ch][k][0] = v;
                kf.ap[ch][k][1] = kg_clamp(yy, -150000, 150000);
                v = kf.ap[ch][k][1];
            }
            kf.fbk[ch] = v >> 1;
            o = (in + v) >> 1;
            break;
        }
        case KF_RESON: {
            int32_t b2, b3;
            kf_svf(&kf.c, in >> 1, kf.s[ch][0], &bp);
            kf_svf(&kf.c2, in >> 1, kf.s[ch][1], &b2);
            kf_svf(&kf.c3, in >> 1, kf.s[ch][2], &b3);
            o = (in >> 2) + bp + b2 + (b3 >> 1);
            break;
        }
        default:
            break;
        }
        if (kf.cur != KF_METAL) y[ch] = o;
    }
    if (nch == 1) y[1] = y[0];
    kf.w++;
    *pl = kg_clamp(x[0] + (((kg_clamp(y[0] - x[0], -(1 << 20), 1 << 20) >> 3) * (kf.g >> 3)) >> 9), -65535, 65535);
    *pr = kg_clamp(x[1] + (((kg_clamp(y[1] - x[1], -(1 << 20), 1 << 20) >> 3) * (kf.g >> 3)) >> 9), -65535, 65535);
}
static void kf_end(void)
{
    if (kf.cur != KF_OFF && kf.g == 0 && (int32_t)kf_type != kf.cur)
        kf.cur = KF_OFF;                           /* faded out: the next type (or none) on the next chunk */
}

/* ---- once a chunk, before its frames: want is the effect that should play (-1 none), sint its intensity + 1 (0: its
 * own); base the capture's index of the chunk's first frame */
static int8_t kd_want = -1;
static kf_c kd_fc;                                /* FILTER CHAOS: its coefficients this chunk */
static void kg_begin(int32_t want, uint32_t sint, uint32_t base)
{
    uint32_t s16, key;
    kd_want = (int8_t)want;
    kd_sint = (uint8_t)sint;
    kf_begin();
    if (kd.cur < 0 && want < 0) {
        kg_on = -1;
        kg_anyown = 0;
        return;
    }
    if (kd.cur < 0) {                                  /* a new one */
        kd.cur = (int8_t)want;
        kd.g = 0;
        kd.t = 0;
        kd.s16 = 0xFFFFFFFFu;
        kd.I = kd_sint ? kd_sint - 1u : kd_int[(uint32_t)want % DD_N];
        dd_engage(want == DD_RAND || want == DD_TOTAL ? DD_STUT : want, base);
    }
    kd.I = kd_sint ? kd_sint - 1u : kd_int[(uint32_t)kd.cur % DD_N];
    if (kd.I > 100u) kd.I = 100u;
    if (sq_play) {                                     /* the 16th: the sequencer's row, else since it started */
        key = sq_rowtick;
        s16 = sq_row & 15u;
    } else {
        key = kd.t / (g_rowlen + 1u);
        s16 = key & 15u;
    }
    if (key != kd.s16) {
        kd.s16 = key;
        dd_tick(base, s16);
    }
    kd.fmul = kd.L ? (13000u << 8) / kd.L : 0u;
    if (kd.sub == DD_GATE) {                           /* the slot of the bar: open or shut */
        uint32_t per = kd.I > 50u ? 32u : 16u, slot = dd_barpos() * per / (dd_beat() * 4u + 1u);
        if (slot < kd.i32 || kd.i32 == 0xFFFFFFFFu) {  /* a new bar: a new pattern */
            uint32_t j;
            kd.mask = 0;
            for (j = 0; j < per; j++)
                if ((j % (per / 4u)) == 0u || kg_rand(&kd.rnd) % 100u >= 20u + kd.I * 6u / 10u)
                    kd.mask |= 1u << j;
        }
        kd.i32 = slot;
    }
    if (dd_isgate(kd.sub))                             /* BEAT's gates: open or shut, on the bar */
        kd.open = (uint8_t)beat_gate(kd.sub, dd_barpos(), kd.I);
    if (kd.sub == DD_FILTER) {                         /* the cutoff glides to its 16th's (a note << 8) */
        kd.cut += (kd.cut_to - kd.cut) / 8;
        kf_coef_idx(&kd_fc, kd.cut, kd.reso);
    }
    kg_on = kd.cur;
    kg_anyown = (uint8_t)(kd.own != 0);
}

/* a frame of the live bus (after the capture took it): the effect playing, then the FILTER; idx its capture index */
static void kg_frame(int32_t *pl, int32_t *pr, uint32_t idx)
{
    if (kd.cur >= 0) {
        int32_t x = *pl, y = *pr, wl, wr, target = kd_want == kd.cur ? 32767 : 0;
        if (kd.g < target) kd.g = kd.g + 512 > target ? target : kd.g + 512;
        else if (kd.g > target) kd.g = kd.g - 512 < target ? target : kd.g - 512;
        if (kd.sub == DD_GATE || kd.sub == DD_BSKIP || dd_isop(kd.sub)) {   /* the gate's gain glides (64 frames) */
            int32_t open = kd.sub == DD_GATE ? (kd.i32 < 32u && (kd.mask >> kd.i32 & 1u)) : dd_isop(kd.sub) ? kd.open : kd.mode != 1u;
            int32_t gt = open ? 32767 : 0;
            kd.gg += gt > kd.gg ? (gt - kd.gg > 512 ? 512 : gt - kd.gg) : -(kd.gg - gt > 512 ? 512 : kd.gg - gt);
        }
        if (kd.sub == DD_FILTER) {
            int32_t fl = kf_svf(&kd_fc, x >> 2, kd_fs[0], &wl) * 4, fr = kf_svf(&kd_fc, y >> 2, kd_fs[1], &wr) * 4;
            wl = fl, wr = fr;
        } else
            dd_sample(x, y, idx, &wl, &wr);
        if (kd.cur == DD_TOTAL) {                      /* TOTAL: crushed, sometimes ring modulated */
            int32_t sh = 6 + (int32_t)kd.I / 14;
            wl = (wl >> sh) << sh, wr = (wr >> sh) << sh;
            if (kd.mode) {
                int32_t s = sine(kd.ph2);
                kd.ph2 += 600u * (0xFFFFFFFFu / FS);   /* (600 Hz) */
                wl = ((wl >> 2) * (s >> 2)) >> 11, wr = ((wr >> 2) * (s >> 2)) >> 11;
            }
        }
        if (kd.xf < 64) {                              /* a sub-effect just came in: from the dry sound */
            wl = x + (((wl - x) * kd.xf) >> 6);
            wr = y + (((wr - y) * kd.xf) >> 6);
            kd.xf++;
        }
        wl = kg_clamp(wl - x, -(1 << 20), 1 << 20), wr = kg_clamp(wr - y, -(1 << 20), 1 << 20);
        *pl = kg_clamp(x + (((wl >> 3) * (kd.g >> 3)) >> 9), -65535, 65535);
        *pr = kg_clamp(y + (((wr >> 3) * (kd.g >> 3)) >> 9), -65535, 65535);
        kd.t++;
    }
    if (kf.cur != KF_OFF)
        kf_frame(pl, pr);
}

/* once a chunk, after its frames */
static void kg_end(void)
{
    if (kd.cur >= 0 && kd.g == 0 && kd_want != kd.cur) {   /* faded out: the next one starts on the next chunk */
        kd.cur = -1;
        kd.own = 0;
        kg_anyown = 0;
    }
    kf_end();
}

/* ---- what the LIVE page shows of them: DESTROY (0..15) and BEAT (16..31), as LFX */
static const lfx_t KGX[DD_N] = {
    {{"BOUCLE", "LOOP", "BUCLE"}, {"DETRUIT", "DESTROY", "DESTRUYE"}, ICO_LOOP},
    {{"BEGAIE", "STUTTER", "TARTAMU"}, {"X", "X", "X"}, ICO_STUT},
    {{"GLITCH", "GLITCH", "GLITCH"}, {"", "", ""}, ICO_PIXEL},
    {{"SCRATCH", "SCRATCH", "SCRATCH"}, {"MAIN", "HAND", "MANO"}, ICO_DISC},
    {{"ENVERS", "REVERSE", "REVES"}, {"X", "X", "X"}, ICO_REV},
    {{"PITCH", "PITCH", "TONO"}, {"FOU", "MAD", "LOCO"}, ICO_UP},
    {{"HACHE", "GATE", "GATE"}, {"X", "X", "X"}, ICO_GATE},
    {{"FILTRE", "FILTER", "FILTRO"}, {"CHAOS", "CHAOS", "CAOS"}, ICO_LPF},
    {{"TEMPS", "BEAT", "PULSO"}, {"REPETE", "REPEAT", "REPITE"}, ICO_AGAIN},
    {{"TEMPS", "BEAT", "PULSO"}, {"SAUTE", "SKIP", "SALTA"}, ICO_NOBEAT},
    {{"MICRO", "MICRO", "MICRO"}, {"BEGAIE", "STUTTER", "TARTAMU"}, ICO_STUT},
    {{"ETIRE", "STRETCH", "ESTIRA"}, {"TEMPS", "TIME", "TIEMPO"}, ICO_TAPE},
    {{"ANNEAU", "RING", "ANILLO"}, {"MOD", "MOD", "MOD"}, ICO_RING},
    {{"FUZZ", "FUZZ", "FUZZ"}, {"", "", ""}, ICO_DIST},
    {{"HASARD", "RANDOM", "AZAR"}, {"", "", ""}, ICO_DICE},
    {{"CHAOS", "TOTAL", "CAOS"}, {"TOTAL", "CHAOS", "TOTAL"}, ICO_CRUSH},
    {{"HASARD", "RANDOM", "AZAR"}, {"BEGAIE", "STUTTER", "TARTAMU"}, ICO_STUT},
    {{"HASARD", "RANDOM", "AZAR"}, {"MESURE", "BAR", "COMPAS"}, ICO_DICE},
    {{"HASARD", "RANDOM", "AZAR"}, {"BREAK", "FILL", "RELLENO"}, ICO_ACCEL},
    {{"ROULEAU", "ROLL", "REDOBLE"}, {"MONTE", "UP", "SUBE"}, ICO_ACCEL},
    {{"SAUTE", "SKIP", "SALTA"}, {"CROCHE", "8TH", "CORCHEA"}, ICO_NOBEAT},
    {{"BAFOUILL", "STAMMER", "TITUBEO"}, {"3-3-2", "3-3-2", "3-3-2"}, ICO_STUT},
    {{"HACHE", "GATE", "GATE"}, {"1/8", "1/8", "1/8"}, ICO_GATE},
    {{"HACHE", "GATE", "GATE"}, {"1/16", "1/16", "1/16"}, ICO_GATE},
    {{"HACHE", "GATE", "GATE"}, {"3-3-2", "3-3-2", "3-3-2"}, ICO_GATE},
    {{"HACHE", "GATE", "GATE"}, {"RAMPE", "RAMP", "RAMPA"}, ICO_GATE},
    {{"ENVERS", "REVERSE", "REVES"}, {"TEMPS", "BEAT", "PULSO"}, ICO_REV},
    {{"ENVERS", "REVERSE", "REVES"}, {"CONTRE", "OFFBEAT", "CONTRA"}, ICO_REV},
    {{"SCRATCH", "SCRATCH", "SCRATCH"}, {"MESURE", "BAR", "COMPAS"}, ICO_DISC},
    {{"SCRATCH", "SCRATCH", "SCRATCH"}, {"COUPE", "CUT", "CORTE"}, ICO_DISC},
    {{"STOP", "TAPE", "CINTA"}, {"K7", "STOP", "PARADA"}, ICO_TAPE},
    {{"DEMI", "HALF", "MEDIO"}, {"TEMPS", "TIME", "TIEMPO"}, ICO_DIVE},
};
static const char *const KF_NAMES[KF_N] = {"OFF", "LP", "HP", "BP", "NOTCH", "DRIVE", "COMB+", "COMB-", "METAL", "VOWEL",
                                           "PHASER", "RESON"};
