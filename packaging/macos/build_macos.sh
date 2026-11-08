#!/usr/bin/env bash
set -euo pipefail

# Builds the macOS packages of cdie, two files per architecture:
#   cdie_mac_<arch>_<version>.zip  die.app, zipped with ditto (Finder-style)
#   cdie_mac_<arch>_<version>.pkg  installs die.app into /Applications
# <arch> is arm64, x64 or universal. By default both arm64 and x64 are built,
# as two separate single-architecture builds in two build trees: an Apple
# silicon Mac cross-compiles x86_64 (and an Intel Mac arm64) with the SDK
# Xcode already has.
#
# die.app carries die-update in Contents/MacOS and the signature database in
# Contents/Resources (src/gui/CMakeLists.txt); macOS ships no console programs.
# Written for the bash 3.2 that macOS includes: no mapfile, no associative
# arrays, and empty arrays are expanded with ${a[@]+"${a[@]}"} because of
# set -u.
#
# Repo convention:
# - build trees and staging always live under the temp directory
# - the repository release/ directory stores only ready-to-use packages
#
#   build_macos.sh [extra cmake configure arguments...]
#
# Optional environment variables:
#   CDIE_DATABASE_DIR            directory holding db and db_custom;
#                                NONE packages the app alone
#   CMAKE_GENERATOR_NAME         CMake generator for a new build tree
#   CMAKE_OSX_ARCHITECTURES or MACOS_ARCHITECTURES (the first one set wins)
#                                the packages to build, separated by spaces or
#                                commas: arm64, x86_64 (or x64), and
#                                "arm64;x86_64" (or universal) for one
#                                universal package. Default: "arm64 x86_64".
#                                Pass architectures only here, never as
#                                -DCMAKE_OSX_ARCHITECTURES.
#   CMAKE_OSX_DEPLOYMENT_TARGET or MACOSX_DEPLOYMENT_TARGET
#                                oldest macOS every package supports
#   MACOS_DEPLOYMENT_TARGET_ARM64, MACOS_DEPLOYMENT_TARGET_X86_64
#                                the same for one architecture; a universal
#                                package takes the x86_64 value (the compiler
#                                raises its arm64 half to 11.0). Defaults:
#                                11.0 for arm64, the first macOS on Apple
#                                silicon; 10.14 for x86_64, because the
#                                xxwidgets Cocoa backend calls macOS 10.14 API
#                                without @available (NSColor separatorColor,
#                                NSAppearance bestMatchFromAppearancesWithNames:,
#                                NSAppearanceNameDarkAqua), besides 10.13's
#                                NSPasteboardTypeFileURL and 10.12's
#                                clock_gettime; newer API is guarded. With no
#                                value at all clang would target the SDK's
#                                macOS and the app would not start on older
#                                systems.
#   CDIE_BUILD_DIR               build in <dir>/arm64, <dir>/x64 or
#                                <dir>/universal instead of fresh trees under
#                                the temp directory; kept afterwards, so it
#                                must not lie inside $TMPDIR/cdie_mac, which
#                                every run deletes (CDIE_RELEASE_DIR neither).
#                                A db_extra an earlier version copied into
#                                die.app is removed by the build
#   CDIE_RELEASE_DIR             where the packages go, default <repo>/release
#   MACOS_PKG_IDENTIFIER         installer package id, default com.ntinfo.cdie
#   MACOS_ADHOC_SIGN             what die.app gets when it is not signed with a
#                                Developer ID (see below):
#                                1 (default) a credential-free ad hoc seal of
#                                  the whole bundle (codesign --sign -). It
#                                  needs no certificate, makes the bundle
#                                  verifiable, and a downloaded copy then gets
#                                  Gatekeeper's "Open Anyway" path instead of
#                                  being reported as damaged.
#                                0 no codesign of the bundle at all: only a
#                                  check that each program in Contents/MacOS
#                                  kept the signature the linker gave it
#                                  (Apple silicon runs no unsigned arm64 code)
#                                  and an ad hoc signature for just a program
#                                  that cmake --install left without a valid
#                                  one. A downloaded (quarantined) copy may be
#                                  reported as damaged; an installed .pkg or a
#                                  local copy runs.
#   KEEP_WORK_ROOT=1             keep the temp work root
#
# Signing and notarization need no setup: without certificates and passwords
# the packages are built all the same. Anything missing, incomplete or
# unusable is skipped with a "skipping ..." notice, never an error; under
# GitHub Actions a credential that is set but cannot be used also becomes a
# warning annotation, since the run still succeeds.
#   X_PRIVATE_CERT_APP           Developer ID Application identity for die.app
#                                (hardened runtime, timestamp): its SHA-1, its
#                                name or part of it, as "security find-identity
#                                -v -p codesigning" lists it. Of several names
#                                containing the text a "Developer ID
#                                Application:" one wins, and codesign gets the
#                                SHA-1 of the identity found, so the identity
#                                checked is the one used. Not set, '-' (ad
#                                hoc), not found, or codesign fails with it:
#                                die.app is not signed with a Developer ID and
#                                gets MACOS_ADHOC_SIGN's treatment.
#   X_PRIVATE_APP_ENTITLEMENTS   entitlements plist for that signature; a path
#                                that does not exist is ignored with a notice
#   X_PRIVATE_CERT_INSTALL       Developer ID Installer identity for the .pkg,
#                                looked up the same way in "security
#                                find-identity -v -p basic" (a "Developer ID
#                                Installer:" name wins); pkgbuild/productbuild
#                                get the full name found. Not set, '-', not
#                                found, or pkgbuild/productbuild fail with it:
#                                the .pkg is unsigned.
#   X_PRIVATE_KEYCHAIN_PATH      keychain holding the identities: a path, the
#                                same without its -db suffix, or a name in
#                                ~/Library/Keychains (as "security
#                                create-keychain" takes it). When none of
#                                these exists neither identity is used.
#   X_PRIVATE_NOTARIZE_KEYCHAIN_PATH
#                                optional file-based keychain for the notary
#                                profile; unset keeps notarytool's default
#   X_PRIVATE_NOTARIZE_TIMEOUT   optional wait limit, for example 30m; unset
#                                keeps notarytool's default wait behavior
#   X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE, or all three of
#   X_PRIVATE_NOTARIZE_LOGIN, X_PRIVATE_NOTARIZE_PWD, X_PRIVATE_NOTARIZE_TEAMID
#                                notarize and staple. The zip (and the die.app
#                                both packages carry) only when die.app was
#                                signed with a Developer ID Application
#                                identity, the .pkg only when it was also
#                                signed with a Developer ID Installer one, and
#                                only when xcrun notarytool exists; otherwise
#                                notarization is skipped and the notice names
#                                what is missing (never a secret). With complete
#                                credentials a submission that fails or that
#                                Apple does not answer with "Accepted" is an
#                                error (the notary log is printed), and so is
#                                a staple that still fails after 3 retries.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_GENERATOR="${CMAKE_GENERATOR_NAME:-}"
ARCH_REQUEST="${CMAKE_OSX_ARCHITECTURES:-${MACOS_ARCHITECTURES:-}}"
DEPLOYMENT_TARGET_ALL="${CMAKE_OSX_DEPLOYMENT_TARGET:-${MACOSX_DEPLOYMENT_TARGET:-}}"
DEPLOYMENT_TARGET_ARM64="${MACOS_DEPLOYMENT_TARGET_ARM64:-${DEPLOYMENT_TARGET_ALL:-11.0}}"
DEPLOYMENT_TARGET_X86_64="${MACOS_DEPLOYMENT_TARGET_X86_64:-${DEPLOYMENT_TARGET_ALL:-10.14}}"
PKG_IDENTIFIER="${MACOS_PKG_IDENTIFIER:-com.ntinfo.cdie}"
ADHOC_SIGN="${MACOS_ADHOC_SIGN:-1}"

