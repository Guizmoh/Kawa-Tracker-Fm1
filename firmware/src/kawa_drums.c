/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the KIT engine: 16 synthesised drums a kit (kawa_kits.h), two voices a track. Included by
 * kawa_snd.c (the audio interrupt: integers only, no division per frame). The track's knobs: KIT, TUNE,
 * DECAY, DRIVE, CRUSH, TONE (P_KIT..P_KTON; TONE is kawa_snd.c's low-pass, after the kit).
 *
 * A drum (drumdef_t) is up to four parts, summed, then shaped:
 *   tone A   a wave (sine, triangle, square, pulse 25 / 12.5 %, saw, the Game Boy's 4-bit triangle) whose
 *            pitch falls (or rises) from note + drop to the note, or steps (a coin), and a wobble on it (an LFO:
 *            tape, a siren, a scratch)
 *   tone B   a second wave at a ratio of A's pitch (it follows A): mixed in, ring-modulating A, or modulating
 *            A's phase (FM), with its own decay (a beater, a shell's second mode, a bell's partial)
 *   noise    white, the 15- or 7-bit LFSR, the metal (six squares, four tunings) or crackle, sampled at a
 *            clock (the chips' noise pitch). Two envelopes: the body (snare wires, a cymbal's wash) and the
 *            spike (the stick, a beater, a hand), the spike struck again for claps, flams, the guiro
 *   filter   a state-variable filter (low, band or high pass, a resonance) on the noise, and on the tones when
 *            the drum asks; its cutoff can follow the pitch envelope (a spring's chirp)
 *   then the drive (a soft knee, flat at -1 dB), the bits (rounded: no DC) and a sample-and-hold rate (the samplers' grit:
 *   32 kHz, 26 kHz .. 5.5 kHz). A rise (an attack) shapes the tones and the body: shakers, brushes, a rumble.
 * Envelopes run once a chunk (16 frames) and ramp inside it; the Game Boy's step in 16 levels, unramped, and
 * its pitch moves in whole semitones. A DC blocker ends it. A voice costs ~100 operations a frame at worst (FM, the metal, the
 * filter, the hold), ~40 for a plain kick. */
typedef struct {
    uint8_t on, drum, crush, kit, tail;         /* tail: chunks left for the blocker's end */
    uint8_t hits, gap;                          /* re-strikes left, chunks to the next */
    int8_t dec;                                 /* the DECAY knob at the hit */
    uint8_t sr;                                 /* the hold's rate (SR_*) */
    uint16_t choke, pad;                        /* a fast fade (per chunk, Q16): CUT or the same drum again */
    uint32_t ph, ph2, nph, wph, shp, rnd, lfsr, t;   /* t: frames since the hit */
    uint32_t mph[6];
    int32_t tune, pe, ae, be, ne, ce, re, lp, bp, nz, hold, drive, dcb, dcb2;   /* the DC blockers */
} drum_t;
static drum_t drum[NTRK][2];
static uint8_t drum_next[NTRK];

static const uint16_t METAL_R[4][6] = {         /* the metal: six squares over its base, Q12 */
    {4096, 6073, 7374, 10428, 10774, 15961},    /* the 808's (205 304 370 523 540 800 Hz) */
    {4096, 5800, 6560, 7530, 8810, 10650},      /* a wider cymbal */
    {4096, 4203, 4368, 4836, 6059, 7486},       /* a tight cluster: jingles, bright hats */
    {4096, 12254, 17607, 21692, 25765, 43372}}; /* wide apart: a ride's ping, bells */
static const uint16_t SR_STEP[8] = {0, 47554, 38698, 32768, 23777, 16384, 11889, 8192};   /* 32, 26, 22, 16, 11, 8, 5.5 kHz */
static const int16_t FQ[16] = {11585, 9830, 8192, 6554, 5325, 4096, 3277, 2621,   /* the filter's damping, Q13 */
                               2048, 1638, 1311, 1024, 819, 655, 492, 410};
static const int32_t FMAX[4] = {8055, 8812, 9618, 10540};   /* the highest stable cutoff at damping 0..3, Q13 */

static int32_t dwave(uint32_t w, uint32_t ph)   /* a wave, -32767..32767; each starts near 0 (no click) */
{
    int32_t x;
    switch (w) {
    case W_SINE:
        return sine(ph);
    case W_TRI:
        x = (int32_t)((ph + 0x40000000u) >> 15);
        return x < 65536 ? x - 32768 : 98303 - x;
    case W_SQUARE:
        return ph < 0x80000000u ? 32767 : -32767;
    case W_PULSE:                               /* 25 %, mean 0 */
        return ph < 0x40000000u ? 24575 : -8192;
    case W_PULSE12:                             /* 12.5 %, mean 0 */
        return ph < 0x20000000u ? 28672 : -4096;
    case W_SAW:
        return (int32_t)((ph + 0x80000000u) >> 16) - 32768;
    default:                                    /* W_GB4: the Game Boy's wave channel, a 32-step 4-bit triangle */
        return (WAVES[0][((ph >> 27) + 8u) & 31u] * 2 - 15) * 2184;
    }
}

static int32_t dsat(int32_t x)                  /* the drive's knee: straight to 14000, then a curve that flattens
                                                 * at 29000 (-1 dB: a drum never reaches the converter's ceiling) */
{
    int32_t a = x < 0 ? -x : x, u;
    if (a <= 14000)
        return x;
    u = a - 14000;
    if (u >= 30000)
        a = 29000;
    else
        a = 14000 + u - ((((u * u) >> 12) * 4474) >> 16);   /* u - u^2 / 60000 */
    return x < 0 ? -a : a;
}

static void drum_on(uint32_t t, uint32_t d, int32_t tune, int32_t crush)   /* tune: semitones more (PITCH, the table); */
{                                                                       /* crush: the cell's (CRUSH), or -1: the knob */
    drum_t *v = &drum[t][drum_next[t]], *o;
    const int8_t *p = prj.snd[t].p;
    uint32_t kit = (uint32_t)clampi(p[P_KIT], 0, NKIT - 1), j;
    const drumdef_t *k = &KITS[kit][d & 15u];
    int32_t g = clampi(k->drive + p[P_DRIVE], 0, 30);
    drum_next[t] ^= 1u;
    o = &drum[t][drum_next[t]];                 /* the other voice: the last hit */
    if (o->on && (o->drum == (d & 15u) ||
                  (o->kit == kit && (k->flags & DR_HAT) && (KITS[kit][o->drum].flags & DR_HAT))))
        o->choke = 40000, o->hits = 0;          /* the same drum again, or a hat over a hat: the old one chokes */
    v->drum = (uint8_t)(d & 15u);
    v->kit = (uint8_t)kit;
    v->tune = p[P_TUNE] + tune;
    v->dec = (int8_t)(p[P_DECAY] * 2);
    v->pe = k->drop * 256;
    v->ae = k->tone ? 65535 : 0;
    v->be = k->l2 ? 65535 : 0;
    v->ne = k->noise && !(k->flags & DR_TAIL) ? 65535 : 0;
    v->ce = k->click ? 65535 : 0;
    v->re = k->rise ? 65535 : 0;
    v->hits = k->hits;
    v->gap = k->hgap;
    v->choke = 0;
    v->drive = (256 + 48 * g + 8 * g * g) * k->vol >> 7;   /* x1, 1.5 (2), 2.25 (4), 4.5 (8), 10.8 (15); Q8 */
    v->crush = (uint8_t)clampi(k->bits + (crush >= 0 ? crush : p[P_KCRUSH]), 0, 7);
    v->sr = k->srate;
    if (!v->rnd)
        v->rnd = 0xACE1u + d + t * 77u;
    v->ph = v->ph2 = v->nph = v->shp = 0;
    v->wph = (k->flags & DR_WRND) ? xr(&v->rnd) : 0u;   /* a wobble of its own each hit */
    for (j = 0; j < 6u; j++)
        v->mph[j] = xr(&v->rnd);                /* the squares run free, as on the 808 */
    v->lfsr = 0x7FFFu;
    v->lp = v->bp = v->nz = v->hold = v->dcb = v->dcb2 = 0;
    v->t = 0;
    v->tail = 0;
    v->on = 1;
}

static void drum_chunk(drum_t *v, int32_t *out)
{
    const drumdef_t *k;
    uint32_t i, j, inc, inc2 = 0, ninc = 0, minc[6], w, w2, src;
    int32_t pit, dec, ck, ka, kn, kb, kc, kr, a0, a1, b0, b1, n0, n1, r0, r1, e0, e1;
    int32_t ta, dta, tb, dtb, td, na, dna, f = 0, q = 0, qn = 8192, m, h, lp, bp, g, dc, dc2;
    int aon, bon, non, fm, ring, mix, tf, fmode, gb;
    if (!v->on)
        return;
    if (v->tail) {                              /* the end: the output blocker's level falls back to 0 (no click;
                                                 * at most 92 ms, at once on a choke) */
        int32_t d2 = v->dcb2, y;
        for (i = 0; i < CHUNK; i++) {
            y = -((d2 + 256) >> 9);
            d2 += y;
            out[i] += y;
        }
        v->dcb2 = d2;
        if (v->choke || (uint32_t)(d2 + 4096) < 8192u || !--v->tail)
            v->on = 0;
        return;
    }
    k = &KITS[v->kit][v->drum];
    gb = (k->flags & DR_GB) != 0;
    dec = v->dec;
    /* a re-strike: a clap's next hand, the guiro's next ridge, a flam */
    if (v->hits && !--v->gap) {
        v->hits--;
        v->gap = (uint8_t)(k->hgap + (xr(&v->rnd) & 1u));
        v->ce = k->click ? 65535 : 0;
        if (k->flags & DR_HTONE) {
            v->ae = k->tone ? 65535 : 0;
            v->be = k->l2 ? 65535 : 0;
            v->ne = k->noise ? 65535 : 0;
            v->pe = k->drop * 256;
        }
        if (!v->hits && (k->flags & DR_TAIL))
            v->ne = 65535;                      /* the last hand: the room */
    }
    /* the envelopes: this chunk's start and end (Q16), the rise taken off the tones and the body */
    ck = v->choke;
    ka = ck ? ck : DECAY_K[clampi(k->atau + dec, 0, 63)];
    kb = ck ? ck : k->btau ? DECAY_K[k->btau] : ka;
    kn = ck ? ck : DECAY_K[clampi(k->ntau + dec, 0, 63)];
    kc = ck ? ck : DECAY_K[k->ctau];
    kr = ck ? ck : DECAY_K[k->rise];
    a0 = v->ae; a1 = v->ae = (int32_t)((uint32_t)a0 * (uint32_t)ka >> 16);
    b0 = v->be; b1 = v->be = (int32_t)((uint32_t)b0 * (uint32_t)kb >> 16);
    n0 = v->ne; n1 = v->ne = (int32_t)((uint32_t)n0 * (uint32_t)kn >> 16);
    e0 = v->ce; e1 = v->ce = (int32_t)((uint32_t)e0 * (uint32_t)kc >> 16);
    r0 = v->re; r1 = v->re = (int32_t)((uint32_t)r0 * (uint32_t)kr >> 16);
    a0 = a0 > r0 ? a0 - r0 : 0; a1 = a1 > r1 ? a1 - r1 : 0;
    b0 = b0 > r0 ? b0 - r0 : 0; b1 = b1 > r1 ? b1 - r1 : 0;
    n0 = n0 > r0 ? n0 - r0 : 0; n1 = n1 > r1 ? n1 - r1 : 0;
    if (gb) {                                   /* the Game Boy: 16 volume steps, no ramp */
        a0 = a1 = a0 & 0xF000;
        b0 = b1 = b0 & 0xF000;
        n0 = n1 = n0 & 0xF000;
        e0 = e1 = e0 & 0xF000;
    }
    ta = (a0 * k->tone) >> 8;                   /* levels: 128 = full scale */
    dta = (((a1 * k->tone) >> 8) - ta) / (int32_t)CHUNK;
    tb = (b0 * k->l2) >> 8;
    dtb = (((b1 * k->l2) >> 8) - tb) / (int32_t)CHUNK;
    na = (n0 * k->noise + e0 * k->click) >> 8;
    if (na > 65535) na = 65535;
    dna = (n1 * k->noise + e1 * k->click) >> 8;
    if (dna > 65535) dna = 65535;
    dna = (dna - na) / (int32_t)CHUNK;
    aon = ta || dta;
    bon = tb || dtb;
    non = na || dna;
    /* the pitch: the envelope (a fall, a rise or a step), the wobble */
    if (k->flags & DR_STEP)
        v->pe = v->t < (uint32_t)k->ptau * 256u ? k->drop * 256 : 0;
    pit = (k->note + v->tune) * 256 + (gb ? v->pe & ~255 : v->pe);
    if (!(k->flags & DR_STEP))
        v->pe = (int32_t)(((int64_t)v->pe * DECAY_K[k->ptau]) >> 16);
    if (k->wob) {
        v->wph += k->wrate * 389571u;           /* 1/4 Hz a step */
        pit += (sine(v->wph) * k->wob) >> 11;   /* 1/16 semitone a step */
    }
    w = k->wave;
    w2 = k->w2 & 7u;
    fm = bon && (k->w2 & B_FM);
    ring = bon && (k->w2 & B_RING);
    mix = bon && !fm && !ring;
    td = tb > 32767 ? 32767 : tb;               /* (the ring's depth) */
    inc = pitch_inc(pit);
    if (bon) {
        uint64_t x = ((uint64_t)inc * k->r2) >> 8;
        inc2 = x > 0x70000000u ? 0x70000000u : (uint32_t)x;
    }
    /* the noise: its clock, the metal's squares, the filter */
    src = k->src & 7u;
    if (non) {
        if (src == N_METAL) {
            uint32_t base = pitch_inc((k->clk + v->tune) * 256) >> 12;
            const uint16_t *r = METAL_R[(k->src >> 4) & 3u];
            for (j = 0; j < 6u; j++)
                minc[j] = base * r[j];
        } else if (k->clk && src != N_CRACKLE)
            ninc = pitch_inc((k->clk + v->tune) * 256);
    }
    fmode = k->filt & 3u;
    if (fmode) {
        int32_t fq = (k->filt >> 2) & 15;
        int32_t fn = k->fnote + v->tune + ((k->flags & DR_FSWEEP) ? v->pe >> 8 : 0);
        f = SVF_F[clampi(fn, 0, 127)] >> 2;     /* Q13 */
        if (fq < 4 && f > FMAX[fq])
            f = FMAX[fq];
        q = FQ[fq];
        if (fmode == F_BP)
            qn = q;                             /* the band at its centre: unity */
    }
    tf = (k->flags & DR_TFILT) != 0;
    m = crush_mask(v->crush);
    h = (~m + 1) >> 1;                          /* (rounded, not truncated: no DC) */
    g = v->drive;
    lp = v->lp;
    bp = v->bp;
    dc = v->dcb;
    dc2 = v->dcb2;
    for (i = 0; i < CHUNK; i++) {
        int32_t s = 0, x = 0, a, b = 0, bo = 0;
        if (bon) {
            b = dwave(w2, v->ph2);
            bo = (b * tb) >> 15;
            v->ph2 += inc2;
            tb += dtb;
        }
        if (aon) {
            uint32_t ph = v->ph;
            if (fm)
                ph += (uint32_t)bo << 17;       /* B's level: the index (128: one cycle) */
            a = dwave(w, ph);
            if (ring)
                a -= (((a * ((32767 - b) >> 1)) >> 14) * td) >> 15;
            s = (a * ta) >> 15;
            v->ph += inc;
            ta += dta;
        }
        if (mix)
            s += bo;
        if (non) {
            int32_t n;
            uint32_t o = v->nph;
            v->nph += ninc;
            switch (src) {
            case N_WHITE:
                if (v->nph <= o)                /* (no clock: a new value every frame) */
                    v->nz = (int32_t)(xr(&v->rnd) >> 16) - 32768;
                n = v->nz;
                break;
            case N_LFSR15:
            case N_LFSR7:
                if (v->nph <= o) {
                    uint32_t l = v->lfsr, bit = (l ^ (l >> 1)) & 1u;
                    l = (l >> 1) | (bit << 14);
                    if (src == N_LFSR7)
                        l = (l & ~0x40u) | (bit << 6);
                    v->lfsr = l;
                }
                n = (v->lfsr & 1u) ? 18918 : -18918;
                break;
            case N_METAL: {
                uint32_t c = 0;
                for (j = 0; j < 6u; j++) {
                    v->mph[j] += minc[j];
                    c += v->mph[j] >> 31;
                }
                n = (int32_t)c * 10922 - 32766;
                break;
            }
            default: {                          /* N_CRACKLE: sparse clicks, clk of 256 frames */
                uint32_t r = xr(&v->rnd);
                n = (r >> 24) < k->clk ? (int32_t)(r & 0xFFFFu) - 32768 : 0;
                break;
            }
            }
            x = (n * (na >> 1)) >> 14;
            na += dna;
        }
        if (tf) {
            x += s;
            s = 0;
        }
        if (fmode && (non || tf)) {             /* the state-variable filter, Q13 (rounded: no drift) */
            int32_t hp;
            x = (x * qn) >> 13;
            lp += (int32_t)(((int64_t)f * bp + 4096) >> 13);   /* (64 bits: a long noise body, DECAY up, */
            lp = lp > 131071 ? 131071 : lp < -131071 ? -131071 : lp;   /* went past 32) */
            hp = x - lp - (int32_t)(((int64_t)q * bp) >> 13);
            bp += (int32_t)(((int64_t)f * hp + 4096) >> 13);
            bp = bp > 131071 ? 131071 : bp < -131071 ? -131071 : bp;
            x = fmode == F_LP ? lp : fmode == F_BP ? bp : hp;
        }
        s += x;
        dc += s - ((dc + 256) >> 9);            /* the DC blocker (14 Hz; dc: 512 x the mean), as the chips' */
        s -= (dc + 256) >> 9;                   /* output capacitor: a lopsided kick, the Game Boy's noise */
        s = dsat((((s * g) >> 8) + h) & m);
        if (v->sr) {                            /* the sampler's rate: a new value at its clock */
            v->shp += SR_STEP[v->sr];
            if (v->shp >= 65536u) {
                v->shp -= 65536u;
                v->hold = s;
            }
            s = v->hold;
        }
        s -= (dc2 + 256) >> 9;                  /* and after the drive and the bits (a pulse clipped hard is */
        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;   /* lopsided), on what goes out: the limit's own */
        dc2 += s;                               /* DC goes too */
        out[i] += s;
    }
    v->lp = lp;
    v->bp = bp;
    v->dcb = dc;
    v->dcb2 = dc2;
    v->t += CHUNK;
    if (v->ae < 40 && v->be < 40 && v->ne < 40 && v->ce < 40 && !v->hits) {
        if (!ck && (uint32_t)(dc2 + 4096) >= 8192u)
            v->tail = 255;                      /* the output blocker still holds a level: let it fall first */
        else
            v->on = 0;
    }
}

static void drum_choke(drum_t *v)               /* CUT: the drum stops now (a short fade) */
{
    v->choke = 50000;
    v->hits = 0;
}
