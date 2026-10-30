/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the pages, the panel, the main loop.
 *
 *   HOME   TRACKS     the tracker: 16 rows, six tracks, the track under the cursor wide (HOME held: help; HOME + PLAY: panic)
 *   SEQ    GRID       the track's 16 steps on the 16 white keys, as a Pocket Operator (OCT-/OCT+: 16, 32, 64 steps)
 *   EDIT   SOUND      the track's engine and sound; EDIT again: SOUND 2, (FM: OPERATOR, OP ENV), EFFECT, TABLE
 *   LFO    LFO        the track's LFO; again: MODULATION
 *   ENV    ENVELOPE   the ADSR, drawn in the knobs' colours; ENV again: MIX (the tracks, the master)
 *   SCL    SCALE      root (a key), scale, lock, the cursor's step; SCL again: MAGIC (16 tools on the white keys)
 *   GLO    SONG       tempo, swing; the song list; GLO again: GROOVE, then OPTIONS (theme, language, clock, font, metronome)
 *   SAVE   PROJECTS   four slots; SAVE held: the tools (OPTIONS, UNDO, REDO, CLEAR, COPY, PASTE, RANDOM)
 *   FX     SENDS      a tap: each track's DRIVE, ECHO, REVERB; again: the live effects; held: them while it is held
 *
 * One knob, one colour: KNOB 1 red, 2 yellow, 3 green, 4 blue on every page (the cards, the bottom line,
 * the emulator's rings). On EDIT, LFO, ENV, FX and GLO, SELECT turns the pages; elsewhere the row (or the cursor); ALGORITHM
 * the track (the entry, the step); PRESETS the pattern (the factory sound on SOUND, the song entry), PLAY, REC (keys write at the cursor; while playing, on the beat), ARP clears
 * the cell, OCT-/OCT+ the keyboard's octave. */

static const char *const PAGE_NAMES[LANG_N][PG_N] = {
    {"PISTES", "GRILLE", "SON", "ENVELOPPE", "MIX", "MAGIE", "GAMME", "CHAINE", "PROJETS", "OPTIONS", "EFFETS", "AIDE", "",
     "GROOVE", "ENVOIS"},
    {"TRACKS", "GRID", "SOUND", "ENVELOPE", "MIX", "MAGIC", "SCALE", "SONG", "PROJECTS", "OPTIONS", "EFFECTS", "HELP", "",
     "GROOVE", "SENDS"},
    {"PISTAS", "REJILLA", "SONIDO", "ENVOLVENTE", "MEZCLA", "MAGIA", "ESCALA", "CANCION", "PROYECTOS", "OPCIONES", "EFECTOS",
     "AYUDA", "", "GROOVE", "ENVIOS"}};
#define PAGE_NAME PAGE_NAMES[lang]
static uint8_t page = PG_BOOT, page_back = PG_TRACK, help_pg = PG_TRACK;
static uint8_t cur_row, cur_trk, ui_pat, rec, snd_page, grid_page, fx_bank, fx_sticky, mg_scope, follow = 1, fm_op = 1;
static uint8_t rec_held, rec_turned;
static int8_t oct_ofs;
static uint8_t mg_inten = 8, mg_seed = 1, song_cur;
static uint8_t arp_held, arp_turned;
static uint8_t brush_vol[NTRK] = {12, 12, 12, 12, 12, 12}, brush_fx[NTRK], brush_fxv[NTRK];
static uint32_t fx_down_ms, home_down_ms, save_down_ms, boot_ms, last_input_ms;
static uint8_t home_held, save_held, save_did, fx_down;
static void preset_commit(void);
static uint8_t pre_cand = 0xFF, pre_cand_t;     /* an edited sound: the factory sound PRESETS is showing, OCT+ loads it */
static uint32_t pre_cand_ms;
static uint8_t song_empty;                      /* nothing written anywhere (the first-start hint) */
static uint8_t home_did;
static uint32_t boot_eat;                       /* buttons that closed the boot screen: their release does nothing */
static uint8_t fx_back = PG_TRACK;              /* where FX goes back to (its own: PROJECTS and the help keep page_back) */                        /* HOME + PLAY (panic) happened: its release does nothing */
static uint8_t tools_open, tools_sel;              /* SAVE held: the tools (OPTIONS, UNDO, REDO, CLEAR, COPY, PASTE, RANDOM) */
#define HOLD_HOME_MS 450u                       /* a hold that opens something: the yellow bar fills to this */
#define HOLD_SAVE_MS 450u
#define HOLD_FX_MS 250u
#define TOOL_N 7u
static const char *const TOOL_NAMES[LANG_N][TOOL_N] = {
    {"OPTIONS", "ANNULER", "REFAIRE", "EFFACER", "COPIER", "COLLER", "HASARD"},
    {"OPTIONS", "UNDO", "REDO", "CLEAR", "COPY", "PASTE", "RANDOM"},
    {"OPCIONES", "DESHACER", "REHACER", "BORRAR", "COPIAR", "PEGAR", "AZAR"}};
static char toast_s[24];
static uint32_t toast_ms;
static uint32_t knob_ms[4];
static uint32_t prev_keys;
static uint8_t key_note[27], key_trk[27];
static int8_t grid_held = -1;                   /* a step held on the grid (its knobs edit it) */
static uint8_t grid_edit;                       /* the held step was edited (its release keeps it) */
static uint8_t wv_peak[224], wv_src = 0xFF;     /* the sample's outline (SOUND page), made when it changes */
static uint8_t names_ok[NSLOT];
static char slot_name[NSLOT][16];
static uint32_t ui_frame_ms;
static int8_t arc_x[21], arc_y[21];             /* the knob gauge's dots */
static char knob_lab[4][24];                    /* what each knob does on this page (the emulator shows it) */
static char help_lab[4][24];
#define MASTER NTRK                             /* MIX: the strip after the tracks */

static struct { uint8_t lang, theme; int8_t oct; uint8_t clock, font, metro, mvol; } set;   /* kept in the settings sector (0.2 kept
                                                                                          * the first four bytes, 0.4 five) */
static uint8_t set_dirty;
static uint32_t set_ms;
static void set_changed(void)
{
    set.lang = lang;
    set.theme = theme;
    set.oct = oct_ofs;
    set.font = g_font;
    set_dirty = 1;
    set_ms = fm1_ms;
    g_force = 1;
}
static void set_lang(uint32_t l)
{
    lang = (uint8_t)(l % LANG_N);
    set_changed();
}

static void toast(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 23u) { toast_s[i] = s[i]; i++; }
    toast_s[i] = 0;
    toast_ms = fm1_ms;
}

static pat_t *P(void) { return &prj.pat[ui_pat]; }
static uint32_t plen(void) { uint32_t l = P()->len; return l && l <= NROW ? l : 16u; }
static uint32_t plen_of(uint32_t p) { uint32_t l = prj.pat[p].len; return l && l <= NROW ? l : 16u; }
/* the grid's pages of 16 steps: the pattern's, then (64 steps) the next pattern's, which this one goes on into */
static uint32_t grid_pages(void)
{
    uint32_t n = (plen() + 15u) / 16u;
    if (pat_linked(ui_pat)) n += (plen_of(ui_pat + 1u) + 15u) / 16u;
    return n > 4u ? 4u : n;
}
static uint32_t gpat(uint32_t pg)               /* the page's pattern (a page past the end: the first) */
{
    if (pg >= grid_pages()) pg = 0;
    return pg < (plen() + 15u) / 16u ? ui_pat : ui_pat + 1u;
}
static uint32_t grow(uint32_t pg)               /* its first row */
{
    uint32_t h = (plen() + 15u) / 16u;
    if (pg >= grid_pages()) pg = 0;
    return (pg < h ? pg : pg - h) * 16u;
}
static uint32_t trk(void) { return cur_trk < NTRK ? cur_trk : NTRK - 1u; }   /* the track (MIX may be on the master) */
static cell_t *CUR(void) { return &P()->c[cur_row][trk()]; }
static uint32_t ENG(uint32_t t) { return prj.snd[t].engine; }

/* ---- text */
static const char *drum_code(uint32_t t, uint32_t d, int wide)   /* a drum of the track's kit, as the tracker shows it */
{
    uint32_t k = (uint32_t)clampi(prj.snd[t].p[P_KIT], 0, NKIT - 1);
    return wide ? KIT_CODE3[k][d & 15u] : KIT_CODE2[k][d & 15u];
}
static void note_text(uint32_t t, uint32_t note, char *s, int wide)
{
    uint32_t nm = note_mode(t);
    if (note == NOTE_NONE) { str_cat(s, wide ? " . " : " ."); return; }
    if (note == NOTE_OFF) { str_cat(s, wide ? "OFF" : "--"); return; }
    if (nm == NM_DRUM) { str_cat(s, drum_code(t, cell_idx(note) & 15u, wide)); if (cell_idx(note) >= 16u) { while (*s) s++; s[-1] = '+'; } return; }   /* (a tuned-up drum: its code ends in +) */
    if (nm == NM_SLICE) { char *p = s; while (*p) p++; if (wide) *p++ = 'S'; fmt_u(p, (cell_idx(note) & 15u) + 1u, 2); if (cell_idx(note) >= 16u) { while (*p) p++; p[-1] = '+'; } return; }
    if (wide) {
        s = str_cat(s, NOTE_NAME[note % 12u]);
        fmt_u(s, note / 12u ? note / 12u - 1u : 0u, 1);
    } else if (NOTE_NAME[note % 12u][1] == '#')
        str_cat(s, NOTE_NAME[note % 12u]);      /* narrow: C# ... or the octave of a natural note: A2 */
    else {
        while (*s) s++;
        s[0] = NOTE_NAME[note % 12u][0];
        fmt_u(s + 1, note / 12u ? note / 12u - 1u : 0u, 1);
    }
}
static void fx_text(uint32_t t, uint32_t fx, uint32_t fxv, char *s)   /* a trick's value as the cell shows it */
{
    int32_t v = (int8_t)fxv;
    s[0] = 0;
    switch (fx) {
    case FX_NONE: case FX_END: break;
    case FX_ARP: str_cat(s, CHORDS[fxv % 14u].name[lang]); break;
    case FX_ROLL: *s++ = 'X'; fmt_u(s, ROLL_N[fxv < 9u ? fxv : 8u], 0); break;
    case FX_BEND: case FX_PITCH: case FX_VSL: case FX_PAN: fmt_s(s, v); break;
    case FX_CHANCE: s = fmt_u(s, fxv * 10u, 0); str_cat(s, "%"); break;
    case FX_REV: str_cat(s, "<<"); break;
    case FX_HOP: fmt_u(s, (fxv & 31u) + 1u, 0); break;     /* the row it goes to */
    case FX_SHAPE: {
        uint32_t e = ENG(t), n = e == E_PULSE ? 5u : e == E_WAVE ? 8u : e == E_NOISE ? 2u : 0u, i;
        const char *const *l = e == E_PULSE ? L_DUTY : e == E_WAVE ? L_WAVE : L_NOISE;
        const char *w;
        if (!n) { fmt_u(s, fxv, 0); break; }
        w = l[lang * n + (e == E_PULSE ? (fxv < 4u ? fxv : 4u) : e == E_NOISE ? (fxv ? 1u : 0u) : fxv % n)];
                                                /* (what plays: PULSE 4 and over is PWM, NOISE any but 0 the metal) */
        for (i = 0; i < 3u && w[i]; i++) s[i] = w[i];
        s[i] = 0;
        break;
    }
    default: fmt_u(s, fxv, 0); break;
    }
}
static void fxv_text(uint32_t t, const cell_t *c, char *s) { fx_text(t, c->fx, c->fxv, s); }
static void trk_name(uint32_t t, char *s)      /* "1 PULSE" */
{
    s[0] = (char)('1' + t);
    s[1] = ' ';
    s[2] = 0;
    str_cat(s, ENGINE_NAME[ENG(t)]);
}
static void src_text(int32_t v, char *s)        /* a SAMPLE's source: a break, or a slot of the user's */
{
    const char *n;
    s[0] = 0;
    if (v < (int32_t)BRK_N) { str_cat(s, BRK_NAME[v < 0 ? 0 : v]); return; }
    n = usr_name((uint32_t)v - BRK_N);
    if (n) { str_cat(s, n); return; }
    str_cat(s, TR("MON ", "MY ", "MI "));
    fmt_u(s + str_len(s), (uint32_t)v - BRK_N + 1u, 0);
    str_cat(s, " -");
}
static const uint16_t ENV_MS[16] = {0, 2, 5, 10, 20, 35, 50, 80, 120, 180, 250, 350, 500, 800, 1200, 2000};
static void param_text(const pdesc_t *d, int32_t v, char *s, int dec)
{
    s[0] = 0;
    if (d->kind == K_LIST || d->kind == K_NAMES) str_cat(s, pd_value(d, v));
    else if (d->kind == K_SIGN) fmt_s(s, v);
    else if (d->kind == K_SRC) src_text(v, s);
    else if (d->kind == K_MS) {
        uint32_t ms = ENV_MS[v & 15] * (dec ? 2u : 1u);
        if (ms >= 1000u) { char *p = fmt_u(s, ms / 1000u, 0); *p++ = '.'; p = fmt_u(p, ms / 100u % 10u, 0); str_cat(p, "S"); }
        else str_cat(fmt_u(s, ms, 0), "MS");
    } else if (d->kind == K_PAN) {
        if (v == 0) str_cat(s, "C");
        else { s[0] = v < 0 ? TR('G', 'L', 'I') : TR('D', 'R', 'D'); fmt_u(s + 1, (uint32_t)(v < 0 ? -v : v), 0); }
    } else fmt_u(s, (uint32_t)v, 0);
}
/* a parameter's name: the effect's A and B take the name of what they do */
static const char *param_name(uint32_t t, uint32_t k)
{
    const int8_t *p = prj.snd[t].p;
    if (k == P_FXA) return FXA_NAMES[lang][clampi(p[P_FXT], 0, FXT_N - 1)];
    if (k == P_FXB) return FXB_NAMES[lang][clampi(p[P_FXT], 0, FXT_N - 1)];
    return PDESC[ENG(t)][k].name[lang];
}

/* ---- the panel's LEDs: 0 off, 1 the glow, 2 lit */
static uint8_t led_want[41];
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

static int quiet(void)                          /* nothing sounds (the flash can be erased) */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)                  /* (a KIT or slice track sounds through its drum or sample voices: its */
        if ((note_mode(t) == NM_NOTE && (tk[t].stage || tk[t].amp)) ||   /* envelope may hold, nothing is heard) */
            drum[t][0].on || drum[t][1].on || smp[t][0].on || smp[t][1].on)
            return 0;
    return !fmn[0].live && !fmn[1].live;
}

/* ================================================================ drawing */
#define ROW_Y0 32
#define ROW_H 12
#define GUT_W 16
#define FOC_W 92
#define NAR_W 25
#define LIVE_X 233
static int32_t lane_x(uint32_t t)
{
    int32_t x = GUT_W;
    uint32_t k;
    for (k = 0; k < t; k++)
        x += k == trk() ? FOC_W : NAR_W;
    return x;
}
static int32_t lane_w(uint32_t t) { return t == trk() ? FOC_W : NAR_W; }

/* ---- the dog: 16 inks; the headband takes the page's colour (the live effect's when one plays), the
 * cups the track's */
static void dog_pal(uint16_t *pal, uint16_t band, uint16_t cups)
{
    pal[0] = 0;
    pal[1] = RGB(26, 18, 32);                   /* outline */
    pal[2] = RGB(70, 46, 44);                   /* dark fur: the saddle, the ears' edge */
    pal[3] = RGB(204, 124, 62);                 /* tan */
    pal[4] = RGB(236, 172, 102);                /* light tan */
    pal[5] = RGB(250, 218, 166);                /* cream: the muzzle */
    pal[6] = RGB(255, 184, 48);                 /* amber */
    pal[7] = RGB(30, 26, 40);                   /* nose */
    pal[8] = RGB(14, 10, 14);                   /* pupil */
    pal[9] = RGB(246, 104, 136);                /* tongue */
    pal[10] = band;
    pal[11] = cups;
    pal[12] = g_mix(cups, RGB(0, 0, 0), 110);   /* the cups' rim */
    pal[13] = RGB(255, 255, 255);               /* highlight */
    pal[14] = RGB(226, 140, 120);               /* inner ear */
    pal[15] = RGB(170, 160, 230);               /* z z z */
}
static void draw_mascot(int32_t x, int32_t y)
{
    uint16_t pal[16];
    uint32_t spr = DOG_IDLE, on = lfx_on, flip = 0, hop = 0;
    uint16_t band = PAGE_COL[page < PG_N ? page : 0];
    if (sq_play && (sq_row & 3u) == 0u && (sq_frac >> 24) < 90u) hop = 1;   /* a nod on the beat */
    if ((fm1_ms % 3400u) < 150u) spr = DOG_BLINK;
    if (rec) band = C_REC;
    if (on) {
        spr = DOG_WILD;
        band = LBANK_COL[(on & 0xFFFF0000u) ? 1 : 0];
        if (on & ((1u << LF_SCR) | (1u << LF_SCRF))) flip = (fm1_ms / 90u) & 1u;
        if (on & ((1u << LF_TAPE) | (1u << LF_FREEZE))) spr = DOG_SLEEP;
    }
    if (!sq_play && fm1_ms - last_input_ms > 20000u) spr = DOG_SLEEP;
    dog_pal(pal, band, TRK_COL[trk()]);
    g_dog(x, y + (int32_t)hop, DOG16[spr], 16, 16, pal, 1, flip);
}

static const char *lfx_first(uint32_t on, uint32_t *id_out)   /* the first live effect sounding */
{
    uint32_t id = 0;
    while (id < LF_N && !(on >> id & 1u)) id++;
    *id_out = id;
    return id < LF_N ? LFX[id].name[lang] : "";
}

static uint32_t bpm_now(void)                   /* the tempo playing: the MIDI clock in's, else the song's */
{
    return clk_in && clk_in_tick ? (1764000u + clk_in_tick / 2u) / clk_in_tick : prj.bpm;   /* (44100 x 60 x 16 / 24) */
}
static void draw_top(void)
{
    char s[24];
    int32_t x, w;
    uint32_t i, beat = sq_play ? (sq_row & 15u) / 4u : 4u, on = lfx_on, pg = page < PG_N ? page : 0u;
    uint16_t pc = PAGE_COL[pg];
    if (!g_vis(0, 20)) return;
    g_rect(0, 0, 240, 20, C_BG);
    g_rect(0, 19, 240, 1, C_RULE);
    draw_mascot(2, 2);
    w = g_text_w(PAGE_NAME[pg]) + 10;
    g_rrect(21, 3, w, 14, pc);
    g_text(26, 3, PAGE_NAME[pg], C_INK, T_CROP | T_BOLD);
    x = 21 + w + 5;
    if (on && page != PG_FX) {                  /* a live effect plays: everyone sees which */
        uint32_t id, n = 0, k;
        const char *nm = lfx_first(on, &id);
        uint16_t bc = LBANK_COL[id >= 16u];
        for (k = 0; k < LF_N; k++) n += on >> k & 1u;
        s[0] = 0;
        str_cat(s, nm);
        if (n > 1u) { str_cat(s, "+"); fmt_u(s + str_len(s), n - 1u, 0); }
        g_rrect(x, 3, 158 - x, 14, ((fm1_ms / 120u) & 1u) ? bc : g_mix(bc, C_BG, 60));
        g_icon(x + 3, 6, LFX[id].icon, C_INK, 1);
        g_text(x + 13, 3, s, C_INK, T_CROP);
    } else if (pg == PG_TRACK || pg == PG_GRID || pg == PG_MAGIC || pg == PG_FX || pg == PG_SOUND || pg == PG_ENV) {
        s[0] = 'P';
        fmt_u(s + 1, ui_pat + 1u, 2);
        if (pat_linked(ui_pat)) str_cat(s, "+");        /* 64 steps: it goes on into the next pattern (P01+, then +P02) */
        else if (ui_pat && pat_linked(ui_pat - 1u)) { s[0] = '+'; s[1] = 'P'; fmt_u(s + 2, ui_pat + 1u, 2); }
        x = g_text(x, 4, s, C_TXT, T_CROP | T_BOLD);
        if (sq_play && sq_pat != ui_pat) {      /* another one plays */
            s[0] = '>';
            fmt_u(s + 1, sq_pat + 1u, 2);
            x = g_text(x + 4, 4, s, C_FAINT, T_CROP);
        } else if (sq_next < NPAT && sq_play) { /* the next one, at the end of this one */
            s[0] = '>';
            fmt_u(s + 1, sq_next + 1u, 2);
            x = g_text(x + 4, 4, s, C_GOLD, T_CROP);
        }
    }
    if (prj.mode && !(on && page != PG_FX)) {
        int32_t cx;
        s[0] = 0;
        str_cat(fmt_u(s, (uint32_t)sq_songpos + 1u, 0), "/");
        fmt_u(s + str_len(s), prj.song_len, 0);
        cx = 172 - g_text_w(s);
        if (cx - 10 >= x + 4) {                 /* (only where it fits: never over the pattern's label) */
            g_icon(cx - 10, 6, ICO_CHAIN, C_MID, 1);
            g_text(cx, 4, s, C_MID, T_CROP);
        }
    }
    fmt_u(s, bpm_now(), 0);
    g_text(203 - g_text_w(s), 4, s, clk_in ? C_GO : C_TXT, T_CROP | T_BOLD);   /* (green: the MIDI clock in's) */
    g_icon(207, 6, sq_play ? ICO_PLAY : ICO_STOP, sq_play ? C_GO : C_FAINT, 1);
    if (rec) g_icon(215, 6, ICO_REC, C_REC, 1);
    for (i = 0; i < 4u; i++)
        g_rect(226 + (int32_t)i * 3, 14, 2, 3, i == beat ? C_GOLD : C_RULE);
}

/* the bottom line: what the four knobs do, each in its colour. name: the long name (the emulator, the help) */
#define HINT_W 56
static void hint_beta(void)                     /* the bottom line's end: this is a beta */
{
    if (!g_vis(224, 16)) return;
    g_rect(4 * HINT_W, 224, 240 - 4 * HINT_W, 16, C_REC);
    g_icon(4 * HINT_W + 4, 228, ICO_BETA, C_WHITE, 1);
}
static void hint(uint32_t k, const char *name, const char *label, const char *val)
{
    int32_t x = (int32_t)k * HINT_W;
    uint16_t c = KNOB_COL[k];
    uint32_t hot = fm1_ms - knob_ms[k] < 700u, i = 0;
    char *d = knob_lab[k];
    d[0] = 0;
    if (name && name[0]) {
        while (name[i] && i < 14u) { d[i] = name[i]; i++; }
        d[i] = 0;
        if (val && val[0]) { str_cat(d, "\n"); str_cat(d, val); d[23] = 0; }   /* (two lines on the page) */
    }
    if (!g_vis(224, 16)) return;
    if (k == 3u) hint_beta();
    g_rect(x, 224, HINT_W, 16, hot ? g_mix(C_BG2, c, 90) : C_BG2);
    g_rect(x, 224, 3, 16, c);
    g_rect(x + HINT_W - 1, 224, 1, 16, C_BG);
    if (!name || !name[0]) return;
    x += 5;
    if (label && label[0])
        x = g_text(x, 226, label, hot ? C_WHITE : c, T_CROP) + 2;
    if (val) {                                  /* (cut to its tile: the next one, or BETA, stays clear) */
        char v[12];
        int32_t room = ((int32_t)k + 1) * HINT_W - 2 - x;
        uint32_t n = room > 0 ? (uint32_t)room / 8u : 0u;
        for (i = 0; i < n && i < 11u && val[i]; i++) v[i] = val[i];
        v[i] = 0;
        g_text(x, 226, v, hot ? C_WHITE : C_TXT, T_CROP | (label && label[0] ? 0 : T_BOLD));
    }
}
static void hint_none(uint32_t k) { hint(k, "", "", ""); }

static void draw_toast(void)
{
    int32_t w;
    if (!toast_s[0] || fm1_ms - toast_ms > 1100u || !g_vis(104, 30)) return;
    w = g_text_w(toast_s) + 24;
    g_rrect(120 - w / 2, 106, w, 26, C_GOLD);
    g_rrect(120 - w / 2 + 1, 107, w - 2, 24, C_BG2);
    g_text_c(120, 112, toast_s, C_GOLD, T_CROP | T_BOLD);
}

/* a knob card: its colour, its name, a gauge, its value */
static void card(uint32_t i, int32_t x, int32_t y, int32_t h, const char *name, const char *val, int32_t num, int32_t den)
{
    uint16_t c = KNOB_COL[i];
    uint32_t j, n, hot = fm1_ms - knob_ms[i] < 700u;
    if (!g_vis(y, h)) return;
    g_rrect(x, y, 58, h, hot ? g_mix(C_BG2, c, 70) : C_BG2);
    g_rect(x + 1, y, 56, 3, c);
    if (!name || !name[0]) { g_rect(x + 1, y, 56, 3, C_RULE); return; }
    g_text_c(x + 29, y + 5, name, c, T_CROP);
    if (h >= 70) {
        n = den > 0 ? (uint32_t)(num * 20 / den) : 0u;
        for (j = 0; j <= 20u; j++)
            g_rect(x + 28 + arc_x[j], y + 44 + arc_y[j], 2, 2, j <= n ? c : C_RULE);
        g_text_c(x + 29, y + 38, val, C_WHITE, T_CROP | T_BOLD);
        g_text_c(x + 29, y + h - 15, i == 0u ? "1" : i == 1u ? "2" : i == 2u ? "3" : "4", g_mix(C_BG2, c, 120), T_CROP);
    } else {
        g_text_c(x + 29, y + 19, val, C_WHITE, T_CROP | T_BOLD);
        g_rect(x + 5, y + h - 6, 48, 2, C_RULE);
        if (den > 0) g_rect(x + 5, y + h - 6, 48 * num / den, 2, c);
    }
}

