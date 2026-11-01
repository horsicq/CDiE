#!/usr/bin/env bash
set -euo pipefail

# Builds the cdie .deb: die (GTK GUI), diec and die-update with the signature
# database, in the FHS layout (programs and databases in /usr/lib/cdie,
# symlinks in /usr/bin, desktop entry, icons, AppStream data, documents in
# /usr/share/doc/cdie).
#
# Repo convention:
# - build trees and CPack staging always live under the temp directory
# - the repository release/ directory stores only ready-to-use packages
#
#   build_deb.sh [extra cmake configure arguments...]
#   build_deb.sh -DXXFCLIB_DIR="$PWD/dep/xxfclib" -DXXFC_CDISASM_DIR="$PWD/dep/cdisasm"
#
# Optional environment variables:
#   CDIE_DATABASE_DIR     directory holding db and db_custom; NONE packages
#                         the programs alone. Unset: the database CMake
#                         finds itself (dep/Detect-It-Easy or
#                         ../_mylibs/Detect-It-Easy), with a warning.
#   CMAKE_GENERATOR_NAME  CMake generator for a new build tree
#   CDIE_BUILD_DIR        build in this directory instead of a fresh one under
#                         the temp directory; it is (re)configured for the FHS
#                         layout and kept afterwards
#   CDIE_RELEASE_DIR      where the .deb is copied, default <repo>/release
#   KEEP_WORK_ROOT=1      keep the temp work root (build tree, CPack staging)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

WORK_ROOT="${TMPDIR:-/tmp}/cdie_deb"
BUILD_DIR="${CDIE_BUILD_DIR:-${WORK_ROOT}/build}"
CPACK_DIR="${WORK_ROOT}/cpack"
CPACK_OUTPUT_DIR="${WORK_ROOT}/output"
RELEASE_DIR="${CDIE_RELEASE_DIR:-${PROJECT_ROOT}/release}"

usage() {
    echo "Usage: $(basename "$0") [extra cmake configure arguments...]"
    echo
    echo "Optional env: CDIE_DATABASE_DIR, CMAKE_GENERATOR_NAME, CDIE_BUILD_DIR,"
    echo "              CDIE_RELEASE_DIR, KEEP_WORK_ROOT=1"
}

fail() {
    echo "$1" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "Required tool not found: $1"
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

require_tool cmake
require_tool cpack
require_tool find
# CPack's DEB generator needs dpkg-shlibdeps for the library dependencies,
# and file to find the ELF binaries to pass it (checked here, not after the
# whole build).
require_tool dpkg-shlibdeps
require_tool file
require_tool dpkg-deb

[[ -f "${PROJECT_ROOT}/release_version.txt" ]] || fail "release_version.txt not found: ${PROJECT_ROOT}/release_version.txt"

if [[ -n "${CDIE_DATABASE_DIR:-}" && "${CDIE_DATABASE_DIR}" != "NONE" && ! -d "${CDIE_DATABASE_DIR}/db" ]]; then
    fail "CDIE_DATABASE_DIR has no db directory: ${CDIE_DATABASE_DIR}"
fi

mkdir -p "${RELEASE_DIR}"

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

echo "Configuring Debian build..."
cmake "${cmake_args[@]}" "$@"

echo "Building Debian Release..."
# A bare --parallel means unlimited jobs to make; give it a number.
cmake --build "${BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

for program in src/gui/die src/console/diec; do
    [[ -x "${BUILD_DIR}/${program}" ]] || fail "Built executable not found: ${BUILD_DIR}/${program}"
done
if [[ ! -x "${BUILD_DIR}/src/updater/die-update" ]]; then
    echo "warning: die-update was not built (xxnetlib/xxgithub sources missing?); the package has no database updater" >&2
fi

CPACK_CONFIG="${BUILD_DIR}/CPackConfig.cmake"
[[ -f "${CPACK_CONFIG}" ]] || fail "CPack config not found: ${CPACK_CONFIG}"

rm -rf "${CPACK_DIR}" "${CPACK_OUTPUT_DIR}"

echo "Creating Debian package with CPack..."
cpack \
    --config "${CPACK_CONFIG}" \
    -G DEB \
    -B "${CPACK_DIR}" \
    -D "CPACK_OUTPUT_FILE_PREFIX=${CPACK_OUTPUT_DIR}"

deb_files=()
while IFS= read -r deb_file; do
    deb_files+=("${deb_file}")
done < <(find "${CPACK_OUTPUT_DIR}" -type f -name "*.deb" -print | sort)

[[ "${#deb_files[@]}" -eq 1 ]] || fail "Expected one .deb in ${CPACK_OUTPUT_DIR}, found ${#deb_files[@]}"

PACKAGE_DEB="${deb_files[0]}"

# The layout the programs depend on: the databases beside the real
# executables, and relative bin/ links that resolve inside any root.
contents="$(dpkg-deb -c "${PACKAGE_DEB}")"
for entry in ./usr/lib/cdie/die ./usr/lib/cdie/diec ./usr/share/applications/io.github.horsicq.cdie.desktop \
    ./usr/share/doc/cdie/copyright; do
    grep -Eq " ${entry}( |$)" <<<"${contents}" || fail "The package is missing ${entry}"
done
for program in die diec; do
    grep -Fq "./usr/bin/${program} -> ../lib/cdie/${program}" <<<"${contents}" ||
        fail "The package has no relative /usr/bin/${program} link"
done
if [[ "${CDIE_DATABASE_DIR:-}" != "NONE" ]]; then
    grep -Eq ' \./usr/lib/cdie/db/.+[^/]$' <<<"${contents}" || fail "The package has no signature database"
fi

FINAL_DEB="${RELEASE_DIR}/$(basename "${PACKAGE_DEB}")"

rm -f "${FINAL_DEB}"
cp -f "${PACKAGE_DEB}" "${FINAL_DEB}"

if [[ "${KEEP_WORK_ROOT:-0}" != "1" ]]; then
    rm -rf "${WORK_ROOT}"
fi

echo
dpkg-deb -f "${FINAL_DEB}" Package Version Architecture Depends Conflicts
echo
echo "Debian package created:"
echo "  ${FINAL_DEB}"
