#!/bin/bash
# Build the cdie GUI (xxwidgets front end) on macOS with clang directly (no
# CMake for the app itself). Produces:  build_macos/cdie_gui
#
# Every .c / .mm under src except the console entry points is compiled in,
# including src/gui, minus the Win32 / GTK / Linux-only files listed below.
# Dependencies (dep/xxwidgets, dep/xxfclib) are built with CMake into
# dep/<name>/build_macos if no prebuilt .a is found there.
#
# Usage:  run/build_macos_gui.sh
# Overrides (optional):
#   CC=clang  ARCH=arm64|x86_64  XXW_LIB=/path/libxxwidgets.a
#   XXF_LIB=/path/libdie_engine.a  CDIS_LIB=/path/libcdisasm.a
#   REBUILD_DEPS=1  NO_RUN=1  MIN_MACOS=11.0
#   NO_NINJA=1 (use Unix Makefiles for the dep builds)  JOBS=2 (parallel jobs)
#   EXTRA_CMAKE_ARGS="-DXXFC_CDISASM_DIR=/path/to/cdisasm"   (extra flags for the dep builds)

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SRC="$ROOT/src"
GUI="$SRC/gui"
DEP="$ROOT/dep"
OUT="$ROOT/build_macos"
CC="${CC:-clang}"
ARCH="${ARCH:-$(uname -m)}"          # arm64 or x86_64
MIN_MACOS="${MIN_MACOS:-11.0}"

[ "$(uname -s)" = "Darwin" ] || { echo "ERROR: this script is for macOS."; exit 1; }
command -v "$CC" >/dev/null 2>&1 || {
    echo "ERROR: $CC not found. Install the Xcode command line tools: xcode-select --install"
    exit 1
}
case "$ARCH" in
    arm64|x86_64) ;;
    *) echo "ERROR: ARCH must be arm64 or x86_64 (got '$ARCH')."; exit 1 ;;
esac

XXW_INC="$DEP/xxwidgets/include"
XXF_INC="$DEP/xxfclib/include"
[ -f "$XXW_INC/xxwidgets/xxwidgets.h" ] || { echo "ERROR: missing $XXW_INC/xxwidgets/xxwidgets.h"; exit 1; }
[ -f "$XXF_INC/xxfclib/die_engine/die_engine.h" ] || { echo "ERROR: missing die_engine.h under $XXF_INC"; exit 1; }

# --- find_lib <search root> <pattern> : first match, searching build_macos only
find_lib() { find "$1/build_macos" -type f -name "$2" 2>/dev/null | head -n 1; }

