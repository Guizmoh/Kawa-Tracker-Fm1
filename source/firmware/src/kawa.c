/* SPDX-License-Identifier: GPL-3.0-only */
/* KAWA: a pocket tracker for the M-VAVE FM-1, for composing in any style (a tracker: time runs down the rows).
 *
 * Six tracks, each with its engine (pulse, 4-bit wave, noise, 6-operator FM on one track, a synthesised drum
 * kit, a sample cut in 16 slices or played as notes),
 * patterns of up to 32 rows with tricks per cell (arpeggio, glide, vibrato, rolls, bend, chance, delay,
 * cut, reverse, pitch, shape, crush), a song list, 32 live effects as on a Pocket Operator, and magic
 * tools that write beats, melodies and break chops in the scale.
 *
 * Built on Dinghy (zvenson), itself on Felucca (Hügelton Instruments) and SLOOP (isod89): platform.c
 * gives updates, the USB rescue, USB-MIDI and audio, the panel, the screen and flash.
 *
 * One compilation unit:
 *   kawa_data.c   names, parameter descriptions, factory sounds, chords, scales, effects, tools
 *   kawa_core.c   the project (patterns, sounds, song) and what the interrupt and the main loop share
 *   kawa_snd.c    the voices                         (audio interrupt)
 *   kawa_fx.c     the live effects and the master    (audio interrupt)
 *   kawa_seq.c    the sequencer, app_audio           (audio interrupt)
 *   kawa_edit.c   editing, magic tools, undo, the flash store
 *   kawa_gfx.c    drawing in bands
 *   kawa_ui.c     the pages, the panel, app_main
 *   kawa_sysex.c  the user's samples over USB-MIDI (SysEx)
 * Rules of the chip (docs/RULES.md): integers only, no 64-bit division, the audio interrupt short. */
#define APP_NAME "KAWA"
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_950"                   /* KAWA's identity, also outside build.py (it passes FM-1_95N / 96N) */
#endif
#if !defined(DINGHY_HOST) && !defined(KAWA_WASM)  /* (the host builds keep the store in memory) */
#ifndef STORE_LO
#define STORE_LO 0x00097000u                    /* the store: 4 project slots of 0x9000, then 4 user samples of */
#define STORE_HI 0x000E0000u                    /* 0x9000 from 0xBB000 (to 0xDF000), below the update staging (0xE0000) */
#endif
#endif
#include "platform.c"
#include "kawa_tables.h"
#include "kawa_breaks.h"
#include "kawa_art.h"
#include "kawa_photo.h"
#include "kawa_fm.h"
#include "kawa_kits.h"
#include "kawa_fonts.h"
#include "kawa_data.c"
#include "kawa_core.c"
#include "kawa_snd.c"
#include "kawa_fx.c"
#include "kawa_seq.c"
#include "kawa_edit.c"
#include "kawa_gfx.c"
#include "kawa_ui.c"
#include "kawa_sysex.c"
