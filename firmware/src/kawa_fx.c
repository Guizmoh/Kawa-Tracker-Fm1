/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the live effects (as a Pocket Operator's: hold, and it happens; let go, and it stops) and the
 * master: the echo, the reverb, the master's effect, the clip. Runs in the audio interrupt.
 *
 * The live effects play on a bus: the whole mix (TARGET: MASTER) or one track (TARGET: a track; the
 * others go on untouched). The buffer effects (loops, stutters, scratches, reverse, tape stop, dive,
 * freeze, the roll) read a capture of the last 0.74 s of that bus. They lock to the beat: a loop starts
 * on the last 1/16 (1/12, 1/32, 1/64) and the first pass is the live sound, then it repeats. Only one
 * plays at a time: the last one pressed. The others stack: crush, decimate, filters, gates, ring, echo,
 * distortion, wobble, vinyl; octave, unison, 6/8, mutes are applied by the voices and the sequencer. */

#define CAPN 32768u                             /* capture: frames (0.74 s), L | R << 16 */
#define CAPM (CAPN - 1u)
#define ECHON 32768u
static uint32_t cap[CAPN] POOL;
static int16_t echo_buf[ECHON] POOL;
static uint32_t afr;                            /* frames since boot */
static uint32_t echo_w;
static int32_t echo_lp;

#define BF_MASK ((1u << LF_LOOP16) | (1u << LF_LOOP12) | (1u << LF_LOOP32) | (1u << LF_LOOP64) | (1u << LF_STUT4) | \
                 (1u << LF_STUT3) | (1u << LF_SCR) | (1u << LF_SCRF) | (1u << LF_REV) | (1u << LF_ROLLUP) | \
                 (1u << LF_TAPE) | (1u << LF_DIVE) | (1u << LF_FREEZE))
static struct {
    uint8_t act, id;                            /* a buffer effect plays; which (LF_...) */
    uint8_t frozen, k;                          /* the capture is held; repeats so far */
    uint32_t S, L, E, t;                        /* segment start, length, the press (absolute frames); frames since */
    uint32_t p, v;                              /* varispeed: position in the segment (Q16 frames), speed (Q16) */
    int32_t mix, mix_to;                        /* 0 live .. 32767 effect */
} bf;
static uint32_t lfx_prev;
static uint8_t lpf_note = 127, hpf_note = 0;    /* the sweeps, MIDI note of the cutoff (the FX page draws them) */
static int32_t svf_lp[2], svf_bp[2];
static uint32_t lfo_ph, ring_ph;
static int32_t hold_l, hold_r, gate_g = 32767;
static uint32_t hold_n;
static uint8_t lv_target;                       /* the bus the live effects play on now: 0 the mix, 1..8 a track */

static uint32_t cap_rd(uint32_t a) { return cap[a & CAPM]; }
static int32_t cap_l(uint32_t w) { return (int16_t)(w & 0xFFFFu); }
static int32_t cap_r(uint32_t w) { return (int16_t)(w >> 16); }

/* frames back to the last grid line of g frames (the sequencer's beat when it plays) */
static uint32_t since_grid(uint32_t g) { return g ? g_beatpos % g : 0u; }

static void bf_engage(uint32_t id)
{
    uint32_t beat = g_rowlen * 4u, L;
    if (beat > CAPN - 4096u)
        beat = (CAPN - 4096u) & ~3u;
    bf.act = 1;
    bf.id = (uint8_t)id;
    bf.E = afr;
    bf.t = 0;
    bf.k = 0;
    bf.p = 0;
    bf.v = 65536u;
    bf.frozen = 0;
    switch (id) {
    case LF_LOOP16: L = beat / 4u; break;
    case LF_LOOP12: L = beat / 3u; break;
    case LF_LOOP32: L = beat / 8u; break;
    case LF_LOOP64: L = beat / 16u; break;
    case LF_STUT4: L = beat / 4u; break;
    case LF_STUT3: L = beat / 3u; break;
    case LF_ROLLUP: L = beat / 2u; break;
    case LF_DIVE: L = beat / 4u; break;
    case LF_SCR: case LF_SCRF: L = beat / 4u; break;
    case LF_REV: L = beat / 2u; break;
    case LF_FREEZE: L = 2048u; break;
    default: L = beat; break;
    }
    if (L < 64u)
        L = 64u;
    bf.L = L;
    if (id == LF_SCR || id == LF_SCRF || id == LF_REV || id == LF_FREEZE) {
        bf.S = afr - L;                         /* what just played */
        bf.frozen = 1;
    } else if (id == LF_TAPE)
        bf.S = afr - 1u;                        /* (one frame behind: the next one is not captured yet) */
    else
        bf.S = afr - since_grid(L);             /* the last grid line: the first pass is the live sound */
    bf.mix_to = 32767;
}

