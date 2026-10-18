/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA's audio at its limits, on this computer, under UBSan (tests/run_stress_test.sh): every factory sound with
 * every track effect at the ends of its knobs, every master effect, every live effect held over a loud song, the
 * kits at DECAY +8 and DRIVE 15, PULSE with DETUNE and UNISON, FM loud and panned, all faders up; then 0.3's
 * features at theirs: two tricks in every cell, tables a tick a step with HITs and every trick, the groove at
 * 1 and 12 ticks at 40 and 300 BPM, sample edits (no width, x16, reversed, a full-scale sound of the user's),
 * the MIDI clock in at 40 and 300 BPM, in bursts and gaps. The interrupt's integers must never wrap (a wrap is a
 * full-scale click on the FM-1): the sanitiser stops the run at the first one and names the line. It also checks
 * the voices end. */
#define main kawa_host_main
#include "../firmware/src/kawa.c"
#undef main

#define BLK 256u
static int32_t out[2u * BLK];
static uint32_t blocks;
static int32_t peak_all;

static void render(uint32_t n)
{
    uint32_t b, i;
    for (b = 0; b < n; b++) {
        for (i = 0; i < 2u * BLK; i++)
            out[i] = 0;
        app_audio(out, BLK);
        for (i = 0; i < 2u * BLK; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            if (a > peak_all) peak_all = a;
        }
        blocks++;
    }
}

static void quiet_all(void)
{
    sq_req = 2;
    lfx_keys = 0;
    ev_push(EV_KILL, NTRK, 0, 0, 0, 0);
    render(40);
}

static void loud_faders(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        prj.snd[t].p[P_VOL] = 15;
        prj.snd[t].p[P_ECHO] = 15;
        prj.snd[t].p[P_VERB] = 15;
    }
}

