#!/usr/bin/env bash
#
# Build and run the polygon-clipping tests. Does not touch the repository
# Makefile: everything is built in this directory's build/ folder.
#
# Usage: ./run_clip_test.sh [timing_iterations]
set -euo pipefail

cd "$(dirname "$0")"

BUILD=build
mkdir -p "$BUILD"

WARN="-g -Wall -Wextra -pedantic -Wno-missing-braces -Wno-missing-field-initializers -Wfloat-equal"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
ITERS="${1:-200000}"

echo "== C99 build =="
$CC $WARN -std=c99 -I../.. clip_test.c -lm -o "$BUILD/clip_test_c99"
"$BUILD/clip_test_c99" "$ITERS"

echo
echo "== C11 build =="
$CC $WARN -std=c11 -I../.. clip_test.c -lm -o "$BUILD/clip_test_c11"
"$BUILD/clip_test_c11" "$ITERS"

echo
echo "== C++98 build =="
$CXX $WARN -std=c++98 -I../.. clip_test.c -lm -o "$BUILD/clip_test_cpp98"
"$BUILD/clip_test_cpp98" "$ITERS"

echo
echo "== C++17 build =="
$CXX $WARN -std=c++17 -I../.. clip_test.c -lm -o "$BUILD/clip_test_cpp17"
"$BUILD/clip_test_cpp17" "$ITERS"

echo
echo "== C11 + AddressSanitizer + UndefinedBehaviorSanitizer =="
$CC $WARN -std=c11 -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I../.. clip_test.c -lm -o "$BUILD/clip_test_asan"
"$BUILD/clip_test_asan" "$ITERS"

echo
echo "all clip test builds passed"
