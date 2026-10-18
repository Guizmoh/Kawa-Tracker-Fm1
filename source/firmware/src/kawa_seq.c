/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: the sequencer and the audio interrupt's entry (app_audio).
 *
 * Rows are 16ths. A row's phase is a 32-bit counter that wraps once per row (swing: even rows longer; the
 * groove: each row its ticks, 6 a straight 16th). In a row, per track and for each of its cell's two
 * tricks: DELAY starts the note later, ROLL plays it again 2..32 times, CUT stops it, CHANCE may skip it;
 * HOP goes on to another row, END to the next pattern. The LIVE lane of the pattern adds its effect on its
 * rows, as if its key were held. Loop mode plays the pattern on the screen (a new one starts at the end of
 * the one playing); song mode plays the song's list, each entry its number of times. The MIDI clock in
 * (OPTIONS) can lead: its ticks set the tempo, and the rows wait for them or catch up. Then the mix: each
 * track panned, its echo and reverb sends; the live effects' track (or the whole mix) through the live bus;
 * the master. */

static uint32_t s_phase, s_inc_even = 1117000u, s_inc_odd = 1117000u, s_len_even = 3845u, s_len_odd = 3845u;
static uint32_t s_len_k[13], s_inc_k[13];       /* a row of k ticks: its frames, its phase step */
static uint32_t s_inc = 1117000u, s_len = 3845u;   /* the row playing */
static uint8_t s_pat, s_row, s_pos, s_rep, s_again_row;
static uint16_t s_bpm;
static uint8_t s_swing, s_trip, s_again, s_tick6;
static uint8_t s_kt = 6;                        /* the ticks of the row playing (6: a straight 16th) */
static uint8_t s_gi;                            /* the groove's step */
static uint8_t s_kt_even = 6, s_kt_odd = 6;     /* the swing in ticks (the clock in counts them) */
static uint32_t s_tick_in;                      /* clk_in_tick as last applied (0: the project's tempo) */
static uint32_t s_inc_p;                        /* the clock in: the row's phase step, nudged to the ticks (s_pll) */
static int32_t s_pll;                           /* +-1/64 (Q16): the next tick comes a little sooner / later */
static uint32_t s_tickbase, s_tick0;            /* the clock in: ticks of the rows before this one; ticks before PLAY */
static int32_t s_adj;                           /* sq_adj as last applied */
static uint32_t s_step;                         /* rows played since PLAY (the swing's long and short rows) */
static uint8_t s_lane_skip;                     /* this beat is a RETRIG's repeat: the LIVE lane's RETRIG waits */
static struct {
    uint8_t note, vol, pending, roll_n, roll_k, cut;
    uint16_t delay16;
    tricks_t k;
} st[NTRK];
static uint8_t last_note[NTRK];                 /* the note each track played last (ROLL on an empty cell) */
static volatile uint8_t sq_suppress[NTRK];      /* main: the note of the next row is already sounding (live take) */
static volatile uint32_t sq_tick0;              /* main: the clock in's ticks before a start (kept from PLAY) */
static volatile uint8_t sq_gstep;               /* ISR: the groove's step playing (GROOVE) */

static void seq_row_len(void)                   /* the row's ticks and length: the groove's, the swing's */
{
    uint32_t tick_mode = prj.grv.on || clk_in;
    if (s_trip && tick_mode)
        s_kt = (uint8_t)(s_step & 1u ? 4u : 8u);   /* 6/8 over the groove: triplet pairs */
    else if (prj.grv.on)
        s_kt = (uint8_t)clampi(prj.grv.tick[s_gi % 16u], 1, 12);
    else if (clk_in)
        s_kt = s_step & 1u ? s_kt_odd : s_kt_even;
    else
        s_kt = 6;
    if (tick_mode) {
        s_inc = s_inc_k[s_kt];
        s_len = s_len_k[s_kt];
    } else {                                    /* (the swing: every other row played, whatever the pattern's */
        s_inc = s_step & 1u ? s_inc_odd : s_inc_even;   /* length: an odd one keeps the tempo) */
        s_len = s_step & 1u ? s_len_odd : s_len_even;
    }
    s_inc_p = s_inc + (uint32_t)(((int64_t)s_inc * s_pll) >> 16);
}

