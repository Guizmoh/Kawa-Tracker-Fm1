#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KAWA's pixel art: firmware/gen/kawa_art.h (drawn here as text) and firmware/gen/kawa_photo.h (from a PNG).

  python3 tools/gen_art.py            (needs Pillow, for the boot picture)

Icons are 8 x 8 ('#' lit), drawn by the firmware in one colour at 1x or 2x.
The mascot (16 x 16, the corner of every page) is KAWA's dog as Omnipotard drew him in 16 bits: a shepherd
with headphones and his tongue out, 16 inks (INKS below) drawn with a palette: the headband and the cups take
the colour of what is going on.
The boot picture is art/boot-dog.png (the dog in 32 bits, by Omnipotard), taken to 240 x 240 and 192 colours,
one byte a pixel (kawa_photo.h, about 58 KB of flash), with the stars of its sky that twinkle.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "gen" / "kawa_art.h"
PHOTO_OUT = ROOT / "firmware" / "gen" / "kawa_photo.h"
BOOT_PNG = ROOT / "art" / "boot-dog.png"
BOOT_COLOURS = 192
TITLE_BOX = (116, 4, 238, 80)    # x0 y0 x1 y1: the title, on the sky (no twinkle under it)
SPARKLES = [(40, 12), (22, 50), (204, 96), (228, 106)]   # twinkles in the gaps of the sky

