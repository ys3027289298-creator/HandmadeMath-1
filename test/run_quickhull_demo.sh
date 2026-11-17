#!/bin/sh
#
# Build and run the Quickhull demo against both language bindings of the
# single-header library. This does not touch the project Makefile or any
# existing test; it only compiles the new quickhull_demo.c translation
# unit with the compilers already used by the repository.
#
# Usage: ./run_quickhull_demo.sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build_quickhull"
mkdir -p "$BUILD_DIR"

CC_BIN="${CC:-cc}"
CXX_BIN="${CXX:-c++}"

echo "=== Building Quickhull demo as C99 ($CC_BIN) ==="
"$CC_BIN" -std=c99 -Wall -Wextra -Wno-missing-braces \
    -I"$SCRIPT_DIR/.." "$SCRIPT_DIR/quickhull_demo.c" -lm \
    -o "$BUILD_DIR/quickhull_demo_c"
"$BUILD_DIR/quickhull_demo_c"

echo
echo "=== Building Quickhull demo as C++98 ($CXX_BIN) ==="
"$CXX_BIN" -std=c++98 -Wall -Wextra -Wno-missing-braces \
    -I"$SCRIPT_DIR/.." -x c++ "$SCRIPT_DIR/quickhull_demo.c" -lm \
    -o "$BUILD_DIR/quickhull_demo_cpp"
"$BUILD_DIR/quickhull_demo_cpp"
