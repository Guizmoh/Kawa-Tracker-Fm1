/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: the eight pages, the panel, the presets, MIDI, app_main.
 *
 *   HOME 01 PLAY     EDIT 02 BASS     ENV 03 ENVELOPE  SEQ 04 FILTER + SEQ BASS
 *   LFO  05 LFO      FX   06 FX       ARP 07 PRESETS + ARP  GLO 08 SYSTEM
 *   the page's button again, or SEL: its next view (OSC A, OSC B, SUB, COMBINE...)
 *   SELECT: the row   ALGORITHM: the row's value   K1..K4: what the footer says   PRESETS: the next patch
 *   PLAY: SEQ BASS play / stop   REC: TAP tempo (SEQ view: step record)   OCT- / OCT+: octave (both: all off)
 *   SAVE: save; held: OCT- UNDO, OCT+ REDO (PRESETS: FAVORITE)
 * Held buttons (a loading bar says what comes): a page button 1.2 s: RANDOM on that page; with K1..K4: that page's
 * knobs from anywhere (SHIFT); LFO or ENV held + a knob or ALGO: assigns LFO 1 / the mod envelope to it (QUICK ASSIGN);
 * SEL held: hear the saved sound (COMPARE); SAVE held on PRESETS: FAVORITE (elsewhere UNDO); PLAY held: HOLD.
 * Page buttons, SEL and PLAY act on release. */
enum { PG_PLAY, PG_BASS, PG_ENV, PG_FILT, PG_LFO, PG_FX, PG_PRE, PG_SYS, NPAGE };
static const uint8_t PAGE_BTN[NPAGE] = {BTN_HOME, BTN_EDIT, BTN_ENV, BTN_SEQ, BTN_LFO, BTN_FX, BTN_ARP, BTN_GLO};
enum { LY_PLAY, LY_LIST, LY_BOX, LY_STEP, LY_LFO, LY_PRESET, LY_SAVE, LY_SYS };
enum { I_L1RATE = 200, I_L2RATE, I_ABMIX, G_MIDICH, G_BPM, G_CLOCK, G_OCT, G_CORES, G_CPU, G_FW, G_THEME, G_PAL, G_FONT, G_GRID, G_BROWSE, G_ACCEL, G_C3COL, G_UI, G_ASK, G_ZOOM, G_MLEARN, G_MMAP, G_MSTART, G_MPROG, G_MCLK,
       G_SQ_STEP, G_SQ_NOTE, G_SQ_TYPE, G_SQ_PAT, G_SQ_LEN, G_SQ_RATE, G_SQ_SWING,   /* SEQ BASS */
       G_AR_ON, G_AR_MODE, G_AR_RATE, G_AR_OCT, G_AR_GATE,                          /* ARP */
       G_LV_FLT, G_LV_PUMP, G_LV_STUT, G_LV_TAPE, G_LV_PRATE, G_LV_FRES,             /* SEL: FX LIVE */
       G_MC1, G_MC2, G_MC3, G_MC4, G_MC_DST, G_MC_AMT,                               /* SEL: MACROS */
       G_EQ_F, G_EQ_G, G_EQ_T, G_EQ_Q,                                               /* FX held: the EQ (the band eq_cur) */
       I_NONE = 255 };

typedef struct {
    uint8_t lay;
    const char *cat;
    uint8_t n, it[20];                          /* the rows, in the order shown: K1..K4 are the first four */
    const char *tag;
} view_t;
#define OSCV(L, X) {LY_LIST, "OSCILLATOR " L, 8, {X##_ENG, X##_P1, X##_P2, X##_P3, X##_LVL, X##_OCT, X##_SEMI, X##_FINE}, "EDIT"}
#define FXV(...) {LY_LIST, "FX RACK", 4, {__VA_ARGS__}, "FX"}
static const view_t VIEWS[] = {
    {LY_PLAY, "CURRENT PATCH", 4, {F_CUT, I_L1RATE, L1_DEP, F_RES}, "HOME"},
    {LY_PLAY, "LIVE MIX", 4, {S_LVL, DR_AMT, DL_MIX, MS_VOL}, "HOME"},
    OSCV("A", A), OSCV("B", B),
    {LY_LIST, "OSCILLATOR C \xB7 SAMPLE", 6, {SM_SEL, SM_LVL, SM_MODE, SM_SPEED, SM_TUNE, SM_KEY}, "EDIT"},
    {LY_LIST, "SUB + NOISE", 6, {S_LVL, S_OCT, S_WAVE, N_LVL, N_COL, S_ROUTE}, "EDIT"},
    {LY_LIST, "COMBINE A + B", 9, {X_MODE, A_LVL, B_LVL, I_ABMIX, X_AMT, V_GLIDE, V_MODE, X_DRIFT, X_RESET}, "EDIT"},
    {LY_BOX, "AMP ENVELOPE", 4, {AE_A, AE_D, AE_S, AE_R}, "ENV"},
    {LY_BOX, "FILTER ENVELOPE", 4, {FE_A, FE_D, FE_S, FE_R}, "ENV"},
    {LY_BOX, "MOD ENVELOPE", 4, {ME_A, ME_D, ME_AMT, ME_DST}, "ENV"},
    {LY_BOX, "FILTER / COLOUR", 4, {F_CUT, F_RES, F_DRV, F_TYPE}, "FX"},
    {LY_LIST, "FILTER MOD", 6, {F_ENV, F_KEY, F_VEL, F_TYPE, F_CUT, F_RES}, "FX"},
    {LY_LIST, "SEQ BASS", 7, {G_SQ_STEP, G_SQ_NOTE, G_SQ_TYPE, G_SQ_PAT, G_SQ_LEN, G_SQ_RATE, G_SQ_SWING}, "SEQ"},
    {LY_LFO, "LFO 1 \xB7 WOBBLE", 9, {I_L1RATE, L1_DEP, L1_SHP, L1_DST, G_GRID, L1_SYNC, L1_TRIG, L1_PH, L1_SMO}, "LFO"},
    {LY_LFO, "LFO 2 \xB7 MOTION", 9, {I_L2RATE, L2_DEP, L2_SHP, L2_DST, G_GRID, L2_SYNC, L2_TRIG, L2_PH, L2_SMO}, "LFO"},
    {LY_STEP, "STEP FX", 7, {ST_PAT, ST_RATE, ST_GATE, ST_DEP, ST_ON, ST_DST, ST_SMO}, "LFO"},
    {LY_LIST, "MOD MATRIX", 12, {M1_AMT, M2_AMT, M3_AMT, M4_AMT, M1_SRC, M1_DST, M2_SRC, M2_DST, M3_SRC, M3_DST, M4_SRC, M4_DST}, "LFO"},
    FXV(DR_AMT, DR_MIX, DR_TYPE, DR_ON),
    {LY_LIST, "FX RACK", 6, {CP_AMT, CP_PUMP, CP_ATK, CP_REL, CP_PRATE, CP_ON}, "FX"},
    FXV(CR_BITS, CR_RATE, CR_MIX, CR_ON), FXV(CH_RATE, CH_DEP, CH_MIX, CH_ON),
    {LY_LIST, "FX RACK", 5, {PH_RATE, PH_DEP, PH_FB, PH_MIX, PH_ON}, "FX"},
    {LY_LIST, "FX RACK", 6, {DL_TIME, DL_FB, DL_MIX, DL_TYPE, DL_TONE, DL_ON}, "FX"},
    {LY_LIST, "FX RACK", 5, {RV_SIZE, RV_MIX, RV_TYPE, RV_DAMP, RV_ON}, "FX"},
    {LY_LIST, "FX RACK", 3, {MS_VOL, MS_GLUE, MS_SUB}, "FX"},
    {LY_PRESET, "PRESET BANK A", 0, {0}, "PRESET"},
    {LY_PRESET, "PRESET BANK B", 0, {0}, "PRESET"},
    {LY_SAVE, "SAVE PATCH", 0, {0}, "PRESET"},
    {LY_LIST, "ARPEGGIATOR", 5, {G_AR_ON, G_AR_MODE, G_AR_RATE, G_AR_OCT, G_AR_GATE}, "ARP"},
    {LY_SYS, "GLOBAL SETUP", 17, {G_BPM, V_MODE, V_GLIDE, G_ACCEL, G_UI, G_ASK, G_THEME, G_PAL, G_FONT, G_ZOOM, G_C3COL, G_GRID, G_BROWSE,
                                  V_BEND, V_VEL, G_CORES, G_CPU}, "SYSTEM"},
    {LY_SYS, "MIDI", 7, {G_MLEARN, G_MMAP, G_MIDICH, G_CLOCK, G_MSTART, G_MPROG, G_MCLK}, "SYSTEM"},
};
/* what K1..K4 turn: the first four rows (the preset lists and SAVE have their own) */
static uint32_t vknob(const view_t *v, uint32_t i) { return v->lay == LY_PRESET || v->lay == LY_SAVE || i >= v->n ? I_NONE : v->it[i]; }
static const uint8_t FX_ON[7] = {DR_ON, CP_ON, CR_ON, CH_ON, PH_ON, DL_ON, RV_ON};   /* the FX page's views, in order */
static const uint8_t PAGE_V0[NPAGE + 1] = {0, 2, 7, 10, 13, 17, 25, 29, 31};   /* each page's first view */
#define V_SEQ 12u                               /* SEQ BASS (the SEQ button's third view), ARP (the ARP button's last) */
#define V_ARP 28u
static const char *const FX_TITLE[8] = {"SATURATION", "COMPRESSOR", "BITCRUSH", "CHORUS", "PHASER", "DELAY", "REVERB", "MASTER"};
static const char *const FT_TITLE[NFT] = {"DARK EDGE", "SOFT EDGE", "BAND RAZOR", "THIN AIR", "HOLLOW", "TALK BOX", "METAL COMB"};
static const char *const FT_SUB[NFT] = {"LOW PASS \xB7 24 dB", "LOW PASS \xB7 12 dB", "BAND PASS \xB7 12 dB", "HIGH PASS \xB7 24 dB",
                                        "NOTCH \xB7 12 dB", "FORMANT \xB7 A E I O U", "COMB \xB7 TUNED BY CUTOFF"};

/* ---- state */
#define NUSER 32u
typedef struct {
    uint8_t magic, sum;
    char name[14];
    int8_t p[NP_STORE];
} upatch_t;                                     /* 128 B: 32 of them fill one 4 KiB sector of the store */
#define UP_MAGIC 0xB5u
static upatch_t ustore[NUSER];
#define UX_MAGIC 0x59u                          /* (0x58: the same with 16 a slot, before PLAY and SPEED) */
#define UX_AT 0x400u                            /* (in the 2nd sector, after SEQ BASS / ARP / MACROS) */
static struct {                                 /* the user slots' settings past NP_STORE (compressor, phaser, types) */
    uint8_t magic, sum;
    int8_t p[NUSER][NP_MAX - NP_STORE];
} uext;
typedef char ids_fit[G_EQ_Q < I_NONE ? 1 : -1];
typedef char perf_fits_before_uext[sizeof perf <= UX_AT ? 1 : -1];   /* (the 2nd sector: SEQ/ARP/MACROS, then the slots' rest) */
static uint8_t ux_sum(void) { const uint8_t *b = (const uint8_t *)uext.p; uint32_t i, x = 0x77; for (i = 0; i < sizeof uext.p; i++) x = (x * 7u + b[i]) & 0xFFu; return (uint8_t)x; }
static void ux_migrate(void)                    /* a store saved with 16 a slot: spread out, the new ones at 0 */
{
    uint8_t old[NUSER * 16u];
    uint32_t i, x = 0x77, k;
    if (uext.magic != 0x58u) return;
    memcpy(old, uext.p, sizeof old);
    for (i = 0; i < sizeof old; i++) x = (x * 7u + old[i]) & 0xFFu;
    if (x != uext.sum) { uext.magic = 0; return; }
    memset(uext.p, 0, sizeof uext.p);
    for (k = 0; k < NUSER; k++) memcpy(uext.p[k], old + k * 16u, 16u);
    uext.magic = UX_MAGIC;
    uext.sum = ux_sum();
}
static struct {
    uint8_t ver, midich, clock, oct;            /* midich 0: omni; clock 0 internal, 1 MIDI */
    uint16_t bpm10;
    uint8_t bank, idx, cores;                   /* cores 1: both (the voices on core 1) */
    uint8_t theme, pal, font;                   /* CLASSIC STUDIO PURE RACK; its palettes; the font */
    uint8_t grid;                               /* LFO GRID: 0 ALL, 1 STRAIGHT (round divisions only) */
    uint8_t browse, fav[12];                    /* PRESETS: 0 all, 1 favorites only; the favorites (A then B) */
    uint8_t accel;                              /* the knobs' acceleration: 0 OFF, 1 ON, 2 FAST (appended: older saves load) */
    uint8_t c3col;                              /* SHIFT's three columns: 0 the theme's colour, 1 green / yellow / orange */
    uint8_t ui, ask;                            /* UI MODE: 0 CLASSIC, 1 SHIFT; ask which at each start */
    uint8_t zoom;                               /* LOUPE: the rows' and values' type as big as they fit */
    uint8_t mstart, mprog, mclk;                /* MIDI: start/stop the SEQ, program change loads, the clock out */
} set = {6, 0, 0, 0xFF, 1400, 0, 0, 1, 0, 0, 0, 0, 0, {0}, 1, 0, KS_B, 1, 0, 1, 1, 0};
static char pname[14];
static uint8_t page, vcur[NPAGE], cursor[sizeof VIEWS / sizeof VIEWS[0]], pre_cur[2], save_slot, save_pos, hold;
static uint8_t key_note[27], latched[128], phys[128];
static uint32_t prev_keys, toast_ms, set_ms, last_turn_ms, tap_ms[4];
static uint8_t set_dirty, prev_dirty = 1;
static char toast_txt[24];
static int16_t prev_wave[224], prev_a[112], prev_b[112], prev_s[112];   /* the pictures of the sound */
static uint8_t led_want[41];
#define OCT ((int32_t)(int8_t)set.oct)

static void toast(const char *s)
{
    uint32_t i;
    for (i = 0; i < 23u && s[i]; i++) toast_txt[i] = s[i];
    toast_txt[i] = 0;
    toast_ms = fm1_ms | 1u;
}
static const view_t *view(void) { return &VIEWS[PAGE_V0[page] + vcur[page]]; }
static uint32_t vidx(void) { return PAGE_V0[page] + vcur[page]; }

#if KS_DUAL
#define DUAL_MAGIC 0x4B533143u                  /* "KS1C": a start on two cores not yet confirmed (survives a reset) */
static uint32_t dual_mark[2] __attribute__((section(".noinit")));
static uint32_t dual_ms;
#endif