ICONS = {
    # the six tracks
    "PULSE": ["........", ".###..##", ".#.#..#.", ".#.#..#.", ".#.#..#.", ".#.#..#.", "##.####.", "........"],
    "WAVE": ["........", "...#....", "..#.#...", ".#...#..", "#.....#.", ".......#", "........", "........"],
    "NOISE": ["........", ".#...#..", ".##.##.#", "#.#.#.##", "#.##.#.#", "#..#...#", "...#....", "........"],
    "KIT": ["........", ".######.", "#......#", "########", "#.#..#.#", "#......#", ".######.", "........"],
    "BREAK": ["....##..", "...##...", "..##....", ".######.", "....##..", "...##...", "..##....", ".##....."],
    # the cell tricks (FX column)
    "ARP": ["........", "......##", "......#.", "....###.", "....#...", "..###...", "..#.....", "###....."],
    "GLIDE": ["........", ".......#", "......#.", ".....#..", "...##...", ".##.....", "#.......", "........"],
    "VIB": ["........", "........", ".#...#..", "#.#.#.#.", "...#...#", "........", "........", "........"],
    "ROLL": ["........", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "........"],
    "BEND": ["....#...", "...###..", "..#.#.#.", "....#...", "...#....", "..#.....", "##......", "........"],
    "DICE": [".######.", "#......#", "#.#..#.#", "#......#", "#.#..#.#", "#......#", ".######.", "........"],
    "CLOCK": ["..####..", ".#....#.", "#...#..#", "#...#..#", "#...##.#", "#......#", ".#....#.", "..####.."],
    "CUT": ["#.....#.", ".#...#..", "..#.#...", "...#....", "..#.#...", ".##.##..", "#..#..#.", ".##..##."],
    "REV": ["........", "...#...#", "..##..##", ".###.###", "..##..##", "...#...#", "........", "........"],
    "SHARP": ["..#..#..", "..#..#..", ".######.", "..#..#..", "..#..#..", ".######.", "..#..#..", "..#..#.."],
    "SHAPE": ["........", ".###....", ".#.#....", ".#.#....", ".#.#....", "##.#####", "........", "........"],
    "CRUSH": ["........", "......##", "......##", "....####", "....####", "..######", "..######", "########"],
    # live effects
    "LOOP": ["..####..", ".#....#.", "#......#", "#......#", "#..#...#", ".#.##.#.", "..####..", "...#...."],
    "UNI": ["........", ".##.##..", "#..#..#.", "#..#..#.", "#..#..#.", ".##.##..", "........", "........"],
    "UP": ["...#....", "..###...", ".#.#.#..", "...#....", "...#....", "...#....", "...#....", "........"],
    "DOWN": ["...#....", "...#....", "...#....", "...#....", ".#.#.#..", "..###...", "...#....", "........"],
    "STUT": ["........", "#.#.#.#.", "#.#.#...", "#.#.#...", "#.#.....", "#.#.....", "#.......", "........"],
    "DISC": ["..####..", ".#....#.", "#..##..#", "#.#..#.#", "#.#..#.#", "#..##..#", ".#....#.", "..####.."],
    "TRI3": ["........", "........", "#..#..#.", "#..#..#.", "........", "#..#..#.", "........", "........"],
    "AGAIN": ["..###.#.", ".#...##.", "#...###.", "#.......", "#......#", ".#....#.", "..####..", "........"],
    "ACCEL": ["........", "#..#.#.#", "#..#.#.#", "#..#.#.#", "#..#.#.#", "#..#.#.#", "#..#.#.#", "........"],
    "PIXEL": ["##..##..", "##..##..", "..##..##", "..##..##", "##..##..", "##..##..", "..##..##", "..##..##"],
    "LPF": ["........", "####....", "....#...", ".....#..", "......#.", "......#.", ".......#", "........"],
    "HPF": ["........", "....####", "...#....", "..#.....", ".#......", ".#......", "#.......", "........"],
    "TAPE": ["........", "########", "#......#", "#.#..#.#", "#......#", "#.####.#", "########", "........"],
    "DIVE": ["#.......", ".#......", "..#.....", "...#....", "....#..#", ".....#.#", "......##", "....####"],
    "GATE": ["........", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "##.##.##", "........", "........"],
    "SNOW": ["...#....", ".#.#.#..", "..###...", "#######.", "..###...", ".#.#.#..", "...#....", "........"],
    "RING": ["..####..", ".#....#.", "#..##..#", "#.#..#.#", "#..##..#", ".#....#.", "..####..", "........"],
    "ECHO": ["........", "#.......", "#.#.....", "#.#.#...", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "........"],
    "DIST": ["........", ".####...", "#....#..", "......#.", ".......#", "......#.", "#....#..", ".####..."],
    "WOB": ["........", "#......#", ".#....#.", "..#..#..", "..#..#..", ".#....#.", "#......#", "........"],
    "NOMEL": ["#....#..", ".#...##.", "..#..#.#", "...#.#..", "..###...", ".####.#.", ".###...#", "........"],
    "NOBEAT": ["#......#", ".######.", "#.#..#.#", "########", "#.#..#.#", "#..##..#", ".######.", "#......#"],
    # the magic tools
    "KNIFE": ["......##", ".....###", "....###.", "...###..", "..###...", ".#.#....", "#.#.....", ".#......"],
    "NOTE": ["...###..", "...#.##.", "...#....", "...#....", ".###....", "####....", ".##.....", "........"],
    "BASS": ["........", "...#....", "...#....", "...#....", ".###....", "####....", ".##.....", "########"],
    "EUCL": ["..#..#..", ".......#", "#.......", "........", "#......#", "........", ".#....#.", "...##..."],
    "VOL": ["........", "......#.", "....#.#.", "..#.#.#.", "#.#.#.#.", "#.#.#.#.", "#.#.#.#.", "........"],
    "X2": ["........", "#...#...", ".#.#....", "..#..##.", ".#.#...#", "#...#.#.", "....####", "........"],
    "RIGHT": ["........", "....#...", "....##..", "#######.", "....##..", "....#...", "........", "........"],
    "MIRROR": ["...#....", ".#.#.#..", "##.#.##.", "##.#.##.", "##.#.##.", ".#.#.#..", "...#....", "........"],
    "UPDN": ["...#....", "..###...", ".#.#.#..", "...#....", ".#.#.#..", "..###...", "...#....", "........"],
    "COPY": ["####....", "#..#....", "#.####..", "#.#..#..", "###..#..", "..#..#..", "..####..", "........"],
    "PASTE": ["..##....", "######..", "#....#..", "#.##.#..", "#....#..", "#.##.#..", "######..", "........"],
    "ERASE": ["........", "#.....#.", ".#...#..", "..#.#...", "...#....", "..#.#...", ".#...#..", "#.....#."],
    # engines, envelope, LFO, routing
    "FM": ["........", ".##..##.", "#..##..#", "#..##..#", "........", ".##..##.", "#..##..#", "........"],
    "SAMPLE": ["........", "...#....", "..##.#..", ".###.##.", "#######.", ".###.##.", "..##.#..", "...#...."],
    "ENV": ["........", "...#....", "..#.#...", "..#..##.", ".#.....#", ".#.....#", "#......#", "........"],
    "LFO": ["........", ".##.....", "#..#....", "#..#...#", "....#..#", "....#..#", ".....##.", "........"],
    "BPF": ["........", "...##...", "..#..#..", "..#..#..", ".#....#.", ".#....#.", "#......#", "........"],
    "TREM": ["........", "#.......", "##.#....", "####.#..", "########", "####.#..", "##.#....", "#......."],
    "VERB": ["..####..", ".#....#.", "#..##..#", "#.#..#.#", "#..##..#", ".#....#.", "..####..", "........"],
    "CLONE": ["####....", "#..#....", "#.####..", "#.#..#..", "###..#..", "..####..", "........", "........"],
    "THEME": ["..###...", ".#...#..", "#.#.#.#.", "#.....#.", "#.##..#.", ".#...#..", "..###...", "........"],
    "TARGET": ["...#....", ".#####..", "#.....#.", "#..#..#.", "#.....#.", ".#####..", "...#....", "........"],
    # small signs
    "PLAY": ["#.......", "##......", "###.....", "####....", "###.....", "##......", "#.......", "........"],
    "STOP": ["........", "######..", "######..", "######..", "######..", "######..", "........", "........"],
    "REC": ["........", "..###...", ".#####..", ".#####..", ".#####..", "..###...", "........", "........"],
    "CHAIN": ["........", "........", ".##..##.", "#..##..#", "#..##..#", ".##..##.", "........", "........"],
    "LOCK": ["..###...", ".#...#..", ".#...#..", "#######.", "###.###.", "###.###.", "#######.", "........"],
    "MUTE": ["...#....", "..##..#.", "####.#..", "####..#.", "####.#.#", "..##....", "...#....", "........"],
    "BETA": [".###....", "#...#...", "#..#....", "####....", "#...#...", "#...#...", "####....", "#......."],
}