APP_CERT="${X_PRIVATE_CERT_APP:-}"
INSTALLER_CERT="${X_PRIVATE_CERT_INSTALL:-}"
APP_ENTITLEMENTS="${X_PRIVATE_APP_ENTITLEMENTS:-}"
KEYCHAIN_PATH="${X_PRIVATE_KEYCHAIN_PATH:-}"
NOTARY_PROFILE="${X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE:-}"
NOTARY_KEYCHAIN_PATH="${X_PRIVATE_NOTARIZE_KEYCHAIN_PATH:-}"
NOTARY_TIMEOUT="${X_PRIVATE_NOTARIZE_TIMEOUT:-}"
NOTARY_LOGIN="${X_PRIVATE_NOTARIZE_LOGIN:-}"
NOTARY_PASSWORD="${X_PRIVATE_NOTARIZE_PWD:-}"
NOTARY_TEAM_ID="${X_PRIVATE_NOTARIZE_TEAMID:-}"

TEMP_ROOT="${TMPDIR:-/tmp}"
WORK_ROOT="${TEMP_ROOT%/}/cdie_mac"
RELEASE_DIR="${CDIE_RELEASE_DIR:-${PROJECT_ROOT}/release}"

usage() {
    echo "Usage: $(basename "$0") [extra cmake configure arguments...]"
    echo
    echo "Builds cdie_mac_<arch>_<version>.zip and .pkg for arm64 and x64 (default),"
    echo "or for what MACOS_ARCHITECTURES lists. Nothing needs certificates or"
    echo "passwords: what is missing is skipped with a notice."
    echo
    echo "Optional env:"
    echo "  CDIE_DATABASE_DIR, CMAKE_GENERATOR_NAME, CDIE_BUILD_DIR, CDIE_RELEASE_DIR"
    echo "  MACOS_ARCHITECTURES or CMAKE_OSX_ARCHITECTURES: \"arm64 x86_64\" (default),"
    echo "    arm64, x86_64, or \"arm64;x86_64\" / universal for one universal package"
    echo "  CMAKE_OSX_DEPLOYMENT_TARGET or MACOSX_DEPLOYMENT_TARGET (every package)"
    echo "  MACOS_DEPLOYMENT_TARGET_ARM64 (default 11.0), MACOS_DEPLOYMENT_TARGET_X86_64 (default 10.14)"
    echo "  MACOS_ADHOC_SIGN=1 (default: ad hoc seal without a Developer ID) or 0 (no codesign)"
    echo "  MACOS_PKG_IDENTIFIER, KEEP_WORK_ROOT=1"
    echo "  X_PRIVATE_CERT_APP, X_PRIVATE_APP_ENTITLEMENTS, X_PRIVATE_CERT_INSTALL, X_PRIVATE_KEYCHAIN_PATH"
    echo "  X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE, X_PRIVATE_NOTARIZE_KEYCHAIN_PATH, X_PRIVATE_NOTARIZE_TIMEOUT"
    echo "  X_PRIVATE_NOTARIZE_LOGIN / X_PRIVATE_NOTARIZE_PWD / X_PRIVATE_NOTARIZE_TEAMID"
}

fail() {
    echo "$1" >&2
    exit 1
}

notice() {
    echo "notice: $1" >&2
}

# A credential that is set but cannot be used. The build goes on without it,
# so under GitHub Actions it also becomes a warning on the run: a green build
# with packages that were meant to be signed or notarized must not go
# unnoticed.
notice_unusable() {
    notice "$1"
    if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
        echo "::warning title=macOS signing::$1"
    fi
}

# The physical form of a path that may not exist yet: its deepest existing
# directory with symbolic links resolved (/var is /private/var on macOS),
# followed by the rest.
physical_path() {
    local path="$1"
    local rest=""
    local resolved=""

    while [[ ! -d "${path}" ]]; do
        rest="/$(basename "${path}")${rest}"
        path="$(dirname "${path}")"
    done
    resolved="$(cd "${path}" 2>/dev/null && pwd -P)" || resolved="${path}"
    resolved="${resolved%/}${rest}"
    printf '%s\n' "${resolved:-/}"
}

