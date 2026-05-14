#!/bin/bash
# CW VPX3-152 wolfBoot flash-and-boot test cycle
#
# Automates: enter PABS, flash wolfBoot + signed app, release PABS, boot.
# Requires:
#   - wolfboot.bin and test-app/image_v1_signed.bin in /srv/tftp/
#   - Pi4 GPIO 16 wired to JB1 (PABS jumper), GPIO 20 = power gate
#   - VPX3-152 UART on /tmp/uart-monitor/pty/ttyUSB9 @115200 (override
#     with BOARD_TTY=ttyUSB13 / ttyUSB15 if it has re-enumerated)
#
# Usage: BOARD_TTY=ttyUSB9 ./cw_vpx3152_flash_test.sh [--flash-only|--boot-only|--full]
#        --flash-only : enter PABS, flash, exit
#        --boot-only  : release PABS, power cycle, monitor UART
#        --full       : both (default)

set -e

MODE="${1:---full}"
BOARD_TTY=${BOARD_TTY:-ttyUSB9}
PTY_BOARD=/tmp/uart-monitor/pty/$BOARD_TTY
LOG_BOARD=/tmp/uart-monitor/latest/$BOARD_TTY.log
PI=${PI:-pi@Pi4}
GPIO_PWR=20
GPIO_PABS=16
TFTP_DIR=/srv/tftp

# Helper: send U-Boot command to board
ub_cmd() {
    printf '%s\r' "$1" > "$PTY_BOARD"
    sleep "${2:-1}"
}

# Helper: power off/on via Pi4 GPIO 20 (current rig - PSU stays on,
# GPIO 20 gates power to the board)
psu_off() { ssh "$PI" "raspi-gpio set $GPIO_PWR op dl" >/dev/null; }
psu_on()  { ssh "$PI" "raspi-gpio set $GPIO_PWR op dh" >/dev/null; }

# Helper: enter PABS mode
enter_pabs() {
    echo "[*] Entering PABS mode..."
    ssh "$PI" "raspi-gpio set $GPIO_PABS op dh" >/dev/null
    sleep 0.5
    uart-monitor clear "$BOARD_TTY" >/dev/null
    psu_off; sleep 4
    psu_on; sleep 25
    if ! grep -q "PABS=>" "$LOG_BOARD" 2>/dev/null; then
        echo "[!] PABS prompt not detected"
        tail -5 "$LOG_BOARD"
        return 1
    fi
    echo "[*] PABS prompt ready"
}

# Helper: flash wolfBoot + app via PABS
flash_via_pabs() {
    echo "[*] Configuring network and flashing..."
    ub_cmd "setenv serverip 10.0.4.24" 0.3
    ub_cmd "setenv ipaddr 10.0.4.152"  0.3
    ub_cmd "setenv netmask 255.255.255.0" 0.3
    ub_cmd "setenv ethact FM1@DTSEC2" 0.3

    echo "[*] Flashing wolfBoot..."
    ub_cmd "tftp 0x1000000 wolfboot.bin" 15
    ub_cmd "erase 0x8FFE0000 +0x20000" 5
    ub_cmd "cp.b 0x1000000 0x8FFE0000 0x20000" 10

    echo "[*] Flashing signed app..."
    ub_cmd "tftp 0x1000000 image_v1_signed.bin" 10
    ub_cmd "erase 0x8FEE0000 +0x100000" 10
    ub_cmd "cp.b 0x1000000 0x8FEE0000 \$filesize" 5
    echo "[*] Flash complete"
}

# Helper: release PABS and power cycle to boot wolfBoot
boot_wolfboot() {
    echo "[*] Releasing PABS, power cycling..."
    ssh "$PI" "raspi-gpio set $GPIO_PABS op dl" >/dev/null
    sleep 0.5
    uart-monitor clear "$BOARD_TTY" >/dev/null
    psu_off; sleep 4
    psu_on
    echo "[*] Monitoring UART (up to 3 min)..."

    # Wait for terminal markers (success, failure, or exception)
    TIMEOUT=180
    START=$(date +%s)
    while [ $(($(date +%s) - START)) -lt $TIMEOUT ]; do
        # Terminal success markers (anything post-wolfBoot-boot)
        if grep -qa "idle loop\|Tests passed\|Tests failed\|Benchmarks complete\|Erase Sector failed\|Write Page: Ret\|Check Data" "$LOG_BOARD" 2>/dev/null; then
            sleep 5  # let final output flush
            break
        fi
        # Exception marker
        if grep -qa "^!$\|panic" "$LOG_BOARD" 2>/dev/null; then
            sleep 2
            break
        fi
        sleep 3
    done

    echo "[*] UART output (readable chars only):"
    strings "$LOG_BOARD" | tail -50
    echo ""
    echo "[*] Last bytes (hex):"
    xxd "$LOG_BOARD" | tail -3
}

# Copy latest build artifacts to TFTP dir
if [ -f wolfboot.bin ] && [ -f test-app/image_v1_signed.bin ]; then
    echo "[*] Copying artifacts to $TFTP_DIR"
    cp wolfboot.bin "$TFTP_DIR/"
    cp test-app/image_v1_signed.bin "$TFTP_DIR/"
fi

case "$MODE" in
    --flash-only) enter_pabs && flash_via_pabs ;;
    --boot-only)  boot_wolfboot ;;
    --full|*)     enter_pabs && flash_via_pabs && boot_wolfboot ;;
esac
