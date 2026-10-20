#!/usr/bin/env bash
set -euo pipefail
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT=${OUT_DIR:-"$HERE/out"}
mkdir -p "$OUT"
CXX=${CXX:-g++}
FLAGS=(-std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -pthread)
if [[ ${SANITIZE:-0} == 1 ]]; then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$CXX" --version > "$OUT/compiler.txt"
for target in correction_range_tests product_pure_tests; do
  SOURCE="$ROOT/tests/$target.cpp"
  if [[ $target == product_pure_tests ]]; then SOURCE="$HERE/product_pure_tests.cpp"; fi
  "$CXX" "${FLAGS[@]}" -I"$ROOT/src" "$SOURCE" "$ROOT/src/display_adapter.cpp" -o "$OUT/$target" 2> "$OUT/$target.compile.log"
  "$OUT/$target" 2>&1 | tee "$OUT/$target.results.log"
done