/* ---- the user store (one sector) */
#if defined(KAWA_WASM)
static uint8_t w_store[8192];
static uint32_t w_store_gen;
static void store_load(void) { memcpy(ustore, w_store, sizeof ustore); memcpy(&perf, w_store + 4096, sizeof perf); memcpy(&uext, w_store + 4096 + UX_AT, sizeof uext); }
static int store_save(void) { memcpy(w_store, ustore, sizeof ustore); w_store_gen++; return 0; }
static int perf_save(void) { memcpy(w_store + 4096, &perf, sizeof perf); memcpy(w_store + 4096 + UX_AT, &uext, sizeof uext); w_store_gen++; return 0; }
static int settings_write(void) { return settings_save(&set, sizeof set); }
#else
static void store_load(void)
{
    if (!flash_ok || st_read(STORE_LO, ustore, sizeof ustore)) memset(ustore, 0xFF, sizeof ustore);
    if (!flash_ok || st_read(STORE_LO + 0x1000u, &perf, sizeof perf)) memset(&perf, 0xFF, sizeof perf);
    if (!flash_ok || st_read(STORE_LO + 0x1000u + UX_AT, &uext, sizeof uext)) memset(&uext, 0xFF, sizeof uext);
}
static int perf_save(void)                      /* the store's second sector: SEQ BASS, ARP, MACROS, the slots' rest */
{
    uint32_t took;
    int rc;
    if (!flash_ok) return -1;
    c1_pause();
    rc = fl_erase4k(STORE_LO + 0x1000u, &took);
    if (!rc) rc = fl_write(STORE_LO + 0x1000u, (const uint8_t *)&perf, sizeof perf);
    if (!rc) rc = fl_write(STORE_LO + 0x1000u + UX_AT, (const uint8_t *)&uext, sizeof uext);
    c1_resume();
    return rc;
}
static int store_save(void)                     /* (core 1 waits in RAM while the flash is busy) */
{
    uint32_t took;
    int rc;
    if (!flash_ok) return -1;
    c1_pause();
    rc = fl_erase4k(STORE_LO, &took);
    if (!rc) rc = fl_write(STORE_LO, (const uint8_t *)ustore, sizeof ustore);
    c1_resume();
    return rc;
}
static int settings_write(void)
{
    int rc;
    c1_pause();
    rc = settings_save(&set, sizeof set);
    c1_resume();
    return rc;
}
#endif
/* ---- SEQ BASS, ARP: kept apart from the patches (a line goes on when the sound changes) */
static uint8_t sq_cur_step, perf_dirty;
static uint32_t perf_ms;
static const char *const L_SQTYPE[5] = {"REST", "NOTE", "ACCENT", "SLIDE", "ACC+SLIDE"};
static const uint8_t SQ_FL[5] = {0, SF_ON, SF_ON | SF_ACC, SF_ON | SF_SLIDE, SF_ON | SF_ACC | SF_SLIDE};
static const char *const L_ARMODE[5] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "ORDER"};
static const char *const NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
/* the first lines, one token a step: a note and its octave above C2 ("D#1"), ^ accent, ~ slide, - a rest */
static const char *const SQ_DEF[8][2] = {
    {"DNB ROLL", "C0^ - - C0 - - C0 - - - C0^ - D#0 - C0 -"},
    {"DUB STEPPA", "C0^ - C0 - G0 - F0 - C0^ - C0 - D#0 - F0~ G0"},
    {"TECHNO OFF", "- - C0 - - - C0^ - - - C0 - - - C0 C1"},
    {"HALFTIME", "C0^~ C0 - - - - - - F0^ - - - D#0~ C0 - -"},
    {"ACID", "C0 C1^ - C0~ D#0 C0 G0^ - C0 A#0~ C1 - C0^ F0 - D#0"},
    {"2-STEP", "C0^ - - C0 - - G0 - - C0 - - A#0~ G0 - -"},
    {"OCTAVES", "C0 C1 C0 C1 C0 C1 C0 C1 D#0 D#1 D#0 D#1 F0 F1 G0 G1"},
    {"ROOT", "C0 - - - C0 - - - C0 - - - C0 - - -"}};
static uint8_t perf_sum(void)                   /* (up to the macros: they check themselves, mc_magic) */
{
    const uint8_t *b = (const uint8_t *)&perf;
    uint32_t i, x = 0x3C, n = (uint32_t)((const uint8_t *)&perf.mc_magic - b);
    for (i = 2; i < n; i++) x = (x * 5u + b[i]) & 0xFFu;
    return (uint8_t)x;
}
/* the first MACROS: FILTER (cutoff, resonance), WOBBLE (depth, rate), GRIT (filter drive, noise, saturation), TONE
 * (both oscillators' first control, X-MOD) */
static void mc_defaults(void)
{
    static const uint8_t D[16] = {D_CUT, D_RES, 0, 0, D_WOB, D_L1RATE, 0, 0, D_DRV, D_NOISE, D_DRAMT, 0, D_AP1, D_BP1, D_XMOD, 0};
    static const int8_t A[16] = {44, 18, 0, 0, 48, 16, 0, 0, 60, 20, 40, 0, 40, 40, 30, 0};
    memcpy(perf.mc_dst, D, 16);
    memcpy(perf.mc_amt, A, 16);
    perf.mc_magic = MC_MAGIC;
}
/* ---- the SEL windows: 1 FX LIVE, 2 MACROS (SEL again: the next, then back to the page) */

/* ---- the EQ (FX held): K1..K4 the bands' frequencies, OCT- held their gains, OCT+ held their types, SELECT a band,
 * ALGO its width; on every sound, kept with SEQ BASS */
static const char *const L_EQT[NEQT] = {"OFF", "LOW CUT", "LOW SHELF", "BELL", "NOTCH", "HIGH SHELF", "HIGH CUT"};
static const char *const L_EQS[NEQT] = {"OFF", "LCUT", "LSHF", "BELL", "NTCH", "HSHF", "HCUT"};
static uint8_t eq_cur, eq_dirty = 1;
static int16_t eq_curve[112];                   /* the response, dB x 16, 20 Hz .. 18 kHz (the EQ window) */
static void eq_defaults(void)
{
    static const uint8_t T[4] = {EQ_LOSH, EQ_BELL, EQ_BELL, EQ_HISH}, F[4] = {10, 19, 32, 41};
    uint32_t k;
    for (k = 0; k < 4u; k++) { perf.eq_t[k] = T[k]; perf.eq_f[k] = F[k]; perf.eq_q[k] = 3; perf.eq_g[k] = 0; }
    perf.eq_magic = 0x45u;
}
static int eq_live(uint32_t k)                  /* a band that does something */
{
    uint32_t t = perf.eq_t[k];
    return t == EQ_LOCUT || t == EQ_HICUT || t == EQ_NOTCH || ((t == EQ_LOSH || t == EQ_BELL || t == EQ_HISH) && perf.eq_g[k]);
}
static void eq_coef(uint32_t k, int32_t *o)     /* RBJ's biquads, Q24, a0 taken out without a 64-bit division */
{
    const int64_t ONE = 1 << 24;
    uint32_t ph = KS_EQ_INC[perf.eq_f[k] % 48u], gi = (uint32_t)clampi(perf.eq_g[k] + 12, 0, 24);
    int64_t s = (int64_t)sin32(ph) << 9, c = (int64_t)sin32(ph + 0x40000000u) << 9;
    int64_t al = (s * KS_EQ_IQ[perf.eq_q[k] & 7u]) >> 14, A = (int64_t)KS_EQ_A[gi] << 10, IA = (int64_t)KS_EQ_IA[gi] << 10;
    int64_t Ap = A + ONE, Am = A - ONE, sa = ((int64_t)KS_EQ_SQ[gi] << 10) * al >> 23, b0, b1, b2, a0, a1, a2;
    int32_t inv;
    switch (perf.eq_t[k]) {
    case EQ_LOCUT: b0 = (ONE + c) / 2; b1 = -(ONE + c); b2 = b0; a0 = ONE + al; a1 = -2 * c; a2 = ONE - al; break;
    case EQ_HICUT: b0 = (ONE - c) / 2; b1 = ONE - c; b2 = b0; a0 = ONE + al; a1 = -2 * c; a2 = ONE - al; break;
    case EQ_NOTCH: b0 = ONE; b1 = -2 * c; b2 = ONE; a0 = ONE + al; a1 = -2 * c; a2 = ONE - al; break;
    case EQ_LOSH:
        b0 = (A * (Ap - (Am * c >> 24) + sa)) >> 24; b1 = (2 * A * (Am - (Ap * c >> 24))) >> 24; b2 = (A * (Ap - (Am * c >> 24) - sa)) >> 24;
        a0 = Ap + (Am * c >> 24) + sa; a1 = -2 * (Am + (Ap * c >> 24)); a2 = Ap + (Am * c >> 24) - sa; break;
    case EQ_HISH:
        b0 = (A * (Ap + (Am * c >> 24) + sa)) >> 24; b1 = (-2 * A * (Am + (Ap * c >> 24))) >> 24; b2 = (A * (Ap + (Am * c >> 24) - sa)) >> 24;
        a0 = Ap - (Am * c >> 24) + sa; a1 = 2 * (Am - (Ap * c >> 24)); a2 = Ap - (Am * c >> 24) - sa; break;
    default:                                    /* BELL */
        b0 = ONE + (al * A >> 24); b1 = -2 * c; b2 = ONE - (al * A >> 24); a0 = ONE + (al * IA >> 24); a1 = -2 * c; a2 = ONE - (al * IA >> 24); break;
    }
    inv = (int32_t)((1 << 30) / (int32_t)(a0 >> 10));   /* (1/a0, Q16) */
    o[0] = (int32_t)((b0 * inv) >> 16); o[1] = (int32_t)((b1 * inv) >> 16); o[2] = (int32_t)((b2 * inv) >> 16);
    o[3] = (int32_t)((a1 * inv) >> 16); o[4] = (int32_t)((a2 * inv) >> 16);
}
static void p_eqhead(char *t)                  /* the band being set, its type and width (the EQ's top line) */
{
    char *p = str_cat(t, "BAND ");
    *p++ = (char)('1' + (eq_cur & 3u)); *p = 0;
    p = str_cat(p, " \xB7 ");
    p = str_cat(p, L_EQT[perf.eq_t[eq_cur & 3u] % NEQT]);
    p = str_cat(p, " \xB7 Q ");
    p = fmt_u(p, KS_EQ_Q10[perf.eq_q[eq_cur & 3u] & 7u] / 10u, 0);
    *p++ = '.'; fmt_u(p, KS_EQ_Q10[perf.eq_q[eq_cur & 3u] & 7u] % 10u, 0);
}
static void eq_update(void)                     /* the coefficients to the audio (once it took the last), the curve */
{
    int32_t q[4][5];
    uint32_t k, x, mask = 0;
    if (eq_new) return;                         /* (the audio has not taken the last ones yet: next time) */
    for (k = 0; k < 4u; k++) if (eq_live(k)) { eq_coef(k, q[k]); mask |= 1u << k; }
    memcpy(eq_n, q, sizeof q);
    eq_nmask = (uint8_t)mask;
    eq_new = 1;
    eq_dirty = 0;
    for (x = 0; x < 112u; x++) {                /* the curve: each band's shape in octaves from its frequency (the sound */
        int32_t fq8 = (int32_t)(x * 47u * 256u / 111u), db = 0;   /* itself is the biquads: this is their picture) */
        for (k = 0; k < 4u; k++) {
            int32_t d = (fq8 - (int32_t)(perf.eq_f[k] % 48u) * 256) * 53 / 256, q2 = (int32_t)KS_EQ_Q10[perf.eq_q[k] & 7u] * 2 / 10 + 1;
            int32_t g = perf.eq_g[k] * 16, u, sm, ad = d < 0 ? -d : d;   /* (d: octaves Q8, 0.209 octave a step) */
            if (!(mask >> k & 1u)) continue;
            u = d * q2;
            sm = clampi(128 - d * 64 / 256, 0, 256);                   /* (a shelf's slope: 0 to 1 over 4 octaves) */
            switch (perf.eq_t[k]) {
            case EQ_BELL: db += g * 256 / (256 + (int32_t)(((int64_t)u * u) >> 8)); break;
            case EQ_NOTCH: db += -24 * 16 * 256 / (256 + (int32_t)(((int64_t)u * u * 16) >> 8)); break;
            case EQ_LOSH: db += g * sm / 256; break;
            case EQ_HISH: db += g * (256 - sm) / 256; break;
            case EQ_LOCUT: db += d < 0 ? -48 - 12 * 16 * ad / 256 : -48 * 256 / (256 + ((int32_t)(((int64_t)d * d) >> 8)) * 8); break;
            default: db += d > 0 ? -48 - 12 * 16 * ad / 256 : -48 * 256 / (256 + ((int32_t)(((int64_t)d * d) >> 8)) * 8); break;   /* HIGH CUT */
            }
        }
        eq_curve[x] = (int16_t)clampi(db, -24 * 16, 24 * 16);
    }
}
static uint8_t ml_state, ml_id;                 /* MIDI LEARN: 1 waits for a setting, 2 for a CC; the setting */
static uint32_t cc_count(void) { uint32_t k, n = 0; if (perf.cc_magic == 0x43u) for (k = 0; k < 16u; k++) n += perf.cc_num[k] != 0xFFu; return n; }
static uint8_t win, win_cur, mc_pos;            /* the window, its row (FX LIVE), the target being edited (MACROS: 0..31) */
static const uint8_t LV_IT[6] = {G_LV_FLT, G_LV_PUMP, G_LV_STUT, G_LV_TAPE, G_LV_PRATE, G_LV_FRES};
static void perf_defaults(void)
{
    uint32_t k, i;
    memset(&perf, 0, sizeof perf);
    perf.magic = PF_MAGIC; perf.ar_rate = 3; perf.ar_oct = 0; perf.ar_gate = 5;
    for (k = 0; k < SQ_NPAT; k++) {
        seqpat_t *q = &perf.pat[k];
        const char *t = k < 8u ? SQ_DEF[k][1] : "";
        q->len = 16; q->rate = 3;
        for (i = 0; i < 16u && *t; i++) {
            while (*t == ' ') t++;
            if (*t == '-') { t++; continue; }
            if (*t >= 'A' && *t <= 'G') {
                static const uint8_t L2N[7] = {9, 11, 0, 2, 4, 5, 7};
                int32_t n = L2N[*t++ - 'A'];
                if (*t == '#') { n++; t++; }
                if (*t >= '0' && *t <= '3') n += (*t++ - '0') * 12;
                q->note[i] = (uint8_t)clampi(n + 12, 0, 48);
                q->fl[i] = SF_ON;
                while (*t == '^' || *t == '~') q->fl[i] |= *t++ == '^' ? SF_ACC : SF_SLIDE;
            }
        }
    }
    perf.sum = perf_sum();
    mc_defaults();
}
static void perf_touch(void) { perf.sum = perf_sum(); perf_dirty = 1; perf_ms = fm1_ms; }
static void seq_play(int on)
{
    ev_push(EV_SEQ, on ? 1u : 0u, set.clock ? 0u : 1u);
    if (!on) sq_rec = 0;
}

