#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA in a browser: compile the firmware to WebAssembly (clang, wasm32, no libc) and put it into the page.
#   web/make_emulator.sh            -> build/web/kawa.wasm, build/web/kawa-emulator.html (the page's body),
#                                      ../emulator/index.html (a whole page, for GitHub Pages)
# Needs clang with the wasm32 target and wasm-ld (lld); nothing else.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/web ../emulator
clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -Wall -Wno-unused-function \
    -DKAWA_WASM -DAPP_VER="\"${APP_VER:-0.4}\"" -Ifirmware/src -Ifirmware/gen \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=4194304 -Wl,-z,stack-size=262144 \
    -o build/web/kawa.wasm firmware/src/kawa.c
KAWA_VER="${APP_VER:-0.4}" python3 - <<'PY'
import base64
import os
from pathlib import Path
b64 = base64.b64encode(Path("build/web/kawa.wasm").read_bytes()).decode()
body = Path("web/emulator.html").read_text().replace("__KAWA_WASM__", b64).replace("__KAWA_VER__", os.environ["KAWA_VER"])
Path("build/web/kawa-emulator.html").write_text(body)
head, rest = body.split("<style>", 1)
css, content = rest.split("</style>", 1)
page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        + head + "<style>" + css + "</style>\n</head>\n<body>" + content + "\n</body>\n</html>\n")
Path("../emulator/index.html").write_text(page)
print("wasm", Path("build/web/kawa.wasm").stat().st_size, "B; page", len(body), "B")
PY
