#!/usr/bin/env bash
# load.sh - flash the wolfHSM TrustZone demo to a NUCLEO-H563ZI and
#           optionally drop the user into a serial console.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

set -euo pipefail

# Resolve paths relative to this script regardless of how it is invoked.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/out"
MANIFEST="${OUT_DIR}/manifest.env"

# Tools. Override via env var if installed elsewhere.
STM32CLI="${STM32_PROGRAMMER_CLI:-STM32_Programmer_CLI}"

# Serial port for the NUCLEO-H563ZI's onboard ST-LINK V3E VCP.
# Linux: /dev/ttyACM0 by default. macOS: /dev/tty.usbmodem*. Override
# via SERIAL_PORT env var.
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
SERIAL_BAUD="${SERIAL_BAUD:-115200}"

# Whether to open a serial console after flashing. Default on; set
# OPEN_SERIAL=0 to skip (useful for unattended runs).
OPEN_SERIAL="${OPEN_SERIAL:-1}"

log() { echo "==> $*"; }
die() { echo "error: $*" >&2; exit 1; }

# Pull the addresses and binary paths produced by `make stage`. Failing
# fast here gives a clearer error than letting STM32_Programmer_CLI
# complain about a missing file.
[[ -f "${MANIFEST}" ]] || die "${MANIFEST} not found. Run 'make' in this directory first."

# shellcheck disable=SC1090
source "${MANIFEST}"

[[ -f "${WOLFBOOT_BIN}" ]] || die "WOLFBOOT_BIN missing: ${WOLFBOOT_BIN}"
[[ -f "${TEST_APP_BIN}" ]] || die "TEST_APP_BIN missing: ${TEST_APP_BIN}"

command -v "${STM32CLI}" >/dev/null 2>&1 || \
    die "${STM32CLI} not on PATH. Install STM32CubeProgrammer or set STM32_PROGRAMMER_CLI."

# Two-stage flash:
#  1. wolfboot.bin -> 0x08000000 (secure side base flash).
#  2. image_v1_signed.bin -> BOOT_ADDR (non-secure side test app slot).
#
# Run a mass-erase first so we never inherit stale partitions from a
# previous run (PKCS11/PSA/fwTPM configs share the same flash range).
log "Mass erase + programming wolfboot.bin to 0x08000000"
"${STM32CLI}" -c port=SWD reset=HWrst -e all -d "${WOLFBOOT_BIN}" 0x08000000 -v

log "Programming test app image to ${BOOT_ADDR}"
"${STM32CLI}" -c port=SWD reset=HWrst -d "${TEST_APP_BIN}" "${BOOT_ADDR}" -v

log "Hardware reset"
"${STM32CLI}" -c port=SWD reset=HWrst -hardRst

if [[ "${OPEN_SERIAL}" != "1" ]]; then
    log "OPEN_SERIAL=0, skipping serial monitor"
    exit 0
fi

# Pick the first available serial monitor. picocom is preferred (clean
# exit with Ctrl-A Ctrl-X); fall back to screen if not installed.
if command -v picocom >/dev/null 2>&1; then
    log "Opening picocom on ${SERIAL_PORT} @ ${SERIAL_BAUD}-8N1 (Ctrl-A Ctrl-X to exit)"
    exec picocom -b "${SERIAL_BAUD}" "${SERIAL_PORT}"
elif command -v screen >/dev/null 2>&1; then
    log "Opening screen on ${SERIAL_PORT} @ ${SERIAL_BAUD} (Ctrl-A k to exit)"
    exec screen "${SERIAL_PORT}" "${SERIAL_BAUD}"
else
    log "No serial monitor found. Install picocom or screen, or set OPEN_SERIAL=0."
    log "To watch the demo manually: <your-tool> ${SERIAL_PORT} ${SERIAL_BAUD}"
    exit 0
fi
