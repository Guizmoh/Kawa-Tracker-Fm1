# Building KAWA

[README](../README.md) · [Français](../README.fr.md) · [Español](../README.es.md)

Everything here is for changing KAWA itself. To put KAWA on an FM-1, see the README: the ready package is in
[`../install/`](../install/). Run every command below from this `source/` folder.

## On a computer, no FM-1: the simulator

```sh
tests/run_host.sh                    # tests/tour.txt: the demo, every page, live effects
tests/run_host.sh tests/edit.txt     # step entry, the chopper, undo, the grid, saving
tests/run_host.sh tests/docs-en.txt  # the screens of the docs (docs-fr.txt, docs-es.txt)
tests/run_host.sh tests/new03.txt    # 0.3's pages: TABLE, SAMPLE EDIT, GROOVE, the clock, the second column
tests/run_host.sh tests/new05.txt    # 0.5's pages: the empty start, EDIT and LFO, SENDS and LIVE, the yellow bar, the SAVE tools, OPTIONS, 64 steps
```

It compiles `firmware/src/kawa.c` with `tests/host.c` (a fake FM-1) using the system C compiler. It writes
`build/host/kawa.wav` and the screens as PNG (Python 3 with Pillow). In a script, each line is
`T keys MASK`, `T press ID`, `T release ID`, `T btn ID`, `T enc ID STEPS`, `T lang L`, `T shot NAME` or
`T end`, where T is in milliseconds. See `tests/host.c` for the button and knob ids.

## In a browser: the emulator

```sh
web/make_emulator.sh                 # -> ../emulator/index.html (and build/web/)
```

This needs clang with the wasm32 target and wasm-ld (`lld`). No libc and no Emscripten are needed.
`web/wasm_host.c` is the FM-1 for the browser and `web/emulator.html` is the page. The wasm module is
embedded in the page, so `../emulator/index.html` stands alone.

## For the FM-1: the package

- Linux x86-64, or macOS / Windows with Docker or WSL.
- Python 3 with Pillow.
- The JieLi toolchain (clang for pi32v2): `tools/get_toolchain.sh` installs it to `~/.jieli/toolchain`.
- Three files of the JieLi AC79 SDK (Apache-2.0), at their tested version:

  ```sh
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

  Only `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin` and `cfg/eq_cfg_hw.bin` are used. `tools/build.py`
  checks their SHA-256 values and stops if one differs.

```sh
./build.sh                  # -> build/kawa.fwsc, identity FM-1_950
./build.sh --release 0.5    # -> build/kawa-0.5.fwsc, identity FM-1_955, version "0.5" on the screen
# a release is then copied twice: install/kawa-X.Y.fwsc (what the installers use, listed in install/SHA256SUMS) and kawa-X.Y.fwsc
# at the top of the repository (the file people download); source/tests/release_check.sh checks that both are the same
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations. The build fails on what would brick or
break an FM-1 firmware: an image, RAM or pool that is too big, a call into the chip ROM, a register touched
outside `hal/`, or a call from code that runs while the flash is busy. It also checks the flash map: the app
area ends at or below `0x93000` (all that the update loader writes), the store stays inside `0x97000..0xE0000`,
clear of the update staging `0xE0000..0xE5000` and the settings `0xFC000`, the app cannot write the flash head
or its own code, the loader cannot write the head, the store or the settings, and the loader's `ota.bin` stays
within 19456 bytes (the most the stock firmware stages, for a first install from it). A release `X.Y` is
`FM-1_95Y` for 0.Y and `FM-1_96Y` for 1.Y; a major 2 is refused until there is a new scheme. The chip has no
FPU, so the code uses integers only and no 64-bit division. See Dinghy's `docs/RULES.md` for the details.

