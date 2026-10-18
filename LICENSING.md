# Licensing

KAWA is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give
them its complete source under the same licence too.

*En bref : KAWA est un logiciel libre sous GPL-3.0. Tu peux l'utiliser, l'étudier, le modifier et le
partager ; si tu donnes un firmware construit à partir de lui, donne aussi son code source.*
*En resumen: KAWA es software libre bajo GPL-3.0. Puedes usarlo, estudiarlo, cambiarlo y compartirlo; si
das un firmware construido a partir de él, da también su código fuente.*

## Who made what

| | |
| --- | --- |
| KAWA: `source/firmware/src/kawa*.c`, `source/firmware/gen/kawa_*.h`, `source/tools/gen_*.py`, `source/tests/`, `source/web/`, `emulator/`, `manual/`, the install launchers and the docs | Omnipotard |
| KAWA's dog: `source/art/boot-dog.png` (the boot picture, the dog in 32 bits) and `source/art/mascot-dog.png` (the 16-bit dog the mascot is drawn after) | Omnipotard |
| The platform: `source/firmware/hal/`, `source/firmware/loader/`, `crt0.S`, `app.ld`, `source/firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `source/tools/build.py`, `install/fm1_install.py`, `source/tools/fm1pkg_make.py`, `source/tools/lz4blk.py`, `source/tools/get_toolchain.sh` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The wiring `source/firmware/src/platform.c` and the host mode that `source/tests/host.c` started from | zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |

KAWA changes the platform in two places: `platform.c` widens the flash range it may write to the project
slots (`STORE_LO` to `STORE_HI`) and includes `source/web/wasm_host.c` for the browser build. `source/tools/build.py`
builds `kawa.c` under KAWA's package identity (`FM-1_95N`).

## Sounds

Every sound of KAWA is synthesised. The breaks are made by `source/tools/gen_breaks.py` from sines, noise and
filters, and played in classic break rhythms; no recording is used. The eight drum kits, the waves and the
noise are generated live by the firmware. The sounds you put in the four user slots are yours.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `source/firmware/gen/felucca_font.h`: the screen font BASE | SIL OFL 1.1 | `source/assets/fonts/Terminus-LICENSE.txt` |
| The screen fonts DOUCE, CLAIRE and 8-BIT (OPTIONS, KNOB 4), rendered into `source/firmware/gen/kawa_fonts.h` by `source/tools/gen_fonts.py`: derived from IBM Plex Mono SemiBold (Copyright © 2017 IBM Corp., Reserved Font Name "Plex"), Atkinson Hyperlegible Mono (Copyright 2020-2024 The Atkinson Hyperlegible Mono Project Authors) and Press Start 2P (Copyright 2012 The Press Start 2P Project Authors, Reserved Font Name "Press Start 2P"). As Modified Versions under the OFL, they do not carry the Reserved Font Names | SIL OFL 1.1 | `source/assets/fonts/` (the fonts and their licences) |
| msfa by Google Inc. and Pascal Gauthier, from Dexed (<https://github.com/asb2m10/dexed>), in Felucca's integer C port (Leo Kuroshita, Hügelton Instruments): the FM track's synthesis. Only msfa is used. Eight of the twelve FM factory sounds (`source/tools/gen_fm_patches.py`) are Felucca's (GPL-3.0); the other four are KAWA's | Apache-2.0 | `source/firmware/src/fm6_core.c`, `source/LICENSES/Apache-2.0-msfa.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| Fonts of the emulator page (Silkscreen, Atkinson Hyperlegible, IBM Plex Mono), loaded from Google Fonts | SIL OFL 1.1 | not in this tree |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. KAWA is not made or endorsed by them.