/* once per block: which effects sound, what the voices must know */
static void fx_block(uint32_t on)
{
    uint32_t newb = on & ~lfx_prev & BF_MASK, t;
    uint8_t mel = 0, beat = 0;
    if (lfx_target != lv_target) {              /* another bus: what it held is not this one's */
        lv_target = lfx_target <= NTRK ? lfx_target : 0u;
        bf.act = 0;
        bf.mix = bf.mix_to = 0;
        svf_lp[0] = svf_lp[1] = svf_bp[0] = svf_bp[1] = 0;
    }
    lfx_on = on;
    lv_mask = (uint8_t)(lv_target ? 1u << (lv_target - 1u) : 0xFFu);
    lv_transpose = (on >> LF_OCTUP & 1u ? 12 : 0) - (on >> LF_OCTDN & 1u ? 12 : 0);
    lv_uni = on >> LF_UNILO & 1u ? 2u : on >> LF_UNI & 1u ? 1u : 0u;
    for (t = 0; t < NTRK; t++) {
        if (IS_TONAL(t)) mel |= (uint8_t)(1u << t);
        else beat |= (uint8_t)(1u << t);
    }
    lv_mute = (uint8_t)(prj.mute | live_mute | (on >> LF_NOMEL & 1u ? mel : 0u) | (on >> LF_NOBEAT & 1u ? beat : 0u));
    if (newb) {                                 /* a new buffer effect: the lowest new one takes over */
        uint32_t id = 0;
        while (!(newb >> id & 1u))
            id++;
        if (bf.act)                             /* (a switch: no fade, the new one starts from the live sound) */
            bf.mix = 0;
        bf_engage(id);
    } else if (bf.act && bf.mix_to && !(on >> bf.id & 1u)) {
        uint32_t rest = on & BF_MASK, id = 0;
        if (rest) {
            while (!(rest >> id & 1u))
                id++;
            bf_engage(id);
        } else
            bf.mix_to = 0;                      /* let go: back to live */
    }
    lfx_prev = on;
}

