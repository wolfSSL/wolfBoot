#!/bin/bash
#
# wolfBoot PolarFire SoC Programming Script
# Automates building, flashing, and verifying wolfBoot on MPFS target
#

set -e

# Configuration
HSS_TTY="${HSS_TTY:-/dev/ttyUSB9}"
WOLFBOOT_TTY="${WOLFBOOT_TTY:-/dev/ttyUSB10}"
BLOCK_DEV="${BLOCK_DEV:-/dev/sde}"
BAUD_RATE="${BAUD_RATE:-115200}"
TIMEOUT_HSS="${TIMEOUT_HSS:-30}"
TIMEOUT_BLOCK="${TIMEOUT_BLOCK:-15}"
TIMEOUT_WOLFBOOT="${TIMEOUT_WOLFBOOT:-30}"
WOLFBOOT_BIN="${WOLFBOOT_BIN:-wolfboot.bin}"
CONFIG_FILE="${CONFIG_FILE:-./config/examples/polarfire_mpfs250.config}"
STORAGE_MODE="${STORAGE_MODE:-}"  # Can be "emmc" or "sdcard"
RESET_METHOD="${RESET_METHOD:-hss}"  # "hss" (HSS console reset) or "flashpro" (FlashPro6)
RESET_DELAY="${RESET_DELAY:-0.2}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check for required tools
check_dependencies() {
    local missing=0
    for tool in dtc hss-payload-generator lsblk stty; do
        if ! command -v "$tool" &>/dev/null; then
            log_error "Required tool '$tool' not found"
            missing=1
        fi
    done

    # Check for FlashPro6 tool if using flashpro reset method
    if [[ "$RESET_METHOD" == "flashpro" ]]; then
        if ! command -v fpgenprog &>/dev/null; then
            log_error "FlashPro6 tool 'fpgenprog' not found (required for flashpro reset method)"
            missing=1
        fi
    fi

    if [[ $missing -eq 1 ]]; then
        exit 1
    fi
}

# Reset control functions
reset_target_hss() {
    log_info "Resetting target via HSS console..."

    # Configure serial port
    stty -F "$HSS_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw -icanon min 1 time 0

    # Open file descriptor for serial port
    exec 3<>"$HSS_TTY"

    # Send Ctrl+C to interrupt any running command
    printf "\003" >&3
    sleep 0.3

    # Clear any pending output
    while read -r -t 0.1 -u 3 line 2>/dev/null; do
        :  # discard
    done

    # Send reset command
    log_info "Sending 'reset' command to HSS..."
    echo "reset" >&3
    sleep 0.5

    # Read response
    while read -r -t 0.5 -u 3 line 2>/dev/null; do
        echo "$line"
    done

    # Close file descriptor
    exec 3>&-

    log_success "Reset command sent via HSS"
    sleep "$RESET_DELAY"
}

reset_target_flashpro() {
    log_info "Resetting target via FlashPro6..."

    # Use fpgenprog to reset the target
    # The exact command may vary based on FlashPro6 configuration
    if fpgenprog -n -a "RESET" 2>&1 | tee /tmp/flashpro_reset.log; then
        log_success "Target reset via FlashPro6"
    else
        log_warn "FlashPro6 reset may have failed (check /tmp/flashpro_reset.log)"
    fi

    sleep "$RESET_DELAY"
}

reset_target() {
    case "$RESET_METHOD" in
        hss)
            reset_target_hss
            ;;
        flashpro)
            reset_target_flashpro
            ;;
        *)
            log_error "Unknown reset method: $RESET_METHOD (use 'hss' or 'flashpro')"
            return 1
            ;;
    esac
}

# Build wolfBoot
build_wolfboot() {
    log_info "Building wolfBoot..."

    log_info "Copying config: $CONFIG_FILE -> .config"
    cp "$CONFIG_FILE" .config

    log_info "Running make clean..."
    make clean

    # Build with storage-specific flags if specified
    local make_opts=""
    if [[ "$STORAGE_MODE" == "emmc" ]]; then
        make_opts="DISK_EMMC=1"
        log_info "Building for eMMC storage..."
    elif [[ "$STORAGE_MODE" == "sdcard" ]]; then
        make_opts="DISK_SDCARD=1"
        log_info "Building for SD card storage..."
    fi

    log_info "Building wolfboot.elf..."
    make $make_opts wolfboot.elf

    log_info "Size of wolfboot.elf:"
    size wolfboot.elf

    log_info "Compiling device tree..."
    dtc -I dts -O dtb hal/mpfs.dts -o hal/mpfs.dtb

    log_info "Generating HSS payload..."
    hss-payload-generator -vvv -c ./hal/mpfs.yaml "$WOLFBOOT_BIN"

    log_success "Build completed successfully!"
}