# path_is_inside <path> <directory>: <path> is <directory> or lies below it.
path_is_inside() {
    local path=""
    local directory=""

    path="$(physical_path "$1")"
    directory="$(physical_path "$2")"
    [[ "${path}" == "${directory}" || "${path}" == "${directory}/"* ]]
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "Required tool not found: $1"
}

lowercase() {
    tr '[:upper:]' '[:lower:]' <<<"$1"
}

uppercase() {
    tr '[:lower:]' '[:upper:]' <<<"$1"
}

# Words of a list in a fixed order, for comparing architecture lists.
sorted_words() {
    tr ' ;,' '\n\n\n' <<<"$1" | sed '/^$/d' | sort -u | tr '\n' ' ' | sed 's/ $//'
}

# ------------------------------------------------------------ architectures

# One entry per package, in parallel arrays (bash 3.2 has no associative
# ones): the file name part, CMAKE_OSX_ARCHITECTURES, what lipo -archs must
# report, and the deployment target.
PACKAGE_COUNT=0
PACKAGE_LABELS=()
PACKAGE_CMAKE_ARCHS=()
PACKAGE_LIPO_ARCHS=()
PACKAGE_DEPLOYMENT_TARGETS=()

add_package() {
    local request="$1"
    local parts=()
    local part=""
    local arm64=0
    local x86_64=0
    local label=""
    local cmake_archs=""
    local lipo_archs=""
    local deployment_target=""
    local i=0

    if [[ "$(lowercase "${request}")" == "universal" ]]; then
        request="arm64;x86_64"
    fi
    # A ;-separated list is one universal package.
    IFS=';' read -r -a parts <<<"${request}"
    for part in ${parts[@]+"${parts[@]}"}; do
        case "$(lowercase "${part}")" in
            "") ;;
            arm64 | aarch64) arm64=1 ;;
            x86_64 | x64 | amd64) x86_64=1 ;;
            *) fail "Unknown macOS architecture '${part}' in '${ARCH_REQUEST}' (use arm64, x86_64, or \"arm64;x86_64\" for a universal package)" ;;
        esac
    done
    if [[ ${arm64} == 1 && ${x86_64} == 1 ]]; then
        label="universal"
        cmake_archs="arm64;x86_64"
        lipo_archs="arm64 x86_64"
        deployment_target="${DEPLOYMENT_TARGET_X86_64}"
    elif [[ ${arm64} == 1 ]]; then
        label="arm64"
        cmake_archs="arm64"
        lipo_archs="arm64"
        deployment_target="${DEPLOYMENT_TARGET_ARM64}"
    elif [[ ${x86_64} == 1 ]]; then
        label="x64"
        cmake_archs="x86_64"
        lipo_archs="x86_64"
        deployment_target="${DEPLOYMENT_TARGET_X86_64}"
    else
        return 0
    fi

    while [[ ${i} -lt ${PACKAGE_COUNT} ]]; do
        if [[ "${PACKAGE_LABELS[${i}]}" == "${label}" ]]; then
            return 0
        fi
        i=$((i + 1))
    done
    PACKAGE_LABELS[${PACKAGE_COUNT}]="${label}"
    PACKAGE_CMAKE_ARCHS[${PACKAGE_COUNT}]="${cmake_archs}"
    PACKAGE_LIPO_ARCHS[${PACKAGE_COUNT}]="${lipo_archs}"
    PACKAGE_DEPLOYMENT_TARGETS[${PACKAGE_COUNT}]="${deployment_target}"
    PACKAGE_COUNT=$((PACKAGE_COUNT + 1))
}

parse_architectures() {
    local request="${ARCH_REQUEST:-arm64 x86_64}"
    local tokens=()
    local token=""

    # "arm64 ; x86_64" is still one universal package; commas separate packages.
    request="$(sed -E 's/[[:space:]]*;[[:space:]]*/;/g' <<<"${request}")"
    request="${request//,/ }"
    read -r -a tokens <<<"${request}"
    for token in ${tokens[@]+"${tokens[@]}"}; do
        add_package "${token}"
    done
    [[ ${PACKAGE_COUNT} -gt 0 ]] || fail "No macOS architecture in '${ARCH_REQUEST}'"
}

# Every die.app program must hold exactly the package's architectures.
check_architectures() {
    local app_bundle="$1"
    local expected="$2"
    local name=""
    local binary=""
    local archs=""

    for name in die die-update; do
        binary="${app_bundle}/Contents/MacOS/${name}"
        [[ -f "${binary}" ]] || continue
        archs="$(lipo -archs "${binary}")" || fail "lipo cannot read ${binary}"
        if [[ "$(sorted_words "${archs}")" != "$(sorted_words "${expected}")" ]]; then
            fail "${name} holds '${archs}', expected '${expected}': ${binary}"
        fi
        echo "${name}: ${archs}"
    done
}

# ------------------------------------------------- signing and notarization

KEYCHAIN_USABLE=1
KEYCHAIN_FILE=""
KEYCHAIN_ARGS=()
APP_IDENTITY_HASH=""
APP_IDENTITY_NAME=""
INSTALLER_IDENTITY_NAME=""
FOUND_IDENTITY_HASH=""
FOUND_IDENTITY_NAME=""
NOTARY_MODE=""
NOTARY_BLOCKER=""
NOTARY_CONFIGURED=0