# KAWA's dog: a shepherd with headphones (Omnipotard's 16-bit dog). 4 bits a pixel, inks:
INKS = ".kDGLWENPTHCcSRz"   # clear, outline, dark / mid / light fur, white, amber eye, nose, pupil, tongue,
#                            headband (accent), cups, cups' rim, highlight, inner ear, extra
DOG16 = {
    "IDLE": [
        "..k..........k..",
        ".kDk........kDk.",
        ".kDRk.HHHH.kRDk.",
        ".kDRGkHHHHkGRDk.",
        ".kGGDDDDDDDDGGk.",
        "CCkGGLDDDDLGGkCC",
        "CHGLSPGDDGSPLGHC",
        "CHGLPPGGGGPPLGHC",
        "CCkGLLLWWLLLGkCC",
        ".kGLWWNNNNWWLGk.",
        ".kLWWWkNNkWWWLk.",
        "..kLWkkTTkkWLk..",
        "..kGLkTTTTkLGk..",
        "...kGkTTTTkGk...",
        "....kkkTTkkk....",
        ".......kk.......",
    ],
    "BLINK": [
        "..k..........k..",
        ".kDk........kDk.",
        ".kDRk.HHHH.kRDk.",
        ".kDRGkHHHHkGRDk.",
        ".kGGDDDDDDDDGGk.",
        "CCkGGLDDDDLGGkCC",
        "CHGLGGGDDGGGLGHC",
        "CHGLkkGGGGkkLGHC",
        "CCkGLLLWWLLLGkCC",
        ".kGLWWNNNNWWLGk.",
        ".kLWWWkNNkWWWLk.",
        "..kLWkkTTkkWLk..",
        "..kGLkTTTTkLGk..",
        "...kGkTTTTkGk...",
        "....kkkTTkkk....",
        ".......kk.......",
    ],
    "WILD": [                      # a live effect: eyes shut with joy, the tongue out further
        "..k..........k..",
        ".kDk........kDk.",
        ".kDRk.HHHH.kRDk.",
        ".kDRGkHHHHkGRDk.",
        ".kGGDDDDDDDDGGk.",
        "CCkGGLDDDDLGGkCC",
        "CHGLkkGDDGkkLGHC",
        "CHGkGGkGGkGGkGHC",
        "CCkGLLLWWLLLGkCC",
        ".kGLWWNNNNWWLGk.",
        ".kLWWWkNNkWWWLk.",
        "..kLWkTTTTkWLk..",
        "..kGLkTTTTkLGk..",
        "...kGkTTTTkGk...",
        "....kkTTTTkk....",
        "......kTTk......",
    ],
    "SLEEP": [
        "..k..........kSz",
        ".kDk........kDk.",
        ".kDRk.HHHH.kRDk.",
        ".kDRGkHHHHkGRDk.",
        ".kGGDDDDDDDDGGk.",
        "CCkGGLDDDDLGGkCC",
        "CHGLGGGDDGGGLGHC",
        "CHGLDDGGGGDDLGHC",
        "CCkGLLLWWLLLGkCC",
        ".kGLWWNNNNWWLGk.",
        ".kLWWWWNNWWWWLk.",
        "..kLWWWkkWWWLk..",
        "..kGLWWWWWWLGk..",
        "...kGLWWWWLGk...",
        "....kkLLLLkk....",
        ".....kkkkkk.....",
    ],
}