static uint8_t up_sum(const upatch_t *u)
{
    uint32_t i, s = 0x5A;
    for (i = 0; i < sizeof u->name; i++) s += (uint8_t)u->name[i];
    for (i = 0; i < NP_STORE; i++) s = (s * 3u + (uint8_t)u->p[i]) & 0xFFu;
    return (uint8_t)s;
}
static int up_valid(uint32_t k) { return k < NUSER && ustore[k].magic == UP_MAGIC && ustore[k].sum == up_sum(&ustore[k]); }
static void user_get(uint32_t k, int8_t *p)     /* a user slot, whole: sector 1, then its rest (or the defaults) */
{
    patch_defaults(p);
    memcpy(p, ustore[k].p, NP_STORE);
    if (uext.magic == UX_MAGIC && uext.sum == ux_sum()) memcpy(p + NP_STORE, uext.p[k], NP_MAX - NP_STORE);
    patch_sanitize(p);
}

/* the engines a patch plays: "SUPER + ANALOG", "FM x FOLD" (ring), "VOCAL < FM" (B cross-modulates A) */
static char *eng_str(char *p, const int8_t *pp)
{
    int a = pp[A_LVL] || pp[X_MODE] == 1, b = pp[B_LVL] || pp[X_MODE];
    if (a) p = str_cat(p, L_ENG[pp[A_ENG] & 7]);
    if (a && b) p = str_cat(p, pp[X_MODE] == 1 ? " x " : pp[X_MODE] == 2 ? " < " : " + ");
    if (b) p = str_cat(p, L_ENG[pp[B_ENG] & 7]);
    if (!a && !b) p = str_cat(p, "SUB");
    return p;
}
static uint8_t dpop_id;                         /* the destination list on screen (for this parameter) */
static uint32_t dpop_ms;
static uint8_t pdirty;                          /* the sound changed since it was loaded or saved */
static uint8_t pend_on, pend_bank, pend_k;      /* a load waiting for SEL (the sound changed) */
static uint32_t pend_ms;

/* ---- undo: the patch before each change (turns of the same knob within 1.5 s are one change), 16 deep */
#define NUNDO 16u
static int8_t undo_p[NUNDO][NP_MAX];
static char undo_nm[NUNDO][14];
static uint32_t undo_w, undo_n, undo_ms, undo_id = 0xFFFFu;
static uint8_t booted;
static int8_t redo_p[NUNDO][NP_MAX];             /* what UNDO took back, for REDO (a new change clears it) */
static char redo_nm[NUNDO][14];
static uint32_t redo_n;
static uint8_t ur_on;                           /* SAVE held: OCT- UNDO, OCT+ REDO */
static void undo_keep(void)                     /* the sound now onto the undo ring */
{
    memcpy(undo_p[undo_w], patch, NP_MAX);
    memcpy(undo_nm[undo_w], pname, 14);
    undo_w = (undo_w + 1u) % NUNDO;
    if (undo_n < NUNDO) undo_n++;
}
static void undo_push(void)
{
    if (!booted) return;
    undo_keep();
    redo_n = 0;
}
static void undo_mark(uint32_t id)              /* a parameter is about to change */
{
    pdirty = 1;
    if (id != undo_id || fm1_ms - undo_ms > 1500u) undo_push();
    undo_id = id;
    undo_ms = fm1_ms;
}
static void undo(void)
{
    char t[16], *p;
    if (!undo_n) { toast("NOTHING TO UNDO"); return; }
    if (redo_n == NUNDO) {                      /* (full: the oldest goes) */
        uint32_t k;
        for (k = 1; k < NUNDO; k++) { memcpy(redo_p[k - 1u], redo_p[k], NP_MAX); memcpy(redo_nm[k - 1u], redo_nm[k], 14); }
        redo_n--;
    }
    memcpy(redo_p[redo_n], patch, NP_MAX);
    memcpy(redo_nm[redo_n], pname, 14);
    redo_n++;
    undo_w = (undo_w + NUNDO - 1u) % NUNDO;
    undo_n--;
    memcpy(patch, undo_p[undo_w], NP_MAX);
    memcpy(pname, undo_nm[undo_w], 14);
    undo_id = 0xFFFFu;
    prev_dirty = 1;
    p = str_cat(t, "UNDO \xB7 ");
    p = fmt_u(p, undo_n, 0);
    str_cat(p, " LEFT");
    toast(t);
}
static void redo(void)
{
    char t[16], *p;
    if (!redo_n) { toast("NOTHING TO REDO"); return; }
    undo_keep();
    redo_n--;
    memcpy(patch, redo_p[redo_n], NP_MAX);
    memcpy(pname, redo_nm[redo_n], 14);
    undo_id = 0xFFFFu;
    prev_dirty = 1;
    pdirty = 1;
    p = str_cat(t, "REDO \xB7 ");
    p = fmt_u(p, redo_n, 0);
    str_cat(p, " LEFT");
    toast(t);
}

/* ---- favorites: a bit for each preset (A, then the user's B) */
static uint32_t fav_i(uint32_t bank, uint32_t k) { return bank ? NFACTORY + k : k; }
static int fav_is(uint32_t bank, uint32_t k) { uint32_t i = fav_i(bank, k); return i < 96u && (set.fav[i >> 3] >> (i & 7u) & 1u); }
static int fav_any(void) { uint32_t i; for (i = 0; i < sizeof set.fav; i++) if (set.fav[i]) return 1; return 0; }

/* ---- compare: SEL held plays the saved sound, released the edited one comes back */
static int8_t cmp_buf[NP_MAX];
static uint8_t cmp_on;

/* ---- the value being turned, big, for a second (before -> after) */
static uint8_t vpop_id;
static uint32_t vpop_ms;
static char vpop_before[24], vpop_pre[12];
static uint32_t splash_until;
static struct { uint8_t b, used, fired; uint32_t at, next; } hb = {0xFF, 0, 0, 0, 0};   /* the button held */
/* SHIFT mode (SYSTEM > UI MODE, asked at each start): a page's first screen is 12 settings in three columns of four,
 * one row a knob: the knobs alone move the left column (the base), OCT- held the middle one, OCT+ held the right one
 * (OCT tapped: the octave). The page's button (or SEL) then goes through its other views, and back. CLASSIC: the views
 * only, OCT is the octave. ini: the cell's initials (0: the engine's) */
typedef struct { const char *title, *head[3]; uint8_t id[3][4]; const char *ini[3][4]; } col3_t;
static const col3_t COL3[NPAGE] = {
    [PG_PLAY] = {"PLAY \xB7 LIVE", {"LIVE", "MIX", "TONE"},
                 {{F_CUT, I_L1RATE, L1_DEP, F_RES}, {S_LVL, DR_AMT, DL_MIX, MS_VOL}, {A_P1, B_P1, X_AMT, F_ENV}},
                 {{"CUT", "RT", "DP", "RES"}, {"SUB", "SAT", "DLY", "VOL"}, {"A1", "B1", "XM", "ENV"}}},
    [PG_BASS] = {"EDIT \xB7 OSCILLATORS", {"MIX", "OSC A", "OSC B"},
                 {{A_LVL, B_LVL, I_ABMIX, X_AMT}, {A_ENG, A_P1, A_P2, A_P3}, {B_ENG, B_P1, B_P2, B_P3}},
                 {{"A", "B", "A/B", "XM"}, {"ENG", 0, 0, 0}, {"ENG", 0, 0, 0}}},   /* (the engine on top, K1) */
    [PG_ENV] = {"ENVELOPES", {"AMP", "FILTER", "MOD"},
                {{AE_A, AE_D, AE_S, AE_R}, {FE_A, FE_D, FE_S, FE_R}, {ME_A, ME_D, ME_AMT, ME_DST}},
                {{"A", "D", "S", "R"}, {"A", "D", "S", "R"}, {"A", "D", "AMT", "DST"}}},
    [PG_FILT] = {"FILTER", {"FILTER", "KEYS", "SUB"},
                 {{F_CUT, F_RES, F_DRV, F_ENV}, {F_TYPE, F_KEY, F_VEL, S_ROUTE}, {S_WAVE, S_OCT, N_LVL, N_COL}},
                 {{"CUT", "RES", "DRV", "ENV"}, {"TYP", "KEY", "VEL", "RTE"}, {"WAV", "OCT", "NOI", "COL"}}},
    [PG_LFO] = {"LFO \xB7 LIVE", {"WOBBLE", "LFO 1", "LFO 2"},
                {{I_L1RATE, L1_DEP, L1_DST, G_GRID}, {L1_SHP, L1_PH, L1_SMO, L1_TRIG}, {L2_SHP, I_L2RATE, L2_DEP, L2_DST}},
                {{"RT", "DP", "DST", "GRD"}, {"SHP", "PH", "SMO", "TRG"}, {"SHP", "RT", "DP", "DST"}}},
    [PG_FX] = {"FX RACK", {"SPACE", "DRIVE", "DYNAMICS"},
               {{DL_TIME, DL_FB, DL_MIX, RV_MIX}, {DR_TYPE, DR_AMT, DR_MIX, CR_BITS}, {CP_AMT, CP_PUMP, PH_MIX, MS_VOL}},
               {{"DT", "FB", "DLY", "REV"}, {"TYP", "SAT", "MIX", "BIT"}, {"CMP", "PMP", "PHA", "VOL"}}},
};
static uint8_t c3_show[NPAGE] = {1, 1, 1, 1, 1, 1, 0, 0}, c3_cur;
static uint8_t ask_on, ask_sel;                 /* the start's question: CLASSIC or SHIFT */
static uint32_t ask_ms;
#define C3_ON (set.ui && COL3[page].title && c3_show[page])
static uint32_t c3_col(void) { return hb.b == BTN_OCT_DOWN ? 1u : hb.b == BTN_OCT_UP ? 2u : 0u; }
static uint32_t hold_time(uint32_t b);
#define PRE_BROWSING (page == PG_PRE && vcur[PG_PRE] < 2u)   /* (SAVE held there: a favorite) */
static const char *hold_label(uint32_t b);
static uint32_t ui_rng = 0x2545F491u;
static uint32_t ui_rnd(void) { ui_rng ^= ui_rng << 13; ui_rng ^= ui_rng >> 17; ui_rng ^= ui_rng << 5; return ui_rng; }

static void load_preset(uint32_t bank, uint32_t k)
{
    int8_t p[NP_MAX];
    uint32_t i;
    if (bank && !up_valid(k)) { toast("EMPTY SLOT"); return; }
    undo_push();                                /* (a load can be undone too) */
    undo_id = 0xFFFFu;
    if (bank) {
        user_get(k, p);
        for (i = 0; i < 13u; i++) pname[i] = ustore[k].name[i];
        pname[13] = 0;
    } else
        preset_factory(k, p, pname);
    memcpy(patch, p, NP_MAX);
    set.bank = (uint8_t)bank;
    set.idx = (uint8_t)k;
    set_dirty = 1;
    set_ms = fm1_ms;
    prev_dirty = 1;
    pdirty = 0;
    pend_on = 0;
}
static void save_user(uint32_t k)
{
    upatch_t *u = &ustore[k];
    uint32_t i;
    u->magic = UP_MAGIC;
    for (i = 0; i < 14u; i++) u->name[i] = i < 13u ? pname[i] : 0;
    memcpy(u->p, patch, NP_STORE);
    u->sum = up_sum(u);
    if (uext.magic != UX_MAGIC || uext.sum != ux_sum()) { memset(uext.p, 0, sizeof uext.p); uext.magic = UX_MAGIC; }
    memcpy(uext.p[k], patch + NP_STORE, NP_MAX - NP_STORE);
    uext.sum = ux_sum();
    if (store_save() || perf_save()) { toast("SAVE FAILED"); return; }
    pdirty = 0;
    set.bank = 1;
    set.idx = (uint8_t)k;
    set_dirty = 1;
    toast("SAVED");
}