The update loader (`firmware/loader/`, Felucca's) reads the package's whole `flash.bin` once and checks it
against the package's CRC16 before it erases anything, so a damaged or cut package leaves the old firmware
as it is. On that pass it keeps two CRC16s of every app sector (two polynomials: 32 bits). Then it writes
the app area `0x4000..0x93000` only, sector by sector; a sector that comes in different from the checked
pass is read again, never written, and every written sector is read back and compared. Until the host says "success" the update
record stays: after a power cut or a lost cable the FM-1 starts in the loader again and the host can resume.
On an unknown flash chip it writes nothing and the old firmware starts again. After `./build.sh`,
`tests/run_loader_tests.sh` runs both steps of an update on a computer against the package (it needs
`AC79_SDK` too).

The audio interrupt works in integers, and a number that wraps is a full-scale click on the FM-1.
`tests/run_stress_test.sh` builds KAWA on the computer under UBSan and plays every factory sound with every
track effect, every master effect and every live effect at the ends of their knobs, the kits at DECAY +8 and
DRIVE 15, FM loud and panned, then 0.3's features at theirs: two tricks in every cell, HOP and END on every
row, tables a tick a step with HITs, the groove at 1 and 12 ticks at 40 and 300 BPM, sample edits of no width
and x16 on a full-scale sound, the MIDI clock in at 40 and 300 BPM with bursts and gaps, then 0.5's: the SAT
send of every track (0, 7 and 15) on every factory sound and with a track effect, the metronome at its loudest
over a loud song at both ends of the tempo, and a 64-step pattern in the loop and in the song (about 18 minutes
of sound): it stops at the first integer that wraps and names the line, and checks every voice and table ends.
`tests/run_features_test.sh` plays the new features through the audio interrupt and checks what comes out
(HOP, END, the two columns, a table's steps and HITs, the groove's row lengths and clock ticks, the sample
edit's positions and gain, the clock in: rows within a tick of the clock, 3 to 5 ms after it in blocks of
256 frames with 0..3 ms of jitter, and a PLAY that waits for a clock that does not come). The products that can go past 32 bits are done in 64 (one `smul` on the pi32v2; only a 64-bit
division is not allowed). `tests/run_kits_test.sh` measures the drums (clipping, DC, the hits ending, how far
apart the kits are), and `tests/breaks_check.py` the breaks.

`tests/run_ui_test.sh` (0.5) drives the panel as the buttons and encoders would (the firmware's own
`ui_input`), under UBSan, and checks what the pages and the sound do: a first start (empty song, 100 BPM,
English); EDIT going round its pages without LFO and MODULATION, and LFO having those two; the factory sound
changing on SOUND only, an edited sound not replaced by one turn by mistake, ARP + PRESETS picking the engine;
SELECT turning the pages of ENV / MIX, FX and GLO and stopping at their ends; FX tap, tap, tap (SENDS, LIVE,
back) and FX held; SENDS (the drive is heard, and bounded); the tools of a SAVE hold and OCT- / OCT+ for undo and
redo; the metronome (off, on, REC; three volumes; kept in the settings); 16, 32 and 64 steps, the 64 going on
into the next pattern in the loop and in the song; SCL going SCALE, MAGIC, back; and a 0.4 project (zeroed pad
bytes) and a damaged pad loading safely.

`node tests/webinstall_test.js` tests the web installer (`../install/webinstall.js`, used by the Install button
of `../index.html`) against a simulated FM-1 on a simulated Web MIDI (`tests/fm1_mock.js`): plain Node 18+, no
hardware and no browser. It installs the newest `../install/kawa-*.fwsc` that `../install/SHA256SUMS` names,
so build the package first. It covers a good install (with MIDI noise, SysEx cut in pieces, a device that
re-enumerates), damaged and wrong packages (refused before any port is opened), the wrong model, an FM-1 found
in update mode, a cable cut mid-write and the resume, and a missing, silent or busy port. With `python3` on the
PATH it also checks the port against `../install/fm1_install.py` itself: same constants, delays, wire format,
CRC16 and the same verdict for every damaged package.

## Safety: rescue, and back to the official firmware

- **Rescue.** Hold OCT- while switching on, or let KAWA fail to start twice within 30 s: the screen shows
  `KAWA USB RESCUE`. Audio, projects and the editor stay off; only USB and the same checked updater run, so
  a package can be installed from there (`../install/fm1_install.py`, or M-UPGRADE for the official firmware).
- **A cut install.** The update record stays until the host says "success": switched on again, the FM-1
  starts in the loader (`ota-FM-1_900`) and `../install/fm1_install.py` finishes the write.
- **Back to the official firmware.** M-UPGRADE with the official V15 package (identity `FM-1_015`, SHA-256
  `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`). KAWA hands over to the official
  updater only if it is M-VAVE's own loader (its CRCs are known, `firmware/src/ota.c`) and only if the
  package's flash head `0..0x4000` is byte for byte the one already on this FM-1: the official loader
  rewrites the head, so it can only write back the same bytes. If either check fails, nothing is written
  and KAWA keeps running. This step is the riskiest one (the official loader is not ours): a stable power
  supply, a data cable, and no unplugging while it runs. The KAWA projects and samples are not kept.
- **Not proven.** The chip's mask-ROM USB mode (WL80UBOOT, the FM-1 Transporter) is documented by
  [fm-1-re](https://github.com/al-255/fm-1-re) but not tested here; KAWA never relies on it.

## Generated files

| File | Made by | |
| --- | --- | --- |
| `firmware/gen/kawa_art.h` | `tools/gen_art.py` | icons 8 x 8 and the mascot 16 x 16, drawn as text |
| `firmware/gen/kawa_photo.h` | `tools/gen_art.py` | the boot picture, from `art/boot-dog.png`: 240 x 240, 192 colours, a byte a pixel (needs Pillow) |
| `firmware/gen/kawa_breaks.h` | `tools/gen_breaks.py` | six breaks, synthesised, 22050 Hz, 8-bit mu-law (needs numpy; `--wav DIR` to listen; `tests/breaks_check.py` checks them) |
| `firmware/gen/kawa_tables.h` | `tools/gen_tables.py` | fixed-point tables: pitch, envelopes, filters, the 4-bit waves |
| `firmware/gen/felucca_font.h` | Dinghy | Terminus 8 x 16 (SIL OFL 1.1): the screen font BASE |
| `firmware/gen/kawa_fonts.h` | `tools/gen_fonts.py` | the screen fonts DOUCE, CLAIRE and 8-BIT, from `assets/fonts/` (SIL OFL 1.1; needs Pillow) |

## The project format

A project is format 5 (KAWA 0.3's, and still 0.5's): 32 patterns of 32 rows of six cells of 5 bytes (the note; the
volume on 4 bits and two tricks on 6 bits each; their two values) and the LIVE lane, the six sounds and the FM patch, the
song, then a table per track (16 steps of 4 bytes, its speed, length, loop), a sample edit per track (start,
end, 16 slices in 65536ths of the sound, the gain, reverse) and the groove: 33,072 bytes with its CRC-32, in a
36 KB slot. A format-4 project (KAWA 0.2: cells of 4 bytes, none of the rest) is converted in place when it
loads (`prj_from_v4` in `kawa_edit.c`: from the last pattern and the last cell, so nothing not yet read is
written over) and keeps playing as it did; `tests/run_format_test.sh` checks it with projects written by
0.2's own code (`tests/fixtures/`), a damaged one, and a format-5 one with every field, byte for byte.
Everything that comes from a slot goes through `prj_sanitize` before the audio interrupt sees it.

KAWA 0.5 stays in format 5: it only gives a meaning to two bytes that 0.4 left as padding, so the size and the
slot do not change. `sound_t.sat` is the SAT send of a sound (the drive of the SENDS page, 0..15; a 0.4 project
has 0 there), and `pat_t.pad[0]` is the link of a 64-step pattern: 1 means it goes on into the next pattern (32 +
32 steps, played as one in the loop and in the song). `prj_sanitize` sets an out-of-range `sat` back to 0 and keeps
only bit 0 of the link. A 0.5 project loads in 0.4 and 0.3, which simply ignore both bytes (no drive, and the 64-step link is not
followed); a 0.4 project loads in 0.5 with no drive and no link. `tests/ui_test.c` checks the 0.4 case.

## Memory and flash

The 0.5 build uses an image of about 495 KB of the 568 KB available (0.4's was about 479 KB; the boot picture
is 58 KB of it). The 0.3 build used 46 KB of the 96 KB of RAM and 292 KB of the 336 KB pool (the effects' capture,
the echo, the project and the screen bands); `tools/build.py` prints the figures of every build and stops when
one does not fit. Projects are kept in four 36 KB slots from `0x97000` (slot 0 is the autosave) and the user samples in four 37 KB slots from
`0xBB000` up to `0xE0000`: the store is `0x97000..0xE0000`, the part of the flash an update never erases.
The language, theme, octave, clock, font and the metronome (its mode and its volume) are kept in the settings
sector `0xFC000`: 7 bytes (`set` in `kawa_ui.c`, with the length and a CRC-32 in front). The settings of 0.4 (5
bytes) and of 0.2 (4 bytes) still load, and what they lack takes its default (font DOUCE, metronome off, volume
MED). Flash is only erased when nothing plays, because an erase stalls the sound.

## All the checks, in one place

From this `source/` folder (none of them needs an FM-1):

```sh
sh tests/run_features_test.sh      # 0.3's features through the audio interrupt (UBSan)
sh tests/run_format_test.sh        # projects saved by 0.2 and 0.3 load as they were
sh tests/run_kits_test.sh          # the drum kits; python3 tests/breaks_check.py: the breaks
sh tests/run_ui_test.sh            # the panel: pages, SELECT, protected sounds, undo, SENDS, holds, tools, metronome, 64 steps
sh tests/run_stress_test.sh        # every sound and effect at the ends of the knobs (about 18 minutes of sound)
AC79_SDK=~/fw-AC79_AIoT_SDK sh tests/run_loader_tests.sh    # the install path on the package in build/ (after ./build.sh)
node tests/webinstall_test.js      # the web installer (Install button) against a simulated FM-1
node tests/websamples_test.js      # the web sample importer, byte for byte against install/kawa_samples.py
NODE_PATH=$(npm root -g) node tests/webinstall_ui_test.js    # the same pages in a real Chromium (Playwright)
NODE_PATH=$(npm root -g) node tests/websamples_ui_test.js
sh tests/release_check.sh          # install/SHA256SUMS and the package at the top of the repository agree
python3 tests/manual_shots.py      # regenerates the manual's screens (../manual/img/)
```