/* ---- TRACKS */
static void draw_lane_head(void)
{
    uint32_t t;
    char s[16];
    if (!g_vis(20, 12)) return;
    g_rect(0, 20, 240, 12, C_BG);
    for (t = 0; t < NTRK; t++) {
        int32_t x = lane_x(t), w = lane_w(t);
        uint16_t c = TRK_COL[t];
        int muted = (prj.mute | live_mute) >> t & 1u, foc = t == trk();
        g_rect(x, 20, w - 1, 11, muted ? C_BG2 : g_mix(C_BG, c, foc ? 200 : 70));
        if (foc) {
            g_icon(x + 3, 22, ENGINE_ICON[ENG(t)], C_INK, 1);
            trk_name(t, s);
            g_text(x + 13, 20, s, C_INK, T_CROP | T_BOLD);
        } else
            g_icon(x + 6, 22, ENGINE_ICON[ENG(t)], muted ? C_FAINT : c, 1);
        if (muted) g_rect(x + 2, 25, w - 5, 1, C_REC);
    }
    g_rect(LIVE_X, 20, 240 - LIVE_X, 11, C_BG2);
    s[0] = '+';                                 /* the rows a note jumps when written (REC held + SELECT) */
    fmt_u(s + 1, prj.step, 0);
    if (prj.step > 9u) s[0] = s[1], s[1] = s[2], s[2] = 0;
    g_text(0, 20, s, rec ? C_REC : C_FAINT, T_CROP);
}

static void draw_track(void)
{
    const pat_t *pt = P();
    uint32_t len = plen(), i, t, base, prow = sq_play && sq_pat == ui_pat ? sq_row : 0xFFu, ct = trk();
    uint32_t focus_row = (sq_play && sq_pat == ui_pat && !rec) ? prow : cur_row, two = arp_held;   /* (the 2nd column) */
    char s[8];
    base = len > 16u ? (focus_row / 16u) * 16u : 0u;
    draw_lane_head();
    for (i = 0; i < 16u; i++) {
        uint32_t r = base + i;
        int32_t y = ROW_Y0 + (int32_t)i * ROW_H;
        uint16_t bg = (r % 4u) == 0u ? C_BEAT : (i & 1u) ? C_BG2 : C_BG;
        if (!g_vis(y, ROW_H)) continue;
        if (r >= len) {
            g_rect(0, y, 240, ROW_H, g_mix(C_BG, RGB(0, 0, 0), 90));
            continue;
        }
        g_rect(0, y, 240, ROW_H, bg);
        if (r == prow) g_icon(4, y + 2, ICO_PLAY, C_GO, 1);
        else { fmt_u(s, r + 1u, 2); g_text(0, y, s, (r % 4u) == 0u ? C_MID : C_FAINT, T_CROP); }
        for (t = 0; t < NTRK; t++) {
            const cell_t *c = &pt->c[r][t];
            int32_t x = lane_x(t), w = lane_w(t);
            int wide = t == ct, on = c->note && c->note != NOTE_OFF;
            uint16_t col = TRK_COL[t];
            g_blend(x, y, w, ROW_H, col, wide ? 26 : 12);
            g_rect(x, y, 1, ROW_H, C_RULE);
            if (r == prow && fm1_ms - trig_ms[t] < 200u && on)
                g_blend(x + 1, y, w - 1, ROW_H, col, 150u - (fm1_ms - trig_ms[t]) * 3u / 4u);
            if (r == cur_row && wide) {
                g_blend(x + 1, y, w - 1, ROW_H, rec ? C_REC : C_WHITE, 50);
                g_frame(x + 1, y, w - 1, ROW_H, rec ? C_REC : C_WHITE);
            }
            s[0] = 0;
            note_text(t, c->note, s, wide);
            if (wide) {
                g_text(x + 3, y, s, on ? g_mix(col, C_WHITE, 90) : C_FAINT, T_CROP | (on ? T_BOLD : 0));
                if (on) {                       /* the volume as a bar */
                    int32_t h = (int32_t)(c->vol * 10u / 15u);
                    g_rect(x + 29, y + 11 - h, 2, h, g_mix(col, C_BG, 80));
                }
                {                       /* a trick (ARP held: the second column's), the other one's icon after it */
                    uint32_t f = two ? c->fx2 : c->fx, fv = two ? c->fxv2 : c->fxv, o = two ? c->fx : c->fx2;
                    uint16_t fc = two ? C_GO : C_GOLD;
                    if (f) {
                        char v[8];
                        g_icon(x + 36, y + 2, FXD[f % FX_N].icon, fc, 1);
                        fx_text(t, f, fv, v);
                        v[4] = 0;
                        g_text(x + 46, y, v, fc, T_CROP);
                    } else
                        g_text(x + 44, y, "...", two ? g_mix(C_RULE, C_GO, 90) : C_RULE, T_CROP);
                    if (o) g_icon(x + 82, y + 2, FXD[o % FX_N].icon, two ? C_GOLD : C_GO, 1);
                }
            } else {
                g_text(x + (w - 16) / 2, y, s, on ? g_mix(C_BG, col, 120u + c->vol * 9u) : C_RULE, T_CROP);
                if (c->fx || c->fx2) g_rect(x + w - 3, y + 1, 2, 2, c->fx ? C_GOLD : C_GO);
            }
        }
        g_rect(LIVE_X, y, 240 - LIVE_X, ROW_H, C_BG);
        if (pt->live[r] && pt->live[r] <= LF_N)
            g_rrect(LIVE_X + 1, y + 1, 240 - LIVE_X - 1, ROW_H - 2, LBANK_COL[pt->live[r] > 16u]);
        if (r == prow)
            g_blend(GUT_W, y, LIVE_X - GUT_W, ROW_H, C_WHITE, 34);
    }
    {
        const cell_t *c = CUR();
        char a[8], b[8];
        a[0] = 0;
        note_text(ct, c->note, a, 1);
        fmt_u(b, c->vol, 0);
        hint(0, TR("NOTE", "NOTE", "NOTA"), "", a);
        hint(1, TR("VOLUME", "VOLUME", "VOLUMEN"), "V", c->note ? b : "-");
        if (two) {                              /* ARP held: KNOB 3 and 4 on the second column */
            hint(2, TR("EFFET 2", "CELL FX 2", "EFECTO 2"), "2", FXD[c->fx2 % FX_N].tag[lang]);
            fx_text(ct, c->fx2, c->fxv2, b);
            hint(3, TR("VALEUR 2", "VALUE 2", "VALOR 2"), "2", c->fx2 && b[0] ? b : "-");
        } else {
            hint(2, TR("EFFET CASE", "CELL FX", "EFECTO"), "", FXD[c->fx % FX_N].tag[lang]);
            fxv_text(ct, c, b);
            hint(3, TR("VALEUR", "VALUE", "VALOR"), "", c->fx && b[0] ? b : "-");
        }
    }
}

/* ---- tiles (GRID, EFFECTS, MAGIC): the 16 white keys as 4 x 4 */
static void tile_xy(uint32_t i, int32_t *x, int32_t *y, int32_t y0, int32_t step)
{
    *x = 1 + (int32_t)(i % 4u) * 60;
    *y = y0 + (int32_t)(i / 4u) * step;
}
static void draw_tile(int32_t x, int32_t y, int32_t h, uint32_t i, uint16_t col, uint32_t lit, uint32_t icon,
                      const char *name, const char *sub)
{
    char n[4];
    if (!g_vis(y, h)) return;
    g_rrect(x, y, 58, h, lit ? col : g_mix(C_BG2, col, 34));
    if (!lit) g_frame(x, y, 58, h, g_mix(C_BG, col, 90));
    fmt_u(n, i + 1u, 0);
    g_text(x + 3, y + 1, n, lit ? C_INK : C_FAINT, T_CROP);
    if (icon < ICO_N) g_icon(x + 21, y + (h > 42 ? 4 : 2), icon, lit ? C_INK : col, 2);
    g_text_c(x + 29, y + (h > 42 ? 20 : 17), name, lit ? C_INK : C_TXT, T_CROP);
    if (sub && sub[0]) g_text_c(x + 29, y + (h > 42 ? 31 : 27), sub, lit ? g_mix(col, C_INK, 160) : C_MID, T_CROP);
}

static void draw_grid(void)
{
    uint32_t gp = gpat(grid_page), len = plen_of(gp), i, base = grow(grid_page), t = trk();
    uint32_t prow = sq_play && sq_pat == gp ? sq_row : 0xFFu, off = gp != ui_pat ? plen() : 0u;   /* (the second half: steps on from there) */
    const pat_t *pt = &prj.pat[gp];
    uint16_t col = TRK_COL[t];
    char s[16], v[8];
    if (g_vis(20, 20)) {
        g_rect(0, 20, 240, 20, C_BG);
        g_rrect(3, 22, 82, 15, col);
        g_icon(6, 25, ENGINE_ICON[ENG(t)], C_INK, 1);
        trk_name(t, s);
        g_text(16, 22, s, C_INK, T_CROP | T_BOLD);
        s[0] = 0;
        note_text(t, brush_note[t], s, 1);
        g_text(94, 22, s, C_TXT, T_CROP | T_BOLD);
        if (brush_fx[t]) {
            g_icon(124, 25, FXD[brush_fx[t] % FX_N].icon, C_GOLD, 1);
            fx_text(t, brush_fx[t], brush_fxv[t], v);
            g_text(134, 22, v, C_GOLD, T_CROP);
        }
        {                                       /* where we are among the steps: 17-32/64 (OCT- / OCT+: 16, 32, 64) */
            uint32_t pn = grid_pages(), tot = plen() + (pat_linked(ui_pat) ? plen_of(ui_pat + 1u) : 0u);
            s[0] = 0;
            if (pn > 1u) {
                str_cat(fmt_u(s, off + base + 1u, 0), "-");
                str_cat(fmt_u(s + str_len(s), off + (base + 16u < len ? base + 16u : len), 0), "/");
            }
            fmt_u(s + str_len(s), tot, 0);
            g_text(236 - g_text_w(s), 22, s, KNOB_COL[1], T_CROP | T_BOLD);
        }
    }
    if (g_vis(38, 2)) g_rect(0, 38, 240, 2, C_BG);
    for (i = 0; i < 16u; i++) {
        uint32_t r = base + i;
        int32_t x, y;
        const cell_t *c = &pt->c[r < NROW ? r : 0][t];
        tile_xy(i, &x, &y, 40, 46);
        if (!g_vis(y - 2, 48)) continue;
        g_rect(x - 1, y - 2, 60, 48, C_BG);
        if (r >= len) { g_frame(x, y, 58, 44, C_BG2); continue; }
        if (c->note) {
            g_rrect(x, y, 58, 44, c->note == NOTE_OFF ? C_BG2 : g_mix(C_BG, col, 80u + c->vol * 8u));
            s[0] = 0;
            note_text(t, c->note, s, 1);
            g_text_c(x + 29, y + 15, s, c->note == NOTE_OFF ? C_FAINT : C_WHITE, T_CROP | T_BOLD);
            if (c->fx || c->fx2) {              /* (the first trick; the second's icon if there is one) */
                uint32_t f = c->fx ? c->fx : c->fx2;
                g_icon(x + 47, y + 3, FXD[f % FX_N].icon, c->fx ? C_GOLD : C_GO, 1);
                if (c->fx && c->fx2) g_icon(x + 37, y + 3, FXD[c->fx2 % FX_N].icon, C_GO, 1);
                fx_text(t, f, c->fx ? c->fxv : c->fxv2, v);
                g_text_c(x + 29, y + 30, v, c->fx ? C_GOLD : C_GO, T_CROP);
            }
        } else
            g_rrect(x, y, 58, 44, (r % 4u) == 0u ? C_BEAT : C_BG2);
        fmt_u(s, off + r + 1u, 0);
        g_text(x + 3, y + 1, s, C_FAINT, T_CROP);
        if (r == prow) { g_frame(x, y, 58, 44, C_WHITE); g_frame(x + 1, y + 1, 56, 42, C_WHITE); }
        if (grid_held == (int32_t)i) g_frame(x + 2, y + 2, 54, 40, C_GOLD);
    }
    {
        cell_t b;
        const cell_t *c = grid_held >= 0 ? &pt->c[base + (uint32_t)grid_held][t] : &b;
        memset(&b, 0, sizeof b);
        b.note = (uint8_t)brush_note[t];
        b.vol = brush_vol[t] & 15u;
        b.fx = brush_fx[t] % FX_N;
        b.fxv = brush_fxv[t];
        s[0] = 0;
        note_text(t, c->note, s, 1);
        fmt_u(v, c->vol, 0);
        hint(0, TR("NOTE", "NOTE", "NOTA"), "", s);
        hint(1, TR("VOLUME", "VOLUME", "VOLUMEN"), "V", v);
        hint(2, TR("EFFET CASE", "CELL FX", "EFECTO"), "", FXD[c->fx % FX_N].tag[lang]);
        fxv_text(t, c, v);
        hint(3, TR("VALEUR", "VALUE", "VALOR"), "", c->fx ? v : "-");
    }
}

/* ---- SOUND */
static void make_peaks(uint32_t src)
{
    const uint8_t *d;
    uint32_t len, rate, x;
    if (src == wv_src) return;
    wv_src = (uint8_t)src;
    memset(wv_peak, 0, sizeof wv_peak);
    if (!smp_source(src, &d, &len, &rate)) return;
    for (x = 0; x < 224u; x++) {
        uint32_t a = x * len / 224u, z = (x + 1u) * len / 224u, m = 0;
        for (; a < z; a++) {
            int32_t v = BRK_ULAW[d[a]];
            uint32_t av = (uint32_t)(v < 0 ? -v : v);
            if (av > m) m = av;
        }
        wv_peak[x] = (uint8_t)(m >> 9);
    }
}

static void draw_wave(int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t e, uint32_t sh, uint16_t col)
{
    int32_t x, py = 0, mid = y0 + h / 2;
    for (x = 0; x < w; x++) {                   /* two cycles of the wave */
        uint32_t ph = (uint32_t)x * (0x80000000u / (uint32_t)w) * 4u;
        int32_t v;
        if (e == E_PULSE) {
            static const uint32_t DUTY[5] = {0x20000000u, 0x40000000u, 0x80000000u, 0xC0000000u, 0x60000000u};
            v = ph < DUTY[sh % 5u] ? 1000 : -1000;
        } else if (e == E_WAVE)
            v = ((int32_t)WAVES[sh & 7u][ph >> 27] * 2 - 15) * 66;
        else
            v = (int32_t)(((uint32_t)(x * 2654435761u) >> 28) & 1u ? 1000 : -1000) * ((x * 7 + 3) % 5 + 1) / 5;
        v = mid - v * (h / 2 - 2) / 1000;
        if (x) g_line(x0 + x - 1, py, x0 + x, v, col);
        py = v;
    }
}

static void draw_fx_curve(int32_t x0, int32_t y0, int32_t w, int32_t h, const int8_t *fp, uint16_t col)
{                                               /* what the effect does: a filter's response, a curve */
    int32_t type = fp[0], a = fp[1], b = fp[2], x, py = 0;
    for (x = 0; x < w; x++) {
        int32_t v = 0, f = x * 15 / w, c = a;
        switch (type) {
        case FXT_LPF: v = f <= c ? 600 + (f == c ? b * 30 : 0) : 600 - (f - c) * 160; break;
        case FXT_HPF: v = f >= c ? 600 + (f == c ? b * 30 : 0) : 600 - (c - f) * 160; break;
        case FXT_BPF: case FXT_WAH: v = 900 - (f > c ? f - c : c - f) * (130 + b * 8); break;
        case FXT_DIST: { int32_t in = (x * 2000 / w) - 1000; v = sat(in * (1 + a * 2) * 16) / 33; v = 500 + v / 2; break; }
        case FXT_CRUSH: { int32_t st = 1 + a / 2; v = ((x * st / w) * 1000) / st; break; }
        case FXT_DECIM: { int32_t q = 2 + a; v = 500 + (sine((uint32_t)((x / q) * q) * (0xFFFFFFFFu / (uint32_t)w) * 2u) * 450 >> 15); break; }
        case FXT_RING: v = 500 + ((sine((uint32_t)x * (0xFFFFFFFFu / (uint32_t)w) * 3u) >> 8) * (sine((uint32_t)x * (0xFFFFFFFFu / (uint32_t)w) * (6u + (uint32_t)a)) >> 8)) * 450 / 16384; break;
        case FXT_TREM: v = 500 + (sine((uint32_t)x * (0xFFFFFFFFu / (uint32_t)w) * 3u) * (b * 30) >> 15); break;
        default: v = 500; break;
        }
        v = clampi(v, 0, 1000);
        v = y0 + h - 2 - v * (h - 4) / 1000;
        if (x) { g_line(x0 + x - 1, py, x0 + x, v, col); g_line(x0 + x - 1, py + 1, x0 + x, v + 1, col); }
        py = v;
    }
}

/* the SOUND page's pages: the FM track has its operators too */
static uint32_t sp_n(uint32_t t) { return ENG(t) == E_FM ? 8u : ENG(t) == E_SAMPLE ? 7u : 6u; }
static uint32_t sp_at(uint32_t t, uint32_t i)
{
    if (ENG(t) == E_FM) return SP_LIST_FM[i % 8u];
    if (ENG(t) == E_SAMPLE) return SP_LIST_SMP[i % 7u];
    return SP_LIST[i % 6u];
}
/* EDIT goes round SOUND, SOUND 2, (OPERATOR, OP ENVELOPE | SAMPLE EDIT), EFFECT, TABLE; LFO has its own two: LFO, MODULATION.
 * SELECT goes along the group of the page it is on */
static uint32_t sg_lfo(uint32_t t, uint32_t i) { uint32_t sp = sp_at(t, i); return sp == SP_LFO || sp == SP_MOD; }
static uint32_t sg_list(uint32_t t, uint32_t lfo, uint8_t *l)   /* the group's pages (indexes into sp_at): its size */
{
    uint32_t i, n = 0;
    for (i = 0; i < sp_n(t); i++)
        if (sg_lfo(t, i) == lfo) l[n++] = (uint8_t)i;
    return n;
}
static void sg_move(int32_t d, int wrap)        /* the next page of the group (SELECT stops at the ends, a button goes round) */
{
    uint8_t l[8];
    uint32_t t = trk(), n = sg_list(t, sg_lfo(t, snd_page), l), i, pos = 0;
    int32_t q;
    for (i = 0; i < n; i++)
        if (l[i] == snd_page) pos = i;
    q = (int32_t)pos + d;
    if (wrap) q = (q + (int32_t)n) % (int32_t)n;   /* (a button: d is 1) */
    snd_page = l[clampi(q, 0, (int32_t)n - 1)];
}

static void op_text(uint32_t k, int32_t v, char *s)   /* an operator knob's value */
{
    s[0] = 0;
    if (k == OPK_RATIO) { if (v == 0) str_cat(s, "X0.5"); else { s[0] = 'X'; fmt_u(s + 1, (uint32_t)v, 0); } }
    else if (k == OPK_DET || k == OPK_FINE) fmt_s(s, v);
    else fmt_u(s, (uint32_t)v, 0);
}

static void draw_fm(int32_t x0, int32_t y0, uint32_t t, uint16_t col, uint32_t sp)
{
    const uint8_t *fp = fm_patch;
    uint32_t alg = fm_alg, car = fm6_carriers(alg), n, live = fmn[fm_cur].live;
    char s[16];
    s[0] = 0;
    str_cat(s, "ALGO ");
    fmt_u(s + str_len(s), alg + 1u, 0);
    g_text(x0, y0, s, C_GOLD, T_CROP | T_BOLD);
    g_rect(x0 + 80, y0 + 4, 6, 6, col);
    g_text(x0 + 90, y0, TR("PORTEUSE", "CARRIER", "PORTADORA"), C_TXT, T_CROP);
    g_rect(x0 + 168, y0 + 4, 6, 6, C_GOLD);
    g_text(x0 + 178, y0, "MOD", C_TXT, T_CROP);
    for (n = 1; n <= 6u; n++) {                 /* operator n is the patch's 6 - n (msfa) */
        uint32_t k = 6u - n, c = car >> k & 1u, ol = fp[k * FP_OP + FP_OL] % 100u;
        int32_t x = x0 + 2 + (int32_t)(n - 1u) * 36, h = (int32_t)(ol * 50u / 99u);
        uint16_t oc = c ? col : C_GOLD;
        int sel = (sp == SP_OP || sp == SP_OPENV) && n == fm_op;
        g_rrect(x, y0 + 16, 30, 58, sel ? g_mix(C_BG, oc, 70) : C_BG);
        if (sel) g_frame(x, y0 + 16, 30, 58, C_WHITE);
        g_rect(x + 5, y0 + 70 - h, 20, h, g_mix(C_BG, oc, live ? 240 : 160));
        fmt_u(s, n, 0);
        g_text_c(x + 15, y0 + 18, s, sel ? C_WHITE : c ? oc : C_MID, T_CROP | T_BOLD);
    }
    (void)t;
}

static void draw_openv(int32_t x0, int32_t y0, int32_t w, int32_t h)   /* the selected operator's envelope */
{
    int32_t a = op_get(fm_op, OPK_ATT), d = op_get(fm_op, OPK_DEC), su = op_get(fm_op, OPK_SUS), r = op_get(fm_op, OPK_REL);
    int32_t by = y0 + h, sy = by - su * h / 99, xa = x0 + 4 + a * 50 / 99, xd = xa + 4 + d * 50 / 99;
    int32_t xr = x0 + w - 4 - r * 50 / 99, x, py = by;
    if (xr < xd + 8) xr = xd + 8;
    for (x = x0; x <= x0 + w; x++) {
        int32_t y;
        uint32_t seg;
        if (x <= xa) { y = by - (x - x0) * h / (xa - x0 ? xa - x0 : 1); seg = 0; }
        else if (x <= xd) { y = y0 + (x - xa) * (sy - y0) / (xd - xa ? xd - xa : 1); seg = 1; }
        else if (x <= xr) { y = sy; seg = 2; }
        else { y = sy + (x - xr) * (by - sy) / (x0 + w - xr ? x0 + w - xr : 1); seg = 3; }
        y = clampi(y, y0, by);
        g_blend(x, y, 1, by - y, KNOB_COL[seg], 50);
        g_line(x - 1, py, x, y, KNOB_COL[seg]);
        g_line(x - 1, py - 1, x, y - 1, KNOB_COL[seg]);
        py = y;
    }
}

/* ---- the sample, drawn (SOUND; SAMPLE EDIT with its markers): its outline, the edit's start and end (outside
 * them dimmed), the slices (even, or where the edit put them), what plays now */
static uint8_t se_cur;                          /* SAMPLE EDIT: 0 the start, 1 the end, 2..16 slice 2..16 */
static void sed_slices_px(const sedit_t *se, int32_t *sx)   /* the 16 slices' starts, 0..224 */
{
    uint32_t i, a = (uint32_t)se->start * 224u >> 16, b = se->end >= 65535u ? 224u : (uint32_t)se->end * 224u >> 16;
    for (i = 0; i < 16u; i++)
        sx[i] = se->manual ? (int32_t)((uint32_t)se->sl[i] * 224u >> 16) : (int32_t)(a + ((b - a) * i >> 4));
}
static void draw_smp(uint32_t t, uint16_t col, int edit)
{
    const int8_t *p = prj.snd[t].p;
    const sedit_t *se = &prj.sed[t];
    uint32_t src = (uint32_t)clampi(p[P_SRC], 0, BRK_N + NUSR - 1), x, playing = 999, i, k = 0, slices = p[P_SMODE] == 0;
    const smpv_t *v = smp[t][0].on ? &smp[t][0] : smp[t][1].on ? &smp[t][1] : 0;
    const uint8_t *d;
    uint32_t len = 1, rate;
    int32_t sx[16], a = (int32_t)((uint32_t)se->start * 224u >> 16), b = se->end >= 65535u ? 224 : (int32_t)((uint32_t)se->end * 224u >> 16);
    make_peaks(src);
    if (!smp_source(src, &d, &len, &rate)) {
        g_text_c(120, 84, TR("EMPLACEMENT VIDE", "EMPTY SLOT", "RANURA VACIA"), C_FAINT, T_CROP | T_BOLD);
        g_text_c(120, 102, TR("GLISSE UN SON DANS", "DROP A SOUND ON THE", "SUELTA UN SONIDO EN"), C_FAINT, T_CROP);
        g_text_c(120, 116, TR("L'EMULATEUR", "EMULATOR", "EL EMULADOR"), C_FAINT, T_CROP);
        return;
    }
    sed_slices_px(se, sx);
    if (v) playing = (v->pos >> 16) * 224u / len;
    for (x = 0; x < 224u; x++) {
        int32_t h = wv_peak[x] * 34 / 64, in = (int32_t)x >= a && (int32_t)x < b;
        while (k < 15u && (int32_t)x >= sx[k + 1u]) k++;
        g_rect(8 + (int32_t)x, 99 - h, 1, h * 2 + 1, x == playing ? C_WHITE :
               !in ? g_mix(C_BG2, col, 50) : g_mix(C_BG2, col, slices && (k & 1u) ? 210 : 150));
    }
    if (slices)
        for (i = 0; i < 16u; i++) {
            int sel = edit && se_cur >= 2u && i == se_cur - 1u;
            g_rect(8 + sx[i], 60, sel ? 2 : 1, 78, sel ? KNOB_COL[0] : C_RULE);
            if (src < BRK_N && !se->manual && se->start == 0u && se->end >= 65535u) {   /* (the break's own beats) */
                if (BRK_TAG[src][i] & 1u) g_rect(8 + sx[i] + 3, 134, 4, 4, RGB(255, 140, 60));
                if (BRK_TAG[src][i] & 2u) g_rect(8 + sx[i] + 8, 134, 4, 4, C_WHITE);
            }
            if (edit && sel) {
                char n[4];
                fmt_u(n, i + 1u, 0);
                g_text(8 + sx[i] + 3, 59, n, KNOB_COL[0], T_CROP | T_BOLD);
            }
        }
    if (edit || se->start || se->end < 65535u) {   /* the start and the end */
        g_rect(8 + a, 58, 2, 82, edit && se_cur == 0u ? KNOB_COL[0] : C_GOLD);
        g_rect(6 + b, 58, 2, 82, edit && se_cur == 1u ? KNOB_COL[0] : C_GOLD);
    }
    if (se->rev) g_icon(222, 60, ICO_REV, C_GOLD, 1);
    if (se->norm) g_icon(210, 60, ICO_VOL, C_GOLD, 1);
}