/* ---- items: a parameter of the patch, or one of the globals */
static int item_patch(uint32_t id) { return id < NP; }
static uint32_t rate_id(uint32_t id) { uint32_t o = id == I_L2RATE ? LFO_2 : 0u; return patch[L1_SYNC + o] ? L1_DIV + o : L1_RATE + o; }
static void item_range(uint32_t id, int32_t *lo, int32_t *hi)
{
    if (id == I_L1RATE || id == I_L2RATE) id = rate_id(id);
    if (item_patch(id)) { *lo = PD[id].lo; *hi = PD[id].hi; return; }
    *lo = 0;
    *hi = id == G_MIDICH ? 16 : id == G_BPM ? 2400 : id == G_CLOCK || id == G_CORES || id == G_GRID || id == G_BROWSE || id == G_C3COL || id == G_UI || id == G_ASK || id == G_ZOOM || id == G_MLEARN || id == G_MSTART || id == G_MPROG || id == G_MCLK ? 1 : id == G_MMAP ? 16 : id == I_ABMIX ? 127 : id == G_ACCEL ? 2 : id == G_THEME || id == G_FONT ? 3 : id == G_PAL ? 7 : id == G_OCT ? 6 : 0;
    if (id == G_BPM) *lo = 600;
    if (id >= G_SQ_STEP && id <= G_AR_GATE) {
        static const int8_t LO[12] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1}, HI[12] = {15, 48, 4, 15, 16, 5, 50, 1, 4, 5, 3, 10};
        *lo = LO[id - G_SQ_STEP]; *hi = HI[id - G_SQ_STEP];
    }
    if (id >= G_EQ_F && id <= G_EQ_Q) {
        static const int8_t LO[4] = {0, -12, 0, 0}, HI[4] = {47, 12, NEQT - 1, 7};
        *lo = LO[id - G_EQ_F]; *hi = HI[id - G_EQ_F];
    }
    if (id >= G_LV_FLT && id <= G_MC_AMT) {
        static const int8_t LO[12] = {-64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -64}, HI[12] = {63, 127, 5, 127, 3, 127, 127, 127, 127, 127, ND - 1, 63};
        *lo = LO[id - G_LV_FLT]; *hi = HI[id - G_LV_FLT];
    }
}
static seqpat_t *sq_pat(void) { return &perf.pat[perf.cur % SQ_NPAT]; }
static uint32_t sq_type(uint32_t f) { return !(f & SF_ON) ? 0u : (f & SF_ACC) && (f & SF_SLIDE) ? 4u : (f & SF_SLIDE) ? 3u : (f & SF_ACC) ? 2u : 1u; }
static int32_t item_get(uint32_t id)
{
    if (id == I_L1RATE || id == I_L2RATE) id = rate_id(id);
    if (item_patch(id)) return patch[id];
    switch (id) {
    case G_MIDICH: return set.midich;
    case G_BPM: return set.bpm10;
    case G_CLOCK: return set.clock;
    case G_OCT: return OCT + 3;
    case G_CORES: return set.cores;
    case G_THEME: return set.theme;
    case G_PAL: return set.pal;
    case G_FONT: return set.font;
    case G_GRID: return set.grid;
    case G_BROWSE: return set.browse;
    case G_ACCEL: return set.accel;
    case G_C3COL: return set.c3col;
    case G_UI: return set.ui;
    case G_ASK: return set.ask;
    case G_ZOOM: return set.zoom;
    case G_MLEARN: return ml_state != 0;
    case G_MMAP: return (int32_t)cc_count();
    case G_MSTART: return set.mstart;
    case G_MPROG: return set.mprog;
    case G_MCLK: return set.mclk;
    case I_ABMIX: {                             /* A/B MIX: from the two levels (the louder one is the scale) */
        int32_t a = patch[A_LVL], b = patch[B_LVL];
        if (!a && !b) return 64;
        return a >= b ? b * 64 / a : 127 - a * 63 / b;
    }
    case G_SQ_STEP: return sq_cur_step;
    case G_SQ_NOTE: return sq_pat()->note[sq_cur_step & 15u];
    case G_SQ_TYPE: return (int32_t)sq_type(sq_pat()->fl[sq_cur_step & 15u]);
    case G_SQ_PAT: return perf.cur;
    case G_SQ_LEN: return sq_pat()->len;
    case G_SQ_RATE: return sq_pat()->rate;
    case G_SQ_SWING: return sq_pat()->swing;
    case G_AR_ON: return perf.ar_on;
    case G_AR_MODE: return perf.ar_mode;
    case G_AR_RATE: return perf.ar_rate;
    case G_AR_OCT: return perf.ar_oct;
    case G_AR_GATE: return perf.ar_gate;
    case G_LV_FLT: return lfx_flt;
    case G_LV_PUMP: return lfx_pump;
    case G_LV_STUT: return lfx_stut;
    case G_LV_TAPE: return lfx_tape;
    case G_LV_PRATE: return lfx_prate;
    case G_LV_FRES: return lfx_fres;
    case G_MC1: case G_MC2: case G_MC3: case G_MC4: return mc_val[id - G_MC1];
    case G_MC_DST: return perf.mc_dst[(mc_pos >> 1) & 15u];
    case G_MC_AMT: return perf.mc_amt[(mc_pos >> 1) & 15u];
    case G_EQ_F: return perf.eq_f[eq_cur & 3u];
    case G_EQ_G: return perf.eq_g[eq_cur & 3u];
    case G_EQ_T: return perf.eq_t[eq_cur & 3u];
    case G_EQ_Q: return perf.eq_q[eq_cur & 3u];
    default: return 0;
    }
}
static void item_set(uint32_t id, int32_t v)
{
    int32_t lo, hi;
    if (id == I_L1RATE || id == I_L2RATE) id = rate_id(id);
    item_range(id, &lo, &hi);
    v = clampi(v, lo, hi);
    if (item_patch(id)) {
        if (patch[id] != v) undo_mark(id);
        patch[id] = (int8_t)v;
        if (PD[id].fmt == FM_DEST && (v == D_DRAMT || v == D_DRMIX) && !patch[DR_ON]) { patch[DR_ON] = 1; toast("SATURATION ON"); }
        if (id < N_LVL || id == S_LVL || id == S_WAVE || id == S_OCT) prev_dirty = 1;
        return;
    }
    if (id >= G_EQ_F && id <= G_EQ_Q) {
        uint32_t k = eq_cur & 3u;
        if (id == G_EQ_F) perf.eq_f[k] = (uint8_t)v;
        else if (id == G_EQ_G) { perf.eq_g[k] = (int8_t)v; if (!perf.eq_t[k]) perf.eq_t[k] = EQ_BELL; }   /* (a gain on an OFF band: a bell) */
        else if (id == G_EQ_T) perf.eq_t[k] = (uint8_t)v;
        else perf.eq_q[k] = (uint8_t)v;
        eq_dirty = 1;
        perf_touch();
        return;
    }
    if (id >= G_LV_FLT && id <= G_MC_AMT) {
        switch (id) {
        case G_LV_FLT: lfx_flt = (int8_t)v; return;
        case G_LV_PUMP: lfx_pump = (uint8_t)v; return;
        case G_LV_STUT: lfx_stut = (uint8_t)v; return;
        case G_LV_TAPE: lfx_tape = (uint8_t)v; return;
        case G_LV_PRATE: lfx_prate = (uint8_t)v; return;
        case G_LV_FRES: lfx_fres = (uint8_t)v; return;
        case G_MC_DST: perf.mc_dst[(mc_pos >> 1) & 15u] = (uint8_t)v; perf_touch(); return;
        case G_MC_AMT: perf.mc_amt[(mc_pos >> 1) & 15u] = (int8_t)v; perf_touch(); return;
        default: mc_val[id - G_MC1] = (uint8_t)v; return;
        }
    }
    if (id >= G_SQ_STEP && id <= G_AR_GATE) {
        seqpat_t *q = sq_pat();
        switch (id) {
        case G_SQ_STEP: sq_cur_step = (uint8_t)v; return;
        case G_SQ_NOTE: q->note[sq_cur_step & 15u] = (uint8_t)v; if (!(q->fl[sq_cur_step & 15u] & SF_ON)) q->fl[sq_cur_step & 15u] = SF_ON; break;
        case G_SQ_TYPE: q->fl[sq_cur_step & 15u] = SQ_FL[v]; break;
        case G_SQ_PAT: perf.cur = (uint8_t)v; break;
        case G_SQ_LEN: q->len = (uint8_t)v; if (sq_cur_step >= v) sq_cur_step = (uint8_t)(v - 1); break;
        case G_SQ_RATE: q->rate = (uint8_t)v; break;
        case G_SQ_SWING: q->swing = (uint8_t)v; break;
        case G_AR_ON: perf.ar_on = (uint8_t)v; break;
        case G_AR_MODE: perf.ar_mode = (uint8_t)v; break;
        case G_AR_RATE: perf.ar_rate = (uint8_t)v; break;
        case G_AR_OCT: perf.ar_oct = (uint8_t)v; break;
        case G_AR_GATE: perf.ar_gate = (uint8_t)v; break;
        }
        perf_touch();
        return;
    }
    switch (id) {
    case G_MIDICH: set.midich = (uint8_t)v; break;
    case G_BPM: set.bpm10 = (uint16_t)v; ks_bpm10 = (uint32_t)v; break;
    case G_CLOCK: set.clock = (uint8_t)v; break;
    case G_OCT: set.oct = (uint8_t)(int8_t)(v - 3); break;
    case G_CORES: set.cores = (uint8_t)v; toast("RESTART TO APPLY"); break;
    case G_THEME: set.theme = (uint8_t)v; theme_set(set.theme, set.pal); g_force = 1; break;
    case G_PAL: set.pal = (uint8_t)v; theme_set(set.theme, set.pal); g_force = 1; break;
    case G_FONT: set.font = (uint8_t)v; font_set(set.font, set.zoom); g_force = 1; break;
    case G_GRID: set.grid = (uint8_t)v; ks_straight = set.grid; break;
    case G_BROWSE: set.browse = (uint8_t)v; break;
    case G_ACCEL: set.accel = (uint8_t)v; break;
    case G_C3COL: set.c3col = (uint8_t)v; break;
    case G_UI: set.ui = (uint8_t)v; c3_show[page] = (uint8_t)v; vcur[page] = 0; break;
    case G_ASK: set.ask = (uint8_t)v; break;
    case G_ZOOM: set.zoom = (uint8_t)v; font_set(set.font, set.zoom); g_force = 1; break;
    case G_MLEARN: ml_state = (uint8_t)v; toast(v ? "LEARN: TURN A KNOB" : "LEARN OFF"); return;
    case G_MMAP: if (!v) { memset(perf.cc_num, 0xFF, sizeof perf.cc_num); perf_touch(); toast("CC MAP CLEARED"); } return;
    case G_MSTART: set.mstart = (uint8_t)v; break;
    case G_MPROG: set.mprog = (uint8_t)v; break;
    case G_MCLK: set.mclk = (uint8_t)v; break;
    case I_ABMIX: {                             /* the louder of A and B stays, the other follows the balance */
        int32_t a = patch[A_LVL], b = patch[B_LVL], l = a > b ? a : b;
        if (!l) l = 100;
        item_set(A_LVL, v <= 64 ? l : l * (127 - v) / 63);
        item_set(B_LVL, v >= 64 ? l : l * v / 64);
        return;
    }
    default: return;
    }
    set_dirty = 1;
    set_ms = fm1_ms;
}
static int osc_pk(uint32_t id, uint32_t *eng)   /* 1..3: the oscillator's P1..P3 */
{
    uint32_t b = id >= B_ENG && id <= B_FINE ? OSC_B : 0u, k = id - b;
    if (id > B_FINE || k < A_P1 || k > A_P3) return 0;
    *eng = (uint32_t)patch[A_ENG + b];
    return (int)(k - A_P1 + 1u);
}
static const char *item_name(uint32_t id)
{
    static char b[20];
    uint32_t e;
    int k = osc_pk(id, &e);
    if (k) { char *p = str_cat(b, id >= B_ENG ? "B " : "A "); str_cat(p, ENG_P[e][k - 1]); return b; }
    if (id == I_L1RATE || id == I_L2RATE) return PD[rate_id(id)].name;
    if (item_patch(id)) return PD[id].name;
    switch (id) {
    case G_MIDICH: return "MIDI CHANNEL";
    case G_BPM: return "TEMPO";
    case G_CLOCK: return "CLOCK";
    case G_OCT: return "KEYS OCTAVE";
    case G_CORES: return "AUDIO CORES";
    case G_CPU: return "CPU LOAD";
    case G_THEME: return "THEME";
    case G_PAL: return "PALETTE";
    case G_FONT: return "FONT";
    case G_GRID: return "LFO GRID";
    case G_BROWSE: return "PRESETS";
    case G_ACCEL: return "KNOB ACCEL";
    case G_C3COL: return "SHIFT COLOURS";
    case G_UI: return "UI MODE";
    case G_ASK: return "ASK AT START";
    case G_ZOOM: return "LOUPE";
    case G_MLEARN: return "MIDI LEARN";
    case G_MMAP: return "CC MAP";
    case G_MSTART: return "START / STOP";
    case G_MPROG: return "PROGRAM CHANGE";
    case G_MCLK: return "CLOCK OUT";
    case I_ABMIX: return "A / B MIX";
    case G_FW: return "FIRMWARE";
    case G_SQ_STEP: return "STEP";
    case G_SQ_NOTE: return "NOTE";
    case G_SQ_TYPE: return "STEP TYPE";
    case G_SQ_PAT: return "PATTERN";
    case G_SQ_LEN: return "LENGTH";
    case G_SQ_RATE: return "RATE";
    case G_SQ_SWING: return "SWING";
    case G_AR_ON: return "ARP";
    case G_AR_MODE: return "MODE";
    case G_AR_RATE: return "RATE";
    case G_AR_OCT: return "OCTAVES";
    case G_AR_GATE: return "GATE";
    case G_LV_FLT: return "DJ FILTER";
    case G_LV_PUMP: return "PUMP";
    case G_LV_STUT: return "STUTTER";
    case G_LV_TAPE: return "TAPE STOP";
    case G_LV_PRATE: return "PUMP RATE";
    case G_LV_FRES: return "FILTER RESO";
    case G_MC1: return "MACRO 1";
    case G_MC2: return "MACRO 2";
    case G_MC3: return "MACRO 3";
    case G_MC4: return "MACRO 4";
    case G_MC_DST: return "TARGET";
    case G_MC_AMT: return "AMOUNT";
    case G_EQ_F: return "FREQUENCY";
    case G_EQ_G: return "GAIN";
    case G_EQ_T: return "BAND TYPE";
    case G_EQ_Q: return "WIDTH (Q)";
    default: return "";
    }
}
static const char *item_sname(uint32_t id)
{
    uint32_t e;
    int k = osc_pk(id, &e);
    if (k) return ENG_P[e][k - 1];
    if (id == I_L1RATE || id == I_L2RATE) return "RATE";
    if (item_patch(id)) return PD[id].sname;
    return id == G_MIDICH ? "MIDI" : id == G_BPM ? "BPM" : id == G_CLOCK ? "CLOCK" : id == G_OCT ? "OCT" : id == G_CORES ? "CORES" : id == G_THEME ? "THEME" : id == G_PAL ? "PALETTE" : id == G_FONT ? "FONT" : id == G_GRID ? "GRID" : id == G_BROWSE ? "BROWSE" : id == G_ACCEL ? "ACCEL" : id == G_C3COL ? "COLOURS" : id == G_UI ? "UI" : id == G_ASK ? "ASK" : id == G_ZOOM ? "LOUPE" : id == G_MLEARN ? "LEARN" : id == G_MMAP ? "CC MAP" : id == I_ABMIX ? "A/B" : id == G_SQ_TYPE ? "TYPE" : id == G_SQ_PAT ? "PAT" : id == G_SQ_LEN ? "LEN" : id == G_AR_OCT ? "OCT" : item_name(id);
}
static uint32_t ilog2q8(uint32_t x)             /* log2(x) in Q8, x >= 1 */
{
    uint32_t m = 31;
    while (!(x >> m)) m--;
    return (m << 8) | (m >= 8u ? (x >> (m - 8u)) & 255u : (x << (8u - m)) & 255u);
}
/* a destination's name: the oscillators' with their engine's words (A WAVE, B DETUNE...) */
static const char *dst_name(uint32_t d)
{
    static char nb[2][20];
    static uint32_t t;
    uint32_t o = d == D_BP1 || d == D_BP2 || d == D_BP3 ? OSC_B : 0u;
    int32_t k = d == D_AP1 || d == D_BP1 ? 0 : d == D_AP2 || d == D_BP2 ? 1 : d == D_AP3 || d == D_BP3 ? 2 : -1;
    if (k >= 0) {
        char *p = nb[t ^= 1u];
        str_cat(str_cat(p, o ? "B " : "A "), ENG_P[patch[A_ENG + o] & 7][k]);
        return p;
    }
    return d < ND ? L_DST[d] : "";
}
static int dst_on(uint32_t d)                   /* an oscillator that is off is not offered */
{
    if (d == D_ALVL || d == D_AP1 || d == D_AP2 || d == D_AP3 || d == D_APIT) return patch[A_LVL] || patch[X_MODE] == 1;
    if (d == D_BLVL || d == D_BP1 || d == D_BP2 || d == D_BP3 || d == D_BPIT) return patch[B_LVL] || patch[X_MODE];
    if (d == D_GATE) return patch[ST_ON];       /* an effect that is off is not offered either */
    if (d >= D_CRBITS && d <= D_CRMIX) return patch[CR_ON];
    if (d >= D_CHRATE && d <= D_CHMIX) return patch[CH_ON];
    if (d >= D_DLFB && d <= D_DLMIX) return patch[DL_ON];
    if (d >= D_RVSIZE && d <= D_RVMIX) return patch[RV_ON];
    if (d == D_CPAMT) return patch[CP_ON];
    if (d >= D_PHRATE && d <= D_PHMIX) return patch[PH_ON];
    return 1;
}
static int32_t dst_step(int32_t v, int32_t d)
{
    int32_t i, at = 0;
    for (i = 0; i < ND; i++) if (DST_ORDER[i] == v) at = i;
    while (d) {
        int32_t s = d > 0 ? 1 : -1;
        for (i = at + s; i >= 0 && i < ND && !dst_on(DST_ORDER[i]); i += s) {}
        if (i < 0 || i >= ND) break;
        at = i;
        d -= s;
    }
    return DST_ORDER[at];
}

