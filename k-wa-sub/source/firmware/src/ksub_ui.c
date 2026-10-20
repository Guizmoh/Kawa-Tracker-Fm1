/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: the eight pages, the panel, the presets, MIDI, app_main.
 *
 *   HOME 01 PLAY     EDIT 02 BASS     ENV 03 ENVELOPE  SEQ 04 FILTER
 *   LFO  05 LFO      FX   06 FX       ARP 07 PRESETS   GLO 08 SYSTEM
 *   the page's button again, or SEL: its next view (OSC A, OSC B, SUB, COMBINE...)
 *   SELECT: the row   ALGORITHM: the row's value   K1..K4: what the footer says   PRESETS: the next patch
 *   PLAY: HOLD (the notes stay)   REC: TAP tempo   OCT- / OCT+: octave (both: all notes off)   SAVE: save */
enum { PG_PLAY, PG_BASS, PG_ENV, PG_FILT, PG_LFO, PG_FX, PG_PRE, PG_SYS, NPAGE };
static const uint8_t PAGE_BTN[NPAGE] = {BTN_HOME, BTN_EDIT, BTN_ENV, BTN_SEQ, BTN_LFO, BTN_FX, BTN_ARP, BTN_GLO};
enum { LY_PLAY, LY_LIST, LY_BOX, LY_FILT, LY_LFO, LY_PRESET, LY_SAVE, LY_SYS };
enum { I_L1RATE = 200, I_L2RATE, G_MIDICH, G_BPM, G_CLOCK, G_OCT, G_CORES, G_CPU, G_FW, I_NONE = 255 };

typedef struct {
    uint8_t lay;
    const char *cat;
    uint8_t n, it[12], k[4];
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
    {LY_FILT, "FILTER / COLOUR", 3, {F_CUT, F_RES, F_DRV}, {F_CUT, F_RES, F_DRV, F_TYPE}, "FX"},
    {LY_LIST, "FILTER MOD", 6, {F_TYPE, F_ENV, F_KEY, F_VEL, F_CUT, F_RES}, {F_ENV, F_KEY, F_VEL, F_TYPE}, "FX"},
    {LY_LFO, "LFO 1 \xB7 WOBBLE", 8, {L1_SHP, I_L1RATE, L1_DEP, L1_DST, L1_SYNC, L1_TRIG, L1_PH, L1_SMO},
     {L1_SHP, I_L1RATE, L1_DEP, L1_DST}, "LFO"},
    {LY_LFO, "LFO 2 \xB7 MOTION", 8, {L2_SHP, I_L2RATE, L2_DEP, L2_DST, L2_SYNC, L2_TRIG, L2_PH, L2_SMO},
     {L2_SHP, I_L2RATE, L2_DEP, L2_DST}, "LFO"},
    {LY_LIST, "MOD MATRIX", 12, {M1_SRC, M1_DST, M1_AMT, M2_SRC, M2_DST, M2_AMT, M3_SRC, M3_DST, M3_AMT, M4_SRC, M4_DST, M4_AMT},
     {M1_AMT, M2_AMT, M3_AMT, M4_AMT}, "LFO"},
    FXV(0, DR_ON, DR_TYPE, DR_AMT, DR_MIX), FXV(1, CR_ON, CR_BITS, CR_RATE, CR_MIX), FXV(2, CH_ON, CH_RATE, CH_DEP, CH_MIX),
    {LY_LIST, "FX RACK", 5, {DL_ON, DL_TIME, DL_FB, DL_TONE, DL_MIX}, {DL_TIME, DL_FB, DL_TONE, DL_MIX}, "FX"},
    FXV(4, RV_ON, RV_SIZE, RV_DAMP, RV_MIX),
    {LY_LIST, "FX RACK", 3, {MS_GLUE, MS_SUB, MS_VOL}, {MS_GLUE, MS_SUB, MS_VOL, I_NONE}, "FX"},
    {LY_PRESET, "PRESET BANK A", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_PRESET, "PRESET BANK B", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_SAVE, "SAVE PATCH", 0, {0}, {I_NONE, I_NONE, I_NONE, I_NONE}, "PRESET"},
    {LY_SYS, "GLOBAL SETUP", 12, {G_MIDICH, V_MODE, V_GLIDE, V_BEND, V_VEL, G_BPM, G_CLOCK, G_OCT, P_STYLE, G_CORES, G_CPU, G_FW},
     {V_MODE, V_GLIDE, G_BPM, G_MIDICH}, "SYSTEM"},
};
static const uint8_t PAGE_V0[NPAGE + 1] = {0, 2, 6, 9, 11, 14, 20, 23, 24};   /* each page's first view */
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
} set = {2, 0, 0, 0xFF, 1400, 0, 0, 1};
static char pname[14];
static uint8_t page, vcur[NPAGE], cursor[24], pre_cur[2], save_slot, save_pos, hold;
static uint8_t key_note[27], latched[128], phys[128];
static uint32_t prev_keys, toast_ms, set_ms, last_turn_ms, tap_ms[4];
static uint8_t set_dirty, prev_dirty = 1;
static char toast_txt[24];
static int16_t prev_wave[224];
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