static void seq_tempo(void)                     /* row lengths from the tempo and the swing; the LFO's rates */
{
    static const uint8_t HALF_ROWS[8] = {128, 64, 32, 16, 8, 4, 2, 1};   /* 4 bars .. 1/32 */
    static const uint32_t FREE[3] = {467000u, 3116000u, 12465000u};      /* 0.3, 2, 8 Hz, per chunk */
    uint32_t base, sw, k;
    s_bpm = prj.bpm < 40u ? 40u : prj.bpm > 300u ? 300u : prj.bpm;
    s_swing = prj.swing;
    s_tick_in = clk_in ? clk_in_tick : 0u;
    sw = s_swing < 50u ? 50u : s_swing > 75u ? 75u : s_swing;
    if (s_trip && sw < 67u)
        sw = 67u;                               /* 6/8: the second 16th of each pair comes late, as a triplet */
    base = s_tick_in ? (uint32_t)clampi((int32_t)((s_tick_in * 6u) >> 4), 2205, 16537) : 661500u / s_bpm;   /* 44100*60/4 */
    s_len_even = 2u * base * sw / 100u;
    s_len_odd = 2u * base - s_len_even;
    s_inc_even = 0xFFFFFFFFu / s_len_even;
    s_inc_odd = 0xFFFFFFFFu / s_len_odd;
    s_kt_even = (uint8_t)((12u * sw + 50u) / 100u);
    s_kt_odd = (uint8_t)(12u - s_kt_even);
    for (k = 1; k <= 12u; k++) {
        if (s_tick_in) {                        /* the clock in's tick, to a 16th of a frame (no drift) */
            s_len_k[k] = (s_tick_in * k) >> 4;
            s_inc_k[k] = (0xFFFFFFFFu / (s_tick_in * k)) << 4;
        } else {
            s_len_k[k] = base * k / 6u;
            s_inc_k[k] = 0xFFFFFFFFu / s_len_k[k];
        }
    }
    g_rowlen = base;
    g_ticklen = base / 6u;
    vsl_k = (65536u * CHUNK) / base;
    for (k = 0; k < 8u; k++)
        lfo_inc[k] = (0xFFFFFFFFu / (HALF_ROWS[k] * (base >> 1) + 1u)) * CHUNK;
    for (k = 0; k < 3u; k++)
        lfo_inc[8u + k] = FREE[k];
    seq_row_len();
}

static void seq_play_cell(uint32_t t, uint32_t note, uint32_t vol, const tricks_t *k)
{
    if (note != NOTE_OFF)
        last_note[t] = (uint8_t)note;
    voice_on(t, note, vol, k, g_rowlen / 6u);
}