static void item_text(uint32_t id, char *b)
{
    int32_t v = item_get(id);
    uint32_t e, rid = id == I_L1RATE || id == I_L2RATE ? rate_id(id) : id;
    int k = osc_pk(id, &e);
    *b = 0;
    if (k) {
        int32_t q = v * 258;
        if (e == E_ANALOG && k == 1) {
            static const char *const W[4] = {"SINE", "TRI", "SAW", "SQUARE"};
            if ((v * 3) % 127 != 0) *b++ = '~';
            str_cat(b, W[(v * 3 + 63) / 127]);
        } else if (e == E_SUPER && k == 3) fmt_u(b, SUPER_N[q >> 13], 0);
        else if (e == E_WAVE && k == 2) str_cat(b, L_WT[q >> 12]);
        else if (e == E_FM && k == 2) str_cat(b, L_RATIO[q >> 11]);
        else if (e == E_PHASE && k == 2) str_cat(b, L_PDT[(q * 6) >> 15]);
        else if (e == E_VOCAL && k == 1) { b[0] = L_VOX[(q * 4 + 16384) >> 15]; b[1] = 0; }
        else if (e == E_SYNC && k == 1) { uint32_t r = 10u + (uint32_t)v * 70u / 127u; b = str_cat(b, "x"); b = fmt_u(b, r / 10u, 0); *b++ = '.'; fmt_u(b, r % 10u, 0); }
        else { b = fmt_u(b, (uint32_t)v * 100u / 127u, 0); str_cat(b, "%"); }
        return;
    }
    if (!item_patch(rid)) {
        switch (id) {
        case G_MIDICH: if (v) fmt_u(b, (uint32_t)v, 2); else str_cat(b, "OMNI"); break;
        case G_BPM: b = fmt_u(b, (uint32_t)v / 10u, 0); *b++ = '.'; b = fmt_u(b, (uint32_t)v % 10u, 0); str_cat(b, " BPM"); break;
        case G_CLOCK: str_cat(b, v ? "MIDI" : "INTERNAL"); break;
        case G_OCT: fmt_s(b, v - 3); break;
        case G_CORES: str_cat(b, !v ? "SINGLE" : dual_on ? "DUAL \xB7 ON" : KS_DUAL ? "DUAL \xB7 OFF" : "DUAL (FM-1)"); break;
        case G_THEME: { static const char *const TN[4] = {"CLASSIC", "STUDIO", "PURE", "RACK"}; str_cat(b, TN[v & 3]); break; }
        case G_PAL: { static const char *const PN[6] = {"SYNTHWAVE", "MINT", "BLACK / WHITE", "INVERTED", "RED", "ICE"}; str_cat(b, v < 2 ? PAL_NAME[set.theme & 3][v] : PN[(v - 2) % 6]); break; }
        case G_GRID: str_cat(b, v ? "STRAIGHT" : "ALL (T, D)"); break;
        case G_BROWSE: str_cat(b, v ? "FAVORITES" : "ALL"); break;
        case G_ACCEL: str_cat(b, v == 2 ? "FAST" : v ? "ON" : "OFF"); break;
        case G_C3COL: str_cat(b, v ? "GREEN YEL ORANGE" : "THEME"); break;
        case G_UI: str_cat(b, v ? "SHIFT (3 COLUMNS)" : "CLASSIC"); break;
        case G_ASK: str_cat(b, v ? "YES" : "NO"); break;
        case G_ZOOM: str_cat(b, v ? "ON (BIG TYPE)" : "OFF"); break;
        case G_MLEARN: str_cat(b, ml_state == 1u ? "TURN A KNOB" : ml_state == 2u ? "SEND A CC" : "OFF"); break;
        case G_MMAP: if (v) { b = fmt_u(b, (uint32_t)v, 0); str_cat(b, v > 1 ? " CCS \xB7 LEFT: CLEAR" : " CC \xB7 LEFT: CLEAR"); } else str_cat(b, "EMPTY"); break;
        case G_MSTART: str_cat(b, v ? "PLAYS THE SEQ" : "IGNORED"); break;
        case G_MPROG: str_cat(b, v ? "LOADS A PRESET" : "IGNORED"); break;
        case G_MCLK: str_cat(b, v ? "SENT (INTERNAL)" : "OFF"); break;
        case I_ABMIX:
            if (v == 64) { str_cat(b, "A = B"); break; }
            b = str_cat(b, "A "); b = fmt_u(b, (uint32_t)(v <= 64 ? 100 : (127 - v) * 100 / 63), 0);
            b = str_cat(b, "% B "); b = fmt_u(b, (uint32_t)(v >= 64 ? 100 : v * 100 / 64), 0); str_cat(b, "%"); break;
        case G_FONT: { static const char *const FN[4] = {"SANS", "MONO", "HYPER", "PIXEL"}; str_cat(b, FN[v & 3]); break; }
        case G_CPU:                             /* each core's share of a block, and the governor's step */
            b = str_cat(b, "C0 "); b = fmt_u(b, ks_load[0], 0); b = str_cat(b, "%");
            if (dual_on) { b = str_cat(b, " C1 "); b = fmt_u(b, ks_load[1], 0); b = str_cat(b, "%"); }
            if (ks_shed) { b = str_cat(b, " \xB7 -"); fmt_u(b, ks_shed, 0); }
            break;
        case G_FW: str_cat(b, "K-WA SUB " APP_VER); break;
        case G_SQ_STEP: b = fmt_u(b, (uint32_t)v + 1u, 2); b = str_cat(b, "/"); fmt_u(b, sq_pat()->len, 2); break;
        case G_SQ_NOTE:
            if (!(sq_pat()->fl[sq_cur_step & 15u] & SF_ON)) { str_cat(b, "REST"); break; }
            b = str_cat(b, NOTE_N[v % 12]); fmt_u(b, 1u + (uint32_t)v / 12u, 0); break;
        case G_SQ_TYPE: str_cat(b, L_SQTYPE[v % 5]); break;
        case G_SQ_PAT: b = str_cat(b, "P"); b = fmt_u(b, (uint32_t)v + 1u, 2); if (v < 8) { b = str_cat(b, " "); str_cat(b, SQ_DEF[v][0]); } break;
        case G_SQ_LEN: b = fmt_u(b, (uint32_t)v, 0); str_cat(b, " STEPS"); break;
        case G_SQ_RATE: case G_AR_RATE: str_cat(b, L_SRATE[v % 6]); break;
        case G_SQ_SWING: if (v) { b = fmt_u(b, 50u + (uint32_t)v / 2u, 0); str_cat(b, "%"); } else str_cat(b, "OFF"); break;
        case G_AR_ON: str_cat(b, v ? "ON" : "OFF"); break;
        case G_AR_MODE: str_cat(b, L_ARMODE[v % 5]); break;
        case G_AR_OCT: fmt_u(b, (uint32_t)v + 1u, 0); break;
        case G_AR_GATE: b = fmt_u(b, (uint32_t)v * 10u, 0); str_cat(b, "%"); break;
        case G_LV_FLT:
            if (!v) { str_cat(b, "OFF"); break; }
            b = str_cat(b, v < 0 ? "LOW PASS " : "HIGH PASS ");
            b = fmt_u(b, (uint32_t)(v < 0 ? -v : v) * 100u / 64u, 0); str_cat(b, "%"); break;
        case G_LV_STUT: { static const char *const ST[6] = {"OFF", "1/4", "1/8", "1/16", "1/32", "1/64"}; str_cat(b, ST[v % 6]); break; }
        case G_LV_TAPE: if (!v) str_cat(b, "OFF"); else if (v >= 127) str_cat(b, "STOP"); else { b = str_cat(b, "SLOW "); b = fmt_u(b, (uint32_t)v * 100u / 127u, 0); str_cat(b, "%"); } break;
        case G_LV_PRATE: { static const char *const PR[4] = {"1/4", "1/8", "1/2", "1 BAR"}; str_cat(b, PR[v & 3]); break; }
        case G_LV_PUMP: case G_LV_FRES: case G_MC1: case G_MC2: case G_MC3: case G_MC4:
            if (id == G_LV_PUMP && !v) { str_cat(b, "OFF"); break; }
            b = fmt_u(b, (uint32_t)v * 100u / 127u, 0); str_cat(b, "%"); break;
        case G_MC_DST: str_cat(b, v ? dst_name((uint32_t)v) : "-"); break;
        case G_MC_AMT: b = v ? fmt_s(b, v * 100 / 64) : fmt_u(b, 0, 0); str_cat(b, "%"); break;
        case G_EQ_F: { uint32_t h = KS_EQ_HZ[v % 48]; if (h < 1000u) { b = fmt_u(b, h, 0); str_cat(b, " Hz"); } else { b = fmt_u(b, h / 1000u, 0); *b++ = '.'; b = fmt_u(b, h / 100u % 10u, 0); str_cat(b, " kHz"); } break; }
        case G_EQ_G: b = v ? fmt_s(b, v) : fmt_u(b, 0, 0); str_cat(b, " dB"); break;
        case G_EQ_T: str_cat(b, L_EQT[v % NEQT]); break;
        case G_EQ_Q: { uint32_t q = KS_EQ_Q10[v & 7]; b = str_cat(b, "Q "); b = fmt_u(b, q / 10u, 0); *b++ = '.'; fmt_u(b, q % 10u, 0); break; }
        }
        return;
    }
    if (rid == SM_SPEED) { b = fmt_u(b, (256u - (uint32_t)v * 230u / 127u) * 100u / 256u, 0); str_cat(b, v ? "% (SLOWER)" : "%"); return; }
    switch (PD[rid].fmt) {
    case FM_PCT: b = fmt_u(b, (uint32_t)v * 100u / 127u, 0); str_cat(b, "%"); break;
    case FM_BIP: b = v ? fmt_s(b, v * 100 / 64) : fmt_u(b, 0, 0); str_cat(b, "%"); break;
    case FM_LIST: case FM_DIV: str_cat(b, PD[rid].list[(rid == L1_DIV || rid == L2_DIV) && set.grid ? DIV_NEAR[v & 15] : v]); break;
    case FM_DEST: str_cat(b, dst_name((uint32_t)v)); break;
    case FM_INT: if (v) fmt_s(b, v); else fmt_u(b, 0, 0); break;
    case FM_SEMI: b = v && PD[rid].lo < 0 ? fmt_s(b, v) : fmt_u(b, (uint32_t)(v < 0 ? -v : v), 0); str_cat(b, " st"); break;
    case FM_CENT: b = v ? fmt_s(b, v) : fmt_u(b, 0, 0); str_cat(b, " ct"); break;
    case FM_TIME: {
        uint32_t t = rid == V_GLIDE ? KS_GLIDE_MS10[v] : KS_ENV_MS10[v];
        if (rid == V_GLIDE && !v) { str_cat(b, "OFF"); break; }
        if (t < 100u) { b = fmt_u(b, t / 10u, 0); *b++ = '.'; b = fmt_u(b, t % 10u, 0); str_cat(b, " ms"); }
        else if (t < 10000u) { b = fmt_u(b, t / 10u, 0); str_cat(b, " ms"); }
        else { b = fmt_u(b, t / 10000u, 0); *b++ = '.'; b = fmt_u(b, t / 1000u % 10u, 0); str_cat(b, " s"); }
        break;
    }
    case FM_CUT: {
        uint32_t hz = KS_CUT_HZ[v * 2];
        if (hz < 1000u) { b = fmt_u(b, hz, 0); str_cat(b, " Hz"); }
        else { b = fmt_u(b, hz / 1000u, 0); *b++ = '.'; b = fmt_u(b, hz / 100u % 10u, 0); str_cat(b, " kHz"); }
        break;
    }
    case FM_LFOHZ: {
        uint32_t h = KS_LFO_HZ100[v];
        b = fmt_u(b, h / 100u, 0); *b++ = '.'; b = fmt_u(b, h % 100u, 2); str_cat(b, " Hz");
        break;
    }
    case FM_VOL: {
        int32_t db;
        if (!v) { str_cat(b, "-INF"); break; }
        db = (int32_t)(ilog2q8((uint32_t)(v * v)) - ilog2q8(10000u)) * 771 / 65536;   /* 10 log10(v^2/100^2) */
        b = db ? fmt_s(b, db) : fmt_u(b, 0, 0);
        str_cat(b, " dB");
        break;
    }
    }
}
static uint32_t item_frac(uint32_t id)          /* 0..256 for the bars */
{
    int32_t lo, hi, v = item_get(id);
    item_range(id, &lo, &hi);
    return hi > lo ? (uint32_t)((v - lo) * 256 / (hi - lo)) : 0u;
}

