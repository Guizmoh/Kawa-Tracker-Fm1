/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: what changes the project: sounds, new project and the demo, undo, the clipboard, the magic
 * tools, the song list, the store (four project slots in flash: 0 the autosave, 1..3 the user's) and the
 * user samples. Main loop only. */

/* ---- sounds */
static void sound_build(uint32_t t)             /* after any change of a track's sound */
{
    if (prj.snd[t].engine == E_FM)
        fm_build(t);
}

static void fm_patch_load(uint32_t t)           /* FM SOUND: a factory patch into the project (its operators) */
{
    uint32_t k, n = (uint32_t)clampi(prj.snd[t].p[P_FMPAT], 0, FM_NPATCH - 1);
    for (k = 0; k < 156u; k++)
        prj.fmp[k] = FM_PATCH[n][k];
}

/* an FM operator's knob (OPERATOR, OP ENV): operator n (1..6) is the patch's 6 - n (msfa). The envelope is
 * shown as an ADSR: ATTACK is R1 (turned round: up is slower), DECAY R2 and R3, SUSTAIN L2 and L3, RELEASE R4 */
static int32_t op_get(uint32_t n, uint32_t k)
{
    const uint8_t *op = prj.fmp + (6u - n) * FP_OP;
    switch (k) {
    case OPK_LEVEL: return op[FP_OL] % 100u;
    case OPK_RATIO: return op[FP_FC] & 31u;
    case OPK_FINE: return op[FP_FF] % 100u;
    case OPK_DET: return (int32_t)(op[FP_DET] % 15u) - 7;
    case OPK_ATT: return 99 - (int32_t)(op[FP_R1] % 100u);
    case OPK_DEC: return 99 - (int32_t)(op[FP_R1 + 1] % 100u);
    case OPK_SUS: return op[FP_L1 + 2] % 100u;
    default: return 99 - (int32_t)(op[FP_R1 + 3] % 100u);
    }
}
static void op_set(uint32_t n, uint32_t k, int32_t v)
{
    uint8_t *op = prj.fmp + (6u - n) * FP_OP;
    v = clampi(v, OPK_LO[k], OPK_HI[k]);
    switch (k) {
    case OPK_LEVEL: op[FP_OL] = (uint8_t)v; break;
    case OPK_RATIO: op[FP_FC] = (uint8_t)v; break;
    case OPK_FINE: op[FP_FF] = (uint8_t)v; break;
    case OPK_DET: op[FP_DET] = (uint8_t)(v + 7); break;
    case OPK_ATT: op[FP_R1] = (uint8_t)(99 - v); break;
    case OPK_DEC: op[FP_R1 + 1] = op[FP_R1 + 2] = (uint8_t)(99 - v); break;
    case OPK_SUS: op[FP_L1 + 1] = op[FP_L1 + 2] = (uint8_t)v; break;
    default: op[FP_R1 + 3] = (uint8_t)(99 - v); break;
    }
}

static int fm_free(uint32_t t)                  /* track t may play FM: no other track does (one FM track) */
{
    uint32_t f = fm_track();
    return f == NTRK || f == t;
}

static void sound_mix_default(uint32_t t)
{
    static const int8_t PAN0[NTRK] = {-2, 3, 0, 2, 0, 0};
    int8_t *p = prj.snd[t].p;
    p[P_VOL] = 12;
    p[P_PAN] = PAN0[t];
    p[P_ECHO] = 0;
    p[P_VERB] = 2;
    p[P_FXT] = FXT_OFF;
    p[P_FXA] = 8;
    p[P_FXB] = 4;
    p[P_FXM] = 15;
    p[P_LFOD] = LFD_OFF;
    p[P_LFOR] = 4;
    p[P_LFOA] = 6;
    p[P_LFOW] = 0;
}

static void sound_preset(uint32_t t, uint32_t k)   /* a factory sound: SOUND and the envelope (the rest stays) */
{
    const preset_t *pr = &PRESETS[k % NPRESET];
    uint32_t i;
    if (pr->engine != prj.snd[t].engine)
        ev_push(EV_KILL, t, 0, 0, 0, 0);        /* another engine: the old one's voices would never end */
    prj.snd[t].engine = pr->engine;
    prj.snd[t].preset = (uint8_t)(k % NPRESET);
    for (i = 0; i < 8u; i++)
        prj.snd[t].p[P_S0 + i] = pr->s[i];
    for (i = 0; i < 4u; i++)
        prj.snd[t].p[P_ATT + i] = pr->env[i];
    if (pr->engine == E_FM)
        fm_patch_load(t);
    sound_build(t);
}
static int sound_is_preset(uint32_t t)          /* the sound is still its preset (no '*') */
{
    const preset_t *pr = &PRESETS[prj.snd[t].preset % NPRESET];
    uint32_t i;
    if (pr->engine != prj.snd[t].engine)
        return 0;
    for (i = 0; i < 8u; i++)
        if (prj.snd[t].p[P_S0 + i] != pr->s[i] && PDESC[pr->engine][P_S0 + i].name[0])
            return 0;
    for (i = 0; i < 4u; i++)
        if (prj.snd[t].p[P_ATT + i] != pr->env[i])
            return 0;
    if (pr->engine == E_FM)                     /* FM: its operators too (OPERATOR, OP ENV) */
        for (i = 0; i < 156u; i++)
            if (prj.fmp[i] != FM_PATCH[clampi(prj.snd[t].p[P_FMPAT], 0, FM_NPATCH - 1)][i])
                return 0;
    return 1;
}
static uint32_t preset_first(uint32_t e)        /* the first factory sound of an engine */
{
    uint32_t k;
    for (k = 0; k < NPRESET; k++)
        if (PRESETS[k].engine == e)
            return k;
    return 0;
}

static uint32_t preset_named(uint32_t e, int32_t s0)   /* a factory sound by its engine and its first knob */
{
    uint32_t k;
    for (k = 0; k < NPRESET; k++)
        if (PRESETS[k].engine == e && PRESETS[k].s[0] == s0)
            return k;
    return preset_first(e);
}

/* ---- the scale */
static int in_scale(uint32_t note)
{
    return (SCALES[prj.scale % 12u].mask >> ((note + 120u - prj.root) % 12u)) & 1u;
}
static uint32_t scale_step(uint32_t note, int32_t dir)   /* the next note of the scale up or down */
{
    int32_t n = (int32_t)note, k;
    for (k = 0; k < 12; k++) {
        n += dir;
        if (n < 12)
            return 12u;
        if (n > 119)
            return 119u;
        if (in_scale((uint32_t)n))
            return (uint32_t)n;
    }
    return (uint32_t)clampi(n, 12, 119);
}
static uint32_t scale_snap(uint32_t note)
{
    uint32_t k;
    for (k = 0; k < 12u; k++) {
        if (in_scale(note - k))
            return note - k;
        if (in_scale(note + k))
            return note + k;
    }
    return note;
}
static uint32_t scale_degree(int32_t deg, uint32_t base)   /* deg-th note of the scale from the root under base */
{
    uint32_t n = base - ((base + 120u - prj.root) % 12u);
    while (deg > 0) { n = scale_step(n, 1); deg--; }
    while (deg < 0) { n = scale_step(n, -1); deg++; }
    return n;
}

/* a track's home note (the magic tools, the grid, the keys' octave) */
static uint32_t lane_base(uint32_t t)
{
    switch (prj.snd[t].engine) {
    case E_WAVE: return 45u;                    /* A2: the bass */
    case E_NOISE: return 96u;
    case E_FM: return 64u;
    case E_KIT: case E_SAMPLE: return note_mode(t) == NM_NOTE ? 60u : 1u;
    default: return 69u;
    }
}
static int is_bass(uint32_t t) { return IS_TONAL(t) && lane_base(t) < 52u; }
static uint32_t brush_note[NTRK];               /* the note the tools and the grid write */

/* ---- new project, demo */
static void name_new(char *s)
{
    const char *a = NAME_A[rnd(8)], *b = NAME_B[rnd(8)];
    uint32_t i = 0;
    while (*a && i < 7u) s[i++] = *a++;
    s[i++] = ' ';
    while (*b && i < 14u) s[i++] = *b++;
    s[i] = 0;
}
static void pat_clear(pat_t *p)
{
    memset(p, 0, sizeof *p);
    p->len = 16;
}
static void sed_reset(sedit_t *e)               /* a sample as it is: all of it, 16 even slices, no gain */
{
    uint32_t i;
    memset(e, 0, sizeof *e);
    e->end = 65535u;
    for (i = 0; i < 16u; i++)
        e->sl[i] = (uint16_t)(i * 4096u);
    e->gain = 256;
}
static void groove_preset(uint32_t k)
{
    const uint8_t *g = GROOVE_PRESET[k % 6u];
    uint32_t i;
    prj.grv.len = g[0];
    for (i = 0; i < 16u; i++)
        prj.grv.tick[i] = g[1u + i % g[0]];
}
static void prj_v5_defaults(void)              /* what format 5 adds: the tables off, the samples whole, no groove */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        memset(&prj.tbl[t], 0, sizeof prj.tbl[t]);
        prj.tbl[t].speed = 6;                   /* a step a row */
        prj.tbl[t].len = 4;
        prj.tbl[t].loop = 0;
        sed_reset(&prj.sed[t]);
    }
    memset(&prj.grv, 0, sizeof prj.grv);
    groove_preset(1);                           /* (off; on, a light swing to start from) */
}
static void brushes_reset(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        brush_note[t] = lane_base(t);
}
static void prj_new(void)
{
    uint32_t i;
    memset(&prj, 0, sizeof prj);
    prj.magic = PRJ_MAGIC;
    prj.version = PRJ_VERSION;
    prj.bpm = 100;
    prj.swing = 50;
    prj.root = 9;                               /* A */
    prj.scale = 2;                              /* MINOR */
    prj.lock = 1;
    prj.step = 1;
    prj.song_len = 1;
    prj.song[0].rep = 1;
    prj.mfx[0] = FXT_OFF;
    prj.mfx[1] = 10;
    prj.mfx[2] = 4;
    prj.mfx[3] = 15;
    for (i = 0; i < NPAT; i++)
        pat_clear(&prj.pat[i]);
    prj_v5_defaults();
    for (i = 0; i < NTRK; i++) {
        sound_mix_default(i);
        sound_preset(i, preset_named(TRK_ENGINE0[i], TRK_S0[i]));
    }
    name_new(prj.name);
    brushes_reset();
}

