#!/bin/bash
#
# PolarFire SoC boot mode 2 programming helper.
#
# Two jobs mpfsBootmodeProgrammer cannot do on its own:
#
#   mpfs_bootmode2.sh [options] wolfboot.elf
#     Program the User Boot Loader (the TARGET=mpfs250_ubl build) into sNVM
#     and select boot mode 2 on a die the programmer does not list (the
#     secure "S" parts, e.g. MPFS250TS, which an authenticated sNVM client
#     needs).  The programmer is run in --dryrun to produce the UBL image
#     (its 60-byte header, SHA-256 and Intel hex) and the USK; the fpgenprog
#     steps it logged are replayed in a project for the requested die.
#
#   mpfs_bootmode2.sh --envm [options] wolfboot_envm_signed.elf
#     Program the eNVM only (the signed wolfBoot and its manifest) and leave
#     the boot mode and sNVM alone, i.e. update the eNVM wolfBoot under boot
#     mode 2.  The programmer always selects boot mode 1 when it programs
#     eNVM, so its envm_client step is replayed without the boot-mode step.
#
# Boot mode 1 is restored with the programmer itself:
#   java -jar mpfsBootmodeProgrammer.jar --bootmode 1 --die MPFS250T ... wolfboot.elf
#
# Options:
#   --die DIE         fpgenprog die (default MPFS250TS)
#   --package PKG     package (default FCG1152)
#   --snvm-page N     first sNVM page of the UBL (default 0)
#   --usk KEY         12-byte hex USK, or 0 for a random one (default 0)
#   --encrypt         store the UBL as authenticated ciphertext
#   --regenerate-smk  also generate a new sNVM Master Key.  The authenticated
#                     sNVM client the UBL lives in cannot be created on a device
#                     whose SMK has never been initialized, and the programmer
#                     never asks for one.  A new SMK invalidates every existing
#                     authenticated or encrypted sNVM client on the device.
#   --programmer ID   fpgenprog programmer, as it names it (e.g. CA148625).  With
#                     no id fpgenprog selects EVERY attached programmer, and an
#                     idle second board fails the scan chain and the whole run.
#                     Default: the Embedded FP6's USB serial, when exactly one
#                     is attached.
#   --workdir DIR     output directory (default: current directory)
#   --dryrun          generate everything, do not program
#
# Requires SC_INSTALL_DIR (SoftConsole) and FPGENPROG (Libero fpgenprog).

set -e

DIE=MPFS250TS
PACKAGE=FCG1152
SNVM_PAGE=0
USK=0
ENCRYPT=""
SMK=0
WORKDIR="$PWD"
PROGRAMMER=
DRYRUN=0
ENVM=0

while [ $# -gt 1 ]; do
    case "$1" in
        --die) DIE="$2"; shift 2 ;;
        --package) PACKAGE="$2"; shift 2 ;;
        --snvm-page) SNVM_PAGE="$2"; shift 2 ;;
        --usk) USK="$2"; shift 2 ;;
        --encrypt) ENCRYPT="--encrypt"; shift ;;
        --regenerate-smk) SMK=1; shift ;;
        --programmer) PROGRAMMER="$2"; shift 2 ;;
        --workdir) WORKDIR="$2"; shift 2 ;;
        --dryrun) DRYRUN=1; shift ;;
        --envm) ENVM=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done
ELF="${1:-}"
[ -n "$ELF" ] && [ -f "$ELF" ] || { echo "usage: $0 [--envm] [options] wolfboot.elf" >&2; exit 1; }
[ -n "${SC_INSTALL_DIR:-}" ] || { echo "SC_INSTALL_DIR is not set" >&2; exit 1; }
[ -x "${FPGENPROG:-}" ] || { echo "FPGENPROG is not an executable" >&2; exit 1; }