static void sed_mark_text(uint32_t t, char *n, char *pos)   /* SAMPLE EDIT: the marker chosen, where it is */
{
    const sedit_t *se = &prj.sed[t];
    const uint8_t *d;
    uint32_t len = 1, rate = 22050, v = se_cur == 0u ? se->start : se_cur == 1u ? se->end : se->sl[se_cur - 1u], f;
    n[0] = 0;
    if (se_cur == 0u) str_cat(n, TR("DEBUT", "START", "INICIO"));
    else if (se_cur == 1u) str_cat(n, TR("FIN", "END", "FIN"));
    else { str_cat(n, TR("TR ", "SL ", "CR ")); fmt_u(n + str_len(n), se_cur, 0); }
    if (se_cur >= 2u && !se->manual) {          /* (even slices: where they are now) */
        uint32_t b = se->end;
        v = se->start + (uint32_t)(((b - se->start) * (se_cur - 1u)) >> 4);
    }
    if (!smp_source((uint32_t)clampi(prj.snd[t].p[P_SRC], 0, BRK_N + NUSR - 1), &d, &len, &rate)) len = 0;
    f = se_cur == 1u && v >= 65535u ? len : sed_at(v, len);
    str_cat(fmt_u(pos, f * 1000u / (rate ? rate : 22050u), 0), "MS");
}

static void smp_cards(uint32_t t)               /* SAMPLE EDIT's four knobs */
{
    const sedit_t *se = &prj.sed[t];
    char n[12], pos[12], g[12];
    const char *nm[4], *val[4];
    int32_t num[4], den[4];
    uint32_t i, v = se_cur == 0u ? se->start : se_cur == 1u ? se->end : se->sl[se_cur - 1u];
    sed_mark_text(t, n, pos);
    g[0] = 0;
    if (se->norm) {
        char *q = fmt_u(str_cat(g, "X"), se->gain / 256u, 0);
        *q++ = '.';
        fmt_u(q, (se->gain % 256u) * 10u / 256u, 0);
    } else
        str_cat(g, TR("NON", "OFF", "NO"));
    nm[0] = TR("DEPLACE", "MOVE", "MOVER");
    nm[1] = TR("PRECIS", "FINE", "FINO");
    nm[2] = "NORM";                             /* (normalise: the card holds 7 letters) */
    nm[3] = TR("ENVERS", "REVERSE", "REVES");
    val[0] = n;
    val[1] = pos;
    val[2] = g;
    val[3] = se->rev ? TR("OUI", "ON", "SI") : TR("NON", "OFF", "NO");
    num[0] = num[1] = (int32_t)(v >> 8);
    den[0] = den[1] = 255;
    num[2] = se->norm;
    num[3] = se->rev;
    den[2] = den[3] = 1;
    for (i = 0; i < 4u; i++) {
        if (g_vis(144, 80)) {
            g_rect((int32_t)i * 60, 144, 60, 80, C_BG);
            card(i, 1 + (int32_t)i * 60, 146, 76, nm[i], val[i], num[i], den[i]);
        }
        hint(i, nm[i], "", val[i]);
    }
}

/* ---- TABLE (EDIT, the last page): the track's 16 steps in two columns, its settings on top, its pitch drawn */
static uint8_t tb_cur;                          /* 0 the settings, 1..16 a step */
static void tb_vol_text(uint32_t v, char *s) { if (v) fmt_u(s, v, 0); else str_cat(s, "--"); }
static void tb_fx_text(const tstep_t *st, char *s)
{
    s[0] = 0;
    if (st->fx == TB_HOP) fmt_u(s, ((uint32_t)st->fxv & 15u) + 1u, 0);
    else if (st->fx == TB_BEND || st->fx == TB_VSL || st->fx == TB_PAN) fmt_s(s, st->fxv);
    else if (st->fx > TB_CUT) fmt_u(s, (uint32_t)(st->fxv < 0 ? 0 : st->fxv), 0);
}
static void tb_loop_text(const table_t *tb, char *s)   /* the step it goes back to after the last, or none */
{
    if (tb->loop < tb->len) fmt_u(s, tb->loop + 1u, 0);
    else str_cat(s, TR("NON", "OFF", "NO"));
}
static void draw_table(uint32_t t, uint16_t col)
{
    const table_t *tb = &prj.tbl[t];
    uint32_t i, len = (uint32_t)clampi(tb->len, 1, NTSTEP), now = tb_now[t];
    char s[16];
    if (g_vis(144, 38)) g_rect(0, 144, 240, 38, C_BG);   /* (draw_sound paints to 144: the rest is the table's) */
    static const char *const SET_L[LANG_N][4] = {{"VIT", "LONG", "BOUC", ""}, {"SPD", "LEN", "LOOP", ""},
                                                 {"VEL", "LARG", "BUCL", ""}};
    for (i = 0; i < 4u; i++) {                  /* the settings: four chips in the knobs' colours */
        int32_t x = 4 + (int32_t)i * 58;
        if (!g_vis(58, 14)) break;
        g_rrect(x, 58, 56, 14, tb_cur == 0u ? g_mix(C_BG2, KNOB_COL[i], 80) : C_BG2);
        s[0] = 0;
        if (i == 0u) str_cat(fmt_u(s, tb->speed, 0), "T");
        else if (i == 1u) fmt_u(s, len, 0);
        else if (i == 2u) { if (tb->loop < len) fmt_u(s, tb->loop + 1u, 0); else str_cat(s, "-"); }
        else str_cat(s, tb->on ? TR("OUI", "ON", "SI") : TR("NON", "OFF", "NO"));
        g_text(x + 3, 59, SET_L[lang][i], KNOB_COL[i], T_CROP);
        if (i == 3u) g_text_c(x + 28, 59, s, tb->on ? C_GO : C_FAINT, T_CROP | T_BOLD);
        else g_text(x + 53 - g_text_w(s), 59, s, tb_cur == 0u ? C_WHITE : C_TXT, T_CROP | T_BOLD);
    }
    if (tb_cur == 0u && g_vis(56, 18)) g_frame(2, 57, 236, 16, C_WHITE);
    for (i = 0; i < NTSTEP; i++) {              /* the steps: two columns of 8 */
        int32_t x = 4 + (int32_t)(i / 8u) * 118, y = 76 + (int32_t)(i % 8u) * 13;
        const tstep_t *st = &tb->s[i];
        int in = i < len, cur = tb_cur == i + 1u, play = now == i && tb->on;
        uint16_t tc = in ? C_TXT : C_RULE;
        char tr[8], vo[8], fv[8];
        if (!g_vis(y, 13)) continue;
        g_rect(x, y, 114, 12, play ? g_mix(C_BG, col, 150) : cur ? C_BG2 : (i % 4u) == 0u ? C_BEAT : C_BG);
        if (cur) g_frame(x, y, 114, 12, C_WHITE);
        fmt_u(s, i + 1u, 2);
        g_text(x + 1, y, s, cur ? C_WHITE : in ? C_MID : C_RULE, T_CROP);
        tr[0] = vo[0] = 0;
        if (st->tr) fmt_s(tr, st->tr); else str_cat(tr, " 0");
        tb_vol_text(st->vol, vo);
        tb_fx_text(st, fv);
        g_text(x + 20, y, tr, cur ? KNOB_COL[0] : tc, T_CROP | (st->tr ? T_BOLD : 0));
        g_text(x + 48, y, vo, cur ? KNOB_COL[1] : st->vol ? tc : C_RULE, T_CROP);
        if (st->fx) {
            g_icon(x + 68, y + 2, TBD[st->fx % TB_N].icon, cur ? KNOB_COL[2] : in ? C_GOLD : C_RULE, 1);
            fv[4] = 0;
            g_text(x + 78, y, fv[0] ? fv : TBD[st->fx % TB_N].tag[lang], cur ? KNOB_COL[3] : in ? C_GOLD : C_RULE, T_CROP);
        } else
            g_text(x + 68, y, "...", C_RULE, T_CROP);
    }
    if (g_vis(182, 42)) {                       /* the table's pitch: a bar a step, up or down from the note */
        g_rect(0, 182, 240, 42, C_BG);
        g_rect(4, 202, 232, 1, C_RULE);
        for (i = 0; i < NTSTEP; i++) {
            int32_t x = 4 + (int32_t)i * 14 + 1, h = clampi(tb->s[i].tr * 19 / 12, -19, 19), in = i < len;   /* (an octave: full) */
            uint16_t c = now == i && tb->on ? C_WHITE : in ? col : C_RULE;
            if (h > 0) g_rect(x, 202 - h, 12, h, c);
            else if (h < 0) g_rect(x, 203, 12, -h, c);
            else g_rect(x, 201, 12, 3, in ? g_mix(C_BG, c, 120) : C_RULE);
            if (tb->loop < len && i == tb->loop) g_rect(x, 220, 12, 2, KNOB_COL[2]);   /* (where the loop goes back) */
        }
        if (!tb->on)
            g_text_c(120, 184, TR("TABLE ARRETEE: K4 SUR LA LIGNE 0", "TABLE OFF: K4 ON ROW 0", "TABLA APAGADA: K4 EN FILA 0"),
                     C_FAINT, T_CROP);
    }
    if (tb_cur == 0u) {
        s[0] = 0;
        str_cat(fmt_u(s, tb->speed, 0), "T");
        hint(0, TR("VITESSE", "SPEED", "VELOCIDAD"), "", s);
        fmt_u(s, len, 0);
        hint(1, TR("LONGUEUR", "LENGTH", "LARGO"), "", s);
        s[0] = 0;
        tb_loop_text(tb, s);
        hint(2, TR("BOUCLE", "LOOP", "BUCLE"), "", s);
        hint(3, "TABLE", "", tb->on ? TR("OUI", "ON", "SI") : TR("NON", "OFF", "NO"));
    } else {
        const tstep_t *st = &tb->s[(tb_cur - 1u) % NTSTEP];
        char v[8];
        s[0] = v[0] = 0;
        if (st->tr) fmt_s(s, st->tr); else str_cat(s, "0");
        hint(0, TR("TRANSPOSE", "TRANSPOSE", "TRANSPONE"), "", s);
        s[0] = 0;
        tb_vol_text(st->vol, s);
        hint(1, TR("VOLUME", "VOLUME", "VOLUMEN"), "V", s);
        hint(2, TR("EFFET", "TRICK", "EFECTO"), "", TBD[st->fx % TB_N].tag[lang]);
        tb_fx_text(st, v);
        hint(3, TR("VALEUR", "VALUE", "VALOR"), "", v[0] ? v : "-");
    }
}

static void draw_sound(void)
{
    uint32_t t = trk(), e = ENG(t), i, sp = sp_at(t, snd_page), first = SP_FIRST[sp];
    if (sp == SP_SMP && prj.snd[t].p[P_SMODE] && se_cur > 1u)
        se_cur = 1;                             /* (notes: no slices to move) */
    const int8_t *p = prj.snd[t].p;
    uint16_t col = TRK_COL[t];
    char s[24];
    if (g_vis(20, 20)) {
        g_rect(0, 20, 240, 20, C_BG);
        g_rrect(3, 22, 82, 15, col);
        g_icon(6, 25, ENGINE_ICON[e], C_INK, 1);
        trk_name(t, s);
        g_text(16, 22, s, C_INK, T_CROP | T_BOLD);
        s[0] = 0;
        if (pre_cand != 0xFF && pre_cand_t == t) {   /* an edited sound: the next factory sound, to load with OCT+ */
            str_cat(s, PRESETS[pre_cand % NPRESET].name[lang]);
            s[11] = 0;
            str_cat(s, "?");
            g_text(90, 22, s, C_GOLD, T_CROP | T_BOLD);
            g_text(90 + g_text_w(s) + 6, 22, "OCT+", KNOB_COL[3], T_CROP | T_BOLD);
        } else {
            str_cat(str_cat(s, PRESETS[prj.snd[t].preset % NPRESET].name[lang]), sound_is_preset(t) ? "" : "*");
            s[15] = 0;                          /* (14 letters and the '*': up to x = 210) */
            g_text(90, 22, s, C_TXT, T_CROP);
        }
    }
    if (g_vis(40, 102)) {
        g_rect(0, 40, 240, 104, C_BG);           /* (to the cards at 144: no row left unpainted) */
        if (sp == SP_SON1) {                    /* the engines: the factory sound's (PRESETS), or ARP + PRESETS (FM: one track only) */
            static const char *const E3[LANG_N][E_N] = {{"CAR", "OND", "BRU", "FM", "KIT", "SMP"},
                                                         {"PUL", "WAV", "NOI", "FM", "KIT", "SMP"},
                                                         {"PUL", "OND", "RUI", "FM", "KIT", "SMP"}};
            for (i = 0; i < E_N; i++) {
                int32_t x = 4 + (int32_t)i * 39;
                int on = i == e, off = i == E_FM && !fm_free(t);
                g_rrect(x, 41, 37, 14, on ? col : C_BG2);
                g_icon(x + 3, 44, ENGINE_ICON[i], on ? C_INK : off ? C_RULE : col, 1);
                g_text(x + 13, 41, E3[lang][i], on ? C_INK : off ? C_RULE : C_TXT, T_CROP | (on ? T_BOLD : 0));
            }
        } else {                                /* the page's name, and where it is among them: EDIT goes round */
            uint8_t gl[8];
            uint32_t n = sg_list(t, sg_lfo(t, snd_page), gl);
            int32_t w = g_text_w(SP_NAMES[lang][sp]) + 10;
            g_rrect(4, 41, w, 14, col);
            g_text(9, 41, SP_NAMES[lang][sp], C_INK, T_CROP | T_BOLD);
            if (sp == SP_OP || sp == SP_OPENV) {
                char o[8] = {'O', 'P', ' ', (char)('0' + fm_op), 0};
                g_text(w + 10, 41, o, C_WHITE, T_CROP | T_BOLD);
                g_text(w + 50, 41, "PRESETS", C_FAINT, T_CROP);
            }
            for (i = 0; i < n; i++)
                g_rect(236 - (int32_t)(n - i) * 10, 45, 7, 7, gl[i] == snd_page ? col : C_RULE);
        }
        if (sp != SP_TABLE)
            g_rrect(4, 56, 232, 86, C_BG2);
        if (sp == SP_TABLE) {
            /* (below) */
        } else if (sp == SP_OPENV) {
            draw_openv(14, 66, 212, 66);
        } else if (sp == SP_OP) {
            draw_fm(12, 60, t, col, sp);
        } else if (sp == SP_FX) {
            int8_t fp[3] = {p[P_FXT], p[P_FXA], p[P_FXB]};
            int32_t m = p[P_FXM];
            g_icon(12, 64, FXT_ICON[clampi(p[P_FXT], 0, FXT_N - 1)], col, 2);
            g_text(34, 64, pd_value(&PDESC[e][P_FXT], p[P_FXT]), C_TXT, T_CROP | T_BOLD);
            g_text(34, 76, TR("SUR CETTE PISTE", "ON THIS TRACK", "EN ESTA PISTA"), C_FAINT, T_CROP);
            if (p[P_FXT]) draw_fx_curve(12, 90, 200, 48, fp, col);
            else {
                g_text_c(116, 100, TR("LE MASTER: ENV, ENV", "THE MASTER: ENV, ENV", "EL MASTER: ENV, ENV"), C_MID, T_CROP);
                g_text_c(116, 114, TR("PUIS ALGO SUR M", "THEN ALGO TO M", "LUEGO ALGO A M"), C_MID, T_CROP);
            }
            g_rect(222, 136 - m * 50 / 15, 6, m * 50 / 15, KNOB_COL[3]);
        } else if (sp == SP_LFO) {
            int32_t x, py = 0, w = p[P_LFOW], dpt = p[P_LFOA];
            for (x = 0; x < 200; x++) {
                uint32_t ph = (uint32_t)x * (0xFFFFFFFFu / 100u);
                int32_t v;
                switch (w) {
                case 1: v = (int32_t)(ph >> 16); v = (v < 32768 ? v : 65535 - v) * 2 - 32767; break;
                case 2: v = ph < 0x80000000u ? 32767 : -32767; break;
                case 3: v = (int32_t)(ph >> 16) - 32768; break;
                case 4: v = ((int32_t)((uint32_t)((x / 25) * 2654435761u) >> 17) - 16384) * 2; break;
                default: v = sine(ph); break;
                }
                v = 98 - (v * (6 + dpt * 2)) / 32767;
                if (x) g_line(16 + x - 1, py, 16 + x, v, p[P_LFOD] ? col : C_FAINT);
                py = v;
            }
            g_text(14, 60, pd_value(&PDESC[e][P_LFOD], p[P_LFOD]), C_GOLD, T_CROP | T_BOLD);
            if (p[P_LFOD]) g_rect(16 + 200 + 4, 98 - (tk[t].lfo * (6 + dpt * 2)) / 32767 - 2, 5, 5, C_WHITE);
        } else if (sp == SP_MOD) {             /* what moves what: the envelope and the velocity */
            static const char *const ML[LANG_N][3] = {{"ENVELOPPE > EFFET A", "VELOCITE > EFFET A", "ENVELOPPE > HAUTEUR"},
                                                       {"ENVELOPE > EFFECT A", "VELOCITY > EFFECT A", "ENVELOPE > PITCH"},
                                                       {"ENVOLVENTE > EFECTO A", "VELOCIDAD > EFECTO A", "ENVOLVENTE > ALTURA"}};
            static const uint8_t MK[3] = {P_MENVA, P_MVELA, P_MENVP};
            for (i = 0; i < 3u; i++) {
                int32_t y = 60 + (int32_t)i * 26, v = p[MK[i]], hi = i == 2u ? 12 : 15, w = v * 100 / hi;
                g_text(12, y, ML[lang][i], KNOB_COL[i], T_CROP);
                g_rect(20, y + 15, 200, 4, C_BG);
                g_rect(120, y + 13, 1, 8, C_RULE);
                if (w > 0) g_rect(120, y + 15, w, 4, KNOB_COL[i]);
                else if (w < 0) g_rect(120 + w, y + 15, -w, 4, KNOB_COL[i]);
            }
            if (p[P_FXT] == FXT_OFF && (p[P_MENVA] || p[P_MVELA]))
                g_text(12, 130, TR("(CHOISIS UN EFFET: EDIT)", "(PICK AN EFFECT: EDIT)", "(ELIGE UN EFECTO: EDIT)"), C_REC, T_CROP);
        } else if (e <= E_NOISE) {
            draw_wave(12, 60, 120, 76, e, (uint32_t)p[P_SHAPE], col);
            draw_wave(13, 60, 120, 76, e, (uint32_t)p[P_SHAPE], col);
            g_text(150, 62, TR("ENV: ADSR", "ENV: ADSR", "ENV: ADSR"), C_FAINT, T_CROP);
            g_text(150, 78, pd_value(&PDESC[e][P_SHAPE], p[P_SHAPE]), C_TXT, T_CROP | T_BOLD);
        } else if (e == E_FM)
            draw_fm(12, 60, t, col, sp);
        else if (e == E_KIT) {
            for (i = 0; i < 16u; i++) {
                int32_t x = 8 + (int32_t)(i % 8u) * 28, y = 60 + (int32_t)(i / 8u) * 40;
                int lit = (drum[t][0].on && drum[t][0].drum == i && drum[t][0].t < 4000u) ||
                          (drum[t][1].on && drum[t][1].drum == i && drum[t][1].t < 4000u);
                g_rrect(x, y, 26, 36, lit ? col : g_mix(C_BG2, col, 50));
                g_text_c(x + 13, y + 12, drum_code(t, i, 0), lit ? C_INK : C_TXT, T_CROP | T_BOLD);
            }
        } else
            draw_smp(t, col, sp == SP_SMP);
    }
    if (sp == SP_TABLE) {                       /* the table takes the page down to the bottom line */
        draw_table(t, col);
        return;
    }
    if (sp == SP_SMP) {
        smp_cards(t);
        return;
    }
    if (sp == SP_OP || sp == SP_OPENV) {        /* an operator's four knobs */
        for (i = 0; i < 4u; i++) {
            uint32_t k = (sp == SP_OP ? OPK_LEVEL : OPK_ATT) + i;
            int32_t v = op_get(fm_op, k);
            op_text(k, v, s);
            if (g_vis(144, 80)) {
                g_rect((int32_t)i * 60, 144, 60, 80, C_BG);
                card(i, 1 + (int32_t)i * 60, 146, 76, OPK_NAMES[lang][k], s, v - OPK_LO[k], OPK_HI[k] - OPK_LO[k]);
            }
            hint(i, OPK_NAMES[lang][k], "", s);
        }
        return;
    }
    for (i = 0; i < 4u; i++) {                  /* the four knob cards, each in its knob's colour */
        uint32_t k = first + i;
        const pdesc_t *d = &PDESC[e][k];
        int32_t x = 1 + (int32_t)i * 60, v = p[k];
        if (!g_vis(144, 80)) { if (d->name[0]) { param_text(d, v, s, k == P_DEC || k == P_REL); hint(i, param_name(t, k), "", s); } else hint_none(i); continue; }
        g_rect(x - 1, 144, 60, 80, C_BG);
        if (!d->name[0]) { card(i, x, 146, 76, "", "", 0, 0); hint_none(i); continue; }
        param_text(d, v, s, k == P_DEC || k == P_REL);
        s[8] = 0;
        card(i, x, 146, 76, param_name(t, k), s, v - d->lo, d->hi - d->lo);
        hint(i, param_name(t, k), "", s);
    }
}

/* ---- ENVELOPE: the ADSR, its four segments in the four knobs' colours, the note's dot moving on it */
static void draw_envpage(void)
{
    uint32_t t = trk(), i;
    const int8_t *p = prj.snd[t].p;
    int32_t x0 = 10, y0 = 50, w = 220, h = 104, by = y0 + h;
    int32_t a = p[P_ATT] * 4 + 2, d = p[P_DEC] * 4 + 2, r = p[P_REL] * 4 + 2, sy = by - p[P_SUS] * h / 15;
    int32_t x1 = x0 + a, x2 = x1 + d, x3 = x0 + w - r, x;
    uint16_t col = TRK_COL[t];
    char s[24];
    if (x3 < x2 + 10) x3 = x2 + 10;
    if (g_vis(20, 20)) {
        g_rect(0, 20, 240, 20, C_BG);
        g_rrect(3, 22, 82, 15, col);
        g_icon(6, 25, ENGINE_ICON[ENG(t)], C_INK, 1);
        trk_name(t, s);
        g_text(16, 22, s, C_INK, T_CROP | T_BOLD);
        if (ENG(t) == E_FM) g_text(92, 22, TR("+ OPERATEURS", "+ OPERATORS", "+ OPERADORES"), C_MID, T_CROP);
        else if (ENG(t) == E_KIT || note_mode(t) == NM_SLICE) g_text(92, 22, TR("COUPE LES COUPS", "SHAPES THE HITS", "CORTA GOLPES"), C_MID, T_CROP);
    }
    if (g_vis(40, 124)) {
        int32_t py = by;
        g_rect(0, 40, 240, 124, C_BG);
        g_rrect(4, 42, 232, 120, C_BG2);
        for (x = x0; x <= x0 + w; x++) {        /* the curve, filled under, each segment its knob's colour */
            int32_t y;
            uint32_t seg;
            if (x <= x1) { y = by - (x - x0) * h / (a ? a : 1); seg = 0; }
            else if (x <= x2) { y = y0 + (x - x1) * (sy - y0) / (d ? d : 1); seg = 1; }
            else if (x <= x3) { y = sy; seg = 2; }
            else { y = sy + (x - x3) * (by - sy) / (r ? r : 1); seg = 3; }
            y = clampi(y, y0, by);
            g_blend(x, y, 1, by - y, KNOB_COL[seg], 60);
            g_line(x - 1, py, x, y, KNOB_COL[seg]);
            g_line(x - 1, py - 1, x, y - 1, KNOB_COL[seg]);
            py = y;
        }
        g_rect(x0, by, w + 1, 1, C_RULE);
        {                                       /* the note now */
            uint32_t st = env_stage[t];
            int32_t ev = env_now[t], dx = -1, dy;
            if (st == 1u) dx = x0 + a * ev / 65535;
            else if (st == 2u) dx = x1 + d * (65535 - ev) / (65535 - p[P_SUS] * 4369 + 1);
            else if (st == 3u) dx = x2 + (x3 - x2) / 2 + (int32_t)((fm1_ms / 60u) % 7u) - 3;
            else if (st == 4u) dx = x3 + r - r * ev / (p[P_SUS] * 4369 + 1);
            if (dx >= 0) {
                dx = clampi(dx, x0, x0 + w);
                dy = by - ev * h / 65535;
                g_rect(dx - 3, dy - 3, 7, 7, C_WHITE);
                g_rect(dx - 2, dy - 2, 5, 5, col);
            }
        }
        g_text(x0 + 2, y0 - 6, "A", KNOB_COL[0], T_CROP | T_BOLD);
        g_text(x1 + 2, y0 - 6, "D", KNOB_COL[1], T_CROP | T_BOLD);
        g_text(x2 + (x3 - x2) / 2 - 4, sy - 14 < y0 ? sy + 2 : sy - 14, "S", KNOB_COL[2], T_CROP | T_BOLD);
        g_text(x3 + 2, by - 14, "R", KNOB_COL[3], T_CROP | T_BOLD);
    }
    for (i = 0; i < 4u; i++) {
        uint32_t k = P_ATT + i;
        const pdesc_t *dd = &PDESC[ENG(t)][k];
        param_text(dd, p[k], s, k == P_DEC || k == P_REL);
        if (g_vis(166, 58)) {
            g_rect(i * 60, 164, 60, 60, C_BG);
            card(i, 1 + (int32_t)i * 60, 166, 56, dd->name[lang], s, p[k], 15);
        }
        hint(i, dd->name[lang], "", s);
    }
}