static void put(uint32_t p, uint32_t r, uint32_t t, uint32_t note, uint32_t vol, uint32_t fx, int32_t fxv)
{
    cell_t *c = &prj.pat[p].c[r][t];
    c->note = (uint8_t)note;
    c->vol = (uint8_t)vol;
    c->fx = (uint8_t)fx;
    c->fxv = (uint8_t)fxv;
    c->fx2 = FX_NONE;
    c->fxv2 = 0;
}

/* a lane written as text, one token a row: "C5" "F#3" (notes), "7" (a drum or a slice), "." (empty),
 * "x" (OFF). Only for the demo */
static void lane(uint32_t p, uint32_t t, const char *s, uint32_t vol)
{
    static const int8_t LETTER[7] = {9, 11, 0, 2, 4, 5, 7};   /* A B C D E F G */
    uint32_t r = 0;
    while (*s && r < NROW) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '.') s++;
        else if (*s == 'x') { put(p, r, t, NOTE_OFF, 0, 0, 0); s++; }
        else if (*s >= 'A' && *s <= 'G') {
            int32_t n = LETTER[*s++ - 'A'];
            if (*s == '#') { n++; s++; }
            n += (*s++ - '0' + 1) * 12;
            put(p, r, t, (uint32_t)n, vol, 0, 0);
        } else {
            uint32_t n = 0;
            while (*s >= '0' && *s <= '9') n = n * 10u + (uint32_t)(*s++ - '0');
            put(p, r, t, n, vol, 0, 0);
        }
        r++;
    }
}
static void fx_at(uint32_t p, uint32_t r, uint32_t t, uint32_t fx, int32_t v)
{
    prj.pat[p].c[r][t].fx = (uint8_t)fx;
    prj.pat[p].c[r][t].fxv = (uint8_t)(int8_t)v;
}


/* the demo, "KAWA QUEST": an overworld in C (bouncy), a dungeon in A minor (an ostinato in the dark),
 * the boss (breakcore on the break), the victory. Seven patterns of two bars, the song plays most twice.
 * T1 a pulse lead, T2 FM (kalimba), T3 the wave bass, T4 noise hats, T5 the kit, T6 the break */