static void load_preset(uint32_t bank, uint32_t k)
{
    int8_t p[NP_MAX];
    uint32_t i;
    if (bank) {
        if (!up_valid(k)) { toast("EMPTY SLOT"); return; }
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
    *hi = id == G_MIDICH ? 16 : id == G_BPM ? 2400 : id == G_CLOCK || id == G_CORES ? 1 : id == G_OCT ? 6 : 0;
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
    return id == G_MIDICH ? "MIDI" : id == G_BPM ? "BPM" : id == G_CLOCK ? "CLOCK" : id == G_OCT ? "OCT" : id == G_CORES ? "CORES" : "";
}
static uint32_t ilog2q8(uint32_t x)             /* log2(x) in Q8, x >= 1 */
{
    uint32_t m = 31;
    while (!(x >> m)) m--;
    return (m << 8) | (m >= 8u ? (x >> (m - 8u)) & 255u : (x << (8u - m)) & 255u);
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
    case FM_LIST: case FM_DIV: str_cat(b, PD[rid].list[v]); break;
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
        }
    }
    pk = (32000 << 12) / pk;                     /* (a gain, Q12: no 64-bit division) */
    for (i = 0; i < 224; i++) prev_wave[i] = (int16_t)((w[i] * pk) >> 12);
}

/* ================================================================ drawing */
static void draw_head(const char *cat)
{
    char n[8], *p = n;
    g_rect(0, g_y0, 240, (int32_t)BAND_H, K_BG);
    if (!g_vis(0, 72)) return;
    g_text(&F_M, 8, 5, "K-WA SUB", K_LIME);
    p = fmt_u(p, (uint32_t)page + 1u, 2);
    if (page == PG_PLAY) str_cat(p, "/08");
    g_text_r(&F_XS, 232, 7, n, K_GREY);
    g_rect(6, 19, 228, 1, K_LINE);
    g_text(&F_S, 8, 24, cat, K_LIME);
}
static void draw_title(const char *t, const char *sub, uint16_t subc)
{
    g_text(&F_L, 8, 37, t, K_WHITE);
    if (sub) g_text(&F_XS, 8, 61, sub, subc);
}
static void draw_foot(const view_t *v, const char *left)
{
    char b[64], *p = b;
    uint32_t k, n = 4;
    if (!g_vis(222, 12)) return;
    if (left) str_cat(b, left);
    else
        for (;;) {                              /* "K1 FREQ · K2 RES · K3 DRIVE": as many as fit */
            p = b;
            *p = 0;
            for (k = 0; k < n; k++) {
                if (v->k[k] == I_NONE) continue;
                if (p != b) p = str_cat(p, " \xB7 ");
                *p++ = 'K'; *p++ = (char)('1' + k); *p++ = ' ';
                p = str_cat(p, item_sname(v->k[k]));
            }
            if (g_text_w(&F_XS, b) < 180 || n <= 1u) break;
            n--;
        }
    g_text(&F_XS, 8, 226, b, K_GREY);
    g_text_r(&F_XS, 232, 226, v->tag, K_CYAN);
}
static void draw_row(int32_t y, const char *lab, const char *val, int sel, uint16_t valc)
{
    if (!g_vis(y, 24)) return;
    g_box(6, y, 228, 24, sel ? K_LIMED : K_PANEL, sel ? K_LIME : K_LINE);
    g_text(&F_S, 14, y + 8, lab, K_WHITE);
    g_text_r(&F_S, 226, y + 8, val, sel ? K_LIME : valc);
}
static void draw_item_row(int32_t y, uint32_t id, int sel)
{
    char b[24];
    item_text(id, b);
    draw_row(y, item_name(id), b, sel, id == G_CPU || id == G_FW ? K_GREY : K_CYAN);
}
static void draw_rows(const view_t *v, int32_t y0, uint32_t vis)
{
    uint32_t c = cursor[vidx()], top = 0, i;
    if (v->n > vis) {
        top = c >= 2u ? c - 2u : 0u;
        if (top + vis > v->n) top = v->n - vis;
        if (g_vis(y0, (int32_t)vis * 29)) {     /* where we are in the list */
            int32_t h = (int32_t)vis * 29 - 5, bh = h * (int32_t)vis / v->n;
            g_rect(236, y0, 2, h, K_TRACK);
            g_rect(236, y0 + (h - bh) * (int32_t)top / (int32_t)(v->n - vis), 2, bh, K_GREY);
        }
    }
    for (i = 0; i < vis && top + i < v->n; i++) draw_item_row(y0 + (int32_t)i * 29, v->it[top + i], top + i == c);
}
static void draw_vbox(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t id, int sel, const char *lab)
{
    char b[24];
    if (!g_vis(y, h)) return;
    item_text(id, b);
    g_box(x, y, w, h, sel ? K_LIMED : K_PANEL, sel ? K_LIME : K_LINE);
    g_text(&F_XS, x + 7, y + 7, lab ? lab : item_name(id), sel ? K_LIME : K_GREY);
    if (h >= 44) g_text(&F_B, x + 7, y + 19, b, sel ? K_LIME : K_WHITE);
    else g_text(&F_M, x + 7, y + 17, b, sel ? K_LIME : K_WHITE);
    g_bar(x + 7, y + h - 11, w - 14, 5, item_frac(id), sel ? K_LIME : K_CYAN);
}
static void draw_wave(int32_t y, int32_t h, const int16_t *w, uint32_t n, uint16_t c)   /* n points over x 8..231 */
{
    uint32_t i;
    int32_t cy = y + h / 2, px = 8, py = cy - (w[0] * (h / 2 - 1)) / 32768;
    if (!g_vis(y - 1, h + 2)) return;
    for (i = 1; i < n; i++) {
        int32_t x = 8 + (int32_t)(i * 223u / (n - 1u)), yy = cy - (w[i] * (h / 2 - 1)) / 32768;
        g_line(px, py, x, yy, c);
        g_line(px, py + 1, x, yy + 1, g_mix(K_BG, c, 120));
        px = x;
        py = yy;
    }
}