/* ---- the picture of the sound (PLAY page, when nothing plays): two cycles of A + B + sub */
static void preview_make(void)
{
    osc_t oa, ob;
    oscp_t qa = {0}, qb = {0};
    int32_t a[16], bb[16], pk = 1, i, j, la = lvl15(patch[A_LVL]), lb = lvl15(patch[B_LVL]), ls = lvl15(patch[S_LVL]);
    int32_t w[224];
    uint32_t ps = 0;
    memset(&oa, 0, sizeof oa);
    memset(&ob, 0, sizeof ob);
    qa.eng = (uint8_t)patch[A_ENG]; qa.p1 = patch[A_P1] * 258; qa.p2 = patch[A_P2] * 258; qa.p3 = patch[A_P3] * 258;
    qb.eng = (uint8_t)patch[B_ENG]; qb.p1 = patch[B_P1] * 258; qb.p2 = patch[B_P2] * 258; qb.p3 = patch[B_P3] * 258;
    qa.inc = 0xFFFFFFFFu / 112u;
    qb.inc = qa.inc;
    for (i = 0; i < 224; i += 16) {
        osc_render(&oa, &qa, a, 0, 16);
        osc_render(&ob, &qb, bb, 0, 16);
        for (j = 0; j < 16; j++) {
            int32_t x = mul15(a[j], la) + mul15(patch[X_MODE] == 1 ? mul15(a[j], bb[j]) : bb[j], lb);
            ps += qa.inc >> (2 - patch[S_OCT]);
            x += mul15(sin32(ps), ls);
            w[i + j] = x;
            if ((x < 0 ? -x : x) > pk) pk = x < 0 ? -x : x;
            if (i + j < 112) { prev_a[i + j] = (int16_t)clip(a[j], 32767); prev_b[i + j] = (int16_t)clip(bb[j], 32767); }
        }
    }
    for (i = 0; i < 112; i++) {                 /* the sub, one cycle */
        uint32_t ph = (uint32_t)i * (0xFFFFFFFFu / 112u);
        prev_s[i] = (int16_t)(patch[S_WAVE] == 0 ? sin32(ph) : patch[S_WAVE] == 1 ? tri32(ph + 0x40000000u)
                              : (ph < (patch[S_WAVE] == 2 ? 0x80000000u : 0x40000000u) ? -30000 : 30000));
    }
    pk = (32000 << 12) / pk;                     /* (a gain, Q12: no 64-bit division) */
    for (i = 0; i < 224; i++) prev_wave[i] = (int16_t)((w[i] * pk) >> 12);
}

#include "ksub_draw.c"

/* ================================================================ the panel */
static void led(uint32_t id, uint32_t lvl) { if (id < 41u) led_want[id] = (uint8_t)lvl; }
#if defined(DINGHY_HOST) || defined(KAWA_WASM)
static void leds_flush(void) { host_leds(led_want); }
#else
static void leds_flush(void)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++) {
        uint8_t on = 0, dim = 0;
        for (r = 1; r < 5u; r++) {
            int8_t id = FM1_KEYMAP[r][p];
            if (id < 0 || id > 40) continue;
            if (led_want[id] == 2u) on |= (uint8_t)(1u << r);
            else if (led_want[id] == 1u) dim |= (uint8_t)(1u << r);
        }
        fm1_led[p] = on;
        fm1_led_dim[p] = dim;
    }
}
#endif
static void ui_leds(void)
{
    uint32_t i, beat = ((ks_beats >> 24) & 1u) == 0u && ((ks_beats >> 21) & 7u) < 2u;
    for (i = 0; i < 41u; i++) led_want[i] = 0;
    for (i = 0; i < NPAGE; i++) led(PAGE_BTN[i], i == page ? 2u : 1u);
    led(BTN_PLAY, sq_play ? (beat ? 2u : 1u) : hold ? 1u : 0u);
    led(BTN_REC, sq_rec ? 2u : beat ? 1u : 0u);
    led(BTN_SAVE, VIEWS[vidx()].lay == LY_SAVE ? 2u : 0u);
    for (i = 0; i < 27u; i++) if (fm1_in.notes & (1u << i)) led(KEY_LED(i), 2u);
    leds_flush();
}

static void notes_all_off(void)
{
    uint32_t n;
    for (n = 0; n < 128u; n++) if (latched[n] || phys[n]) { ev_push(EV_OFF, n, 0); latched[n] = phys[n] = 0; }
}
static void key_down(uint32_t n, uint32_t vel)
{
    uint32_t i, any = 0;
    if (sq_rec) {                               /* REC: the key goes to the step, the cursor on (heard when stopped) */
        seqpat_t *q = sq_pat();
        uint32_t c = sq_cur_step & 15u;
        q->note[c] = (uint8_t)clampi((int32_t)n - 48 - 12 * OCT, 0, 48);   /* (the keys' C: C2) */
        if (!(q->fl[c] & SF_ON)) q->fl[c] = SF_ON;
        sq_cur_step = (uint8_t)((c + 1u) % (q->len ? q->len : 16u));
        perf_touch();
        if (sq_play) return;
    }
    if (hold) {                                 /* a new phrase on a latched one: the old notes go */
        for (i = 0; i < 128u; i++) any |= phys[i];
        if (!any) for (i = 0; i < 128u; i++) if (latched[i]) { latched[i] = 0; ev_push(EV_OFF, i, 0); }
    }
    phys[n] = 1;
    ev_push(EV_ON, n, vel);
}
static void key_up(uint32_t n)
{
    if (!phys[n]) return;
    phys[n] = 0;
    if (hold) latched[n] = 1;
    else ev_push(EV_OFF, n, 0);
}

static uint32_t rate_like(uint32_t id) { return id == I_L1RATE || id == I_L2RATE ? rate_id(id) : id; }
static uint32_t turn_id = I_NONE;
static uint32_t fx_on_of(uint32_t id)           /* the ON of the effect this setting belongs to (0: none) */
{
    if (id > DR_ON && id <= DR_MIX) return DR_ON;
    if (id > CR_ON && id <= CR_MIX) return CR_ON;
    if (id > CH_ON && id <= CH_MIX) return CH_ON;
    if ((id > DL_ON && id <= DL_MIX) || id == DL_TYPE) return DL_ON;
    if ((id > RV_ON && id <= RV_MIX) || id == RV_TYPE) return RV_ON;
    if (id > ST_ON && id <= ST_SMO) return ST_ON;
    if (id > CP_ON && id <= CP_PRATE) return CP_ON;
    if (id > PH_ON && id <= PH_MIX) return PH_ON;
    return 0;
}
static void edit_item(uint32_t id, int32_t d)
{
    int32_t lo, hi, v = item_get(id), step = 1;
    uint32_t k;
    if (id == I_NONE || id == G_CPU || id == G_FW) return;
    if (ml_state == 1u && id != G_MLEARN && id != G_MMAP) { ml_id = (uint8_t)id; ml_state = 2; toast("LEARN: SEND A CC"); }
    k = fx_on_of(id);                           /* an effect (or STEP) that is off comes on when its setting goes up */
    if (k && !patch[k] && d > 0) {
        char t[24];
        uint32_t f;
        patch[k] = 1;
        for (f = 0; f < 7u && FX_ON[f] != k; f++) {}
        str_cat(str_cat(t, f < 7u ? FX_TITLE[f] : "STEP FX"), " ON");
        toast(t);
    }
    if (item_patch(id) && PD[id].fmt == FM_DEST) { item_set(id, dst_step(v, d)); dpop_id = (uint8_t)id; dpop_ms = fm1_ms | 1u; return; }
    if (item_patch(id) && PD[id].list == L_SRC) { item_set(id, v + d); dpop_id = (uint8_t)id; dpop_ms = fm1_ms | 1u; return; }   /* (a source: the list too) */
    if (set.grid && (rate_like(id) == L1_DIV || rate_like(id) == L2_DIV)) {   /* STRAIGHT: from round to round */
        int32_t i, at = 0;
        if (vpop_id != (uint8_t)id || !vpop_ms) { item_text(id, vpop_before); vpop_id = (uint8_t)id; }
        vpop_ms = fm1_ms | 1u;
        for (i = 0; i < 9; i++) if (DIV_STRAIGHT[i] == DIV_NEAR[v & 15]) at = i;
        item_set(id, DIV_STRAIGHT[clampi(at + d, 0, 8)]);
        return;
    }
    if (id != G_CPU && (vpop_id != (uint8_t)id || !vpop_ms)) { item_text(id, vpop_before); vpop_id = (uint8_t)id; }
    vpop_ms = fm1_ms | 1u;
    item_range(id, &lo, &hi);
    if (set.accel && hi - lo > 24 && id == turn_id) {   /* ACCEL: the faster the turn, the bigger the step */
        uint32_t dt = fm1_ms - last_turn_ms;
        step = dt < 20u ? 6 : dt < 40u ? 4 : dt < 70u ? 2 : 1;
        if (set.accel == 2u) step *= 2;
        if (hi - lo <= 64 && step > 3) step = 3;   /* (short ranges: never past a few) */
        if (hi - lo > 300) step *= (hi - lo) / 128;   /* (the tempo: tenths of a BPM) */
    }
    turn_id = id;
    last_turn_ms = fm1_ms;
    item_set(id, v + d * step);
}
static void ask_load(uint32_t bank, uint32_t k)   /* load, or (the sound changed) ask first: SEL loads */
{
    if (bank && !up_valid(k)) { toast("EMPTY SLOT"); return; }
    pre_cur[bank] = (uint8_t)k;
    if (pdirty) { pend_on = 1; pend_bank = (uint8_t)bank; pend_k = (uint8_t)k; pend_ms = fm1_ms; return; }
    load_preset(bank, k);
    toast(pname);
}
static void preset_step(int32_t d)              /* bank A then the user's saved patches of bank B, round */
{
    uint32_t n = NFACTORY + NUSER, at = pend_on ? (pend_bank ? NFACTORY + pend_k : pend_k) : (set.bank ? NFACTORY + set.idx : set.idx);
    uint32_t st = d < 0 ? n - 1u : 1u, cnt = (uint32_t)(d < 0 ? -d : d);
    int fo = set.browse && fav_any();          /* (FAVORITES: only those) */
    while (cnt--)
        do at = (at + st) % n; while ((at >= NFACTORY && !up_valid(at - NFACTORY)) || (fo && !fav_is(at >= NFACTORY, at >= NFACTORY ? at - NFACTORY : at)));
    ask_load(at >= NFACTORY, at >= NFACTORY ? at - NFACTORY : at);
}
static void save_open(void)
{
    uint32_t i;
    if (set.bank) save_slot = set.idx;
    else for (save_slot = 0; save_slot < NUSER - 1u && up_valid(save_slot); save_slot++) {}
    for (i = 0; i < 13u && pname[i]; i++) {}
    for (; i < 13u; i++) pname[i] = ' ';        /* the name as 13 letters to edit */
    pname[13] = 0;
    save_pos = 0;
    page = PG_PRE;
    vcur[PG_PRE] = 2;
}
static void name_trim(void) { int32_t i; for (i = 12; i >= 0 && pname[i] == ' '; i--) pname[i] = 0; }

static void view_next(void)                     /* the page's next view (SHIFT: the 3 columns first) */
{
    uint32_t nv = (uint32_t)(PAGE_V0[page + 1u] - PAGE_V0[page]);
    if (set.ui && COL3[page].title) {
        if (c3_show[page]) { c3_show[page] = 0; vcur[page] = 0; }
        else if (vcur[page] + 1u >= nv) { c3_show[page] = 1; vcur[page] = 0; }
        else vcur[page]++;
        return;
    }
    vcur[page] = (uint8_t)((vcur[page] + 1u) % nv);
}
static void on_button(uint32_t b)
{
    uint32_t i;
    dpop_ms = 0;                                /* (a button closes the destination list) */
    if (pend_on) {                              /* the question: SEL loads, any other button keeps the sound */
        pend_on = 0;
        if (b == BTN_SEL) { load_preset(pend_bank, pend_k); toast(pname); }
        else toast("KEPT");
        return;
    }
    for (i = 0; i < NPAGE; i++)
        if (PAGE_BTN[i] == b) {
            if (win) { win = 0; vpop_ms = 0; page = (uint8_t)i; name_trim(); return; }   /* (a window open: back to that page) */
            if (page == i) view_next();
            else page = (uint8_t)i;
            if (VIEWS[vidx()].lay == LY_SAVE) vcur[i]++;   /* (SAVE opens from SAVE only: on to the ARP) */
            name_trim();
            return;
        }
    switch (b) {
    case BTN_SEL:                               /* the windows: FX LIVE, MACROS, back (held: COMPARE) */
        win = win == 3u ? 0u : (uint8_t)((win + 1u) % 3u);   /* (from the EQ: back to the page) */
        win_cur = 0;
        vpop_ms = 0;
        g_force = 1;
        break;
    case BTN_SAVE:
        if (VIEWS[vidx()].lay == LY_SAVE) { name_trim(); save_user(save_slot); vcur[PG_PRE] = 1; pre_cur[1] = save_slot; }
        else save_open();
        break;
    case BTN_PLAY:                              /* SEQ BASS: play / stop (held: HOLD, ui_input) */
        seq_play(!sq_play);
        toast(sq_play ? "SEQ STOP" : "SEQ PLAY");
        break;
    case BTN_REC: {                             /* SEQ view: step record; elsewhere TAP: the tempo from the last taps */
        if (vidx() == V_SEQ) {
            sq_rec ^= 1u;
            toast(sq_rec ? "REC: KEYS WRITE STEPS" : "REC OFF");
            break;
        }
        uint32_t now = fm1_ms, d = now - tap_ms[0];
        if (d > 250u && d < 2000u) {
            uint32_t avg = tap_ms[1] && tap_ms[0] - tap_ms[1] < 2000u ? (d + (tap_ms[0] - tap_ms[1])) / 2u : d;
            item_set(G_BPM, (int32_t)(600000u / avg));
        }
        tap_ms[1] = tap_ms[0];
        tap_ms[0] = now;
        ev_push(EV_SYNC, 0, 0);
        toast("TAP");
        break;
    }
    case BTN_OCT_DOWN:
    case BTN_OCT_UP:
        if ((fm1_in.buttons & 3u) == 3u) { notes_all_off(); ev_push(EV_PANIC, 0, 0); toast("ALL NOTES OFF"); break; }
        item_set(G_OCT, OCT + 3 + (b == BTN_OCT_UP ? 1 : -1));
        {
            char t[12], *p = str_cat(t, "OCTAVE ");
            if (OCT) fmt_s(p, OCT); else str_cat(p, "0");
            toast(t);
        }
        break;
    }
}

