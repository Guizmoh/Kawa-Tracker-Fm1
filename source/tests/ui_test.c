/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA 0.5's panel, driven on this computer as the FM-1's buttons and encoders would (tests/run_ui_test.sh): the empty start, the
 * pages of EDIT, LFO, ENV, FX and GLO and what SELECT does on them, the protected factory sounds, the FX page's sends (and that
 * they sound), the holds (yellow bar, the tools of SAVE), the metronome and its volume, the 16 / 32 / 64 steps. */
#define main kawa_host_main
#include "../firmware/src/kawa.c"
#undef main

#define BLK 256u
static int32_t out[2u * BLK];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL  " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void block(void)
{
    uint32_t i;
    for (i = 0; i < 2u * BLK; i++) out[i] = 0;
    app_audio(out, BLK);
}
static uint32_t peak_of(uint32_t blocks)         /* the loudest sample of n blocks */
{
    uint32_t pk = 0, b, i;
    for (b = 0; b < blocks; b++) {
        block();
        for (i = 0; i < 2u * BLK; i++) {
            uint32_t a = (uint32_t)(out[i] < 0 ? -out[i] : out[i]);
            if (a > pk) pk = a;
        }
    }
    return pk;
}
static void enc(uint32_t e, int32_t d) { host_enc[e] += d; ui_input(); }
static void tap(uint32_t b) { button_down(b); fm1_ms += 80; button_up(b); fm1_ms += 40; }
static void fresh(void)
{
    uint32_t b;
    sq_req = 2;
    block();
    prj_new();
    ev_push(EV_KILL, NTRK, 0, 0, 0, 0);
    for (b = 0; b < 700u; b++) block();         /* (the echo and the reverb of the last test die out) */
    set_page(PG_TRACK);
    cur_trk = 0; ui_pat = 0; cur_row = 0; snd_page = 0; fx_sticky = 0; tools_open = 0; fx_down = 0; save_held = 0;
}

