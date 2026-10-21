#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# K-WA SUB's sound under UBSan (signed overflow stops it): tests/run_stress.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
${CC:-gcc} -O1 -g -DKAWA_WASM -DKS_B=${KS_B:-0} -Ifirmware/src -Ifirmware/gen -Wno-attributes -Wno-builtin-declaration-mismatch \
    -fsanitize=signed-integer-overflow,integer-divide-by-zero,bounds -fno-sanitize-recover=all -o build/host/stress tests/stress.c
build/host/stress