static void draw_play(const view_t *v)
{
    char sub[40], *p = sub;
    int32_t y = 37, ysub, sy, sh, by, bh, i, split = -1, w = g_text_w(&F_L, pname);
    uint32_t c = cursor[vidx()];
    static int16_t live[224];
    if (w > 224) {                              /* two lines, cut at the last space that fits */
        char t[14];
        for (i = 0; pname[i]; i++) {
            if (pname[i] != ' ') continue;
            memcpy(t, pname, (uint32_t)i);
            t[i] = 0;
            if (g_text_w(&F_L, t) <= 224) split = i;
        }
    }
    if (split > 0) {
        char t[14];
        memcpy(t, pname, (uint32_t)split);
        t[split] = 0;
        g_text(&F_L, 8, y, t, K_WHITE);
        g_text(&F_L, 8, y + 22, pname + split + 1, K_WHITE);
        ysub = 83;
    } else {
        g_text(&F_L, 8, y, pname, K_WHITE);
        ysub = 61;
    }
    p = str_cat(p, L_STYLE[patch[P_STYLE] & 7]);
    p = str_cat(p, set.bank ? " / BANK B " : " / BANK A ");
    fmt_u(p, (uint32_t)set.idx + 1u, 2);
    g_text(&F_XS, 8, ysub, sub, K_GREY);
    p = sub;
    if (hold) p = str_cat(p, "HOLD \xB7 ");
    p = fmt_u(p, ks_bpm10 / 10u, 0);
    str_cat(p, " BPM");
    g_text_r(&F_XS, 232, ysub, sub, hold ? K_LIME : K_CYAN);
    sy = ysub + 12;
    sh = split > 0 ? 28 : 34;
    if (ks_sounding) {                          /* the last ~20 ms of sound, from a rising zero crossing */
        uint32_t w = scope_w - 512u, k, at = 0;
        for (k = 1; k < 288u; k++) if (scope[(w + k - 1u) & 511u] < 0 && scope[(w + k) & 511u] >= 0) { at = k; break; }
        for (k = 0; k < 224u; k++) live[k] = scope[(w + at + k) & 511u];
        draw_wave(sy, sh, live, 224, K_LIME);
    } else
        draw_wave(sy, sh, prev_wave, 224, K_LIME);
    by = sy + sh + 6;
    bh = (220 - by - 6) / 2;
    for (i = 0; i < 4; i++)
        draw_vbox(i & 1 ? 123 : 6, by + (i >> 1) * (bh + 6), 111, bh, v->it[i], (uint32_t)i == c,
                  v->it[i] == I_L1RATE ? "WOB RATE" : v->it[i] == L1_DEP ? "WOB DEPTH" : 0);
}