# Wait for HSS CLI prompt and enter usbdmsc mode
enter_usbdmsc_mode() {
    log_info "Waiting for HSS CLI prompt on $HSS_TTY..."

    # Configure serial port
    stty -F "$HSS_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw -icanon min 1 time 0

    # Open file descriptor for serial port
    exec 3<>"$HSS_TTY"

    local buffer=""
    local elapsed=0
    local found_prompt=0
    local found_cli=0

    # Wait for "Press a key to enter CLI, ESC to skip"
    log_info "Waiting for HSS boot prompt..."
    while [[ $elapsed -lt $TIMEOUT_HSS ]]; do
        # Read available data with timeout
        if read -r -t 1 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *"Press a key to enter CLI"* ]]; then
                found_prompt=1
                break
            fi
        fi
        elapsed=$((elapsed + 1))
    done

    if [[ $found_prompt -eq 0 ]]; then
        exec 3>&-
        log_error "Timeout waiting for HSS CLI prompt"
        return 1
    fi

    # Send a key to enter CLI
    sleep 0.5
    echo -e "\r" >&3

    # Wait for the >> prompt
    buffer=""
    elapsed=0
    log_info "Waiting for HSS command prompt..."
    while [[ $elapsed -lt 10 ]]; do
        if read -r -t 1 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *">>"* ]]; then
                found_cli=1
                break
            fi
        fi
        elapsed=$((elapsed + 1))
    done

    if [[ $found_cli -eq 0 ]]; then
        exec 3>&-
        log_error "Timeout waiting for HSS command prompt"
        return 1
    fi

    # IMPORTANT: Always select SD card storage for USBDMSC mode
    # (We're flashing wolfboot.bin to the SD card boot partition)
    # Don't use QSPI for USBDMSC unless that's the actual target
    local usbdmsc_storage="${USBDMSC_STORAGE:-sdcard}"

    if [[ -n "$STORAGE_MODE" ]] && [[ "$STORAGE_MODE" != "qspi" ]]; then
        # If user specified a storage mode (and it's not qspi), use that
        usbdmsc_storage="$STORAGE_MODE"
    fi

    log_info "Setting storage to '$usbdmsc_storage' for USBDMSC mode..."
    echo "$usbdmsc_storage" >&3
    sleep 1

    # Read response
    while read -r -t 1 -u 3 line 2>/dev/null; do
        echo "$line"
    done

    # Send usbdmsc command
    log_info "Sending usbdmsc command..."
    echo "usbdmsc" >&3
    sleep 2

    # Read any remaining output
    while read -r -t 1 -u 3 line 2>/dev/null; do
        echo "$line"
    done

    # Close file descriptor
    exec 3>&-

    log_success "USBDMSC mode activated on $usbdmsc_storage"
}

# Unmount any mounted partitions on the block device
unmount_block_device() {
    log_info "Checking for mounted partitions on $BLOCK_DEV..."

    # Find all mounted partitions on this device
    local mounted_parts
    mounted_parts=$(mount | grep "^${BLOCK_DEV}" | awk '{print $1}' || true)

    if [[ -n "$mounted_parts" ]]; then
        for part in $mounted_parts; do
            log_info "Unmounting $part..."
            if sudo umount "$part" 2>/dev/null; then
                log_success "Unmounted $part"
            else
                log_warn "Failed to unmount $part (may already be unmounted)"
            fi
        done
        # Give system time to fully release the device
        sleep 1
    else
        log_info "No mounted partitions found on $BLOCK_DEV"
    fi
}

# Wait for block device to become available
wait_for_block_device() {
    local partition="${BLOCK_DEV}1"
    local elapsed=0

    log_info "Waiting for $partition to become available..."

    while [[ $elapsed -lt $TIMEOUT_BLOCK ]]; do
        if lsblk "$partition" &>/dev/null; then
            # Wait a bit more for the device to be fully ready
            sleep 1
            if lsblk "$partition" &>/dev/null; then
                log_success "Block device $partition is available"
                lsblk "$partition"
                return 0
            fi
        fi
        sleep 1
        elapsed=$((elapsed + 1))
        echo -n "."
    done

    echo ""
    log_error "Timeout waiting for $partition"
    return 1
}

