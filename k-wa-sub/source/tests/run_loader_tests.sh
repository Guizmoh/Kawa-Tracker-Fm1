#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The update path on this computer, against the device build's package (./build.sh first):
#   tests/ota_test.c   step 1 (firmware/src/ota.c): stage the package's loader, the record
#   tests/ldr_test.c   step 2 (firmware/loader/ldr_core.c): another app installed -> this build, with a damaged
#                      package, a sector changed in transfer, a power cut, no "success", an unknown flash chip
# Then again with ASan / UBSan when the compiler has them. Exits non-zero on a failure.
#   tests/run_loader_tests.sh
set -e
cd "$(dirname "$0")/.."
OUT=build/loader-tests
CC="${CC:-cc}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
for f in build/k-wa-sub.fwsc build/ksub.bin build/loader/ota.bin; do
    [ -f "$f" ] || { echo "run_loader_tests.sh: no $f (run ./build.sh first)"; exit 1; }
done
mkdir -p "$OUT"
# the app installed before: KAWA's (../../source/build/kawa.bin, after its ./build.sh), else the first half of this
# one, in a package of its own (tools/fm1pkg_make.py)
if [ -f ../../source/build/kawa.bin ]; then cp ../../source/build/kawa.bin "$OUT/old_app.bin"
else head -c $(($(wc -c < build/ksub.bin) / 2)) build/ksub.bin > "$OUT/old_app.bin"; fi
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null

fail=0
run() {                                         # run NAME CMD...: its output, then a summary line
    name=$1
    shift
    if "$@"; then echo "== $name: passed"; else echo "== $name: FAILED"; fail=1; fi
}
build_and_run() {                               # build_and_run SUFFIX LABEL CFLAGS...
    x=$1
    tag=$2
    shift 2
    $CC "$@" -O1 -Wall -Wextra -Wno-unused-function -o "$OUT/ota_test$x" tests/ota_test.c
    $CC "$@" -O1 -Wall -Wextra -Wno-unused-function -o "$OUT/ldr_test$x" tests/ldr_test.c
    run "step 1, ota.c$tag" "$OUT/ota_test$x" build/k-wa-sub.fwsc
    run "update loader, ldr_core.c: other app -> this build$tag" "$OUT/ldr_test$x" "$OUT/old.fwsc" build/k-wa-sub.fwsc
}
build_and_run "" ""
if echo 'int main(void){return 0;}' | $CC -fsanitize=address,undefined -x c -o "$OUT/san_probe" - 2>/dev/null &&
   "$OUT/san_probe" 2>/dev/null; then
    build_and_run _san " (ASan/UBSan)" -g -fsanitize=address,undefined -fno-sanitize-recover=undefined
else
    echo "== ASan/UBSan: not available with $CC, skipped"
fi
[ "$fail" = 0 ] && echo "loader tests passed" || echo "LOADER TESTS FAILED"
exit "$fail"