# --- build_dep <source dir> [target]
build_dep() {
    [ -f "$1/CMakeLists.txt" ] || { echo "ERROR: no CMakeLists.txt in $1"; return 1; }
    command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found (brew install cmake ninja)."; return 1; }
    GEN=; [ -z "$NO_NINJA" ] && command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
    TGT=; [ -n "$2" ] && TGT="--target $2"
    echo "Building $1 ..."
    # shellcheck disable=SC2086
    cmake -S "$1" -B "$1/build_macos" $GEN -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DXXWIDGETS_BUILD_EXAMPLES=OFF -DXXWIDGETS_BUILD_TESTS=OFF \
        -DXXWIDGETS_BUILD_SETTINGS=OFF -DBUILD_TESTING=OFF $EXTRA_CMAKE_ARGS
    # shellcheck disable=SC2086
    cmake --build "$1/build_macos" $TGT -j "${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 2)}"
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
[ -n "$XXW_LIB" ] || { echo "ERROR: still no libxxwidgets*.a under $DEP/xxwidgets/build_macos"; exit 1; }

if [ -z "$XXF_LIB" ]; then XXF_LIB=$(find_lib "$DEP/xxfclib" '*die_engine*.a'); fi
if [ -z "$XXF_LIB" ]; then
    echo "die_engine lib not found - building xxfclib..."
    build_dep "$DEP/xxfclib"
    XXF_LIB=$(find_lib "$DEP/xxfclib" '*die_engine*.a')
fi
[ -n "$XXF_LIB" ] || { echo "ERROR: still no *die_engine*.a under $DEP/xxfclib/build_macos"; exit 1; }

if [ -z "$CDIS_LIB" ]; then CDIS_LIB=$(find "$DEP" -path '*/build_macos/*' -name '*cdisasm*.a' 2>/dev/null | head -n 1); fi
echo "Architecture        : $ARCH (min macOS $MIN_MACOS)"
echo "Using xxwidgets lib : $XXW_LIB"
echo "Using die_engine lib: $XXF_LIB"
if [ -n "$CDIS_LIB" ]; then echo "Using cdisasm lib   : $CDIS_LIB"
else echo "WARNING: no *cdisasm*.a found - link may fail with cdisasm_x86_* unresolved."; fi

mkdir -p "$OUT"

# --- Sources: engine (minus entry points and gui dir) + gui (minus other platforms)
SOURCES=()
while IFS= read -r f; do
    case "$f" in
        "$GUI"/*) continue ;;
        */utils_entry.c|*/main_console.c) continue ;;
        "$SRC"/installer/*) continue ;;                   # Windows installer project, not part of the GUI
    esac
    SOURCES+=("$f")
done < <(find "$SRC" -type f -name '*.c' | sort)

while IFS= read -r f; do
    case "$f" in
        */main_gui.c) continue ;;                          # Windows entry (main_gui_mac.mm replaces it)
        *_win32.c|*_win32.cpp|*_win32.mm) continue ;;      # Win32-only
        *_gtk.c|*_gtk*.c|*_linux.c|*_linux*.c) continue ;; # GTK / Linux-only
        */main_gui_linux.c|*/main_gui_gtk.c) continue ;;
    esac
    SOURCES+=("$f")
done < <(find "$GUI" -type f \( -name '*.c' -o -name '*.mm' -o -name '*.m' \) | sort)

echo "[1/2] compiling ${#SOURCES[@]} sources ($CC, $ARCH)..."
CFLAGS=(-O2 -Wall -DNDEBUG -DXXWIDGETS_STATIC -DXXFC_STATIC
    -arch "$ARCH" -mmacosx-version-min="$MIN_MACOS" -fobjc-arc
    -I"$SRC" -I"$GUI" -I"$XXW_INC" -I"$XXF_INC")
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
OBJECTS=()
i=0
for f in "${SOURCES[@]}"; do
    i=$((i+1))
    obj="$OUT/obj/$i.o"
    LANGOPT=()
    # A plain .c that pulls in Cocoa / uses ObjC syntax must be compiled as Objective-C.
    case "$f" in
        *.c) if grep -qE '<(Cocoa|AppKit|Foundation)/|^[[:space:]]*#[[:space:]]*import|@(interface|implementation|autoreleasepool)' "$f"; then
                 LANGOPT=(-x objective-c)
             fi ;;
    esac
    "$CC" "${CFLAGS[@]}" ${LANGOPT[@]+"${LANGOPT[@]}"} -c "$f" -o "$obj"
    OBJECTS+=("$obj")
done

echo "[2/2] linking cdie_gui..."
"$CC" -arch "$ARCH" -mmacosx-version-min="$MIN_MACOS" \
    "${OBJECTS[@]}" \
    -o "$OUT/cdie_gui" \
    "$XXW_LIB" "$XXF_LIB" ${CDIS_LIB:+"$CDIS_LIB"} \
    -framework Cocoa -framework AppKit -framework Foundation \
    -framework CoreGraphics -framework QuartzCore \
    -framework UserNotifications -framework UniformTypeIdentifiers \
    -lc++ -lm -lpthread

echo
echo "Built: $OUT/cdie_gui"
[ -n "$NO_RUN" ] || "$OUT/cdie_gui"