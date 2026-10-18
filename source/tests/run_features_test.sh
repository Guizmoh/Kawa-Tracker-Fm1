#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KAWA 0.3's features through the audio interrupt (tests/features_test.c), under UBSan.
#   tests/run_features_test.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/features
cc -O1 -g -Wall -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" \
   -fsanitize=signed-integer-overflow,shift-exponent,bounds -fno-sanitize-recover=all \
   -Ifirmware/src -Ifirmware/gen -o build/features/features_test tests/features_test.c -lm
build/features/features_test
