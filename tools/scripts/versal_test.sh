#!/bin/bash
# Build, flash QSPI, and boot VMK180 - all in one script
#
# Usage:
#   ./versal_test.sh               # Full build, flash, and boot wolfBoot
#   ./versal_test.sh --test-app    # Full build + flash test app to boot partition
#   ./versal_test.sh --test-update # Full build + flash test app v2 to update partition
#   ./versal_test.sh --boot-sdcard # Test SD card boot mode only
#   ./versal_test.sh --boot-qspi   # Test QSPI boot mode only
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "$WOLFBOOT_ROOT"

# Config
UART_PORT="${UART_PORT:-/dev/ttyUSB2}"
UART_BAUD="${UART_BAUD:-115200}"
SERVER_IP="${SERVER_IP:-10.0.4.24}"
BOARD_IP="${BOARD_IP:-10.0.4.90}"
TFTP_DIR="${TFTP_DIR:-/srv/tftp}"
VITIS_PATH="${VITIS_PATH:-/opt/Xilinx/Vitis/2024.2}"
RELAY_PORT="${RELAY_PORT:-/dev/ttyACM1}"
UART_LOG="${UART_LOG:-${WOLFBOOT_ROOT}/uart_log.txt}"
LINUX_IMAGES_DIR="${LINUX_IMAGES_DIR:-}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_ok() { echo -e "${GREEN}[OK]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Check for required tools
for cmd in expect socat; do
    command -v "$cmd" &>/dev/null || { log_error "$cmd not found - install with: sudo apt install $cmd"; exit 1; }
done

# Load configuration from .config file
load_config() {
    local config_file="${1:-.config}"
    [ ! -f "$config_file" ] && { log_error "Config file not found: $config_file"; return 1; }

    while IFS= read -r line; do
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "${line// }" ]] && continue
        if [[ "$line" =~ ^[[:space:]]*([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*(\?)?=[[:space:]]*(.*)$ ]]; then
            local key="${BASH_REMATCH[1]}" conditional="${BASH_REMATCH[2]}" value="${BASH_REMATCH[3]}"
            value="${value#\"}"; value="${value%\"}"; value="${value%"${value##*[![:space:]]}"}"
            if [ -n "$conditional" ]; then
                [ -z "${!key:-}" ] && declare -g "${key}=${value}"
            else
                declare -g "${key}=${value}"
            fi
        fi
    done < <(grep -E '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(\?)?=' "$config_file" 2>/dev/null || true)

    export IMAGE_HEADER_SIZE SIGN HASH SECONDARY_SIGN_OPTIONS SECONDARY_PRIVATE_KEY

    case "${SIGN:-}" in
        ECC256) IMAGE_SIGNATURE_SIZE=64 ;;
        ECC384) IMAGE_SIGNATURE_SIZE=96 ;;
        ECC521) IMAGE_SIGNATURE_SIZE=132 ;;
        ED25519) IMAGE_SIGNATURE_SIZE=64 ;;
        ED448) IMAGE_SIGNATURE_SIZE=114 ;;
        RSA2048) IMAGE_SIGNATURE_SIZE=256 ;;
        RSA3072) IMAGE_SIGNATURE_SIZE=384 ;;
        RSA4096) IMAGE_SIGNATURE_SIZE=512 ;;
        *) IMAGE_SIGNATURE_SIZE=96 ;;
    esac
    export IMAGE_SIGNATURE_SIZE

    SIGN_OPTIONS=""
    case "${SIGN:-}" in
        ECC256) SIGN_OPTIONS="--ecc256" ;; ECC384) SIGN_OPTIONS="--ecc384" ;;
        ECC521) SIGN_OPTIONS="--ecc521" ;; ED25519) SIGN_OPTIONS="--ed25519" ;;
        ED448) SIGN_OPTIONS="--ed448" ;; RSA2048) SIGN_OPTIONS="--rsa2048" ;;
        RSA3072) SIGN_OPTIONS="--rsa3072" ;; RSA4096) SIGN_OPTIONS="--rsa4096" ;;
    esac
    case "${HASH:-}" in
        SHA256) SIGN_OPTIONS="$SIGN_OPTIONS --sha256" ;;
        SHA384) SIGN_OPTIONS="$SIGN_OPTIONS --sha384" ;;
        SHA3) SIGN_OPTIONS="$SIGN_OPTIONS --sha3" ;;
    esac
    export SIGN_OPTIONS
}

