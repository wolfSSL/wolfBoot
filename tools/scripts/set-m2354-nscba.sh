#!/bin/bash
#
# Provision the Nuvoton M2354 TrustZone secure/non-secure flash boundary.
#
# NSCBA is a flash config word at 0x00210800, not a register. It fixes how
# much of APROM the hardware treats as secure and only takes effect after a
# chip reset. wolfBoot never programs it; this is a one-time provisioning
# step, as the STM32 targets use set-stm32-tz-option-bytes.sh.
#
# Recovery: a full chip erase returns NSCBA to its erased state.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

set -e
set -o pipefail

SECURE_SIZE=${1:-0x80000}
TARGET=m2354kjfae
PROBE_ARG=""
[ -n "$M2354_PROBE" ] && PROBE_ARG="-u $M2354_PROBE"

SYS_REGLCTL=0x40000100
FMC_ISPCTL=0x4000C000
FMC_ISPADDR=0x4000C004
FMC_ISPDAT=0x4000C008
FMC_ISPCMD=0x4000C00C
FMC_ISPTRG=0x4000C010
FMC_NSCBA=0x00210800
SCU_FNSADDR=0x4002F028
SYS_IPRST0=0x40000008

ISPCTL_EN_CFG=0x11        # ISPEN | CFGUEN
ISPCMD_READ=0x00
ISPCMD_PAGE_ERASE=0x22
ISPCMD_PROGRAM=0x21
ISPTRG_GO=0x1

echo "Provisioning M2354 NSCBA: secure flash size = $SECURE_SIZE"
echo "  (APROM below $SECURE_SIZE is secure; at and above it is non-secure,"
echo "   seen by the non-secure world at its +0x10000000 alias)"
echo

# Read the current NSCBA first. Erasing an already-blank config page sets the
# ISP fail flag, so only erase when there is something to erase.
echo "Reading current NSCBA..."
CUR=$(pyocd cmd -t $TARGET $PROBE_ARG \
    -c "reset halt" \
    -c "write32 $SYS_REGLCTL 0x59" \
    -c "write32 $SYS_REGLCTL 0x16" \
    -c "write32 $SYS_REGLCTL 0x88" \
    -c "write32 $FMC_ISPCTL $ISPCTL_EN_CFG" \
    -c "write32 $FMC_ISPADDR $FMC_NSCBA" \
    -c "write32 $FMC_ISPCMD $ISPCMD_READ" \
    -c "write32 $FMC_ISPTRG $ISPTRG_GO" \
    -c "read32 $FMC_ISPDAT 4" 2>/dev/null \
    | grep -oiE '^4000c008: +[0-9a-f]+' | awk '{print tolower($2)}' || true)

# Refuse to act on an unparsed readback: an empty CUR would otherwise fall
# through to the erase-and-program path on a provisioning step.
if ! printf '%s' "$CUR" | grep -qE '^[0-9a-f]{8}$'; then
    echo "Error: could not read NSCBA back from the target (got '$CUR')." >&2
    echo "Check the probe selection and that the part is halted." >&2
    exit 1
fi
echo "  NSCBA currently reads 0x$CUR"

if [ "$CUR" = "$(printf '%08x' $((SECURE_SIZE)))" ]; then
    echo "  already provisioned to $SECURE_SIZE, nothing to do"
    exit 0
fi

if [ "$CUR" != "ffffffff" ]; then
    echo "Erasing the config page first..."
    pyocd cmd -t $TARGET $PROBE_ARG \
        -c "write32 $FMC_ISPADDR $FMC_NSCBA" \
        -c "write32 $FMC_ISPCMD $ISPCMD_PAGE_ERASE" \
        -c "write32 $FMC_ISPTRG $ISPTRG_GO" \
        -c "read32 $FMC_ISPCTL 4" >/dev/null
fi

echo "Programming NSCBA = $SECURE_SIZE ..."
pyocd cmd -t $TARGET $PROBE_ARG \
    -c "write32 $FMC_ISPADDR $FMC_NSCBA" \
    -c "write32 $FMC_ISPDAT $SECURE_SIZE" \
    -c "write32 $FMC_ISPCMD $ISPCMD_PROGRAM" \
    -c "write32 $FMC_ISPTRG $ISPTRG_GO" \
    -c "read32 $FMC_ISPCTL 4"
echo "  ISPCTL above: bit 6 (0x40) set would mean the program failed."

echo
echo "Issuing a chip reset so the new boundary latches."
echo "The debugger link drops here; 'memory transfer failed' is expected."
pyocd cmd -t $TARGET $PROBE_ARG \
    -c "write32 $SYS_REGLCTL 0x59" \
    -c "write32 $SYS_REGLCTL 0x16" \
    -c "write32 $SYS_REGLCTL 0x88" \
    -c "write32 $SYS_IPRST0 0x1" 2>&1 | grep -v "memory transfer failed" || true

echo
echo "Live boundary after reset:"
pyocd cmd -t $TARGET $PROBE_ARG -c "reset halt" -c "read32 $SCU_FNSADDR 4" 2>/dev/null | tail -1
echo "SCU->FNSADDR should read $SECURE_SIZE."
echo "wolfBoot's hal_init() checks this value and panics on a mismatch."