/* ---- MIX: eight strips and the master; ALGORITHM past the eighth track: the master's effect */
static int32_t meter_h(uint32_t lv, int32_t full)
{
    int32_t h = 0;
    while (lv > 64u && h < 30) { lv = (lv * 3u) >> 2; h++; }
    return h * full / 30;
}
static void draw_mix(void)
{
    uint32_t t;
    char s[16];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    for (t = 0; t < NTRK; t++) {
        const int8_t *p = prj.snd[t].p;
        int32_t x = (int32_t)t * 34, h, vy;
        uint16_t col = TRK_COL[t];
        int muted = (prj.mute | live_mute) >> t & 1u;
        if (t == cur_trk) { g_frame(x + 1, 21, 32, 202, C_WHITE); g_frame(x + 2, 22, 30, 200, C_WHITE); }
        g_icon(x + 13, 25, ENGINE_ICON[ENG(t)], muted ? C_FAINT : col, 1);
        h = meter_h(level[t], 126);
        g_rect(x + 9, 38, 16, 126, C_BG2);
        g_rect(x + 9, 164 - h, 16, h, muted ? C_FAINT : g_mix(C_BG, col, 220));
        vy = 164 - p[P_VOL] * 126 / 15;
        g_rect(x + 5, vy - 1, 24, 3, C_WHITE);
        g_rect(x + 5, 172, 24, 1, C_RULE);
        g_rect(x + 16 + p[P_PAN] * 2, 168, 3, 9, KNOB_COL[1]);
        g_rect(x + 5, 181, p[P_ECHO] * 24 / 15 + 1, 4, KNOB_COL[2]);
        g_rect(x + 5, 188, p[P_VERB] * 24 / 15 + 1, 4, KNOB_COL[3]);
        if (p[P_FXT]) g_icon(x + 13, 195, FXT_ICON[clampi(p[P_FXT], 0, FXT_N - 1)], C_GOLD, 1);
        if (muted) g_text_c(x + 17, 207, "M", C_REC, T_CROP | T_BOLD);
        else { fmt_u(s, t + 1u, 0); g_text_c(x + 17, 207, s, col, T_CROP | T_BOLD); }
    }
    {                                           /* the master */
        int32_t x = 204, h = meter_h(level[NTRK], 126);
        int32_t ty = clampi(prj.mfx[0], 0, FXT_N - 1);
        g_rect(x, 21, 1, 202, C_RULE);
        if (cur_trk == MASTER) { g_frame(x + 1, 21, 35, 202, C_WHITE); g_frame(x + 2, 22, 33, 200, C_WHITE); }
        g_text_c(x + 18, 22, "M", C_GOLD, T_CROP | T_BOLD);
        g_rect(x + 9, 38, 18, 126, C_BG2);
        g_rect(x + 9, 164 - h, 18, h, C_GOLD);
        g_icon(x + 10, 172, FXT_ICON[ty], ty ? C_GOLD : C_FAINT, 2);
        g_rect(x + 6, 192, prj.mfx[3] * 24 / 15 + 1, 4, KNOB_COL[3]);
    }
    if (cur_trk == MASTER) {
        const pdesc_t *d = &PDESC[0][P_FXT];
        int32_t ty = clampi(prj.mfx[0], 0, FXT_N - 1);
        fmt_u(s, (uint32_t)prj.mfx[1], 0);
        hint(0, TR("EFFET MASTER", "MASTER FX", "EFECTO MASTER"), "", pd_value(d, ty));
        hint(1, FXA_NAMES[lang][ty], "A", s);
        fmt_u(s, (uint32_t)prj.mfx[2], 0);
        hint(2, FXB_NAMES[lang][ty], "B", s);
        fmt_u(s, (uint32_t)prj.mfx[3], 0);
        hint(3, TR("DOSAGE", "MIX", "MEZCLA"), "M", s);
    } else {
        const int8_t *p = prj.snd[trk()].p;
        char b[8];
        fmt_u(s, (uint32_t)p[P_VOL], 0);
        hint(0, TR("VOLUME", "VOLUME", "VOLUMEN"), "VOL", s);
        param_text(&PDESC[0][P_PAN], p[P_PAN], b, 0);
        hint(1, TR("PANO", "PAN", "PANEO"), "PAN", b);
        fmt_u(s, (uint32_t)p[P_ECHO], 0);
        hint(2, "ECHO", TR("ECH", "ECH", "ECO"), s);
        fmt_u(s, (uint32_t)p[P_VERB], 0);
        hint(3, "REVERB", "REV", s);
    }
}

/* ---- MAGIC */
static void draw_magic(void)
{
    uint32_t i, t = trk();
    char s[16];
    uint16_t mc = PAGE_COL[PG_MAGIC];
    if (g_vis(20, 20)) {
        g_rect(0, 20, 240, 20, C_BG);
        if (mg_scope) {
            g_rrect(3, 22, 120, 15, C_GOLD);
            g_icon(6, 25, ICO_CHAIN, C_INK, 1);
            g_text(16, 22, TR("TOUT LE MOTIF", "WHOLE PATTERN", "TODO EL PATRON"), C_INK, T_CROP | T_BOLD);
        } else {
            g_rrect(3, 22, 82, 15, TRK_COL[t]);
            g_icon(6, 25, ENGINE_ICON[ENG(t)], C_INK, 1);
            trk_name(t, s);
            g_text(16, 22, s, C_INK, T_CROP | T_BOLD);
        }
    }
    if (g_vis(38, 2)) g_rect(0, 38, 240, 2, C_BG);
    for (i = 0; i < MG_N; i++) {
        int32_t x, y;
        tile_xy(i, &x, &y, 40, 46);
        if (g_vis(y - 2, 48)) g_rect(x - 1, y - 2, 60, 48, C_BG);
        draw_tile(x, y, 44, i, mc, 0, MAGIC[i].icon, MAGIC[i].name[lang], MAGIC[i].sub[lang]);
    }
    fmt_u(s, mg_inten, 0);
    hint(0, TR("FORCE", "AMOUNT", "FUERZA"), TR("FOR", "AMT", "FZA"), s);
    fmt_u(s, mg_seed, 0);
    hint(1, TR("DE", "DIE", "DADO"), TR("DE", "DIE", "DAD"), s);
    hint(2, TR("PORTEE", "SCOPE", "ALCANCE"), "", L_SCOPE[mg_scope]);
    hint_none(3);
}

/* ---- EFFECTS: the bank, the target, the scope of the bus, the tiles alive while they play */
static uint32_t lfx_phase(uint32_t id)          /* how far the effect is in its cycle, 0..255 (the tiles' bar) */
{
    uint32_t g = g_rowlen ? g_rowlen : 1u;
    if ((BF_MASK >> id & 1u) && bf.act && bf.id == id && bf.L)
        return ((afr - bf.S) % bf.L) * 255u / bf.L;
    if (id == LF_LPF) return (127u - lpf_note) * 255u / 93u;
    if (id == LF_HPF) return hpf_note * 255u / 100u;
    if (id == LF_GATE32) g /= 2u;
    return (g_beatpos % g) * 255u / g;
}
static void draw_fx(void)
{
    uint32_t i, t, on = lfx_on;
    uint16_t col = LBANK_COL[fx_bank];
    char s[16];
    if (g_vis(20, 20)) {
        g_rect(0, 20, 240, 20, C_BG);
        for (i = 0; i < 2u; i++) {
            int32_t x = 3 + (int32_t)i * 56;
            if (i == fx_bank) g_rrect(x, 22, 54, 15, LBANK_COL[i]);
            else g_frame(x, 22, 54, 15, g_mix(C_BG, LBANK_COL[i], 120));
            g_text_c(x + 27, 22, LBANK_NAME[i], i == fx_bank ? C_INK : LBANK_COL[i], T_CROP | T_BOLD);
        }
        if (rec && sq_play) { g_rrect(116, 22, 28, 15, C_REC); g_text_c(130, 22, "REC", C_WHITE, T_CROP); }
        g_icon(150, 25, ICO_TARGET, KNOB_COL[3], 1);   /* where they play: KNOB 4 */
        if (lfx_target) {
            g_rrect(160, 22, 77, 15, TRK_COL[lfx_target - 1u]);
            trk_name(lfx_target - 1u, s);
            g_text(164, 22, s, C_INK, T_CROP | T_BOLD);
        } else {
            g_rrect(160, 22, 77, 15, C_GOLD);
            g_text(164, 22, "MASTER", C_INK, T_CROP | T_BOLD);
        }
    }
    if (g_vis(40, 20)) {                        /* the bus, as it sounds */
        uint32_t w = scope_w, x;
        int32_t py = 50;
        g_rect(0, 40, 240, 20, C_BG);
        g_rrect(2, 41, 236, 18, C_BG2);
        for (x = 0; x < 232u; x++) {
            int32_t v = scope[(w + 8u + x) % 240u], y = 50 - v * 8 / 32768;
            if (x) g_line(4 + (int32_t)x - 1, py, 4 + (int32_t)x, y, on ? col : C_MID);
            py = y;
        }
    }
    for (i = 0; i < 16u; i++) {
        uint32_t id = fx_bank * 16u + i, lit = on >> id & 1u;
        int32_t x, y;
        tile_xy(i, &x, &y, 62, 40);
        if (g_vis(y - 2, 42)) g_rect(x - 1, y - 2, 60, 42, C_BG);
        draw_tile(x, y, 38, i, col, lit, LFX[id].icon, LFX[id].name[lang], LFX[id].sub[lang]);
        if (lit && g_vis(y + 34, 3)) {          /* alive: where it is in its cycle */
            uint32_t ph = lfx_phase(id);
            g_rect(x + 3, y + 34, 52, 2, g_mix(col, C_INK, 120));
            g_rect(x + 3, y + 34, (int32_t)(ph * 52u / 255u), 2, C_WHITE);
        }
    }
    for (t = 0; t < NTRK; t++) { (void)t; }
    hint(0, TR("BANQUE", "BANK", "BANCO"), "", LBANK_NAME[fx_bank]);
    hint_none(1);
    hint(2, TR("ENREG. LIVE", "REC LIVE", "GRABAR LIVE"), "REC", rec ? TR("OUI", "YES", "SI") : TR("NON", "NO", "NO"));
    if (lfx_target) { s[0] = 'T'; s[1] = (char)('0' + lfx_target); s[2] = 0; }
    else { s[0] = 0; str_cat(s, "MAST"); }
    hint(3, TR("CIBLE", "TARGET", "DESTINO"), "", s);
}

/* ---- SENDS (FX, first page): for each track, how much goes to the saturation, the echo and the reverb. KNOB 1, 2, 3 for the
 * track chosen with ALGORITHM; 0 is off */
static void draw_send(void)
{
    static const char *const CH[LANG_N][3] = {{"SATURE", "ECHO", "REVERB"}, {"DRIVE", "ECHO", "REVERB"}, {"SATURA", "ECO", "REVERB"}};
    uint32_t t, k;
    char s[16];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    for (k = 0; k < 3u; k++)
        g_text_c(108 + (int32_t)k * 52, 22, CH[lang][k], KNOB_COL[k], T_CROP | T_BOLD);
    for (t = 0; t < NTRK; t++) {
        int32_t y = 40 + (int32_t)t * 30;
        const int8_t *p = prj.snd[t].p;
        uint8_t v[3];
        if (!g_vis(y, 30)) continue;
        v[0] = (uint8_t)(prj.snd[t].sat & 15u);
        v[1] = (uint8_t)clampi(p[P_ECHO], 0, 15);
        v[2] = (uint8_t)clampi(p[P_VERB], 0, 15);
        if (t == trk()) { g_frame(0, y, 240, 29, C_WHITE); }
        g_rrect(3, y + 3, 76, 15, TRK_COL[t]);
        g_icon(6, y + 6, ENGINE_ICON[ENG(t)], C_INK, 1);
        trk_name(t, s);
        g_text(16, y + 3, s, C_INK, T_CROP | T_BOLD);
        for (k = 0; k < 3u; k++) {
            int32_t x = 84 + (int32_t)k * 52;
            g_rrect(x, y + 3, 48, 22, C_BG2);
            if (v[k]) g_rect(x + 2, y + 21, 44 * v[k] / 15, 3, KNOB_COL[k]);
            fmt_u(s, v[k], 0);
            g_text_c(x + 24, y + 4, v[k] ? s : "-", v[k] ? C_WHITE : C_FAINT, T_CROP | T_BOLD);
        }
    }
    {
        const int8_t *p = prj.snd[trk()].p;
        fmt_u(s, prj.snd[trk()].sat, 0);
        hint(0, CH[lang][0], "", s);
        fmt_u(s, (uint32_t)p[P_ECHO], 0);
        hint(1, CH[lang][1], "", s);
        fmt_u(s, (uint32_t)p[P_VERB], 0);
        hint(2, CH[lang][2], "", s);
        hint_none(3);
    }
}

/* the tools (SAVE held) over the page, and the yellow bar of a hold that is about to open something */
static void draw_tools(void)
{
    uint32_t i;
    char s[24];
    if (!tools_open || !g_vis(30, 190)) return;
    g_rrect(54, 34, 132, 182, C_GOLD);
    g_rrect(55, 35, 130, 180, C_BG2);
    for (i = 0; i < TOOL_N; i++) {
        int32_t y = 40 + (int32_t)i * 21;
        if (i == tools_sel) g_rrect(60, y, 120, 19, C_GOLD);
        g_text_c(120, y + 1, TOOL_NAMES[lang][i], i == tools_sel ? C_INK : C_TXT, T_CROP | T_BOLD);
    }
    s[0] = 0;                                   /* what CLEAR, COPY, PASTE, RANDOM act on (K3) */
    str_cat(s, TR("SUR ", "ON ", "EN "));
    if (mg_scope) str_cat(s, L_SCOPE[1]);
    else trk_name(trk(), s + str_len(s));
    g_text_c(120, 190, s, KNOB_COL[2], T_CROP);
}
static void draw_first(void)                    /* an empty song on TRACKS: three steps, until the first note */
{
    static const char *const L[LANG_N][4] = {
        {"KAWA - MORCEAU VIDE", "1. REC  2. JOUE LES TOUCHES", "3. PLAY POUR ECOUTER", "HOME TENU: AIDE"},
        {"KAWA - EMPTY SONG", "1. REC  2. PLAY THE KEYS", "3. PLAY TO LISTEN", "HOME HELD: HELP"},
        {"KAWA - CANCION VACIA", "1. REC  2. TOCA LAS TECLAS", "3. PLAY PARA ESCUCHAR", "HOME PULSADO: AYUDA"}};
    uint32_t i;
    if (page != PG_TRACK || !song_empty || rec || sq_play || tools_open || !g_vis(70, 92)) return;
    g_rrect(8, 72, 224, 88, C_GOLD);
    g_rrect(9, 73, 222, 86, C_BG2);
    for (i = 0; i < 4u; i++)
        g_text_c(120, 80 + (int32_t)i * 18, L[lang][i], i == 0u ? C_GOLD : i == 3u ? C_MID : C_TXT, T_CROP | (i == 0u ? T_BOLD : 0u));
}
static void draw_hold(void)
{
    uint32_t since = 0, need = 0;
    const char *what = "";
    if (home_held && !home_did && page != PG_HELP) { since = fm1_ms - home_down_ms; need = HOLD_HOME_MS; what = TR("AIDE", "HELP", "AYUDA"); }
    else if (fx_down && page == PG_SEND) { since = fm1_ms - fx_down_ms; need = HOLD_FX_MS; what = "LIVE"; }
    else if (save_held && !tools_open && !save_did) { since = fm1_ms - save_down_ms; need = HOLD_SAVE_MS; what = TR("OUTILS", "TOOLS", "HERRAM."); }
    if (!need || since < 100u || since >= need || !g_vis(190, 26)) return;
    g_text_c(120, 190, what, C_GOLD, T_CROP | T_BOLD);
    g_rrect(66, 205, 108, 10, C_BG);
    g_frame(67, 206, 106, 8, C_GOLD);
    g_rect(69, 208, (int32_t)(102u * since / need), 4, C_GOLD);
}

/* ---- SCALE */
static void draw_scale(void)
{
    uint32_t i, mask = SCALES[prj.scale % 12u].mask;
    char s[16];
    static const uint8_t WHITE7[7] = {0, 2, 4, 5, 7, 9, 11}, BLACK5[5] = {1, 3, 6, 8, 10};
    static const uint8_t BLACK_X[5] = {0, 1, 3, 4, 5};
    uint16_t pc = PAGE_COL[PG_SCALE];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    s[0] = 0;
    str_cat(s, NOTE_NAME[prj.root % 12u]);
    if (s[1] == '-') s[1] = 0;
    g_text_l(16, 32, s, C_GOLD);
    g_text(64, 34, SCALES[prj.scale % 12u].name[lang], C_TXT, T_BOLD);
    g_text(64, 50, prj.lock ? TR("VERROU: LES TOUCHES", "LOCKED: THE KEYS", "FIJA: LAS TECLAS")
                            : TR("LIBRE: CHROMATIQUE", "FREE: CHROMATIC", "LIBRE: CROMATICA"), C_MID, T_CROP);
    g_text(64, 62, prj.lock ? TR("RESTENT DANS LA GAMME", "STAY IN THE SCALE", "QUEDAN EN LA ESCALA")
                            : TR("TOUTES LES NOTES", "ALL THE NOTES", "TODAS LAS NOTAS"), C_FAINT, T_CROP);
    for (i = 0; i < 7u; i++) {                  /* one octave from C, the scale's notes lit */
        uint32_t n = WHITE7[i], in = mask >> ((n + 12u - prj.root) % 12u) & 1u;
        int32_t x = 8 + (int32_t)i * 32;
        g_rrect(x, 86, 30, 96, in ? (n == prj.root ? C_GOLD : g_mix(C_TXT, pc, 60)) : C_RULE);
        if (n == prj.root) g_text_c(x + 15, 164, "*", C_INK, T_BOLD);
    }
    for (i = 0; i < 5u; i++) {
        uint32_t n = BLACK5[i], in = mask >> ((n + 12u - prj.root) % 12u) & 1u;
        int32_t x = 8 + (int32_t)BLACK_X[i] * 32 + 22;
        g_rrect(x, 86, 18, 58, in ? (n == prj.root ? C_GOLD : pc) : C_BG2);
        g_frame(x, 86, 18, 58, C_BG);
    }
    g_text(8, 192, TR("UNE TOUCHE = TONIQUE", "A KEY = THE ROOT", "UNA TECLA = TONICA"), C_FAINT, T_CROP);
    {
        char a[4] = {0}, b[8];
        str_cat(a, NOTE_NAME[prj.root % 12u]);
        if (a[1] == '-') a[1] = 0;
        hint(0, TR("TONIQUE", "ROOT", "TONICA"), TR("TON", "KEY", "TON"), a);
        hint(1, TR("GAMME", "SCALE", "ESCALA"), "", SCALES[prj.scale % 12u].name[lang]);
        hint(2, TR("VERROU", "LOCK", "FIJAR"), "", prj.lock ? TR("VERROU", "LOCK", "FIJA") : TR("LIBRE", "FREE", "LIBRE"));
        fmt_u(b, prj.step, 0);
        hint(3, TR("PAS DU CURSEUR", "CURSOR STEP", "PASO CURSOR"), TR("PAS", "STP", "PAS"), b);
    }
}

/* ---- SONG: the entries (a pattern, played N times) */
static void draw_song(void)
{
    uint32_t i, top = song_cur >= 16u ? ((song_cur - 8u) / 8u) * 8u : 0u;
    char s[16];
    uint16_t sc = PAGE_COL[PG_SONG];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    if (g_vis(22, 50)) {
        fmt_u(s, bpm_now(), 0);
        g_text_l(8, 24, s, clk_in ? C_GO : C_WHITE);
        g_text(64, 26, clk_in ? "BPM IN" : "BPM", clk_in ? C_GO : C_FAINT, T_CROP);
        s[0] = 0;
        if (prj.grv.on) str_cat(s, "GROOVE");   /* (the groove plays in place of the swing) */
        else str_cat(fmt_u(str_cat(s, "SWING "), prj.swing, 0), "%");
        g_text(64, 40, s, prj.grv.on ? C_GOLD : C_MID, T_CROP);
        g_rrect(150, 24, 84, 18, prj.mode ? sc : C_BG2);
        g_text_c(192, 27, prj.mode ? TR("CHAINE", "SONG", "CANCION") : TR("BOUCLE", "LOOP", "BUCLE"), prj.mode ? C_INK : C_TXT, T_CROP | T_BOLD);
        g_text(150, 44, TR("ARP: MODE", "ARP: MODE", "ARP: MODO"), C_FAINT, T_CROP);
        g_rect(4, 60, 232, 1, C_RULE);
    }
    for (i = 0; i < 24u; i++) {
        uint32_t k = top + i;
        int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 66 + (int32_t)(i / 8u) * 44;
        if (!g_vis(y, 42)) continue;
        if (k < prj.song_len) {
            uint32_t pn = prj.song[k].pat, rep = prj.song[k].rep ? prj.song[k].rep : 1u;
            int playing = sq_play && prj.mode && k == sq_songpos;
            uint16_t pcol = TRK_COL[pn % NTRK];
            g_rrect(x, y, 27, 38, playing ? C_WHITE : g_mix(C_BG2, pcol, 120));
            fmt_u(s, pn + 1u, 2);
            g_text_c(x + 14, y + 4, s, playing ? C_INK : C_WHITE, T_CROP | T_BOLD);
            s[0] = 'x';
            fmt_u(s + 1, rep, 0);
            g_text_c(x + 14, y + 20, s, playing ? C_INK : C_GOLD, T_CROP);
            if (playing && rep > 1u) {           /* the repeats done */
                uint32_t j;
                for (j = 0; j < rep && j < 8u; j++)
                    g_rect(x + 3 + (int32_t)j * 3, y + 33, 2, 2, j <= sq_rep ? C_REC : C_RULE);
            }
        } else if (k == prj.song_len)
            g_text_c(x + 14, y + 12, "+", C_FAINT, T_CROP | T_BOLD);
        if (k == song_cur) { g_frame(x - 1, y - 1, 29, 40, C_GOLD); g_frame(x - 2, y - 2, 31, 42, C_GOLD); }
    }
    {
        char a[8], b[8], c[8], d[8];
        uint32_t rep = song_cur < prj.song_len ? prj.song[song_cur].rep : 0u;
        fmt_u(a, prj.bpm, 0);
        fmt_u(b, prj.swing, 0);
        c[0] = 'x';
        fmt_u(c + 1, rep ? rep : 1u, 0);
        fmt_u(d, plen(), 0);
        hint(0, "TEMPO", "BPM", a);
        hint(1, "SWING", "SW", b);
        hint(2, TR("REPETER", "REPEAT", "REPETIR"), TR("REP", "REP", "REP"), song_cur < prj.song_len ? c : "-");
        hint(3, TR("PAS DU MOTIF", "PATTERN STEPS", "PASOS PATRON"), TR("PAS", "STP", "PAS"), d);
    }
}

/* ---- PROJECTS: a menu. SELECT moves, OCT+ OK, OCT- back. The song is kept in a slot (1..3): SAVE writes
 * it there again, SAVE AS picks a slot and a name; slot 0 is the autosave */
enum { PM_NEW, PM_SAVE, PM_SAVEAS, PM_LOAD, PM_RENAME, PM_DEMO, PM_N };
static const char *const PM_NAMES[LANG_N][PM_N] = {
    {"NOUVEAU MORCEAU", "SAUVER", "SAUVER SOUS...", "CHARGER...", "RENOMMER...", "DEMO"},
    {"NEW SONG", "SAVE", "SAVE AS...", "LOAD...", "RENAME...", "DEMO"},
    {"NUEVA CANCION", "GUARDAR", "GUARDAR COMO...", "CARGAR...", "RENOMBRAR...", "DEMO"}};
static const uint8_t PM_ICON[PM_N] = {ICO_NOTE, ICO_DOWN, ICO_COPY, ICO_UP, ICO_SHAPE, ICO_PLAY};
enum { PMS_MENU, PMS_SLOT, PMS_NAME, PMS_ASK };
static uint8_t pm_sel = PM_SAVE, pm_state, pm_act, pm_slot = 1, proj_cur = 0xFF, nm_pos;
static char nm_buf[16];
static const char NM_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!?";

static void pm_name_turn(uint32_t knob, int32_t d)   /* the name: K1 a letter, K2 a random name, K3 cleared */
{
    uint32_t i, n = sizeof NM_CHARS - 1u, c = 0;
    if (knob == 0u) {
        for (i = 0; i < n; i++) if (NM_CHARS[i] == nm_buf[nm_pos]) { c = i; break; }
        c = (uint32_t)(((int32_t)c + d % (int32_t)n + (int32_t)n) % (int32_t)n);
        for (i = 0; i < nm_pos; i++) if (!nm_buf[i]) nm_buf[i] = ' ';
        nm_buf[nm_pos] = NM_CHARS[c];
    } else if (knob == 1u)
        name_new(nm_buf);
    else if (knob == 2u)
        memset(nm_buf, 0, sizeof nm_buf);
    nm_buf[15] = 0;
}

static void slots_scan(void)
{
    uint32_t k;
    for (k = 0; k < NSLOT; k++)
        names_ok[k] = (uint8_t)slot_valid(k, slot_name[k]);
}