static void seq_row_start(void)
{
    const pat_t *pt = &prj.pat[s_pat];
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        const cell_t *c = &pt->c[s_row][t];
        uint32_t sup = sq_suppress[t];          /* (taken at every row: it is about this row only) */
        tricks_t k;
        int32_t v;
        sq_suppress[t] = 0;
        k.fx = (uint8_t)c->fx;
        k.fx2 = (uint8_t)c->fx2;
        k.v = (int8_t)c->fxv;
        k.v2 = (int8_t)c->fxv2;
        st[t].note = c->note;
        st[t].vol = (uint8_t)c->vol;
        st[t].k = k;
        st[t].pending = 0;
        st[t].roll_n = 0;
        st[t].cut = 0;
        tk[t].vslide = 0;                       /* VSL lasts its row */
        if (c->note == NOTE_NONE && trick(&k, FX_VSL, &v))
            tk[t].vslide = (int8_t)v;           /* (an empty cell: the note playing slides) */
        if (trick(&k, FX_CUT, &v))
            st[t].cut = (uint8_t)(((uint32_t)clampi(v, 1, 23) * 256u) / 24u);
        if (c->note == NOTE_NONE) {
            if (trick(&k, FX_ROLL, &v) && last_note[t]) {    /* a roll on an empty cell: the last note again */
                st[t].note = last_note[t];
                st[t].vol = c->vol ? (uint8_t)c->vol : 12u;
                st[t].roll_n = ROLL_N[clampi(v, 0, 8)];
                st[t].roll_k = 1;
                seq_play_cell(t, st[t].note, st[t].vol, &NO_TRICKS);
            }
            continue;
        }
        if (sup)                                /* played live a moment ago: do not play it twice */
            continue;
        if (trick(&k, FX_CHANCE, &v) && (xr(&rnd_isr) >> 8) % 10u >= (uint32_t)clampi(v, 0, 9))
            continue;
        if (trick(&k, FX_DELAY, &v)) {
            st[t].pending = 1;
            st[t].delay16 = (uint16_t)(((uint32_t)clampi(v, 1, 23) * 65536u) / 24u);
            continue;
        }
        if (trick(&k, FX_ROLL, &v) && c->note != NOTE_OFF) {
            st[t].roll_n = ROLL_N[clampi(v, 0, 8)];
            st[t].roll_k = 1;
        }
        seq_play_cell(t, c->note, c->vol, &k);
    }
}

static uint32_t song_rep(uint32_t pos) { return prj.song[pos].rep ? prj.song[pos].rep : 1u; }

static void seq_start(void)
{
    s_pat = sq_start_pat < NPAT ? sq_start_pat : 0u;
    s_pos = sq_start_pos;
    s_rep = 0;
    if (prj.mode && prj.song_len) {
        if (s_pos >= prj.song_len)
            s_pos = 0;
        s_pat = prj.song[s_pos].pat < NPAT ? prj.song[s_pos].pat : 0u;
    }
    s_row = 0;
    s_step = 0;
    s_gi = 0;
    sq_gstep = 0;
    s_tickbase = 0;
    s_tick0 = sq_tick0;
    s_pll = 0;
    s_lane_skip = 0;
    s_phase = 0;
    s_again = 0;
    s_adj = sq_adj;                             /* (a fresh start: earlier song edits are already in place) */
    sq_goto = 0xFF;
    seq_tempo();
    sq_pat = s_pat;
    sq_row = 0;
    sq_songpos = s_pos;
    sq_rep = 0;
    s_tick6 = 0;
    clk_start = 1;
    sq_play = 1;
    seq_row_start();
    sq_rowtick++;
}