int main(void)
{
    uint32_t k, t, fx, a, b, id, kit, d, n = 0;
    /* 1. every factory sound, on its engine's track, with every track effect at the ends of A and B */
    for (k = 0; k < NPRESET; k++) {
        prj_new();
        t = PRESETS[k].engine == E_FM ? 1u : 0u;
        if (PRESETS[k].engine == E_FM) {
            sound_preset(1, k);
        } else {
            sound_preset(0, k);
            if (prj.snd[1].engine == E_FM) sound_preset(1, preset_first(E_PULSE));
        }
        prj.snd[t].p[P_VOL] = 15;
        for (fx = 1; fx < FXT_N; fx++)
            for (a = 0; a <= 15u; a += 15u)
                for (b = 0; b <= 15u; b += 15u) {
                    prj.snd[t].p[P_FXT] = (int8_t)fx;
                    prj.snd[t].p[P_FXA] = (int8_t)a;
                    prj.snd[t].p[P_FXB] = (int8_t)b;
                    prj.snd[t].p[P_FXM] = 15;
                    for (d = 0; d < 3u; d++) {      /* three notes, overlapping, loud */
                        ev_push(EV_ON, t, IS_TONAL(t) ? 36u + d * 19u : 1u + d * 5u, 15, 0, 0);
                        render(12);
                    }
                    ev_push(EV_OFF, t, 74, 0, 0, 0);
                    render(20);
                    n++;
                }
        quiet_all();
    }
    printf("1. %u factory sounds x track effects: ok (%u settings)\n", NPRESET, n);
    /* 2. PULSE with DETUNE at its top, and the live UNISON (two oscillators summed) */
    prj_new();
    prj.snd[0].p[P_DETUNE] = 15;
    prj.snd[0].p[P_TON] = 15;
    prj.snd[0].p[P_VOL] = 15;
    for (d = 0; d < 3u; d++) {
        lfx_keys = d == 1u ? 1u << LF_UNI : d == 2u ? 1u << LF_UNILO : 0u;
        ev_push(EV_ON, 0, 84, 15, 0, 0);
        render(80);
    }
    quiet_all();
    printf("2. PULSE DETUNE 15, UNISON: ok\n");
    /* 3. the kits: every drum at DECAY +8, DRIVE 15, TONE 13 (the one-pole low-pass), two voices overlapping */
    for (kit = 0; kit < NKIT; kit++) {
        prj_new();
        sound_preset(4, preset_named(E_KIT, (int32_t)kit));
        prj.snd[4].p[P_DECAY] = 8;
        prj.snd[4].p[P_DRIVE] = 15;
        prj.snd[4].p[P_KTON] = 13;
        prj.snd[4].p[P_VOL] = 15;
        for (d = 1; d <= NDRUM; d++) {
            ev_push(EV_ON, 4, d, 15, 0, 0);
            render(6);
            ev_push(EV_ON, 4, d, 15, 0, 0);
            render(30);
        }
        quiet_all();
    }
    printf("3. %u kits at DECAY +8, DRIVE 15: ok\n", NKIT);
    /* 4. FM loud: every FM sound, vol 15, panned hard each way, reverb and echo at 15, notes overlapping */
    for (k = 0; k < NPRESET; k++) {
        if (PRESETS[k].engine != E_FM) continue;
        prj_new();
        sound_preset(1, k);
        for (d = 0; d < 2u; d++) {
            prj.snd[1].p[P_PAN] = d ? 7 : -7;
            prj.snd[1].p[P_VOL] = 15;
            prj.snd[1].p[P_VERB] = 15;
            prj.snd[1].p[P_ECHO] = 15;
            for (a = 0; a < 8u; a++) {
                ev_push(EV_ON, 1, 43u + a * 5u, 15, 0, 0);
                render(10);
            }
        }
        quiet_all();
    }
    printf("4. FM loud, panned, reverb and echo 15: ok\n");
    /* 5. the demo, every fader up, with each master effect at its ends */
    for (fx = 0; fx < FXT_N; fx++)
        for (a = 0; a <= 15u; a += 15u) {
            prj_demo();
            loud_faders();
            prj.mfx[0] = (int8_t)fx;
            prj.mfx[1] = (int8_t)a;
            prj.mfx[2] = 15;
            prj.mfx[3] = 15;
            prj.mode = 1;
            sq_req = 1;
            render(400);
            quiet_all();
        }
    printf("5. the demo, faders up, %u master effects: ok\n", FXT_N);
    /* 6. every live effect held over the demo (faders up), on the master and on a track; then two at once */
    for (id = 0; id < LF_N; id++)
        for (t = 0; t < 2u; t++) {
            prj_demo();
            loud_faders();
            prj.mode = 1;
            lfx_target = (uint8_t)(t ? 5u : 0u);
            sq_req = 1;
            render(60);
            lfx_keys = 1u << id;
            render(250);
            lfx_keys = (1u << id) | (1u << ((id + 7u) % LF_N));
            render(120);
            lfx_keys = 0;
            render(60);
            quiet_all();
        }
    lfx_target = 0;
    printf("6. %u live effects held over the demo (master and a track): ok\n", LF_N);
    /* 7. a RETRIG in the LIVE lane must not lock the song on its beat */
    prj_demo();
    prj.mode = 1;
    prj.pat[prj.song[0].pat].live[7] = (uint8_t)(LF_AGAIN + 1u);
    sq_req = 1;
    {
        uint32_t seen = 0;
        for (k = 0; k < 2000u && !seen; k++) {
            render(1);
            if (sq_songpos != 0u) seen = 1;
        }
        if (!seen) { printf("FAIL  7. a RETRIG in the LIVE lane locks the song\n"); return 1; }
    }
    quiet_all();
    printf("7. a RETRIG in the LIVE lane plays once, the song goes on: ok\n");
    /* 9. two tricks in every cell of the demo, each at an end of its range */
    prj_demo();
    loud_faders();
    for (k = 0; k < NPAT; k++)
        for (a = 0; a < NROW; a++)
            for (t = 0; t < NTRK; t++) {
                cell_t *c = &prj.pat[k].c[a][t];
                uint32_t f1 = (k * 7u + a * 3u + t) % FX_N, f2 = (k + a * 5u + t * 11u) % FX_N;
                if (f1 == FX_HOP || f1 == FX_END) f1 = FX_ROLL;   /* (the song must go on: HOP / END in 9b) */
                if (f2 == FX_HOP || f2 == FX_END) f2 = FX_CHANCE;
                c->fx = (uint8_t)f1;
                c->fxv = (uint8_t)(int8_t)((a & 1u) ? FXD[f1].hi : FXD[f1].lo);
                c->fx2 = (uint8_t)f2;
                c->fxv2 = (uint8_t)(int8_t)((a & 2u) ? FXD[f2].hi : FXD[f2].lo);
            }
    prj.mode = 1;
    sq_req = 1;
    render(1500);
    quiet_all();
    printf("9. two tricks in every cell, at their ends: ok\n");
    /* 9b. HOP and END everywhere: rows jump, patterns end early, the song goes round */
    prj_demo();
    for (k = 0; k < 7u; k++)
        for (a = 0; a < NROW; a++) {
            prj.pat[k].c[a][(a + k) % NTRK].fx2 = (uint8_t)((a % 3u) ? FX_HOP : FX_END);
            prj.pat[k].c[a][(a + k) % NTRK].fxv2 = (uint8_t)((a * 7u + 3u) % NROW);
        }
    prj.mode = 1;
    sq_req = 1;
    render(800);
    quiet_all();
    printf("9b. HOP and END on every row: ok\n");
    /* 10. tables: a tick a step, 16 steps, +-24, every trick at its ends, HITs, on every track of the demo */
    for (d = 0; d < 2u; d++) {
        prj_demo();
        loud_faders();
        for (t = 0; t < NTRK; t++) {
            table_t *tb = &prj.tbl[t];
            tb->on = 1;
            tb->speed = (uint8_t)(d ? 1u : 24u);
            tb->len = 16;
            tb->loop = (uint8_t)(t % 3u);
            for (k = 0; k < NTSTEP; k++) {
                uint32_t f = (k + t) % TB_N;
                tb->s[k].tr = (int8_t)((k & 1u) ? 24 : -24);
                tb->s[k].vol = (uint8_t)((k & 2u) ? 15u : 1u);
                tb->s[k].fx = (uint8_t)f;
                tb->s[k].fxv = (int8_t)((k & 4u) ? TBD[f].hi : TBD[f].lo);
            }
        }
        prj.mode = 1;
        sq_req = 1;
        render(1200);
        quiet_all();
    }
    printf("10. tables a tick a step (and 24), +-24, every trick, HITs, on every track: ok\n");
    /* 11. the groove at 1 and 12 ticks, at 40 and 300 BPM, with the tables on */
    for (d = 0; d < 2u; d++) {
        prj.bpm = (uint16_t)(d ? 300u : 40u);
        prj.grv.on = 1;
        prj.grv.len = 3;
        prj.grv.tick[0] = 1;
        prj.grv.tick[1] = 12;
        prj.grv.tick[2] = 1;
        sq_req = 1;
        render(d ? 800u : 1600u);
        quiet_all();
    }
    prj.grv.on = 0;
    printf("11. the groove at 1 and 12 ticks, 40 and 300 BPM: ok\n");
    /* 12. sample edits: no width, x16, reversed, slices piled up; a full-scale sound of the user's, DRIVE 15 */
    {
        static uint8_t loud[USR_CAP];
        uint32_t j;
        for (j = 0; j < USR_CAP; j++) loud[j] = (uint8_t)((j / 3u) & 1u ? 0x00u : 0x80u);   /* full scale both ways */
        usr_put(0, loud, USR_CAP, 22050, "LOUD");
        for (d = 0; d < 6u; d++) {
            sedit_t *se = &prj.sed[5];
            prj_new();
            sound_preset(5, preset_named(E_SAMPLE, d < 3u ? (int32_t)BRK_DEMO : (int32_t)BRK_N));
            prj.snd[5].p[P_SDRIVE] = 15;
            prj.snd[5].p[P_VOL] = 15;
            prj.snd[5].p[P_SMODE] = (int8_t)(d & 1u);
            prj.snd[5].p[P_FIT] = 1;
            se->norm = 1;
            se->gain = 4096;
            se->rev = (uint8_t)(d % 3u != 0u);
            se->manual = (uint8_t)(d >= 2u);
            se->start = (uint16_t)(d == 1u ? 65535u - 512u : 0u);
            se->end = (uint16_t)(d == 1u ? 65535u : d == 4u ? 600u : 65535u);
            for (j = 0; j < 16u; j++) se->sl[j] = (uint16_t)(d == 2u ? 30000u : j * 4096u);
            sed_fix(se);
            se->gain = 4096;                    /* (x16: more than any sound needs) */
            prj.tbl[5].on = 1;
            prj.tbl[5].speed = 1;
            prj.tbl[5].len = 2;
            prj.tbl[5].s[0].tr = 24;
            prj.tbl[5].s[1].tr = -24;
            prj.tbl[5].s[1].fx = TB_HIT;
            for (j = 1; j <= 16u; j++) {
                ev_push(EV_ON, 5, d & 1u ? 36u + j * 4u : j, 15, j & 1u ? FX_PITCH : FX_OFS, j & 1u ? 12u : 15u);
                render(6);
            }
            quiet_all();
        }
    }
    printf("12. sample edits: no width, x16, reversed, piled-up slices, a full-scale sound, DRIVE 15: ok\n");
    /* 13. the MIDI clock in: 300 BPM, 40 BPM, 50 ticks at once, two seconds without, START in the middle */
    prj_demo();
    loud_faders();
    prj.mode = 1;
    set.clock = 2;
    clk_in = 1;
    clock_in_reset();
    clock_in(0xFA);
    {
        uint32_t j, f = 0;
        for (j = 0; j < 4000u; j++) {
            uint32_t per = j < 1000u ? 367u : j < 1600u ? 2756u : 900u;   /* frames a tick */
            render(1);
            f += 256u;
            fm1_ms = f / 44u;
            if (j == 2000u) { uint32_t b; for (b = 0; b < 50u; b++) clock_in(0xF8); }
            if (j > 2400u && j < 2750u) continue;   /* (no clock for two seconds) */
            if (j == 3000u) clock_in(0xFA);
            if (f % per < 256u) clock_in(0xF8);
        }
        clock_in(0xFC);
        render(2);
    }
    clk_in = 0;
    set.clock = 0;
    quiet_all();
    printf("13. the MIDI clock in at 300 and 40 BPM, a burst, a gap, START again: ok\n");
    /* 8. everything ends: no voice left on once all is stopped */
    render(400);
    for (t = 0; t < NTRK; t++)
        if (drum[t][0].on || drum[t][1].on || smp[t][0].on || smp[t][1].on || tk[t].tb_run) {
            printf("FAIL  8. a voice of track %u never ends\n", t + 1u);
            return 1;
        }
    printf("8. every voice ended: ok\n");
    printf("stress_test: passed (%u blocks, %.1f s of sound, master peak %d)\n", blocks, blocks * 256.0 / 44100.0,
           peak_all);
    return 0;
}
