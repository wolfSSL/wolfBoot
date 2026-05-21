#!/bin/bash
# Flash a built CW VPX3-152 main-bank U-Boot via PABS fwupd, then boot it.
#
# Prerequisite: a built `u-boot.bin` (CW152 variant, 786432 bytes) staged at
#   /srv/tftp/608603-140_rev-_u-boot.bin
# (with the FMAN microcode at /srv/tftp/608603-140_rev-_fman.bin -- already
#  present from prior runs).
#
# This is non-destructive to the PABS bank; it only rewrites the main bank
# (where wolfBoot was previously). To restore production U-Boot, copy
# /srv/tftp/608603-140_rev-_u-boot.bin.orig back over the staging name and
# re-run this script.

set -e

# UART can re-enumerate across reboots; default to ttyUSB9 (current) but
# allow override. Prior runs used ttyUSB13/15 -- check `uart-monitor status`
# if unsure.
BOARD_TTY=${BOARD_TTY:-ttyUSB9}
PTY_BOARD=/tmp/uart-monitor/pty/$BOARD_TTY
LOG_BOARD=/tmp/uart-monitor/latest/$BOARD_TTY.log
PI=${PI:-pi@Pi4}
GPIO_PWR=20
GPIO_PABS=16

ub_cmd() {
    printf '%s\r' "$1" > "$PTY_BOARD"
    sleep "${2:-0.4}"
}

ub_wait() {
    local cmd="$1" pat="$2" timeout="${3:-30}"
    local start_pos
    start_pos=$(wc -c < "$LOG_BOARD" 2>/dev/null || echo 0)
    printf '%s\r' "$cmd" > "$PTY_BOARD"
    local s=$SECONDS
    while [ $((SECONDS - s)) -lt "$timeout" ]; do
        if tail -c +"$((start_pos + 1))" "$LOG_BOARD" 2>/dev/null | grep -qa "$pat"; then
            return 0
        fi
        sleep 0.3
    done
    echo "[!] timeout waiting for '$pat' after: $cmd" >&2
    return 1
}

psu_off() { ssh "$PI" "raspi-gpio set $GPIO_PWR op dl" >/dev/null; }
psu_on()  { ssh "$PI" "raspi-gpio set $GPIO_PWR op dh" >/dev/null; }

echo "[*] Stage check:"
ls -la /srv/tftp/608603-140_rev-_u-boot.bin /srv/tftp/608603-140_rev-_fman.bin

echo "[*] Entering PABS mode..."
ssh "$PI" "raspi-gpio set $GPIO_PABS op dh" >/dev/null
sleep 0.5
uart-monitor clear "$BOARD_TTY" >/dev/null
psu_off; sleep 4
psu_on; sleep 25
if ! grep -q "PABS=>" "$LOG_BOARD" 2>/dev/null; then
    echo "[!] PABS prompt not detected"
    tail -5 "$LOG_BOARD"
    exit 1
fi

echo "[*] Configuring TFTP..."
ub_cmd "setenv serverip 10.0.4.24"   0.3
ub_cmd "setenv ipaddr 10.0.4.152"    0.3
ub_cmd "setenv netmask 255.255.255.0" 0.3
ub_cmd "setenv ethact FM1@DTSEC2"    0.3

echo "[*] Running fwupd 608603-140_rev- (writes main-bank U-Boot)..."
# Real completion message is "Images successfully updated"; also accept
# the followup "Please reset card" or a panic. Failing earlier wait
# patterns ("Done", "fwupd: ") never matched fwupd's actual output, so
# the script idled the full timeout (~240 s) on every successful flash.
ub_wait "fwupd 608603-140_rev-" \
    "Images successfully updated\|Please reset card\|FAIL\|ERROR" 90 \
    || echo "[!] fwupd did not report completion within 90 s"
sleep 1

echo "[*] Releasing PABS jumper, power-cycling..."
ssh "$PI" "raspi-gpio set $GPIO_PABS op dl" >/dev/null
sleep 0.5
uart-monitor clear "$BOARD_TTY" >/dev/null
psu_off; sleep 4
psu_on

echo "[*] Waiting for VPX3-152 prompt (debug build)..."
START=$(date +%s)
TIMEOUT=60
while [ $(($(date +%s) - START)) -lt $TIMEOUT ]; do
    if grep -qa "VPX3-152=>" "$LOG_BOARD"; then
        echo "[*] Got main U-Boot prompt"
        echo
        echo "=== Last 40 strings ==="
        strings "$LOG_BOARD" | tail -40
        exit 0
    fi
    sleep 1
done

echo "[!] Did not see VPX3-152=> prompt in $TIMEOUT s"
strings "$LOG_BOARD" | tail -40
exit 2