# Flash and verify wolfboot.bin
flash_and_verify() {
    local partition="${BLOCK_DEV}1"

    log_info "Flashing $WOLFBOOT_BIN to $partition..."

    # Flash the image
    sudo dd if="$WOLFBOOT_BIN" of="$partition" bs=512 status=progress
    sync

    log_info "Verifying flash..."

    # Verify - cmp should report "EOF on wolfboot.bin" if successful
    local cmp_output
    cmp_output=$(sudo cmp "$WOLFBOOT_BIN" "$partition" 2>&1) || true

    if echo "$cmp_output" | grep -q "EOF on $WOLFBOOT_BIN"; then
        log_success "Verification successful: $cmp_output"
        return 0
    elif [[ -z "$cmp_output" ]]; then
        # No output from cmp means files are identical up to the size of the smaller one
        log_success "Verification successful (files match)"
        return 0
    else
        log_error "Verification failed: $cmp_output"
        return 1
    fi
}

# Capture wolfBoot output
capture_wolfboot_output() {
    local output_file="${1:-wolfboot_output_$(date +%Y%m%d_%H%M%S).log}"

    log_info "Capturing wolfBoot output from $WOLFBOOT_TTY to $output_file..."
    log_info "Press Ctrl+C to stop capture"

    # Configure serial port
    stty -F "$WOLFBOOT_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw

    # Use timeout with cat to capture output, or just cat if user wants manual stop
    if [[ -n "$TIMEOUT_WOLFBOOT" ]] && [[ "$TIMEOUT_WOLFBOOT" -gt 0 ]]; then
        timeout "$TIMEOUT_WOLFBOOT" cat "$WOLFBOOT_TTY" | tee "$output_file" || true
    else
        cat "$WOLFBOOT_TTY" | tee "$output_file"
    fi

    log_success "Output captured to $output_file"
}

# Capture wolfBoot output using timeout and cat
capture_wolfboot_output_timed() {
    local output_file="${1:-wolfboot_output_$(date +%Y%m%d_%H%M%S).log}"

    log_info "Capturing wolfBoot output from $WOLFBOOT_TTY..."
    log_info "Capture will run for $TIMEOUT_WOLFBOOT seconds"

    # Configure serial port
    stty -F "$WOLFBOOT_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw

    # Use timeout with cat to capture output
    timeout "$TIMEOUT_WOLFBOOT" cat "$WOLFBOOT_TTY" 2>/dev/null | tee "$output_file" || true

    echo ""
    log_success "Output captured to $output_file"
}

# Set storage mode and boot (for final boot after flashing)
# Starts wolfBoot capture immediately before sending boot command to avoid missing output
set_storage_and_boot() {
    local output_file="${1:-wolfboot_output_$(date +%Y%m%d_%H%M%S).log}"

    if [[ -z "$STORAGE_MODE" ]]; then
        log_info "No storage mode specified, letting device boot normally"
        return 0
    fi

    log_info "Waiting for HSS CLI prompt to set storage mode..."

    # Configure serial port immediately so we don't miss early boot output
    stty -F "$HSS_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw -icanon min 1 time 0

    # Open file descriptor for serial port
    exec 3<>"$HSS_TTY"

    # Clear any stale data from before power cycle
    while read -r -t 0.1 -u 3 line 2>/dev/null; do
        :  # discard
    done

    local buffer=""
    local elapsed=0
    local found_prompt=0
    local found_cli=0

    # Wait for "Press a key to enter CLI, ESC to skip"
    # Use longer timeout for post-flash boot (device may need to initialize)
    local boot_timeout=$((TIMEOUT_HSS * 2))
    log_info "Waiting for HSS boot prompt (timeout: ${boot_timeout}s)..."
    while [[ $elapsed -lt $boot_timeout ]]; do
        # Read available data with timeout
        if read -r -t 1 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *"Press a key to enter CLI"* ]]; then
                found_prompt=1
                break
            fi
        else
            # Show progress every 5 seconds
            if [[ $((elapsed % 5)) -eq 0 ]] && [[ $elapsed -gt 0 ]]; then
                echo -n "."
            fi
        fi
        elapsed=$((elapsed + 1))
    done

    if [[ $found_prompt -eq 0 ]]; then
        exec 3>&-
        echo ""
        log_error "Timeout waiting for HSS CLI prompt after ${boot_timeout}s"
        log_error "Device may still be booting or HSS output not detected"
        return 1
    fi

    # Send a key to enter CLI
    sleep 0.5
    echo -e "\r" >&3

    # Wait for the >> prompt
    buffer=""
    elapsed=0
    log_info "Waiting for HSS command prompt..."
    while [[ $elapsed -lt 10 ]]; do
        if read -r -t 1 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *">>"* ]]; then
                found_cli=1
                break
            fi
        fi
        elapsed=$((elapsed + 1))
    done

    if [[ $found_cli -eq 0 ]]; then
        exec 3>&-
        log_error "Timeout waiting for HSS command prompt"
        return 1
    fi

    # Send storage mode command
    log_info "Setting storage mode to: $STORAGE_MODE"
    echo "$STORAGE_MODE" >&3
    sleep 1

    # Read response
    while read -r -t 1 -u 3 line 2>/dev/null; do
        echo "$line"
    done

    # Configure wolfBoot serial port BEFORE sending boot command
    stty -F "$WOLFBOOT_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw

    # Start wolfBoot capture in background BEFORE sending boot command
    # This ensures we don't miss any early boot output
    log_info "Starting wolfBoot capture (output: $output_file)..."
    timeout "$TIMEOUT_WOLFBOOT" cat "$WOLFBOOT_TTY" 2>/dev/null | tee "$output_file" &
    local capture_pid=$!

    # Give capture process a moment to start
    sleep 0.2

    # Send boot command to boot from the selected storage
    log_info "Sending boot command..."
    echo "boot" >&3

    # Close HSS file descriptor immediately to avoid delays
    exec 3>&-

    log_success "Boot command sent, capturing wolfBoot output for ${TIMEOUT_WOLFBOOT}s..."

    # Wait for capture to complete
    wait $capture_pid || true

    echo ""
    log_success "Output captured to $output_file"
}