static void draw_lfo(const view_t *v, uint32_t k)
{
    const int8_t *p = patch + k * LFO_2;
    char t[24], sub[40], *s;
    int16_t w[112];
    uint32_t i, ph = lfo_ph_show[k];
    lfo_t l;
    s = str_cat(t, k ? "MOTION " : "WOBBLE ");
    item_text(k ? I_L2RATE : I_L1RATE, s);
    s = str_cat(sub, L_SHAPE[p[L1_SHP]]);
    s = str_cat(s, " \xB7 ");
    s = str_cat(s, L_DST[p[L1_DST]]);
    *s++ = ' ';
    item_text(L1_DEP + k * LFO_2, s);
    draw_title(t, sub, K_GREY);
    memset(&l, 0, sizeof l);
    l.sh = 20000; l.sh0 = -12000;
    for (i = 0; i < 112u; i++) w[i] = (int16_t)lfo_shape(&l, i * (0xFFFFFFFFu / 56u), (uint32_t)p[L1_SHP]);
    if (g_vis(70, 36)) {
        g_rect(8, 87, 224, 1, K_LINE);
        draw_wave(71, 34, w, 112, K_CYAN);
        {                                       /* where it is now */
            int32_t x = 8 + (int32_t)((ph >> 24) * 111u / 255u), y = 88 - (lfo[k].out * 16) / 32768;
            g_rect(x - 2, y - 2, 5, 5, K_LIME);
        }
    }
    draw_rows(v, 110, 4);
}

static void draw_presets(uint32_t bank)
{
    uint32_t c = pre_cur[bank], n = bank ? NUSER : NFACTORY, top, i;
    char sub[32], *p;
    p = str_cat(sub, bank ? "USER \xB7 " : "FACTORY \xB7 ");
    p = fmt_u(p, n, 0);
    str_cat(p, bank ? " SLOTS" : " PATCHES");
    draw_title("SELECT PATCH", sub, K_GREY);
    top = c >= 2u ? c - 2u : 0u;
    if (top + 5u > n) top = n - 5u;
    for (i = 0; i < 5u; i++) {
        uint32_t k = top + i;
        int32_t y = 74 + (int32_t)i * 29;
        char b[24];
        const char *nm = bank ? (up_valid(k) ? ustore[k].name : "- EMPTY -") : FACTORY[k].name;
        p = fmt_u(b, k + 1u, 2);
        *p++ = ' '; *p++ = ' ';
        str_cat(p, nm);
        draw_row(y, b, "", k == c, K_CYAN);
        if (set.bank == bank && set.idx == k && g_vis(y, 24)) {   /* the one playing */
            int32_t j;
            for (j = 0; j < 4; j++) g_rect(220 + j, y + 8 + j, 1, 8 - 2 * j, k == c ? K_LIME : K_CYAN);
        }
    }
}
static void draw_save(void)
{
    char sub[32], *p;
    int32_t x = 8, i;
    p = str_cat(sub, "TO USER SLOT B");
    fmt_u(p, save_slot + 1u, 2);
    draw_title(pname, sub, K_ORANGE);
    for (i = 0; i < (int32_t)save_pos; i++) { char ch[2] = {pname[i], 0}; x += g_text_w(&F_L, ch); }
    if (g_vis(58, 2)) { char ch[2] = {pname[save_pos] ? pname[save_pos] : ' ', 0}; g_rect(x, 58, g_text_w(&F_L, ch), 2, K_LIME); }
    p = sub;
    if (up_valid(save_slot)) str_cat(p, ustore[save_slot].name);
    else str_cat(p, "- EMPTY -");
    {
        char b[8];
        p = str_cat(b, "B");
        fmt_u(p, save_slot + 1u, 2);
        draw_row(74, "SLOT", b, 1, K_CYAN);
    }
    draw_row(103, "NOW IN SLOT", sub, 0, K_GREY);
    draw_row(132, "STYLE", L_STYLE[patch[P_STYLE] & 7], 0, K_CYAN);
    if (g_vis(170, 30)) {
        g_text(&F_XS, 8, 172, "K2 LETTER POSITION \xB7 K3 LETTER", K_GREY);
        g_text(&F_XS, 8, 184, "K4 STYLE \xB7 PRESS SAVE AGAIN TO WRITE", K_LIME);
    }
}