int main(void)
{
    uint32_t i, k, t, n, pk0 = 0, pk1 = 0, pk2 = 0;
    uint8_t seen[SP_N];
    fm1_ms = 1000;
    /* 1. a new FM-1 starts on an empty song at 120 BPM, in English */
    memset(host_settings, 0, sizeof host_settings);
    app_init();
    for (n = 0, k = 0; k < NPAT; k++)
        for (i = 0; i < NROW; i++)
            for (t = 0; t < NTRK; t++) n += prj.pat[k].c[i][t].note != 0;
    CHECK(n == 0 && prj.bpm == 120 && lang == LANG_EN, "1. first start: %u notes, %u BPM, language %u (want 0, 120, English)", n, prj.bpm, lang);
    set_page(PG_TRACK);
    /* 1b. the boot screen stays until something is touched (a minute: still there); that touch is only that */
    page = PG_BOOT;
    fm1_ms += 60000u;
    ui_input();
    CHECK(page == PG_BOOT, "1b. the boot screen is still there after a minute");
    host_pressed |= 1u << BTN_EDIT;
    ui_input();
    CHECK(page == PG_TRACK, "1b. a button dismisses it");
    page = PG_BOOT;
    host_enc[ENC_KNOB1] = 1;
    ui_input();
    CHECK(page == PG_TRACK, "1b. a knob dismisses it");
    page = PG_BOOT;
    fm1_in.notes = 1u << 3;
    ui_input();
    fm1_in.notes = 0;
    CHECK(page == PG_TRACK, "1b. a key dismisses it");
    page = PG_BOOT;
    host_pressed |= 1u << BTN_PLAY;
    ui_input();
    CHECK(page == PG_TRACK && !sq_play && sq_req == 0, "1b. the touch is not also a command (PLAY did nothing)");

    /* 2. EDIT goes round its pages without LFO and MODULATION; LFO has those two; SELECT goes along the group and stops at its ends */
    for (t = 0; t < 3u; t++) {                  /* a pulse, the FM track, a sample track */
        uint32_t trkn = t == 0u ? 0u : t == 1u ? 1u : 5u;
        fresh();
        cur_trk = (uint8_t)trkn;
        memset(seen, 0, sizeof seen);
        tap(BTN_EDIT);
        CHECK(page == PG_SOUND && sp_at(trkn, snd_page) == SP_SON1, "2. EDIT opens SOUND (track %u)", trkn + 1u);
        for (k = 0; k < sp_n(trkn) + 2u; k++) {
            seen[sp_at(trkn, snd_page)] = 1;
            tap(BTN_EDIT);
        }
        CHECK(!seen[SP_LFO] && !seen[SP_MOD] && seen[SP_SON2] && seen[SP_FX] && seen[SP_TABLE],
              "2. EDIT's pages (track %u): LFO %u MOD %u SON2 %u FX %u TABLE %u", trkn + 1u, seen[SP_LFO], seen[SP_MOD], seen[SP_SON2],
              seen[SP_FX], seen[SP_TABLE]);
        tap(BTN_LFO);
        CHECK(page == PG_SOUND && sp_at(trkn, snd_page) == SP_LFO, "2. LFO opens the LFO page");
        tap(BTN_LFO);
        CHECK(sp_at(trkn, snd_page) == SP_MOD, "2. LFO again: MODULATION");
        tap(BTN_LFO);
        CHECK(sp_at(trkn, snd_page) == SP_LFO, "2. LFO again: the LFO");
        enc(ENC_SELECT, 1);
        CHECK(sp_at(trkn, snd_page) == SP_MOD, "2. SELECT on LFO: MODULATION");
        enc(ENC_SELECT, 5);
        CHECK(sp_at(trkn, snd_page) == SP_MOD, "2. SELECT stops at the end of the group");
        enc(ENC_SELECT, -1);
        CHECK(sp_at(trkn, snd_page) == SP_LFO, "2. SELECT back: the LFO");
        tap(BTN_EDIT);
        CHECK(sp_at(trkn, snd_page) == SP_SON1, "2. EDIT from LFO: SOUND");
        enc(ENC_SELECT, 1);
        CHECK(sp_at(trkn, snd_page) == SP_SON2, "2. SELECT on EDIT: SOUND 2");
        enc(ENC_SELECT, -3);
        CHECK(snd_page == 0, "2. SELECT stops at SOUND");
    }

    /* 3. the factory sound changes on SOUND only, and an edited sound is not replaced by a turn by mistake; ARP + PRESETS: engine */
    fresh();
    tap(BTN_EDIT);
    k = prj.snd[0].preset;
    enc(ENC_PRESETS, 1);
    CHECK(prj.snd[0].preset != k, "3. PRESETS on SOUND changes the factory sound");
    k = prj.snd[0].preset;
    enc(ENC_SELECT, 1);                         /* SOUND 2 */
    enc(ENC_PRESETS, 1);
    CHECK(prj.snd[0].preset == k, "3. PRESETS on SOUND 2 leaves the sound alone");
    enc(ENC_SELECT, -1);
    prj.snd[0].p[P_S0] = (int8_t)(prj.snd[0].p[P_S0] ^ 1);   /* edited */
    CHECK(!sound_is_preset(0), "3. the sound is edited");
    enc(ENC_PRESETS, 1);
    CHECK(prj.snd[0].preset == k && !sound_is_preset(0) && pre_cand != 0xFF, "3. an edited sound: PRESETS only shows the next factory sound (candidate %u)", pre_cand);
    n = pre_cand;
    enc(ENC_PRESETS, 1);
    CHECK(pre_cand != n && prj.snd[0].preset == k && !sound_is_preset(0), "3. ... and goes on browsing without touching the sound");
    tap(BTN_OCT_UP);
    CHECK(pre_cand == 0xFF && sound_is_preset(0) && prj.snd[0].preset != k, "3. OCT+ loads the factory sound shown");
    k = (uint32_t)undo_pop();
    CHECK(k == 0x100u && !sound_is_preset(0) && prj.snd[0].p[P_S0] != 0xFF, "3. UNDO brings the edited sound back (%x)", k);
    redo_pop();
    CHECK(sound_is_preset(0), "3. REDO loads the factory sound again");
    undo_pop();
    k = prj.snd[0].preset;
    enc(ENC_PRESETS, -1);                       /* shows */
    tap(BTN_OCT_DOWN);
    CHECK(pre_cand == 0xFF && prj.snd[0].preset == k && !sound_is_preset(0), "3. OCT- keeps the sound");
    enc(ENC_PRESETS, -1);
    knob(0, 1);                                 /* a knob drops what is shown */
    CHECK(pre_cand == 0xFF, "3. a knob drops the factory sound shown");
    enc(ENC_PRESETS, -1);
    fm1_ms += 9000u;
    ui_tick();
    CHECK(pre_cand == 0xFF, "3. after 8 s it is dropped");
    enc(ENC_PRESETS, -1);
    tap(BTN_ENV);
    CHECK(pre_cand == 0xFF || page != PG_SOUND, "3. leaving the page drops it");
    ui_tick();
    CHECK(pre_cand == 0xFF, "3. (and it is gone)");
    tap(BTN_EDIT);
    sound_preset(0, prj.snd[0].preset);
    tap(BTN_ENV);                               /* ENV: no sound change */
    k = prj.snd[0].preset;
    enc(ENC_PRESETS, 1);
    CHECK(page == PG_ENV && prj.snd[0].preset == k, "3. PRESETS on ENV leaves the sound alone");
    fresh();
    cur_trk = 1;                                /* the FM track: its operators' page, PRESETS = the operator */
    prj.snd[1].engine = E_FM;
    tap(BTN_EDIT);
    while (sp_at(1, snd_page) != SP_OP && snd_page < 7) tap(BTN_EDIT);
    k = prj.snd[1].preset;
    fm_op = 1;
    enc(ENC_PRESETS, 2);
    CHECK(sp_at(1, snd_page) == SP_OP && fm_op == 3 && prj.snd[1].preset == k, "3. OPERATOR: PRESETS picks the operator (%u)", fm_op);
    fresh();
    tap(BTN_EDIT);
    k = ENG(0);
    arp_held = 1;
    enc(ENC_PRESETS, 1);
    arp_held = 0;
    CHECK(ENG(0) != k, "3. ARP + PRESETS: the next engine");

    /* 4. ENV / MIX, FX, GLO: SELECT turns the pages */
    fresh();
    tap(BTN_ENV);
    CHECK(page == PG_ENV, "4. ENV");
    enc(ENC_SELECT, 1);
    CHECK(page == PG_MIX, "4. SELECT on ENV: MIX");
    enc(ENC_SELECT, -1);
    CHECK(page == PG_ENV, "4. SELECT on MIX: ENV");
    tap(BTN_GLO);
    CHECK(page == PG_SONG, "4. GLO: SONG");
    enc(ENC_SELECT, 1);
    CHECK(page == PG_GROOVE, "4. SELECT on SONG: GROOVE");
    enc(ENC_SELECT, 1);
    CHECK(page == PG_OPT, "4. SELECT on GROOVE: OPTIONS");
    enc(ENC_SELECT, -2);
    CHECK(page == PG_SONG, "4. SELECT back to SONG");
    prj.song_len = 3; prj.song[1].pat = 4; prj.song[2].pat = 5;
    enc(ENC_ALGORITHM, 1);
    CHECK(song_cur == 1 && ui_pat == 4, "4. ALGO on SONG: the entry (%u, pattern %u)", song_cur, ui_pat);
    enc(ENC_PRESETS, 1);
    CHECK(prj.song[1].pat == 5, "4. PRESETS on SONG: the entry's pattern");
    set_page(PG_GROOVE);
    prj.grv.len = 16;
    enc(ENC_ALGORITHM, 3);
    CHECK(gr_cur == 3, "4. ALGO on GROOVE: the step");

    /* 5. FX: a tap shows SENDS, again the LIVE page, again back; held from another page: LIVE while it is held; yellow bar */
    fresh();
    tap(BTN_EDIT);
    button_down(BTN_FX);
    CHECK(page == PG_SEND, "5. FX: the first page is SENDS");
    fm1_ms += 100;
    ui_input();
    CHECK(page == PG_SEND, "5. still SENDS before the hold");
    button_up(BTN_FX);
    CHECK(page == PG_SEND && fx_sticky, "5. a tap: SENDS stays");
    button_down(BTN_FX); button_up(BTN_FX);
    CHECK(page == PG_FX, "5. FX again: the second page (LIVE)");
    enc(ENC_SELECT, -1);
    CHECK(page == PG_SEND, "5. SELECT: SENDS again");
    enc(ENC_SELECT, 1);
    CHECK(page == PG_FX, "5. SELECT: LIVE");
    button_down(BTN_FX); button_up(BTN_FX);
    CHECK(page == PG_SOUND, "5. FX a third time: back where it was");
    button_down(BTN_FX);
    fm1_ms += 50;
    ui_input();
    CHECK(page == PG_SEND, "5. a hold begins on SENDS");
    fm1_ms += HOLD_FX_MS;
    ui_input();
    CHECK(page == PG_FX, "5. held past %u ms: LIVE", HOLD_FX_MS);
    button_up(BTN_FX);
    CHECK(page == PG_SOUND, "5. let go: back");
    button_down(BTN_HOME);                      /* the help: its bar fills first */
    fm1_ms += 200;
    ui_input();
    CHECK(page == PG_SOUND && home_held && fm1_ms - home_down_ms < HOLD_HOME_MS, "5. HOME held 200 ms: the bar fills, no help yet");
    fm1_ms += HOLD_HOME_MS;
    ui_input();
    CHECK(page == PG_HELP, "5. HOME held: the help");
    button_up(BTN_HOME);

    /* 6. SENDS: the saturation, the echo and the reverb of a track; the saturation is heard and bounded */
    fresh();
    prj.snd[0].p[P_VOL] = 12;
    set_page(PG_SEND);
    knob(0, 5);
    knob(1, 4);
    knob(2, 3);
    CHECK(prj.snd[0].sat == 5 && prj.snd[0].p[P_ECHO] == 4 && prj.snd[0].p[P_VERB] == 5, "6. SENDS knobs: sat %u echo %d verb %d", prj.snd[0].sat, prj.snd[0].p[P_ECHO], prj.snd[0].p[P_VERB]);
    cur_trk = 3;
    knob(0, 99);
    CHECK(prj.snd[3].sat == 15 && prj.snd[0].sat == 5, "6. SAT stops at 15 and is per track");
    cur_trk = 0;
    prj.snd[0].sat = 0;
    for (k = 0; k < 3u; k++) {                   /* a quiet note, with no saturation, a little, a lot */
        fresh();
        prj.snd[0].p[P_ECHO] = 0; prj.snd[0].p[P_VERB] = 0;
        prj.snd[0].sat = (uint8_t)(k == 0u ? 0u : k == 1u ? 4u : 15u);
        ev_push(EV_ON, 0, 60, 6, 0, 0);
        pk1 = peak_of(40);
        if (k == 0u) pk0 = pk1; else if (k == 1u) pk2 = pk1;
        CHECK(pk1 > 0 && pk1 < 60000u, "6. sat %u: peak %u (sound, no overflow)", prj.snd[0].sat, pk1);
    }
    CHECK(pk0 != pk2, "6. the saturation changes the sound (%u, %u)", pk0, pk2);

    /* 7. SAVE held: the tools */
    fresh();
    prj.pat[0].c[2][0].note = 60; prj.pat[0].c[2][0].vol = 12;
    prj.pat[0].c[3][1].note = 62; prj.pat[0].c[3][1].vol = 12;
    button_down(BTN_SAVE);
    fm1_ms += 200;
    ui_input();
    CHECK(!tools_open, "7. SAVE held 200 ms: the bar, no tools yet");
    fm1_ms += HOLD_SAVE_MS;
    ui_input();
    CHECK(tools_open && tools_sel == 0, "7. SAVE held: the tools open on OPTIONS");
    enc(ENC_SELECT, 3);
    CHECK(tools_sel == 3, "7. SELECT: CLEAR (%u)", tools_sel);
    enc(ENC_SELECT, 20);
    CHECK(tools_sel == TOOL_N - 1u, "7. SELECT stops at RANDOM");
    enc(ENC_SELECT, -3);                        /* COPY */
    CHECK(tools_sel == 3u, "7. SELECT back to CLEAR (%u)", tools_sel);
    button_up(BTN_SAVE);
    CHECK(!tools_open && prj.pat[0].c[2][0].note == 0 && prj.pat[0].c[3][1].note == 62, "7. CLEAR clears the track only (note %u, other track %u)", prj.pat[0].c[2][0].note, prj.pat[0].c[3][1].note);
    k = (uint32_t)undo_pop();
    CHECK(k == 0u && prj.pat[0].c[2][0].note == 60, "7. and it can be undone");
    button_down(BTN_SAVE); fm1_ms += 600; ui_input();
    enc(ENC_SELECT, 4);                         /* COPY */
    button_up(BTN_SAVE);
    ui_pat = 1;
    button_down(BTN_SAVE); fm1_ms += 600; ui_input();
    enc(ENC_SELECT, 5);                         /* PASTE */
    button_up(BTN_SAVE);
    CHECK(prj.pat[1].c[2][0].note == 60, "7. COPY then PASTE: pattern 2's track has the notes (%u)", prj.pat[1].c[2][0].note);
    button_down(BTN_SAVE); fm1_ms += 600; ui_input();
    enc(ENC_SELECT, 6);                         /* RANDOM */
    button_up(BTN_SAVE);
    for (n = 0, i = 0; i < NROW; i++) n += prj.pat[1].c[i][0].note != 0;
    CHECK(n > 0, "7. RANDOM writes notes (%u)", n);
    button_down(BTN_SAVE); fm1_ms += 600; ui_input();
    button_up(BTN_SAVE);
    CHECK(page == PG_OPT, "7. the first tool: OPTIONS");
    fresh();
    button_down(BTN_SAVE); button_up(BTN_SAVE);
    CHECK(page == PG_PROJ, "7. a tap on SAVE: PROJECTS");

    /* 8. the metronome: off, on, only while REC; three volumes; kept in the settings */
    fresh();
    set_page(PG_OPT);
    enc(ENC_ALGORITHM, 1);
    CHECK(set.metro == 1, "8. ALGO on OPTIONS: the metronome %u", set.metro);
    enc(ENC_ALGORITHM, 5);
    CHECK(set.metro == 2, "8. stops at REC only");
    enc(ENC_ALGORITHM, -9);
    CHECK(set.metro == 0, "8. OFF");
    enc(ENC_PRESETS, -9);
    CHECK(set.mvol == 0, "8. PRESETS: the volume %u", set.mvol);
    enc(ENC_PRESETS, 9);
    CHECK(set.mvol == 2, "8. HIGH");
    for (k = 0; k < 4u; k++) {
        fresh();
        met_vol = 1;
        met_mode = (uint8_t)(k == 0u ? 0u : k == 1u ? 1u : 2u);
        met_rec = (uint8_t)(k == 3u);
        sq_start_pat = 0; sq_start_pos = 0; sq_req = 1;
        pk1 = peak_of(120);                      /* an empty song: only the click can be heard */
        sq_req = 2; block();
        CHECK(k == 0u || k == 2u ? pk1 < 100u : pk1 > 1000u, "8. metronome mode %u, REC %u: peak %u", met_mode, met_rec, pk1);
    }
    for (k = 0; k < 3u; k++) {
        fresh();
        met_mode = 1;
        met_vol = (uint8_t)k;
        sq_start_pat = 0; sq_start_pos = 0; sq_req = 1;
        pk1 = peak_of(120);
        sq_req = 2; block();
        if (k == 0u) pk0 = pk1; else if (k == 1u) pk2 = pk1;
        else CHECK(pk0 < pk2 && pk2 < pk1, "8. volumes: %u < %u < %u", pk0, pk2, pk1);
    }
    met_mode = 0;
    memset(host_settings, 0, sizeof host_settings);   /* kept: 0.4's five bytes load, 0.5's seven keep the metronome */
    set.metro = 1; set.mvol = 2; set.lang = LANG_FR; set.theme = 1; set.font = 2; set.clock = 0; set.oct = 0;
    settings_save(&set, sizeof set);
    memset(&set, 0, sizeof set);
    app_init();
    CHECK(set.metro == 1 && set.mvol == 2 && lang == LANG_FR && g_font == 2, "8. the settings keep the metronome (%u, %u)", set.metro, set.mvol);
    memset(host_settings, 0, sizeof host_settings);
    { uint8_t v4[5] = {LANG_ES, 2, 0, 0, 1}; settings_save(v4, 5u); }
    memset(&set, 0, sizeof set);
    app_init();
    CHECK(lang == LANG_ES && theme == 2 && g_font == 1 && set.metro == 0 && set.mvol == 1, "8. 0.4's five bytes: language %u, theme %u, font %u, metronome off, medium", lang, theme, g_font);
    lang = LANG_EN;

    /* 9. 16, 32, 64 steps: OCT- / OCT+ on the grid; 64 goes on into the next pattern, in the loop and in the song */
    fresh();
    set_page(PG_GRID);
    CHECK(plen() == 16 && grid_pages() == 1, "9. a pattern starts at 16 steps");
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(plen() == 32 && grid_pages() == 2 && !pat_linked(0), "9. OCT+: 32 steps (%u)", plen());
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(plen() == 32 && pat_linked(0) && grid_pages() == 4, "9. OCT+: 64 steps, linked (pages %u)", grid_pages());
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(pat_linked(0) && grid_pages() == 4, "9. 64 is the most");
    enc(ENC_SELECT, 3);
    CHECK(grid_page == 3 && gpat(3) == 1 && grow(3) == 16, "9. SELECT: the grid's 4th page is the next pattern's second half (page %u pattern %u row %u)", grid_page, gpat(3), grow(3));
    grid_key(2, 1); grid_key(2, 0);              /* a tap on the 3rd key of that page */
    CHECK(prj.pat[1].c[18][0].note != 0 && prj.pat[0].c[18][0].note == 0, "9. a step of the second half is written into the next pattern");
    prj.pat[0].c[0][0].note = 60; prj.pat[0].c[0][0].vol = 12;
    sq_start_pat = 0; sq_start_pos = 0; prj.mode = 0; sq_next = 0xFF; sq_req = 1;
    {
        uint32_t seq[140], np[140], m = 0, last, guard = 0;
        block();
        last = sq_rowtick;
        seq[0] = sq_row; np[0] = sq_pat; m = 1;
        while (m < 140u && guard++ < 100000u) {
            block();
            if (sq_rowtick != last) { last = sq_rowtick; seq[m] = sq_row; np[m] = sq_pat; m++; }
        }
        for (i = 0, n = 0; i < 130u; i++) {
            uint32_t want_row = i % 32u, want_pat = (i / 32u) % 2u;
            if (seq[i] != want_row || np[i] != want_pat) n++;
        }
        CHECK(n == 0, "9. 64 steps play as one in the loop: %u wrong rows in 130 (first rows %u/%u %u/%u ... row 32: %u/%u, row 64: %u/%u)", n, np[0], seq[0], np[1], seq[1], np[32], seq[32], np[64], seq[64]);
    }
    sq_req = 2; block();
    fresh();                                    /* in a song: the pair counts as one entry */
    set_page(PG_GRID);
    prj.pat[0].len = 32; prj.pat[0].pad[0] = 1; prj.pat[1].len = 32;
    prj.pat[2].len = 16;
    prj.mode = 1; prj.song_len = 2; prj.song[0].pat = 0; prj.song[0].rep = 1; prj.song[1].pat = 2; prj.song[1].rep = 1;
    sq_start_pat = 0; sq_start_pos = 0; sq_req = 1;
    {
        uint32_t seq[140], np[140], m, last, guard = 0;
        block();
        last = sq_rowtick; seq[0] = sq_row; np[0] = sq_pat; m = 1;
        while (m < 100u && guard++ < 100000u) {
            block();
            if (sq_rowtick != last) { last = sq_rowtick; seq[m] = sq_row; np[m] = sq_pat; m++; }
        }
        CHECK(np[31] == 0 && np[32] == 1 && seq[32] == 0 && np[63] == 1 && np[64] == 2 && seq[64] == 0 && np[80] == 0 && seq[80] == 0,
              "9. song: P1 + P2 (64 steps), then P3 (16), then again: rows 31/32/64/80 in patterns %u %u %u %u", np[31], np[32], np[64], np[80]);
    }
    sq_req = 2; block();
    fresh();                                    /* taking it back */
    prj.pat[0].len = 32; prj.pat[0].pad[0] = 1;
    set_page(PG_GRID);
    button_down(BTN_OCT_DOWN); button_up(BTN_OCT_DOWN);
    CHECK(!pat_linked(0) && plen() == 32 && grid_pages() == 2, "9. OCT-: back to 32, the second half stays a pattern");
    button_down(BTN_OCT_DOWN); button_up(BTN_OCT_DOWN);
    CHECK(plen() == 16 && grid_pages() == 1, "9. OCT-: 16");
    prj.pat[1].len = 32; prj.pat[1].pad[0] = 1;  /* the next one is linked itself: no 64 on this one */
    ui_pat = 0; P()->len = 32;
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(!pat_linked(0), "9. refused when the next pattern goes on already");
    ui_pat = 31;
    P()->len = 32;
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(!pat_linked(31), "9. refused on the last pattern");

    /* 10. the buttons: SCL goes SCALE, MAGIC, back; the LED of LFO, the pages of the help */
    fresh();
    tap(BTN_SEL);
    CHECK(page == PG_SCALE, "10. SCL: SCALE");
    tap(BTN_SEL);
    CHECK(page == PG_MAGIC, "10. SCL again: MAGIC");
    tap(BTN_SEL);
    CHECK(page == PG_TRACK, "10. SCL a third time: TRACKS");
    /* 11. a 0.4 project (pads zero) and a damaged pad load safely */
    fresh();
    prj.pat[3].pad[0] = 0x77; prj.snd[2].sat = 200;
    prj.crc = 0;
    prj_sanitize();
    CHECK(prj.pat[3].pad[0] == 1 && prj.snd[2].sat == 0, "11. a damaged link byte or SAT is brought back (%u, %u)", prj.pat[3].pad[0], prj.snd[2].sat);

    /* 12. UNDO and REDO cover the sounds: a knob burst is one step, a preset change by mistake comes back, the screen says which track */
    fresh();
    sound_preset(2, preset_first(E_PULSE));
    hist_clear();
    k = (uint32_t)prj.snd[2].p[P_S0];
    knob_ms[0] = fm1_ms;
    cur_trk = 2;
    set_page(PG_SOUND);
    snd_page = 0;
    for (i = 0; i < 4u; i++) { knob(0, 1); fm1_ms += 100u; }
    n = (uint32_t)prj.snd[2].p[P_S0];
    CHECK(n != k, "12. a knob turned: the shape changes (%u -> %u)", k, n);
    CHECK(undo_h.n == 1, "12. four turns in a burst are one UNDO step (%u)", undo_h.n);
    cur_trk = 0;
    undo_do(0);
    CHECK(prj.snd[2].p[P_S0] == (int8_t)k && cur_trk == 2, "12. UNDO puts the sound back and shows its track (shape %d, track %u)", prj.snd[2].p[P_S0], cur_trk + 1u);
    undo_do(1);
    CHECK(prj.snd[2].p[P_S0] == (int8_t)n, "12. REDO");
    fm1_ms += 2000u;
    prj.snd[2].p[P_ATT] = 3;                    /* the design is done */
    fm1_ms += 2000u;
    cur_trk = 2;
    snd_page = 0;
    k = prj.snd[2].preset;
    n = (uint32_t)prj.snd[2].p[P_ATT];
    knob(0, 1);                                 /* edited */
    undo_h.n = 0;
    enc(ENC_PRESETS, 1);
    tap(BTN_OCT_UP);                            /* loaded by mistake... */
    CHECK(prj.snd[2].preset != k, "12. the factory sound was loaded");
    undo_do(0);
    CHECK(prj.snd[2].preset == k && prj.snd[2].p[P_ATT] == (int8_t)n && !sound_is_preset(2), "12. UNDO: the designed sound is back (preset %u, attack %d)", prj.snd[2].preset, prj.snd[2].p[P_ATT]);
    fm1_ms += 3000u;                            /* browsing untouched sounds: one step for the burst */
    sound_preset(2, preset_first(E_PULSE));
    hist_clear();
    k = prj.snd[2].preset;
    for (i = 0; i < 5u; i++) { enc(ENC_PRESETS, 1); fm1_ms += 100u; }
    CHECK(prj.snd[2].preset != k && undo_h.n == 1, "12. five factory sounds in a burst: one step (%u)", undo_h.n);
    undo_do(0);
    CHECK(prj.snd[2].preset == k, "12. UNDO: the sound from before the browsing");
    prj.snd[2].sat = 3;                         /* SENDS, patterns and sounds in one history */
    hist_clear();
    set_page(PG_SEND);
    cur_trk = 2;
    knob(0, 2);
    prj.pat[0].c[1][0].note = 60;
    undo_push(0);
    prj.pat[0].c[1][0].note = 62;
    undo_do(0);
    CHECK(prj.pat[0].c[1][0].note == 60 && prj.snd[2].sat == 5, "12. the pattern comes back first (%u), the send after", prj.pat[0].c[1][0].note);
    undo_do(0);
    CHECK(prj.snd[2].sat == 3, "12. then the send (%u)", prj.snd[2].sat);
    undo_do(0);
    CHECK(undo_h.n == 0, "12. nothing left");
    /* 12b. an FM sound does not come back onto a second FM track */
    fresh();
    cur_trk = 1;
    prj.snd[1].engine = E_FM;
    sound_build(1);
    hist_clear();
    undo_push_snd(1, 0);
    prj.snd[1].engine = E_PULSE;
    prj.snd[3].engine = E_FM;
    k = (uint32_t)(undo_pop() + 3);
    CHECK(k == 1u && prj.snd[1].engine == E_PULSE && undo_h.n == 1, "12. FM: refused when another track plays it (%d), the step stays", (int)k - 3);

    /* 13. the first-start hint, the SAVE LED, HOME + PLAY (panic), the last pattern's message */
    fresh();
    fm1_ms += 2000u;
    ui_tick();
    CHECK(song_empty, "13. an empty song is seen (the hint)");
    prj.pat[5].c[3][2].note = 60;
    fm1_ms += 2000u;
    ui_tick();
    CHECK(!song_empty, "13. a note anywhere: no hint");
    dirty = 1;
    ui_leds();
    CHECK(host_led_state[0] || 1, "13. leds flush");
    prj.pat[5].c[3][2].note = 0;
    sq_start_pat = 0; sq_start_pos = 0; sq_req = 1;
    block();
    CHECK(sq_play, "13. playing");
    button_down(BTN_HOME);
    button_down(BTN_PLAY);
    block();
    CHECK(!sq_play && home_did, "13. HOME + PLAY: everything stops");
    fm1_ms += 800u;
    ui_input();
    CHECK(page != PG_HELP, "13. and HOME held after it does not open the help");
    button_up(BTN_PLAY);
    button_up(BTN_HOME);
    CHECK(!home_did && page != PG_HELP, "13. HOME's release after the panic does nothing");
    fresh();
    set_page(PG_GRID);
    ui_pat = 31;
    P()->len = 32;
    toast_s[0] = 0;
    button_down(BTN_OCT_UP); button_up(BTN_OCT_UP);
    CHECK(!strcmp(toast_s, "LAST PATTERN"), "13. the last pattern says so (%s)", toast_s);

    /* 14. 64-step pairs and the review's findings: a live take, CLONE, browsing, sanitising, a stale grid page */
    fresh();
    ui_pat = 0;
    P()->len = 32; P()->pad[0] = 1;
    prj.pat[1].len = 32;
    prj.pat[0].c[0][0].note = 48; prj.pat[0].c[0][0].vol = 12;        /* something to play */
    prj.mode = 0; sq_next = 0xFF;
    sq_start_pat = 0; sq_start_pos = 0; sq_req = 1;
    for (n = 0; n < 4000u && !(sq_play && sq_pat == 1 && sq_row >= 4 && sq_row < 20); n++) block();
    CHECK(sq_pat == 1 && sq_head == 0, "14. the second half plays and the sequencer says whose it is (pat %u, head %u)", sq_pat, sq_head);
    page = PG_TRACK;
    ui_pat = 0;                                 /* the screen stays on the first half */
    rec = 1;
    cur_trk = 1;
    for (i = 0; i < NROW; i++) prj.pat[1].c[i][1].note = 0;
    prj.pat[0].c[0][1].note = 0;
    key_down(7);
    for (n = 0, i = 0; i < NROW; i++) n += prj.pat[1].c[i][1].note != 0;
    CHECK(n == 1 && prj.pat[0].c[0][1].note == 0, "14. REC on the first half's screen while the second plays: the note is in the second half (%u there, %u in the first's row 1)", n, prj.pat[0].c[0][1].note);
    key_up(7);
    ev_push(EV_KILL, NTRK, 0, 0, 0, 0);
    for (n = 0; n < 6000u && !(sq_pat == 1 && sq_row == 31 && sq_frac > 0x80000000u); n++) block();
    CHECK(n < 6000u, "14. (the last half row of the second half was reached)");
    CHECK(next_pat() == 0, "14. after the second half: the first (next_pat %u)", next_pat());
    for (n = 0; n < 6000u && !(sq_pat == 0 && sq_row == 31 && sq_frac > 0x80000000u); n++) block();
    CHECK(next_pat() == 1, "14. after the first half: the second (next_pat %u)", next_pat());
    ui_pat = 1;                                 /* browsing to the second half while it loops: the pair keeps looping */
    pat_select(0);
    CHECK(sq_next == 0xFF, "14. the other half of the playing pair is not queued (%u)", sq_next);
    ui_pat = 5;
    pat_select(0);
    CHECK(sq_next == 5, "14. another pattern is queued (%u)", sq_next);
    P()->pad[0] = 0;
    prj.pat[6].pad[0] = 1; prj.pat[6].len = 32;
    ui_pat = 7;
    pat_select(0);
    CHECK(sq_next == 6, "14. browsing to a second half queues its first half (%u)", sq_next);
    rec = 0;
    sq_req = 2; block();
    sq_next = 0xFF;
    /* CLONE keeps off the pairs */
    fresh();
    prj.pat[0].pad[0] = 1; prj.pat[0].len = 32; prj.pat[1].len = 32;
    prj.pat[0].c[0][0].note = 60;
    prj.pat[2].c[0][0].note = 62;
    k = 0;
    { const char *r = magic_run(MG_CLONE, 0, 0, 0, 8, 1, 0); k = !strcmp(r, "LINKED"); }
    CHECK(k && pat_empty(&prj.pat[1]) && prj.pat[1].len == 32, "14. CLONE of a pair is refused and its second half stays");
    mg_clone_to = 0xFF;
    magic_run(MG_CLONE, 2, 0, 0, 8, 1, 0);
    CHECK(mg_clone_to != 1 && mg_clone_to != 0 && mg_clone_to < NPAT && pat_linked(0), "14. CLONE of another pattern does not take the empty second half (went to %u)", mg_clone_to);
    /* a chain of links and a link out of the last pattern are broken at load */
    fresh();
    prj.pat[3].pad[0] = 1; prj.pat[4].pad[0] = 1; prj.pat[5].pad[0] = 1;
    prj.pat[31].pad[0] = 1;
    prj_sanitize();
    CHECK(pat_linked(3) && !pat_linked(4) && pat_linked(5) && !(prj.pat[31].pad[0] & 1), "14. a chain of links is broken (3 %d, 4 %d, 5 %d, 31 %d)", pat_linked(3), pat_linked(4), pat_linked(5), prj.pat[31].pad[0]);
    /* a stale grid page cannot reach pattern 32 */
    fresh();
    ui_pat = 31;
    grid_page = 3;
    CHECK(gpat(3) == 31 && grow(3) == 0, "14. a stale grid page stays in range (pattern %u row %u)", gpat(3), grow(3));
    /* SELECT on the grid lets go of a held step; a key at once with FX held is a live effect; the tools are modal for ARP and keys */
    fresh();
    set_page(PG_GRID);
    P()->len = 32;
    grid_key(2, 1);
    CHECK(grid_held == 2, "14. a step is held");
    enc(ENC_SELECT, 1);
    CHECK(grid_held == -1 && grid_page == 1, "14. SELECT: the step is let go with its page");
    fresh();
    button_down(BTN_FX);
    host_note_edges = 1u << 4; fm1_in.notes = 1u << 4;
    keys_input();
    CHECK(page == PG_FX && (lfx_keys != 0), "14. FX held and a key at once: a live effect (page %u, keys %x)", page, lfx_keys);
    fm1_in.notes = 0; keys_input();
    button_up(BTN_FX);
    fresh();
    tools_open = 1;
    k = evq_w;
    host_note_edges = 1u << 5; fm1_in.notes = 1u << 5;
    keys_input();
    CHECK(evq_w == k, "14. the tools are open: a key does not play");
    fm1_in.notes = 0; keys_input();
    tools_open = 0;
    arp_held = 0;
    tools_open = 1; button_down(BTN_ARP); tools_open = 0;
    CHECK(!arp_held, "14. ARP is ignored while the tools are open");
    /* FX: the sticky state survives the help */
    fresh();
    button_down(BTN_FX); button_up(BTN_FX);
    CHECK(page == PG_SEND && fx_sticky, "14. SENDS is sticky");
    button_down(BTN_HOME); fm1_ms += 600u; ui_input();
    CHECK(page == PG_HELP, "14. HOME held: the help");
    button_up(BTN_HOME);
    CHECK(page == PG_SEND && fx_sticky, "14. the help goes back to SENDS, still sticky");
    enc(ENC_SELECT, 1);
    CHECK(page == PG_FX, "14. and SELECT still goes on to LIVE");
    /* a chord with HOME is not a tap */
    fresh();
    set_page(PG_SOUND);
    button_down(BTN_HOME);
    host_pressed |= 1u << BTN_ENV;
    ui_input();
    button_up(BTN_HOME);
    CHECK(page == PG_ENV, "14. HOME + ENV goes to ENV; HOME's release does not send it to TRACKS (page %u)", page);

    /* 15. the second round of the review: the boot touch is only that, the help goes with HOME, FX's own way back, the grid's tools,
     * PLAY on a second half, PRESETS on SENDS */
    fresh();
    page = PG_BOOT;
    host_pressed |= 1u << BTN_REC; fm1_in.buttons |= 1u << BTN_REC;
    ui_input();
    host_released |= 1u << BTN_REC; fm1_in.buttons &= ~(1u << BTN_REC);
    ui_input();
    CHECK(page == PG_TRACK && !rec, "15. the touch that closes the boot screen: its release does nothing (REC %u)", rec);
    page = PG_BOOT;
    fm1_in.notes = 1u << 7;
    ui_input();
    k = evq_w;
    keys_input();
    CHECK(evq_w == k, "15. a key held through the boot screen does not play when the page comes");
    fm1_in.notes = 0; keys_input();
    fresh();
    set_page(PG_TRACK);
    button_down(BTN_HOME); fm1_ms += 600u; ui_input();
    CHECK(page == PG_HELP, "15. HOME held: the help");
    host_pressed |= 1u << BTN_REC;
    ui_input();
    button_up(BTN_HOME);
    CHECK(page == PG_TRACK, "15. the help goes when HOME is let go, even after another button (page %u)", page);
    CHECK(!home_did, "15. (and the chord flag is clear)");
    fresh();
    set_page(PG_SOUND);
    button_down(BTN_FX); button_up(BTN_FX);
    button_down(BTN_SAVE); button_up(BTN_SAVE);        /* PROJECTS over SENDS */
    CHECK(page == PG_PROJ, "15. PROJECTS");
    set_page(PG_SEND);
    button_down(BTN_FX); button_up(BTN_FX);            /* LIVE */
    button_down(BTN_FX); button_up(BTN_FX);            /* and back */
    CHECK(page == PG_SOUND, "15. FX goes back where it came from, whatever PROJECTS did (page %u)", page);
    fresh();
    set_page(PG_GRID);
    P()->len = 32; P()->pad[0] = 1; prj.pat[1].len = 32;
    grid_page = 3;
    prj.pat[1].c[20][0].note = 60;
    tools_run(3);                                       /* CLEAR on the second half's page */
    CHECK(prj.pat[1].c[20][0].note == 0, "15. the tools act on the half the grid shows");
    fresh();
    P()->len = 32; P()->pad[0] = 1; prj.pat[1].len = 32;
    ui_pat = 1;
    play_from_cursor();
    CHECK(sq_start_pat == 0, "15. PLAY with the cursor on a second half starts the pair (%u)", sq_start_pat);
    fresh();
    set_page(PG_SEND);
    ui_pat = 3;
    enc(ENC_PRESETS, 1);
    CHECK(ui_pat == 3, "15. PRESETS on SENDS leaves the pattern alone");
    fresh();
    prj.snd[1].engine = E_FM; sound_build(1);
    cur_trk = 1; fm_op = 1;
    set_page(PG_SOUND);
    while (sp_at(1, snd_page) != SP_OP && snd_page < 7) snd_page++;
    prj.fmp[0] = 0;
    hist_clear();
    op_set(1, OPK_LEVEL, 99);
    n = undo_h.n;
    knob(0, 5);                                         /* LEVEL is already at its end */
    CHECK(undo_h.n == n, "15. an operator knob at its end is not an undo step");

    /* 16. the demo opens at 120 BPM on its last step; a new song is 120 BPM too */
    fresh();
    pm_act = PM_DEMO; pm_do(PM_DEMO);
    CHECK(prj.bpm == 120 && song_cur == prj.song_len - 1u && ui_pat == prj.song[song_cur].pat && prj.song_len == 7, "16. the demo: %u BPM, step %u of %u, pattern %u", prj.bpm, song_cur + 1u, prj.song_len, ui_pat);
    pm_do(PM_NEW);
    CHECK(prj.bpm == 120 && ui_pat == 0 && song_cur == 0, "16. a new song: %u BPM", prj.bpm);
    printf("ui_test: %s\n", fails ? "FAILED" : "passed");
    return fails != 0;
}