static void seq_next_row(void)
{
    const cell_t *cr = prj.pat[s_pat].c[s_row < NROW ? s_row : 0u];
    uint32_t len = prj.pat[s_pat].len, t, hop = NROW, end = 0, glen = (uint32_t)clampi(prj.grv.len, 1, 16);
    if (!len || len > NROW)
        len = 16u;
    for (t = 0; t < NTRK; t++) {                /* the row just played: HOP, END (on any track) */
        if (cr[t].fx == FX_HOP) hop = cr[t].fxv;
        if (cr[t].fx2 == FX_HOP) hop = cr[t].fxv2;
        if (cr[t].fx == FX_END || cr[t].fx2 == FX_END) end = 1;
    }
    s_tickbase += s_kt;
    s_step++;
    if (++s_gi >= glen)
        s_gi = 0;
    sq_gstep = s_gi;
    if (end)
        s_row = (uint8_t)len;                   /* END: the pattern ends here */
    else if (hop < len)
        s_row = (uint8_t)hop;                   /* HOP: on to its row */
    else
        s_row++;
    if (s_again && (s_row & 3u) == 0u) {        /* RETRIG: the same beat again */
        s_row = s_again_row;
        s_lane_skip = 1;                        /* (a RETRIG of the LIVE lane: once, not for ever) */
    } else if ((s_row & 3u) == 0u)
        s_lane_skip = 0;
    if (s_row >= len) {                         /* the end of the pattern */
        s_row = 0;
        if (prj.mode && prj.song_len) {
            if (sq_goto < NSONG) {              /* the entry playing was deleted: the one after it */
                s_pos = sq_goto < prj.song_len ? sq_goto : 0u;
                s_rep = 0;
                sq_goto = 0xFF;
            } else if (s_pos >= prj.song_len)
                s_pos = 0, s_rep = 0;
            else if (++s_rep >= song_rep(s_pos)) {   /* played its times: the next entry */
                s_rep = 0;
                if (++s_pos >= prj.song_len)
                    s_pos = 0;
            }
            s_pat = prj.song[s_pos].pat < NPAT ? prj.song[s_pos].pat : 0u;
        } else if (sq_next < NPAT) {
            s_pat = sq_next;
            sq_next = 0xFF;
        }
        sq_songpos = s_pos;
        sq_rep = s_rep;
    }
    sq_pat = s_pat;
    sq_row = s_row;
    if (rec_live && lfx_keys) {                 /* recording the live effects into the LIVE lane */
        uint32_t id = 0;
        while (id < LF_N && !(lfx_keys >> id & 1u))
            id++;
        if (id < LF_N)
            prj.pat[s_pat].live[s_row] = (uint8_t)(id + 1u);
    }
    seq_row_len();
    seq_row_start();
    sq_rowtick++;
    s_tick6 = 0;
}

static void seq_chunk(void)
{
    uint32_t t, old, f16, inc = (clk_in ? s_inc_p : s_inc) * CHUNK;
    if (clk_in) {                               /* the MIDI clock in leads: two ticks ahead the row waits for it, */
        uint32_t got = clk_in_ticks - s_tick0, have = s_tickbase + s_tick6;   /* two behind it catches up */
        if (have >= got + 2u)
            inc = 0;
        else if (got >= have + 2u) {
            inc = 0;
            if (s_tick6 >= s_kt) {
                s_phase = 0;
                seq_next_row();
            } else                              /* to the start of the next tick, rounded up (down, it would never get
                                                 * there: the same jump again and again); only when it lags */
                s_phase = ((((uint32_t)s_tick6 << 16) + s_kt - 1u) / s_kt) << 16;
        }
    }
    old = s_phase;
    s_phase += inc;
    if (s_phase < old)
        seq_next_row();
    sq_frac = s_phase;
    f16 = s_phase >> 16;
    while (s_tick6 < ((f16 * s_kt) >> 16) + 1u) {   /* the MIDI clock: the row's ticks (6: 24 a beat) */
        s_tick6++;
        if (!clk_in)
            clk_ticks++;
        else {                                  /* the clock in: its tick already here, the next one sooner; not yet, */
            uint32_t got = clk_in_ticks - s_tick0, j = s_tickbase + s_tick6;   /* later (a 1/64 nudge, tick by tick) */
            s_pll = got >= j ? 1024 : -1024;
            s_inc_p = s_inc + (uint32_t)(((int64_t)s_inc * s_pll) >> 16);
        }
    }
    for (t = 0; t < NTRK; t++) {                /* inside the row: late notes, rolls, cuts */
        if (st[t].pending && f16 >= st[t].delay16) {
            tricks_t k = st[t].k;
            st[t].pending = 0;
            if (k.fx == FX_DELAY) k.fx = FX_NONE;
            if (k.fx2 == FX_DELAY) k.fx2 = FX_NONE;
            seq_play_cell(t, st[t].note, st[t].vol, &k);
        }
        if (st[t].roll_n && st[t].roll_k < st[t].roll_n && f16 >= (st[t].roll_k * 65536u) / st[t].roll_n) {
            st[t].roll_k++;
            seq_play_cell(t, st[t].note, st[t].vol, &NO_TRICKS);
        }
        if (st[t].cut && (f16 >> 8) >= st[t].cut) {
            st[t].cut = 0;
            voice_cut(t);
        }
    }
    g_beatpos = (s_row & 3u) * g_rowlen + ((f16 * s_len) >> 16);
    if (g_beatpos >= g_rowlen * 4u)             /* (a groove's long row, a HOP: the beat stays in range) */
        g_beatpos %= g_rowlen * 4u;
}

