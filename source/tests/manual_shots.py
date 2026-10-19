#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The manual's screens: the tutorials played on the simulator (tests/host.c), in French, English and Spanish.

  python3 tests/manual_shots.py            (-> ../manual/img/{fr,en,es}/NN-name.png, 480 x 480)

Each step is a list of panel actions; a shot is taken after it. Buttons: 0 OCT- 1 OCT+ 2 FX 3 SCL 4 ENV 5 LFO
6 EDIT 7 GLO 8 HOME 9 SAVE 10 ARP 11 SEQ 12 PLAY 13 REC; encoders: 0 SELECT 1 ALGORITHM 2..5 KNOB 1..4
6 PRESETS; keys: bit k = key k (0 = F3; white keys 0 2 4 6 7 9 11 12 14 16 18 19 21 23 24 26)."""
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]

# (name, actions) - an action: ("btn", id) ("press", id) ("release", id) ("enc", id, steps) ("key", k) (a tap)
# ("hold", k) ("up",) ("wait", ms)
STEPS = [
    ("01-boot", [("wait", 700)]),
    ("08-first-start", [("btn", 8), ("wait", 900)]),   # (the boot screen waits for a key; an empty song shows how to start)
    ("02-tracks", [("btn", 9), ("enc", 0, 4), ("btn", 1), ("wait", 700)]),   # (KAWA starts empty: the demo, from PROJECTS)
    ("03-play", [("btn", 12), ("wait", 1500)]),
    ("04-help", [("press", 8), ("wait", 700)]),
    ("05-tracks-stop", [("release", 8), ("btn", 12)]),
    # tutorial 1: a new song
    ("10-projects-menu", [("btn", 9)]),
    ("11-new-song-select", [("enc", 0, -10)]),
    ("12-new-song", [("btn", 1), ("wait", 600)]),
    ("13-sound-page", [("btn", 6)]),
    ("14-engine-picker", [("press", 10), ("enc", 6, 1), ("release", 10)]),
    ("15-preset", [("press", 10), ("enc", 6, -1), ("release", 10), ("enc", 6, 1)]),
    # tutorial 2: writing notes
    ("20-rec", [("btn", 8), ("btn", 13), ("key", 7), ("key", 11), ("key", 14), ("key", 11)]),
    ("21-step-jump", [("press", 13), ("enc", 0, 1), ("release", 13)]),
    ("22-cell-knobs", [("key", 7), ("key", 9), ("enc", 0, -2), ("enc", 4, 1)]),
    ("23-grid-kit", [("btn", 13), ("enc", 1, 4), ("btn", 11), ("key", 0), ("key", 7), ("key", 14), ("key", 21),
                     ("key", 4), ("key", 19)]),
    ("24-magic", [("btn", 3), ("btn", 3)]),
    ("25-magic-beat", [("key", WHITE[1]), ("wait", 200)]),
    ("26-clone", [("key", WHITE[11]), ("wait", 200)]),
    ("27-tracks-p02", [("btn", 8), ("wait", 1300)]),
    # tutorial 3: the song
    ("30-song", [("btn", 7)]),
    ("31-song-repeat", [("enc", 1, -1), ("enc", 4, 1)]),
    ("32-song-add", [("enc", 1, 2), ("key", WHITE[0])]),
    ("33-song-mode", [("btn", 10)]),
    ("34-song-play", [("enc", 1, -10), ("btn", 12), ("wait", 2000)]),
    # tutorial 4: saving
    ("40-save", [("btn", 12), ("btn", 9)]),
    ("41-save-slot", [("btn", 1)]),
    ("42-save-name", [("btn", 1), ("enc", 3, 1)]),
    ("43-saved", [("btn", 1), ("wait", 300)]),
    # the sound
    ("50-envelope", [("btn", 9), ("btn", 8), ("enc", 1, -4), ("btn", 4), ("btn", 12), ("key", 7), ("wait", 120)]),
    ("51-mix", [("btn", 12), ("btn", 4)]),
    ("52-fm-sound", [("enc", 1, 1), ("btn", 6)]),
    ("53-fm-operator", [("btn", 6), ("btn", 6), ("enc", 6, 2)]),
    ("54-fm-openv", [("btn", 6)]),
    ("55-effect", [("btn", 6), ("enc", 2, 1), ("enc", 3, -3)]),
    ("56-lfo", [("btn", 5), ("enc", 2, 3)]),
    ("57-mod", [("btn", 5), ("enc", 2, 6), ("enc", 3, 8)]),
    ("58-kit", [("enc", 1, 3), ("btn", 6), ("enc", 2, 1)]),
    ("59-sample", [("enc", 1, 1)]),
    # live
    ("60-fx", [("btn", 8), ("btn", 12), ("btn", 2), ("btn", 2), ("hold", WHITE[0]), ("wait", 600)]),
    ("61-fx-bank2", [("up",), ("btn", 1), ("hold", WHITE[2]), ("wait", 600)]),
    ("62-options", [("up",), ("btn", 12), ("btn", 7), ("btn", 7), ("btn", 7)]),
    ("63-scale", [("btn", 3)]),
    # 0.3: the groove, a table, a sample's edit, the second trick column
    ("64-groove", [("btn", 7), ("btn", 7), ("enc", 4, 2), ("enc", 1, 1), ("btn", 12), ("wait", 900)]),
    ("65-table", [("btn", 12), ("btn", 8), ("enc", 1, -10), ("btn", 6), ("btn", 6), ("btn", 6), ("btn", 6),
                  ("enc", 5, 1), ("enc", 2, -4), ("enc", 3, -1), ("enc", 6, 2), ("enc", 2, 4), ("enc", 6, 1),
                  ("enc", 2, 7), ("hold", WHITE[3]), ("wait", 300)]),
    ("66-sample-edit", [("up",), ("enc", 1, 5), ("btn", 6), ("btn", 6), ("btn", 6), ("enc", 2, 12), ("enc", 6, 1),
                        ("enc", 2, -10), ("enc", 6, 4), ("enc", 2, 3), ("enc", 4, 1), ("wait", 300)]),
    ("67-fx2", [("btn", 8), ("enc", 1, -10), ("press", 10), ("enc", 4, 16), ("enc", 5, 8)]),
    # 0.5: SENDS (the first FX page), the yellow bar of a hold, the tools of SAVE, 64 steps
    ("70-sends", [("release", 10), ("btn", 8), ("btn", 2), ("enc", 2, 5), ("enc", 3, 4), ("enc", 4, 3), ("enc", 1, 1),
                  ("enc", 2, 9), ("enc", 3, 2)]),
    ("71-hold-bar", [("btn", 8), ("press", 9)]),
    ("72-tools", [("wait", 300), ("enc", 0, 3)]),
    ("73-tools-options", [("enc", 0, -3), ("release", 9)]),
    ("74-grid-64", [("btn", 8), ("btn", 11), ("btn", 1), ("btn", 1), ("wait", 100), ("enc", 0, 3)]),
    # 0.5: an edited sound is not replaced by PRESETS (shown, then OCT+), and UNDO brings a sound back
    ("75-preset-browse", [("btn", 8), ("enc", 1, -10), ("btn", 6), ("enc", 2, 1), ("wait", 200), ("enc", 6, 1)]),
    ("76-preset-loaded", [("wait", 1200), ("btn", 1)]),
    ("77-undo-sound", [("wait", 1200), ("press", 9), ("btn", 0), ("release", 9)]),
]

def script(lang):
    t, out = 100, [f"100 lang {lang}"]
    held = 0
    for name, acts in STEPS:
        for a in acts:
            if a[0] == "wait":
                t += a[1]
                continue
            t += 120
            if a[0] in ("btn", "press", "release"):
                out.append(f"{t} {a[0]} {a[1]}")
            elif a[0] == "enc":
                out.append(f"{t} enc {a[1]} {a[2]}")
            elif a[0] == "key":
                out.append(f"{t} keys {hex(held | 1 << a[1])}")
                t += 90
                out.append(f"{t} keys {hex(held)}")
            elif a[0] == "hold":
                held |= 1 << a[1]
                out.append(f"{t} keys {hex(held)}")
            elif a[0] == "up":
                held = 0
                out.append(f"{t} keys 0x0")
        t += 260
        out.append(f"{t} shot {name}")
    out.append(f"{t + 200} end")
    return "\n".join(out) + "\n"


def main():
    build = ROOT / "build" / "manual"
    build.mkdir(parents=True, exist_ok=True)
    exe = ROOT / "build" / "host" / "kawa"
    subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-Wno-unused-parameter", "-DDINGHY_HOST",
                    '-DAPP_VER="0.5"', "-Ifirmware/src", "-Ifirmware/gen", "-o", str(exe), "firmware/src/kawa.c"],
                   cwd=ROOT, check=True)
    for code, lang in (("fr", 0), ("en", 1), ("es", 2)):
        s = build / f"shots-{code}.txt"
        s.write_text(script(lang))
        for p in build.glob(f"{code}-*.ppm"):
            p.unlink()
        subprocess.run([str(exe), str(build / code), str(s)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        dst = ROOT.parent / "manual" / "img" / code
        dst.mkdir(parents=True, exist_ok=True)
        n = 0
        for p in sorted(build.glob(f"{code}-*.ppm")):
            Image.open(p).resize((480, 480), Image.NEAREST).save(dst / (p.stem[len(code) + 1:] + ".png"), optimize=True)
            p.unlink()
            n += 1
        print(f"{code}: {n} screens -> {dst}")


if __name__ == "__main__":
    sys.exit(main())