static void draw_proj(void)
{
    uint32_t k;
    char s[32];
    uint16_t pc = PAGE_COL[PG_PROJ];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    if (g_vis(20, 22)) {                        /* the song: its name, saved or not, its slot */
        g_text(6, 24, prj.name, C_GOLD, T_CROP | T_BOLD);
        s[0] = 0;
        if (proj_cur < NSLOT) { s[0] = '['; fmt_u(s + 1, proj_cur, 0); str_cat(s, "]"); }
        else str_cat(s, "[-]");
        if (dirty) str_cat(s, " *");
        g_text(236 - g_text_w(s), 24, s, dirty ? C_REC : C_MID, T_CROP | T_BOLD);
        g_rect(4, 40, 232, 1, C_RULE);
    }
    if (pm_state == PMS_MENU) {
        for (k = 0; k < PM_N; k++) {
            int32_t y = 46 + (int32_t)k * 25;
            int on = k == pm_sel;
            if (!g_vis(y, 23)) continue;
            g_rrect(6, y, 228, 22, on ? pc : C_BG2);
            g_icon(14, y + 7, PM_ICON[k], on ? C_INK : pc, 1);
            g_text(28, y + 4, PM_NAMES[lang][k], on ? C_INK : C_TXT, T_CROP | T_BOLD);
            if (k == PM_SAVE) {
                s[0] = 0;
                if (proj_cur >= 1u && proj_cur < NSLOT) { str_cat(s, "> "); fmt_u(s + 2, proj_cur, 0); }
                else str_cat(s, "> ?");
                g_text(226 - g_text_w(s), y + 4, s, on ? C_INK : C_FAINT, T_CROP);
            }
        }
    } else if (pm_state == PMS_SLOT) {
        g_text(8, 46, pm_act == PM_LOAD ? TR("CHARGER DE :", "LOAD FROM:", "CARGAR DE:") : TR("SAUVER DANS :", "SAVE INTO:", "GUARDAR EN:"),
               C_MID, T_CROP);
        for (k = 0; k < NSLOT; k++) {
            int32_t y = 62 + (int32_t)k * 34;
            int on = k == pm_slot, off = pm_act != PM_LOAD && k == 0u;
            if (!g_vis(y, 30)) continue;
            g_rrect(6, y, 228, 30, on ? g_mix(C_BG2, pc, 120) : C_BG2);
            if (on) g_frame(6, y, 228, 30, C_WHITE);
            s[0] = 0;
            if (k == 0u) str_cat(s, "AUTO");
            else fmt_u(s, k, 0);
            g_text(14, y + 9, s, off ? C_RULE : C_MID, T_CROP | T_BOLD);
            g_text(62, y + 9, names_ok[k] ? slot_name[k] : TR("- VIDE -", "- EMPTY -", "- VACIO -"),
                   off ? C_RULE : names_ok[k] ? C_TXT : C_FAINT, T_CROP);
        }
    } else if (pm_state == PMS_NAME) {
        g_text(8, 50, TR("LE NOM :", "THE NAME:", "EL NOMBRE:"), C_MID, T_CROP);
        for (k = 0; k < 15u; k++) {             /* one box a letter, the cursor's lit */
            int32_t x = 8 + (int32_t)k * 15, y = 74;
            char c[2] = {nm_buf[k] ? nm_buf[k] : ' ', 0};
            int on = k == nm_pos;
            if (!g_vis(y, 26)) continue;
            g_rrect(x, y, 14, 24, on ? KNOB_COL[0] : C_BG2);
            g_text(x + 3, y + 5, c, on ? C_INK : C_WHITE, T_CROP | T_BOLD);
        }
        g_text(8, 112, TR("SELECT: LA PLACE", "SELECT: THE PLACE", "SELECT: EL SITIO"), C_FAINT, T_CROP);
        g_text(8, 126, TR("K1: LA LETTRE", "K1: THE LETTER", "K1: LA LETRA"), KNOB_COL[0], T_CROP);
        g_text(8, 140, TR("K2: UN NOM AU HASARD", "K2: A RANDOM NAME", "K2: UN NOMBRE AL AZAR"), KNOB_COL[1], T_CROP);
        g_text(8, 154, TR("K3: TOUT EFFACER", "K3: CLEAR IT", "K3: BORRARLO"), KNOB_COL[2], T_CROP);
    } else if (g_vis(70, 100)) {                /* PMS_ASK */
        g_rrect(14, 76, 212, 96, C_GOLD);
        g_rrect(16, 78, 208, 92, C_BG2);
        g_text_c(120, 88, pm_act == PM_SAVEAS ? TR("CET EMPLACEMENT EST PRIS", "THIS SLOT IS TAKEN", "ESTA RANURA ESTA USADA")
                                              : TR("LE MORCEAU N'EST PAS SAUVE", "THE SONG IS NOT SAVED", "LA CANCION NO ESTA GUARDADA"),
                 C_GOLD, T_CROP | T_BOLD);
        g_text_c(120, 108, pm_act == PM_SAVEAS ? TR("L'ECRASER ?", "WRITE OVER IT?", "SOBREESCRIBIR?")
                                               : TR("CONTINUER QUAND MEME ?", "GO ON ANYWAY?", "SEGUIR DE TODOS MODOS?"),
                 C_TXT, T_CROP);
        g_text_c(120, 140, TR("OCT+ OUI     OCT- NON", "OCT+ YES     OCT- NO", "OCT+ SI     OCT- NO"), C_WHITE, T_CROP | T_BOLD);
    }
    if (g_vis(200, 22)) {
        g_rect(0, 200, 240, 22, C_BG);
        g_rrect(6, 202, 110, 18, C_GO);
        g_text_c(61, 204, TR("OCT+ : OK", "OCT+ : OK", "OCT+ : OK"), C_INK, T_CROP | T_BOLD);
        g_rrect(124, 202, 110, 18, C_BG2);
        g_text_c(179, 204, TR("OCT- : RETOUR", "OCT- : BACK", "OCT- : VOLVER"), C_TXT, T_CROP | T_BOLD);
    }
    if (pm_state == PMS_NAME) {
        hint(0, TR("LETTRE", "LETTER", "LETRA"), "", "A-Z");
        hint(1, TR("HASARD", "RANDOM", "AZAR"), "", "?");
        hint(2, TR("EFFACER", "CLEAR", "BORRAR"), "", "X");
        hint_none(3);
    } else {
        hint(0, pm_state == PMS_SLOT ? TR("EMPLACEMENT", "SLOT", "RANURA") : TR("CHOIX", "CHOICE", "ELECCION"), "", "");
        hint_none(1);
        hint_none(2);
        hint_none(3);
    }
}

/* ---- GROOVE (GLO twice): the ticks of each row in turn, 6 a straight 16th; on, it plays in place of the swing */
static uint8_t gr_cur, gr_preset;
static void draw_groove(void)
{
    const groove_t *g = &prj.grv;
    uint32_t i, len = (uint32_t)clampi(g->len, 1, 16), sum = 0, play = sq_play && g->on ? sq_gstep : 99u, acc = 0;
    uint16_t pc = PAGE_COL[PG_GROOVE];
    char s[32];
    for (i = 0; i < len; i++)
        sum += (uint32_t)clampi(g->tick[i], 1, 12);
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    if (g_vis(22, 20)) {
        g_rrect(4, 24, 72, 16, g->on ? pc : C_BG2);
        g_text_c(40, 26, g->on ? TR("ACTIF", "ON", "ACTIVO") : TR("ARRETE", "OFF", "APAGADO"), g->on ? C_INK : C_TXT,
                 T_CROP | T_BOLD);
        g_text(84, 26, GROOVE_NAMES[lang][gr_preset % 6u], C_MID, T_CROP);
    }
    if (g_vis(44, 12)) {                        /* what it adds up to: as many ticks as straight rows keeps the bar */
        s[0] = 0;
        str_cat(fmt_u(s, sum, 0), TR(" TICKS EN ", " TICKS IN ", " TICKS EN "));
        str_cat(fmt_u(s + str_len(s), len, 0), TR(" LIGNES", " ROWS", " FILAS"));
        g_text(8, 44, s, sum == len * 6u ? C_GOLD : C_REC, T_CROP);
    }
    for (i = 0; i < 16u; i++) {                 /* the steps: a bar of 1..12 ticks, the straight 6 dotted */
        int32_t x = 1 + (int32_t)i * 15, tk = clampi(g->tick[i], 1, 12), h = tk * 8, in = i < len;
        uint16_t c = i == play ? C_WHITE : in ? g_mix(C_BG, pc, i == gr_cur ? 255 : 170) : C_RULE;
        if (!g_vis(58, 118)) break;
        g_rect(x, 62, 13, 96, in ? C_BG2 : C_BG);
        g_rect(x, 158 - h, 13, h, c);
        if (i == gr_cur) g_frame(x - 1, 60, 15, 100, KNOB_COL[0]);
        g_rect(x, 158 - 48, 13, 1, in ? C_FAINT : C_RULE);
        fmt_u(s, (uint32_t)tk, 0);
        g_text_c(x + 7, 162, s, in ? (i == gr_cur ? C_WHITE : C_TXT) : C_RULE, T_CROP);
    }
    if (g_vis(182, 30)) {                       /* one turn of it as it plays: each row its length */
        g_text(8, 180, TR("UN TOUR:", "ONE TURN:", "UNA VUELTA:"), C_FAINT, T_CROP);
        for (i = 0; i < len; i++) {
            uint32_t x0 = 4u + acc * 232u / sum, x1;
            acc += (uint32_t)clampi(g->tick[i], 1, 12);
            x1 = 4u + acc * 232u / sum;
            g_rect((int32_t)x0, 196, (int32_t)(x1 - x0) - 1, 12, i == play ? C_WHITE : (i & 1u) ? g_mix(C_BG, pc, 120) : pc);
        }
        for (i = 0; i <= len; i++)              /* the straight rows under it */
            g_rect(4 + (int32_t)(i * 6u * 232u / (sum > len * 6u ? sum : len * 6u)), 210, 1, 4, C_FAINT);
    }
    fmt_u(s, (uint32_t)clampi(g->tick[gr_cur % 16u], 1, 12), 0);
    hint(0, "TICKS", "", s);
    fmt_u(s, len, 0);
    hint(1, TR("LONGUEUR", "LENGTH", "LARGO"), "", s);
    hint(2, TR("MODELE", "PRESET", "MODELO"), "", GROOVE_NAMES[lang][gr_preset % 6u]);
    hint(3, "GROOVE", "", g->on ? TR("OUI", "ON", "SI") : TR("NON", "OFF", "NO"));
}

/* ---- OPTIONS (GLO three times): the theme, the language, the MIDI clock */
static const char *clock_name(void)
{
    return set.clock == 2u ? TR("ENTREE", "IN", "ENTRADA") : set.clock ? TR("SORTIE", "OUT", "SALIDA") : TR("NON", "OFF", "NO");
}
static const char *const FONT_NAMES[LANG_N][NFONT] = {{"BASE", "DOUCE", "CLAIRE", "8-BIT"},
                                                       {"BASIC", "SMOOTH", "CLEAR", "8-BIT"},
                                                       {"BASE", "SUAVE", "CLARA", "8-BIT"}};
static void draw_opt(void)
{
    uint32_t k, i;
    char s[24];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    g_text(8, 22, TR("THEME", "THEME", "TEMA"), KNOB_COL[0], T_CROP | T_BOLD);
    for (k = 0; k < NTHEME; k++) {              /* each theme as a little screen */
        const theme_t *th = &THEMES[k];
        int32_t x = 8 + (int32_t)k * 76, y = 37;
        if (!g_vis(y - 3, 50)) continue;
        if (k == theme) { g_frame(x - 2, y - 2, 72, 48, C_WHITE); g_frame(x - 3, y - 3, 74, 50, KNOB_COL[0]); }
        g_rect(x, y, 68, 44, th->bg);
        g_rect(x, y, 68, 7, th->bg2);
        for (i = 0; i < NTRK; i++)
            g_rect(x + 2 + (int32_t)i * 8, y + 9, 6, 18 - (int32_t)((i * 5u) % 12u), th->trk[i]);
        g_rect(x + 2, y + 29, 40, 2, th->gold);
        g_text_c(x + 34, y + 31, th->name, th->txt, T_CROP);
    }
    g_text(8, 85, TR("LANGUE", "LANGUAGE", "IDIOMA"), KNOB_COL[1], T_CROP | T_BOLD);
    for (k = 0; k < LANG_N; k++) {
        int32_t x = 8 + (int32_t)k * 76;
        g_rrect(x, 98, 68, 16, k == lang ? KNOB_COL[1] : C_BG2);
        g_text_c(x + 34, 98, k == 0u ? "FRANCAIS" : k == 1u ? "ENGLISH" : "ESPANOL", k == lang ? C_INK : C_TXT, T_CROP | T_BOLD);
    }
    s[0] = 0;                                   /* the fonts, each written in itself */
    str_cat(str_cat(s, TR("POLICE: ", "FONT: ", "FUENTE: ")), FONT_NAMES[lang][g_font]);
    g_text(8, 117, s, KNOB_COL[3], T_CROP | T_BOLD);
    for (k = 0; k < NFONT; k++) {
        const felucca_font_t *keep = g_fs;
        int32_t x = 6 + (int32_t)k * 58;
        int on = k == g_font;
        g_rrect(x, 130, 54, 18, on ? KNOB_COL[3] : C_BG2);
        g_fs = FONTS_S[k];
        g_text_c(x + 27, 131, "Aa 12", on ? C_INK : C_TXT, T_CROP);
        g_fs = keep;
    }
    {                                           /* the metronome (ALGO: off, on, only while REC) and its volume (PRESETS) */
        static const char *const MV[LANG_N][3] = {{"NON", "OUI", "REC"}, {"OFF", "ON", "REC"}, {"NO", "SI", "REC"}};
        static const char *const VV[LANG_N][3] = {{"FAI", "MOY", "FORT"}, {"LOW", "MED", "HIGH"}, {"BAJ", "MED", "ALT"}};
        s[0] = 0;
        g_text(8, 151, TR("METRONOME", "METRONOME", "METRONOMO"), C_MID, T_CROP | T_BOLD);
        g_text(86, 151, "ALGO", C_FAINT, T_CROP);
        g_text(124, 151, TR("VOLUME", "VOLUME", "VOLUMEN"), C_MID, T_CROP | T_BOLD);
        g_text(180, 151, "PRESETS", C_FAINT, T_CROP);
        for (k = 0; k < 3u; k++) {
            int32_t x = 6 + (int32_t)k * 38, y = 164;
            g_rrect(x, y, 36, 16, k == set.metro ? C_GOLD : C_BG2);
            g_text_c(x + 18, y, MV[lang][k], k == set.metro ? C_INK : C_TXT, T_CROP | T_BOLD);
            x = 124 + (int32_t)k * 38;
            g_rrect(x, y, 36, 16, k == set.mvol ? C_GOLD : C_BG2);
            g_text_c(x + 18, y, VV[lang][k], k == set.mvol ? C_INK : C_TXT, T_CROP | T_BOLD);
        }
    }
    {                                           /* the MIDI clock: out, or in (its tempo leads) */
        char c[32];
        c[0] = 0;
        str_cat(c, TR("HORLOGE MIDI: ", "MIDI CLOCK: ", "RELOJ MIDI: "));
        str_cat(c, clock_name());
        if (clk_in && clk_in_tick) { str_cat(c, " "); fmt_u(c + str_len(c), bpm_now(), 0); }
        g_text_c(120, 184, c, set.clock ? KNOB_COL[2] : C_FAINT, T_CROP);
    }
    g_text_c(120, 197, "KAWA " APP_VER " BETA by Omnipotard", C_MID, T_CROP);
    {                                           /* the audio interrupt's load (for the beta's testers) */
        char c[32];
        c[0] = 0;
        str_cat(fmt_u(str_cat(c, "CPU "), cpu_pct, 0), "%  MAX ");
        str_cat(fmt_u(c + str_len(c), cpu_max, 0), "%");
        if (cpu_shed) { str_cat(c, "  ECO "); fmt_u(c + str_len(c), cpu_shed, 0); }
        if (audio_late) { str_cat(c, " !"); fmt_u(c + str_len(c), audio_late > 99u ? 99u : audio_late, 0); }
        g_text_c(120, 210, c, cpu_max > 70u ? C_REC : cpu_max > 45u ? C_GOLD : C_GO, T_CROP);
    }
    hint(0, TR("THEME", "THEME", "TEMA"), "", THEMES[theme].name);
    hint(1, TR("LANGUE", "LANGUAGE", "IDIOMA"), "", LANG_CODE[lang]);
    hint(2, TR("HORLOGE MIDI", "MIDI CLOCK", "RELOJ MIDI"), "", clock_name());
    hint(3, TR("POLICE", "FONT", "FUENTE"), "", FONT_NAMES[lang][g_font]);
}

/* ---- HELP (HOME held): what the knobs, the encoders and the keys do on the page it was held on */
typedef struct { const char *e[3], *l[3]; } help_t;
static const help_t HELP[LANG_N][PG_HELP] = {
    {{{"LIGNE", "PISTE", "MOTIF"}, {"REC+SEL: SAUT  ARP+SEL: LIGNE", "SAVE TENU: LES OUTILS", "ARP + K3 K4: LE 2E EFFET"}},
     {{"PAGE DE 16 PAS", "PISTE", "MOTIF"}, {"BLANCHES: PAS OUI/NON", "NOIRES: LA PISTE", "OCT-/OCT+: 16, 32, 64 PAS"}},
     {{"PAGE SUIVANTE", "PISTE", "USINE / CURSEUR"}, {"MODIFIE: PRESETS PUIS OCT+", "ARP+PRESETS: LE MOTEUR", "ANNULER: SAVE+OCT- LE RAMENE"}},
     {{"ENV / MIX", "PISTE", "-"}, {"ENV ENCORE: LE MIX", "TOUCHES: ECOUTER"}},
     {{"ENV / MIX", "PISTE / MASTER", "-"}, {"NOIRES: MUET  ARP+NOIRE: SOLO", "APRES LA 6: LE MASTER"}},
     {{"-", "PISTE", "MOTIF"}, {"BLANCHE: UN OUTIL", "SCL: GAMME, PUIS MAGIE", "SAVE TENU: LES OUTILS"}},
     {{"-", "PISTE", "MOTIF"}, {"UNE TOUCHE: LA TONIQUE", "VERROU: NOTES DE LA GAMME", "SCL ENCORE: MAGIE"}},
     {{"GROOVE, OPTIONS", "ENTREE", "SON MOTIF"}, {"BLANCHE: MOTIF  K3: X FOIS", "OCT+ AJOUTE  OCT- ENLEVE", "GLO: GROOVE, OPTIONS"}},
     {{"CHOIX", "-", "-"}, {"OCT+: OK   OCT-: RETOUR", "SAVE PUIS OCT+: SAUVER VITE"}},
     {{"CHAINE / GROOVE", "METRONOME", "VOLUME CLIC"}, {"K1-K4: THEME LANGUE HORLOGE", "POLICE: TOUS GARDES", "GLO: RETOUR A LA CHAINE"}},
     {{"ENVOIS / LIVE", "PISTE", "MOTIF"}, {"FX TENU + BLANCHES", "OCT: BANQUE  NOIRES: MUETS"}}},
    {{{"ROW", "TRACK", "PATTERN"}, {"REC+SEL: STEP  ARP+SEL: ROW", "SAVE HELD: THE TOOLS", "ARP + K3 K4: THE 2ND FX"}},
     {{"16 STEPS PAGE", "TRACK", "PATTERN"}, {"WHITE KEYS: STEPS ON/OFF", "BLACK KEYS: THE TRACK", "OCT-/OCT+: 16, 32, 64 STEPS"}},
     {{"NEXT PAGE", "TRACK", "FACTORY / CURSOR"}, {"EDITED: PRESETS, THEN OCT+", "ARP+PRESETS: THE ENGINE", "UNDO: SAVE+OCT- BRINGS IT BACK"}},
     {{"ENV / MIX", "TRACK", "-"}, {"ENV AGAIN: THE MIX", "KEYS: LISTEN"}},
     {{"ENV / MIX", "TRACK / MASTER", "-"}, {"BLACK: MUTE  ARP+BLACK: SOLO", "PAST TRACK 6: MASTER"}},
     {{"-", "TRACK", "PATTERN"}, {"WHITE KEY: A TOOL", "SCL: SCALE, THEN MAGIC", "SAVE HELD: THE TOOLS"}},
     {{"-", "TRACK", "PATTERN"}, {"A KEY: THE ROOT", "LOCK: NOTES OF THE SCALE", "SCL AGAIN: MAGIC"}},
     {{"GROOVE, OPTIONS", "ENTRY", "ITS PATTERN"}, {"WHITE KEY: PATTERN K3: TIMES", "OCT+ ADD  OCT- DELETE", "GLO: GROOVE, OPTIONS"}},
     {{"CHOICE", "-", "-"}, {"OCT+: OK   OCT-: BACK", "SAVE THEN OCT+: QUICK SAVE"}},
     {{"SONG / GROOVE", "METRONOME", "CLICK VOLUME"}, {"K1-K4: THEME LANG CLOCK FONT", "ALL OF THEM ARE KEPT", "GLO: BACK TO THE SONG"}},
     {{"SENDS / LIVE", "TRACK", "PATTERN"}, {"HOLD FX + WHITE KEYS", "OCT: BANK  BLACK: MUTES"}}},
    {{{"FILA", "PISTA", "PATRON"}, {"REC+SEL: SALTO  ARP+SEL: FILA", "SAVE PULSADO: HERRAMIENTAS", "ARP + K3 K4: EL 2O EFECTO"}},
     {{"PAGINA DE 16", "PISTA", "PATRON"}, {"BLANCAS: PASOS SI/NO", "NEGRAS: LA PISTA", "OCT-/OCT+: 16, 32, 64 PASOS"}},
     {{"PAG. SIGUIENTE", "PISTA", "FABRICA / CURSOR"}, {"EDITADO: PRESETS Y OCT+", "ARP+PRESETS: EL MOTOR", "DESHACER: SAVE+OCT- LO VUELVE"}},
     {{"ENV / MEZCLA", "PISTA", "-"}, {"ENV OTRA VEZ: LA MEZCLA", "TECLAS: ESCUCHAR"}},
     {{"ENV / MEZCLA", "PISTA / MASTER", "-"}, {"NEGRAS: MUTE  ARP+NEGRA: SOLO", "TRAS LA 6: EL MASTER"}},
     {{"-", "PISTA", "PATRON"}, {"BLANCA: UNA HERRAMIENTA", "SCL: ESCALA, LUEGO MAGIA", "SAVE PULSADO: HERRAMIENTAS"}},
     {{"-", "PISTA", "PATRON"}, {"UNA TECLA: LA TONICA", "FIJA: NOTAS DE LA ESCALA", "SCL OTRA VEZ: MAGIA"}},
     {{"GROOVE, OPCIONES", "ENTRADA", "SU PATRON"}, {"BLANCA: PATRON  K3: VECES", "OCT+ ANADE  OCT- QUITA", "GLO: GROOVE, OPCIONES"}},
     {{"ELECCION", "-", "-"}, {"OCT+: OK   OCT-: VOLVER", "SAVE Y OCT+: GUARDAR RAPIDO"}},
     {{"CANCION / GROOVE", "METRONOMO", "VOLUMEN CLIC"}, {"K1-K4: TEMA IDIOMA RELOJ FUENTE", "TODO SE GUARDA", "GLO: VOLVER A LA CANCION"}},
     {{"ENVIOS / LIVE", "PISTA", "PATRON"}, {"FX PULSADO + BLANCAS", "OCT: BANCO  NEGRAS: MUTE"}}}};
static const help_t HELP_SEND[LANG_N] = {   /* the first page of FX */
    {{"ENVOIS / LIVE", "PISTE", "-"}, {"K1 SATURE  K2 ECHO  K3 REVERB", "0: RIEN, PAR PISTE", "FX ENCORE: LES EFFETS LIVE"}},
    {{"SENDS / LIVE", "TRACK", "-"}, {"K1 DRIVE  K2 ECHO  K3 REVERB", "0: NOTHING, PER TRACK", "FX AGAIN: THE LIVE EFFECTS"}},
    {{"ENVIOS / LIVE", "PISTA", "-"}, {"K1 SATURA  K2 ECO  K3 REVERB", "0: NADA, POR PISTA", "FX OTRA VEZ: EFECTOS LIVE"}}};
static const help_t HELP_GROOVE[LANG_N] = {   /* GROOVE, and two pages of SOUND (EDIT): TABLE, SAMPLE EDIT */
    {{"CHAINE / OPTIONS", "PAS DU GROOVE", "-"}, {"BLANCHE: UN PAS  K1: TICKS", "6 TICKS: UNE LIGNE DROITE", "GLO: LES OPTIONS"}},
    {{"SONG / OPTIONS", "GROOVE STEP", "-"}, {"WHITE KEY: A STEP K1: TICKS", "6 TICKS: A STRAIGHT ROW", "GLO: THE OPTIONS"}},
    {{"CANCION / OPCIONES", "PASO DEL GROOVE", "-"}, {"BLANCA: UN PASO  K1: TICKS", "6 TICKS: UNA FILA RECTA", "GLO: LAS OPCIONES"}}};
static const help_t HELP_TABLE[LANG_N] = {
    {{"PAGE SUIVANTE", "PISTE", "LIGNE TABLE"}, {"LIGNE 0: VITESSE LONG BOUCLE", "K4 SUR LA LIGNE 0: ACTIVE", "ARP: EFFACE LE PAS"}},
    {{"NEXT PAGE", "TRACK", "TABLE ROW"}, {"ROW 0: SPEED LENGTH LOOP", "K4 ON ROW 0: ON / OFF", "ARP: CLEARS THE STEP"}},
    {{"PAG. SIGUIENTE", "PISTA", "FILA TABLA"}, {"FILA 0: VELOC LARGO BUCLE", "K4 EN LA FILA 0: ACTIVA", "ARP: BORRA EL PASO"}}};
static const help_t HELP_SMP[LANG_N] = {
    {{"PAGE SUIVANTE", "PISTE", "DEBUT FIN TRANCHE"}, {"K1: DEPLACE  K2: PRECIS", "K3 NORMALISE  K4 A L'ENVERS", "ARP: TRANCHES EGALES"}},
    {{"NEXT PAGE", "TRACK", "START END SLICE"}, {"K1: MOVE  K2: FINE", "K3 NORMALISE  K4 REVERSE", "ARP: EVEN SLICES"}},
    {{"PAG. SIGUIENTE", "PISTA", "INICIO FIN CORTE"}, {"K1: MOVER  K2: FINO", "K3 NORMALIZA  K4 AL REVES", "ARP: CORTES IGUALES"}}};