/* ---- the load governor (after Felucca's audio.c shed_check): two blocks in a row over 70 % of the 5.8 ms
 * and the work drops a step: 1 the reverb and the FM release voice, 2 the tracks' own effects and the echo,
 * 3 the second voice of the kits and samples. Two seconds under 45 % and it comes back a step. The sound
 * thins out instead of crackling, and the watchdog never sees a block that does not end */
#define BLOCK_US (HALF_FRAMES * 1000000u / FS)
static uint8_t shed_hist;
static uint32_t calm_n;
static void governor(uint32_t us)
{
    uint32_t pct = us * 100u / BLOCK_US;
    cpu_pct = (uint8_t)(pct > 255u ? 255u : pct);
    if (cpu_pct > cpu_max) cpu_max = cpu_pct;
    shed_hist = (uint8_t)(shed_hist << 1 | (pct > 70u));
    if ((shed_hist & 3u) == 3u && cpu_shed < 3u) {
        cpu_shed++;
        shed_hist = 0;
        calm_n = 0;
        if (cpu_shed == 1u) fmn[fm_cur ^ 1u].live = 0;   /* the FM voice releasing goes now */
    }
    if (pct < 45u) {
        if (++calm_n > 344u && cpu_shed) { cpu_shed--; calm_n = 0; }   /* 344 blocks: 2 s */
    } else
        calm_n = 0;
}

