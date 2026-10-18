#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA's drum kits on this computer (tests/kits_test.c): every drum of the 8 kits measured and compared,
# build/kits/kit_<n>.wav and build/kits/beat_<n>.wav written. Exits non-zero on a failure.
#   tests/run_kits_test.sh [extra cc flags, e.g. -DKT_TODAY]
set -e
cd "$(dirname "$0")/.."
mkdir -p build/kits
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -Ifirmware/src -Ifirmware/gen "$@" -o build/kits/kits_test tests/kits_test.c -lm
build/kits/kits_test
