#!/usr/bin/env bash
set -euo pipefail

# Builds the cdie AppImage: die (GTK GUI), diec and die-update with the
# signature database, from the FHS install (prefix /usr inside the AppDir).
#
# The image bundles no libraries: GTK 3 and libcurl (die-update's https) come
# from the host, and glibc sets the oldest host it runs on, so release images
# are built on an old distribution (CI uses Ubuntu 22.04).
#
# AppRun picks the program (see packaging/appimage/AppRun):
#   cdie.AppImage [file]                 GUI
#   cdie.AppImage diec <options> <file>  command-line scanner
#   cdie.AppImage die-update [options]   database updater
#
# Repo convention:
# - build trees and staging always live under the temp directory
# - the repository release/ directory stores only ready-to-use packages
#
#   build_appimage.sh [--appdir-only] [extra cmake configure arguments...]
#
# --appdir-only stops after building and checking the AppDir (no appimagetool)
# and keeps the work root so the AppDir can be inspected.
#
# Optional environment variables:
#   CDIE_DATABASE_DIR     directory holding db and db_custom; NONE packages
#                         the programs alone
#   CMAKE_GENERATOR_NAME  CMake generator for a new build tree
#   CDIE_BUILD_DIR        build in this directory instead of a fresh one under
#                         the temp directory; it is (re)configured for the FHS
#                         layout and kept afterwards
#   CDIE_RELEASE_DIR      where the AppImage is copied, default <repo>/release
#   APPIMAGETOOL          appimagetool to use. Otherwise the one on PATH, else
#                         the continuous build is downloaded from GitHub.
#   KEEP_WORK_ROOT=1      keep the temp work root

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

WORK_ROOT="${TMPDIR:-/tmp}/cdie_appimage"
BUILD_DIR="${CDIE_BUILD_DIR:-${WORK_ROOT}/build}"
APP_DIR="${WORK_ROOT}/AppDir"
RELEASE_DIR="${CDIE_RELEASE_DIR:-${PROJECT_ROOT}/release}"
APP_ID="io.github.horsicq.cdie"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

usage() {
    echo "Usage: $(basename "$0") [--appdir-only] [extra cmake configure arguments...]"
    echo
    echo "Optional env: CDIE_DATABASE_DIR, CMAKE_GENERATOR_NAME, CDIE_BUILD_DIR,"
    echo "              CDIE_RELEASE_DIR, APPIMAGETOOL, KEEP_WORK_ROOT=1"
}

fail() {
    echo "$1" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "Required tool not found: $1"
}

APPDIR_ONLY=0
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi
if [[ "${1:-}" == "--appdir-only" ]]; then
    APPDIR_ONLY=1
    shift
fi

require_tool cmake
require_tool find

[[ -f "${PROJECT_ROOT}/release_version.txt" ]] || fail "release_version.txt not found: ${PROJECT_ROOT}/release_version.txt"
RELEASE_VERSION="$(head -n 1 "${PROJECT_ROOT}/release_version.txt" | tr -d '[:space:]')"

case "$(uname -m)" in
    x86_64 | amd64) APPIMAGE_ARCH="x86_64" ;;
    aarch64 | arm64) APPIMAGE_ARCH="aarch64" ;;
    *) fail "Unsupported AppImage architecture: $(uname -m)" ;;
esac

if [[ -n "${CDIE_DATABASE_DIR:-}" && "${CDIE_DATABASE_DIR}" != "NONE" && ! -d "${CDIE_DATABASE_DIR}/db" ]]; then
    fail "CDIE_DATABASE_DIR has no db directory: ${CDIE_DATABASE_DIR}"
fi

rm -rf "${WORK_ROOT}"
mkdir -p "${WORK_ROOT}"

cmake_args=(
    -S "${PROJECT_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr
    -DCDIE_INSTALL_LAYOUT=FHS
    -DCDIE_BUILD_GUI=ON
    -DCDIE_BUILD_CONSOLE=ON
    -DCDIE_BUILD_LIBRARY=OFF
    -DCDIE_BUILD_TESTS=OFF
)

