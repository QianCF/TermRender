#!/usr/bin/env bash
#
# TermRender - Linux build (g++)
#
#     ./build.sh            release build
#     ./build.sh --debug    debug build (no optimisation, symbols)
#
# Produces out/termrender
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
DEPS="$ROOT/deps"
OUT="$ROOT/out"

MODE="release"
if [ "${1:-}" = "--debug" ]; then MODE="debug"; fi

mkdir -p "$OUT"

if [ "$MODE" = "debug" ]; then
	OPT=(-O0 -g)
else
	OPT=(-O2 -DNDEBUG)
fi

echo "==> compiling TermRender (g++, $MODE)"
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter \
    -I"$SRC" -I"$DEPS" \
    "${OPT[@]}" \
    "$SRC"/*.cpp \
    -o "$OUT/termrender" \
    -pthread

echo "==> built: $OUT/termrender"