static void c3_edit(uint32_t id, int32_t d)
{
    vpop_pre[0] = 0;
    edit_item(id, d);
}
static void on_enc(uint32_t e, int32_t d)
{
    const view_t *v = view();
    uint32_t vi = vidx();
    if (win == 3u && e != ENC_PRESETS) {        /* the EQ: K1..K4 frequencies (OCT- held: gains, OCT+ held: types) */
        vpop_pre[0] = 0;
        if (e == ENC_SELECT) eq_cur = (uint8_t)clampi(eq_cur + d, 0, 3);
        else if (e == ENC_ALGORITHM) edit_item(G_EQ_Q, d);
        else if (e >= ENC_KNOB1 && e <= ENC_KNOB4) {
            uint32_t col = c3_col();
            eq_cur = (uint8_t)(e - ENC_KNOB1);
            edit_item(col == 1u ? G_EQ_G : col == 2u ? G_EQ_T : G_EQ_F, d);
        }
        return;
    }
    if (win && e != ENC_PRESETS) {              /* the SEL windows: K1..K4 their four, SELECT a row, ALGO its value */
        vpop_pre[0] = 0;
        if (win == 1u) {
            if (e == ENC_SELECT) win_cur = (uint8_t)clampi(win_cur + d, 0, 5);
            else if (e == ENC_ALGORITHM) edit_item(LV_IT[win_cur], d);
            else if (e >= ENC_KNOB1 && e <= ENC_KNOB4) { win_cur = (uint8_t)(e - ENC_KNOB1); edit_item(LV_IT[win_cur], d); }
            return;
        }
        if (e == ENC_SELECT) mc_pos = (uint8_t)clampi(mc_pos + d, 0, 31);
        else if (e == ENC_ALGORITHM) {
            if (mc_pos & 1u) edit_item(G_MC_AMT, d);
            else {                              /* (a target: every destination, the oscillators' with their engine's words) */
                int32_t i, at = 0, cur = perf.mc_dst[(mc_pos >> 1) & 15u];
                for (i = 0; i < ND; i++) if (DST_ORDER[i] == cur) at = i;
                item_set(G_MC_DST, DST_ORDER[clampi(at + d, 0, ND - 1)]);
                dpop_id = G_MC_DST; dpop_ms = fm1_ms | 1u;
                if (!perf.mc_amt[(mc_pos >> 1) & 15u] && perf.mc_dst[(mc_pos >> 1) & 15u]) item_set(G_MC_AMT, 32);
            }
        } else if (e >= ENC_KNOB1 && e <= ENC_KNOB4) edit_item(G_MC1 + (e - ENC_KNOB1), d);
        return;
    }
    if (C3_ON && e != ENC_PRESETS) {
        const col3_t *c = &COL3[page];
        if (e == ENC_SELECT) { c3_cur = (uint8_t)clampi(c3_cur + d, 0, 11); return; }
        if (e == ENC_ALGORITHM) { c3_edit(c->id[c3_cur >> 2][c3_cur & 3u], d); return; }
        if (e >= ENC_KNOB1 && e <= ENC_KNOB4) {
            uint32_t col = c3_col(), r = e - ENC_KNOB1;
            c3_cur = (uint8_t)(col * 4u + r);
            c3_edit(c->id[col][r], d);
        }
        return;
    }
    if (e == ENC_PRESETS) {                     /* (on PLAY and BASS only: not by mistake elsewhere) */
        if (page == PG_PLAY || page == PG_BASS) preset_step(d);
        else toast("PRESETS: HOME OR EDIT");
        return;
    }
    if (v->lay == LY_PRESET) {
        uint32_t bank = vi - PAGE_V0[PG_PRE], n = bank ? NUSER : NFACTORY;
        if (e == ENC_SELECT || e == ENC_KNOB1) pre_cur[bank] = (uint8_t)clampi(pre_cur[bank] + d, 0, (int32_t)n - 1);
        else if (e == ENC_KNOB2) { item_set(G_BROWSE, d > 0); toast(set.browse ? "BROWSE: FAVORITES" : "BROWSE: ALL"); }
        else if (e == ENC_KNOB3 || e == ENC_ALGORITHM) ask_load(bank, pre_cur[bank]);
        return;
    }
    if (v->lay == LY_SAVE) {
        if (e == ENC_KNOB1 || e == ENC_SELECT) save_slot = (uint8_t)clampi(save_slot + d, 0, NUSER - 1);
        else if (e == ENC_KNOB2) save_pos = (uint8_t)clampi(save_pos + d, 0, 12);
        else if (e == ENC_KNOB3 || e == ENC_ALGORITHM) {
            static const char CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.#";
            int32_t i, n = (int32_t)sizeof CH - 1;
            for (i = 0; i < n && CH[i] != pname[save_pos]; i++) {}
            pname[save_pos] = CH[((i + d) % n + n) % n];
        } else if (e == ENC_KNOB4) patch[P_STYLE] = (int8_t)clampi(patch[P_STYLE] + d, 0, NSTYLE - 1);
        return;
    }
    if (e == ENC_SELECT) {
        if (v->n) cursor[vi] = (uint8_t)clampi(cursor[vi] + d, 0, v->n - 1);
        return;
    }
    if (e == ENC_ALGORITHM) {
        if (v->n) edit_item(v->it[cursor[vi]], d);
        return;
    }
    if (e >= ENC_KNOB1 && e <= ENC_KNOB4) {
        uint32_t id = vknob(v, e - ENC_KNOB1), i;
        if (id == I_NONE) return;
        edit_item(id, d);
        for (i = 0; i < v->n; i++) if (v->it[i] == id) cursor[vi] = (uint8_t)i;
    }
}

static int page_of_btn(uint32_t b) { uint32_t i; for (i = 0; i < NPAGE; i++) if (PAGE_BTN[i] == b) return (int)i; return -1; }
static int holdable(uint32_t b) { return page_of_btn(b) >= 0 || b == BTN_SAVE || b == BTN_SEL || b == BTN_PLAY || ((set.ui || win == 3u) && b <= BTN_OCT_UP); }
static void hold_toggle(void)
{
    uint32_t i;
    hold ^= 1u;
    if (!hold) for (i = 0; i < 128u; i++) if (latched[i]) { latched[i] = 0; ev_push(EV_OFF, i, 0); }
    toast(hold ? "HOLD ON" : "HOLD OFF");
}
static uint32_t hold_time(uint32_t b)           /* how long until the held action (0: none, only combos) */
{
    if (b == BTN_SAVE) return PRE_BROWSING ? 700u : 600u;
    if (b == BTN_SEL) return 400u;
    if (b == BTN_FX) return 600u;
    if (b == BTN_PLAY) return 600u;
    if (b == BTN_ARP) return 0u;
    if (b == BTN_LFO || b == BTN_ENV || b <= BTN_OCT_UP) return 0u;   /* (they wait for a knob: QUICK ASSIGN, SHIFT) */
    return 1200u;
}
static const char *hold_label(uint32_t b)
{
    static char t[32];
    int pg = page_of_btn(b);
    if (b == BTN_SAVE && PRE_BROWSING) { uint32_t bk = vcur[PG_PRE] ? 1u : 0u;
        return fav_is(bk, pre_cur[bk]) ? "REMOVE FROM FAVORITES" : "ADD TO FAVORITES"; }
    if (b == BTN_SAVE) return "UNDO / REDO";
    if (b == BTN_SEL) return "COMPARE: THE SAVED SOUND";
    if (b == BTN_FX) return win == 3u ? "CLOSE THE EQ" : "EQ";
    if (b == BTN_PLAY) return hold ? "HOLD OFF" : "HOLD: THE KEYS STAY ON";
    if (b == BTN_LFO) return "TURN A KNOB: LFO 1 ON IT";
    if (b == BTN_ENV) return "TURN A KNOB: MOD ENV ON IT";
    str_cat(str_cat(t, "RANDOM "), PAGE_SHORT[pg >= 0 ? pg : 0]);
    return t;
}

/* QUICK ASSIGN (as Pure SUB's drag and drop): LFO 1 (or the mod envelope) onto what this knob moves, the turn
 * sets how much */
static void quick_assign(uint32_t src, uint32_t id, int32_t d)
{
    int32_t dst = dest_of(id);
    uint32_t pd = src == BTN_LFO ? L1_DST : ME_DST, pa = src == BTN_LFO ? L1_DEP : ME_AMT;
    char t[32], *p;
    if (dst < 0) { toast("NOT A DESTINATION"); return; }
    if (patch[pd] != dst) { undo_mark(pd); patch[pd] = (int8_t)dst; patch[pa] = 0; }
    undo_mark(pa);
    patch[pa] = (int8_t)clampi(patch[pa] + d * 2, -64, 63);
    if ((dst == D_DRAMT || dst == D_DRMIX) && !patch[DR_ON]) patch[DR_ON] = 1;
    p = str_cat(t, src == BTN_LFO ? "LFO1 > " : "ENV > ");
    p = str_cat(p, dst_name((uint32_t)dst));
    *p++ = ' ';
    item_text(pa, p);
    toast(t);
}
/* RANDOM: the page's view shaken by about a quarter (lists move one time in five; switches and destinations stay) */
static void randomize(uint32_t pg)
{
    static const uint8_t PLAYR[] = {A_P1, A_P2, A_P3, B_P1, B_P2, B_P3, F_CUT, F_RES, X_AMT, L1_DIV, L1_SHP, L1_DEP};
    const view_t *v = &VIEWS[PAGE_V0[pg] + vcur[pg]];
    const uint8_t *it = pg == PG_PLAY ? PLAYR : v->it;
    uint32_t n = pg == PG_PLAY ? sizeof PLAYR : v->n, i;
    undo_push();
    undo_id = 0xFFFFu;
    for (i = 0; i < n; i++) {
        uint32_t id = it[i];
        int32_t lo, hi, r;
        if (id >= NP || PD[id].fmt == FM_DEST || PD[id].list == L_ONOFF || id == V_MODE || id == P_STYLE) continue;
        lo = PD[id].lo; hi = PD[id].hi;
        if (PD[id].fmt == FM_LIST || PD[id].fmt == FM_DIV) {
            if (ui_rnd() % 5u == 0u) patch[id] = (int8_t)(lo + (int32_t)(ui_rnd() % (uint32_t)(hi - lo + 1)));
            continue;
        }
        r = (hi - lo) / 4 + 1;
        patch[id] = (int8_t)clampi(patch[id] + (int32_t)(ui_rnd() % (uint32_t)(2 * r + 1)) - r, lo, hi);
    }
    pdirty = 1;
    prev_dirty = 1;
    toast("RANDOM \xB7 SAVE HELD = UNDO");
}
static void compare(int on)
{
    if (on) {
        int8_t p[NP_MAX];
        char nm[14];
        if (!pdirty) { toast("UNCHANGED"); return; }
        memcpy(cmp_buf, patch, NP_MAX);
        if (set.bank) user_get(set.idx, p);
        else preset_factory(set.idx, p, nm);
        memcpy(patch, p, NP_MAX);
        cmp_on = 1;
    } else if (cmp_on) {
        memcpy(patch, cmp_buf, NP_MAX);
        cmp_on = 0;
    }
    prev_dirty = 1;
}
static void favorite(void)                       /* the preset under the PRESETS cursor */
{
    uint32_t bk = vcur[PG_PRE] ? 1u : 0u, k = pre_cur[bk], i = fav_i(bk, k);
    if (i >= 96u || (bk && !up_valid(k))) { toast("EMPTY SLOT"); return; }
    set.fav[i >> 3] ^= (uint8_t)(1u << (i & 7u));
    set_dirty = 1;
    set_ms = fm1_ms;
    toast(fav_is(bk, k) ? "* FAVORITE" : "NOT A FAVORITE");
}

static void ask_done(uint32_t sel)
{
    ask_on = 0;
    splash_until = 0;
    if (set.ui != sel) { set.ui = (uint8_t)sel; set_dirty = 1; set_ms = fm1_ms; }
    toast(sel ? "SHIFT MODE" : "CLASSIC MODE");
    g_force = 1;
}
static void ui_input(void)
{
    uint32_t rel, pr = fm1_input_edges(&rel), b, k, now = fm1_in.notes, ne = fm1_input_note_edges();
    int32_t d;
    if (ask_on) {                               /* the start's question: OCT- CLASSIC, OCT+ SHIFT, a knob turns, SEL says yes */
        for (b = 0; b < 7u; b++) if ((d = fm1_enc_take(b)) != 0) ask_sel = (uint8_t)(d > 0);
        if (pr & (1u << BTN_OCT_DOWN)) ask_done(0);
        else if (pr & (1u << BTN_OCT_UP)) ask_done(1);
        else if (pr & ~3u) ask_done(ask_sel);
        else if (fm1_ms - ask_ms > 20000u) ask_done(ask_sel);   /* (no answer: the one shown) */
        pr = 0;
    }
    for (b = 0; b < 14u; b++) {
        if (!(pr & (1u << b))) continue;
        splash_until = 0;
        if (hb.b == BTN_SAVE && !PRE_BROWSING && b <= BTN_OCT_UP) {   /* SAVE held: OCT- UNDO, OCT+ REDO */
            ur_on = 1; hb.fired = 1;
            if (b == BTN_OCT_DOWN) undo(); else redo();
            continue;
        }
        if ((set.ui || win == 3u) && b <= BTN_OCT_UP && (fm1_in.buttons & 3u) == 3u) {
            notes_all_off(); ev_push(EV_PANIC, 0, 0); toast("ALL NOTES OFF");
            if (hb.b <= BTN_OCT_UP) hb.used = 1;
            continue;
        }
        if (holdable(b) && hb.b == 0xFF && !pend_on) { hb.b = (uint8_t)b; hb.used = hb.fired = 0; hb.at = fm1_ms; hb.next = hold_time(b); }
        else if (!holdable(b) || pend_on) on_button(b);
    }
    if (hb.b != 0xFF) {                         /* a button held: its action when the bar is full */
        uint32_t held = (fm1_in.buttons >> hb.b) & 1u;
        if (held && !hb.used && hb.next && fm1_ms - hb.at >= hb.next) {
            if (hb.b == BTN_SAVE && PRE_BROWSING) { favorite(); hb.next = 0; }
            else if (hb.b == BTN_SAVE) { ur_on = 1; hb.next = 0; }   /* (then OCT- UNDO, OCT+ REDO) */
            else if (hb.b == BTN_SEL) { compare(1); hb.next = 0; }
            else if (hb.b == BTN_PLAY) { hold_toggle(); hb.next = 0; }
            else if (hb.b == BTN_FX) { win = win == 3u ? 0u : 3u; vpop_ms = 0; g_force = 1; hb.next = 0; }
            else { randomize((uint32_t)page_of_btn(hb.b)); hb.next = 0; }
            hb.fired = 1;
        }
        if ((rel >> hb.b) & 1u || !held) {
            uint32_t bb = hb.b;
            hb.b = 0xFF;
            if (bb == BTN_SAVE) ur_on = 0;
            if (cmp_on) compare(0);
            else if (!hb.used && !hb.fired && (bb > BTN_OCT_UP || fm1_ms - hb.at < 500u)) on_button(bb);   /* (OCT held long: no octave) */
        }
    }
    for (b = 0; b < 7u; b++) if ((d = fm1_enc_take(b)) != 0) {
        if (cmp_on) continue;                   /* (comparing: nothing changes) */
        if (hb.b <= BTN_OCT_UP && b >= ENC_KNOB1 && b <= ENC_KNOB4) hb.used = 1;   /* (SHIFT: no octave on release) */
        if (hb.b != 0xFF && (hb.b == BTN_LFO || hb.b == BTN_ENV) && ((b >= ENC_KNOB1 && b <= ENC_KNOB4) || b == ENC_ALGORITHM)) {
            const view_t *v = view();
            uint32_t id = b == ENC_ALGORITHM ? (v->n ? v->it[cursor[vidx()]] : I_NONE) : vknob(v, b - ENC_KNOB1);
            if (win) id = I_NONE;
            else if (C3_ON) id = b == ENC_ALGORITHM ? COL3[page].id[c3_cur >> 2][c3_cur & 3u] : COL3[page].id[0][b - ENC_KNOB1];
            hb.used = 1;
            if (id != I_NONE) quick_assign(hb.b, rate_like(id), d);
            continue;
        }
        if (hb.b != 0xFF && page_of_btn(hb.b) >= 0 && b >= ENC_KNOB1 && b <= ENC_KNOB4) {   /* SHIFT: that page's knobs */
            uint32_t pg = (uint32_t)page_of_btn(hb.b), id = vknob(&VIEWS[PAGE_V0[pg] + vcur[pg]], b - ENC_KNOB1);
            hb.used = 1;
            if (id == I_NONE || VIEWS[PAGE_V0[pg] + vcur[pg]].lay >= LY_PRESET) continue;
            str_cat(str_cat(vpop_pre, PAGE_SHORT[pg]), " \xB7 ");
            edit_item(id, d);
            continue;
        }
        vpop_pre[0] = 0;
        on_enc(b, d);
    }
    for (k = 0; k < 27u; k++) {
        uint32_t m = 1u << k;
        if ((ne & m) || ((now & m) && !(prev_keys & m))) {
            int32_t n = clampi(53 + (int32_t)k + 12 * OCT, 0, 127);
            if (prev_keys & m) key_up(key_note[k]);
            key_note[k] = (uint8_t)n;
            key_down((uint32_t)n, 100);
        } else if (!(now & m) && (prev_keys & m))
            key_up(key_note[k]);
    }
    prev_keys = now;
}