/* ---- the audio interrupt: 256 frames, stereo, Q15, out is zero */
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t c, i, t, t0 = fm1_ticks(), on, mpk = 0;
    uint16_t peak[NTRK] = {0};
    audio_blocks++;
    if (smp_kill) {                             /* a user slot is about to be rewritten: no voice reads it */
        for (t = 0; t < NTRK; t++)
            smp[t][0].on = smp[t][1].on = 0;
        smp_kill = 0;
    }
    if (audio_hold)                             /* the flash is being written (kawa_sysex.c): silence */
        return;
    if (sq_adj != s_adj) {                      /* song entries inserted or deleted before the one playing */
        int32_t p = (int32_t)s_pos + (sq_adj - s_adj);
        s_adj = sq_adj;
        s_pos = (uint8_t)(p < 0 ? 0 : p);
        sq_songpos = s_pos;
    }
    while (evq_r != evq_w) {                    /* the keys */
        const ev_t *e = &evq[evq_r % EVQ];
        if (e->type == EV_ON) {
            tricks_t k;
            k.fx = e->fx;
            k.fx2 = e->fx2;
            k.v = (int8_t)e->fxv;
            k.v2 = (int8_t)e->fxv2;
            if (e->trk < NTRK)
                seq_play_cell(e->trk, e->note, e->vol, &k);
        }
        else if (e->type == EV_OFF) {
            if (e->trk < NTRK && tk[e->trk].note == e->note && note_mode(e->trk) == NM_NOTE)
                voice_off(e->trk);
        } else if (e->type == EV_ALLOFF)
            voices_silence();
        else if (e->type == EV_KILL)
            voice_kill(e->trk);
        evq_r++;
    }
    if (sq_req == 1u) {
        sq_req = 0;
        seq_start();
    } else if (sq_req == 2u) {
        sq_req = 0;
        sq_play = 0;
        clk_stop = 1;
        voices_silence();
        s_again = 0;
    }
    if (prj.bpm != s_bpm || prj.swing != s_swing || s_trip != (uint8_t)(lfx_on >> LF_TRIP & 1u) ||
        (clk_in ? clk_in_tick : 0u) != s_tick_in) {
        s_trip = (uint8_t)(lfx_on >> LF_TRIP & 1u);
        seq_tempo();
    }
    on = lfx_keys;
    if (sq_play && prj.pat[s_pat].live[s_row] && prj.pat[s_pat].live[s_row] <= LF_N &&
        !(s_lane_skip && prj.pat[s_pat].live[s_row] == LF_AGAIN + 1u))
        on |= 1u << (prj.pat[s_pat].live[s_row] - 1u);
    if ((on >> LF_AGAIN & 1u) && !s_again && sq_play) {
        s_again = 1;
        s_again_row = (uint8_t)(s_row & ~3u);
    } else if (!(on >> LF_AGAIN & 1u))
        s_again = 0;
    fx_block(on);
    for (c = 0; c < n; c += CHUNK) {
        int32_t L[CHUNK], R[CHUNK], BL[CHUNK], BR[CHUNK], E[CHUNK], V[CHUNK], mono[CHUNK];
        if (c && fm1_ticks() - t0 > BLOCK_US * FM1_TICKS_PER_US * 9u / 10u) {
            audio_late++;                       /* past 90 % of the block: the rest stays silent (never a block */
            break;                              /* that does not end: the watchdog) */
        }
        if (sq_play)
            seq_chunk();
        else
            g_beatpos = (g_beatpos + CHUNK) % (g_rowlen * 4u);
        for (t = 0; t < NTRK; t++)              /* the tables step on */
            tb_chunk(t);
        for (i = 0; i < CHUNK; i++)
            L[i] = R[i] = BL[i] = BR[i] = E[i] = V[i] = 0;
        for (t = 0; t < NTRK; t++) {
            const int8_t *p = prj.snd[t].p;
            int32_t pan = p[P_PAN], gl, gr, es = clampi(p[P_ECHO], 0, 15) * 1300, vs = clampi(p[P_VERB], 0, 15) * 1700;
            int32_t *dl = lv_target == t + 1u || !lv_target ? BL : L, *dr = lv_target == t + 1u || !lv_target ? BR : R;
            uint32_t pk = peak[t];
            if (p[P_LFOD] == LFD_PAN)
                pan += (tk[t].lfo * 7) >> 15;
            pan += tk[t].npan;                  /* PAN: this note's */
            pan = clampi(pan, -7, 7) + 7;
            gl = PAN_G[pan][0];
            gr = PAN_G[pan][1];
            for (i = 0; i < CHUNK; i++)
                mono[i] = 0;
            track_chunk(t, mono);
            for (i = 0; i < CHUNK; i++) {
                int32_t m = mono[i], a = m < 0 ? -m : m;
                if ((uint32_t)a > pk)
                    pk = (uint32_t)a;
                dl[i] += (int32_t)(((int64_t)m * (gl * 5)) >> 16);   /* x 0.707 at the centre, x 5/8: six */
                dr[i] += (int32_t)(((int64_t)m * (gr * 5)) >> 16);   /* tracks keep headroom (64-bit products: */
                E[i] += (int32_t)(((int64_t)m * es) >> 15);          /* the FM track goes past 100000) */
                V[i] += (int32_t)(((int64_t)m * vs) >> 15);
            }
            peak[t] = (uint16_t)(pk > 65535u ? 65535u : pk);
        }
        fx_live(BL, BR, E);
        for (i = 0; i < CHUNK; i++) {
            L[i] += BL[i];
            R[i] += BR[i];
        }
        fx_master(L, R, E, V);
        for (i = 0; i < CHUNK; i++) {
            int32_t a = L[i] < 0 ? -L[i] : L[i];
            if ((uint32_t)a > mpk)
                mpk = (uint32_t)a;
            out[2u * (c + i)] += L[i];
            out[2u * (c + i) + 1u] += R[i];
        }
    }
    for (t = 0; t < NTRK; t++)
        level[t] = peak[t];
    level[NTRK] = (uint16_t)(mpk > 65535u ? 65535u : mpk);
    audio_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    governor(audio_us);
}