# Show usage
usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS] [COMMAND]

wolfBoot PolarFire SoC Programming Script

Commands:
    all             Run full workflow (build, flash, capture)
    build           Build wolfBoot only
    flash           Flash and verify only (assumes device in USBDMSC mode)
    capture         Capture wolfBoot output only
    reset           Reset the target only

Options:
    -h, --help              Show this help message
    -c, --config FILE       Config file (default: $CONFIG_FILE)
    -d, --device DEV        Block device (default: $BLOCK_DEV)
    -H, --hss-tty TTY       HSS serial port (default: $HSS_TTY)
    -W, --wolfboot-tty TTY  wolfBoot serial port (default: $WOLFBOOT_TTY)
    -r, --reset-method TYPE Reset method: 'hss' or 'flashpro' (default: $RESET_METHOD)
    -o, --output FILE       Output file for captured log
    -t, --timeout SEC       Timeout for wolfBoot capture (default: $TIMEOUT_WOLFBOOT)
    -s, --storage MODE      Storage mode for final boot: 'emmc' or 'sdcard' (default: none)
    -u, --usbdmsc-storage   Storage for USBDMSC flashing: 'emmc' or 'sdcard' (default: sdcard)
    --skip-build            Skip the build step in 'all' command

Environment Variables:
    HSS_TTY          HSS serial port
    WOLFBOOT_TTY     wolfBoot serial port
    BLOCK_DEV        Block device for flashing
    RESET_METHOD     Reset method: 'hss' or 'flashpro' (default: hss)
    BAUD_RATE        Serial baud rate (default: 115200)
    TIMEOUT_HSS      Timeout for HSS prompt (default: 30s)
    TIMEOUT_BLOCK    Timeout for block device (default: 15s)
    TIMEOUT_WOLFBOOT Timeout for capture (default: 30s)
    STORAGE_MODE     Storage mode for final boot: 'emmc' or 'sdcard'
    USBDMSC_STORAGE  Storage for USBDMSC flashing: 'emmc' or 'sdcard' (default: sdcard)

Examples:
    # Flash wolfBoot to SD card, boot from SD card
    $(basename "$0") -s sdcard all

    # Flash wolfBoot to eMMC, boot from eMMC
    $(basename "$0") -u emmc -s emmc all

    # Flash to SD (default), but boot from eMMC
    $(basename "$0") -s emmc all

    # Build only
    $(basename "$0") build

    # Flash existing build (skip build step)
    $(basename "$0") --skip-build all

    # Capture boot output only
    $(basename "$0") -o boot.log capture

    # Reset target using FlashPro6 instead of HSS
    $(basename "$0") -r flashpro reset

