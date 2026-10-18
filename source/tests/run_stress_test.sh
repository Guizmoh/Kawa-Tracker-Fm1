#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA's audio at its limits (tests/stress_test.c) under UBSan: the run stops at the first integer that wraps
# (a left shift of a negative number is left out: the msfa core does it, and every compiler of ours does it right).
#   tests/run_stress_test.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/stress
cc -O1 -g -Wall -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -fsanitize=signed-integer-overflow,shift-exponent,bounds -fno-sanitize-recover=all \
   -Ifirmware/src -Ifirmware/gen -o build/stress/stress_test tests/stress_test.c -lm
build/stress/stress_test