/* the buffer effect's sample for the frame now (L, R) */
static void bf_read(int32_t *l, int32_t *r)
{
    uint32_t t = bf.t, w = 0, L = bf.L;
    int32_t g = 32767;
    switch (bf.id) {
    case LF_LOOP16: case LF_LOOP12: case LF_LOOP32: case LF_LOOP64: case LF_STUT4: case LF_STUT3: case LF_ROLLUP: {
        uint32_t rel = afr - bf.S, p;
        if (bf.id == LF_ROLLUP) {               /* the loop halves every beat: 1/8, 1/16, 1/32, 1/64 */
            uint32_t beats = (afr - bf.E) / (g_rowlen * 4u + 1u), Lr = L >> (beats > 3u ? 3u : beats);
            if (Lr < 64u) Lr = 64u;
            L = Lr;
        }
        if (rel < bf.L && !bf.frozen)           /* the first pass: live */
            return;
        bf.frozen = 1;
        p = rel % L;
        w = cap_rd(bf.S + p);
        if (p >= L - 64u) {                     /* the seam: fade into what came before the start */
            uint32_t w2 = cap_rd(bf.S + p - L);
            int32_t a = (int32_t)(p - (L - 64u)) * 512;
            *l = (cap_l(w) * (32767 - a) + cap_l(w2) * a) >> 15;
            *r = (cap_r(w) * (32767 - a) + cap_r(w2) * a) >> 15;
        } else {
            *l = cap_l(w);
            *r = cap_r(w);
        }
        if (bf.id == LF_STUT4 || bf.id == LF_STUT3) {   /* each repeat softer, back to full every beat */
            uint32_t n = bf.id == LF_STUT4 ? 4u : 3u, k = (rel / L) % n;
            g = 32767 - (int32_t)(k * (24000u / n));
            *l = (*l * g) >> 15;
            *r = (*r * g) >> 15;
        }
        return;
    }
    case LF_REV: {
        uint32_t p = t % L;
        int32_t e = (int32_t)(p < 128u ? p : (L - p < 128u ? L - p : 128u)) * 256;
        w = cap_rd(bf.S + L - 1u - p);
        *l = (cap_l(w) * e) >> 15;
        *r = (cap_r(w) * e) >> 15;
        return;
    }
    case LF_FREEZE: {                           /* two heads, triangle windows: a smooth drone of 46 ms */
        uint32_t p1 = t % L, p2 = (t + L / 2u) % L, w2;
        int32_t a1 = (int32_t)(p1 < L / 2u ? p1 : L - p1) * 32767 / (int32_t)(L / 2u), a2 = 32767 - a1;
        w = cap_rd(bf.S + p1);
        w2 = cap_rd(bf.S + p2);
        *l = (cap_l(w) * a1 + cap_l(w2) * a2) >> 15;
        *r = (cap_r(w) * a1 + cap_r(w2) * a2) >> 15;
        return;
    }
    case LF_SCR: case LF_SCRF: {                /* back and forth over the last 1/16, one stroke per 1/8 (1/16) */
        uint32_t per = (bf.id == LF_SCR ? g_rowlen * 2u : g_rowlen) + 1u, ph = (t % per) * (0xFFFFFFFFu / per);
        int32_t c = 32767 - sine(ph + 0x40000000u), off = (int32_t)(((int64_t)L * (c + 32767)) >> 16);
        uint32_t a = bf.S + L - 1u - (uint32_t)(off < (int32_t)L ? off : (int32_t)L - 1);
        uint32_t w2 = cap_rd(a - 1u);
        w = cap_rd(a);
        *l = (cap_l(w) + cap_l(w2)) >> 1;
        *r = (cap_r(w) + cap_r(w2)) >> 1;
        return;
    }
    case LF_TAPE: case LF_DIVE: {               /* varispeed through the capture */
        uint32_t ip, fr;
        int32_t a0, b0, a1, b1;
        if (bf.id == LF_TAPE) {                 /* slows to a stop over a beat */
            uint32_t dv = 65536u / (g_rowlen * 4u + 1u) + 1u;
            bf.v = bf.v > dv ? bf.v - dv : 0u;
            bf.p += bf.v;
            if (!bf.v) { *l = *r = 0; return; }
        } else {                                /* each repeat of a 1/16, lower */
            if (bf.k == 0 && (afr - bf.S) < L && !bf.frozen) return;
            bf.frozen = 1;
            bf.p += bf.v;
            if ((bf.p >> 16) >= L) {
                bf.p -= L << 16;
                bf.k++;
                bf.v = (uint32_t)(((uint64_t)bf.v * 56000u) >> 16);
                if (bf.v < 16384u) bf.v = 16384u;
            }
        }
        ip = bf.S + (bf.p >> 16);
        fr = (bf.p >> 1) & 0x7FFFu;
        w = cap_rd(ip);
        a0 = cap_l(w); b0 = cap_r(w);
        w = cap_rd(ip + 1u);
        a1 = cap_l(w); b1 = cap_r(w);
        *l = a0 + (((a1 - a0) * (int32_t)fr) >> 15);
        *r = b0 + (((b1 - b0) * (int32_t)fr) >> 15);
        return;
    }
    }
}