def main():
    names = list(ICONS)
    lines = ["/* generated by tools/gen_art.py: KAWA's icons (8 x 8) and the mascot (16 x 16, 4 bits) */",
             "#pragma once", "#include <stdint.h>",
             "enum { " + ", ".join(f"ICO_{n}" for n in names) + ", ICO_N };"]
    rows = []
    for n in names:
        img = ICONS[n]
        assert len(img) == 8 and all(len(r) == 8 for r in img), n
        rows.append("{" + ", ".join("0x%02X" % int(r.replace("#", "1").replace(".", "0"), 2) for r in img) + "}")
    lines.append("static const uint8_t ICONS[ICO_N][8] = {\n    " + ",\n    ".join(rows) + "};")
    def pack4(rows, w):
        out = []
        for r in rows:
            assert len(r) == w, r
            for x in range(0, w, 2):
                out.append(INKS.index(r[x]) | INKS.index(r[x + 1]) << 4)
        return out
    names16 = list(DOG16)
    lines.append("enum { " + ", ".join(f"DOG_{n}" for n in names16) + ", DOG_N };")
    lines.append("static const uint8_t DOG16[DOG_N][128] = {\n    " + ",\n    ".join(
        "{" + ", ".join(map(str, pack4(DOG16[n], 16))) + "}" for n in names16) + "};")
    OUT.write_text("\n".join(lines) + "\n")
    print("->", OUT, len(names), "icons,", len(names16), "dog frames")
    photo()


def photo():
    """the boot picture: 240 x 240, BOOT_COLOURS colours (RGB565), a byte a pixel; and its twinkling stars"""
    from PIL import Image
    im = Image.open(BOOT_PNG).convert("RGB").resize((240, 240), Image.LANCZOS)
    q = im.quantize(colors=BOOT_COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat = q.getpalette()[:3 * BOOT_COLOURS]
    rgb = [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]
    rgb += [(0, 0, 0)] * (BOOT_COLOURS - len(rgb))
    pal = [(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3) for r, g, b in rgb]
    idx = list(q.get_flattened_data() if hasattr(q, 'get_flattened_data') else q.getdata())
    lum = [0.3 * rgb[i][0] + 0.59 * rgb[i][1] + 0.11 * rgb[i][2] for i in idx]

    def at(x, y):
        return lum[y * 240 + x]
    stars = []                                 # bright and alone in a dark sky, outside the title
    for y in range(4, 120):
        for x in range(4, 236):
            if TITLE_BOX[0] <= x <= TITLE_BOX[2] and TITLE_BOX[1] <= y <= TITLE_BOX[3]:
                continue
            v = at(x, y)
            near = [at(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
            far = [at(x + dx, y + dy) for dx in (-4, 0, 4) for dy in (-4, 0, 4) if dx or dy]
            if v >= max(near) and v - max(far) > 60 and max(far) < 70 and \
                    all(abs(x - a) + abs(y - b) > 12 for a, b in stars):
                stars.append((x, y))
    for x, y in SPARKLES:                      # and a few more in the gaps of the sky
        assert max(at(x + dx, y + dy) for dx in (-3, 0, 3) for dy in (-3, 0, 3)) < 90, ("not on dark sky", x, y)
        stars.append((x, y))
    stars = stars[:8]
    lines = ["/* generated by tools/gen_art.py from art/boot-dog.png (KAWA's dog in 32 bits, by Omnipotard): the boot",
             " * picture, 240 x 240, a byte a pixel into BOOT_PAL (RGB565); BOOT_STAR: stars of its sky that twinkle */",
             "#pragma once", "#include <stdint.h>",
             f"#define BOOT_NSTAR {len(stars)}u",
             "static const uint8_t BOOT_STAR[BOOT_NSTAR][2] = {" + ", ".join(f"{{{x}, {y}}}" for x, y in stars) + "};",
             f"static const uint16_t BOOT_PAL[{BOOT_COLOURS}] = {{" + ", ".join(map(str, pal)) + "};",
             "static const uint8_t BOOT_IMG[240u * 240u] = {"]
    for y in range(240):
        lines.append("    " + ",".join(map(str, idx[y * 240:(y + 1) * 240])) + ",")
    lines.append("};")
    PHOTO_OUT.write_text("\n".join(lines) + "\n")
    print("->", PHOTO_OUT, "240 x 240,", BOOT_COLOURS, "colours,", len(stars), "stars", stars)


if __name__ == "__main__":
    main()
