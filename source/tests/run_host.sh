#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run KAWA on this computer (tests/host.c): build/host/kawa.wav and the screens as PNG.
#   tests/run_host.sh [script]        (default: tests/tour.txt)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -Ifirmware/src -Ifirmware/gen -o build/host/kawa firmware/src/kawa.c
rm -f build/host/*.ppm build/host/*.png
build/host/kawa build/host/kawa "${1:-tests/tour2.txt}"
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("kawa-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
PY
ls build/host/*.png | wc -l | xargs echo "screens:"