# Initialize UART capture variables
UART_PIDS=()
UART_PTY=""
KEEP_UART_CAPTURE=false

# Relay control
relay_set_mode() {
    local pattern=$1 port="${2:-$RELAY_PORT}"
    [ ${#pattern} -ne 4 ] && { log_error "Pattern must be 4 binary digits"; return 1; }
    echo "$pattern" | grep -qE '^[01]{4}$' || { log_error "Pattern must contain only 0s and 1s"; return 1; }

    stty -F "$port" 115200 raw -echo -echoe -echok -echoctl -echoke cs8 -cstopb -parenb 2>/dev/null || {
        log_error "Failed to configure serial port $port"; return 1;
    }
    sleep 0.1
    exec 3<>"$port" || { log_error "Failed to open serial port $port"; return 1; }

    log_info "Setting relay pattern: $pattern"
    for i in 0 1 2 3; do
        local bit="${pattern:$i:1}"
        local relay_num=$((i + 1))
        local state=$([ "$bit" = "1" ] && echo 1 || echo 0)
        local checksum=$(( (0xA0 + relay_num + state) & 0xFF ))
        echo "  Relay $relay_num: $( [ $state -eq 1 ] && echo ON || echo OFF ) (pattern[$i]=$bit) -> [0xA0, $relay_num, $state, 0x$(printf "%02x" $checksum)]"
        printf "%b" "\x$(printf "%02x" 0xA0)\x$(printf "%02x" $relay_num)\x$(printf "%02x" $state)\x$(printf "%02x" $checksum)" >&3
        sync; sleep 0.05
    done
    exec 3<&-; exec 3>&-
}

boot_sdcard() {
    log_info "Booting from SD card..."
    # Set boot mode pins for SD boot (MODE3:0 = 0b1110 = 0xE)
    # Hold reset for 1 second to ensure clean reset
    relay_set_mode "1000" && sleep 0.2 && relay_set_mode "1000" && sleep 1.0 && relay_set_mode "0000" && sleep 0.5
    log_ok "SD card boot mode set, reset released"
}

boot_qspi() {
    log_info "Booting from QSPI..."
    # Set boot mode pins for QSPI boot (MODE3:0 = 0b0011 = 0x3)
    # Hold reset for 1 second to ensure clean reset
    relay_set_mode "1000" && sleep 0.2 && relay_set_mode "1011" && sleep 1.0 && relay_set_mode "0011" && sleep 0.5
    log_ok "QSPI boot mode set, reset released"
}

# UART capture functions
kill_existing_uart_processes() {
    local pids=$(lsof -t "$UART_PORT" 2>/dev/null || true)
    [ -n "$pids" ] && {
        log_info "Killing existing processes using $UART_PORT: $pids"
        for pid in $pids; do kill "$pid" 2>/dev/null || true; sleep 0.2; kill -9 "$pid" 2>/dev/null || true; done
        sleep 0.5
    }
}

start_uart_capture() {
    log_info "Starting UART capture: $UART_PORT -> $UART_LOG"
    UART_PTY=$(mktemp -u /tmp/vmk180_uart_XXXXXX)
    [ ! -e "$UART_PORT" ] && { log_error "Serial port not found: $UART_PORT"; exit 1; }
    [ ! -r "$UART_PORT" ] || [ ! -w "$UART_PORT" ] && { log_error "No read/write access to $UART_PORT"; exit 1; }
    lsof "$UART_PORT" >/dev/null 2>&1 && { kill_existing_uart_processes; lsof "$UART_PORT" >/dev/null 2>&1 && { log_error "Failed to free serial port"; exit 1; }; }
    [ -e "$UART_PTY" ] && rm -f "$UART_PTY"
    stty -F "$UART_PORT" "$UART_BAUD" raw -echo -echoe -echok -echoctl -echoke cs8 -cstopb -parenb 2>/dev/null || { log_error "Failed to configure serial port"; exit 1; }

    socat_err=$(mktemp /tmp/socat_err_XXXXXX)
    log_info "Creating PTY bridge: $UART_PTY <-> $UART_PORT @ ${UART_BAUD}bps"
    socat PTY,link="$UART_PTY",raw,echo=0 "GOPEN:$UART_PORT" >/dev/null 2>"$socat_err" &
    socat_pid=$!; UART_PIDS+=($socat_pid)

    for i in {1..10}; do
        sleep 0.2; [ -e "$UART_PTY" ] && break
        kill -0 "$socat_pid" 2>/dev/null || { log_error "socat process died:"; cat "$socat_err" >&2; rm -f "$socat_err"; exit 1; }
    done
    [ -s "$socat_err" ] && { log_error "socat errors:"; cat "$socat_err" >&2; }
    rm -f "$socat_err"
    [ ! -e "$UART_PTY" ] && { log_error "Failed to create PTY after 2 seconds"; exit 1; }
    log_ok "UART PTY created: $UART_PTY"
}

stop_uart_capture() {
    for pid in "${UART_PIDS[@]}"; do kill "$pid" 2>/dev/null || true; sleep 0.1; kill -9 "$pid" 2>/dev/null || true; done
    [ -n "$UART_PTY" ] && [ -e "$UART_PTY" ] && rm -f "$UART_PTY"
}

cleanup() { [ "$KEEP_UART_CAPTURE" = "false" ] && { log_info "Cleaning up..."; stop_uart_capture; kill_existing_uart_processes; }; }
trap cleanup EXIT INT TERM

# Helper: Check Linux images directory
check_linux_images() {
    local required_files="$1"
    [ -z "$LINUX_IMAGES_DIR" ] && { log_error "LINUX_IMAGES_DIR not set."; log_info "Example: LINUX_IMAGES_DIR=/path/to/images/linux $0 $2"; exit 1; }
    [ ! -d "$LINUX_IMAGES_DIR" ] && { log_error "Linux images directory not found: $LINUX_IMAGES_DIR"; exit 1; }
    for f in $required_files; do
        [ ! -f "${LINUX_IMAGES_DIR}/${f}" ] && { log_error "Required file not found: ${LINUX_IMAGES_DIR}/${f}"; exit 1; }
    done
    log_ok "All required Linux files found in $LINUX_IMAGES_DIR"
}

# Helper: Copy PDI file (from Linux images or soc-prebuilt-firmware)
copy_pdi() {
    if [ -f "${LINUX_IMAGES_DIR}/project_1.pdi" ]; then
        cp "${LINUX_IMAGES_DIR}/project_1.pdi" .
    else
        export PREBUILT_DIR="${WOLFBOOT_ROOT}/../soc-prebuilt-firmware/vmk180-versal"
        [ ! -f "${PREBUILT_DIR}/project_1.pdi" ] && { log_error "project_1.pdi not found in Linux images or soc-prebuilt-firmware"; exit 1; }
        cp "${PREBUILT_DIR}/project_1.pdi" .
    fi
}

# Helper: Flash QSPI via U-Boot and capture boot output
# Args: $1=files_to_flash (space-separated: "file1:addr1 file2:addr2 ...")
#       $2=capture_time (seconds)
#       $3=description
flash_and_boot() {
    local flash_items="$1" capture_time="${2:-30}" desc="${3:-Boot}"

    # Build expect script dynamically
    local expect_script="
set timeout 120
set pty \"$UART_PTY\"
set board_ip \"$BOARD_IP\"
set server_ip \"$SERVER_IP\"

spawn -open [open \$pty r+]
stty raw < \$pty
log_user 0
log_file -a \"$UART_LOG\"

send_user \"Waiting for autoboot prompt...\\n\"
expect {
    \"Hit any key to stop autoboot\" { send_user \"Autoboot prompt detected, sending Enter...\\n\"; send \"\\r\" }
    \"Versal>\" { send_user \"U-Boot prompt found\\n\" }
    timeout { send_user \"Timeout waiting for U-Boot prompt\\n\"; exit 1 }
}
expect \"Versal>\"
send_user \"At U-Boot prompt, configuring network...\\n\"
sleep 0.5

send \"setenv ipaddr \$board_ip\\r\"; expect \"Versal>\"
send \"setenv serverip \$server_ip\\r\"; expect \"Versal>\"
send \"setenv netmask 255.255.255.0\\r\"; expect \"Versal>\"

send_user \"Probing SPI flash...\\n\"
send \"sf probe 0\\r\"; expect \"Versal>\"
"
    # Add flash operations for each file
    for item in $flash_items; do
        local file="${item%%:*}" addr="${item##*:}"
        local size=$(stat -c%s "${TFTP_DIR}/${file}")
        local size_hex=$(printf "0x%x" $size)
        expect_script+="
send_user \"Downloading ${file} via TFTP...\\n\"
send \"tftpboot 0x10000000 ${file}\\r\"
expect {
    -re \"Bytes transferred.*Versal>\" { send_user \"TFTP download successful\\n\" }
    \"Error\" { send_user \"TFTP download failed\\n\"; exit 1 }
    timeout { send_user \"TFTP timeout\\n\"; exit 1 }
}

send_user \"Erasing and writing ${file} to flash at ${addr}...\\n\"
send \"sf update 0x10000000 ${addr} ${size_hex}\\r\"
expect {
    -re \"Versal>\" { send_user \"${file} flash complete\\n\" }
    timeout { send_user \"Flash timeout\\n\"; exit 1 }
}
"
    done

    expect_script+="
send_user \"\\n\"
send_user \"All flash operations complete!\\n\"
close
"
    boot_sdcard
    echo "$expect_script" | expect
    log_ok "Flash operations complete!"

    # Restart UART logging and boot from QSPI
    log_info "Restarting continuous UART logging..."
    cat "$UART_PTY" >> "$UART_LOG" 2>&1 &
    UART_PIDS+=($!)
    sleep 1

    log_info "Switching to QSPI boot mode..."
    boot_qspi
    log_ok "Board booting from QSPI"

    log_info "Capturing UART output for ${capture_time} seconds (${desc})..."
    log_info "Watch live: tail -f $UART_LOG"
    sleep "$capture_time"

    log_ok "Capture complete"
    log_info "UART log saved to: $UART_LOG"
    KEEP_UART_CAPTURE=true
    log_info "UART capture still active in background (PID: ${UART_PIDS[*]})"
    log_info "To stop: kill ${UART_PIDS[*]}"
}

# Show help message
show_help() {
    cat <<EOF
Build, flash QSPI, and boot VMK180 - all in one script

Usage: $0 [OPTIONS]

Options:
  (none)              Full build, flash, and boot wolfBoot
  --test-app          Full build + flash test app to boot partition
  --test-update       Full build + flash test app v2 to update partition
  --linux             Build wolfBoot + signed Linux FIT image and boot
  --linux-uboot       Build BOOT.BIN with U-Boot and flash Linux FIT image
  --boot-sdcard       Test SD card boot mode only (no build/flash)
  --boot-qspi         Test QSPI boot mode only (no build/flash)
  --skipuart          Skip UART capture (use with --boot-sdcard/--boot-qspi)
  -h, --help          Show this help message

Environment Variables:
  UART_PORT           Serial port for UART capture (default: /dev/ttyUSB2)
  SERVER_IP           TFTP server IP address (default: 10.0.4.24)
  BOARD_IP            Board IP address (default: 10.0.4.90)
  TFTP_DIR            TFTP directory path (default: /srv/tftp)
  VITIS_PATH          Xilinx Vitis installation path (default: /opt/Xilinx/Vitis/2024.2)
  LINUX_IMAGES_DIR    Path to PetaLinux images directory (for --linux and --linux-uboot)

Examples:
  $0 --boot-sdcard --skipuart    # Reset to SD boot without UART capture
  $0 --boot-qspi --skipuart      # Reset to QSPI boot without UART capture
EOF
}

# Check for --skipuart flag before starting UART capture
SKIP_UART=false
for arg in "$@"; do
    case "$arg" in
        --skipuart) SKIP_UART=true ;;
    esac
done

# Check for help option before starting UART capture
case "${1:-}" in -h|--help) show_help; exit 0 ;; esac

# Start UART capture immediately (unless --skipuart specified)
if [ "$SKIP_UART" = "false" ]; then
    log_info "Starting UART capture..."
    start_uart_capture || { log_error "Failed to start UART capture"; exit 1; }
    log_info "UART capture active, PIDs: ${UART_PIDS[*]}"
else
    log_info "Skipping UART capture (--skipuart specified)"
fi

# Test boot helper
test_boot() {
    local boot_func=$1 mode_name=$2
    log_info "=== Testing $mode_name relay sequence ==="

    # If UART capture is skipped, just run relay and exit
    if [ "$SKIP_UART" = "true" ]; then
        $boot_func || { log_error "$mode_name failed"; exit 1; }
        log_ok "Board reset to $mode_name mode"
        log_info "Monitor console manually: picocom -b 115200 $UART_PORT"
        exit 0
    fi

    [ ${#UART_PIDS[@]} -eq 0 ] && { log_error "UART capture not running!"; exit 1; }
    [ -z "$UART_PTY" ] || [ ! -e "$UART_PTY" ] && { log_error "UART PTY not available: $UART_PTY"; exit 1; }

    cat "$UART_PTY" >> "$UART_LOG" 2>&1 &
    UART_PIDS+=($!); sleep 0.5
    $boot_func || { log_error "$mode_name failed"; exit 1; }

    log_info "Monitoring UART for 30 seconds..."
    trap 'log_info "Interrupted"; KEEP_UART_CAPTURE=true; exit 0' INT
    sleep 30 || true
    KEEP_UART_CAPTURE=true; exit 0
}

# Parse options
FLASH_TEST_APP=false
FLASH_UPDATE_APP=false
case "${1:-}" in
    test-boot|--boot-sdcard) test_boot boot_sdcard "boot-sdcard" ;;
    --boot-qspi) test_boot boot_qspi "boot-qspi" ;;
    --test-app) FLASH_TEST_APP=true ;;
    --test-update) FLASH_TEST_APP=true; FLASH_UPDATE_APP=true ;;
    --linux-uboot)
        log_info "=== Linux U-Boot Mode ==="
        check_linux_images "plm.elf psmfw.elf bl31.elf u-boot.elf system-default.dtb image.ub" "--linux-uboot"

        log_info "Copying Linux boot files..."
        for f in plm.elf psmfw.elf bl31.elf u-boot.elf system-default.dtb image.ub; do cp "${LINUX_IMAGES_DIR}/${f}" .; done
        copy_pdi

        log_info "Generating BOOT.BIN with U-Boot..."
        source "${VITIS_PATH}/settings64.sh"
        rm -f BOOT.BIN
        bootgen -arch versal -image ./tools/scripts/versal_uboot_linux.bif -w -o BOOT.BIN || { log_error "bootgen failed"; exit 1; }

        cp BOOT.BIN "${TFTP_DIR}/"
        cp image.ub "${TFTP_DIR}/"
        log_ok "BOOT.BIN size: $(stat -c%s BOOT.BIN) bytes"
        log_ok "image.ub size: $(stat -c%s image.ub) bytes"

        flash_and_boot "BOOT.BIN:0x0 image.ub:0xF40000" 90 "Linux boot"
        exit 0
        ;;
    --linux)
        log_info "=== Linux wolfBoot Mode ==="
        check_linux_images "plm.elf psmfw.elf bl31.elf Image system-default.dtb" "--linux"
        command -v mkimage &>/dev/null || { log_error "mkimage not found - install with: sudo apt install u-boot-tools"; exit 1; }

        log_info "Copying Linux boot files..."
        for f in plm.elf psmfw.elf bl31.elf Image system-default.dtb; do cp "${LINUX_IMAGES_DIR}/${f}" .; done
        copy_pdi

        log_info "Building wolfBoot..."
        cp config/examples/versal_vmk180.config .config
        make clean && make || { log_error "Failed to build wolfBoot"; exit 1; }
        [ ! -f "wolfboot.elf" ] && { log_error "wolfboot.elf not found"; exit 1; }
        load_config .config

        log_info "Creating FIT image..."
        mkimage -f ./hal/versal.its fitImage || { log_error "mkimage failed"; exit 1; }
        log_ok "FIT image created: fitImage"

        log_info "Signing FIT image..."
        export IMAGE_HEADER_SIZE IMAGE_SIGNATURE_SIZE
        ./tools/keytools/sign $SIGN_OPTIONS fitImage "${PRIVATE_KEY:-wolfboot_signing_private_key.der}" 1 || { log_error "Signing failed"; exit 1; }
        log_ok "Signed FIT image: fitImage_v1_signed.bin"

        log_info "Generating BOOT.BIN with wolfBoot..."
        source "${VITIS_PATH}/settings64.sh"
        rm -f BOOT.BIN
        bootgen -arch versal -image ./tools/scripts/versal_boot.bif -w -o BOOT.BIN || { log_error "bootgen failed"; exit 1; }

        cp BOOT.BIN "${TFTP_DIR}/"
        cp fitImage_v1_signed.bin "${TFTP_DIR}/"
        log_ok "BOOT.BIN size: $(stat -c%s BOOT.BIN) bytes"
        log_ok "Signed FIT size: $(stat -c%s fitImage_v1_signed.bin) bytes"

        flash_and_boot "BOOT.BIN:0x0 fitImage_v1_signed.bin:0x800000" 90 "wolfBoot + Linux boot"
        exit 0
        ;;
    "")
        ;; # Default mode - continue below
    *)
        log_error "Unknown option: $1"; echo ""; show_help; exit 1
        ;;
