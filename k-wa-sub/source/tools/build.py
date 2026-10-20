#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build K-WA SUB (KAWA's build, on Dinghy / Felucca / SLOOP's platform): the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]]

Outputs in build/: ksub.bin (app), loader/ota.bin (update loader),
k-wa-sub.fwsc (package). See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
LOADER_RAM = 0x14000                # loader.ld: the RAM the SPL loads the loader into (also ota.c's own-loader check)
# The most of an ota.bin the stock FM-1 firmware stages and reads back intact: 19456 B (fm-1-re
# docs/io/11-ota-protocol.md, docs/ota-loader-shrinking.md). A first install from the stock firmware goes
# through it. Checked on the whole ota.bin (its dlen is 32 B less). Not padded to 512 B: Felucca's is not either
# (the 512 B rule was seen only when forcing a replacement of the stock loader).
OTA_STOCK_MAX = 0x4C00
# The flash map (KAWA, on Felucca's): head [0, 0x4000) never written by an update; the app area up to 0x93000
# (all the loader writes); KAWA's store 0x97000..0xE0000 (4 project slots of 0x9000, 4 user samples of 0x9400
# from 0xBB000; the loader never erases there); the update staging 0xE0000..0xE5000 (ota.c); settings 0xFC000.
FL_HEAD = (0, 0x4000)
FL_STAGING = (0xE0000, 0xE5000)
FL_SETTINGS = (0xFC000, 0xFD000)
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