Notes:
    - USBDMSC storage (-u) is for flashing wolfboot.bin (default: sdcard)
    - Boot storage (-s) is what HSS will boot from after flashing
    - If QSPI flash doesn't respond (JEDEC ID 000000), check IOMUX/FPGA routing

EOF
}

# Main workflow
run_all() {
    local skip_build="${1:-0}"
    local output_file="${2:-}"

    log_info "Starting full wolfBoot programming workflow..."
    echo ""

    # Step 1: Build (if not skipped)
    if [[ "$skip_build" -eq 0 ]]; then
        log_info "=== Step 1: Building wolfBoot ==="
        build_wolfboot
        echo ""
    else
        log_info "=== Step 1: Skipping build ==="
        if [[ ! -f "$WOLFBOOT_BIN" ]]; then
            log_error "$WOLFBOOT_BIN not found. Cannot skip build."
            exit 1
        fi
        echo ""
    fi

    # Step 2: Reset target
    log_info "=== Step 2: Resetting target ==="
    reset_target
    echo ""

    # Step 3: Enter USBDMSC mode
    log_info "=== Step 3: Entering USBDMSC mode ==="
    enter_usbdmsc_mode
    echo ""

    # Step 4: Wait for block device
    log_info "=== Step 4: Waiting for block device ==="
    wait_for_block_device
    echo ""

    # Step 5: Flash and verify
    log_info "=== Step 5: Flashing and verifying ==="
    flash_and_verify
    echo ""

    # Step 6: Unmount and reset again
    log_info "=== Step 6: Unmounting and resetting for boot ==="
    unmount_block_device
    sleep 1
    reset_target
    echo ""

    # Step 7/8: Set storage mode and boot, then capture wolfBoot output
    if [[ -n "$STORAGE_MODE" ]]; then
        # When storage mode is set, set_storage_and_boot handles both boot and capture
        # to avoid missing early wolfBoot output
        log_info "=== Step 7: Setting storage mode, booting, and capturing output ==="
        set_storage_and_boot "$output_file"
        echo ""
    else
        # No storage mode - just capture output (device boots automatically)
        log_info "=== Step 7: Capturing wolfBoot output ==="
        capture_wolfboot_output_timed "$output_file"
        echo ""
    fi

    log_success "=== Workflow completed successfully! ==="
}

# Parse command line arguments
SKIP_BUILD=0
OUTPUT_FILE=""
COMMAND=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -c|--config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -d|--device)
            BLOCK_DEV="$2"
            shift 2
            ;;
        -H|--hss-tty)
            HSS_TTY="$2"
            shift 2
            ;;
        -W|--wolfboot-tty)
            WOLFBOOT_TTY="$2"
            shift 2
            ;;
        -r|--reset-method)
            RESET_METHOD="$2"
            if [[ "$RESET_METHOD" != "hss" && "$RESET_METHOD" != "flashpro" ]]; then
                log_error "Invalid reset method: $RESET_METHOD (must be 'hss' or 'flashpro')"
                exit 1
            fi
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -t|--timeout)
            TIMEOUT_WOLFBOOT="$2"
            shift 2
            ;;
        -s|--storage)
            STORAGE_MODE="$2"
            if [[ "$STORAGE_MODE" != "emmc" && "$STORAGE_MODE" != "sdcard" ]]; then
                log_error "Invalid storage mode: $STORAGE_MODE (must be 'emmc' or 'sdcard')"
                exit 1
            fi
            shift 2
            ;;
        -u|--usbdmsc-storage)
            USBDMSC_STORAGE="$2"
            if [[ "$USBDMSC_STORAGE" != "emmc" && "$USBDMSC_STORAGE" != "sdcard" ]]; then
                log_error "Invalid USBDMSC storage: $USBDMSC_STORAGE (must be 'emmc' or 'sdcard')"
                exit 1
            fi
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        all|build|flash|capture|reset)
            COMMAND="$1"
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Default to 'all' if no command specified
COMMAND="${COMMAND:-all}"

# Check dependencies
check_dependencies

# Execute command
case "$COMMAND" in
    all)
        run_all "$SKIP_BUILD" "$OUTPUT_FILE"
        ;;
    build)
        build_wolfboot
        ;;
    flash)
        wait_for_block_device
        flash_and_verify
        unmount_block_device
        ;;
    capture)
        capture_wolfboot_output_timed "$OUTPUT_FILE"
        ;;
    reset)
        reset_target
        ;;
    *)
        log_error "Unknown command: $COMMAND"
        usage
        exit 1
        ;;
esac
