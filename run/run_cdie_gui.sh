#!/bin/bash
# Build the cdie GUI (xxwidgets front end) with gcc directly (no CMake for
# the app itself). Produces:  build_linux/cdie_gui
#
# Every .c under src except the console entry points is compiled in,
# including src/gui, minus the Win32-only files listed below.
# Dependencies (dep/xxwidgets, dep/xxfclib) are built with CMake into
# dep/<name>/build_linux if no prebuilt .a is found there.
#
# Usage:  run/build_linux_gui.sh
# Overrides (optional):
#   CC=clang  XXW_LIB=/path/libxxwidgets.a  XXF_LIB=/path/libdie_engine.a
#   CDIS_LIB=/path/libcdisasm.a  REBUILD_DEPS=1

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SRC="$ROOT/src"
GUI="$SRC/gui"
DEP="$ROOT/dep"
OUT="$ROOT/build_linux"
CC="${CC:-gcc}"

command -v "$CC" >/dev/null 2>&1 || { echo "ERROR: $CC not found."; exit 1; }
PKGS="gtk+-3.0 ayatana-appindicator3-0.1 libnotify"
command -v pkg-config >/dev/null 2>&1 && pkg-config --exists $PKGS || {
    echo "ERROR: GTK 3 / appindicator / libnotify development files not found."
    echo "  Debian/Ubuntu: sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev libayatana-appindicator3-dev libnotify-dev"
    exit 1
}

XXW_INC="$DEP/xxwidgets/include"
XXF_INC="$DEP/xxfclib/include"
CDIS_INC="$DEP/cdisasm/include"
[ -f "$XXW_INC/xxwidgets/xxwidgets.h" ] || { echo "ERROR: missing $XXW_INC/xxwidgets/xxwidgets.h"; exit 1; }
[ -f "$XXF_INC/xxfclib/die_engine/die_engine.h" ] || { echo "ERROR: missing die_engine.h under $XXF_INC"; exit 1; }

# --- find_lib <search root> <pattern> : first match, searching build_linux only
find_lib() { find "$1/build_linux" -type f -name "$2" 2>/dev/null | head -n 1; }

# --- build_dep <source dir> [target]
build_dep() {
    [ -f "$1/CMakeLists.txt" ] || { echo "ERROR: no CMakeLists.txt in $1"; return 1; }
    command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found."; return 1; }
    GEN=; command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
    TGT=; [ -n "$2" ] && TGT="--target $2"
    echo "Building $1 ..."
    # shellcheck disable=SC2086
    cmake -S "$1" -B "$1/build_linux" $GEN -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DXXWIDGETS_BUILD_EXAMPLES=OFF -DXXWIDGETS_BUILD_TESTS=OFF \
        -DXXWIDGETS_BUILD_SETTINGS=OFF -DBUILD_TESTING=OFF
    # shellcheck disable=SC2086
    cmake --build "$1/build_linux" $TGT -j "$(nproc 2>/dev/null || echo 2)"
}

if [ -n "$REBUILD_DEPS" ]; then
    XXW_LIB=; XXF_LIB=
    build_dep "$DEP/xxwidgets" xxwidgets
    build_dep "$DEP/xxfclib"
fi

if [ -z "$XXW_LIB" ]; then XXW_LIB=$(find_lib "$DEP/xxwidgets" 'libxxwidgets*.a'); fi
if [ -z "$XXW_LIB" ]; then
    echo "xxwidgets lib not found - building it..."
    build_dep "$DEP/xxwidgets" xxwidgets
    XXW_LIB=$(find_lib "$DEP/xxwidgets" 'libxxwidgets*.a')
fi
[ -n "$XXW_LIB" ] || { echo "ERROR: still no libxxwidgets*.a under $DEP/xxwidgets/build_linux"; exit 1; }

if [ -z "$XXF_LIB" ]; then XXF_LIB=$(find_lib "$DEP/xxfclib" '*die_engine*.a'); fi
if [ -z "$XXF_LIB" ]; then
    echo "die_engine lib not found - building xxfclib..."
    build_dep "$DEP/xxfclib"
    XXF_LIB=$(find_lib "$DEP/xxfclib" '*die_engine*.a')
fi
[ -n "$XXF_LIB" ] || { echo "ERROR: still no *die_engine*.a under $DEP/xxfclib/build_linux"; exit 1; }

if [ -z "$CDIS_LIB" ]; then CDIS_LIB=$(find "$DEP" -path '*/build_linux/*' -name '*cdisasm*.a' 2>/dev/null | head -n 1); fi
echo "Using xxwidgets lib : $XXW_LIB"
echo "Using die_engine lib: $XXF_LIB"
if [ -n "$CDIS_LIB" ]; then echo "Using cdisasm lib   : $CDIS_LIB"
else echo "WARNING: no *cdisasm*.a found - link may fail with cdisasm_x86_* unresolved."; fi

mkdir -p "$OUT/obj_gui"

# --- Sources: engine (minus entry points and gui dir) + gui (minus Win32-only)
SOURCES=()
while IFS= read -r f; do
    case "$f" in
        "$GUI"/*) continue ;;
        "$SRC"/installer/*) continue ;;
        */utils_entry.c|*/main_console.c) continue ;;
    esac
    SOURCES+=("$f")
done < <(find "$SRC" -type f -name '*.c' | sort)

while IFS= read -r f; do
    case "$f" in
	*/main_gui.c|*/platform_win32.c|*/tray_win32.c|*/versions.c) continue ;;
    esac
    SOURCES+=("$f")
done < <(find "$GUI" -type f -name '*.c' | sort)

echo "[1/1] cdie_gui ($CC, ${#SOURCES[@]} sources)..."
# shellcheck disable=SC2046
"$CC" -O2 -Wall -DNDEBUG -DXXWIDGETS_STATIC -DXXFC_STATIC \
    $(pkg-config --cflags $PKGS) \
    -I"$SRC" -I"$GUI" -I"$XXW_INC" -I"$XXF_INC" -I"$CDIS_INC" \
    "${SOURCES[@]}" \
    -o "$OUT/cdie_gui" \
    "$XXW_LIB" "$XXF_LIB" ${CDIS_LIB:+"$CDIS_LIB"} \
       $(pkg-config --libs $PKGS) -lm -lpthread -ldl

echo
echo "Built: $OUT/cdie_gui"
./build_linux/cdie_gui