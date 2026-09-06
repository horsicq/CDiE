#!/bin/sh
# ----------------------------------------------------------------------------
# Build cdie with TCC (Tiny C Compiler) on a POSIX host. Produces, in build_tcc/:
#
#   cdie        the console scanner
#   libdie.so   the die_library-compatible shared library
#   libdie.a    the static library
#
# cdie is dependency-free C99, so TCC compiles and links it in one invocation.
#
# Usage:  run/build_tcc.sh      (tcc on PATH, or TCC=/path/to/tcc run/build_tcc.sh)
# ----------------------------------------------------------------------------
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"
OUT="$ROOT/build_tcc"
TCC="${TCC:-tcc}"

if ! command -v "$TCC" >/dev/null 2>&1 && [ ! -x "$TCC" ]; then
    echo "TCC not found. Put tcc on PATH or set TCC to its path." >&2
    exit 1
fi

mkdir -p "$OUT" "$OUT/obj_static"

# Console: every .c under src. Library: the same minus the two entry points
# (utils_entry.c, main_console.c), plus lib/die.c.
#
# src/gui is the Windows GUI front end (its own entry point, and it includes
# windows.h unconditionally on a hosted build), so it is skipped by path here
# exactly as run/build_tcc.cmd and run/build_msvc.cmd already do.
EXE_SOURCES=$(find "$SRC" -name '*.c' -not -path '*/gui/*')
LIB_SOURCES="$(find "$SRC" -name '*.c' -not -path '*/gui/*' ! -name 'utils_entry.c' ! -name 'main_console.c') $ROOT/lib/die.c"

DEF="-DNDEBUG"

echo "[1/3] cdie..."
# shellcheck disable=SC2086
"$TCC" $DEF -I"$SRC" $EXE_SOURCES -o "$OUT/cdie"

echo "[2/3] libdie.so (shared library)..."
# shellcheck disable=SC2086
"$TCC" -shared -DDIE_BUILD_SHARED $DEF -I"$SRC" -I"$ROOT/lib" $LIB_SOURCES -o "$OUT/libdie.so"

echo "[3/3] libdie.a (static library)..."
OBJS=""
for f in $LIB_SOURCES; do
    o="$OUT/obj_static/$(basename "${f%.c}").o"
    "$TCC" -c -DDIE_STATIC $DEF -I"$SRC" -I"$ROOT/lib" "$f" -o "$o"
    OBJS="$OBJS $o"
done
# shellcheck disable=SC2086
"$TCC" -ar rcs "$OUT/libdie.a" $OBJS

echo
echo "Built in $OUT/ :  cdie  libdie.so  libdie.a"