/* ---- MIDI in: notes, CC (learnt ones first), bend, pressure, the clock, start / stop, program change */
static uint32_t clk_last, clk_per, clk_n;
static int midi_learnt(uint32_t cc, uint32_t v)  /* 1: a CC learnt (it moves its setting) */
{
    uint32_t k;
    if (perf.cc_magic != 0x43u) return 0;
    if (ml_state == 2u) {                       /* (learning: this CC goes to the setting turned) */
        char t[24], *p;
        for (k = 0; k < 16u && perf.cc_num[k] != cc && perf.cc_num[k] != 0xFFu; k++) {}
        if (k == 16u) k = 15u;
        perf.cc_num[k] = (uint8_t)cc; perf.cc_id[k] = ml_id;
        ml_state = 0;
        perf_touch();
        p = str_cat(t, "CC "); p = fmt_u(p, cc, 0); p = str_cat(p, " > "); str_cat(p, item_name(ml_id));
        toast(t);
        return 1;
    }
    for (k = 0; k < 16u; k++)
        if (perf.cc_num[k] == cc) {
            int32_t lo, hi;
            item_range(perf.cc_id[k], &lo, &hi);
            item_set(perf.cc_id[k], lo + (int32_t)v * (hi - lo) / 127);
            return 1;
        }
    return 0;
}
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        uint32_t ty = st & 0xF0u, ch = (st & 15u) + 1u;
        if (st == 0xF8) {                       /* the clock: its tempo, and the beat it is on */
            uint32_t t = fm1_ticks();
            if (set.clock && clk_last) {
                uint32_t p = t - clk_last;
                clk_per = clk_per ? clk_per + ((int32_t)(p - clk_per) >> 3) : p;
                if (clk_per) { uint32_t b = 600000000u / clk_per; if (b >= 300u && b <= 3000u) ks_bpm10 = b; }
                if (++clk_n % 24u == 0u) ev_push(EV_SYNC, 1, clk_n % 1152u);
            }
            clk_last = t;
            continue;
        }
        if (st == 0xFA) { clk_n = 0; ev_push(EV_SYNC, 0, 0); if (set.mstart) seq_play(1); continue; }
        if (st == 0xFB) { if (set.mstart) ev_push(EV_SEQ, 1, 0); continue; }   /* (continue: from where it is) */
        if (st == 0xFC) { if (set.mstart) seq_play(0); continue; }
        if (st >= 0xF0) continue;
        if (set.midich && ch != set.midich) continue;
        if (ty == 0x90 && d2) ev_push(EV_ON, d1, d2);
        else if (ty == 0x80 || ty == 0x90) ev_push(EV_OFF, d1, 0);
        else if (ty == 0xE0) ks_bend = (((int32_t)d2 << 7 | d1) - 8192) * 4;
        else if (ty == 0xD0) ks_press = d1 * 258;
        else if (ty == 0xC0) {                  /* program change: bank A, then the user's B */
            if (!set.mprog) continue;
            if (d1 < NFACTORY) { load_preset(0, d1); toast(pname); }
            else if (up_valid(d1 - NFACTORY)) { load_preset(1, d1 - NFACTORY); toast(pname); }
        }
        else if (ty == 0xB0) {
            if (midi_learnt(d1, d2)) continue;
            switch (d1) {
            case 1: ks_wheel = d2 * 258; break;
            case 74: patch[F_CUT] = (int8_t)d2; pdirty = 1; break;
            case 71: patch[F_RES] = (int8_t)d2; pdirty = 1; break;
            case 76: patch[L1_DIV] = (int8_t)(set.grid ? DIV_NEAR[d2 >> 3] : d2 >> 3); break;
            case 77: patch[L1_DEP] = (int8_t)(d2 - 64); break;
            case 123: notes_all_off(); ev_push(EV_PANIC, 0, 0); break;
            }
        }
    }
}

static void midi_clock_out(void)                /* CLOCK OUT (internal clock): 24 a beat, start and stop with the SEQ */
{
    static uint32_t last = 0xFFFFFFFFu;
    static uint8_t was;
    uint32_t now = (uint32_t)(((uint64_t)ks_beats * 24u) >> 24), n;
    if (!set.mclk || set.clock) { last = 0xFFFFFFFFu; return; }
    if (sq_play != was) { was = sq_play; midi_send(sq_play ? 0xFAu : 0xFCu, 0, 0); }
    if (last == 0xFFFFFFFFu) last = now;
    n = (now + 1152u - last) % 1152u;           /* (ks_beats: 48 beats round) */
    if (n > 8u) n = 1u;                         /* (the main loop was away: no burst) */
    while (n--) midi_send(0xF8u, 0, 0);
    last = now;
}
static void app_poll(void) { midi_in(); midi_clock_out(); }
static int app_sysex(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }

/* ================================================================ the main loop */
static uint32_t ui_frame_ms, prev_ms;
static void app_init(void)
{
    uint32_t i;
    int ok;
    {                                           /* (a save from before a field was added: the fields it has, the rest as is) */
        const uint32_t LEN[5] = {sizeof set, (uint32_t)((uint8_t *)&set.mstart - (uint8_t *)&set), (uint32_t)((uint8_t *)&set.zoom - (uint8_t *)&set),
                                 (uint32_t)((uint8_t *)&set.ui - (uint8_t *)&set), (uint32_t)((uint8_t *)&set.accel - (uint8_t *)&set)};
        for (ok = 0, i = 0; i < 5u && !ok; i++) ok = !settings_load(&set, LEN[i]) && set.ver == 6u;
    }
    if (!ok) {
        set.theme = set.pal = set.font = set.grid = set.browse = 0;
        memset(set.fav, 0, sizeof set.fav);
        set.ver = 6; set.midich = 0; set.clock = 0; set.oct = 0xFF; set.bpm10 = 1400; set.bank = 0; set.idx = 0; set.cores = 1; set.accel = 1; set.c3col = 0; set.ui = KS_B; set.ask = 1; set.zoom = 0; set.mstart = 1; set.mprog = 1; set.mclk = 0;
    }
    set.cores = set.cores ? 1u : 0u;
    theme_set(set.theme, set.pal);
    font_set(set.font, set.zoom);
    set.grid = set.grid ? 1u : 0u;
    ks_straight = set.grid;
    set.bpm10 = (uint16_t)clampi(set.bpm10, 600, 2400);
    set.oct = (uint8_t)(int8_t)clampi(OCT, -3, 3);
    ks_bpm10 = set.bpm10;
    store_load();
    ux_migrate();
    for (i = 0; i < NUSER; i++) if (!up_valid(i)) ustore[i].magic = 0;
    if (perf.magic != PF_MAGIC || perf.sum != perf_sum()) perf_defaults();
    perf.ar_on = 0;                             /* (the ARP starts off: a key does what it says) */
    if (perf.mc_magic != MC_MAGIC) mc_defaults();
    if (perf.cc_magic != 0x43u) { memset(perf.cc_num, 0xFF, sizeof perf.cc_num); perf.cc_magic = 0x43u; }
    if (perf.eq_magic != 0x45u) eq_defaults();
    eq_dirty = 1;
    load_preset(set.bank, set.idx);
    if (set.bank && !up_valid(set.idx)) load_preset(0, 0);
    pre_cur[set.bank] = set.idx;
    set_dirty = 0;
    dual_on = 0;
#if KS_DUAL
    if (set.cores) {                            /* the voices on core 1: unless the last try did not hold */
        if (dual_mark[0] == DUAL_MAGIC && dual_mark[1] == ~DUAL_MAGIC) {
            set.cores = 0;                      /* (it crashed within 10 s: one core from now, SYSTEM to try again) */
            set_dirty = 1;
            set_ms = fm1_ms;
            toast("ONE CORE (SAFE)");
        } else {
            dual_mark[0] = DUAL_MAGIC;
            dual_mark[1] = ~DUAL_MAGIC;
            dual_ms = fm1_ms;
            dual_on = fm1_cpu1_start(core1_main) == 0;
            if (!dual_on) { dual_mark[0] = dual_mark[1] = 0; toast("CORE 1 SILENT: ONE CORE"); }
        }
    }
    if (!set.cores) dual_mark[0] = dual_mark[1] = 0;
#endif
    booted = 1;
    splash_until = fm1_ms + 2200u;            /* the start screen: the last patch, the cores */
    if (set.ask) { ask_on = 1; ask_sel = set.ui; ask_ms = fm1_ms; }   /* ... and the question: CLASSIC or SHIFT */
    g_force = 1;
}

static void app_step(void)
{
    ui_input();
    if (eq_dirty) eq_update();
    if (toast_ms && (int32_t)(fm1_ms - toast_ms) > 1400) toast_ms = 0;
    if (pend_on && fm1_ms - pend_ms > 6000u) { pend_on = 0; toast("KEPT"); }
    if (dpop_ms && (int32_t)(fm1_ms - dpop_ms) > 1500) dpop_ms = 0;
    if (vpop_ms && (int32_t)(fm1_ms - vpop_ms) > 1000) vpop_ms = 0;   /* (toast_ms can be 1 ms ahead: | 1) */
    if (prev_dirty && fm1_ms - prev_ms > 60u) { prev_dirty = 0; prev_ms = fm1_ms; preview_make(); }
    if (fm1_ms - ui_frame_ms >= 33u) {          /* ~30 frames a second, only what changed is sent */
        ui_frame_ms = fm1_ms;
        ui_leds();
        g_frame_send(draw_all);
    }
    if (sq_rec && vidx() != V_SEQ) sq_rec = 0;
    if (perf_dirty && !sq_play && !ks_sounding && fm1_ms - perf_ms > 2000u) { perf_dirty = 0; if (perf_save()) toast("SEQ NOT SAVED"); }
    if (set_dirty && !ks_sounding && fm1_ms - set_ms > 2000u) {   /* (erasing stalls the sound: only when silent) */
        set_dirty = 0;
        settings_write();
    }
#if KS_DUAL
    if (c1_failed) {                            /* core 1 stalled (no sound, frozen LFOs): one core, kept */
        c1_failed = 0;
        set.cores = 0; set_dirty = 1; set_ms = fm1_ms;
        dual_mark[0] = dual_mark[1] = 0;
        toast("CORE 1 TOO SLOW: 1 CORE");
    }
    if (dual_on && dual_mark[0] && fm1_ms - dual_ms > 10000u) dual_mark[0] = dual_mark[1] = 0;   /* 10 s on two cores: it holds */
#endif
}

static void app_main(void)
{
    app_init();
    for (;;) {
        platform_poll();
        app_step();
    }
}

#if defined(KAWA_WASM)
/* the emulator: what the knobs do on this page (their labels), the page, the user store */
WASM_EXPORT("ksub_load") void ksub_load(uint32_t bank, uint32_t k) { load_preset(bank, k); }
WASM_EXPORT("ksub_nfactory") uint32_t ksub_nfactory(void) { return NFACTORY; }
WASM_EXPORT("ksub_page") uint32_t ksub_page(void) { return page; }
WASM_EXPORT("ksub_knob_label") const char *ksub_knob_label(uint32_t i)
{
    const view_t *v = view();
    if (i >= 4u) return "";
    if (v->lay == LY_PRESET) return i == 0u ? "SCROLL" : i == 2u ? "LOAD" : "";
    if (v->lay == LY_SAVE) return i == 0u ? "SLOT" : i == 1u ? "POSITION" : i == 2u ? "LETTER" : "STYLE";
    if (win == 3u) return c3_col() == 1u ? "GAIN" : c3_col() == 2u ? "TYPE" : "FREQ";
    if (win) return item_sname(win == 1u ? LV_IT[i] : G_MC1 + i);
    if (C3_ON) return item_sname(COL3[page].id[c3_col()][i]);
    return vknob(v, i) == I_NONE ? "" : item_sname(vknob(v, i));
}
WASM_EXPORT("ksub_store") uint8_t *ksub_store(void) { return w_store; }
WASM_EXPORT("ksub_store_size") uint32_t ksub_store_size(void) { return sizeof w_store; }
WASM_EXPORT("ksub_store_gen") uint32_t ksub_store_gen(void) { return w_store_gen; }
WASM_EXPORT("ksub_patch_name") const char *ksub_patch_name(void) { return pname; }
#endif