static void draw_help(void)
{
    static const char *const BT[12] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
    static const uint8_t BT_PG[12] = {PG_FX, PG_SCALE, PG_ENV, PG_SOUND, PG_SOUND, PG_SONG, PG_TRACK, PG_PROJ, 255, PG_GRID, 255, 255};
    uint32_t hsp = sp_at(trk(), snd_page);
    const help_t *h = help_pg == PG_GROOVE ? &HELP_GROOVE[lang]
                      : help_pg == PG_SEND ? &HELP_SEND[lang]
                      : help_pg == PG_SOUND && hsp == SP_TABLE ? &HELP_TABLE[lang]
                      : help_pg == PG_SOUND && hsp == SP_SMP ? &HELP_SMP[lang] : &HELP[lang][help_pg < PG_HELP ? help_pg : 0];
    static const char *const ENC[3] = {"SELECT", "ALGO", "PRESETS"};
    uint32_t i;
    char s[32];
    if (g_vis(20, 204)) g_rect(0, 20, 240, 204, C_BG);
    if (g_vis(20, 18)) {
        s[0] = 0;
        str_cat(s, PAGE_NAME[help_pg < PG_N ? help_pg : 0]);
        g_rrect(4, 22, g_text_w(s) + 10, 14, PAGE_COL[help_pg < PG_N ? help_pg : 0]);
        g_text(9, 22, s, C_INK, T_CROP | T_BOLD);
    }
    for (i = 0; i < 4u; i++) {                  /* the knobs, in their colours */
        int32_t x = 4 + (int32_t)(i % 2u) * 117, y = 40 + (int32_t)(i / 2u) * 30;
        char n[4] = {'K', (char)('1' + i), 0, 0};
        if (!g_vis(y, 28)) continue;
        g_rrect(x, y, 114, 27, C_BG2);
        g_rect(x, y, 4, 27, KNOB_COL[i]);
        g_text(x + 7, y + 1, n, KNOB_COL[i], T_CROP | T_BOLD);
        if (!help_lab[i][0]) { g_text(x + 7, y + 13, "-", C_FAINT, T_CROP); continue; }
        {                                       /* its name after K1, its value under it (each cut to the box) */
            char a[12], b[14];
            const char *q = help_lab[i];
            uint32_t j = 0, m = 0;
            while (*q && *q != '\n') { if (j < 10u) a[j++] = *q; q++; }
            a[j] = 0;
            if (*q == '\n') q++;
            while (*q) { if (m < 13u) b[m++] = *q; q++; }
            b[m] = 0;
            g_text(x + 27, y + 1, a, C_TXT, T_CROP | T_BOLD);
            g_text(x + 7, y + 13, b, C_GOLD, T_CROP);
        }
    }
    for (i = 0; i < 3u; i++) {
        int32_t y = 102 + (int32_t)i * 13;
        g_text(6, y, ENC[i], C_MID, T_CROP | T_BOLD);
        g_text(80, y, h->e[i], C_GOLD, T_CROP);
    }
    for (i = 0; i < 3u; i++)
        if (h->l[i]) g_text(6, 141 + (int32_t)i * 12, h->l[i], C_TXT, T_CROP);
    g_rect(4, 178, 232, 1, C_RULE);
    for (i = 0; i < 12u; i++) {                 /* the buttons, coloured as their pages */
        int32_t x = 4 + (int32_t)(i % 6u) * 39, y = 181 + (int32_t)(i / 6u) * 22;
        uint16_t c = BT_PG[i] < PG_N ? PAGE_COL[BT_PG[i]] : i == 10u ? C_GO : i == 11u ? C_REC : C_MID;
        g_rrect(x, y, 37, 20, g_mix(C_BG2, c, 90));
        g_text_c(x + 18, y + 4, BT[i], C_WHITE, T_CROP | T_BOLD);
    }
    hint(0, "", "", "");
    hint(1, "", "", "");
    hint(2, "", "", "");
    hint(3, "", "", "");
}

/* ---- the boot: Omnipotard's dog in 32 bits (kawa_photo.h), notes out of his headphones, stars that twinkle */
static void g_text_sh(int32_t x, int32_t y, const char *s, uint16_t c, uint32_t mode)   /* with a shadow */
{
    g_text(x + 1, y + 1, s, RGB(8, 6, 20), mode);
    g_text(x, y, s, c, mode);
}
static void draw_boot(void)
{
    uint32_t t = fm1_ms - boot_ms, i;
    g_pic(0, 0, BOOT_IMG, 240, 240, BOOT_PAL);
    for (i = 0; i < BOOT_NSTAR; i++) {          /* the stars twinkle, each in its turn */
        uint32_t ph = (t / 70u + i * 5u) % 14u;
        int32_t sx = BOOT_STAR[i][0], sy = BOOT_STAR[i][1];
        if (ph < 4u) {
            uint16_t c = ph == 1u || ph == 2u ? C_WHITE : RGB(170, 190, 255);
            g_rect(sx - 1, sy, 3, 1, c);
            g_rect(sx, sy - 1, 1, 3, c);
            if (ph == 2u) { g_px(sx - 2, sy, c); g_px(sx + 2, sy, c); g_px(sx, sy - 2, c); g_px(sx, sy + 2, c); }
        }
    }
    for (i = 0; i < 4u; i++) {                  /* notes rise out of the cup, up into the sky */
        uint32_t ph = (t + i * 350u) % 1400u;
        int32_t nx = 34 - (int32_t)(ph / 60u) + (int32_t)((ph / 120u) % 3u), ny = 104 - (int32_t)(ph / 15u);
        if (ny > 4) g_icon(nx < 0 ? 0 : nx, ny, ICO_NOTE, TRK_COL[(i * 3u) % NTRK], 2);
    }
    g_text_l(233 - 64 + 1, 9, "KAWA", RGB(8, 6, 20));
    g_text_l(233 - 64, 8, "KAWA", C_WHITE);
    g_text_sh(233 - g_text_w("Pocket Tracker"), 44, "Pocket Tracker", C_GOLD, T_BOLD);
    g_text_sh(233 - g_text_w("by Omnipotard"), 60, "by Omnipotard", C_TXT, 0);
    g_rrect(4, 224, 44, 14, C_REC);
    g_text(8, 224, "BETA", C_WHITE, T_CROP | T_BOLD);
    g_text_sh(236 - g_text_w("V" APP_VER), 226, "V" APP_VER, C_TXT, T_CROP);
    if (t > 600u && ((t / 550u) & 1u)) {        /* it waits for you: a key, a button or a knob */
        const char *m = TR("APPUIE SUR UNE TOUCHE", "PRESS ANY KEY", "PULSA UNA TECLA");
        g_text_sh(120 - g_text_w(m) / 2, 204, m, C_WHITE, T_CROP | T_BOLD);
    }
}

static void draw_all(void)
{
    if (page == PG_BOOT) { draw_boot(); return; }
    draw_top();
    switch (page) {
    case PG_TRACK: draw_track(); break;
    case PG_GRID: draw_grid(); break;
    case PG_SOUND: draw_sound(); break;
    case PG_ENV: draw_envpage(); break;
    case PG_MIX: draw_mix(); break;
    case PG_MAGIC: draw_magic(); break;
    case PG_SCALE: draw_scale(); break;
    case PG_SONG: draw_song(); break;
    case PG_PROJ: draw_proj(); break;
    case PG_OPT: draw_opt(); break;
    case PG_GROOVE: draw_groove(); break;
    case PG_FX: draw_fx(); break;
    case PG_SEND: draw_send(); break;
    case PG_HELP: draw_help(); break;
    }
    draw_first();
    draw_tools();
    draw_hold();
    draw_toast();
}

/* ================================================================ input */
static void set_page(uint32_t p)
{
    if (page == PG_FX && p != PG_FX) {
        lfx_keys = 0;                           /* leaving the effects: they all stop */
        rec_live = 0;
    }
    if (p != PG_FX && p != PG_SEND && p != PG_HELP && p != PG_PROJ) fx_sticky = 0;
    if (p == PG_PROJ) {
        slots_scan();
        pm_state = PMS_MENU;
    }
    if (p == PG_GRID && grid_page >= grid_pages())
        grid_page = 0;
    if (p != PG_MIX && cur_trk >= NTRK)
        cur_trk = NTRK - 1u;
    if (p == PG_HELP)                           /* the knobs of the page asked about */
        memcpy(help_lab, knob_lab, sizeof help_lab);
    if (p == PG_SONG) {
        int f = song_find(ui_pat);
        if (song_cur >= prj.song_len || prj.song[song_cur].pat != ui_pat) song_cur = (uint8_t)(f < 0 ? 0 : f);
    }
    grid_held = -1;
    page = (uint8_t)p;
}

static void play_from_cursor(void)               /* PLAY: the pattern on the screen, or the song from its entry */
{
    sq_start_pat = (uint8_t)pat_head(ui_pat);   /* (a 64-step pair: from its first half) */
    sq_start_pos = 0;
    if (prj.mode) {                             /* the song: from the cursor's entry, else from where the pattern is */
        int f = song_find(pat_head(ui_pat));
        sq_start_pos = song_cur < prj.song_len ? song_cur : 0u;
        if (page != PG_SONG && f >= 0) sq_start_pos = (uint8_t)f;
    }
    sq_next = 0xFF;
    follow = 1;
    sq_req = 1;
}
static void play_toggle(void)
{
    if (sq_play) {
        sq_req = 2;
        return;
    }
    sq_tick0 = clk_in_ticks;                    /* (the clock in: its ticks from now) */
    play_from_cursor();
}

/* the note a key plays on track t (a black key on a drum or slice track: its left neighbour, tuned an octave up) */
static uint32_t key_to_note(uint32_t t, uint32_t k, uint8_t *fx, uint8_t *fxv)
{
    *fx = 0;
    *fxv = 0;
    if (IS_TONAL(t)) {
        int32_t n = (int32_t)KEY0 + (int32_t)k + 12 * oct_ofs, b = (int32_t)lane_base(t);
        if (b < 52) n -= 12;                    /* a bass track an octave lower, the noise higher */
        else if (b > 90) n += 24;
        n = clampi(n, 12, 119);
        return prj.lock ? scale_snap((uint32_t)n) : (uint32_t)n;
    }
    {
        int32_t w = WHITE_OF_KEY[k];
        if (w < 0) {
            w = k ? WHITE_OF_KEY[k - 1u] : 0;
            return (uint32_t)w + 17u;           /* a black key: its left neighbour an octave up (notes 17..32), a sound of its own in the cell */
        }
        return (uint32_t)w + 1u;
    }
}

static void write_cell(uint32_t pi, uint32_t r, uint32_t t, uint32_t note, uint32_t vol, uint32_t fx, uint32_t fxv, int merge)
{                                               /* (a note keeps the second column; an empty cell clears it) */
    cell_t *c = &prj.pat[pi].c[r][t];
    undo_push_merge(pi, merge);
    c->note = (uint8_t)note;
    c->vol = (uint8_t)(vol & 15u);
    c->fx = (uint8_t)(fx % FX_N);
    c->fxv = (uint8_t)fxv;
    if (!note && !fx) {
        c->fx2 = FX_NONE;
        c->fxv2 = 0;
    }
}

/* a note heard to try something (a step, a cell, a sound): it ends by itself after a moment (app_step) */
static uint8_t pv_note[NTRK];
static uint32_t pv_ms[NTRK];
static void try_note(uint32_t t, uint32_t note, uint32_t vol, uint32_t fx, uint32_t fxv)
{
    ev_push(EV_ON, t, note, vol, fx, fxv);
    pv_note[t] = (uint8_t)note;
    pv_ms[t] = fm1_ms | 1u;
}
static void try_cell(uint32_t t, const cell_t *c)   /* a cell heard as it plays: both its tricks */
{
    ev_push2(EV_ON, t, c->note, c->vol, c->fx, c->fxv, c->fx2, c->fxv2);
    pv_note[t] = c->note;
    pv_ms[t] = fm1_ms | 1u;
}
static void try_note_ends(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (pv_ms[t] && fm1_ms - pv_ms[t] > 300u) {
            ev_push(EV_OFF, t, pv_note[t], 0, 0, 0);
            pv_ms[t] = 0;
        }
}

/* the pattern that plays after this one ends (a live take on its first row) */
static uint32_t next_pat(void)                  /* the pattern after the one playing, by the sequencer's own rule at the end of a pattern */
{
    if (pat_linked(sq_pat)) return sq_pat + 1u;  /* 64 steps: on into the second half */
    if (prj.mode && prj.song_len) {
        uint32_t pos = sq_songpos;
        if (pos >= prj.song_len) pos = 0;
        else if (sq_rep + 1u >= song_rep(pos)) pos = (pos + 1u) % prj.song_len;
        return prj.song[pos].pat < NPAT ? prj.song[pos].pat : 0u;
    }
    if (sq_next < NPAT) return sq_next;
    return sq_head < NPAT ? sq_head : sq_pat;    /* (the loop of a 64-step pair: back to its first half) */
}

static void key_down(uint32_t k)
{
    uint8_t fx, fxv;
    uint32_t t = trk(), note = key_to_note(t, k, &fx, &fxv), vol = brush_vol[t];
    if (page == PG_SCALE) {                     /* SCALE: a key is the root */
        prj.root = (uint8_t)((KEY0 + k) % 12u);
        touched();
    }
    key_note[k] = (uint8_t)note;
    key_trk[k] = (uint8_t)t;
    brush_note[t] = note;
    pv_ms[t] = 0;                               /* (the key has the voice now: no preview's end cuts it) */
    ev_push(EV_ON, t, note, vol, fx, fxv);
    if (!rec || (page != PG_TRACK && page != PG_GRID && page != PG_SOUND && page != PG_ENV))
        return;
    if (sq_play && pat_same(sq_pat, ui_pat)) {  /* live take (either half of a 64-step pair): on the nearest row */
        uint32_t pi = sq_pat, r = sq_row, len = prj.pat[pi].len ? prj.pat[pi].len : 16u;
        if (sq_frac > 0x80000000u) {            /* the second half of the row: the next one */
            if (++r >= len) {
                r = 0;
                pi = next_pat();                /* (the first row of the pattern that comes next) */
            }
            sq_suppress[t] = 1;
        }
        write_cell(pi, r, t, note, vol, fx, fxv, 1);
    } else {                                    /* step entry at the cursor */
        write_cell(ui_pat, cur_row, t, note, vol, fx, fxv, 1);
        cur_row = (uint8_t)((cur_row + prj.step) % plen());
    }
}

static void rows_shift(int32_t d, int all)      /* rows pushed down from the cursor (d > 0) or pulled up into it */
{
    uint32_t len = plen(), t0 = all ? 0u : trk(), t1 = all ? NTRK : trk() + 1u, t, r;
    pat_t *pt = P();
    undo_push_merge(ui_pat, 1);
    while (d) {
        int s = d > 0 ? 1 : -1;
        d -= s;
        for (t = t0; t < t1; t++) {
            if (s > 0) {
                for (r = len - 1u; r > cur_row; r--) pt->c[r][t] = pt->c[r - 1u][t];
                memset(&pt->c[cur_row][t], 0, sizeof(cell_t));
            } else {
                for (r = cur_row; r + 1u < len; r++) pt->c[r][t] = pt->c[r + 1u][t];
                memset(&pt->c[len - 1u][t], 0, sizeof(cell_t));
            }
        }
        if (all) {
            if (s > 0) { for (r = len - 1u; r > cur_row; r--) pt->live[r] = pt->live[r - 1u]; pt->live[cur_row] = 0; }
            else { for (r = cur_row; r + 1u < len; r++) pt->live[r] = pt->live[r + 1u]; pt->live[len - 1u] = 0; }
        }
        toast(s > 0 ? TR("LIGNE INSEREE", "ROW IN", "FILA INSERTADA") : TR("LIGNE OTEE", "ROW OUT", "FILA QUITADA"));
    }
}

static void key_up(uint32_t k)
{
    ev_push(EV_OFF, key_trk[k], key_note[k], 0, 0, 0);
}

static void cell_turn(cell_t *c, uint32_t pi, uint32_t t, uint32_t knob, int32_t d, int preview, int undo, int two)
{                                               /* two: KNOB 3 and 4 on the second column (ARP held, TRACKS) */
    int32_t v;
    if (undo) undo_push_merge(pi, 1);
    switch (knob) {
    case 0: {                                   /* the note: ... empty, OFF, notes */
        int32_t lo = IS_TONAL(t) ? 12 : 1, hi = IS_TONAL(t) ? 119 : 16;
        int32_t n = c->note == NOTE_NONE ? -2 : c->note == NOTE_OFF ? -1 : c->note;
        while (d) {
            int32_t s = d > 0 ? 1 : -1;
            d -= s;
            if (n == -2) n = s > 0 ? (int32_t)brush_note[t] : -2;
            else if (n == -1) n = s > 0 ? (int32_t)brush_note[t] : -2;
            else if (s < 0 && n <= lo) n = -1;
            else if (IS_TONAL(t) && prj.lock) n = (int32_t)scale_step((uint32_t)n, s);
            else n = clampi(n + s, lo, hi);
        }
        c->note = n == -2 ? NOTE_NONE : n == -1 ? NOTE_OFF : (uint8_t)n;
        if (c->note && c->note != NOTE_OFF) {
            if (!c->vol) c->vol = brush_vol[t];
            brush_note[t] = c->note;
            if (preview && !sq_play) try_cell(t, c);
        }
        break;
    }
    case 1:
        if (!c->note) break;
        c->vol = (uint8_t)clampi((int32_t)c->vol + d, 1, 15);   /* (4 bits) */
        brush_vol[t] = c->vol;
        break;
    case 2:
        v = ((int32_t)(two ? c->fx2 : c->fx) + d) % (int32_t)FX_N;
        if (v < 0) v += FX_N;
        if (two) { c->fx2 = (uint8_t)v; c->fxv2 = (uint8_t)FXD[v].def; }
        else { c->fx = (uint8_t)v; c->fxv = (uint8_t)FXD[v].def; }
        break;
    case 3:
        if (two) {
            if (!c->fx2) break;
            v = clampi((int8_t)c->fxv2 + d, FXD[c->fx2].lo, FXD[c->fx2].hi);
            c->fxv2 = (uint8_t)(int8_t)v;
        } else {
            if (!c->fx) break;
            v = clampi((int8_t)c->fxv + d, FXD[c->fx].lo, FXD[c->fx].hi);
            c->fxv = (uint8_t)(int8_t)v;
        }
        break;
    }
}

static void param_turn(uint32_t t, uint32_t k, int32_t d)
{
    const pdesc_t *pd = &PDESC[ENG(t)][k];
    int32_t nv;
    if (!pd->name[0]) return;
    nv = clampi(prj.snd[t].p[k] + d, pd->lo, pd->hi);
    if (nv == prj.snd[t].p[k]) return;
    undo_push_snd(t, 1);
    prj.snd[t].p[k] = (int8_t)nv;
    if (k <= P_S7) sound_build(t);
    if (k == P_SRC || k == P_SMODE) brush_note[t] = lane_base(t);
    if (k == P_FMPAT && ENG(t) == E_FM) { fm_patch_load(t); sound_build(t); }
    touched();
}

static void clock_in_reset(void);
static void table_turn(uint32_t t, uint32_t i, int32_t d)   /* TABLE: row 0 its settings, rows 1..16 a step */
{
    table_t *tb = &prj.tbl[t];
    int32_t v;
    if (tb_cur == 0u) {
        uint32_t len = (uint32_t)clampi(tb->len, 1, NTSTEP);
        if (i == 0u) tb->speed = (uint8_t)clampi(tb->speed + d, 1, 24);
        else if (i == 1u) tb->len = (uint8_t)clampi(tb->len + d, 1, NTSTEP);
        else if (i == 2u) {                     /* a step of it, then none */
            v = clampi((tb->loop < len ? (int32_t)tb->loop : (int32_t)len) + d, 0, (int32_t)len);
            tb->loop = (uint8_t)(v >= (int32_t)len ? NTSTEP : (uint32_t)v);
        } else
            tb->on = (uint8_t)(d > 0);
    } else {
        tstep_t *st = &tb->s[(tb_cur - 1u) % NTSTEP];
        if (i == 0u) st->tr = (int8_t)clampi(st->tr + d, -24, 24);
        else if (i == 1u) st->vol = (uint8_t)clampi(st->vol + d, 0, 15);
        else if (i == 2u) {
            v = ((int32_t)st->fx + d) % (int32_t)TB_N;
            if (v < 0) v += TB_N;
            st->fx = (uint8_t)v;
            st->fxv = TBD[v].def;
        } else if (st->fx)
            st->fxv = (int8_t)clampi(st->fxv + d, TBD[st->fx].lo, TBD[st->fx].hi);
    }
    touched();
}

static void smp_even(sedit_t *se)               /* the slices even again across the edit */
{
    uint32_t k;
    se->manual = 0;
    for (k = 0; k < 16u; k++)
        se->sl[k] = (uint16_t)(se->start + (((uint32_t)(se->end - se->start) * k) >> 4));
}
static void smp_edit_turn(uint32_t t, uint32_t i, int32_t d)   /* SAMPLE EDIT: K1 K2 move the marker, K3 K4 */
{
    sedit_t *se = &prj.sed[t];
    if (i < 2u) {
        uint16_t *m;
        if (se_cur >= 2u && !se->manual) {      /* a slice moved by hand: the others stay where they were */
            smp_even(se);
            se->manual = 1;
        }
        m = se_cur == 0u ? &se->start : se_cur == 1u ? &se->end : &se->sl[se_cur - 1u];
        *m = (uint16_t)clampi((int32_t)*m + d * (i == 0u ? 256 : 16), 0, 65535);
        if (se_cur >= 2u) {                     /* (between its neighbours) */
            uint32_t lo = se->sl[se_cur - 2u], hi = se_cur < 16u ? se->sl[se_cur] : se->end;
            *m = (uint16_t)clampi(*m, (int32_t)lo, (int32_t)hi);
        }
        sed_fix(se);
        if (!sq_play) {                         /* hear it: the slice moved, or the sound from its start */
            uint32_t n = prj.snd[t].p[P_SMODE] ? 60u : se_cur >= 2u ? se_cur : se_cur == 1u ? 16u : 1u;
            try_note(t, n, 12, 0, 0);
        }
    } else if (i == 2u)
        se->norm = (uint8_t)(d > 0);
    else
        se->rev = (uint8_t)(d > 0);
    sed_normalise(t);
    touched();
}

static void knob(uint32_t i, int32_t d)
{
    uint32_t t = trk();
    knob_ms[i] = fm1_ms;
    if (tools_open) {                           /* the tools: K3 the scope (the track or the pattern) */
        if (i == 2u) mg_scope = (uint8_t)clampi(mg_scope + d, 0, 1);
        return;
    }
    switch (page) {
    case PG_TRACK:
        if (arp_held && i >= 2u) arp_turned = 1;   /* (ARP's release then clears nothing) */
        cell_turn(CUR(), ui_pat, t, i, d, 1, 1, arp_held && i >= 2u);
        break;
    case PG_GRID:
        if (grid_held >= 0) {
            grid_edit = 1;
            cell_turn(&prj.pat[gpat(grid_page)].c[grow(grid_page) + (uint32_t)grid_held][t], gpat(grid_page), t, i, d, 1, 1, 0);
        } else {
            cell_t b;
            memset(&b, 0, sizeof b);
            b.note = (uint8_t)brush_note[t];
            b.vol = brush_vol[t] & 15u;
            b.fx = brush_fx[t] % FX_N;
            b.fxv = brush_fxv[t];
            cell_turn(&b, ui_pat, t, i, d, 1, 0, 0);
            if (b.note && b.note != NOTE_OFF) brush_note[t] = b.note;
            brush_vol[t] = b.vol ? b.vol : brush_vol[t];
            brush_fx[t] = b.fx;
            brush_fxv[t] = b.fxv;
        }
        break;
    case PG_SOUND: {
        uint32_t sp = sp_at(t, snd_page);
        pre_cand = 0xFF;                        /* (a knob: the factory sound shown is dropped, the sound stays) */
        if (sp == SP_TABLE) { undo_push_snd(t, 1); table_turn(t, i, d); break; }
        if (sp == SP_SMP) { undo_push_snd(t, 1); smp_edit_turn(t, i, d); break; }
        if (sp == SP_OP || sp == SP_OPENV) {    /* an operator of the FM track */
            uint32_t k = (sp == SP_OP ? OPK_LEVEL : OPK_ATT) + i;
            if (clampi(op_get(fm_op, k) + d, OPK_LO[k], OPK_HI[k]) == op_get(fm_op, k)) break;   /* (at its end: nothing to undo) */
            undo_push_snd(t, 1);
            op_set(fm_op, k, op_get(fm_op, k) + d);
            sound_build(t);
            touched();
        } else
            param_turn(t, SP_FIRST[sp] + i, d);
        break;
    }
    case PG_ENV:
        param_turn(t, P_ATT + i, d);
        break;
    case PG_MIX:
        if (cur_trk == MASTER) {
            prj.mfx[i] = (int8_t)clampi(prj.mfx[i] + d, 0, i ? 15 : FXT_N - 1);
            touched();
        } else {
            static const uint8_t K[4] = {P_VOL, P_PAN, P_ECHO, P_VERB};
            param_turn(t, K[i], d);
        }
        break;
    case PG_MAGIC:
        if (i == 0u) mg_inten = (uint8_t)clampi(mg_inten + d, 0, 15);
        else if (i == 1u) mg_seed = (uint8_t)clampi(mg_seed + d, 0, 99);
        else if (i == 2u) mg_scope = (uint8_t)clampi(mg_scope + d, 0, 1);
        break;
    case PG_SCALE:
        if (i == 0u) prj.root = (uint8_t)((prj.root + 12 + d % 12 + 12) % 12);
        else if (i == 1u) prj.scale = (uint8_t)clampi(prj.scale + d, 0, 11);
        else if (i == 2u) prj.lock = (uint8_t)clampi(prj.lock + d, 0, 1);
        else prj.step = (uint8_t)clampi(prj.step + d, 0, 16);
        touched();
        break;
    case PG_SONG:
        if (i == 0u) prj.bpm = (uint16_t)clampi(prj.bpm + d, 40, 300);
        else if (i == 1u) prj.swing = (uint8_t)clampi(prj.swing + d, 50, 75);
        else if (i == 2u) { follow = 0; if (song_cur < prj.song_len) prj.song[song_cur].rep = (uint8_t)clampi(prj.song[song_cur].rep + d, 1, 16); }
        else { follow = 0; undo_push_merge(ui_pat, 1); P()->len = (uint8_t)clampi((int32_t)plen() + d, 1, NROW); }
        touched();
        break;
    case PG_PROJ:
        if (pm_state == PMS_NAME) pm_name_turn(i, d);
        else if (i == 0u && pm_state == PMS_MENU) pm_sel = (uint8_t)clampi(pm_sel + d, 0, PM_N - 1);
        else if (i == 0u && pm_state == PMS_SLOT) pm_slot = (uint8_t)clampi(pm_slot + d, pm_act == PM_LOAD ? 0 : 1, NSLOT - 1);
        break;
    case PG_OPT:
        if (i == 0u) { theme_set((uint32_t)clampi(theme + d, 0, NTHEME - 1)); set_changed(); }
        else if (i == 1u) set_lang((uint32_t)clampi(lang + d, 0, LANG_N - 1));
        else if (i == 3u) { g_font_set((uint32_t)clampi(g_font + d, 0, NFONT - 1)); set_changed(); }
        else if (i == 2u) {                     /* OFF, OUT, IN */
            uint32_t c = (uint32_t)clampi(set.clock + d, 0, 2);
            if (c != set.clock) {
                if (sq_play) sq_req = 2;        /* (another clock: the song stops, it starts again on it) */
                set.clock = (uint8_t)c;
                clock_in_reset();
                clk_in = (uint8_t)(c == 2u);
                set_changed();
            }
        }
        break;
    case PG_GROOVE: {
        groove_t *g = &prj.grv;
        if (i == 0u) g->tick[gr_cur % 16u] = (uint8_t)clampi(g->tick[gr_cur % 16u] + d, 1, 12);
        else if (i == 1u) {
            g->len = (uint8_t)clampi(g->len + d, 1, 16);
            if (gr_cur >= g->len) gr_cur = (uint8_t)(g->len - 1u);
        } else if (i == 2u) {                   /* a preset: it plays at once */
            gr_preset = (uint8_t)clampi(gr_preset + d, 0, 5);
            groove_preset(gr_preset);
            g->on = 1;
            if (gr_cur >= g->len) gr_cur = 0;
        } else
            g->on = (uint8_t)(d > 0);
        touched();
        break;
    }
    case PG_SEND:
        if (i == 0u) {
            int32_t ns = clampi((int32_t)prj.snd[t].sat + d, 0, 15);
            if (ns != prj.snd[t].sat) { undo_push_snd(t, 1); prj.snd[t].sat = (uint8_t)ns; touched(); }
        }
        else if (i == 1u) param_turn(t, P_ECHO, d);
        else if (i == 2u) param_turn(t, P_VERB, d);
        break;
    case PG_FX:
        if (i == 0u) { fx_bank = (uint8_t)clampi(fx_bank + d, 0, 1); lfx_keys = 0; }
        else if (i == 2u) rec = (uint8_t)clampi(rec + d, 0, 1);
        else if (i == 3u) { lfx_target = (uint8_t)clampi(lfx_target + d, 0, NTRK); lfx_keys = 0; }
        break;
    }
}