esac

# Default mode: Build wolfBoot and optionally test app
log_info "Building wolfBoot..."
cp config/examples/versal_vmk180.config .config
make clean && make || { log_error "Failed to build wolfBoot"; exit 1; }
[ ! -f "wolfboot.elf" ] && { log_error "wolfboot.elf not found"; exit 1; }

# Build test app if requested
if [ "$FLASH_TEST_APP" = "true" ]; then
    load_config .config
    export IMAGE_HEADER_SIZE IMAGE_SIGNATURE_SIZE
    PRIVATE_KEY="${PRIVATE_KEY:-wolfboot_signing_private_key.der}"

    if [ "$FLASH_UPDATE_APP" = "true" ]; then
        log_info "Building and signing test application version 2..."
        make test-app/image.bin
        ./tools/keytools/sign $SIGN_OPTIONS test-app/image.bin "$PRIVATE_KEY" 2 || { log_error "Signing failed"; exit 1; }
        cp test-app/image_v2_signed.bin "${TFTP_DIR}/"
        log_ok "Test app v2 copied to TFTP"
    else
        log_info "Building and signing test application version 1..."
        make test-app/image.bin
        make test-app/image_v1_signed.bin
        cp test-app/image_v1_signed.bin "${TFTP_DIR}/"
        log_ok "Test app copied to TFTP"
    fi