# fpgenprog names an Embedded FP6 by its bare USB serial.  Pin it by serial
# rather than by hub path: the path moves on every recable.
if [ -z "$PROGRAMMER" ]; then
    for d in /sys/bus/usb/devices/*; do
        [ "$(cat "$d/idVendor" 2>/dev/null)" = "1514" ] || continue
        [ "$(cat "$d/idProduct" 2>/dev/null)" = "200b" ] || continue
        sn=$(cat "$d/serial" 2>/dev/null)
        [ -n "$sn" ] || continue
        if [ -n "$PROGRAMMER" ]; then
            echo "several Embedded FP6 probes attached; pass --programmer" >&2
            exit 1
        fi
        PROGRAMMER="$sn"
    done
fi
[ -n "$PROGRAMMER" ] || { echo "no Embedded FP6 (1514:200b) found; pass --programmer" >&2; exit 1; }

JAVA="$SC_INSTALL_DIR/eclipse/jre/bin/java"
JAR="$SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar"
# The programmer knows the non-secure die names only; it is just generating
# the image here, so hand it the base die.
JAR_DIE="${DIE%S}"
mkdir -p "$WORKDIR"
WORKDIR="$(cd "$WORKDIR" && pwd)"
NAME="$(basename "$ELF" .elf)"
# The programmer takes the ELF relative to its working directory.
cmp -s "$ELF" "$WORKDIR/$NAME.elf" 2>/dev/null || cp "$ELF" "$WORKDIR/$NAME.elf"

if [ "$ENVM" = 1 ]; then
    MODE=1
    STEP=envm_client
    GEN="$WORKDIR/bootmode1"
    JAR_ARGS="--bootmode 1"
else
    MODE=2
    STEP=snvm_client
    GEN="$WORKDIR/bootmode2"
    JAR_ARGS="--bootmode 2 --snvm_page $SNVM_PAGE --usk $USK $ENCRYPT"
fi
PROJ="$WORKDIR/bootmode${MODE}_$DIE/fpgenprogProject"
LOG="$GEN/debugLog.txt"

echo "[1/3] generating the image with mpfsBootmodeProgrammer (dry run)"
# Boot mode 2 on a non-secure die fails at bitstream generation; the image
# and the USK are already written by then.
# shellcheck disable=SC2086
"$JAVA" -jar "$JAR" $JAR_ARGS --die "$JAR_DIE" --package "$PACKAGE" --dryrun \
    --workdir "$WORKDIR" "$NAME.elf" > "$WORKDIR/bootmode${MODE}_jar.log" 2>&1 || true
[ -f "$LOG" ] && grep -q "fpgenprog $STEP " "$LOG" || {
    echo "mpfsBootmodeProgrammer produced no image; see $WORKDIR/bootmode${MODE}_jar.log" >&2
    exit 1
}

# The exact arguments the programmer used for a step, pointed at the new project.
cmd_args() {
    grep -o "fpgenprog $1 [^\"]*" "$LOG" | tail -1 | sed "s/^fpgenprog $1 //" | \
        sed "s|--location [^ ]*|--location $PROJ|"
}
CLIENT_ARGS="$(cmd_args $STEP)"
if [ "$SMK" = 1 ]; then
    [ "$MODE" = 2 ] || { echo "--regenerate-smk applies to boot mode 2 only" >&2; exit 1; }
    echo "note: generating a new sNVM Master Key; authenticated sNVM clients"
    echo "      already on this device will no longer authenticate"
    CLIENT_ARGS="$CLIENT_ARGS --regenerate_smk"
fi
BOOT_INFO_ARGS=""
[ "$MODE" = 2 ] && BOOT_INFO_ARGS="$(cmd_args mss_boot_info)"

echo "[2/3] generating the bitstream for $DIE"
rm -rf "$PROJ"
mkdir -p "$(dirname "$PROJ")"
"$FPGENPROG" new_project --location "$PROJ" --target_die "$DIE" --target_package "$PACKAGE"
if [ -n "$BOOT_INFO_ARGS" ]; then
    # shellcheck disable=SC2086
    "$FPGENPROG" mss_boot_info $BOOT_INFO_ARGS
fi
# shellcheck disable=SC2086
"$FPGENPROG" $STEP $CLIENT_ARGS
"$FPGENPROG" generate_bitstream --location "$PROJ"

if [ "$MODE" = 2 ]; then
    USKFILE="$(ls "$GEN"/"$NAME"-bm2-usk.txt 2>/dev/null | head -1)"
    [ -n "$USKFILE" ] && echo "USK (keep it: a UBL update needs it): $USKFILE"
fi
if [ "$DRYRUN" = 1 ]; then
    echo "[3/3] dry run: not programming"
    exit 0
fi
echo "[3/3] programming"
"$FPGENPROG" run_action --location "$PROJ" --action PROGRAM \
    --programmer_id "$PROGRAMMER"
if [ "$MODE" = 2 ]; then
    echo "done: boot mode 2, UBL in sNVM from page $SNVM_PAGE"
else
    echo "done: eNVM programmed, boot mode unchanged"
fi
