#!/usr/bin/env bash
#
# keystore_file_check.sh
# Find files with the same name as given targets in any other local branch.

echo "$0 v1.0"
echo ""

# Specify the executable shell checker you want to use:
MY_SHELLCHECK="shellcheck"

# Check if the executable is available in the PATH
if command -v "$MY_SHELLCHECK" >/dev/null 2>&1; then
    # Run your command here
    shellcheck "$0" || exit 1
else
    echo "$MY_SHELLCHECK is not installed. Please install it if changes to this script have been made."
fi

if git --version >/dev/null 2>&1; then
    # Run your command here
    echo "Confirmed git is installed: $(git --version)"
else
    echo "git is not installed. Please install it to use this script."  >&2
    exit 1
fi


set -euo pipefail

# Defaults for wolfBoot, used when no args are given
DEFAULTS=(
    "wolfboot_signing_private_key.der"
    "keystore.der"
    "keystore.c"
)

usage() {
    echo "Usage: $0 [file1 [file2 ...]]"
    echo "If no files are provided, the defaults are:"
    printf "  %s\n" "${DEFAULTS[@]}"
}


# --------------------------------------------------------------------------------------------
# Begin common section to start at repo root, for /tools/scripts
# --------------------------------------------------------------------------------------------
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
# --------------------------------------------------------------------------------------------
# End common section to start at repo root, for /tools/scripts
# --------------------------------------------------------------------------------------------





# Resolve targets
if [ "$#" -eq 0 ]; then
    TARGETS=("${DEFAULTS[@]}")
else
    TARGETS=("$@")
fi

# Ensure we are in a git repo
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "Error: not inside a git repository."
    exit 2
fi

# Determine current branch (may be detached)
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")"

# Get list of local branches
mapfile -t BRANCHES < <(git for-each-ref --format="%(refname:short)" refs/heads/)

# Filter out current branch if named
OTHER_BRANCHES=()
for b in "${BRANCHES[@]}"; do
    if [ "$CURRENT_BRANCH" != "HEAD" ] && [ -n "$CURRENT_BRANCH" ]; then
        if [ "$b" != "$CURRENT_BRANCH" ]; then
            OTHER_BRANCHES+=("$b")
        fi
    else
        # Detached HEAD, consider all branches
        OTHER_BRANCHES+=("$b")
    fi
done

# Helper to list and count files with this basename in the current working tree
# Includes tracked, untracked, and ignored files (fast: no disk crawl)
list_and_count_in_current_branch() {
    local base="$1"
    local count=0
    local printed=0
    # Collect working-tree paths: tracked + untracked + ignored, unique
    while IFS= read -r path; do
        name="${path##*/}"
        if [ "$name" = "$base" ]; then
            if [ $printed -eq 0 ]; then
                echo "Paths in current branch ${CURRENT_BRANCH:-(detached)}:"
                printed=1
            fi
            echo "  ./$path"
            count=$((count + 1))
        fi
    done < <( { git ls-files -co --exclude-standard; git ls-files -i --others --exclude-standard; } | sort -u )
    echo "$count"
}


exit_code=0

for target in "${TARGETS[@]}"; do
    base="${target##*/}"

    echo "=== Searching for name: ${base} ==="

    # Report presence count on current branch
    cur_count="$(list_and_count_in_current_branch  "$base")"
    echo "Current branch ${CURRENT_BRANCH:-(detached)} has ${cur_count} file(s) named ${base}"

    # Search other branches
    found_any=0
    for br in "${OTHER_BRANCHES[@]}"; do
        # List all names in branch and print matches by basename
        while IFS= read -r path; do
            name="${path##*/}"
            if [ "$name" = "$base" ]; then
                if [ "$found_any" -eq 0 ]; then
                    echo "Matches in other branches:"
                    found_any=1
                fi
                echo "  ${br}:${path}"
            fi
        done < <(git ls-tree -r --name-only "$br")
    done

    if [ "$found_any" -eq 0 ]; then
        echo "No matches in other branches."
    else
        exit_code=1
    fi

    echo
done

exit "$exit_code"
