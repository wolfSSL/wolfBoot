#!/bin/bash
#
# Switch the in-tree FSP set (UPD headers and FSP binaries) between QEMU,
# Tiger Lake and NAI; their generators all write the same paths.
#
# Usage:
#   select_fsp.sh save {tgl|qemu|nai} snapshot the current in-tree set
#   select_fsp.sh use  {tgl|qemu|nai} restore a snapshotted set into the tree
#   select_fsp.sh show                report what is cached and what is in-tree
#
set -e

CACHE="${WOLFBOOT_FSP_CACHE:-$HOME/.cache/wolfboot-fsp}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HDRS="FspUpd.h FspmUpd.h FspsUpd.h FsptUpd.h"
# Headers only some generators emit; each set restores only its own, so one
# cannot leak into another.
HDRS_OPT_ALL="MemInfoHob.h"
HDRS_OPT_tgl="MemInfoHob.h"
HDRS_OPT_nai="MemInfoHob.h"
BINS="fsp_t.bin fsp_m.bin fsp_s.bin"
# ucode0.bin is only produced by the TGL flow
EXTRA_tgl="ucode0.bin"
# NAI: the TGL flow with NAI's own FSP and microcode (nai_extract_fsp.sh)
EXTRA_nai="ucode0.bin"

usage() { sed -n '3,9p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

check_set() {
    case "$1" in
        tgl|qemu|nai) ;;
        *) echo "error: set must be 'tgl', 'qemu' or 'nai'" >&2; exit 1 ;;
    esac
}

do_save() {
    local s="$1" extra opt n=0
    eval "extra=\${EXTRA_$s:-}"
    eval "opt=\${HDRS_OPT_$s:-}"
    mkdir -p "$CACHE/$s"
    for f in $HDRS; do
        [ -f "$ROOT/include/x86/fsp/$f" ] || { echo "error: missing include/x86/fsp/$f" >&2; exit 1; }
        cp "$ROOT/include/x86/fsp/$f" "$CACHE/$s/"; n=$((n+1))
    done
    for f in $HDRS_OPT_ALL; do
        rm -f "$CACHE/$s/$f"
    done
    for f in $opt; do
        [ -f "$ROOT/include/x86/fsp/$f" ] && { cp "$ROOT/include/x86/fsp/$f" "$CACHE/$s/"; n=$((n+1)); }
    done
    for f in $BINS $extra; do
        [ -f "$ROOT/src/x86/$f" ] || { echo "error: missing src/x86/$f" >&2; exit 1; }
        cp "$ROOT/src/x86/$f" "$CACHE/$s/"; n=$((n+1))
    done
    echo "saved $n files to $CACHE/$s"
}

do_use() {
    local s="$1" extra opt n=0
    eval "extra=\${EXTRA_$s:-}"
    eval "opt=\${HDRS_OPT_$s:-}"
    [ -d "$CACHE/$s" ] || { echo "error: no snapshot for '$s'. Run the generator then 'select_fsp.sh save $s'." >&2; exit 1; }
    # Check every required file before copying any, so a partial cache fails
    # here rather than leaving a half-updated tree.
    for f in $HDRS $BINS $extra; do
        [ -f "$CACHE/$s/$f" ] || { echo "error: cached '$s' set is missing $f; re-run the generator then 'select_fsp.sh save $s'" >&2; exit 1; }
    done
    for f in $HDRS; do
        cp "$CACHE/$s/$f" "$ROOT/include/x86/fsp/"; n=$((n+1))
    done
    for f in $HDRS_OPT_ALL; do
        rm -f "$ROOT/include/x86/fsp/$f"
    done
    for f in $opt; do
        [ -f "$CACHE/$s/$f" ] && { cp "$CACHE/$s/$f" "$ROOT/include/x86/fsp/"; n=$((n+1)); }
    done
    for f in $BINS $extra; do
        cp "$CACHE/$s/$f" "$ROOT/src/x86/"; n=$((n+1))
    done
    echo "restored $n files of the '$s' FSP set"
}

do_show() {
    echo "cache: $CACHE"
    for s in tgl qemu nai; do
        if [ -d "$CACHE/$s" ]; then
            echo "  $s: $(ls "$CACHE/$s" | wc -l) files"
        else
            echo "  $s: (not snapshotted)"
        fi
    done
    echo "in-tree include/x86/fsp/FspmUpd.h:"
    if [ -f "$ROOT/include/x86/fsp/FspmUpd.h" ]; then
        # the TGL header is thousands of lines; the QEMU one is a small stub
        printf "  %s lines -> looks like %s\n" \
            "$(wc -l < "$ROOT/include/x86/fsp/FspmUpd.h")" \
            "$([ "$(wc -l < "$ROOT/include/x86/fsp/FspmUpd.h")" -gt 1000 ] && echo TGL || echo QEMU)"
    else
        echo "  (absent)"
    fi
}

[ $# -ge 1 ] || usage
case "$1" in
    save) [ $# -eq 2 ] || usage; check_set "$2"; do_save "$2" ;;
    use)  [ $# -eq 2 ] || usage; check_set "$2"; do_use  "$2" ;;
    show) do_show ;;
    *) usage ;;
esac
