#!/bin/sh
# route_through_sbom.sh - SBOM for IDE/SDK targets that build through the Makefile.
#
# Several "IDE" ecosystems supported by wolfBoot are, on the command line, just
# the normal Makefile build with a vendor toolchain and a target .config:
#
#   * TI Code Composer Studio (Hercules TMS570) -> make CCS_ROOT=.. F021_DIR=..
#   * Xilinx SDK / Vitis (Zynq / ZynqMP)        -> make TARGET=zynqmp ..
#   * Renesas RX (e2studio) via CCRX            -> make TARGET=rx72n RX_GCC..=..
#   * Microchip MPLAB X (nbproject makefiles)   -> make (generated makefile)
#
# For those, the SBOM is produced by the same `make sbom` engine as any other
# Make build - this wrapper just makes that explicit and self-documenting by
# staging a config and forwarding the vendor make variables.
#
# Usage:
#   route_through_sbom.sh --config config/examples/<file>.config [MAKE_VAR=VAL ...]
#   route_through_sbom.sh --no-config [MAKE_VAR=VAL ...]
#
# Everything after the flags is passed verbatim to `make sbom`, so the vendor
# variables (CCS_ROOT, F021_DIR, TARGET, ...) and SBOM overrides (GEN_SBOM,
# HOSTCC, ...) all work.
#
# Examples:
#   # TI Hercules (CCS toolchain, built from the command line):
#   route_through_sbom.sh --config config/examples/ti-tms570lc435.config \
#       CCS_ROOT=/opt/ti/ccs/tools/compiler/ti-cgt-arm_20.2.7.LTS \
#       F021_DIR=/opt/ti/Hercules/F021_Flash_API/02.01.01
#
#   # Xilinx ZynqMP:
#   route_through_sbom.sh --config config/examples/zynqmp.config
set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)

CONFIG=""
NO_CONFIG=0
while [ $# -gt 0 ]; do
    case "$1" in
        --config)    CONFIG="$2"; shift 2 ;;
        --no-config) NO_CONFIG=1; shift ;;
        -h|--help)   sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) break ;;
    esac
done

cd "$ROOT"

if [ "$NO_CONFIG" -ne 1 ]; then
    if [ -z "$CONFIG" ]; then
        echo "ERROR: pass --config <file> (or --no-config to use the current .config)." >&2
        exit 2
    fi
    if [ ! -f "$CONFIG" ]; then
        echo "ERROR: config not found: $CONFIG" >&2
        exit 1
    fi
    echo "Staging $CONFIG -> .config"
    cp "$CONFIG" .config
fi

echo "Running: make sbom $*"
exec make sbom "$@"