/* the live bus for CHUNK frames, in place; E: the echo's input (ECHO adds the bus to it) */
static void fx_live(int32_t *L, int32_t *R, int32_t *E)
{
    uint32_t i, on = lfx_on;
    int32_t q = 13000, f = 0, mode = 0;
    /* the filter: the LPF sweep wins, then the HPF, then wobble, then vinyl's top cut */
    if (on >> LF_LPF & 1u) { if (lpf_note > 34u) lpf_note--; }
    else if (lpf_note < 127u) lpf_note = (uint8_t)(lpf_note + 4u > 127u ? 127u : lpf_note + 4u);
    if (on >> LF_HPF & 1u) { if (hpf_note < 100u) hpf_note++; }
    else if (hpf_note) hpf_note = (uint8_t)(hpf_note > 4u ? hpf_note - 4u : 0u);
    lfo_ph += (0xFFFFFFFFu / (g_rowlen + 1u)) * (CHUNK / 2u);          /* two cycles a beat (1/8) */
    if (lpf_note < 127u) { mode = 1; f = SVF_F[lpf_note]; q = 9000; }
    else if (hpf_note) { mode = 2; f = SVF_F[hpf_note + 20u]; q = 12000; }
    else if (on >> LF_WOB & 1u) { mode = 1; f = SVF_F[64 + (sine(lfo_ph) * 28 >> 15)]; q = 7000; }
    else if (on >> LF_VINYL & 1u) { mode = 1; f = SVF_F[98]; q = 20000; }
    for (i = 0; i < CHUNK; i++) {
        int32_t l = clampi(L[i], -65535, 65535), r = clampi(R[i], -65535, 65535);   /* (two full scales: the
                                                 * products below stay inside 32 bits) */
        if (!(bf.act && bf.frozen) || ((afr - (bf.S - 64u)) & CAPM) >= bf.L + 128u)   /* (frozen: all but the */
            cap[afr & CAPM] = (uint32_t)(uint16_t)clampi(l, -32767, 32767) | (uint32_t)clampi(r, -32767, 32767) << 16;
        if (bf.act) {
            int32_t fl = l, fr = r;
            bf_read(&fl, &fr);
            if (bf.mix < bf.mix_to) {           /* 64 frames from live to the effect, and back */
                bf.mix += 512;
                if (bf.mix > bf.mix_to) bf.mix = bf.mix_to;
            } else if (bf.mix > bf.mix_to) {
                bf.mix -= 512;
                if (bf.mix < bf.mix_to) bf.mix = bf.mix_to;
            }
            l += (int32_t)(((int64_t)(fl - l) * bf.mix) >> 15);
            r += (int32_t)(((int64_t)(fr - r) * bf.mix) >> 15);
            bf.t++;
            if (!bf.mix && !bf.mix_to)
                bf.act = 0;
        }
        if (on >> LF_CRUSH & 1u) { l &= ~0x7FF; r &= ~0x7FF; }
        if (on >> LF_DECIM & 1u) {
            if (++hold_n >= 6u) { hold_n = 0; hold_l = l; hold_r = r; }
            l = hold_l; r = hold_r;
        }
        if (mode) {                             /* Chamberlin state variable filter, stereo */
            int32_t hp;
            svf_lp[0] += (int32_t)(((int64_t)f * svf_bp[0]) >> 15);   /* (64-bit products, the state bounded) */
            hp = l - svf_lp[0] - (int32_t)(((int64_t)q * svf_bp[0]) >> 15);
            svf_bp[0] += (int32_t)(((int64_t)f * hp) >> 15);
            l = mode == 1 ? svf_lp[0] : hp;
            svf_lp[1] += (int32_t)(((int64_t)f * svf_bp[1]) >> 15);
            hp = r - svf_lp[1] - (int32_t)(((int64_t)q * svf_bp[1]) >> 15);
            svf_bp[1] += (int32_t)(((int64_t)f * hp) >> 15);
            r = mode == 1 ? svf_lp[1] : hp;
            svf_bp[0] = clampi(svf_bp[0], -200000, 200000);
            svf_bp[1] = clampi(svf_bp[1], -200000, 200000);
            svf_lp[0] = clampi(svf_lp[0], -200000, 200000);
            svf_lp[1] = clampi(svf_lp[1], -200000, 200000);
            l = clampi(l, -65535, 65535);       /* (the ring, the gate and the drive after it stay in 32 bits) */
            r = clampi(r, -65535, 65535);
        } else
            svf_lp[0] = svf_lp[1] = svf_bp[0] = svf_bp[1] = 0;
        if (on >> LF_RING & 1u) {
            int32_t s;
            ring_ph += 21424000u;               /* 220 Hz */
            s = sine(ring_ph);
            l = (l * s) >> 15;
            r = (r * s) >> 15;
        }
        if (on & ((1u << LF_GATE16) | (1u << LF_GATE32))) {
            uint32_t g = on >> LF_GATE16 & 1u ? g_rowlen : g_rowlen / 2u, ph = since_grid(g);
            int32_t tgt = ph < (g * 3u) / 5u ? 32767 : 0;
            gate_g += gate_g < tgt ? 2048 : -2048;
            gate_g = clampi(gate_g, 0, 32767);
            l = (l * gate_g) >> 15;
            r = (r * gate_g) >> 15;
        } else
            gate_g = 32767;
        if (on >> LF_DIST & 1u) {
            l = (sat(l * 5) * 22000) >> 15;
            r = (sat(r * 5) * 22000) >> 15;
        }
        if (on >> LF_VINYL & 1u) {              /* crackle */
            uint32_t x = xr(&rnd_isr);
            if (x < 0x00080000u) { l += 9000; r += 9000; }
            l += (int32_t)(x >> 24) - 128;
            r += (int32_t)(x >> 24) - 128;
        }
        if (on >> LF_ECHO & 1u)
            E[i] += (l + r) >> 1;
        L[i] = l;
        R[i] = r;
        if ((afr & 7u) == 0u) {
            scope[scope_w] = (int16_t)clampi((l + r) >> 1, -32767, 32767);
            if (++scope_w >= 240u) scope_w = 0;
        }
        afr++;
    }
}