static void draw_all(void)
{
    const view_t *v = view();
    uint32_t vi = vidx();
    char b[24];
    draw_head(v->cat);
    switch (v->lay) {
    case LY_PLAY: draw_play(v); draw_foot(v, vi == 0u ? "KNOBS = LIVE MACROS" : 0); break;
    case LY_LIST:
        if (page == PG_BASS && vi <= 3u) {
            uint32_t e = (uint32_t)patch[vi == 2u ? A_ENG : B_ENG];
            draw_title(ENG_TITLE[e], ENG_SUB[e], K_GREY);
        } else if (page == PG_BASS && vi == 4u) draw_title("SUB / NOISE", "LOW END CHARACTER", K_GREY);
        else if (page == PG_BASS) draw_title(patch[X_MODE] == 0 ? "A + B" : patch[X_MODE] == 1 ? "A x B  RING" : "B > A  X-MOD", "TWO ENGINES, ONE BASS", K_GREY);
        else if (page == PG_FILT) draw_title(FT_TITLE[patch[F_TYPE]], "ENVELOPE \xB7 KEY \xB7 VELOCITY", K_GREY);
        else if (page == PG_LFO) draw_title("ROUTING", "4 SLOTS \xB7 SOURCE > DEST", K_GREY);
        else if (page == PG_FX) {
            uint32_t f = vi - PAGE_V0[PG_FX];
            const char *st = f < 5u ? (patch[v->it[0]] ? "ON" : "OFF") : "ALWAYS ON";
            char *p = str_cat(b, f == 0u ? L_DRT[patch[DR_TYPE]] : f == 3u ? L_DIV[patch[DL_TIME]] : "");
            if (p != b) p = str_cat(p, " \xB7 ");
            str_cat(p, st);
            draw_title(FX_TITLE[f], b, K_ORANGE);
        }
        draw_rows(v, 74, 5);
        draw_foot(v, 0);
        break;
    case LY_BOX: {
        uint32_t i, c = cursor[vi];
        draw_title(page == PG_ENV && vi == PAGE_V0[PG_ENV] ? "ADSR CONTROL" : vi == PAGE_V0[PG_ENV] + 1u ? "FILTER ADSR" : "PITCH / MOD",
                   vi == PAGE_V0[PG_ENV] + 2u ? L_DST[patch[ME_DST]] : 0, K_GREY);
        for (i = 0; i < 4u; i++) draw_vbox(i & 1u ? 123 : 6, 74 + (int32_t)(i >> 1) * 70, 111, 64, v->it[i], i == c, 0);
        draw_foot(v, 0);
        break;
    }
    case LY_FILT: {
        uint32_t c = cursor[vi];
        char inf[48], *p;
        draw_title(FT_TITLE[patch[F_TYPE]], FT_SUB[patch[F_TYPE]], K_GREY);
        item_text(F_CUT, b);
        draw_row(74, "CUTOFF", b, c == 0u, K_CYAN);
        if (g_vis(104, 8)) g_bar(6, 104, 228, 8, item_frac(F_CUT), K_LIME);
        draw_vbox(6, 118, 111, 62, F_RES, c == 1u, "RESONANCE");
        draw_vbox(123, 118, 111, 62, F_DRV, c == 2u, "DRIVE");
        p = str_cat(inf, "ENV ");
        item_text(F_ENV, p); p += str_len(p);
        p = str_cat(p, " \xB7 KEY ");
        item_text(F_KEY, p); p += str_len(p);
        p = str_cat(p, " \xB7 LFO1 > ");
        str_cat(p, L_DST[patch[L1_DST]]);
        g_text(&F_XS, 8, 190, inf, K_GREY);
        draw_foot(v, 0);
        break;
    }
    case LY_LFO: draw_lfo(v, vi - PAGE_V0[PG_LFO]); draw_foot(v, 0); break;
    case LY_PRESET: draw_presets(vi - PAGE_V0[PG_PRE]); draw_foot(v, "K1 SCROLL \xB7 K3 LOAD"); break;
    case LY_SAVE: draw_save(); draw_foot(v, "K1 SLOT \xB7 SAVE = WRITE"); break;
    case LY_SYS:
        draw_title("DEVICE SETUP", 0, K_GREY);
        draw_rows(v, 74, 4);
        if (g_vis(192, 14)) {
            g_box(6, 192, 228, 14, K_LIMED, K_LIME);
            g_text_c(&F_XS, 120, 195, "DNB / DUB / DUBSTEP / TECHNO EDITION", K_LIME);
        }
        draw_foot(v, "SELECT ROW \xB7 ALGO VALUE");
        break;
    }
    if (toast_ms && g_vis(196, 22)) {
        g_box(30, 196, 180, 22, K_PANEL, K_LIME);
        g_text_c(&F_S, 120, 202, toast_txt, K_WHITE);
    }
}

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

