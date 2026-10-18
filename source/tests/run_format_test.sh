#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA's project format (tests/format_test.c): 0.2 projects load, a format-5 one comes back as it was.
#   tests/run_format_test.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/format
cc -O1 -g -Wall -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -fsanitize=address,undefined -fno-sanitize-recover=all \
   -Ifirmware/src -Ifirmware/gen -o build/format/format_test tests/format_test.c -lm
build/format/format_test
