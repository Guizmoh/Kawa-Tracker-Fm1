/* SPDX-License-Identifier: GPL-3.0-only */
/* K-WA SUB: a bass synthesizer for the M-VAVE FM-1 (drum & bass, dub, dubstep, techno), born from KAWA.
 *
 * Two oscillators with eight engines each (ANALOG, SUPER/REESE, WAVETABLE, FM, PHASE DIST, WAVEFOLDER, HARD
 * SYNC, VOCAL), added, ring-modulated or cross-modulated, a sub and a noise, a filter with seven characters
 * (ladder LP 24 / LP 12 / BP / HP / NOTCH, VOWEL, COMB), three envelopes, two LFOs locked to the tempo for the
 * wobble (rate changes stay on the beat), a 4-slot matrix, and an effect rack (saturation, crush, chorus,
 * dub delay, reverb, glue). Mono, legato or 4 voices.
 *
 * Built on KAWA's platform (Dinghy, Felucca, SLOOP): platform.c gives updates, the USB rescue, USB-MIDI
 * and audio, the panel, the screen and flash. One compilation unit:
 *   ksub_data.c   the patch's parameters, lists, factory presets
 *   ksub_dsp.c    the voices (core 1) and the effects (core 0)
 *   ksub_gfx.c    drawing in bands
 *   ksub_ui.c     the eight pages, the panel, MIDI, app_main
 * Rules of the chip (docs/RULES.md of Dinghy): integers only, no 64-bit division, the audio interrupt short. */
#define APP_NAME "K-WA SUB"
#ifndef APP_VER
#define APP_VER "0.2"
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_970"                   /* K-WA SUB's identity (KAWA's are 95N / 96N) */
#endif
#if !defined(DINGHY_HOST) && !defined(KAWA_WASM)
#define STORE_LO 0x00097000u                    /* the store: one sector of 32 user patches (+ one spare) */
#define STORE_HI 0x00099000u
#endif
#include "platform.c"
#if !defined(DINGHY_HOST) && !defined(KAWA_WASM)
#include "fm1_cpu1.h"
#ifndef KS_DUAL
#define KS_DUAL 1                               /* the voices on core 1 (SYSTEM: AUDIO CORES) */
#endif
#define POOL __attribute__((section(".pool")))
#else
#define POOL
#endif
#include "ksub_tables.h"
#include "ksub_fonts.h"
#include "ksub_samples.h"
#include "ksub_data.c"
#include "ksub_dsp.c"
#include "ksub_gfx.c"
#include "ksub_ui.c"
