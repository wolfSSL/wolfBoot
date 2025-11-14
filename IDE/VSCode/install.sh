#!/bin/bash

# From an already-cloned wolfBoot:
#
#   ./IDE/VSCode/install.sh
#
# or
#   cd IDE/VSCode
#   ./install.sh
#
# VSCode can be installed:
#
#   https://code.visualstudio.com/download
#
# Or:
#
#   sudo snap install --classic code

# Begin common dir init, for [WOLFBOOT_ROOT]/IDE/VSCode
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

pwd
exit 0


git clone https://github.com/gojimmypi/wolfBoot.git
cd wolfBoot


sudo apt-get update
sudo apt-get install -y pass gnupg2
sudo apt-get install -y autoconf automake libtool
sudo apt-get install -y make
sudo apt-get install -y g++
sudo apt-get install -y gawk
sudo apt-get install -y build-essential clang clang-tidy
sudo apt-get install -y cppcheck shellcheck valgrind
sudo apt-get install -y pcre2-utils   # provides pcre2grep
sudo apt-get install -y qemu-user qemu-system-x86
sudo apt-get install -y wine          # or wine64 on 64-bit

sudo apt-get install cmake
sudo apt-get install ninja-build

cd IDE/VSCode

sudo apt-get install -y snapd
sudo snap install --classic code

code wolfBoot.code-workspace
