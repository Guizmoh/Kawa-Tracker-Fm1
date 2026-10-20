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
    for (k = 0; k < 8; k++) {                   /* each engine on both oscillators, each filter, the extremes of P1..P3 */
        uint32_t f;
        for (f = 0; f < NFT; f++) {
            load_preset(0, NFACTORY - 1u);
            patch[A_ENG] = patch[B_ENG] = (int8_t)k; patch[B_LVL] = 127; patch[A_LVL] = 127; patch[F_TYPE] = (int8_t)f;
            patch[A_P1] = patch[A_P2] = patch[A_P3] = 127; patch[B_P1] = patch[B_P2] = patch[B_P3] = 0;
            patch[X_MODE] = (int8_t)(f % 3u); patch[X_AMT] = 127; patch[F_RES] = 127; patch[F_DRV] = 127;
            patch[L1_DEP] = 63; patch[L1_DST] = (int8_t)(1 + f % (ND - 1)); patch[L1_SHP] = (int8_t)(f % 9u);
            play(chord, 4, 20);
            play(low, 1, 10);
        }
    }
    printf("stress: %u parameter runs, peak %.3f, late blocks %u: no integer wrapped\n", runs, (double)peak, ks_late);
    return 0;
}
