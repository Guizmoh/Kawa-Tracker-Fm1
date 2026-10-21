#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# K-WA SUB in a browser: the firmware compiled to WebAssembly (clang, wasm32, no libc), put into the page.
#   web/make_emulator.sh            -> build/web/ksub.wasm, ../emulator/index.html (one file, works offline)
#   KS_B=1 web/make_emulator.sh     version B (four engines) -> build/web/ksub-b.wasm, ../emulator/b.html
# Needs clang with the wasm32 target and wasm-ld (lld); nothing else.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/web ../emulator
SFX=""; [ "${KS_B:-0}" = 1 ] && SFX="-b"
clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -Wno-sign-compare -DKAWA_WASM -DKS_B=${KS_B:-0} -DAPP_VER="\"${APP_VER:-0.2}${SFX}\"" -Ifirmware/src -Ifirmware/gen \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=4194304 -Wl,-z,stack-size=262144 \
    -o build/web/ksub$SFX.wasm firmware/src/ksub.c
KSUB_VER="${APP_VER:-0.2}${SFX:+-B}" SFX="$SFX" python3 - <<'PY'
import base64, os
from pathlib import Path
b64 = base64.b64encode(Path(f"build/web/ksub{os.environ['SFX']}.wasm").read_bytes()).decode()
body = Path("web/emulator.html").read_text().replace("__KSUB_WASM__", b64).replace("__KSUB_VER__", os.environ["KSUB_VER"])
Path(f"build/web/ksub-emulator{os.environ['SFX']}.html").write_text(body)          # the page's content alone (to embed or publish)
head, rest = body.split("</style>", 1)
page = ('<!doctype html>\n<html lang="fr">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        + head + "</style>\n</head>\n<body>" + rest + "</body>\n</html>\n")
Path("../emulator/" + ("b.html" if os.environ["SFX"] else "index.html")).write_text(page)
print("wasm", Path(f"build/web/ksub{os.environ['SFX']}.wasm").stat().st_size, "B; page", len(page), "B")
PY
