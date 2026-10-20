/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: the eight pages, the panel, the presets, MIDI, app_main.
 *
 *   HOME 01 PLAY     EDIT 02 BASS     ENV 03 ENVELOPE  SEQ 04 FILTER
 *   LFO  05 LFO      FX   06 FX       ARP 07 PRESETS   GLO 08 SYSTEM
 *   the page's button again, or SEL: its next view (OSC A, OSC B, SUB, COMBINE...)
 *   SELECT: the row   ALGORITHM: the row's value   K1..K4: what the footer says   PRESETS: the next patch
 *   PLAY: HOLD (the notes stay)   REC: TAP tempo   OCT- / OCT+: octave (both: all notes off)
 *   SAVE: save; held: UNDO (and again every 0.4 s while it stays held) */
enum { PG_PLAY, PG_BASS, PG_ENV, PG_FILT, PG_LFO, PG_FX, PG_PRE, PG_SYS, NPAGE };
static const uint8_t PAGE_BTN[NPAGE] = {BTN_HOME, BTN_EDIT, BTN_ENV, BTN_SEQ, BTN_LFO, BTN_FX, BTN_ARP, BTN_GLO};
enum { LY_PLAY, LY_LIST, LY_BOX, LY_STEP, LY_LFO, LY_PRESET, LY_SAVE, LY_SYS };
enum { I_L1RATE = 200, I_L2RATE, G_MIDICH, G_BPM, G_CLOCK, G_OCT, G_CORES, G_CPU, G_FW, G_THEME, G_PAL, G_FONT, G_GRID, I_NONE = 255 };

