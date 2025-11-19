#!/bin/bash

# cmake_dot_config.sh
#
# Example for using cmake with .config
#
# Reminder for WSL:
# git update-index --chmod=+x wolfboot_cmake_full_build.sh
# git commit -m "Make wolfboot_cmake_full_build.sh executable"
# git push

# Specify the executable shell checker you want to use:
MY_SHELLCHECK="shellcheck"

# Check if the executable is available in the PATH
if command -v "$MY_SHELLCHECK" >/dev/null 2>&1; then
    # Run your command here
    shellcheck "$0" || exit 1
else
    echo "$MY_SHELLCHECK is not installed. Please install it if changes to this script have been made."
fi

set -euo pipefail

# Begin common dir init, for /tools/scripts
# Resolve this script's absolute path and its directories
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${SCRIPT_DIR}/$(basename -- "${BASH_SOURCE[0]}")"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

# Normalize to physical paths (no symlinks, no trailing slashes)
# SCRIPT_DIR_P="$(cd -- "$SCRIPT_DIR" && pwd -P)"
REPO_ROOT_P="$(cd -- "$REPO_ROOT" && pwd -P)"
CALLER_CWD_P="$(pwd -P)"   # where the user ran the script from

# Print only if caller's cwd is neither REPO_ROOT nor REPO_ROOT/scripts
case "$CALLER_CWD_P" in
    "$REPO_ROOT_P" | "$REPO_ROOT_P"/scripts)
        : # silent
        ;;
    *)
        echo "Script paths:"
        echo "-- SCRIPT_PATH =$SCRIPT_PATH"
        echo "-- SCRIPT_DIR  =$SCRIPT_DIR"
        echo "-- REPO_ROOT   =$REPO_ROOT"
        ;;
esac

# Always work from the repo root, regardless of where the script was invoked
cd -- "$REPO_ROOT_P" || { printf 'Failed to cd to: %s\n' "$REPO_ROOT_P" >&2; exit 1; }
echo "Starting $0 from $(pwd -P)"

# End common dir init

# Regardless of where launches, we should not be at pwd=WOLFBOOT_ROOT

# Set some logging params
LOG_FILE="run.log"
KEYWORD="Config mode: dot"
echo "Saving output to $LOG_FILE"

echo "Fetch stm32h7 example .config"

SRC="./config/examples/stm32h7.config"
DST="./.config"

# Exit if the .config file already exists (perhaps it is valid? we will delete our copy when done here)
if [ -e "$DST" ]; then
    echo "ERROR: .config already exists! We need to copy a new test file. Delete or save existing file." >&2
    exit 1
fi

echo "Source config file: $SRC"
echo "Destination file:   $DST"

# Ensure source exists
if [ ! -f "$SRC" ]; then
    echo "ERROR: Source not found: $SRC" >&2
    exit 1
fi

echo "copy $SRC $DST"
cp -p "$SRC" "$DST" || { echo "ERROR: Copy failed." >&2; exit 1; }

# Verify destination exists and is non-empty
if [ ! -s "$DST" ]; then
    echo "ERROR: Destination missing after copy or file is empty: $DST" >&2
    exit 1
fi

# Optional: verify content matches exactly
if ! cmp -s "$SRC" "$DST"; then
    echo "ERROR: Destination content does not match source." >&2
    exit 1
fi

echo "OK: $DST created and verified."

cp   ./config/examples/stm32h7.config ./.config
ls   .config

echo ""
echo "This .config contents:"
cat  .config
echo ""

echo "Clean"
rm -rf ./build-stm32h7
cmake -S . -B build-stm32h7 \
  -DUSE_DOT_CONFIG=ON       \
  -DWOLFBOOT_TARGET=stm32h7 2>&1 | tee "$LOG_FILE" >/dev/tty

# Config dot-config mode
if grep -q -- "$KEYWORD" "$LOG_FILE"; then
    echo "Keyword found: $KEYWORD"
else
    echo "Keyword not found: $KEYWORD" >&2
    exit 1
fi

# Sample build
cmake --build build-stm32h7 -j10

rm .config

