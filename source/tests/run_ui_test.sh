#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA 0.5's panel (tests/ui_test.c): the pages, SELECT, the protected sounds, SENDS, the holds, the tools, the metronome, 16/32/64 steps.
#   tests/run_ui_test.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/ui
cc -O1 -g -Wall -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -fsanitize=signed-integer-overflow,shift-exponent,bounds -fno-sanitize-recover=all \
   -Ifirmware/src -Ifirmware/gen -o build/ui/ui_test tests/ui_test.c -lm
build/ui/ui_test