typedef struct {
    uint8_t lay;
    const char *cat;
    uint8_t n, it[14], k[4];
    const char *tag;
} view_t;
#define OSCV(L, X) {LY_LIST, "OSCILLATOR " L, 8, {X##_ENG, X##_LVL, X##_P1, X##_P2, X##_P3, X##_OCT, X##_SEMI, X##_FINE}, \
                    {X##_LVL, X##_P1, X##_P2, X##_P3}, "EDIT"}
#define FXV(C, ...) {LY_LIST, "FX RACK", 4, {__VA_ARGS__}, {__VA_ARGS__}, "FX"}
static const view_t VIEWS[] = {
    {LY_PLAY, "CURRENT PATCH", 4, {F_CUT, I_L1RATE, L1_DEP, F_RES}, {F_CUT, I_L1RATE, L1_DEP, F_RES}, "HOME"},
    {LY_PLAY, "LIVE MIX", 4, {S_LVL, DR_AMT, DL_MIX, MS_VOL}, {S_LVL, DR_AMT, DL_MIX, MS_VOL}, "HOME"},
    OSCV("A", A), OSCV("B", B),
    {LY_LIST, "SUB + NOISE", 6, {S_LVL, S_WAVE, S_OCT, S_ROUTE, N_LVL, N_COL}, {S_LVL, S_WAVE, N_LVL, N_COL}, "EDIT"},
    {LY_LIST, "COMBINE A + B", 6, {X_MODE, X_AMT, A_LVL, B_LVL, X_DRIFT, X_RESET}, {X_MODE, X_AMT, A_LVL, B_LVL}, "EDIT"},
    {LY_BOX, "AMP ENVELOPE", 4, {AE_A, AE_D, AE_S, AE_R}, {AE_A, AE_D, AE_S, AE_R}, "ENV"},
    {LY_BOX, "FILTER ENVELOPE", 4, {FE_A, FE_D, FE_S, FE_R}, {FE_A, FE_D, FE_S, FE_R}, "ENV"},
    {LY_BOX, "MOD ENVELOPE", 4, {ME_A, ME_D, ME_AMT, ME_DST}, {ME_A, ME_D, ME_AMT, ME_DST}, "ENV"},
    {LY_BOX, "FILTER / COLOUR", 4, {F_CUT, F_RES, F_DRV, F_TYPE}, {F_CUT, F_RES, F_DRV, F_TYPE}, "FX"},
    {LY_LIST, "FILTER MOD", 6, {F_TYPE, F_ENV, F_KEY, F_VEL, F_CUT, F_RES}, {F_ENV, F_KEY, F_VEL, F_TYPE}, "FX"},
    {LY_LFO, "LFO 1 \xB7 WOBBLE", 9, {L1_SHP, I_L1RATE, L1_DEP, L1_DST, G_GRID, L1_SYNC, L1_TRIG, L1_PH, L1_SMO},
     {L1_SHP, I_L1RATE, L1_DEP, L1_DST}, "LFO"},
    {LY_LFO, "LFO 2 \xB7 MOTION", 9, {L2_SHP, I_L2RATE, L2_DEP, L2_DST, G_GRID, L2_SYNC, L2_TRIG, L2_PH, L2_SMO},
     {L2_SHP, I_L2RATE, L2_DEP, L2_DST}, "LFO"},
    {LY_STEP, "STEP FX", 7, {ST_ON, ST_PAT, ST_RATE, ST_GATE, ST_DST, ST_DEP, ST_SMO}, {ST_PAT, ST_RATE, ST_GATE, ST_DEP}, "LFO"},
    {LY_LIST, "MOD MATRIX", 12, {M1_SRC, M1_DST, M1_AMT, M2_SRC, M2_DST, M2_AMT, M3_SRC, M3_DST, M3_AMT, M4_SRC, M4_DST, M4_AMT},
     {M1_AMT, M2_AMT, M3_AMT, M4_AMT}, "LFO"},
    FXV(0, DR_ON, DR_TYPE, DR_AMT, DR_MIX), FXV(1, CR_ON, CR_BITS, CR_RATE, CR_MIX), FXV(2, CH_ON, CH_RATE, CH_DEP, CH_MIX),
    {LY_LIST, "FX RACK", 5, {DL_ON, DL_TIME, DL_FB, DL_TONE, DL_MIX}, {DL_TIME, DL_FB, DL_TONE, DL_MIX}, "FX"},
    FXV(4, RV_ON, RV_SIZE, RV_DAMP, RV_MIX),
    {LY_LIST, "FX RACK", 3, {MS_GLUE, MS_SUB, MS_VOL}, {MS_GLUE, MS_SUB, MS_VOL, I_NONE}, "FX"},
    {LY_PRESET, "PRESET BANK A", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_PRESET, "PRESET BANK B", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_SAVE, "SAVE PATCH", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_SYS, "GLOBAL SETUP", 13, {G_THEME, G_PAL, G_FONT, G_GRID, G_MIDICH, V_MODE, V_GLIDE, V_BEND, V_VEL, G_BPM, G_CLOCK, G_CORES, G_CPU},
     {G_THEME, G_PAL, G_FONT, G_BPM}, "SYSTEM"},
};
static const uint8_t PAGE_V0[NPAGE + 1] = {0, 2, 6, 9, 11, 15, 21, 24, 25};   /* each page's first view */
static const char *const FX_TITLE[6] = {"SATURATION", "BITCRUSH", "CHORUS", "DUB DELAY", "REVERB", "MASTER"};
static const char *const FT_TITLE[NFT] = {"DARK EDGE", "SOFT EDGE", "BAND RAZOR", "THIN AIR", "HOLLOW", "TALK BOX", "METAL COMB"};
static const char *const FT_SUB[NFT] = {"LOW PASS \xB7 24 dB", "LOW PASS \xB7 12 dB", "BAND PASS \xB7 12 dB", "HIGH PASS \xB7 24 dB",
                                        "NOTCH \xB7 12 dB", "FORMANT \xB7 A E I O U", "COMB \xB7 TUNED BY CUTOFF"};

/* ---- state */
#define NUSER 32u
typedef struct {
    uint8_t magic, sum;
    char name[14];
    int8_t p[NP_MAX];
} upatch_t;                                     /* 128 B: 32 of them fill one 4 KiB sector of the store */
#define UP_MAGIC 0xB5u
static upatch_t ustore[NUSER];
static struct {
    uint8_t ver, midich, clock, oct;            /* midich 0: omni; clock 0 internal, 1 MIDI */
    uint16_t bpm10;
    uint8_t bank, idx, cores;                   /* cores 1: both (the voices on core 1) */
    uint8_t theme, pal, font;                   /* CLASSIC STUDIO PURE RACK; its palettes; the font */
    uint8_t grid;                               /* LFO GRID: 0 ALL, 1 STRAIGHT (round divisions only) */
} set = {5, 0, 0, 0xFF, 1400, 0, 0, 1, 0, 0, 0, 0};
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
static uint8_t w_store[4096];
static uint32_t w_store_gen;
static void store_load(void) { memcpy(ustore, w_store, sizeof ustore); }
static int store_save(void) { memcpy(w_store, ustore, sizeof ustore); w_store_gen++; return 0; }
static int settings_write(void) { return settings_save(&set, sizeof set); }
#else
static void store_load(void) { if (!flash_ok || st_read(STORE_LO, ustore, sizeof ustore)) memset(ustore, 0xFF, sizeof ustore); }
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
static uint8_t up_sum(const upatch_t *u)
{
    uint32_t i, s = 0x5A;
    for (i = 0; i < sizeof u->name; i++) s += (uint8_t)u->name[i];
    for (i = 0; i < NP_MAX; i++) s = (s * 3u + (uint8_t)u->p[i]) & 0xFFu;
    return (uint8_t)s;
}
static int up_valid(uint32_t k) { return k < NUSER && ustore[k].magic == UP_MAGIC && ustore[k].sum == up_sum(&ustore[k]); }

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
static uint8_t pdirty;                          /* the sound changed since it was loaded or saved */
static uint8_t pend_on, pend_bank, pend_k;      /* a load waiting for SEL (the sound changed) */
static uint32_t pend_ms;

/* ---- undo: the patch before each change (turns of the same knob within 1.5 s are one change), 16 deep */
#define NUNDO 16u
static int8_t undo_p[NUNDO][NP_MAX];
static char undo_nm[NUNDO][14];
static uint32_t undo_w, undo_n, undo_ms, undo_id = 0xFFFFu;
static uint8_t booted;
static void undo_push(void)
{
    if (!booted) return;
    memcpy(undo_p[undo_w], patch, NP_MAX);
    memcpy(undo_nm[undo_w], pname, 14);
    undo_w = (undo_w + 1u) % NUNDO;
    if (undo_n < NUNDO) undo_n++;
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

static void load_preset(uint32_t bank, uint32_t k)
{
    int8_t p[NP_MAX];
    uint32_t i;
    if (bank && !up_valid(k)) { toast("EMPTY SLOT"); return; }
    undo_push();                                /* (a load can be undone too) */
    undo_id = 0xFFFFu;
    if (bank) {
        memcpy(p, ustore[k].p, NP_MAX);
        patch_sanitize(p);
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
    memcpy(u->p, patch, NP_MAX);
    u->sum = up_sum(u);
    if (store_save()) { toast("SAVE FAILED"); return; }
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
    *hi = id == G_MIDICH ? 16 : id == G_BPM ? 2400 : id == G_CLOCK || id == G_CORES || id == G_GRID ? 1 : id == G_THEME || id == G_FONT ? 3 : id == G_PAL ? 5 : id == G_OCT ? 6 : 0;
    if (id == G_BPM) *lo = 600;
}
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
        if (id < N_LVL || id == S_LVL || id == S_WAVE || id == S_OCT) prev_dirty = 1;
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
    case G_FONT: set.font = (uint8_t)v; font_set(set.font); g_force = 1; break;
    case G_GRID: set.grid = (uint8_t)v; ks_straight = set.grid; break;
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
    case G_FW: return "FIRMWARE";
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
    return id == G_MIDICH ? "MIDI" : id == G_BPM ? "BPM" : id == G_CLOCK ? "CLOCK" : id == G_OCT ? "OCT" : id == G_CORES ? "CORES" : id == G_THEME ? "THEME" : id == G_PAL ? "PALETTE" : id == G_FONT ? "FONT" : id == G_GRID ? "GRID" : "";
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
    if (d == D_DRAMT || d == D_DRMIX) return patch[DR_ON];
    if (d >= D_CRBITS && d <= D_CRMIX) return patch[CR_ON];
    if (d >= D_CHRATE && d <= D_CHMIX) return patch[CH_ON];
    if (d >= D_DLFB && d <= D_DLMIX) return patch[DL_ON];
    if (d >= D_RVSIZE && d <= D_RVMIX) return patch[RV_ON];
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
        case G_PAL: { static const char *const PN[4] = {"SYNTHWAVE", "MINT", "BLACK / WHITE", "INVERTED"}; str_cat(b, v < 2 ? PAL_NAME[set.theme & 3][v] : PN[(v - 2) & 3]); break; }
        case G_GRID: str_cat(b, v ? "STRAIGHT" : "ALL (T, D)"); break;
        case G_FONT: { static const char *const FN[4] = {"SANS", "MONO", "HYPER", "PIXEL"}; str_cat(b, FN[v & 3]); break; }
        case G_CPU:                             /* each core's share of a block, and the governor's step */
            b = str_cat(b, "C0 "); b = fmt_u(b, ks_load[0], 0); b = str_cat(b, "%");
            if (dual_on) { b = str_cat(b, " C1 "); b = fmt_u(b, ks_load[1], 0); b = str_cat(b, "%"); }
            if (ks_shed) { b = str_cat(b, " \xB7 -"); fmt_u(b, ks_shed, 0); }
            break;
        case G_FW: str_cat(b, "K-WA SUB " APP_VER); break;
        }
        return;
    }
    switch (PD[rid].fmt) {
    case FM_PCT: b = fmt_u(b, (uint32_t)v * 100u / 127u, 0); str_cat(b, "%"); break;
    case FM_BIP: b = v ? fmt_s(b, v * 100 / 64) : fmt_u(b, 0, 0); str_cat(b, "%"); break;
    case FM_LIST: case FM_DIV: str_cat(b, PD[rid].list[(rid == L1_DIV || rid == L2_DIV) && set.grid ? DIV_NEAR[v & 15] : v]); break;
    case FM_DEST: str_cat(b, dst_name((uint32_t)v)); break;
    case FM_INT: if (v) fmt_s(b, v); else fmt_u(b, 0, 0); break;
    case FM_SEMI: b = v && PD[rid].lo < 0 ? fmt_s(b, v) : fmt_u(b, (uint32_t)(v < 0 ? -v : v), 0); str_cat(b, " st"); break;
    case FM_CENT: b = v ? fmt_s(b, v) : fmt_u(b, 0, 0); str_cat(b, " ct"); break;
    case FM_TIME: {
        uint32_t t = KS_ENV_MS10[v];
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
    oscp_t qa, qb;
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
    led(BTN_PLAY, hold ? 2u : 0u);
    led(BTN_REC, beat ? 2u : 0u);
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
static void edit_item(uint32_t id, int32_t d)
{
    int32_t lo, hi, v = item_get(id), step = 1;
    if (id == I_NONE || id == G_CPU || id == G_FW) return;
    if (item_patch(id) && PD[id].fmt == FM_DEST) { item_set(id, dst_step(v, d)); return; }
    if (set.grid && (rate_like(id) == L1_DIV || rate_like(id) == L2_DIV)) {   /* STRAIGHT: from round to round */
        int32_t i, at = 0;
        for (i = 0; i < 9; i++) if (DIV_STRAIGHT[i] == DIV_NEAR[v & 15]) at = i;
        item_set(id, DIV_STRAIGHT[clampi(at + d, 0, 8)]);
        return;
    }
    item_range(id, &lo, &hi);
    if (hi - lo > 40 && fm1_ms - last_turn_ms < 45u) step = id == G_BPM ? 10 : 3;   /* fast: bigger steps */
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
    while (cnt--)
        do at = (at + st) % n; while (at >= NFACTORY && !up_valid(at - NFACTORY));
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

static void on_button(uint32_t b)
{
    uint32_t i;
    if (pend_on) {                              /* the question: SEL loads, any other button keeps the sound */
        pend_on = 0;
        if (b == BTN_SEL) { load_preset(pend_bank, pend_k); toast(pname); }
        else toast("KEPT");
        return;
    }
    for (i = 0; i < NPAGE; i++)
        if (PAGE_BTN[i] == b) {
            if (page == i) vcur[i] = (uint8_t)((vcur[i] + 1u) % (uint32_t)(PAGE_V0[i + 1u] - PAGE_V0[i]));
            else page = (uint8_t)i;
            if (VIEWS[vidx()].lay == LY_SAVE && b == BTN_ARP) vcur[i] = 0;   /* (SAVE opens from SAVE only) */
            name_trim();
            return;
        }
    switch (b) {
    case BTN_SEL:
        vcur[page] = (uint8_t)((vcur[page] + 1u) % (uint32_t)(PAGE_V0[page + 1u] - PAGE_V0[page]));
        if (VIEWS[vidx()].lay == LY_SAVE) vcur[page] = 0;
        name_trim();
        break;
    case BTN_SAVE:
        if (VIEWS[vidx()].lay == LY_SAVE) { name_trim(); save_user(save_slot); vcur[PG_PRE] = 1; pre_cur[1] = save_slot; }
        else save_open();
        break;
    case BTN_PLAY:
        hold ^= 1u;
        if (!hold) for (i = 0; i < 128u; i++) if (latched[i]) { latched[i] = 0; ev_push(EV_OFF, i, 0); }
        toast(hold ? "HOLD ON" : "HOLD OFF");
        break;
    case BTN_REC: {                             /* TAP: the tempo from the last taps, the LFOs on the beat */
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

static void on_enc(uint32_t e, int32_t d)
{
    const view_t *v = view();
    uint32_t vi = vidx();
    if (e == ENC_PRESETS) {                     /* (on PLAY and BASS only: not by mistake elsewhere) */
        if (page == PG_PLAY || page == PG_BASS) preset_step(d);
        else toast("PRESETS: HOME OR EDIT");
        return;
    }
    if (v->lay == LY_PRESET) {
        uint32_t bank = vi - PAGE_V0[PG_PRE], n = bank ? NUSER : NFACTORY;
        if (e == ENC_SELECT || e == ENC_KNOB1) pre_cur[bank] = (uint8_t)clampi(pre_cur[bank] + d, 0, (int32_t)n - 1);
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
        uint32_t id = v->k[e - ENC_KNOB1], i;
        if (id == I_NONE) return;
        edit_item(id, d);
        for (i = 0; i < v->n; i++) if (v->it[i] == id) cursor[vi] = (uint8_t)i;
    }
}

static void ui_input(void)
{
    uint32_t rel, pr = fm1_input_edges(&rel), b, k, now = fm1_in.notes, ne = fm1_input_note_edges();
    int32_t d;
    for (b = 0; b < 14u; b++) if ((pr & (1u << b)) && b != BTN_SAVE) on_button(b);
    {                                           /* SAVE: a tap saves (on release), held 0.6 s undoes, and on */
        static uint32_t at, next;
        static uint8_t down, undone;
        if (pr & (1u << BTN_SAVE)) { down = 1; undone = 0; at = fm1_ms; next = 600u; }
        if (down && (fm1_in.buttons & (1u << BTN_SAVE)) && fm1_ms - at >= next) { undo(); undone = 1; next += 400u; }
        if (rel & (1u << BTN_SAVE)) { if (down && !undone) on_button(BTN_SAVE); down = 0; }
    }
    for (b = 0; b < 7u; b++) if ((d = fm1_enc_take(b)) != 0) on_enc(b, d);
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

/* ---- MIDI in: notes, CC, bend, pressure, the clock */
static uint32_t clk_last, clk_per, clk_n;
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
        if (st == 0xFA) { clk_n = 0; ev_push(EV_SYNC, 0, 0); continue; }
        if (st >= 0xF0) continue;
        if (set.midich && ch != set.midich) continue;
        if (ty == 0x90 && d2) ev_push(EV_ON, d1, d2);
        else if (ty == 0x80 || ty == 0x90) ev_push(EV_OFF, d1, 0);
        else if (ty == 0xE0) ks_bend = (((int32_t)d2 << 7 | d1) - 8192) * 4;
        else if (ty == 0xD0) ks_press = d1 * 258;
        else if (ty == 0xB0) {
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

static void app_poll(void) { midi_in(); }
static int app_sysex(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }

/* ================================================================ the main loop */
static uint32_t ui_frame_ms, prev_ms;
static void app_init(void)
{
    uint32_t i;
    if (settings_load(&set, sizeof set) || set.ver != 5u) {
        set.theme = set.pal = set.font = set.grid = 0;
        set.ver = 5; set.midich = 0; set.clock = 0; set.oct = 0xFF; set.bpm10 = 1400; set.bank = 0; set.idx = 0; set.cores = 1;
    }
    set.cores = set.cores ? 1u : 0u;
    theme_set(set.theme, set.pal);
    font_set(set.font);
    set.grid = set.grid ? 1u : 0u;
    ks_straight = set.grid;
    set.bpm10 = (uint16_t)clampi(set.bpm10, 600, 2400);
    set.oct = (uint8_t)(int8_t)clampi(OCT, -3, 3);
    ks_bpm10 = set.bpm10;
    store_load();
    for (i = 0; i < NUSER; i++) if (!up_valid(i)) ustore[i].magic = 0;
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
    g_force = 1;
}

static void app_step(void)
{
    ui_input();
    if (toast_ms && (int32_t)(fm1_ms - toast_ms) > 1400) toast_ms = 0;
    if (pend_on && fm1_ms - pend_ms > 6000u) { pend_on = 0; toast("KEPT"); }   /* (toast_ms can be 1 ms ahead: | 1) */
    if (prev_dirty && fm1_ms - prev_ms > 60u) { prev_dirty = 0; prev_ms = fm1_ms; preview_make(); }
    if (fm1_ms - ui_frame_ms >= 33u) {          /* ~30 frames a second, only what changed is sent */
        ui_frame_ms = fm1_ms;
        ui_leds();
        g_frame_send(draw_all);
    }
    if (set_dirty && !ks_sounding && fm1_ms - set_ms > 2000u) {   /* (erasing stalls the sound: only when silent) */
        set_dirty = 0;
        settings_write();
    }
#if KS_DUAL
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
    return v->k[i] == I_NONE ? "" : item_sname(v->k[i]);
}
WASM_EXPORT("ksub_store") uint8_t *ksub_store(void) { return w_store; }
WASM_EXPORT("ksub_store_size") uint32_t ksub_store_size(void) { return sizeof w_store; }
WASM_EXPORT("ksub_store_gen") uint32_t ksub_store_gen(void) { return w_store_gen; }
WASM_EXPORT("ksub_patch_name") const char *ksub_patch_name(void) { return pname; }
#endif