fi

# Generate BOOT.BIN
log_info "Generating BOOT.BIN..."
export PREBUILT_DIR="${WOLFBOOT_ROOT}/../soc-prebuilt-firmware/vmk180-versal"
[ ! -d "${PREBUILT_DIR}" ] && { log_error "Prebuilt firmware directory not found: ${PREBUILT_DIR}"; exit 1; }

for f in project_1.pdi plm.elf psmfw.elf bl31.elf system-default.dtb; do cp "${PREBUILT_DIR}/${f}" .; done

source "${VITIS_PATH}/settings64.sh"
rm -f BOOT.BIN
bootgen -arch versal -image ./tools/scripts/versal_boot.bif -w -o BOOT.BIN || { log_error "bootgen failed"; exit 1; }
cp BOOT.BIN "${TFTP_DIR}/"

# Build flash items list
FLASH_ITEMS="BOOT.BIN:0x0"
if [ "$FLASH_TEST_APP" = "true" ]; then
    if [ "$FLASH_UPDATE_APP" = "true" ]; then
        FLASH_ITEMS="$FLASH_ITEMS image_v2_signed.bin:0x3400000"
    else
        FLASH_ITEMS="$FLASH_ITEMS image_v1_signed.bin:0x800000"
    fi
fi

flash_and_boot "$FLASH_ITEMS" 30 "wolfBoot boot"