static void edit_item(uint32_t id, int32_t d)
{
    int32_t lo, hi, v = item_get(id), step = 1;
    if (id == I_NONE || id == G_CPU || id == G_FW) return;
    item_range(id, &lo, &hi);
    if (hi - lo > 40 && fm1_ms - last_turn_ms < 45u) step = id == G_BPM ? 10 : 3;   /* fast: bigger steps */
    last_turn_ms = fm1_ms;
    item_set(id, v + d * step);
}
static void preset_step(int32_t d)
{
    uint32_t bank = set.bank, n = bank ? NUSER : NFACTORY, k = set.idx, tries;
    d %= (int32_t)n;
    for (tries = 0; tries < n; tries++) {
        k = (uint32_t)((int32_t)k + d + (int32_t)n) % n;
        d = d < 0 ? -1 : 1;
        if (!bank || up_valid(k)) break;
    }
    load_preset(bank, k);
    pre_cur[bank] = (uint8_t)k;
    toast(pname);
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
    if (e == ENC_PRESETS) { preset_step(d); return; }
    if (v->lay == LY_PRESET) {
        uint32_t bank = vi - PAGE_V0[PG_PRE], n = bank ? NUSER : NFACTORY;
        if (e == ENC_SELECT || e == ENC_KNOB1) pre_cur[bank] = (uint8_t)clampi(pre_cur[bank] + d, 0, (int32_t)n - 1);
        else if (e == ENC_KNOB3 || e == ENC_ALGORITHM) load_preset(bank, pre_cur[bank]);
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
        } else if (e == ENC_KNOB4) patch[P_STYLE] = (int8_t)clampi(patch[P_STYLE] + d, 0, 7);
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
    for (b = 0; b < 14u; b++) if (pr & (1u << b)) on_button(b);
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
            case 74: patch[F_CUT] = (int8_t)d2; break;
            case 71: patch[F_RES] = (int8_t)d2; break;
            case 76: patch[L1_DIV] = (int8_t)(d2 >> 3); break;
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
    if (settings_load(&set, sizeof set) || set.ver != 2u) {
        set.ver = 2; set.midich = 0; set.clock = 0; set.oct = 0xFF; set.bpm10 = 1400; set.bank = 0; set.idx = 0; set.cores = 1;
    }
    set.cores = set.cores ? 1u : 0u;
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
    g_force = 1;
}

static void app_step(void)
{
    ui_input();
    if (toast_ms && fm1_ms - toast_ms > 1400u) toast_ms = 0;
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