/* ---- the reverb: four combs and two all-pass filters a side (Freeverb's), 14 KiB */
#define RV_C0 1116u
#define RV_C1 1188u
#define RV_C2 1277u
#define RV_C3 1356u
#define RV_AL0 556u
#define RV_AL1 441u
#define RV_AR0 579u
#define RV_AR1 464u
static int16_t rv_buf[RV_C0 + RV_C1 + RV_C2 + RV_C3 + RV_AL0 + RV_AL1 + RV_AR0 + RV_AR1];
static const uint16_t RV_LEN[8] = {RV_C0, RV_C1, RV_C2, RV_C3, RV_AL0, RV_AL1, RV_AR0, RV_AR1};
static uint16_t rv_pos[8];
static int32_t rv_lp[4];
static int32_t rv_room = 28000, rv_damp = 9000;  /* feedback 0.85, damping 0.27 (Q15) */

static int16_t *rv_line(uint32_t k)
{
    uint32_t o = 0, j;
    for (j = 0; j < k; j++)
        o += RV_LEN[j];
    return rv_buf + o;
}
static int16_t *rv_lines[8];
static void rv_init(void)
{
    uint32_t k;
    for (k = 0; k < 8u; k++)
        rv_lines[k] = rv_line(k);
}

static int32_t rv_allpass(uint32_t k, int32_t x)
{
    int16_t *b = rv_lines[k];
    uint32_t p = rv_pos[k];
    int32_t d = b[p], y = d - x;
    b[p] = (int16_t)clampi(x + (d >> 1), -32767, 32767);
    if (++p >= RV_LEN[k]) p = 0;
    rv_pos[k] = (uint16_t)p;
    return y;
}