# Package identity (the .fwsc marker, the handshake answer). The installers want FM-1_9NN; K-WA SUB takes 97Y (KAWA: 95Y / 96Y):
#   no --release   FM-1_950 (firmware/src/kawa.c's default too, for a build outside build.py)
#   --release X.Y  FM-1_9(5+X)Y: 0.Y -> 95Y (0.2 -> FM-1_952), 1.Y -> 96Y (1.0 -> FM-1_960): majors 0 and 1
#                  never share one. X.Y.Z and X.Y-suffix keep X.Y's (the full version is in the text).
# Taken by others: Felucca 900 and 9XY for its X.Y (v1.5.1: 915), Dinghy 99N. A major 2 (97N) is refused:
# it needs a new scheme first.
PRODUCT = "FM-1_970"
VERSION = None                      # APP_VER for release builds


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src"]
    *_, dm = tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
                    ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"),
                    ("cc", *flags, "-E", "-dM", src / "loader.c"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > LOADER_RAM:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota, image, dm


# ---- app

def build_app():
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen", f'-DFELUCCA_ID="{PRODUCT}"', f'-DAPP_VER="{VERSION or "0.0"}"']
    cmds = [("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
            ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
            ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
            ("cc", *flags, "-c", FW / "src" / "ksub.c", "-o", OUT / "ksub.o"),
            ("cc", *flags, "-E", "-dM", FW / "src" / "ksub.c")]
    dm = tc_all(*cmds)[-1]
    elf = OUT / "ksub.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o",
       OUT / "fm1_isr.o", OUT / "ksub.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "ksub.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "ksub.bin").write_bytes(img)
    return bytes(img), syms, dis, rt, dm


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    ram = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - 0x01C20000
    notes.append(f"image {len(img)} B of {APP_SLOT}; RAM .data+.bss {ram} B of 98304; pool {pool} B of 344064")
    if ram > 96 * 1024:
        errors.append(f"RAM .data+.bss {ram} B > 96 KiB")
    return errors, notes


def cdefs(dm):
    """a cc -E -dM listing -> {macro name: body}"""
    return {m[0]: m[1].strip() for m in re.findall(r"^#define (\w+)(?:\([^)]*\))?(?: (.*))?$", dm, re.M)}


def cval(defs, expr, depth=0):
    """an integer macro of a -dM listing: literals, macro names and + only"""
    v = 0
    for t in expr.replace("(", " ").replace(")", " ").split("+"):
        t = t.strip()
        if re.fullmatch(r"(?:0x[0-9a-fA-F]+|[1-9][0-9]*|0)[uUlL]*", t):
            v += int(t.rstrip("uUlL"), 0)
        elif t in defs and depth < 8:
            v += cval(defs, defs[t], depth + 1)
        else:
            raise SystemExit(f"build: cannot read {expr!r} as a flash offset")
    return v


def windows(defs):
    """FL_RANGE_OK's [lo, hi) windows: where the RAM flash driver may erase / program"""
    body = defs.get("FL_RANGE_OK", "")
    w = re.findall(r"FL_IN\(\s*off\s*,\s*n\s*,\s*([^,()]+?)\s*,\s*([^,()]+?)\s*\)", body)
    if not w or len(w) != body.count("FL_IN("):
        raise SystemExit(f"build: FL_RANGE_OK {body!r} is not a list of FL_IN windows")
    return [(cval(defs, lo), cval(defs, hi)) for lo, hi in w]


def app_area_end(flash):
    """where the app area fm1pkg_make wrote ends in flash: the app_area_head block, then the cfg directory"""
    def size(off):                  # size field of the JLFS entry at app area offset off (SFC, 32-byte blocks)
        a = off & ~31
        b = bytearray(flash[0x4000 + a:0x4000 + a + 64])
        fm1pkg_make.sfc(b, 0, len(b), -a, fm1pkg_make.KEY)
        e = bytes(b[off - a:off - a + 32])
        if struct.unpack_from("<H", e)[0] != fm1pkg_make.crc16(e[2:]):
            raise SystemExit(f"build: no JLFS entry at app area offset {off:#x}")
        return struct.unpack_from("<I", e, 8)[0]
    blk = size(0)
    return 0x4000 + blk + size(blk)


def flash_check(img, flash, ota, image, dm_app, dm_ldr):
    """the flash map the app, the loader and the package must agree on"""
    errors, notes = [], []
    app, ldr = cdefs(dm_app), cdefs(dm_ldr)

    def h(r):
        return f"{r[0]:#x}..{r[1]:#x}"

    def meets(a, b):
        return a[0] < b[1] and b[0] < a[1]
    app_hi, keep = cval(ldr, "LDR_APP_HI"), (cval(ldr, "LDR_DATA_LO"), cval(ldr, "LDR_DATA_HI"))
    # the app: all of it in the area the loader writes
    end, app_end = app_area_end(flash), 0x4000 + 0x120 + len(img)
    if len(flash) != app_hi or app_end > app_hi or end > app_hi:
        errors.append(f"app.bin ends at {app_end:#x}, the app area at {end:#x} in flash (flash.bin {len(flash):#x} B): "
                      f"the loader writes up to {app_hi:#x} only")
    else:
        notes.append(f"app.bin ends at {app_end:#x}, the app area at {end:#x} in flash (the loader writes to {app_hi:#x})")
    # the store: clear of the app, the staging, the settings, and where the loader never erases
    store = (cval(app, "STORE_LO"), cval(app, "STORE_HI"))
    why = [w for w, bad in (("empty or not in 4 KiB sectors", store[0] >= store[1] or (store[0] | store[1]) & 0xFFF),
                            (f"meets the update staging {h(FL_STAGING)}", meets(store, FL_STAGING)),
                            (f"meets the settings {h(FL_SETTINGS)}", meets(store, FL_SETTINGS)),
                            (f"meets the app area (to {app_hi:#x})", store[0] < app_hi),
                            (f"is not inside {h(keep)}, where the loader never erases", not keep[0] <= store[0] <= store[1] <= keep[1]))
           if bad]
    if why:
        errors.append(f"store {h(store)} " + ", ".join(why))
    else:
        notes.append(f"store {h(store)}: clear of the staging {h(FL_STAGING)} and the settings {h(FL_SETTINGS)}, "
                     "the loader leaves it alone")
    # where the app and the loader may erase / program
    aw, lw = windows(app), windows(ldr)
    bad = [f"app {h(w)}" for w in aw if w[0] < app_hi]
    bad += [f"loader {h(w)}" for w in lw if meets(w, FL_HEAD) or meets(w, store) or meets(w, FL_SETTINGS)]
    if bad:
        errors.append(f"flash windows {', '.join(bad)}: the head, the app, the store or the settings in reach")
    else:
        notes.append(f"flash windows: app {', '.join(h(w) for w in aw)}; loader {', '.join(h(w) for w in lw)}")
    # the loader: it fits its RAM and the stock firmware's staging
    if len(image) > LOADER_RAM or len(ota) > OTA_STOCK_MAX:
        errors.append(f"loader image {len(image)} B (RAM {LOADER_RAM}), ota.bin {len(ota)} B: above the stock "
                      f"firmware's staging limit of {OTA_STOCK_MAX} B")
    else:
        notes.append(f"loader image {len(image)} B of {LOADER_RAM}, ota.bin {len(ota)} B of {OTA_STOCK_MAX} "
                     "(the stock firmware's staging limit)")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global PRODUCT, VERSION
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_95Y (0.Y) / FM-1_96Y (1.Y), "
                    "version string X.Y")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    name = "k-wa-sub.fwsc"
    if a.release:                   # one digit each: the identity has room for two (X.Y; a .Z only in the text)
        m = re.fullmatch(r"(\d)\.(\d)(\.\d)?(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y, X.Y.Z or X.Y-suffix, one digit each")
        if int(m[1]) > 0:
            raise SystemExit(f"--release {a.release}: K-WA SUB's identities cover major 0 (FM-1_97Y); "
                             "a major 2 needs a new scheme first (tools/build.py, PRODUCT)")
        PRODUCT = f"FM-1_97{m[2]}"
        VERSION = m[1] + "." + m[2] + (m[3] or "")
        name = f"k-wa-sub-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK, or with another one
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            raise SystemExit(f"build: SDK file {rel} is not the one of AC79NN_SDK_V1.2.1_2023-12-13 (SHA-256 "
                             f"{sha[:16]}...): the package would carry an untested SPL / config. Check out that tag "
                             "in AC79_SDK (BUILDING.md)")
    OUT.mkdir(parents=True, exist_ok=True)
    ota, image, dm_ldr = build_loader()
    img, syms, dis, rt, dm_app = build_app()
    flash = fm1pkg_make.flash_image(img, fm1pkg_make.KEY)
    errors, notes = check(img, syms, dis, rt)
    fe, fn = flash_check(img, flash, ota, image, dm_app, dm_ldr)
    errors += fe
    notes += fn
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    pkg = fm1pkg_make.ufw(flash, ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    print(f"app      {OUT / 'ksub.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
