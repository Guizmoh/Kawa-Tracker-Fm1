/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB under UBSan (tests/run_stress.sh): every factory preset, every parameter at both ends of its range,
 * low, middle and high notes, poly chords, the wheel, bend and pressure at their ends, every effect on: the first
 * signed integer that wraps stops it (on the FM-1 a wrap is a full-scale click). Also: no NaN, no stuck voice. */
#include "../firmware/src/ksub.c"
int printf(const char *, ...);
static float peak;
static void blocks(int n)
{
    int b, i;
    for (b = 0; b < n; b++) {
        float *f = ksub_audio();
        for (i = 0; i < 512; i++) {
            float a = f[i] < 0 ? -f[i] : f[i];
            if (!(a <= 1.0f)) { printf("bad sample %f\n", (double)f[i]); __builtin_trap(); }
            if (a > peak) peak = a;
        }
        ksub_step();
    }
}
static void play(const int *notes, int n, int hold)
{
    int i;
    for (i = 0; i < n; i++) ksub_midi(0x90, (uint32_t)notes[i], 127);
    blocks(hold);
    for (i = 0; i < n; i++) ksub_midi(0x80, (uint32_t)notes[i], 0);
    blocks(6);
}
int main(void)
{
    static const int low[] = {12}, mid[] = {36}, high[] = {96}, chord[] = {24, 31, 36, 43};
    uint32_t k, p, e, runs = 0;
    ksub_init();
    for (k = 0; k < NFACTORY; k++) {
        for (p = 0; p < NP; p++) {
            for (e = 0; e < 2u; e++) {
                load_preset(0, k);
                patch[p] = e ? PD[p].hi : PD[p].lo;
                play(p & 1u ? low : high, 1, 8);
                runs++;
            }
        }
        load_preset(0, k);                      /* everything on, at its loudest, wheel / bend / pressure at the ends */
        patch[DR_ON] = patch[CR_ON] = patch[CH_ON] = patch[DL_ON] = patch[RV_ON] = 1;
        patch[DR_AMT] = patch[F_DRV] = patch[F_RES] = patch[MS_VOL] = patch[MS_SUB] = patch[DL_FB] = patch[RV_SIZE] = 127;
        patch[V_MODE] = VM_POLY;
        ksub_midi(0xB0, 1, 127); ksub_midi(0xE0, 127, 127); ksub_midi(0xD0, 127, 0);
        play(chord, 4, 40);
        ksub_midi(0xE0, 0, 0);
        play(mid, 1, 20);
        ksub_midi(0xB0, 1, 0); ksub_midi(0xE0, 0, 64); ksub_midi(0xD0, 0, 0);
        ksub_midi(0xB0, 123, 0);
        blocks(400);                            /* the tails end */
        for (e = 0; e < NVOICE; e++) if (vo[e].on) { printf("%s: a voice still on\n", FACTORY[k].name); return 1; }
    }
    for (k = 0; k < ND; k++) {                  /* each destination, at full depth both ways, from LFO 1, the mod */
        for (e = 0; e < 2u; e++) {              /* envelope and the matrix (LFO 2) */
            load_preset(0, 9);
            patch[B_LVL] = 100;
            patch[L1_DST] = patch[ME_DST] = patch[M1_DST] = (int8_t)k;
            patch[L1_DEP] = patch[ME_AMT] = patch[M1_AMT] = e ? 63 : -64;
            patch[M1_SRC] = SR_LFO2; patch[L2_DEP] = 0; patch[L1_SHP] = 4; patch[L1_DIV] = 15;
            play(e ? high : low, 1, 12);
        }
    }
    for (k = 0; k < 8; k++) {                   /* each engine on both oscillators, each filter, the extremes of P1..P3 */
        uint32_t f;
        for (f = 0; f < NFT; f++) {
            load_preset(0, NFACTORY - 1u);
            patch[A_ENG] = patch[B_ENG] = (int8_t)k; patch[B_LVL] = 127; patch[A_LVL] = 127; patch[F_TYPE] = (int8_t)f;
            patch[A_P1] = patch[A_P2] = patch[A_P3] = 127; patch[B_P1] = patch[B_P2] = patch[B_P3] = 0;
            patch[X_MODE] = (int8_t)(f % 4u); patch[X_AMT] = 127; patch[F_RES] = 127; patch[F_DRV] = 127;
            patch[L1_DEP] = 63; patch[L1_DST] = (int8_t)(1 + f % (ND - 1)); patch[L1_SHP] = (int8_t)(f % 9u);
            play(chord, 4, 20);
            play(low, 1, 10);
        }
    }
    {                                           /* FX LIVE at their extremes, the MACROS full, SEQ BASS and the ARP */
        static const int8_t FL[3] = {-64, 63, 20};
        uint32_t j;
        load_preset(0, 0);
        mc_defaults();
        for (j = 0; j < 4u; j++) mc_val[j] = 127;
        for (j = 0; j < 3u; j++) {
            lfx_flt = FL[j]; lfx_fres = 127; lfx_pump = 127; lfx_prate = (uint8_t)j; lfx_stut = (uint8_t)(j + 1u); lfx_tape = j == 2u ? 127 : 60;
            play(chord, 4, 60);
            lfx_stut = 0; lfx_tape = 0;
            play(low, 1, 30);
        }
        lfx_flt = 0; lfx_pump = 0;
        for (j = 1; j < 16u; j++) {             /* OSC C: every sample, from the keys and fixed, tuned to the ends */
            load_preset(0, 0);
            patch[SM_SEL] = (int8_t)j; patch[SM_LVL] = 127; patch[SM_KEY] = (int8_t)(j % 3u); patch[SM_TUNE] = j & 1u ? 24 : -24;
            play(chord, 4, 40);
            play(low, 1, 20);
        }
        for (j = 0; j < 4u; j++) {              /* every delay and reverb type, the compressor hard with its pump, the phaser */
            load_preset(0, j);
            patch[DL_ON] = patch[RV_ON] = patch[CP_ON] = patch[PH_ON] = patch[CH_ON] = patch[DR_ON] = 1;
            patch[DL_TYPE] = patch[RV_TYPE] = (int8_t)j; patch[DL_FB] = 127; patch[RV_SIZE] = 127; patch[RV_DAMP] = (int8_t)(j * 40);
            patch[CP_AMT] = 127; patch[CP_ATK] = (int8_t)(j * 40); patch[CP_REL] = 127; patch[CP_PUMP] = 127; patch[CP_PRATE] = (int8_t)j;
            patch[PH_FB] = 127; patch[PH_DEP] = 127; patch[PH_RATE] = 127; patch[PH_MIX] = 127; patch[MS_VOL] = 127;
            play(chord, 4, 80);
            play(low, 1, 120);
        }
        for (j = 0; j < 4u; j++) mc_val[j] = 0;
        perf_defaults();
        ev_push(EV_SEQ, 1, 1); blocks(400); ev_push(EV_SEQ, 0, 0); blocks(20);
        perf.ar_on = 1; perf.ar_oct = 3; perf.ar_mode = 2;
        play(chord, 4, 200);
        perf.ar_on = 0; blocks(10);
    }
    printf("stress: %u parameter runs, peak %.3f, late blocks %u: no integer wrapped\n", runs, (double)peak, ks_late);
    return 0;
}