# X_PRIVATE_KEYCHAIN_PATH may name the keychain in any form security and
# codesign accept: a path, the same path without the -db suffix keychain
# files carry since macOS 10.12, or a bare name in ~/Library/Keychains
# ("security create-keychain build.keychain" makes
# ~/Library/Keychains/build.keychain-db). Prints the file that exists.
find_keychain() {
    local candidates=("${KEYCHAIN_PATH}" "${KEYCHAIN_PATH}-db")
    local candidate=""

    if [[ "${KEYCHAIN_PATH}" != */* && -n "${HOME:-}" ]]; then
        candidates+=("${HOME}/Library/Keychains/${KEYCHAIN_PATH}" "${HOME}/Library/Keychains/${KEYCHAIN_PATH}-db")
    fi
    for candidate in "${candidates[@]}"; do
        if [[ -f "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

# find_identity <policy> <identity> <preferred name prefix>: prints
# "<SHA-1> <certificate name>" of the valid identity <identity> selects: the
# one with that SHA-1 or exactly that name, else, of the names containing
# <identity>, the first that starts with <preferred name prefix>, else the
# first. Fails when there is none. Signing then names the identity found
# (codesign by its SHA-1), never the text the variable holds, so the identity
# checked here is the one that signs and the one notarization is judged by.
find_identity() {
    local policy="$1"
    local wanted="$2"
    local prefix="$3"
    local wanted_upper=""
    local listing=""
    local line=""
    local hash=""
    local name=""
    local first=""
    local preferred=""
    local pattern='^[[:space:]]*[0-9]+\)[[:space:]]+([0-9A-Fa-f]{40})[[:space:]]+"(.*)"[[:space:]]*$'

    listing="$(security find-identity -v -p "${policy}" ${KEYCHAIN_ARGS[@]+"${KEYCHAIN_ARGS[@]}"} 2>/dev/null)" || return 1
    wanted_upper="$(uppercase "${wanted}")"
    while IFS= read -r line; do
        [[ "${line}" =~ ${pattern} ]] || continue
        name="${BASH_REMATCH[2]}"
        hash="$(uppercase "${BASH_REMATCH[1]}")"
        if [[ "${hash}" == "${wanted_upper}" || "${name}" == "${wanted}" ]]; then
            printf '%s %s\n' "${hash}" "${name}"
            return 0
        fi
        if [[ "${name}" == *"${wanted}"* ]]; then
            if [[ -z "${first}" ]]; then
                first="${hash} ${name}"
            fi
            if [[ -z "${preferred}" && "${name}" == "${prefix}"* ]]; then
                preferred="${hash} ${name}"
            fi
        fi
    done <<<"${listing}"
    if [[ -n "${preferred}" ]]; then
        printf '%s\n' "${preferred}"
    elif [[ -n "${first}" ]]; then
        printf '%s\n' "${first}"
    else
        return 1
    fi
}

# resolve_identity <what> <variable name> <identity> <policy> <preferred name
# prefix>: sets FOUND_IDENTITY_HASH and FOUND_IDENTITY_NAME when the identity
# is usable, else leaves them empty and prints a "skipping" notice.
resolve_identity() {
    local what="$1"
    local variable="$2"
    local identity="$3"
    local policy="$4"
    local prefix="$5"
    local found=""

    FOUND_IDENTITY_HASH=""
    FOUND_IDENTITY_NAME=""
    if [[ -z "${identity}" ]]; then
        notice "skipping Developer ID signing of ${what}: ${variable} is not set"
    elif [[ "${identity}" == "-" ]]; then
        notice "skipping Developer ID signing of ${what}: ${variable} is '-', the ad hoc identity, not a Developer ID one"
    elif [[ ${KEYCHAIN_USABLE} != 1 ]]; then
        notice_unusable "skipping Developer ID signing of ${what}: the keychain in X_PRIVATE_KEYCHAIN_PATH was not found (${KEYCHAIN_PATH}, also tried with -db and in ~/Library/Keychains)"
    elif ! command -v security >/dev/null 2>&1; then
        notice_unusable "skipping Developer ID signing of ${what}: the security tool is not available"
    elif found="$(find_identity "${policy}" "${identity}" "${prefix}")"; then
        FOUND_IDENTITY_HASH="${found%% *}"
        FOUND_IDENTITY_NAME="${found#* }"
    else
        notice_unusable "skipping Developer ID signing of ${what}: no valid identity matching ${variable} ('${identity}') in ${KEYCHAIN_FILE:-the keychain search list} (security find-identity -v -p ${policy})"
    fi
}

resolve_signing() {
    if [[ -n "${KEYCHAIN_PATH}" ]]; then
        if KEYCHAIN_FILE="$(find_keychain)"; then
            KEYCHAIN_ARGS=("${KEYCHAIN_FILE}")
        else
            KEYCHAIN_FILE=""
            KEYCHAIN_USABLE=0
        fi
    fi
    resolve_identity die.app X_PRIVATE_CERT_APP "${APP_CERT}" codesigning "Developer ID Application:"
    APP_IDENTITY_HASH="${FOUND_IDENTITY_HASH}"
    APP_IDENTITY_NAME="${FOUND_IDENTITY_NAME}"
    resolve_identity "the .pkg" X_PRIVATE_CERT_INSTALL "${INSTALLER_CERT}" basic "Developer ID Installer:"
    INSTALLER_IDENTITY_NAME="${FOUND_IDENTITY_NAME}"
}

# Notarization needs complete credentials and notarytool; the signatures are
# checked per package. Only variable names ever reach the output.
resolve_notarization() {
    local missing=""

    if [[ -n "${NOTARY_PROFILE}${NOTARY_LOGIN}${NOTARY_PASSWORD}${NOTARY_TEAM_ID}" ]]; then
        NOTARY_CONFIGURED=1
    fi
    if [[ -n "${NOTARY_PROFILE}" ]]; then
        NOTARY_MODE="profile"
    elif [[ -n "${NOTARY_LOGIN}" || -n "${NOTARY_PASSWORD}" || -n "${NOTARY_TEAM_ID}" ]]; then
        [[ -n "${NOTARY_LOGIN}" ]] || missing="${missing} X_PRIVATE_NOTARIZE_LOGIN"
        [[ -n "${NOTARY_PASSWORD}" ]] || missing="${missing} X_PRIVATE_NOTARIZE_PWD"
        [[ -n "${NOTARY_TEAM_ID}" ]] || missing="${missing} X_PRIVATE_NOTARIZE_TEAMID"
        if [[ -z "${missing}" ]]; then
            NOTARY_MODE="apple-id"
        else
            NOTARY_BLOCKER="incomplete notarization credentials, not set:${missing}"
        fi
    else
        NOTARY_BLOCKER="no notarization credentials (X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE, or X_PRIVATE_NOTARIZE_LOGIN, X_PRIVATE_NOTARIZE_PWD and X_PRIVATE_NOTARIZE_TEAMID)"
    fi
    if [[ -n "${NOTARY_MODE}" ]]; then
        if ! command -v xcrun >/dev/null 2>&1 || ! xcrun notarytool --help >/dev/null 2>&1; then
            NOTARY_MODE=""
            NOTARY_BLOCKER="xcrun notarytool is not available (it comes with Xcode 13 or newer)"
        fi
    fi
}

# Why the zip (and so die.app) cannot be notarized; empty when it can.
app_notarization_blocker() {
    local app_signed_with="$1"

    if [[ -z "${NOTARY_MODE}" ]]; then
        echo "${NOTARY_BLOCKER}"
    elif [[ "${app_signed_with}" != "Developer ID Application:"* ]]; then
        echo "die.app is not signed with a Developer ID Application identity, and Apple notarizes nothing else"
    fi
}

# Why the .pkg cannot be notarized; empty when it can.
pkg_notarization_blocker() {
    local app_signed_with="$1"
    local pkg_signed_with="$2"

    if [[ -z "${NOTARY_MODE}" ]]; then
        echo "${NOTARY_BLOCKER}"
    elif [[ "${pkg_signed_with}" != "Developer ID Installer:"* ]]; then
        echo "the .pkg is not signed with a Developer ID Installer identity"
    elif [[ "${app_signed_with}" != "Developer ID Application:"* ]]; then
        echo "the die.app inside is not signed with a Developer ID Application identity"
    fi
}

# A skipped notarization: a warning too when notarization was configured.
notarization_skipped() {
    if [[ ${NOTARY_CONFIGURED} == 1 ]]; then
        notice_unusable "$1"
    else
        notice "$1"
    fi
}

# Called only with complete credentials: a failure is an error. Success is
# the "status" of notarytool's JSON answer, not only its exit status; for
# anything but Accepted the submission's notary log is printed to show why.
notarize_file() {
    local artifact="$1"
    local name=""
    local answer=""
    local result=0
    local submission_id=""
    local auth_args=()
    local submit_args=()
    local accepted='"status"[[:space:]]*:[[:space:]]*"Accepted"'
    local id_pattern='"id"[[:space:]]*:[[:space:]]*"([^"]+)"'

    name="$(basename "${artifact}")"
    if [[ "${NOTARY_MODE}" == "profile" ]]; then
        auth_args=(--keychain-profile "${NOTARY_PROFILE}")
        if [[ -n "${NOTARY_KEYCHAIN_PATH}" ]]; then
            auth_args+=(--keychain "${NOTARY_KEYCHAIN_PATH}")
        fi
    else
        auth_args=(--apple-id "${NOTARY_LOGIN}" --password "${NOTARY_PASSWORD}" --team-id "${NOTARY_TEAM_ID}")
    fi
    if [[ -n "${NOTARY_TIMEOUT}" ]]; then
        submit_args=(--timeout "${NOTARY_TIMEOUT}")
    fi
    echo "Submitting ${name} for notarization and waiting for Apple's answer..."
    answer="$(xcrun notarytool submit "${artifact}" "${auth_args[@]}" --wait ${submit_args[@]+"${submit_args[@]}"} --output-format json)" || result=$?
    printf '%s\n' "${answer}"
    if [[ ${result} == 0 && "${answer}" =~ ${accepted} ]]; then
        return 0
    fi
    if [[ "${answer}" =~ ${id_pattern} ]]; then
        submission_id="${BASH_REMATCH[1]}"
        echo "Notary log of submission ${submission_id}:"
        xcrun notarytool log "${submission_id}" "${auth_args[@]}" || true
    fi
    fail "Notarization of ${name} failed: notarytool exit status ${result}, and Apple's answer is not \"Accepted\""
}

# A ticket Apple has just issued can take a moment to reach the servers
# stapler asks, so a failed staple is retried before it counts as an error.
staple_file() {
    local target="$1"
    local attempt=1

    echo "Stapling $(basename "${target}")..."
    until xcrun stapler staple "${target}"; do
        if [[ ${attempt} -ge 4 ]]; then
            fail "Stapling $(basename "${target}") failed"
        fi
        echo "Stapling failed (attempt ${attempt} of 4); retrying in 30 seconds..."
        sleep 30
        attempt=$((attempt + 1))
    done
    xcrun stapler validate "${target}" || fail "The ticket stapled to $(basename "${target}") does not validate"
}

# Developer ID signature, made on a copy so that a codesign failure leaves the
# staged bundle untouched. Every change to die.app has to be followed by
# signing it again: a stale seal makes Gatekeeper report the app as damaged.
# Called as a condition, so each step is checked explicitly.
sign_with_identity() {
    local app_bundle="$1"
    local copy_root="$2"
    local copy=""
    local helper=""
    local sign_args=()

    copy="${copy_root}/$(basename "${app_bundle}")"
    rm -rf "${copy_root}" || return 1
    mkdir -p "${copy_root}" || return 1
    ditto "${app_bundle}" "${copy}" || return 1

    # The SHA-1 found by resolve_signing: exactly the identity checked, never
    # ambiguous (a renewed certificate leaves an expired one of the same name).
    sign_args=(--force --timestamp --options runtime --sign "${APP_IDENTITY_HASH}")
    if [[ -n "${KEYCHAIN_FILE}" ]]; then
        sign_args+=(--keychain "${KEYCHAIN_FILE}")
    fi
    # Helper programs beside the main executable first, then the bundle.
    for helper in "${copy}/Contents/MacOS/"*; do
        if [[ -f "${helper}" && "$(basename "${helper}")" != "die" ]]; then
            codesign "${sign_args[@]}" "${helper}" || return 1
        fi
    done
    if [[ -n "${APP_ENTITLEMENTS}" ]]; then
        if [[ -f "${APP_ENTITLEMENTS}" ]]; then
            sign_args+=(--entitlements "${APP_ENTITLEMENTS}")
        else
            notice "X_PRIVATE_APP_ENTITLEMENTS does not exist (${APP_ENTITLEMENTS}); signing die.app without entitlements"
        fi
    fi
    codesign "${sign_args[@]}" --deep "${copy}" || return 1
    codesign --verify --deep --strict --verbose=2 "${copy}" || return 1

    rm -rf "${app_bundle}" || return 1
    mv "${copy}" "${app_bundle}" || return 1
}

# MACOS_ADHOC_SIGN=1 without a Developer ID: an ad hoc seal of the whole
# bundle. "codesign --sign -" uses no certificate and cannot lack one.
seal_ad_hoc() {
    local app_bundle="$1"
    local helper=""

    for helper in "${app_bundle}/Contents/MacOS/"*; do
        if [[ -f "${helper}" && "$(basename "${helper}")" != "die" ]]; then
            codesign --force --sign - "${helper}"
        fi
    done
    codesign --force --sign - --deep "${app_bundle}"
    codesign --verify --deep --strict --verbose=2 "${app_bundle}"
}

# MACOS_ADHOC_SIGN=0: no codesign of the bundle. Apple silicon runs arm64 code
# only with a valid signature, which the linker adds by itself and which
# cmake --install (install_name_tool, strip) can break; x86_64 code may be
# unsigned. Each program is checked as a standalone copy, because codesign
# takes the path of a bundle's main executable for the whole bundle, and only
# a program without a valid signature is signed ad hoc and copied back.
keep_linker_signatures() {
    local app_bundle="$1"
    local work="$2"
    local binary=""
    local copy=""
    local arch=""
    local details=""
    local resign=0

    rm -rf "${work}"
    mkdir -p "${work}"
    for binary in "${app_bundle}/Contents/MacOS/"*; do
        [[ -f "${binary}" ]] || continue
        copy="${work}/$(basename "${binary}")"
        cp -p "${binary}" "${copy}"
        resign=0
        for arch in $(lipo -archs "${copy}"); do
            if codesign --verify --strict --architecture "${arch}" "${copy}" >/dev/null 2>&1; then
                continue
            fi
            details="$(codesign --display --architecture "${arch}" "${copy}" 2>&1 || true)"
            if [[ "${arch}" != arm64* ]] && grep -q 'not signed at all' <<<"${details}"; then
                continue
            fi
            resign=1
        done
        if [[ ${resign} == 1 ]]; then
            notice "$(basename "${binary}") has no valid signature after cmake --install; signing that program ad hoc"
            codesign --force --sign - "${copy}"
            codesign --verify --strict "${copy}"
            cp -p "${copy}" "${binary}"
        else
            echo "$(basename "${binary}"): linker signature kept"
        fi
    done
}

zip_app_bundle() {
    local app_bundle="$1"
    local archive="$2"
    local listing=""

    rm -f "${archive}"
    ditto -c -k --sequesterRsrc --keepParent "${app_bundle}" "${archive}"
    # --sequesterRsrc keeps extended attributes in __MACOSX/; a signed,
    # attribute-free bundle needs none, and other unzip tools extract them
    # as stray files. (Listings are read into a variable: with pipefail,
    # "unzip | grep -q" fails when grep stops reading early.)
    listing="$(unzip -Z -1 "${archive}")"
    if grep -q '^__MACOSX/' <<<"${listing}"; then
        zip -q -d "${archive}" '__MACOSX/*'
        listing="$(unzip -Z -1 "${archive}")"
    fi
    grep -qx 'die.app/Contents/MacOS/die' <<<"${listing}" || fail "die.app is missing from ${archive}"
    if grep -q '^__MACOSX/' <<<"${listing}"; then
        fail "${archive} still contains __MACOSX"
    fi
}

# build_installer <pkg root> <component plist> <work dir> <output> <signed: 0|1>
# Fails (returns non-zero) only when signing with X_PRIVATE_CERT_INSTALL
# fails, so the caller can build an unsigned package instead; a package with
# the wrong contents ends the script.
build_installer() {
    local pkg_root="$1"
    local component_plist="$2"
    local work="$3"
    local output="$4"
    local signed="$5"
    local component_pkg="${work}/cdie-component.pkg"
    local expanded="${work}/component-expanded"
    local pkg_sign_args=()
    local payload=""
    local package_info=""
    local relocate=""

    if [[ ${signed} == 1 ]]; then
        # The full certificate name resolve_signing found ("the name of the
        # identity" is what pkgbuild(1) documents), not the variable's text.
        pkg_sign_args=(--sign "${INSTALLER_IDENTITY_NAME}" --timestamp)
        if [[ -n "${KEYCHAIN_FILE}" ]]; then
            pkg_sign_args+=(--keychain "${KEYCHAIN_FILE}")
        fi
    fi
    rm -rf "${component_pkg}" "${expanded}" "${output}"

    pkgbuild \
        --root "${pkg_root}" \
        --component-plist "${component_plist}" \
        --identifier "${PKG_IDENTIFIER}" \
        --version "${RELEASE_VERSION}" \
        --install-location /Applications \
        ${pkg_sign_args[@]+"${pkg_sign_args[@]}"} \
        "${component_pkg}" || return 1
    payload="$(pkgutil --payload-files "${component_pkg}")" || fail "pkgutil cannot read ${component_pkg}"
    grep -qx './die.app/Contents/MacOS/die' <<<"${payload}" || fail "The component package does not install die.app"
    # Installed into /Applications and never relocated: PackageInfo lists no
    # bundle under <relocate>.
    pkgutil --expand "${component_pkg}" "${expanded}" || fail "pkgutil cannot expand ${component_pkg}"
    package_info="${expanded}/PackageInfo"
    grep -q 'install-location="/Applications"' "${package_info}" || fail "The package does not install into /Applications"
    relocate="$(sed -n '/<relocate>/,/<\/relocate>/p' "${package_info}")"
    if grep -q '<bundle' <<<"${relocate}"; then
        fail "die.app is still relocatable in ${component_pkg}"
    fi

    productbuild \
        --package "${component_pkg}" \
        ${pkg_sign_args[@]+"${pkg_sign_args[@]}"} \
        "${output}" || return 1
    [[ -s "${output}" ]] || fail "productbuild did not produce ${output}"
    if [[ ${signed} == 1 ]]; then
        pkgutil --check-signature "${output}" || return 1
    fi
}

# --------------------------------------------------------------- packages

SUMMARY=""

# build_package <label> <CMAKE_OSX_ARCHITECTURES> <lipo archs> <deployment target> [cmake args...]
build_package() {
    local label="$1"
    local cmake_archs="$2"
    local lipo_archs="$3"
    local deployment_target="$4"
    shift 4

    local package_basename="cdie_mac_${label}_${RELEASE_VERSION}"
    local package_work="${WORK_ROOT}/${label}"
    local build_dir="${package_work}/build"
    local stage_dir="${package_work}/stage"
    local pkg_root="${package_work}/pkgroot"
    local output_dir="${package_work}/output"
    local app_bundle="${stage_dir}/die.app"
    local zip_path="${output_dir}/${package_basename}.zip"
    local pkg_path="${output_dir}/${package_basename}.pkg"
    local component_plist="${package_work}/component.plist"
    local cmake_args=()
    local app_signed_with=""
    local app_state=""
    local pkg_signed_with=""
    local blocker=""
    local zip_state="not notarized"
    local pkg_state="not notarized"

    if [[ -n "${CDIE_BUILD_DIR:-}" ]]; then
        build_dir="${CDIE_BUILD_DIR%/}/${label}"
    fi
    mkdir -p "${package_work}" "${output_dir}"

    echo
    echo "=== ${package_basename}: ${cmake_archs}, macOS ${deployment_target} or newer, build tree ${build_dir}"

    cmake_args=(
        -S "${PROJECT_ROOT}"
        -B "${build_dir}"
        -DCMAKE_BUILD_TYPE=Release
        -DCDIE_BUILD_LIBRARY=OFF
        -DCDIE_BUILD_TESTS=OFF
        "-DCMAKE_OSX_ARCHITECTURES=${cmake_archs}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${deployment_target}"
    )
    if [[ -n "${BUILD_GENERATOR}" && ! -f "${build_dir}/CMakeCache.txt" ]]; then
        cmake_args+=(-G "${BUILD_GENERATOR}")
    fi
    if [[ -n "${CDIE_DATABASE_DIR:-}" ]]; then
        cmake_args+=("-DCDIE_DATABASE_DIR=${CDIE_DATABASE_DIR}")
    fi

    echo "Configuring macOS build (${label})..."
    cmake "${cmake_args[@]}" ${1+"$@"}

    echo "Building macOS Release (${label})..."
    # A bare --parallel means unlimited jobs to make; give it a number.
    cmake --build "${build_dir}" --config Release \
        --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

    # The installed bundle, not the build tree's: the package carries what
    # cmake --install produces.
    echo "Installing die.app (${label})..."
    rm -rf "${stage_dir}"
    cmake --install "${build_dir}" --config Release --prefix "${stage_dir}"

    [[ -x "${app_bundle}/Contents/MacOS/die" ]] || fail "Installed app bundle not found: ${app_bundle}"
    plutil -lint "${app_bundle}/Contents/Info.plist"
    if [[ ! -x "${app_bundle}/Contents/MacOS/die-update" ]]; then
        echo "warning: die-update was not built (xxnetlib/xxgithub sources missing?); the app has no database updater" >&2
    fi
    if [[ "${CDIE_DATABASE_DIR:-}" != "NONE" && -z "$(find "${app_bundle}/Contents/Resources/db" -type f -print -quit 2>/dev/null)" ]]; then
        fail "die.app has no signature database in Contents/Resources/db"
    fi
    check_architectures "${app_bundle}" "${lipo_archs}"

    # codesign refuses resource forks and Finder information in a bundle.
    xattr -cr "${app_bundle}"
    if [[ -n "${APP_IDENTITY_HASH}" ]]; then
        echo "Signing die.app with ${APP_IDENTITY_NAME} (${APP_IDENTITY_HASH})..."
        if sign_with_identity "${app_bundle}" "${package_work}/signing"; then
            app_signed_with="${APP_IDENTITY_NAME}"
            app_state="signed: ${APP_IDENTITY_NAME}"
        else
            notice_unusable "skipping Developer ID signing of die.app: codesign failed with ${APP_IDENTITY_NAME} (X_PRIVATE_CERT_APP)"
        fi
    fi
    if [[ -z "${app_signed_with}" ]]; then
        if [[ "${ADHOC_SIGN}" == 1 ]]; then
            echo "Sealing die.app ad hoc (no certificate involved)..."
            seal_ad_hoc "${app_bundle}"
            app_state="ad hoc seal"
        else
            echo "MACOS_ADHOC_SIGN=0: die.app is not signed; checking the linker signatures..."
            keep_linker_signatures "${app_bundle}" "${package_work}/linker-signatures"
            app_state="not signed (linker signatures)"
        fi
    fi

    echo "Creating ${package_basename}.zip..."
    zip_app_bundle "${app_bundle}" "${zip_path}"

    blocker="$(app_notarization_blocker "${app_signed_with}")"
    if [[ -z "${blocker}" ]]; then
        # A zip cannot be stapled: notarize it, staple the ticket into die.app
        # and zip the stapled app again. The .pkg below is built from it too.
        notarize_file "${zip_path}"
        staple_file "${app_bundle}"
        zip_app_bundle "${app_bundle}" "${zip_path}"
        zip_state="notarized, die.app stapled"
    else
        notarization_skipped "skipping notarization of ${package_basename}.zip: ${blocker}"
    fi

    rm -rf "${pkg_root}"
    mkdir -p "${pkg_root}"
    ditto "${app_bundle}" "${pkg_root}/die.app"

    # Without BundleIsRelocatable=false the installer "upgrades" any other copy
    # of com.ntinfo.cdie it finds (a die.app in Downloads, say) in place instead
    # of installing into /Applications. Older pkgbuild wrote the key as true,
    # newer ones (macOS 26+) leave it out, and PlistBuddy's Set fails on a
    # missing key: drop whatever is there and add the key.
    rm -f "${component_plist}"
    pkgbuild --analyze --root "${pkg_root}" "${component_plist}"
    /usr/libexec/PlistBuddy -c "Delete :0:BundleIsRelocatable" "${component_plist}" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :0:BundleIsRelocatable bool false" "${component_plist}"

    if [[ -n "${INSTALLER_IDENTITY_NAME}" ]]; then
        echo "Creating ${package_basename}.pkg, signed with ${INSTALLER_IDENTITY_NAME}..."
        if build_installer "${pkg_root}" "${component_plist}" "${package_work}" "${pkg_path}" 1; then
            pkg_signed_with="${INSTALLER_IDENTITY_NAME}"
        else
            notice_unusable "skipping signing of ${package_basename}.pkg: pkgbuild/productbuild failed with ${INSTALLER_IDENTITY_NAME} (X_PRIVATE_CERT_INSTALL); building it unsigned"
        fi
    fi
    if [[ -z "${pkg_signed_with}" ]]; then
        echo "Creating ${package_basename}.pkg (unsigned)..."
        build_installer "${pkg_root}" "${component_plist}" "${package_work}" "${pkg_path}" 0 ||
            fail "Could not build ${pkg_path}"
    fi

    blocker="$(pkg_notarization_blocker "${app_signed_with}" "${pkg_signed_with}")"
    if [[ -z "${blocker}" ]]; then
        notarize_file "${pkg_path}"
        staple_file "${pkg_path}"
        pkg_state="notarized and stapled"
    else
        notarization_skipped "skipping notarization of ${package_basename}.pkg: ${blocker}"
    fi

    cp -f "${zip_path}" "${RELEASE_DIR}/"
    cp -f "${pkg_path}" "${RELEASE_DIR}/"

    SUMMARY="${SUMMARY}  ${RELEASE_DIR}/${package_basename}.zip (die.app ${app_state}; ${zip_state})
  ${RELEASE_DIR}/${package_basename}.pkg (${pkg_signed_with:-unsigned}; ${pkg_state})
"
}

# ------------------------------------------------------------------- main

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

[[ "$(uname -s)" == "Darwin" ]] || fail "build_macos.sh must be run on macOS"

for argument in ${1+"$@"}; do
    case "${argument}" in
        -DCMAKE_OSX_ARCHITECTURES[=:]* | CMAKE_OSX_ARCHITECTURES[=:]*)
            fail "Pass architectures with MACOS_ARCHITECTURES, not ${argument}: every package is built for its own" ;;
    esac
done

case "${ADHOC_SIGN}" in
    0 | 1) ;;
    *) fail "MACOS_ADHOC_SIGN must be 0 or 1, not '${ADHOC_SIGN}'" ;;
esac

require_tool cmake
require_tool find
require_tool ditto
require_tool codesign
require_tool lipo
require_tool xattr
require_tool plutil
require_tool pkgbuild
require_tool productbuild
require_tool pkgutil
require_tool unzip
require_tool zip
[[ -x /usr/libexec/PlistBuddy ]] || fail "Required tool not found: /usr/libexec/PlistBuddy"

[[ -f "${PROJECT_ROOT}/release_version.txt" ]] || fail "release_version.txt not found: ${PROJECT_ROOT}/release_version.txt"
RELEASE_VERSION="$(head -n 1 "${PROJECT_ROOT}/release_version.txt" | tr -d '[:space:]')"

if [[ -n "${CDIE_DATABASE_DIR:-}" && "${CDIE_DATABASE_DIR}" != "NONE" && ! -d "${CDIE_DATABASE_DIR}/db" ]]; then
    fail "CDIE_DATABASE_DIR has no db directory: ${CDIE_DATABASE_DIR}"
fi

# The work root is deleted at the start and at the end of every run.
if [[ -n "${CDIE_BUILD_DIR:-}" ]] && path_is_inside "${CDIE_BUILD_DIR}" "${WORK_ROOT}"; then
    fail "CDIE_BUILD_DIR must not lie inside ${WORK_ROOT}, which every run deletes: ${CDIE_BUILD_DIR}"
fi
if path_is_inside "${RELEASE_DIR}" "${WORK_ROOT}"; then
    fail "CDIE_RELEASE_DIR must not lie inside ${WORK_ROOT}, which every run deletes: ${RELEASE_DIR}"
fi

parse_architectures

resolve_signing
resolve_notarization
if [[ -n "${APP_IDENTITY_HASH}" ]]; then
    echo "die.app: signature with ${APP_IDENTITY_NAME} (${APP_IDENTITY_HASH})"
elif [[ "${ADHOC_SIGN}" == 1 ]]; then
    echo "die.app: no Developer ID; ad hoc seal (MACOS_ADHOC_SIGN=1)"
else
    echo "die.app: no Developer ID; not signed, linker signatures kept (MACOS_ADHOC_SIGN=0)"
fi
echo ".pkg: ${INSTALLER_IDENTITY_NAME:-unsigned}"
if [[ -n "${NOTARY_MODE}" ]]; then
    echo "Notarization: credentials complete (${NOTARY_MODE}); used for packages signed with Developer ID"
else
    echo "Notarization: skipped, ${NOTARY_BLOCKER}"
fi

mkdir -p "${RELEASE_DIR}"

rm -rf "${WORK_ROOT}"
mkdir -p "${WORK_ROOT}"

package_index=0
while [[ ${package_index} -lt ${PACKAGE_COUNT} ]]; do
    build_package \
        "${PACKAGE_LABELS[${package_index}]}" \
        "${PACKAGE_CMAKE_ARCHS[${package_index}]}" \
        "${PACKAGE_LIPO_ARCHS[${package_index}]}" \
        "${PACKAGE_DEPLOYMENT_TARGETS[${package_index}]}" \
        ${1+"$@"}
    package_index=$((package_index + 1))
done

if [[ "${KEEP_WORK_ROOT:-0}" != "1" ]]; then
    rm -rf "${WORK_ROOT}"
fi

echo
echo "macOS packages created:"
printf '%s' "${SUMMARY}"