static void pat_select(int32_t d)
{
    ui_pat = (uint8_t)clampi(ui_pat + d, 0, NPAT - 1);
    if (cur_row >= plen()) cur_row = 0;
    follow = (uint8_t)(sq_play && pat_same(ui_pat, sq_pat));   /* browsing while it plays: the screen stays here */
    if (sq_play && !prj.mode) sq_next = pat_same(ui_pat, sq_pat) ? 0xFF : (uint8_t)pat_head(ui_pat);   /* at the end of the one playing (a pair: from its first half) */
}

static void song_fresh(const char *msg)       /* after a new song, a load, the demo: the screen from the top */
{
    hist_clear();
    ui_pat = 0;
    cur_row = 0;
    song_cur = 0;
    pm_state = PMS_MENU;
    toast(msg);
    set_page(PG_TRACK);
}
static void kw_hold_wait(void);
static void pm_do(uint32_t act)                 /* the action, its questions answered */
{
    if (sq_play) sq_req = 2;
    switch (act) {
    case PM_NEW: case PM_DEMO:
        ev_push(EV_KILL, NTRK, 0, 0, 0, 0);     /* (every voice of the old song goes) */
        if (act == PM_NEW) prj_new(); else prj_demo();
        proj_cur = 0xFF;
        dirty = 0;                              /* nothing to lose yet; the autosave takes it */
        auto_due = 1;
        song_fresh(act == PM_NEW ? prj.name : "DEMO !");
        if (act == PM_DEMO) {                   /* the demo opens on its last step (7/7), pattern on the screen */
            song_cur = (uint8_t)(prj.song_len - 1u);
            ui_pat = prj.song[song_cur].pat;
        }
        break;
    case PM_LOAD:
        if (prj_load(pm_slot)) { toast(TR("RIEN ICI", "NOTHING HERE", "NADA AQUI")); pm_state = PMS_SLOT; break; }
        ev_push(EV_KILL, NTRK, 0, 0, 0, 0);     /* (every voice of the old song goes) */
        proj_cur = prj.home >= 1u && prj.home < NSLOT ? prj.home : 0xFF;
        song_fresh(TR("CHARGE !", "LOADED !", "CARGADO !"));
        break;
    case PM_SAVE: case PM_SAVEAS: {
        int rc;
        if (act == PM_SAVEAS) { memcpy(prj.name, nm_buf, 16); prj.name[15] = 0; }
        audio_hold = 1;                         /* erasing stalls the bus: the sound is held silent first (the DMA */
        kw_hold_wait();                         /* would loop the last block), as for a sample (kawa_sysex.c) */
        rc = prj_save(pm_slot);
        audio_hold = 0;
        if (rc) { toast(TR("ERREUR !", "ERROR !", "ERROR !")); break; }
        proj_cur = pm_slot;
        pm_state = PMS_MENU;
        toast(TR("SAUVE !", "SAVED !", "GUARDADO !"));
        break;
    }
    case PM_RENAME:
        memcpy(prj.name, nm_buf, 16);
        prj.name[15] = 0;
        touched();
        pm_state = PMS_MENU;
        break;
    }
    slots_scan();
}
static void pm_ok(void)                         /* OCT+ on the PROJECTS page */
{
    switch (pm_state) {
    case PMS_MENU:
        pm_act = pm_sel;
        if (pm_sel == PM_SAVE && proj_cur >= 1u && proj_cur < NSLOT) { pm_slot = proj_cur; pm_do(PM_SAVE); break; }
        if (pm_sel == PM_SAVE) pm_act = PM_SAVEAS;
        if (pm_act == PM_SAVEAS || pm_act == PM_LOAD) {
            uint32_t k;
            pm_state = PMS_SLOT;
            pm_slot = proj_cur >= 1u && proj_cur < NSLOT ? proj_cur : 1u;
            if (pm_act == PM_SAVEAS && names_ok[pm_slot])   /* save as: the first empty slot, if there is one */
                for (k = 1; k < NSLOT; k++) if (!names_ok[k]) { pm_slot = (uint8_t)k; break; }
        } else if (pm_act == PM_RENAME) {
            memcpy(nm_buf, prj.name, 16);
            nm_pos = 0;
            pm_state = PMS_NAME;
        } else if (dirty)
            pm_state = PMS_ASK;
        else
            pm_do(pm_act);
        break;
    case PMS_SLOT:
        if (pm_act == PM_LOAD) { if (dirty) pm_state = PMS_ASK; else pm_do(PM_LOAD); }
        else if (pm_slot) { memcpy(nm_buf, prj.name, 16); nm_pos = 0; pm_state = PMS_NAME; }
        break;
    case PMS_NAME:
        if (pm_act == PM_SAVEAS && names_ok[pm_slot] && pm_slot != proj_cur) pm_state = PMS_ASK;
        else pm_do(pm_act);
        break;
    case PMS_ASK:
        pm_do(pm_act);
        break;
    }
}
static void pm_back(void)                       /* OCT- on the PROJECTS page */
{
    if (pm_state == PMS_MENU) set_page(page_back == PG_PROJ ? PG_TRACK : page_back);
    else if (pm_state == PMS_NAME && pm_act == PM_SAVEAS) pm_state = PMS_SLOT;
    else pm_state = PMS_MENU;
}

/* GRID, OCT- / OCT+: the pattern's size, 16 steps (the classic), 32 or 64. 64 goes on into the next pattern (32 + 32): both play as
 * one, in the song and in the loop. Taking it back keeps the second half as a pattern of its own */
static void steps_turn(int32_t d)
{
    uint32_t pi = ui_pat, now = pat_linked(pi) ? 64u : plen() > 16u ? 32u : 16u, n = d > 0 ? now * 2u : now / 2u;
    char s[24];
    if (n < 16u || n > 64u) return;
    if (n == 64u) {
        if (pi + 1u >= NPAT) {
            toast(TR("DERNIER MOTIF", "LAST PATTERN", "ULTIMO PATRON"));
            return;
        }
        if (pat_linked(pi + 1u) || (pi && pat_linked(pi - 1u))) {
            toast(TR("MOTIF DEJA LIE", "PATTERN ALREADY LINKED", "PATRON YA ENLAZADO"));
            return;
        }
        prj.pat[pi + 1u].len = 32;              /* (the second half's length is not an undo step of its own: one UNDO takes the link back) */
    }
    undo_push(pi);
    P()->len = (uint8_t)(n == 64u ? 32u : n);
    P()->pad[0] = (uint8_t)(n == 64u);
    if (cur_row >= plen()) cur_row = 0;
    grid_page = 0;
    touched();
    s[0] = 0;
    fmt_u(str_cat(s, TR("PAS ", "STEPS ", "PASOS ")), n, 0);
    toast(s);
}

/* ---- SAVE held: the tools. SELECT picks one, it runs when SAVE is let go (OCT- / OCT+ still undo / redo at once).
 * CLEAR, COPY, PASTE, RANDOM act on the track of the pattern on the screen, or on the whole pattern (K3 in the list) */
static void undo_do(int redo)                   /* UNDO or REDO: a pattern or a sound comes back; the screen says which */
{
    int p = redo ? redo_pop() : undo_pop();
    char s[24];
    if (p >= 0x100) {                           /* a sound: its track is shown */
        uint32_t t = (uint32_t)p & 7u;
        cur_trk = (uint8_t)t;
        if (snd_page >= sp_n(t)) snd_page = 0;
        pre_cand = 0xFF;
        s[0] = 0;
        fmt_u(str_cat(s, redo ? TR("REFAIT: SON ", "REDONE: SOUND ", "REHECHO: SONIDO ")
                              : TR("ANNULE: SON ", "UNDONE: SOUND ", "DESHECHO: SONIDO ")), t + 1u, 0);
        toast(s);
        return;
    }
    if (p >= 0) { ui_pat = (uint8_t)p; if (cur_row >= plen()) cur_row = 0; }
    toast(p == -2 ? TR("FM: UNE SEULE PISTE", "FM: ONE TRACK ONLY", "FM: UNA SOLA PISTA")
          : p < 0 ? (redo ? TR("RIEN A REFAIRE", "NOTHING TO REDO", "NADA QUE REHACER")
                          : TR("RIEN A ANNULER", "NOTHING TO UNDO", "NADA QUE DESHACER"))
                  : (redo ? TR("REFAIT", "REDONE", "REHECHO") : TR("ANNULE", "UNDONE", "DESHECHO")));
}
static void tools_run(uint32_t k)
{
    char s[24];
    uint32_t t = trk(), tool;
    if (k == 0u) {
        page_back = page == PG_HELP ? help_pg : page;
        set_page(PG_OPT);
        return;
    }
    if (k <= 2u) {
        undo_do(k == 2u);
        return;
    }
    if (k == 3u) tool = MG_CLEAR;
    else if (k == 4u) tool = MG_COPY;
    else if (k == 5u) tool = MG_PASTE;
    else                                        /* RANDOM: what suits the track (the whole pattern: each its own) */
        tool = IS_TONAL(t) && ENG(t) != E_NOISE ? (is_bass(t) ? MG_BASS : MG_MELO) : note_mode(t) == NM_SLICE ? MG_CHOP : MG_BEAT;
    if (k == 6u && mg_scope) tool = MG_MELO;
    s[0] = 0;
    str_cat(str_cat(str_cat(s, TOOL_NAMES[lang][k]), " "),
            magic_run(tool, page == PG_GRID ? gpat(grid_page) : ui_pat, t, mg_scope, mg_inten, mg_seed, song_cur));   /* (the grid: the half it shows) */
    toast(s);
    mg_seed = (uint8_t)((mg_seed + 1u) % 100u);
}

static void sound_button(uint32_t lfo)          /* EDIT (0) or LFO (1): open its pages, or go round them */
{
    uint8_t l[8];
    if (page == PG_HELP) page = help_pg;
    if (page == PG_SOUND && sg_lfo(trk(), snd_page) == lfo) { sg_move(1, 1); return; }
    set_page(PG_SOUND);
    sg_list(trk(), lfo, l);
    snd_page = l[0];
}

static void page_button(uint32_t p)             /* a page's button: open it, or go round its pages */
{
    if (page == PG_HELP) page = help_pg;
    switch (p) {
    case PG_SOUND:                              /* EDIT (LFO has its own button: sound_button) */
        sound_button(0);
        break;
    case PG_ENV: set_page(page == PG_ENV ? PG_MIX : PG_ENV); break;
    case PG_GRID: set_page(page == PG_GRID ? PG_TRACK : PG_GRID); break;
    case PG_MAGIC: set_page(page == PG_MAGIC ? PG_TRACK : PG_MAGIC); break;
    case PG_SCALE: set_page(page == PG_SCALE ? PG_MAGIC : page == PG_MAGIC ? PG_TRACK : PG_SCALE); break;   /* SCL: SCALE, MAGIC */
    case PG_SONG: set_page(page == PG_SONG ? PG_GROOVE : page == PG_GROOVE ? PG_OPT : PG_SONG); break;
    default: set_page(p); break;
    }
}

static void button_down(uint32_t b)
{
    switch (b) {
    case BTN_PLAY:
        if (home_held) {                        /* HOME + PLAY: panic, everything stops and goes quiet */
            home_did = 1;
            sq_req = 2;
            lfx_keys = 0;
            ev_push(EV_KILL, NTRK, 0, 0, 0, 0);
            toast("PANIC");
        } else
            play_toggle();
        break;
    case BTN_REC: rec_held = 1; rec_turned = 0; break;   /* REC: on its release (held + SELECT: the jump) */
    case BTN_FX:                                /* SENDS, then LIVE, then back; held from another page: LIVE while held */
        fx_down = 0;
        if (page == PG_SEND) { set_page(PG_FX); break; }
        if (page == PG_FX && fx_sticky) { set_page(fx_back); fx_sticky = 0; break; }
        if (page != PG_FX) {                    /* (from the help: the page it was on; from PROJECTS: TRACKS) */
            uint32_t from = page == PG_HELP ? help_pg : page;
            if (from != PG_SEND && from != PG_FX) fx_back = (uint8_t)(from == PG_PROJ || from == PG_HELP ? PG_TRACK : from);
        }
        set_page(PG_SEND);
        fx_sticky = 0;
        fx_down = 1;
        fx_down_ms = fm1_ms;
        break;
    case BTN_HOME: home_down_ms = fm1_ms; home_held = 1; help_pg = page == PG_HELP ? help_pg : page; break;
    case BTN_SEQ: page_button(PG_GRID); break;
    case BTN_EDIT: page_button(PG_SOUND); break;
    case BTN_ENV: page_button(PG_ENV); break;
    case BTN_LFO: sound_button(1); break;           /* LFO, MODULATION */
    case BTN_SEL: page_button(PG_SCALE); break;
    case BTN_GLO: page_button(PG_SONG); break;      /* SONG, again OPTIONS */
    case BTN_SAVE: save_down_ms = fm1_ms; save_held = 1; save_did = 0; break;
    case BTN_ARP:
        if (tools_open) break;
        arp_held = 1;
        arp_turned = 0;
        if (page == PG_TRACK) {
            /* on its release (held + SELECT: rows in or out of the track, + ALGORITHM: of the pattern) */
        } else if (page == PG_FX) {
            undo_push(ui_pat);
            memset(P()->live, 0, sizeof P()->live);
            toast(TR("LIVE EFFACE", "LIVE CLEARED", "LIVE BORRADO"));
        } else if (page == PG_SONG) {
            prj.mode ^= 1u;
            touched();
        } else if (page == PG_GRID && grid_held >= 0) {
            write_cell(gpat(grid_page), grow(grid_page) + (uint32_t)grid_held, trk(), 0, 0, 0, 0, 0);
            grid_edit = 1;
        } else if (page == PG_SOUND && sp_at(trk(), snd_page) == SP_TABLE && tb_cur) {
            undo_push_snd(trk(), 0);
            memset(&prj.tbl[trk()].s[(tb_cur - 1u) % NTSTEP], 0, sizeof(tstep_t));   /* the step cleared */
            touched();
        } else if (page == PG_SOUND && sp_at(trk(), snd_page) == SP_SMP) {
            undo_push_snd(trk(), 0);
            smp_even(&prj.sed[trk()]);
            sed_normalise(trk());
            touched();
            toast(TR("TRANCHES EGALES", "EVEN SLICES", "CORTES IGUALES"));
        }
        break;
    case BTN_OCT_DOWN:
    case BTN_OCT_UP: {
        int32_t d = b == BTN_OCT_UP ? 1 : -1;
        if (save_held) {                        /* SAVE held: OCT- undo, OCT+ redo */
            save_did = 1;
            undo_do(d > 0);
        } else if (page == PG_SOUND && pre_cand != 0xFF) {   /* a factory sound is shown: OCT+ loads it, OCT- keeps the sound */
            if (d > 0) preset_commit();
            else { pre_cand = 0xFF; toast(TR("GARDE", "KEPT", "CONSERVADO")); }
        } else if (page == PG_FX) { fx_bank = d > 0; lfx_keys = 0; }
        else if (page == PG_GRID) steps_turn(d);
        else if (page == PG_PROJ) { if (d > 0) pm_ok(); else pm_back(); }
        else if (page == PG_SONG) {
            follow = 0;
            if (d > 0) {                        /* a copy of the entry after it (or the pattern on screen) */
                uint32_t at = song_cur < prj.song_len ? song_cur + 1u : prj.song_len;
                uint32_t pn = song_cur < prj.song_len ? prj.song[song_cur].pat : ui_pat;
                uint32_t rp = song_cur < prj.song_len ? prj.song[song_cur].rep : 1u;
                if (song_insert(at, pn, rp)) song_cur = (uint8_t)at;
            } else if (song_cur < prj.song_len) {
                song_delete(song_cur);
                if (song_cur >= prj.song_len && song_cur) song_cur--;
            }
        } else {
            oct_ofs = (int8_t)clampi(oct_ofs + d, -3, 3);
            set_changed();
        }
        break;
    }
    }
}

static void button_up(uint32_t b)
{
    switch (b) {
    case BTN_FX:
        if (fx_down) {
            fx_down = 0;
            if (page == PG_FX && !fx_sticky) set_page(fx_back);   /* it was held: LIVE only while it is */
            else if (page == PG_SEND) fx_sticky = 1;                /* a tap: the page stays */
        }
        break;
    case BTN_HOME:
        home_held = 0;
        if (page == PG_HELP) set_page(help_pg);  /* the help goes, the page stays */
        else if (home_did) {}                   /* (HOME + a button: a chord, not a tap) */
        else set_page(PG_TRACK);                 /* a tap: the tracks */
        home_did = 0;
        break;
    case BTN_SAVE:
        save_held = 0;
        if (tools_open) {                       /* held: the tool chosen with SELECT runs when it is let go */
            tools_open = 0;
            if (!save_did) tools_run(tools_sel);
        } else if (!save_did) {                 /* a tap: the PROJECTS menu (again: back) */
            if (page == PG_PROJ) set_page(page_back == PG_PROJ ? PG_TRACK : page_back);
            else { page_back = page == PG_HELP ? help_pg : page; set_page(PG_PROJ); pm_sel = PM_SAVE; }
        }
        break;
    case BTN_ARP:
        if (arp_held && !arp_turned && page == PG_TRACK) {   /* a tap on TRACKS: the cell is cleared */
            write_cell(ui_pat, cur_row, trk(), 0, 0, 0, 0, 0);
            if (rec) cur_row = (uint8_t)((cur_row + prj.step) % plen());
        }
        arp_held = 0;
        break;
    case BTN_REC:
        rec_held = 0;
        if (!rec_turned) rec ^= 1u;
        break;
    }
}

static void grid_key(uint32_t i, int down)
{
    uint32_t gp = gpat(grid_page), r = grow(grid_page) + i, t = trk();
    if (r >= plen_of(gp)) return;
    if (down) {
        grid_held = (int8_t)i;
        grid_edit = 0;
        return;
    }
    if (grid_held == (int8_t)i && !grid_edit) { /* a tap: on / off */
        cell_t *c = &prj.pat[gp].c[r][t];
        if (c->note) write_cell(gp, r, t, 0, 0, 0, 0, 0);
        else {
            write_cell(gp, r, t, brush_note[t], brush_vol[t], brush_fx[t], brush_fxv[t], 0);
            if (!sq_play) try_note(t, brush_note[t], brush_vol[t], brush_fx[t], brush_fxv[t]);
        }
    }
    if (grid_held == (int8_t)i) grid_held = -1;
}

static void keys_input(void)
{
    static uint32_t keys_played;                /* keys that played a note (key_down): their release ends it, */
    uint32_t edges = fm1_input_note_edges(), keys = fm1_in.notes | edges, k, pass;   /* whatever the page then */
    uint32_t down = keys & ~prev_keys, up = (prev_keys | edges) & ~fm1_in.notes;   /* (a tap quicker than a pass: */
    prev_keys = fm1_in.notes;                                                     /* down and up both) */
    if (down | up) last_input_ms = fm1_ms;
    for (pass = 0; pass < 2u; pass++)           /* the presses, then the releases */
    for (k = 0; k < 27u; k++) {
        int32_t w = WHITE_OF_KEY[k], bl = BLACK_OF_KEY[k];
        uint32_t dn = pass == 0u ? down >> k & 1u : 0u, u = pass == 1u ? up >> k & 1u : 0u;
        if (!dn && !u) continue;
        if (dn && tools_open) continue;         /* (the tools are open: the keys wait) */
        if (dn && page == PG_SEND && fx_down) set_page(PG_FX);   /* FX held and a key at once: that key is a live effect */
        if (u && (keys_played >> k & 1u)) {    /* a note played: it ends, on whatever page */
            keys_played &= ~(1u << k);
            key_up(k);
            continue;
        }
        switch (page) {
        case PG_FX:
            if (w >= 0) {
                uint32_t id = fx_bank * 16u + (uint32_t)w;
                if (dn) lfx_keys |= 1u << id;
                else lfx_keys &= ~(1u << id);
                rec_live = (uint8_t)(rec && sq_play);
                if (dn && rec_live) undo_push_merge(sq_pat, 1);   /* a take into the LIVE lane: an edit */
            } else if (dn && bl < (int32_t)NTRK)
                live_mute ^= (uint8_t)(1u << bl);
            break;
        case PG_GRID:
            if (w >= 0) grid_key((uint32_t)w, (int)dn);
            else if (dn && bl < (int32_t)NTRK) { cur_trk = (uint8_t)bl; grid_held = -1; }
            break;
        case PG_MAGIC:
            if (dn && w >= 0) {
                const char *r = magic_run((uint32_t)w, ui_pat, trk(), mg_scope, mg_inten, mg_seed, song_cur);
                char s[24];
                s[0] = 0;
                str_cat(str_cat(str_cat(s, MAGIC[w].name[lang]), " "), r);
                toast(s);
                mg_seed = (uint8_t)((mg_seed + 1u) % 100u);
                if (mg_clone_to < NPAT) {       /* CLONE: here is the new one (and it stays here: no follow; */
                    ui_pat = mg_clone_to;       /* in loop mode it plays next) */
                    mg_clone_to = 0xFF;
                    if (cur_row >= plen()) cur_row = 0;
                    follow = (uint8_t)(sq_play && ui_pat == sq_pat);
                    if (sq_play && !prj.mode) sq_next = ui_pat;
                }
            } else if (dn && bl < (int32_t)NTRK)
                cur_trk = (uint8_t)bl;
            break;
        case PG_MIX:
            if (bl >= 0) {
                if (dn && bl < (int32_t)NTRK) {     /* a black key mutes its track; ARP held + it: solo */
                    uint8_t solo = (uint8_t)(((1u << NTRK) - 1u) & ~(1u << bl));
                    if (arp_held) { arp_turned = 1; prj.mute = prj.mute == solo ? 0 : solo; }
                    else prj.mute ^= (uint8_t)(1u << bl);
                    touched();
                }
            }
            else if (cur_trk < NTRK && dn) { key_down(k); keys_played |= 1u << k; }
            break;
        case PG_SONG:
            if (dn && w >= 0 && song_cur <= prj.song_len && song_cur < NSONG) {
                follow = 0;                     /* (an edit: the cursor stops following the song) */
                if (song_cur == prj.song_len) song_insert(song_cur, (uint32_t)w, 1);
                else { prj.song[song_cur].pat = (uint8_t)w; touched(); }
                ui_pat = (uint8_t)w;
                if (song_cur + 1u < NSONG) song_cur++;
            }
            break;
        case PG_PROJ:
            if (dn && w >= 0 && w < (int32_t)NSLOT && pm_state == PMS_SLOT && (w > 0 || pm_act == PM_LOAD)) pm_slot = (uint8_t)w;
            break;
        case PG_GROOVE:                         /* a white key: its step */
            if (dn && w >= 0 && w < (int32_t)clampi(prj.grv.len, 1, 16)) gr_cur = (uint8_t)w;
            break;
        case PG_OPT: case PG_HELP:
            break;
        default:
            if (dn) { key_down(k); keys_played |= 1u << k; }
            break;
        }
    }
}

/* SELECT along the pages of a button (GLO: SONG, GROOVE, OPTIONS; ENV: ENV, MIX; FX: SENDS, LIVE): d pages, stopping at the ends */
static void page_walk(const uint8_t *l, uint32_t n, int32_t d)
{
    uint32_t i, at = 0;
    for (i = 0; i < n; i++)
        if (l[i] == page) at = i;
    d = clampi((int32_t)at + d, 0, (int32_t)n - 1);
    if ((uint32_t)d != at) set_page(l[d]);
}
/* the factory sounds (SOUND, PRESETS). A sound nobody touched changes at once; every change is a step of UNDO (browsing is one
 * step: SAVE+OCT- brings back the sound you had before). A sound that was edited ('*') is never replaced by a turn: PRESETS only shows
 * the next factory sound (gold, with a '?'), OCT+ loads it, OCT- or a knob or leaving keeps the sound as it is. ARP held: the next
 * engine (its first sound) */