static void reverb_chunk(const int32_t *in, int32_t *L, int32_t *R)
{
    uint32_t i, k;
    if (!rv_lines[0])
        rv_init();
    for (i = 0; i < CHUNK; i++) {
        int32_t x = in[i] >> 2, s = 0, l, r;
        for (k = 0; k < 4u; k++) {
            int16_t *b = rv_lines[k];
            uint32_t p = rv_pos[k];
            int32_t d = b[p];
            rv_lp[k] += ((d - rv_lp[k]) * (32767 - rv_damp)) >> 15;
            b[p] = (int16_t)clampi(x + ((rv_lp[k] * rv_room) >> 15), -32767, 32767);
            if (++p >= RV_LEN[k]) p = 0;
            rv_pos[k] = (uint16_t)p;
            s += d;
        }
        s >>= 1;
        l = rv_allpass(5, rv_allpass(4, s));
        r = rv_allpass(7, rv_allpass(6, s));
        L[i] += l;
        R[i] += r;
    }
}

/* ---- the limiter: the chunk's peak sets the gain (one division a chunk), down at once and ramped over the
 * chunk, back up in ~50 ms: six tracks at full volume stay under the clip instead of crackling */
#define LIM_T 26000
static int32_t lim_g = 32767;
static void limiter(int32_t *L, int32_t *R)
{
    uint32_t i;
    int32_t pk = 0, tgt = 32767, g = lim_g, dg;
    for (i = 0; i < CHUNK; i++) {
        int32_t a = L[i] < 0 ? -L[i] : L[i], b = R[i] < 0 ? -R[i] : R[i];
        if (a > pk) pk = a;
        if (b > pk) pk = b;
    }
    if (pk > LIM_T)
        tgt = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)pk);
    if (tgt < g) g = tgt;                       /* attack: this chunk already */
    else g += (tgt - g) >> 7;                   /* release */
    dg = (g - lim_g) / (int32_t)CHUNK;
    for (i = 0; i < CHUNK; i++) {
        int32_t gi = lim_g + dg * (int32_t)(i + 1u);
        if (gi > g && tgt < lim_g) gi = g;      /* (never above what the peak allows) */
        L[i] = (int32_t)(((int64_t)L[i] * (tgt < lim_g ? g : gi)) >> 15);
        R[i] = (int32_t)(((int64_t)R[i] * (tgt < lim_g ? g : gi)) >> 15);
    }
    lim_g = g;
}

/* ---- the master for CHUNK frames: L, R in (the tracks and the live bus); E, V: the echo's and the
 * reverb's sends. The echo is 3/16 (a dotted 8th), darker each repeat; then the master's effect */
static fxs_t mfx_l, mfx_r;
static void fx_master(int32_t *L, int32_t *R, const int32_t *E, const int32_t *V)
{
    uint32_t i, edly = g_rowlen * 3u;
    int32_t fb = lfx_on >> LF_ECHO & 1u ? 22000 : 13000;
    if (edly >= ECHON - 16u)
        edly = g_rowlen * 2u;
    if (edly >= ECHON - 16u)
        edly = ECHON - 16u;
    for (i = 0; i < CHUNK && cpu_shed < 2u; i++) {
        int32_t e = echo_buf[(echo_w - edly) & (ECHON - 1u)], in;
        echo_lp += ((e - echo_lp) * 12000) >> 15;
        in = E[i] + ((echo_lp * fb) >> 15);
        echo_buf[echo_w & (ECHON - 1u)] = (int16_t)clampi(in, -32767, 32767);
        echo_w++;
        L[i] += echo_lp;
        R[i] += echo_lp;
    }
    if (cpu_shed < 1u)                          /* (the load governor: kawa_seq.c) */
        reverb_chunk(V, L, R);
    fx_insert(&mfx_l, prj.mfx[0], prj.mfx[1], prj.mfx[2], prj.mfx[3], L, CHUNK);
    fx_insert(&mfx_r, prj.mfx[0], prj.mfx[1], prj.mfx[2], prj.mfx[3], R, CHUNK);
    limiter(L, R);
    for (i = 0; i < CHUNK; i++) {
        L[i] = sat(L[i]);
        R[i] = sat(R[i]);
    }
}
