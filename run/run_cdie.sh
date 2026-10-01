#!/bin/sh
# Convenience launcher: scans a target with the Detect It Easy databases.
#
#   ./run_cdie.sh <target> [extra cdie options]
#
# Set CDIE_DB_ROOT to the directory that holds db, db_extra and db_custom.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${CDIE_DB_ROOT:-}" ]; then
    if [ -d "$HERE/../dep/Detect-It-Easy/db" ]; then
        CDIE_DB_ROOT="$HERE/../dep/Detect-It-Easy"
    else
        CDIE_DB_ROOT="$HERE/../../_mylibs/Detect-It-Easy"
    fi
fi
CDIE_EXE="$HERE/../../cdie_build/src/console/cdie"

if [ ! -x "$CDIE_EXE" ]; then
    echo "cdie not found at $CDIE_EXE"
    echo "Build it first:  cmake -S . -B ../cdie_build && cmake --build ../cdie_build"
    exit 1
fi

exec "$CDIE_EXE" \
    -D "$CDIE_DB_ROOT/db" \
    -E "$CDIE_DB_ROOT/db_extra" \
    -C "$CDIE_DB_ROOT/db_custom" \
    "$@"
