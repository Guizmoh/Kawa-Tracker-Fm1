#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KAWA's FM sounds (the FM engine, firmware/src/fm6_core.c) -> firmware/gen/kawa_fm.h.

  python3 tools/gen_fm_patches.py

Six-operator patches in the classic 155-byte single-voice layout (six operators with 4-rate / 4-level
envelopes, 32 algorithms, feedback, LFO, pitch envelope), written as readable operator settings.
op(), voice() and the first eight patches come from Felucca's tools/gen_fm6_patches.py (GPL-3.0-only,
Leo Kuroshita, Hügelton Instruments); the rest are KAWA's. No factory data of any instrument.
Operators are listed OP1..OP6; the layout stores OP6 first.
"""
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "kawa_fm.h"


def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=0, fc=1, ff=0, det=7, mode=0, kvs=0, ams=0, rs=0,
       bp=39, ld=0, rd=0, lc=0, rc=0):
    return dict(r=r, l=l, ol=ol, fc=fc, ff=ff, det=det, mode=mode, kvs=kvs, ams=ams, rs=rs, bp=bp, ld=ld, rd=rd,
                lc=lc, rc=rc)


def voice(name, alg, ops, fb=0, oks=1, pr=(99, 99, 99, 99), pl=(50, 50, 50, 50), lfs=35, lfd=0, lpmd=0, lamd=0,
          lks=1, lfw=0, lpms=3, trnsp=24):
    assert len(ops) == 6 and 1 <= alg <= 32 and len(name) <= 10
    v = []
    for o in reversed(ops):
        v += list(o["r"]) + list(o["l"]) + [o["bp"], o["ld"], o["rd"], o["lc"], o["rc"], o["rs"], o["ams"],
                                            o["kvs"], o["ol"], o["mode"], o["fc"], o["ff"], o["det"]]
    v += list(pr) + list(pl) + [alg - 1, fb, oks, lfs, lfd, lpmd, lamd, lks, lfw, lpms, trnsp]
    v += [ord(c) for c in name.ljust(10)]
    assert len(v) == 155
    return v


# (name FR, EN, ES, the voice). Algorithm 5 = three pairs (1<-2, 3<-4, 5<-6 with the feedback), 32 = six carriers.
PATCHES = [
    (("PIANO TINE", "TINE EP", "PIANO TINE"), voice("TINE EP", 5, [
        op(r=(96, 25, 25, 67), l=(99, 75, 0, 0), ol=98, kvs=2, rs=3),
        op(r=(95, 50, 35, 78), l=(99, 75, 0, 0), ol=58, kvs=7, rs=3),
        op(r=(95, 20, 20, 50), l=(99, 95, 0, 0), ol=90, kvs=2, det=8, rs=2),
        op(r=(97, 62, 40, 60), l=(99, 60, 0, 0), ol=68, fc=14, kvs=6, rs=3),
        op(r=(95, 30, 20, 60), l=(99, 90, 0, 0), ol=78, det=6, kvs=1),
        op(r=(95, 40, 30, 60), l=(99, 80, 0, 0), ol=52, kvs=3)],
        fb=3, lfs=34, lfd=33, lfw=4, lpms=2)),
    (("CLOCHE", "BELL", "CAMPANA"), voice("GLASS BELL", 5, [
        op(r=(99, 40, 25, 35), l=(99, 80, 0, 0), ol=96, kvs=1),
        op(r=(99, 35, 25, 35), l=(99, 70, 0, 0), ol=78, fc=3, ff=50, kvs=3),
        op(r=(99, 45, 30, 40), l=(99, 70, 0, 0), ol=84, fc=2, ff=38, det=9, kvs=1),
        op(r=(99, 50, 30, 40), l=(99, 60, 0, 0), ol=70, fc=5, kvs=3),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=78, fc=5, ff=40, det=5),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=60)],
        fb=0, lpms=0)),
    (("BASSE FM", "FM BASS", "BAJO FM"), voice("ROUND BASS", 5, [
        op(r=(99, 40, 30, 75), l=(99, 85, 70, 0), ol=99, kvs=2),
        op(r=(99, 60, 40, 75), l=(99, 72, 55, 0), ol=80, kvs=5),
        op(r=(99, 55, 40, 75), l=(99, 70, 50, 0), ol=80, det=8, kvs=1),
        op(r=(99, 72, 50, 75), l=(99, 65, 40, 0), ol=78, fc=2, kvs=6),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    (("CUIVRES", "BRASS", "METALES"), voice("BRASS SECT", 5, [
        op(r=(65, 50, 40, 60), l=(99, 92, 90, 0), ol=98, kvs=1),
        op(r=(55, 50, 40, 60), l=(99, 88, 85, 0), ol=80, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=92, det=9, kvs=1),
        op(r=(50, 50, 40, 60), l=(99, 88, 85, 0), ol=78, kvs=3),
        op(r=(62, 50, 40, 60), l=(99, 92, 90, 0), ol=88, det=5, kvs=1),
        op(r=(52, 50, 40, 60), l=(99, 85, 82, 0), ol=72, kvs=2)],
        fb=5, pr=(80, 60, 99, 60), pl=(46, 50, 50, 50), lfs=33, lfd=50, lpmd=6, lfw=4, lpms=3)),
    (("NAPPE", "PAD", "COLCHON"), voice("SOFT PAD", 5, [
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=5),
        op(r=(35, 30, 40, 40), l=(85, 90, 80, 0), ol=60, kvs=1),
        op(r=(40, 30, 40, 40), l=(99, 95, 90, 0), ol=92, det=10),
        op(r=(30, 25, 40, 40), l=(80, 90, 85, 0), ol=55, fc=2, kvs=1),
        op(r=(38, 30, 40, 40), l=(99, 95, 90, 0), ol=80, fc=2),
        op(r=(35, 30, 40, 40), l=(85, 90, 85, 0), ol=50)],
        fb=2, lfs=30, lfd=60, lpmd=4, lfw=4, lpms=2)),
    (("MARIMBA", "MARIMBA", "MARIMBA"), voice("WOOD BARS", 5, [
        op(r=(99, 52, 40, 55), l=(99, 0, 0, 0), ol=99, kvs=2, rs=2),
        op(r=(99, 75, 50, 60), l=(99, 0, 0, 0), ol=70, fc=4, kvs=5, rs=3),
        op(r=(99, 70, 50, 60), l=(99, 0, 0, 0), ol=72, fc=4, kvs=3, rs=3),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=45, kvs=3, rs=3),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    (("ORGUE", "ORGAN", "ORGANO"), voice("DRAWBARS", 32, [
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=90),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=84, fc=0),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=86, fc=2),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=76, fc=3),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=78, fc=4),
        op(r=(99, 99, 99, 85), l=(99, 99, 99, 0), ol=72, fc=1, ff=50)],
        fb=0, lfs=60, lpmd=3, lfw=4, lpms=3)),
    (("GUITARE", "PLUCK", "PUNTEO"), voice("NYLON PICK", 5, [
        op(r=(99, 45, 30, 60), l=(99, 60, 0, 0), ol=98, kvs=2, rs=2),
        op(r=(99, 70, 40, 60), l=(99, 40, 0, 0), ol=76, fc=3, kvs=4, rs=2),
        op(r=(99, 60, 40, 60), l=(99, 30, 0, 0), ol=74, fc=2, det=8, kvs=2, rs=2),
        op(r=(99, 75, 40, 60), l=(99, 30, 0, 0), ol=68, kvs=3, rs=2),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    # KAWA's own
    (("LEAD CHIP", "CHIP LEAD", "LEAD CHIP"), voice("CHIP LEAD", 5, [
        op(r=(99, 50, 50, 70), l=(99, 92, 88, 0), ol=96),
        op(r=(99, 60, 50, 70), l=(99, 80, 75, 0), ol=82, fc=1, kvs=1),
        op(r=(99, 50, 50, 70), l=(99, 92, 88, 0), ol=80, det=9),
        op(r=(99, 60, 50, 70), l=(99, 85, 80, 0), ol=70, fc=2),
        op(ol=0),
        op(r=(99, 50, 50, 70), l=(99, 90, 85, 0), ol=40)],
        fb=7, lfs=36, lfd=40, lpmd=5, lfw=4, lpms=3)),
    (("BASSE ACIDE", "ACID BASS", "BAJO ACIDO"), voice("ACID BASS", 5, [
        op(r=(99, 45, 35, 80), l=(99, 80, 60, 0), ol=99),
        op(r=(99, 70, 30, 80), l=(99, 50, 0, 0), ol=86, kvs=5),
        op(r=(99, 55, 40, 80), l=(99, 75, 55, 0), ol=72, fc=0),
        op(r=(99, 80, 30, 80), l=(99, 40, 0, 0), ol=80, fc=1, kvs=4),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0, trnsp=12)),
    (("HOOVER", "HOOVER", "HOOVER"), voice("HOOVER", 5, [
        op(r=(80, 40, 40, 55), l=(99, 95, 92, 0), ol=96, det=3),
        op(r=(80, 40, 40, 55), l=(99, 90, 88, 0), ol=88, fc=1, det=11),
        op(r=(80, 40, 40, 55), l=(99, 95, 92, 0), ol=94, det=11),
        op(r=(80, 40, 40, 55), l=(99, 90, 88, 0), ol=86, fc=1, det=3),
        op(r=(80, 40, 40, 55), l=(99, 95, 92, 0), ol=80, fc=0, det=7),
        op(r=(80, 40, 40, 55), l=(99, 90, 88, 0), ol=84, fc=1)],
        fb=6, pr=(70, 50, 99, 60), pl=(40, 50, 50, 50), lfs=40, lfd=0, lpmd=8, lfw=0, lpms=4)),
    (("KALIMBA", "KALIMBA", "KALIMBA"), voice("KALIMBA", 5, [
        op(r=(99, 55, 35, 50), l=(99, 0, 0, 0), ol=99, kvs=2, rs=3),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=72, fc=6, kvs=4, rs=3),
        op(r=(99, 60, 40, 50), l=(99, 0, 0, 0), ol=60, fc=3, kvs=2, rs=3),
        op(r=(99, 85, 50, 60), l=(99, 0, 0, 0), ol=60, fc=8, kvs=3, rs=3),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
]


def main():
    L = ["/* generated by tools/gen_fm_patches.py: KAWA's FM sounds, 155-byte voices (the first eight are Felucca's) */",
         "#pragma once", "#include <stdint.h>", f"#define FM_NPATCH {len(PATCHES)}u",
         f"static const uint8_t FM_PATCH[FM_NPATCH][156] = {{"]
    for _, v in PATCHES:
        L.append("    {" + ", ".join(map(str, v)) + ", 0},")
    L.append("};")
    L.append("static const char *const FM_PATCH_NAME[3][FM_NPATCH] = {")
    for lang in range(3):
        L.append("    {" + ", ".join(f'"{n[lang]}"' for n, _ in PATCHES) + "},")
    L.append("};")
    OUT.write_text("\n".join(L) + "\n")
    print("->", OUT, len(PATCHES), "patches")


if __name__ == "__main__":
    main()