static void preset_load(uint32_t t, uint32_t k, int own_step)
{
    undo_push_snd(t, own_step ? 0 : 1);
    if (PRESETS[k].engine != ENG(t)) snd_page = 0;
    sound_preset(t, k);
    brush_note[t] = lane_base(t);
    touched();
    if (!sq_play) try_note(t, brush_note[t], 12, 0, 0);
}
static void preset_commit(void)
{
    uint32_t t = pre_cand_t;
    if (pre_cand == 0xFF || t >= NTRK) return;
    if (PRESETS[pre_cand].engine == E_FM && !fm_free(t)) { toast(TR("FM: UNE SEULE PISTE", "FM: ONE TRACK ONLY", "FM: UNA SOLA PISTA")); pre_cand = 0xFF; return; }
    preset_load(t, pre_cand, 1);
    pre_cand = 0xFF;
    toast(TR("CHARGE", "LOADED", "CARGADO"));
}
static void preset_turn(int32_t d)
{
    uint32_t t = trk(), k, from;
    int showing = pre_cand != 0xFF && pre_cand_t == t;
    from = showing ? pre_cand : prj.snd[t].preset;
    if (arp_held) {                             /* the engine */
        int32_t ne = (int32_t)PRESETS[from % NPRESET].engine + (d > 0 ? 1 : -1);
        uint32_t e;
        arp_turned = 1;
        if (ne == E_FM && !fm_free(t)) ne += d > 0 ? 1 : -1;   /* FM: one track only */
        e = (uint32_t)clampi(ne, 0, E_N - 1);
        if (e == E_FM && !fm_free(t)) { toast(TR("FM: UNE SEULE PISTE", "FM: ONE TRACK ONLY", "FM: UNA SOLA PISTA")); return; }
        if (e == PRESETS[from % NPRESET].engine) return;
        k = preset_first(e);
    } else {
        uint32_t guard = NPRESET;
        k = from % NPRESET;
        do                                      /* the next factory sound (FM: only if no other track has it) */
            k = (uint32_t)(((int32_t)k + (d > 0 ? 1 : -1) + (int32_t)NPRESET) % (int32_t)NPRESET);
        while (--guard && PRESETS[k].engine == E_FM && !fm_free(t));
    }
    if (!sound_is_preset(t)) {                  /* edited: only shown */
        if (!showing) toast(TR("OCT+: CHARGER", "OCT+: LOAD IT", "OCT+: CARGAR"));
        pre_cand = (uint8_t)k;
        pre_cand_t = (uint8_t)t;
        pre_cand_ms = fm1_ms;
        return;
    }
    pre_cand = 0xFF;
    preset_load(t, k, 0);
}

static void ui_input(void)
{
    uint32_t rel, b = fm1_input_edges(&rel), i;
    int32_t d;
    if (b | rel) last_input_ms = fm1_ms;
    if (page == PG_BOOT) {
        uint32_t e, moved = 0;
        for (e = 0; e < 7u; e++)
            if (fm1_enc_take(e) != 0) moved = 1;
        if (b || moved || fm1_in.notes) {      /* (the screen stays until something is touched: that touch is only this) */
            boot_eat = b | fm1_in.buttons;
            prev_keys = fm1_in.notes;           /* (a key held: not a note when the page comes) */
            (void)fm1_input_note_edges();
            set_page(PG_TRACK);
        }
        return;
    }
    for (i = 0; i < 14u; i++) {
        if ((b >> i & 1u) && home_held && i != BTN_HOME) home_did = 1;   /* HOME + a button: a chord, not a tap */
        if (b >> i & 1u) button_down(i);
        if ((rel >> i & 1u) && (boot_eat >> i & 1u)) { boot_eat &= ~(1u << i); continue; }
        if (rel >> i & 1u) button_up(i);
    }
    if (home_held && !home_did && page != PG_HELP && fm1_ms - home_down_ms > HOLD_HOME_MS) set_page(PG_HELP);
    if (fx_down && page == PG_SEND && fm1_ms - fx_down_ms > HOLD_FX_MS) set_page(PG_FX);   /* held: LIVE, while it is */
    if (save_held && !tools_open && !save_did && fm1_ms - save_down_ms > HOLD_SAVE_MS) { tools_open = 1; tools_sel = 0; }

    if ((d = fm1_enc_take(ENC_SELECT)) != 0 && rec_held) {   /* REC held + SELECT: the rows a note jumps */
        char s[16];
        last_input_ms = fm1_ms;
        rec_turned = 1;
        prj.step = (uint8_t)clampi(prj.step + d, 0, 16);
        touched();
        s[0] = 0;
        fmt_u(str_cat(s, TR("SAUT ", "STEP ", "SALTO ")), prj.step, 0);
        toast(s);
        d = 0;
    }
    if (d != 0 && tools_open) {                 /* the tools: SELECT picks one */
        last_input_ms = fm1_ms;
        tools_sel = (uint8_t)clampi((int32_t)tools_sel + d, 0, (int32_t)TOOL_N - 1);
        d = 0;
    }
    if (d != 0 && arp_held && page == PG_TRACK) {   /* ARP held + SELECT: a row in (+) or out (-) of the track */
        last_input_ms = fm1_ms;
        arp_turned = 1;
        rows_shift(d, 0);
        d = 0;
    }
    if (d != 0) {                               /* SELECT: on EDIT, LFO, ENV, FX and GLO the page; elsewhere the cursor */
        last_input_ms = fm1_ms;
        switch (page) {
        case PG_GRID: grid_page = (uint8_t)clampi((int32_t)grid_page + d, 0, (int32_t)grid_pages() - 1); grid_held = -1; break;
        case PG_SONG: case PG_GROOVE: case PG_OPT: {
            static const uint8_t L[3] = {PG_SONG, PG_GROOVE, PG_OPT};
            page_walk(L, 3, d);
            break;
        }
        case PG_ENV: case PG_MIX: {
            static const uint8_t L[2] = {PG_ENV, PG_MIX};
            page_walk(L, 2, d);
            break;
        }
        case PG_SEND: case PG_FX: {
            static const uint8_t L[2] = {PG_SEND, PG_FX};
            if (fx_sticky) page_walk(L, 2, d);
            break;
        }
        case PG_PROJ:
            if (pm_state == PMS_MENU) pm_sel = (uint8_t)clampi(pm_sel + d, 0, PM_N - 1);
            else if (pm_state == PMS_SLOT) pm_slot = (uint8_t)clampi(pm_slot + d, pm_act == PM_LOAD ? 0 : 1, NSLOT - 1);
            else if (pm_state == PMS_NAME) nm_pos = (uint8_t)clampi(nm_pos + d, 0, 14);
            break;
        case PG_SOUND: sg_move(d, 0); break;
        case PG_TRACK: {
            int32_t r = ((int32_t)cur_row + d) % (int32_t)plen();
            cur_row = (uint8_t)(r < 0 ? r + (int32_t)plen() : r);
            break;
        }
        default: break;
        }
    }
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0 && arp_held && page == PG_TRACK) {   /* ARP + ALGO: of the pattern */
        last_input_ms = fm1_ms;
        arp_turned = 1;
        rows_shift(d, 1);
        d = 0;
    }
    if (d != 0 && page == PG_SONG) {            /* ALGO on SONG: the entry of the chain */
        last_input_ms = fm1_ms;
        song_cur = (uint8_t)clampi(song_cur + d, 0, prj.song_len < NSONG ? prj.song_len : NSONG - 1);
        if (song_cur < prj.song_len) { ui_pat = prj.song[song_cur].pat; follow = 0; }
        d = 0;
    }
    if (d != 0 && page == PG_GROOVE) {          /* ... on GROOVE: the step */
        last_input_ms = fm1_ms;
        gr_cur = (uint8_t)clampi(gr_cur + d, 0, clampi(prj.grv.len, 1, 16) - 1);
        d = 0;
    }
    if (d != 0 && page == PG_OPT) {             /* ALGO on OPTIONS: the metronome (OFF, ON, ONLY WHILE REC) */
        last_input_ms = fm1_ms;
        set.metro = (uint8_t)clampi((int32_t)set.metro + d, 0, 2);
        set_changed();
        d = 0;
    }
    if (d != 0) {
        uint32_t sp_was = sp_at(trk(), snd_page), k;
        last_input_ms = fm1_ms;
        cur_trk = (uint8_t)clampi(cur_trk + d, 0, page == PG_MIX ? NTRK : NTRK - 1);
        snd_page = 0;                           /* the same page on the new track, if it has it (FM has seven) */
        for (k = 0; k < sp_n(trk()); k++)
            if (sp_at(trk(), k) == sp_was) { snd_page = (uint8_t)k; break; }
        grid_held = -1;
    }
    if ((d = fm1_enc_take(ENC_PRESETS)) != 0) {
        last_input_ms = fm1_ms;
        if (tools_open) {
        } else if (page == PG_SOUND) {          /* the page's cursor; the factory sound only on SOUND (see preset_turn) */
            uint32_t t = trk(), sp = sp_at(t, snd_page);
            if (sp == SP_OP || sp == SP_OPENV) fm_op = (uint8_t)clampi(fm_op + d, 1, 6);
            else if (sp == SP_TABLE) tb_cur = (uint8_t)clampi(tb_cur + d, 0, NTSTEP);
            else if (sp == SP_SMP) se_cur = (uint8_t)clampi(se_cur + d, 0, prj.snd[t].p[P_SMODE] ? 1 : 16);
            else if (sp == SP_SON1) preset_turn(d);
        } else if (page == PG_SONG) {
            if (song_cur < prj.song_len) {
                prj.song[song_cur].pat = (uint8_t)clampi(prj.song[song_cur].pat + d, 0, NPAT - 1);
                ui_pat = prj.song[song_cur].pat;
                follow = 0;
                touched();
            }
        } else if (page == PG_OPT) {            /* PRESETS on OPTIONS: the click's volume (you hear it) */
            set.mvol = (uint8_t)clampi((int32_t)set.mvol + d, 0, 2);
            set_changed();
            met_vol = set.mvol;
            met_trig = 2;
        } else if (page != PG_ENV && page != PG_MIX && page != PG_GROOVE && page != PG_PROJ && page != PG_SEND)
            pat_select(d);
    }
    for (i = 0; i < 4u; i++)
        if ((d = fm1_enc_take(ENC_KNOB1 + i)) != 0) {
            last_input_ms = fm1_ms;
            knob(i, d);
        }
    keys_input();
}

static void ui_leds(void)
{
    uint32_t i, k, t = trk();
    static const uint8_t PAGE_BTN[PG_N] = {BTN_HOME, BTN_SEQ, BTN_EDIT, BTN_ENV, BTN_ENV, BTN_SEL, BTN_SEL, BTN_GLO,
                                           BTN_SAVE, BTN_GLO, BTN_FX, BTN_HOME, 255, BTN_GLO, BTN_FX};   /* (GROOVE, OPTIONS: GLO) */
    for (i = 0; i < 41u; i++) led_want[i] = 0;
    for (i = 2; i < 12u; i++) led_want[i] = 1;   /* the page buttons glow, the one open is lit */
    if (page < PG_N && PAGE_BTN[page] < 14u) led(page == PG_SOUND && sg_lfo(t, snd_page) ? BTN_LFO : PAGE_BTN[page], 2);
    led(BTN_PLAY, sq_play ? 2u : 1u);
    if (dirty && page != PG_PROJ) led(BTN_SAVE, (fm1_ms / 500u) & 1u ? 2u : 1u);   /* unsaved changes: SAVE blinks */
    led(BTN_REC, rec ? ((fm1_ms / 300u) & 1u ? 2u : 1u) : 1u);
    led(BTN_OCT_DOWN, oct_ofs < 0 ? 2u : 1u);
    led(BTN_OCT_UP, oct_ofs > 0 ? 2u : 1u);
    switch (page) {
    case PG_GRID: {
        uint32_t gp = gpat(grid_page), gl = plen_of(gp);
        const pat_t *pt = &prj.pat[gp];
        uint32_t prow = sq_play && sq_pat == gp ? sq_row : 0xFFu;
        for (i = 0; i < 16u; i++) {
            uint32_t r = grow(grid_page) + i;
            uint32_t lit = r < gl && pt->c[r][t].note;
            if (r == prow) lit = !lit;
            led(KEY_LED(KEY_OF_WHITE[i]), lit ? 2u : (r < gl ? 1u : 0u));
        }
        for (i = 0; i < NTRK; i++) led(KEY_LED(KEY_OF_BLACK[i]), i == t ? 2u : 1u);
        break;
    }
    case PG_FX:
        for (i = 0; i < 16u; i++) led(KEY_LED(KEY_OF_WHITE[i]), (lfx_on >> (fx_bank * 16u + i) & 1u) ? 2u : 1u);
        for (i = 0; i < NTRK; i++) led(KEY_LED(KEY_OF_BLACK[i]), ((prj.mute | live_mute) >> i & 1u) ? 2u : 1u);
        break;
    case PG_MAGIC:
        for (i = 0; i < 16u; i++) led(KEY_LED(KEY_OF_WHITE[i]), 1);
        for (i = 0; i < NTRK; i++) led(KEY_LED(KEY_OF_BLACK[i]), i == t ? 2u : 1u);
        break;
    case PG_MIX:
        for (i = 0; i < NTRK; i++) led(KEY_LED(KEY_OF_BLACK[i]), (prj.mute >> i & 1u) ? 2u : 1u);
        break;
    case PG_SONG: case PG_PROJ:
        for (i = 0; i < 16u; i++) led(KEY_LED(KEY_OF_WHITE[i]), 1);
        break;
    case PG_GROOVE: {                           /* its steps glow, the one playing (or chosen) is lit */
        uint32_t len = (uint32_t)clampi(prj.grv.len, 1, 16), on = sq_play && prj.grv.on ? sq_gstep : gr_cur;
        for (i = 0; i < 16u; i++) led(KEY_LED(KEY_OF_WHITE[i]), i == on ? 2u : i < len ? 1u : 0u);
        break;
    }
    default:
        if (IS_TONAL(t) && prj.lock)            /* the scale's notes glow */
            for (k = 0; k < 27u; k++)
                if (in_scale(KEY0 + k)) led(KEY_LED(k), 1);
        for (k = 0; k < 27u; k++)
            if (fm1_in.notes >> k & 1u) led(KEY_LED(k), 2);
        break;
    }
    leds_flush();
}

/* ---- the MIDI clock in (OPTIONS, KNOB 3: IN): START (FA) or CONTINUE (FB), then the first tick starts the
 * song where PLAY would; STOP (FC) stops it. Each tick (F8) is counted for the sequencer (it waits for them, or
 * catches up) and timed: the last 24 (a beat) give the tempo. Taken in app_poll, as often as the screen allows */
#define CI_N 96u                                /* the tempo over four beats */
static uint32_t ci_at[CI_N + 1u], ci_n, ci_last;
static uint8_t ci_armed;                        /* START came: the next tick starts the song */
static void clock_in_reset(void)
{
    ci_n = 0;
    ci_armed = 0;
    clk_in_tick = 0;
    clk_in_ticks = 0;
}
static void clock_in(uint32_t st)
{
    uint32_t now = fm1_ticks();
    if (!clk_in)
        return;
    if (st == 0xFAu || st == 0xFBu) {
        ci_armed = 1;
        if (sq_play) sq_req = 2;
        return;
    }
    if (st == 0xFCu) {
        ci_armed = 0;
        if (sq_play) sq_req = 2;
        return;
    }
    if (st != 0xF8u)
        return;
    if (ci_n && now - ci_last > 500000u * FM1_TICKS_PER_US)
        ci_n = 0;                               /* half a second without one: the tempo is measured again */
    ci_last = now;
    ci_at[ci_n % (CI_N + 1u)] = now;
    ci_n++;
    if (ci_n >= 25u) {                          /* the last four beats (16ths of a frame a tick = us x 44100 x 16 */
        uint32_t n = ci_n - 1u < CI_N ? ci_n - 1u : CI_N, f, f1;   /* / 1e6 / n); the last one alone when it moved */
        uint32_t us = (now - ci_at[(ci_n - 1u - n) % (CI_N + 1u)]) / FM1_TICKS_PER_US;
        uint32_t us1 = (now - ci_at[(ci_n - 25u) % (CI_N + 1u)]) / FM1_TICKS_PER_US;
        uint32_t c = clk_in_tick;
        f = (uint32_t)clampi((int32_t)(us / 100u * 7056u / (n * 100u)), 367 * 16, 2756 * 16);   /* 300..40 BPM */
        f1 = (uint32_t)clampi((int32_t)(us1 / 100u * 7056u / 2400u), 367 * 16, 2756 * 16);
        if ((f1 > f ? f1 - f : f - f1) * 50u > f)   /* (2 %: a new tempo, followed in a beat) */
            f = f1;
        if (!c || (f > c ? f - c : c - f) * 1024u > c)   /* (0.1 %: the jitter does not move it; the ticks' nudge */
            clk_in_tick = f;                             /* in kawa_seq.c makes up the rest) */
    }
    if (ci_armed) {                             /* the first tick after START: the song starts on it */
        ci_armed = 0;
        clk_in_ticks = 1;
        sq_tick0 = 0;
        play_from_cursor();
        return;
    }
    clk_in_ticks++;
}

static void midi_in(void)                       /* USB-MIDI in: the clock; notes play the track under the cursor */
{
    static uint8_t midi_trk[128];               /* (the note-off goes where the note-on went) */
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        uint32_t t = trk();
        if (st >= 0xF8u) {
            clock_in(st);
            continue;
        }
        d1 &= 127u;
        if ((st & 0xF0u) == 0x90u && d2) {
            midi_trk[d1] = (uint8_t)t;
            pv_ms[t] = 0;
            ev_push(EV_ON, t, IS_TONAL(t) ? d1 : 1u + d1 % 16u, 1u + d2 / 9u, 0, 0);
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            ev_push(EV_OFF, midi_trk[d1] < NTRK ? midi_trk[d1] : t, d1, 0, 0, 0);
    }
}

/* ---- the MIDI clock out (OPTIONS, KNOB 3: OUT): start, 24 ticks a beat, stop, over USB. Sent from platform_poll,
 * called often (between the screen's bands too), so the ticks keep close to the sound */
static uint32_t clk_sent;
static void app_poll(void)
{
    uint32_t out = set.clock == 1u;
    midi_in();
    if (clk_start) {
        clk_start = 0;
        clk_sent = clk_ticks;
        if (out) midi_send(0xFA, 0, 0);         /* (the first tick follows the start) */
        if (clk_sent) clk_sent--;
    }
    while (clk_sent != clk_ticks) {
        clk_sent++;
        if (out) midi_send(0xF8, 0, 0);
    }
    if (clk_stop) {
        clk_stop = 0;
        if (out) midi_send(0xFC, 0, 0);
    }
}

/* ================================================================ the main loop */
static void usr_boot(void);
static void kw_poll(void);
static void app_init(void)
{
    uint32_t i;
    keys_init();
    for (i = 0; i <= 20u; i++) {                /* the gauge: 270 degrees from bottom left */
        uint32_t ph = 0x60000000u - i * (0xC0000000u / 20u);
        arc_x[i] = (int8_t)((sine(ph + 0x40000000u) * 13) >> 15);
        arc_y[i] = (int8_t)(-(sine(ph) * 13) >> 15);
    }
    set.font = FN_DOUCE;                        /* (what the settings of 0.2 and 0.4 leave as they are: font, metronome) */
    set.metro = 0;
    set.mvol = 1;
    if ((settings_load(&set, sizeof set) == 0 || settings_load(&set, 5u) == 0 || settings_load(&set, 4u) == 0) &&
        set.lang < LANG_N && set.theme < NTHEME) {   /* (5 bytes: 0.4's, before the metronome; 4: 0.2's, before the font) */
        lang = set.lang;
        oct_ofs = (int8_t)clampi(set.oct, -3, 3);
        theme_set(set.theme);
        g_font_set(set.font);
        if (set.clock > 2u) set.clock = 0;
        if (set.metro > 2u) set.metro = 0;
        if (set.mvol > 2u) set.mvol = 1;
        clk_in = (uint8_t)(set.clock == 2u);
    } else {
        theme_set(0);
        g_font_set(FN_DOUCE);
        set.lang = lang;
        set.font = g_font;
    }
    usr_boot();                                 /* the user's samples (kawa_sysex.c) */
    if (prj_load(0)) {
#if defined(KAWA_WASM)
        prj_demo();                             /* the browser starts with the demo; the FM-1 still starts empty */
        sq_req = 1u;                            /* POWER ON is a browser gesture, so audio is enabled */
#else
        prj_new();                              /* the FM-1 starts with an empty song (the demo is in PROJECTS) */
#endif
    }
    proj_cur = prj.home >= 1u && prj.home < NSLOT ? prj.home : 0xFF;   /* (SAVE goes back into its slot) */
    boot_ms = fm1_ms;
    last_input_ms = fm1_ms;
    g_force = 1;
}

static void ui_tick(void)                       /* what the panel keeps up with, apart from the keys and the screen */
{
    static uint32_t scan_ms;
    if (pre_cand != 0xFF && (page != PG_SOUND || sp_at(trk(), snd_page) != SP_SON1 || trk() != pre_cand_t || fm1_ms - pre_cand_ms > 8000u))
        pre_cand = 0xFF;                        /* (the factory sound shown: dropped when the page, the track or the time moves on) */
    if (fm1_ms - scan_ms > 700u) {              /* (the first-start hint: nothing written anywhere) */
        uint32_t k;
        scan_ms = fm1_ms;
        for (k = 0; k < NPAT && pat_empty(&prj.pat[k]); k++) {}
        song_empty = (uint8_t)(k >= NPAT);
    }
}

static void app_step(void)
{
    ui_input();
    kw_poll();
    if (sq_play && follow && page != PG_SONG && page != PG_PROJ && page != PG_OPT && page != PG_GROOVE && ui_pat != sq_pat &&
        !(pat_linked(ui_pat) && sq_pat == ui_pat + 1u) && !(sq_pat + 1u == ui_pat && pat_linked(sq_pat))) {
        ui_pat = sq_pat;                        /* playing: the screen follows (until the pattern is changed by hand) */
        if (cur_row >= plen()) cur_row = 0;
    }
    if (sq_play && follow && prj.mode && page == PG_SONG) {
        song_cur = sq_songpos;                  /* (and the pattern the knobs act on is the one playing) */
        if (song_cur < prj.song_len) ui_pat = prj.song[song_cur].pat;
    }
    if (cur_row >= plen())
        cur_row = 0;
    if (grid_page >= grid_pages())
        grid_page = 0;
    try_note_ends();
    ui_tick();
    met_mode = set.metro;                       /* (the audio interrupt reads them) */
    met_vol = set.mvol;
    met_rec = rec;
    if (clk_in && sq_play && fm1_ticks() - ci_last > 1000000u * FM1_TICKS_PER_US &&   /* (a second) */
        fm1_ms - toast_ms > 2500u)              /* PLAY on the clock in, and no clock: it waits for it */
        toast(TR("ATTEND L'HORLOGE MIDI", "WAITING FOR MIDI CLOCK", "ESPERA EL RELOJ MIDI"));
    if (fm1_ms - ui_frame_ms >= 33u) {          /* ~30 frames a second, only what changed is sent */
        ui_frame_ms = fm1_ms;
        ui_leds();
        g_frame_send(draw_all);
    }
    if (set_dirty && !sq_play && quiet() && fm1_ms - set_ms > 2000u) {
        set_dirty = 0;                          /* the settings (erasing stalls the sound: only when silent) */
        settings_save(&set, sizeof set);
    }
    if (auto_due && !sq_play && quiet() && fm1_ms - dirty_ms > 8000u && fm1_ms - last_input_ms > 8000u) {
        uint32_t d = dirty;                     /* the autosave (erasing stalls the sound: only when silent) */
        if (prj_save(0)) dirty_ms = fm1_ms;     /* (a flash error: again in 8 s, not at every pass) */
        else if (d) toast("AUTO");
    }
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
/* the emulator reads what the knobs do on this page (their labels under the knobs), the page, and
 * puts the user's samples in (drag and drop: the page decodes, resamples and mu-law encodes them) */
static uint8_t w_smp_stage[USR_CAP];
static char w_smp_name[16];
WASM_EXPORT("kawa_page") uint32_t kawa_page(void) { return page; }
WASM_EXPORT("kawa_knob_label") const char *kawa_knob_label(uint32_t i) { return i < 4u ? knob_lab[i] : ""; }
WASM_EXPORT("kawa_track") uint32_t kawa_track(void) { return cur_trk; }
WASM_EXPORT("kawa_theme") uint32_t kawa_theme(void) { return theme; }
WASM_EXPORT("kawa_sample_buf") uint8_t *kawa_sample_buf(void) { return w_smp_stage; }
WASM_EXPORT("kawa_sample_name") char *kawa_sample_name(void) { return w_smp_name; }
WASM_EXPORT("kawa_sample_size") uint32_t kawa_sample_size(void) { return USR_CAP; }
WASM_EXPORT("kawa_sample_commit") void kawa_sample_commit(uint32_t slot, uint32_t len, uint32_t rate)
{
    usr_put(slot, w_smp_stage, len, rate, w_smp_name);
    wv_src = 0xFF;
    toast(TR("SAMPLE PRET !", "SAMPLE READY !", "SAMPLE LISTO !"));
}
WASM_EXPORT("kawa_sample_slot_name") const char *kawa_sample_slot_name(uint32_t slot)
{
    const char *n = usr_name(slot);
    return n ? n : "";
}
/* the project slots live in the page's memory: the page keeps them (localStorage) and puts them back */
WASM_EXPORT("kawa_store") uint8_t *kawa_store(void) { return &host_flash[0][0]; }
WASM_EXPORT("kawa_store_size") uint32_t kawa_store_size(void) { return NSLOT * SLOT_SIZE; }
WASM_EXPORT("kawa_store_gen") uint32_t kawa_store_gen(void) { return store_gen; }
WASM_EXPORT("kawa_store_loaded") void kawa_store_loaded(void)   /* the page put the slots back: the autosave, if any */
{
    uint32_t k;
    if (!prj_load(0)) { ui_pat = 0; cur_row = 0; song_cur = 0; }
    for (k = 0; k < NSLOT; k++) names_ok[k] = (uint8_t)slot_valid(k, slot_name[k]);
    g_force = 1;
}
#endif
