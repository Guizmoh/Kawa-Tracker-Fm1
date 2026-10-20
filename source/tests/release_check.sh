#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The release files agree: install/SHA256SUMS lists what is in install/, and the package at the top of the repository (the one
# people download) is byte for byte the newest one in install/.   sh source/tests/release_check.sh
cd "$(dirname "$0")/../.." || exit 1
fail=0
( cd install && sha256sum -c SHA256SUMS >/dev/null 2>&1 ) || { echo "FAIL  install/SHA256SUMS does not match the files"; fail=1; }
newest=$(ls install/kawa-*.fwsc 2>/dev/null | sort -V | tail -n 1)
[ -n "$newest" ] || { echo "FAIL  no package in install/"; exit 1; }
top="$(basename "$newest")"
if [ -f "$top" ] && cmp -s "$top" "$newest"; then echo "ok    $top at the top of the repository = $newest"
else echo "FAIL  $top is missing at the top of the repository or differs from $newest"; fail=1; fi
[ $fail = 0 ] && echo "release_check: passed"
exit $fail