if [[ -n "${CMAKE_GENERATOR_NAME:-}" && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake_args+=(-G "${CMAKE_GENERATOR_NAME}")
fi

if [[ -n "${CDIE_DATABASE_DIR:-}" ]]; then
    cmake_args+=(-DCDIE_DATABASE_DIR="${CDIE_DATABASE_DIR}")
fi

echo "Configuring AppImage build..."
cmake "${cmake_args[@]}" "$@"

echo "Building AppImage Release..."
# A bare --parallel means unlimited jobs to make; give it a number.
cmake --build "${BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

echo "Installing into the AppDir..."
DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}"

PROGRAMS="${APP_DIR}/usr/lib/cdie"
for program in die diec; do
    [[ -x "${PROGRAMS}/${program}" ]] || fail "Installed program not found: ${PROGRAMS}/${program}"
    [[ -L "${APP_DIR}/usr/bin/${program}" && -x "${APP_DIR}/usr/bin/${program}" ]] ||
        fail "usr/bin/${program} is not a symlink that resolves inside the AppDir"
done
if [[ ! -x "${PROGRAMS}/die-update" ]]; then
    echo "warning: die-update was not built (xxnetlib/xxgithub sources missing?); the image has no database updater" >&2
fi
if [[ "${CDIE_DATABASE_DIR:-}" != "NONE" && -z "$(find "${PROGRAMS}/db" -type f -print -quit 2>/dev/null)" ]]; then
    fail "The AppDir has no signature database in ${PROGRAMS}/db"
fi

# AppDir root: the entry point, the desktop entry and its icon.
install -m 0755 "${SCRIPT_DIR}/AppRun" "${APP_DIR}/AppRun"
cp -f "${APP_DIR}/usr/share/applications/${APP_ID}.desktop" "${APP_DIR}/${APP_ID}.desktop"
cp -f "${APP_DIR}/usr/share/icons/hicolor/256x256/apps/${APP_ID}.png" "${APP_DIR}/${APP_ID}.png"
ln -sf "${APP_ID}.png" "${APP_DIR}/.DirIcon"

echo "Checking the AppDir..."
"${APP_DIR}/AppRun" diec --version
if [[ -x "${PROGRAMS}/die-update" ]]; then
    "${APP_DIR}/AppRun" die-update --version
fi

if [[ "${APPDIR_ONLY}" == "1" ]]; then
    echo
    echo "AppDir created (appimagetool not run):"
    echo "  ${APP_DIR}"
    exit 0
fi

find_appimagetool() {
    if [[ -n "${APPIMAGETOOL:-}" ]]; then
        [[ -x "${APPIMAGETOOL}" ]] || fail "APPIMAGETOOL is not executable: ${APPIMAGETOOL}"
        echo "${APPIMAGETOOL}"
        return 0
    fi
    if command -v appimagetool >/dev/null 2>&1; then
        command -v appimagetool
        return 0
    fi
    [[ "${APPIMAGE_ARCH}" == "x86_64" ]] || fail "Set APPIMAGETOOL: only the x86_64 appimagetool is downloaded automatically"
    local tool="${WORK_ROOT}/tools/appimagetool-x86_64.AppImage"
    mkdir -p "${WORK_ROOT}/tools"
    echo "Downloading appimagetool..." >&2
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "${tool}" "${APPIMAGETOOL_URL}"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "${tool}" "${APPIMAGETOOL_URL}"
    else
        fail "Neither curl nor wget is available to download appimagetool; set APPIMAGETOOL"
    fi
    chmod +x "${tool}"
    echo "${tool}"
}

APPIMAGETOOL_BIN="$(find_appimagetool)"

mkdir -p "${RELEASE_DIR}"
FINAL_APPIMAGE="${RELEASE_DIR}/cdie_${RELEASE_VERSION}_${APPIMAGE_ARCH}.AppImage"
rm -f "${FINAL_APPIMAGE}"

echo "Creating AppImage..."
# Extract-and-run: no FUSE needed for appimagetool itself (containers, CI).
ARCH="${APPIMAGE_ARCH}" APPIMAGE_EXTRACT_AND_RUN=1 "${APPIMAGETOOL_BIN}" "${APP_DIR}" "${FINAL_APPIMAGE}"
[[ -s "${FINAL_APPIMAGE}" ]] || fail "appimagetool did not produce ${FINAL_APPIMAGE}"
chmod +x "${FINAL_APPIMAGE}"

if [[ "${KEEP_WORK_ROOT:-0}" != "1" ]]; then
    rm -rf "${WORK_ROOT}"
fi

echo
echo "AppImage created:"
echo "  ${FINAL_APPIMAGE}"