enum { T1, T2, T3, T4, T5, T6 };
static void prj_demo(void)
{
    uint32_t p, r;
    prj_new();
    prj.bpm = 160;                              /* (the demo is written for 160; a new song starts at 100) */
    memcpy(prj.name, "KAWA QUEST", 11);
    prj.root = 0;                               /* C major (the dungeon and the boss are A minor: the same notes) */
    prj.scale = 1;
    sound_preset(T2, preset_named(E_FM, 11));   /* KALIMBA */
    sound_preset(T5, preset_named(E_KIT, KIT_DEMO));
    sound_preset(T6, preset_named(E_SAMPLE, BRK_DEMO));
    prj.snd[T1].p[P_ECHO] = 3;
    prj.snd[T2].p[P_ECHO] = 7;
    prj.snd[T2].p[P_VERB] = 6;
    prj.snd[T2].p[P_VOL] = 11;
    prj.snd[T4].p[P_VOL] = 10;
    prj.snd[T6].p[P_VOL] = 13;
    prj.snd[T1].p[P_VERB] = 3;
    for (p = 0; p < 7u; p++)
        prj.pat[p].len = 32;
    /* P1, P2: the overworld */
    lane(0, T1, "C5 . E5 G5 . E5 C6 . A5 . G5 . E5 . D5 . F5 . A5 . G5 E5 . C5 D5 . E5 . C5 . x .", 12);
    lane(1, T1, "E5 . G5 A5 . G5 E5 . D5 . E5 D5 C5 . A4 . B4 . C5 D5 . E5 G5 . F5 . E5 D5 C5 . . x", 12);
    lane(0, T3, "C3 . C4 . G3 . C4 . A2 . A3 . E3 . A3 . F2 . F3 . C3 . F3 . G2 . G3 . D3 . G3 .", 13);
    lane(1, T3, "A2 . A3 . E3 . A3 . F2 . F3 . C3 . F3 . G2 . G3 . D3 . G3 . C3 . C4 . G3 . C4 .", 13);
    for (p = 0; p < 2u; p++) {
        static const uint8_t CH[2][4] = {{72, 69, 65, 67}, {69, 65, 67, 72}};   /* roots: C Am F G / Am F G C */
        static const uint8_t MN[2][4] = {{0, 1, 0, 0}, {1, 0, 0, 0}};
        for (r = 0; r < 32u; r += 8u) {
            put(p, r + 2u, T2, CH[p][r / 8u], 9, FX_ARP, MN[p][r / 8u]);
            put(p, r + 6u, T2, CH[p][r / 8u], 7, FX_ARP, MN[p][r / 8u]);
        }
        lane(p, T4, ". . 96 . . . 96 96 . . 96 . . . 96 . . . 96 . . . 96 96 . . 96 . . 96 96 96", 9);
        lane(p, T5, "1 . . . 3 . . 1 1 . 1 . 3 . . . 1 . . . 3 . . 1 1 . 1 . 3 . 3 3", 13);
    }
    put(1, 4, T2, 96, 8, 0, 0);                 /* a counter-melody on the kalimba, between the chords */
    put(1, 12, T2, 100, 8, 0, 0);
    put(1, 20, T2, 98, 8, 0, 0);
    put(1, 28, T2, 103, 8, 0, 0);
    fx_at(1, 28, T1, FX_VIB, 6);
    fx_at(0, 6, T1, FX_GLIDE, 4);
    /* P3, P4: the dungeon (A minor, a 16th ostinato on the kalimba, drops of water) */
    lane(2, T2, "A4 E5 C5 E5 A4 E5 C5 E5 A#4 F5 D5 F5 A#4 F5 D5 F5 A4 E5 C5 E5 A4 E5 C5 E5 G#4 E5 B4 E5 G#4 E5 B4 E5", 8);
    lane(3, T2, "D5 A5 F5 A5 D5 A5 F5 A5 A4 E5 C5 E5 A4 E5 C5 E5 A#4 F5 D5 F5 A#4 F5 D5 F5 G#4 E5 B4 E5 G#4 E5 B4 E5", 8);
    lane(2, T3, "A2 . . . . . . . A#2 . . . . . . . A2 . . . . . . . E2 . . . . . . .", 12);
    lane(3, T3, "D3 . . . . . . . A2 . . . . . . . A#2 . . . . . . . E2 . . . . . . .", 12);
    lane(2, T1, ". . . . . . A6 x . . . . . . . . . . . . . . E6 x . . . . . . . .", 6);
    lane(3, T1, "E5 . . . . . F5 . E5 . . . D5 . . . C5 . . . B4 . . . G#4 . . . . . x .", 11);
    fx_at(3, 24, T1, FX_VIB, 7);
    fx_at(3, 6, T1, FX_GLIDE, 6);
    lane(2, T5, "1 . . . . . . . . . . . . . . . 1 . . . . . . . . . . . . . . .", 10);
    lane(3, T5, "1 . . . . . . . 4 . 1 . . . . . 1 . . . . . . . 4 . 1 . . . 4 4", 11);
    lane(3, T4, "96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 . 96 96", 5);
    fx_at(3, 30, T5, FX_ROLL, 2);
    /* P5, P6: the boss (breakcore: the break chopped, kicks, a riff in harmonic minor) */
    lane(4, T6, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 1 2 1 2 5 6 7 8 3 4 11 12 13 13 15 16", 13);
    lane(5, T6, "1 2 5 6 3 4 7 7 9 10 13 14 11 12 16 16 1 . 1 2 5 5 7 8 9 9 11 12 13 14 15 16", 13);
    fx_at(4, 29, T6, FX_ROLL, 1);
    fx_at(4, 31, T6, FX_ROLL, 3);
    fx_at(5, 8, T6, FX_REV, 0);
    fx_at(5, 14, T6, FX_ROLL, 2);
    fx_at(5, 20, T6, FX_PITCH, -5);
    fx_at(5, 31, T6, FX_ROLL, 5);
    lane(4, T5, "1 . . . 1 . . . 1 . . . 1 . 1 . 1 . . . 1 . . . 1 . . . 1 . 1 1", 15);
    lane(5, T5, "1 . . 1 1 . . . 1 . . 1 1 . 1 . 1 . . 1 1 . . . 1 . 1 . 1 1 1 1", 15);
    fx_at(5, 30, T5, FX_ROLL, 3);
    lane(4, T3, "A2 . A2 A3 . A2 C3 . G2 . G2 G3 . G2 A#2 . F2 . F2 F3 . F2 A2 . E2 . E2 E3 . G#2 B2 .", 14);
    lane(5, T3, "A2 . A2 A3 . A2 C3 . G2 . G2 G3 . G2 A#2 . F2 . F2 F3 . F2 A2 . E2 E3 E2 E3 G#2 G#3 B2 B3", 14);
    lane(4, T1, "A5 . . C6 . . B5 . G#5 . A5 . E5 . . . F5 . . E5 . . D5 . C5 . . . B4 . G#4 .", 12);
    lane(5, T1, "A5 . . . . . . . A#5 . . . . . . . A5 . . . . . . . G#5 . . . . . . .", 12);
    fx_at(4, 24, T1, FX_GLIDE, 5);
    fx_at(5, 0, T1, FX_ARP, 1);                 /* chip arpeggios: Am, Bb, Am, E dim */
    fx_at(5, 8, T1, FX_ARP, 0);
    fx_at(5, 16, T1, FX_ARP, 1);
    fx_at(5, 24, T1, FX_ARP, 8);
    lane(4, T2, "A4 . . . . . . . G4 . . . . . . . F4 . . . . . . . E4 . . . . . . .", 7);
    for (r = 0; r < 32u; r += 8u) fx_at(4, r, T2, FX_ARP, r ? 0 : 1);   /* Am G F E */
    lane(4, T4, "96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96", 6);
    lane(5, T4, "96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96 96", 7);
    fx_at(5, 15, T4, FX_ROLL, 2);
    for (r = 12; r < 16u; r++) prj.pat[5].live[r] = LF_LOOP32 + 1u;   /* live effects, recorded */
    prj.pat[5].live[30] = prj.pat[5].live[31] = LF_LOOP64 + 1u;
    for (r = 24; r < 30u; r++) prj.pat[5].live[r] = LF_LPF + 1u;
    /* P7: the victory */
    lane(6, T1, "G4 C5 E5 G5 C6 . . . . . . . A#5 . C6 . D6 . . . E6 . . . . . . . x . . .", 13);
    fx_at(6, 4, T1, FX_VIB, 4);
    fx_at(6, 20, T1, FX_VIB, 6);
    lane(6, T2, "C5 . . . . . . . F4 . . . A#4 . . . C5 . . . . . . . C7 . G6 . E6 . C6 .", 8);
    fx_at(6, 0, T2, FX_ARP, 0);
    fx_at(6, 8, T2, FX_ARP, 0);
    fx_at(6, 12, T2, FX_ARP, 0);
    fx_at(6, 16, T2, FX_ARP, 0);
    lane(6, T3, "C3 . C4 . C3 . C4 . F2 . F3 . A#2 . A#3 . C3 . . . . . . . . . . . x . . .", 13);
    lane(6, T5, "9 . . . 3 . . . 1 . . . 3 . 3 3 9 . . . . . . . . . . . . . . .", 14);
    fx_at(6, 15, T5, FX_ROLL, 3);
    {
        static const song_t SONG[7] = {{0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 1}};
        memcpy(prj.song, SONG, sizeof SONG);
        prj.song_len = 7;
    }
    prj.mode = 1;
    brushes_reset();
}

/* ---- undo and redo: the last edits of patterns (SAVE held: OCT- undo, OCT+ redo; SAVE held alone: undo) */
#define NUNDO 10u
/* a sound as it was (UNDO and REDO cover the sounds too, a factory sound loaded by mistake comes back): its engine, parameters
 * and SAT send, its table, its sample edit, and the FM patch (one track at most plays FM) */
typedef struct {
    sound_t snd;
    table_t tbl;
    sedit_t sed;
    uint8_t fmp[156];
} snap_t;
typedef union { pat_t p; snap_t s; } hslot_t;
typedef char snap_fits_a_slot[sizeof(snap_t) <= sizeof(pat_t) ? 1 : -1];
#define H_SND 0x80u                             /* idx: H_SND | track is a sound, else a pattern */
typedef struct {
    hslot_t slot[NUNDO];
    uint8_t idx[NUNDO], n, top;
    uint8_t sop[NUNDO], sat[NUNDO];             /* and the song with it (CLONE): 1 take entry sat out, 2 put it back */
} hist_t;
static hist_t undo_h, redo_h;
static uint32_t undo_pat_last = 0xFFu, undo_ms_last;
static uint8_t dirty;                           /* changed since the last save into a slot of the user's (the '*') */
static uint8_t auto_due;                        /* changed since the autosave */
static uint32_t dirty_ms;
static int song_insert(uint32_t at, uint32_t pat, uint32_t rep);
static void song_delete(uint32_t at);
static void touched(void)
{
    dirty = 1;
    auto_due = 1;
    dirty_ms = fm1_ms;
}
static void hist_push(hist_t *h, uint32_t p)
{
    h->slot[h->top].p = prj.pat[p];
    h->idx[h->top] = (uint8_t)p;
    h->sop[h->top] = 0;
    h->top = (uint8_t)((h->top + 1u) % NUNDO);
    if (h->n < NUNDO)
        h->n++;
}
static void hist_push_snd(hist_t *h, uint32_t t)
{
    snap_t *s = &h->slot[h->top].s;
    s->snd = prj.snd[t];
    s->tbl = prj.tbl[t];
    s->sed = prj.sed[t];
    memcpy(s->fmp, prj.fmp, sizeof s->fmp);
    h->idx[h->top] = (uint8_t)(H_SND | t);
    h->sop[h->top] = 0;
    h->top = (uint8_t)((h->top + 1u) % NUNDO);
    if (h->n < NUNDO)
        h->n++;
}
/* put the last state back, keep the present in other: the pattern's number, 0x100 | track for a sound, -1 nothing,
 * -2 an FM sound that cannot come back (another track plays FM now) */
static int hist_pop(hist_t *h, hist_t *other)
{
    uint32_t p, op, at, o;
    if (!h->n)
        return -1;
    h->top = (uint8_t)((h->top + NUNDO - 1u) % NUNDO);
    h->n--;
    p = h->idx[h->top];
    if (p & H_SND) {
        uint32_t t = p & 7u;
        const snap_t *s = &h->slot[h->top].s;
        if (t >= NTRK || (s->snd.engine == E_FM && !fm_free(t))) {
            h->top = (uint8_t)((h->top + 1u) % NUNDO);   /* (it stays where it was) */
            h->n++;
            return -2;
        }
        hist_push_snd(other, t);
        if (s->snd.engine != prj.snd[t].engine)
            ev_push(EV_KILL, t, 0, 0, 0, 0);    /* another engine: the old one's voices would never end */
        prj.snd[t] = s->snd;
        prj.tbl[t] = s->tbl;
        prj.sed[t] = s->sed;
        if (s->snd.engine == E_FM)
            memcpy(prj.fmp, s->fmp, sizeof prj.fmp);
        sound_build(t);
        undo_pat_last = 0xFFu;
        touched();
        return (int)(0x100u | t);
    }
    op = h->sop[h->top];
    at = h->sat[h->top];
    hist_push(other, p);
    o = (other->top + NUNDO - 1u) % NUNDO;
    prj.pat[p] = h->slot[h->top].p;
    if (op == 1u && at < prj.song_len && prj.song[at].pat == p && prj.song_len > 1u) {
        song_delete(at);                        /* CLONE undone: its song entry goes too */
        other->sop[o] = 2;
        other->sat[o] = (uint8_t)at;
    } else if (op == 2u && song_insert(at, p, 1)) {
        other->sop[o] = 1;                      /* redone: and back into the song */
        other->sat[o] = (uint8_t)at;
    }
    undo_pat_last = 0xFFu;
    touched();
    return (int)p;
}
static void undo_push_merge(uint32_t p, int merge)   /* before an edit of pattern p; merge: knob turns and notes */
{                                                       /* typed within 1.5 s of the last edit are one step */
    redo_h.n = 0;                               /* a new edit: what was undone stays undone */
    if (merge && p == undo_pat_last && fm1_ms - undo_ms_last < 1500u) {
        undo_ms_last = fm1_ms;
        touched();
        return;
    }
    hist_push(&undo_h, p);
    undo_pat_last = merge ? p : 0xFFu;
    undo_ms_last = fm1_ms;
    touched();
}
static void undo_push(uint32_t p) { undo_push_merge(p, 0); }   /* a step of its own */
static void undo_push_snd(uint32_t t, int merge)   /* before an edit of track t's sound: knob turns within 1.5 s are one step */
{
    redo_h.n = 0;
    if (merge && undo_pat_last == (H_SND | t) && fm1_ms - undo_ms_last < 1500u) {
        undo_ms_last = fm1_ms;
        touched();
        return;
    }
    hist_push_snd(&undo_h, t);
    undo_pat_last = merge ? (H_SND | t) : 0xFFu;
    undo_ms_last = fm1_ms;
    touched();
}
static int undo_pop(void) { return hist_pop(&undo_h, &redo_h); }   /* the pattern put back, or -1 */
static int redo_pop(void) { return hist_pop(&redo_h, &undo_h); }
static void hist_clear(void) { undo_h.n = redo_h.n = 0; }

/* ---- the song list */
static int pat_empty(const pat_t *p)
{
    uint32_t r, t;
    for (r = 0; r < NROW; r++) {
        if (p->live[r])
            return 0;
        for (t = 0; t < NTRK; t++)
            if (p->c[r][t].note || p->c[r][t].fx || p->c[r][t].fx2)
                return 0;
    }
    return 1;
}
static int song_insert(uint32_t at, uint32_t pat, uint32_t rep)   /* an entry at `at`; 0 when full */
{
    uint32_t k;
    if (prj.song_len >= NSONG)
        return 0;
    if (at > prj.song_len)
        at = prj.song_len;
    for (k = prj.song_len; k > at; k--)
        prj.song[k] = prj.song[k - 1u];
    prj.song[at].pat = (uint8_t)pat;
    prj.song[at].rep = (uint8_t)(rep ? rep : 1u);
    prj.song_len++;
    if (sq_play && prj.mode && at <= sq_songpos)
        sq_adj += 1;                            /* the entry playing moved one on: so does the sequencer */
    touched();
    return 1;
}
static void song_delete(uint32_t at)
{
    uint32_t k;
    if (at >= prj.song_len || prj.song_len <= 1u)
        return;
    for (k = at; k + 1u < prj.song_len; k++)
        prj.song[k] = prj.song[k + 1u];
    prj.song_len--;
    if (sq_play && prj.mode) {
        if (at < sq_songpos)
            sq_adj -= 1;                        /* one before it went: the sequencer moves back one */
        else if (at == sq_songpos)
            sq_goto = (uint8_t)(at < prj.song_len ? at : 0u);   /* this one went: after it, the one that followed */
    }
    touched();
}
static int song_find(uint32_t pat)              /* the first entry that plays pattern pat, or -1 */
{
    uint32_t k;
    for (k = 0; k < prj.song_len; k++)
        if (prj.song[k].pat == pat)
            return (int)k;
    return -1;
}

/* ---- the clipboard: a track's rows, or a whole pattern */
static pat_t clip_pat;
static cell_t clip_lane[NROW];
static uint8_t clip_kind, clip_len, clip_trk;   /* 0 empty, 1 a track, 2 a pattern */

/* ---- the magic tools: on the track (TRACK) or on the whole pattern (PATTERN) */
static uint8_t mg_clone_to = 0xFF;              /* CLONE: the new pattern (the screen goes to it) */

static void mg_chop(pat_t *p, uint32_t t, uint32_t inten)
{
    uint32_t r, len = p->len, src = (uint32_t)clampi(prj.snd[t].p[P_SRC], 0, BRK_N + NUSR - 1), brk = src < BRK_N ? src : 99u;
    if (note_mode(t) == NM_SLICE) {
        for (r = 0; r < len; r++) {
            uint32_t s = r % 16u, k, pick = s;
            cell_t *c = &p->c[r][t];
            if ((r % 8u) == 0u)
                pick = (r % 16u) == 0u ? 0u : 8u;           /* the downbeats keep a kick */
            else if ((r % 8u) == 4u && brk < BRK_N) {
                for (k = 0; k < 16u; k++) {                 /* a snare on 2 and 4 */
                    uint32_t q = (r + k * 5u) % 16u;
                    if (BRK_TAG[brk][q] & 2u) { pick = q; break; }
                }
            } else if (rnd(16) < inten)
                pick = rnd(16);
            c->note = (uint8_t)(pick + 1u);
            c->vol = (uint8_t)(r % 4u ? 12u : 14u);
            c->fx = c->fx2 = 0;
            c->fxv = c->fxv2 = 0;
            if ((r % 8u) == 7u && rnd(24) < inten) { c->fx = FX_ROLL; c->fxv = (uint8_t)rnd(5); }
            else if (rnd(40) < inten) { c->fx = FX_REV; }
            else if (rnd(56) < inten) { c->fx = FX_PITCH; c->fxv = (uint8_t)(int8_t)(rnd(2) ? 12 : -5); }
        }
        if (inten > 10u && len >= 4u) {                     /* a roll into the next bar */
            p->c[len - 1u][t].fx = FX_ROLL;
            p->c[len - 1u][t].fxv = (uint8_t)(3u + rnd(3));
        }
        return;
    }
    for (r = 0; r + 4u <= len; r += 4u) {                   /* other tracks: shuffle beats of 4 rows */
        if (rnd(16) < inten) {
            uint32_t a = rnd(len / 4u) * 4u, k;
            for (k = 0; k < 4u; k++) {
                cell_t x = p->c[r + k][t];
                p->c[r + k][t] = p->c[a + k][t];
                p->c[a + k][t] = x;
            }
        }
    }
}

static void mg_beat(pat_t *p, uint32_t t, uint32_t inten)
{
    uint32_t r, len = p->len, e = prj.snd[t].engine;
    for (r = 0; r < len; r++)
        p->c[r][t].note = p->c[r][t].fx = p->c[r][t].fxv = p->c[r][t].fx2 = p->c[r][t].fxv2 = 0;
    if (e == E_KIT) {                           /* low: four on the floor; mid: broken; high: gabber */
        uint32_t kick = inten > 10u ? 2u : 1u;
        for (r = 0; r < len; r++) {
            cell_t *c = &p->c[r][t];
            uint32_t b = r % 16u;
            int hit = 0;
            if (inten < 5u) hit = b % 4u == 0u;
            else if (inten < 11u) hit = b == 0u || b == 6u || b == 10u || (b == 13u && rnd(2));
            else hit = b % 4u == 0u || b == 3u || b == 6u || b == 10u || b == 14u || rnd(6) == 0u;
            if (hit) { c->note = (uint8_t)kick; c->vol = (uint8_t)(b % 4u ? 12u : 15u); }
            else if ((b == 4u || b == 12u) && inten >= 3u) { c->note = 3; c->vol = 13; }   /* SNARE */
            else if (inten >= 7u && rnd(6) == 0u) { c->note = 5; c->vol = 7; }             /* a hat */
        }
        if (inten > 8u) { cell_t *c = &p->c[len - 1u][t]; c->note = 3; c->vol = 12; c->fx = FX_ROLL; c->fxv = 2; }
        return;
    }
    if (note_mode(t) == NM_SLICE) {
        mg_chop(p, t, inten);
        return;
    }
    for (r = 0; r < len; r++) {                 /* the noise: hats; the others: a rhythm on the brush note */
        cell_t *c = &p->c[r][t];
        int hit = e == E_NOISE ? ((r % 2u) == 1u || rnd(16) < inten) : (r % 4u == 0u || rnd(24) < inten);
        if (hit) {
            c->note = (uint8_t)brush_note[t];
            c->vol = (uint8_t)(r % 2u ? 8u + rnd(4) : 11u);
            if (e == E_NOISE && rnd(24) < inten) { c->fx = FX_ROLL; c->fxv = (uint8_t)rnd(3); }
        }
    }
}

static void mg_melo(pat_t *p, uint32_t t, uint32_t inten, int bass)
{
    uint32_t r, len = p->len, base = bass ? 45u : lane_base(t), n;
    int32_t deg = 0;
    if (!IS_TONAL(t) || prj.snd[t].engine == E_NOISE) {
        mg_beat(p, t, inten);
        return;
    }
    if (is_bass(t) && !bass)
        base = 57u;
    n = scale_snap(base);
    for (r = 0; r < len; r++) {
        cell_t *c = &p->c[r][t];
        uint32_t dens = bass ? (r % 4u == 0u ? 16u : 4u + inten / 2u) : 5u + inten;
        c->fx = c->fxv = c->fx2 = c->fxv2 = 0;
        if (r && rnd(20) >= dens) {             /* a rest (the first row always plays) */
            c->note = (bass || rnd(3)) ? 0u : NOTE_OFF;
            c->vol = 0;
            continue;
        }
        if (bass) {
            static const int8_t STEPS[6] = {0, 0, 4, 7, 3, 5};
            deg = STEPS[rnd(6)];
            c->note = (uint8_t)(rnd(4) ? scale_degree(deg, n) : scale_degree(deg, n) + 12u);
        } else {
            deg += (int32_t)rnd(5) - 2;
            deg = clampi(deg, -4, 9);
            c->note = (uint8_t)scale_degree(deg, n);
            if (rnd(30) < inten) { c->fx = FX_ARP; c->fxv = (uint8_t)(in_scale(c->note + 4u) ? 0u : 1u); }
            else if (rnd(40) < inten) { c->fx = FX_GLIDE; c->fxv = 8; }
            else if (rnd(50) < inten) { c->fx = FX_VIB; c->fxv = 6; }
        }
        c->vol = (uint8_t)(r % 4u ? 11u : 13u);
    }
}

static void mg_arp(pat_t *p, uint32_t t)
{
    uint32_t r;
    for (r = 0; r < p->len; r++) {
        cell_t *c = &p->c[r][t];
        if (c->note && c->note != NOTE_OFF && IS_TONAL(t)) {
            c->fx = FX_ARP;
            c->fxv = in_scale(c->note + 4u) ? 0u : in_scale(c->note + 3u) ? 1u : 3u;
        }
    }
}

static void mg_eucl(pat_t *p, uint32_t t, uint32_t inten, uint32_t rot)
{
    uint32_t r, len = p->len, k = 1u + inten * len / 16u, acc = 0;
    if (k > len) k = len;
    for (r = 0; r < len; r++) {
        cell_t *c = &p->c[(r + rot) % len][t];
        acc += k;
        c->fx = c->fxv = c->fx2 = c->fxv2 = 0;
        if (acc >= len) {
            acc -= len;
            c->note = (uint8_t)brush_note[t];
            c->vol = 12;
        } else
            c->note = 0;
    }
}

static void mg_vary(pat_t *p, uint32_t t, uint32_t inten)
{
    uint32_t r, len = p->len;
    for (r = 0; r < len; r++) {
        cell_t *c = &p->c[r][t];
        if (rnd(32) >= inten + 2u)
            continue;
        switch (rnd(4)) {
        case 0:                                 /* another note near it */
            if (c->note && c->note != NOTE_OFF) {
                if (IS_TONAL(t)) c->note = (uint8_t)scale_step(c->note, rnd(2) ? 1 : -1);
                else c->note = (uint8_t)(1u + (cell_idx(c->note) + 1u + rnd(15)) % 16u);
            }
            break;
        case 1: {                               /* trade with a neighbour */
            uint32_t q = (r + 1u) % len;
            cell_t x = *c;
            *c = p->c[q][t];
            p->c[q][t] = x;
            break;
        }
        case 2:                                 /* a trick */
            if (c->note && c->note != NOTE_OFF && !c->fx) {
                static const uint8_t PICK[5] = {FX_ROLL, FX_VIB, FX_BEND, FX_CHANCE, FX_ARP};
                c->fx = PICK[rnd(5)];
                c->fxv = (uint8_t)FXD[c->fx].def;
                if (!IS_TONAL(t) && c->fx != FX_ROLL && c->fx != FX_CHANCE) c->fx = FX_ROLL, c->fxv = 1;
            }
            break;
        default:                                /* volume */
            if (c->note) c->vol = (uint8_t)clampi((int32_t)c->vol + (int32_t)rnd(7) - 3, 3, 15);
            break;
        }
    }
}

static void mg_rolls(pat_t *p, uint32_t t, uint32_t inten)
{
    uint32_t r, len = p->len;
    for (r = 3; r < len; r += 4u) {
        cell_t *c = &p->c[r][t];
        if (rnd(16) > inten && r != len - 1u)
            continue;
        if (!c->note)
            c->note = (uint8_t)(prj.snd[t].engine == E_KIT ? 3u : note_mode(t) == NM_SLICE ? 13u : brush_note[t]), c->vol = 12;
        c->fx = FX_ROLL;
        c->fxv = (uint8_t)(r == len - 1u ? 3u + rnd(4) : rnd(3));
    }
}

static void lane_apply(uint32_t tool, pat_t *p, uint32_t t, uint32_t inten, uint32_t seed)
{
    uint32_t r, len = p->len;
    switch (tool) {
    case MG_CHOP: mg_chop(p, t, inten); break;
    case MG_BEAT: mg_beat(p, t, inten); break;
    case MG_MELO: mg_melo(p, t, inten, 0); break;
    case MG_BASS: mg_melo(p, t, inten, 1); break;
    case MG_ARP: mg_arp(p, t); break;
    case MG_EUCL: mg_eucl(p, t, inten, seed % (len ? len : 1u)); break;
    case MG_VARY: mg_vary(p, t, inten); break;
    case MG_ROLLS: mg_rolls(p, t, inten); break;
    case MG_NUANCE:
        for (r = 0; r < len; r++)
            if (p->c[r][t].note && p->c[r][t].note != NOTE_OFF)
                p->c[r][t].vol = (uint8_t)clampi(r % 4u == 0u ? 15 : r % 2u ? 8 + (int32_t)rnd(1u + inten / 3u) : 12, 1, 15);
        break;
    case MG_SHIFT: {
        cell_t last = p->c[len - 1u][t];
        for (r = len - 1u; r > 0u; r--)
            p->c[r][t] = p->c[r - 1u][t];
        p->c[0][t] = last;
        break;
    }
    case MG_TRANSP:
        for (r = 0; r < len; r++) {
            cell_t *c = &p->c[r][t];
            if (!c->note || c->note == NOTE_OFF) continue;
            if (IS_TONAL(t)) c->note = (uint8_t)(prj.lock ? scale_step(c->note, 1) : (c->note < 119u ? c->note + 1u : c->note));
            else c->note = (uint8_t)(1u + (cell_idx(c->note) + 1u) % 16u);
        }
        break;
    case MG_CLEAR:
        for (r = 0; r < NROW; r++)
            memset(&p->c[r][t], 0, sizeof(cell_t));
        break;
    }
}

/* CLONE: the pattern into the next empty one, and into the song after it (where the cursor's entry is) */
static const char *mg_clone(uint32_t pi, uint32_t song_at)
{
    uint32_t k, q = NPAT;
    if (pat_in_pair(pi))
        return TR("MOTIF LIE", "LINKED", "ENLAZADO");   /* (a 64-step pair: its two halves would not both come) */
    for (k = 1; k < NPAT; k++) {
        uint32_t c = (pi + k) % NPAT;
        if (pat_empty(&prj.pat[c]) && !pat_in_pair(c)) { q = c; break; }   /* (an empty half of a pair is not free) */
    }
    if (q >= NPAT)
        return TR("PLUS DE PLACE", "NO ROOM", "SIN ESPACIO");
    undo_push(q);
    prj.pat[q] = prj.pat[pi];
    prj.pat[q].pad[0] = 0;                      /* (a copy goes on into nothing) */
    if (song_at >= prj.song_len || prj.song[song_at].pat != pi) {
        int f = song_find(pi);
        song_at = f < 0 ? prj.song_len : (uint32_t)f;
    }
    if (song_insert(song_at + 1u, q, 1)) {      /* (undone with the pattern) */
        uint32_t o = (undo_h.top + NUNDO - 1u) % NUNDO;
        undo_h.sop[o] = 1;
        undo_h.sat[o] = (uint8_t)(song_at + 1u);
    }
    mg_clone_to = (uint8_t)q;
    return "OK";
}

/* one tool, on the track or the pattern; returns a word for the screen */
static const char *magic_run(uint32_t tool, uint32_t pi, uint32_t t, uint32_t scope_all, uint32_t inten, uint32_t seed,
                             uint32_t song_at)
{
    pat_t *p = &prj.pat[pi];
    uint32_t k;
    rnd_main ^= seed * 0x9E3779B1u + fm1_ms;
    if (!rnd_main)
        rnd_main = 1;
    if (tool == MG_COPY) {
        if (scope_all) { clip_pat = *p; clip_kind = 2; }
        else {
            for (k = 0; k < NROW; k++) clip_lane[k] = p->c[k][t];
            clip_kind = 1;
            clip_len = p->len;
            clip_trk = (uint8_t)t;
        }
        return TR("COPIE", "COPIED", "COPIADO");
    }
    if (tool == MG_CLONE)
        return mg_clone(pi, song_at);
    if (tool == MG_PASTE && !clip_kind)
        return TR("VIDE", "EMPTY", "VACIO");    /* (refused: no undo step, the redo stays) */
    if (tool == MG_DOUBLE && p->len > 16u)
        return "MAX 32";
    undo_push(pi);                              /* each tool its own undo step */
    if (tool == MG_PASTE) {
        if (clip_kind == 2) { uint8_t lk = p->pad[0]; *p = clip_pat; p->pad[0] = lk; }   /* (its own link stays) */
        else
            for (k = 0; k < NROW; k++) p->c[k][t] = clip_lane[k];
        return TR("COLLE", "PASTED", "PEGADO");
    }
    if (tool == MG_DOUBLE) {
        uint32_t len = p->len, r, tt;
        for (r = 0; r < len; r++)
            for (tt = 0; tt < NTRK; tt++)
                if (scope_all || tt == t) p->c[r + len][tt] = p->c[r][tt];
        for (r = 0; r < len && scope_all; r++) p->live[r + len] = p->live[r];
        p->len = (uint8_t)(len * 2u);
        return "X2";
    }
    if (!scope_all) {
        lane_apply(tool, p, t, inten, seed);
        return "OK";
    }
    for (k = 0; k < NTRK; k++) {                /* the whole pattern: each track gets what suits it */
        uint32_t tl = tool, tonal = IS_TONAL(k) && prj.snd[k].engine != E_NOISE;
        if (tool == MG_MELO && is_bass(k)) tl = MG_BASS;
        if ((tool == MG_MELO || tool == MG_BASS) && !tonal) tl = MG_BEAT;
        if (tool == MG_BASS && tonal && !is_bass(k)) continue;
        if (tool == MG_BEAT && tonal) continue;
        if (tool == MG_CHOP && note_mode(k) != NM_SLICE) continue;
        if (tool == MG_ARP && (!tonal || is_bass(k))) continue;
        if (tool == MG_TRANSP && !IS_TONAL(k)) continue;
        if (tool == MG_EUCL && k != t) continue;
        lane_apply(tl, p, k, inten, seed + k);
    }
    if (tool == MG_CLEAR)
        memset(p->live, 0, sizeof p->live);
    return "OK";
}

/* ---- the store: four slots of 36 KiB from STORE_LO [project_t with its CRC]; the user samples after
 * them (USR_AT: 4 x 37 KiB, up to STORE_HI) */
#define SLOT_SIZE 0x9000u
#define NSLOT 4u
#define USR_AT (STORE_LO + NSLOT * SLOT_SIZE)
typedef char prj_fits_a_slot[sizeof(project_t) <= SLOT_SIZE ? 1 : -1];
#define V4_CELL 4u                              /* format 4 (KAWA 0.2): cells of 4 bytes, no tables, no groove */
#define V4_PAT (NROW * NTRK * V4_CELL + NROW + 4u)
static uint32_t prj_body(uint32_t ver)          /* the bytes the CRC covers (the CRC follows them) */
{
    uint32_t pat_at = (uint32_t)((uint8_t *)&prj.pat - (uint8_t *)&prj);
    return ver == 4u ? pat_at + NPAT * V4_PAT : (uint32_t)((uint8_t *)&prj.crc - (uint8_t *)&prj);
}
static uint32_t slot_at(uint32_t k) { return STORE_LO + k * SLOT_SIZE; }

#if defined(DINGHY_HOST) || defined(KAWA_WASM)
static uint8_t host_flash[NSLOT][SLOT_SIZE];
static int store_read(uint32_t k, uint32_t off, void *p, uint32_t n) { memcpy(p, host_flash[k] + off, n); return 0; }
static uint32_t store_gen;                      /* counts the saves (the emulator keeps the slots when it changes) */
static int store_write(uint32_t k, const void *p, uint32_t n) { memcpy(host_flash[k], p, n); store_gen++; return 0; }
#else
static int store_read(uint32_t k, uint32_t off, void *p, uint32_t n) { return st_read(slot_at(k) + off, p, n); }
static int store_write(uint32_t k, const void *p, uint32_t n)
{
    uint32_t a = slot_at(k), s, took;
    int rc;
    if (!flash_ok)
        return -1;
    for (s = 0; s < SLOT_SIZE; s += 0x1000u)
        if ((rc = fl_erase4k(a + s, &took)) != 0)
            return rc;
    if ((rc = fl_write(a + 8u, (const uint8_t *)p + 8u, n - 8u)) != 0)   /* the magic last: a cut save stays invalid */
        return rc;
    return fl_write(a, (const uint8_t *)p, 8u);
}
#endif

static uint32_t crc_step(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static uint32_t prj_crc(const project_t *p)
{
    return ~crc_step(0xFFFFFFFFu, (const uint8_t *)p, (uint32_t)((const uint8_t *)&p->crc - (const uint8_t *)p));
}

static int prj_save(uint32_t k)
{
    prj.magic = PRJ_MAGIC;
    prj.version = PRJ_VERSION;
    if (k && k < NSLOT)
        prj.home = (uint8_t)k;
    prj.unsaved = k ? 0u : dirty;               /* the autosave keeps the '*': it comes back at boot */
    prj.crc = prj_crc(&prj);
    if (store_write(k, &prj, sizeof prj))
        return -1;
    if (k) {
        dirty = 0;                              /* saved into a slot of the user's: the '*' goes, */
        auto_due = 1;                           /* and the autosave follows (it is what the FM-1 starts with) */
    } else
        auto_due = 0;                           /* the autosave leaves the '*' alone */
    return 0;
}

static int slot_valid(uint32_t k, char *name)   /* the slot holds a project: its format (4 or 5), else 0 (its name */
{                                               /* into name, 16 B) */
    uint32_t h[2], c = 0xFFFFFFFFu, off, body, crc_at;
    uint8_t buf[256];
    uint32_t crc;
    if (store_read(k, 0, h, 8) || h[0] != PRJ_MAGIC || (h[1] != PRJ_VERSION && h[1] != 4u))
        return 0;
    body = crc_at = prj_body(h[1]);
    for (off = 0; off < body; off += 256u) {    /* the CRC by pieces: no second copy of the project in RAM */
        uint32_t n = body - off < 256u ? body - off : 256u;
        if (store_read(k, off, buf, n))
            return 0;
        c = crc_step(c, buf, n);
        if (off == 0u && name)
            memcpy(name, buf + 8, 16);
    }
    if (store_read(k, crc_at, &crc, 4))
        return 0;
    return ~c == crc ? (int)h[1] : 0;
}

/* format 4 to 5, in place: each pattern widened from the last, each cell from the last (a cell lands at or
 * after where it was: nothing not yet read is written over) */
static void prj_from_v4(void)
{
    uint8_t *base = (uint8_t *)prj.pat;
    int32_t p, i;
    for (p = (int32_t)NPAT - 1; p >= 0; p--) {
        uint8_t *src = base + (uint32_t)p * V4_PAT, *dst = base + (uint32_t)p * sizeof(pat_t);
        for (i = (int32_t)NROW + 3; i >= 0; i--)   /* LIVE, the length (from the end: the FM-1 has no memmove) */
            dst[NROW * NTRK * sizeof(cell_t) + (uint32_t)i] = src[NROW * NTRK * V4_CELL + (uint32_t)i];
        for (i = (int32_t)(NROW * NTRK) - 1; i >= 0; i--) {
            const uint8_t *c4 = src + (uint32_t)i * V4_CELL;
            uint8_t n = c4[0], v = c4[1], f = c4[2], fv = c4[3];
            cell_t *c = (cell_t *)(void *)(dst + (uint32_t)i * sizeof(cell_t));
            c->note = n;
            c->vol = (uint8_t)(v & 15u);
            c->fx = (uint8_t)(f < FX_HOP ? f : FX_NONE);
            c->fxv = fv;
            c->fx2 = FX_NONE;
            c->fxv2 = 0;
        }
    }
    prj_v5_defaults();
    prj.version = PRJ_VERSION;
}

/* the edit's NORMALISE: the gain that brings the edited part's peak to full scale (x 1 .. x 16) */
static void sed_normalise(uint32_t t)
{
    sedit_t *e = &prj.sed[t];
    const uint8_t *d;
    uint32_t len, rate, a, b, peak = 0;
    e->gain = 256;
    if (prj.snd[t].engine != E_SAMPLE || !e->norm ||
        !smp_source((uint32_t)clampi(prj.snd[t].p[P_SRC], 0, BRK_N + NUSR - 1), &d, &len, &rate))
        return;
    a = sed_at(e->start, len);
    b = sed_at(e->end, len);
    for (; a < b && a < len; a++) {
        int32_t x = BRK_ULAW[d[a]];
        uint32_t ax = (uint32_t)(x < 0 ? -x : x);
        if (ax > peak) peak = ax;
    }
    if (peak > 0u)
        e->gain = (uint16_t)clampi((int32_t)(31000u * 256u / peak), 256, 4096);
}

static void sed_fix(sedit_t *e)                 /* the edit in order: start < end, the slices between them, rising */
{
    uint32_t i;
    if (e->start + 512u > e->end) e->start = e->end > 512u ? (uint16_t)(e->end - 512u) : 0u;
    if (e->start + 512u > e->end) e->end = (uint16_t)(e->start + 512u);
    e->sl[0] = e->start;
    for (i = 1; i < 16u; i++)
        e->sl[i] = (uint16_t)clampi(e->sl[i], e->sl[i - 1u], e->end);
    e->rev = e->rev ? 1u : 0u;
    e->manual = e->manual ? 1u : 0u;
    e->norm = e->norm ? 1u : 0u;
    e->gain = (uint16_t)clampi(e->gain, 64, 4096);
}

static void prj_sanitize(void)                  /* a loaded project: nothing out of range reaches the voices */
{
    uint32_t t, i, k;
    prj.name[15] = 0;
    if (!prj.song_len || prj.song_len > NSONG) prj.song_len = 1;
    for (k = 0; k < NSONG; k++) {
        if (prj.song[k].pat >= NPAT) prj.song[k].pat = 0;
        if (!prj.song[k].rep || prj.song[k].rep > 16u) prj.song[k].rep = 1;
    }
    for (t = 0; t < NTRK; t++) {
        sound_t *s = &prj.snd[t];
        if (s->engine >= E_N) s->engine = E_PULSE;
        if (s->sat > 15u) s->sat = 0;           /* (0.5: the SAT send, in what 0.4 kept as padding) */
        for (i = 0; i < NPARAM; i++) {
            const pdesc_t *d = &PDESC[s->engine][i];
            if (d->name[0]) s->p[i] = (int8_t)clampi(s->p[i], d->lo, d->hi);
        }
        sound_build(t);
    }
    for (k = 0; k < NPAT; k++) {
        uint32_t r;
        if (!prj.pat[k].len || prj.pat[k].len > NROW) prj.pat[k].len = 16;
        prj.pat[k].pad[0] &= 1u;                /* (0.5: a 64-step pattern goes on into the next one; 0.4's pad is 0) */
        if (prj.pat[k].pad[0] && (k + 1u >= NPAT || (k && (prj.pat[k - 1u].pad[0] & 1u))))
            prj.pat[k].pad[0] = 0;              /* (a pair is two patterns: a second half goes on into nothing, the last one neither) */
        for (r = 0; r < NROW; r++)
            for (t = 0; t < NTRK; t++) {
                cell_t *c = &prj.pat[k].c[r][t];
                if (c->fx >= FX_N) c->fx = FX_NONE;
                if (c->fx2 >= FX_N) c->fx2 = FX_NONE;
            }
    }
    for (i = 0; i < 4u; i++)
        prj.mfx[i] = (int8_t)clampi(prj.mfx[i], 0, i ? 15 : FXT_N - 1);
    for (t = 0; t < NTRK; t++) {                /* format 5: the tables, the sample edits, the groove */
        table_t *tb = &prj.tbl[t];
        tb->on = tb->on ? 1u : 0u;
        tb->speed = (uint8_t)clampi(tb->speed, 1, 24);
        tb->len = (uint8_t)clampi(tb->len, 1, NTSTEP);
        tb->loop = (uint8_t)clampi(tb->loop, 0, NTSTEP);
        for (i = 0; i < NTSTEP; i++) {
            tstep_t *st = &tb->s[i];
            st->tr = (int8_t)clampi(st->tr, -24, 24);
            st->vol = (uint8_t)clampi(st->vol, 0, 15);
            if (st->fx >= TB_N) st->fx = TB_NONE;
            st->fxv = (int8_t)clampi(st->fxv, TBD[st->fx].lo, TBD[st->fx].hi);
        }
        sed_fix(&prj.sed[t]);
    }
    prj.grv.on = prj.grv.on ? 1u : 0u;
    prj.grv.len = (uint8_t)clampi(prj.grv.len, 1, 16);
    for (i = 0; i < 16u; i++)
        prj.grv.tick[i] = (uint8_t)clampi(prj.grv.tick[i], 1, 12);
}

static int prj_load(uint32_t k)
{
    uint32_t t;
    int ver = slot_valid(k, 0);
    if (!ver)
        return -1;
    if (store_read(k, 0, &prj, ver == 4 ? prj_body(4u) + 4u : sizeof prj))
        return -1;
    if (ver == 4)
        prj_from_v4();                          /* a project of KAWA 0.2: it plays as it did */
    prj_sanitize();
    for (t = 0; t < NTRK; t++)
        sed_normalise(t);                       /* (a user's sample may have changed since) */
    if (prj.home >= NSLOT)
        prj.home = 0;
    if (k)
        prj.home = (uint8_t)k;
    dirty = k ? 0u : (prj.unsaved != 0u);       /* the autosave: what was not saved still is not */
    auto_due = 0;
    hist_clear();
    brushes_reset();
    return 0;
}

/* ---- the user samples. In the emulator they come in by drag and drop (web/wasm_host.c: decoded,
 * mono, 22050 or 11025 Hz, 8-bit mu-law as the breaks). On the FM-1 the slots stay empty until the
 * transfer over USB comes (their place in flash is kept: USR_AT) */
#if defined(DINGHY_HOST) || defined(KAWA_WASM)
static uint8_t usr_mem[NUSR][USR_CAP];
static void usr_put(uint32_t k, const uint8_t *d, uint32_t len, uint32_t rate, const char *name)
{
    uint32_t i;
    if (k >= NUSR)
        return;
    if (len > USR_CAP)
        len = USR_CAP;
    usr_data[k] = 0;                            /* (the voices stop reading it while it changes) */
    for (i = 0; i < len; i++)
        usr_mem[k][i] = d[i];

    usr_hdr[k].magic = USR_MAGIC;
    usr_hdr[k].len = len;
    usr_hdr[k].rate = rate == 11025u ? 11025u : 22050u;
    for (i = 0; i < 11u && name && name[i]; i++)
        usr_hdr[k].name[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    usr_hdr[k].name[i] = 0;
    usr_data[k] = usr_mem[k];
}
#endif
static const char *usr_name(uint32_t k)         /* the slot's name for the screen */
{
    if (k < NUSR && usr_data[k] && usr_hdr[k].name[0])
        return usr_hdr[k].name;
    return 0;
}
