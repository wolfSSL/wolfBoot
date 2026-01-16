#!/bin/bash
#
# wolfBoot PolarFire SoC Programming Script
# Automates building, flashing, and verifying wolfBoot on MPFS target
#
# Supports two modes:
#   S-Mode: Traditional HSS-based boot via USB mass storage flashing
#   M-Mode: Direct eNVM programming via JTAG using mpfsBootmodeProgrammer
#

set -e

# Configuration - S-Mode (HSS-based boot)
HSS_TTY="${HSS_TTY:-/dev/ttyUSB1}"
WOLFBOOT_TTY="${WOLFBOOT_TTY:-/dev/ttyUSB4}"
BLOCK_DEV="${BLOCK_DEV:-/dev/sda}"
PI_HOST="${PI_HOST:-pi@Pi4}"
GPIO_PIN="${GPIO_PIN:-26}"
BAUD_RATE="${BAUD_RATE:-115200}"
TIMEOUT_HSS="${TIMEOUT_HSS:-30}"
TIMEOUT_BLOCK="${TIMEOUT_BLOCK:-15}"
TIMEOUT_WOLFBOOT="${TIMEOUT_WOLFBOOT:-30}"
WOLFBOOT_BIN="${WOLFBOOT_BIN:-wolfboot.bin}"
CONFIG_FILE="${CONFIG_FILE:-./config/examples/polarfire_mpfs250.config}"
STORAGE_MODE="${STORAGE_MODE:-}"  # Can be "emmc" or "sdcard"

# Configuration - M-Mode (JTAG-based programming)
MMODE_TTY="${MMODE_TTY:-/dev/ttyUSB1}"
MMODE_TTY_U54="${MMODE_TTY_U54:-/dev/ttyUSB4}"  # U54 hart 1 UART (MMUART1)
MMODE_CONFIG="${MMODE_CONFIG:-./config/examples/polarfire_mpfs250-m.config}"
MMODE_DIE="${MMODE_DIE:-MPFS250T}"
MMODE_PACKAGE="${MMODE_PACKAGE:-FCG1152}"
TIMEOUT_MMODE="${TIMEOUT_MMODE:-30}"

# Configuration - HSS (Hart Software Services)
HSS_DIR="${HSS_DIR:-../hart-software-services}"
HSS_BOARD="${HSS_BOARD:-mpfs-video-kit}"
HSS_TTY_DEBUG="${HSS_TTY_DEBUG:-/dev/ttyUSB1}"  # E51 UART for DDR debug output
HSS_CROSS_COMPILE="${HSS_CROSS_COMPILE:-/opt/Microchip/SoftConsole-v2022.2-RISC-V-747/riscv-unknown-elf-gcc/bin/riscv64-unknown-elf-}"
TIMEOUT_HSS_CAPTURE="${TIMEOUT_HSS_CAPTURE:-60}"

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

# Check for required tools (S-Mode)
check_dependencies() {
    local missing=0
    for tool in ssh dtc hss-payload-generator lsblk; do
        if ! command -v "$tool" &>/dev/null; then
            log_error "Required tool '$tool' not found"
            missing=1
        fi
    done
    if [[ $missing -eq 1 ]]; then
        exit 1
    fi
}

# Check for required tools (M-Mode)
check_mmode_dependencies() {
    local missing=0

    # Check for ssh (needed for GPIO power control)
    if ! command -v ssh &>/dev/null; then
        log_error "Required tool 'ssh' not found"
        missing=1
    fi

    # Check SC_INSTALL_DIR is set
    if [[ -z "$SC_INSTALL_DIR" ]]; then
        log_error "SC_INSTALL_DIR environment variable is not set"
        log_error "Please set it to your SoftConsole installation directory"
        missing=1
    fi

    # Check mpfsBootmodeProgrammer.jar exists
    local programmer_jar="$SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar"
    if [[ -n "$SC_INSTALL_DIR" ]] && [[ ! -f "$programmer_jar" ]]; then
        log_error "mpfsBootmodeProgrammer.jar not found at: $programmer_jar"
        missing=1
    fi

    # Check Java exists in SoftConsole
    local java_bin="$SC_INSTALL_DIR/eclipse/jre/bin/java"
    if [[ -n "$SC_INSTALL_DIR" ]] && [[ ! -x "$java_bin" ]]; then
        log_error "Java not found at: $java_bin"
        missing=1
    fi

    if [[ $missing -eq 1 ]]; then
        exit 1
    fi
}

# Power control functions
power_off() {
    log_info "Powering OFF target..."
    ssh "$PI_HOST" "raspi-gpio set $GPIO_PIN op dl"
}

power_on() {
    log_info "Powering ON target..."
    ssh "$PI_HOST" "raspi-gpio set $GPIO_PIN op dh"
}

power_cycle() {
    log_info "Power cycling target..."
    ssh "$PI_HOST" "raspi-gpio set $GPIO_PIN op dl && sleep 1 && raspi-gpio set $GPIO_PIN op dh"
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

# Build wolfBoot for M-Mode
build_wolfboot_mmode() {
    log_info "Building wolfBoot for M-Mode..."

    log_info "Copying config: $MMODE_CONFIG -> .config"
    cp "$MMODE_CONFIG" .config

    log_info "Running make clean..."
    make clean

    log_info "Building wolfboot.elf..."
    make wolfboot.elf

    log_info "Size of wolfboot.elf:"
    size wolfboot.elf

    log_success "M-Mode build completed successfully!"
}

# Flash wolfboot.elf via JTAG using mpfsBootmodeProgrammer
flash_jtag() {
    log_info "Flashing wolfboot.elf via JTAG (bootmode 1)..."
    log_info "Die: $MMODE_DIE, Package: $MMODE_PACKAGE"

    local java_bin="$SC_INSTALL_DIR/eclipse/jre/bin/java"
    local programmer_jar="$SC_INSTALL_DIR/extras/mpfs/mpfsBootmodeProgrammer.jar"

    # Run the bootmode programmer
    "$java_bin" -jar "$programmer_jar" \
        --bootmode 1 \
        --die "$MMODE_DIE" \
        --package "$MMODE_PACKAGE" \
        --workdir "$PWD" \
        wolfboot.elf

    log_success "JTAG flash completed successfully!"
}

# Capture M-Mode UART output from both E51 and U54 UARTs
capture_mmode_output() {
    local output_file="${1:-mmode_output_$(date +%Y%m%d_%H%M%S).log}"
    local e51_pid=""
    local u54_pid=""

    log_info "Capturing M-Mode output..."
    log_info "  E51 (hart 0): $MMODE_TTY"
    log_info "  U54 (hart 1): $MMODE_TTY_U54"
    log_info "Capture will run for $TIMEOUT_MMODE seconds"

    # Configure serial ports
    stty -F "$MMODE_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw 2>/dev/null || true

    # Check if U54 UART exists
    if [[ -e "$MMODE_TTY_U54" ]]; then
        stty -F "$MMODE_TTY_U54" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw 2>/dev/null || true
    fi

    # Create temp files for each UART
    local e51_tmp=$(mktemp)
    local u54_tmp=$(mktemp)

    # Start capturing from E51 UART (MMUART0) in background
    timeout "$TIMEOUT_MMODE" cat "$MMODE_TTY" 2>/dev/null > "$e51_tmp" &
    e51_pid=$!

    # Start capturing from U54 UART (MMUART1) in background if available
    if [[ -e "$MMODE_TTY_U54" ]]; then
        timeout "$TIMEOUT_MMODE" cat "$MMODE_TTY_U54" 2>/dev/null > "$u54_tmp" &
        u54_pid=$!
    fi

    # Wait for captures to complete
    wait $e51_pid 2>/dev/null || true
    if [[ -n "$u54_pid" ]]; then
        wait $u54_pid 2>/dev/null || true
    fi

    # Combine outputs with prefixes
    {
        echo "=== E51 (hart 0) Output - MMUART0 ==="
        cat "$e51_tmp"
        echo ""
        if [[ -e "$MMODE_TTY_U54" ]] && [[ -s "$u54_tmp" ]]; then
            echo "=== U54 (hart 1) Output - MMUART1 ==="
            cat "$u54_tmp"
        fi
    } | tee "$output_file"

    # Cleanup temp files
    rm -f "$e51_tmp" "$u54_tmp"

    echo ""
    log_success "Output captured to $output_file"
}

# Wait for HSS "Press a key to enter CLI" prompt on M-Mode TTY
# This indicates the device is powered on and ready for JTAG programming
wait_for_hss_ready_mmode() {
    log_info "Waiting for HSS ready prompt on $MMODE_TTY..."

    # Configure serial port
    stty -F "$MMODE_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw -icanon min 1 time 0

    # Open file descriptor for serial port
    exec 4<>"$MMODE_TTY"

    # Clear any stale data
    while read -r -t 0.1 -u 4 line 2>/dev/null; do
        :  # discard
    done

    local buffer=""
    local elapsed=0

    log_info "Waiting for 'Press a key to enter CLI' (timeout: ${TIMEOUT_HSS}s)..."
    while [[ $elapsed -lt $TIMEOUT_HSS ]]; do
        # Read available data with timeout
        if read -r -t 1 -u 4 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *"Press a key to enter CLI"* ]]; then
                exec 4>&-
                log_success "HSS ready - device is powered and initialized"
                return 0
            fi
        else
            # Show progress every 5 seconds
            if [[ $((elapsed % 5)) -eq 0 ]] && [[ $elapsed -gt 0 ]]; then
                echo -n "."
            fi
        fi
        elapsed=$((elapsed + 1))
    done

    exec 4>&-
    echo ""
    log_error "Timeout waiting for HSS ready prompt"
    return 1
}

# Wait for HSS CLI prompt and enter usbdmsc mode
# NOTE: Serial port must be opened BEFORE power cycling to catch the CLI prompt!
enter_usbdmsc_mode() {
    local do_power_cycle="${1:-0}"  # Optional: power cycle as part of this function

    log_info "Waiting for HSS CLI prompt on $HSS_TTY..."

    # Configure serial port FIRST (before power cycle if applicable)
    stty -F "$HSS_TTY" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw -icanon min 1 time 0

    # Open file descriptor for serial port BEFORE power cycling
    # This ensures we don't miss the CLI prompt window
    exec 3<>"$HSS_TTY"

    # Clear any stale data from the serial buffer
    log_info "Clearing stale serial data..."
    while read -r -t 0.1 -u 3 line 2>/dev/null; do
        :  # discard stale data
    done

    # Power cycle if requested (do this AFTER opening serial port)
    if [[ "$do_power_cycle" -eq 1 ]]; then
        log_info "Power cycling target..."
        ssh "$PI_HOST" "raspi-gpio set $GPIO_PIN op dl && sleep 1 && raspi-gpio set $GPIO_PIN op dh"
        log_info "Waiting for HSS to start..."
    fi

    local buffer=""
    local found_prompt=0
    local found_cli=0
    local start_time=$(date +%s)
    local current_time

    # Wait for "Press a key to enter CLI, ESC to skip"
    log_info "Waiting for HSS boot prompt (timeout: ${TIMEOUT_HSS}s)..."
    while true; do
        # Check actual elapsed time
        current_time=$(date +%s)
        if [[ $((current_time - start_time)) -ge $TIMEOUT_HSS ]]; then
            break
        fi

        # Read available data with short timeout (0.5s to stay responsive)
        if read -r -t 0.5 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *"Press a key to enter CLI"* ]]; then
                found_prompt=1
                # Send key immediately to enter CLI before timeout
                log_info "Found CLI prompt, sending key..."
                echo -e "\r" >&3
                break
            fi
        fi
    done

    if [[ $found_prompt -eq 0 ]]; then
        exec 3>&-
        log_error "Timeout waiting for HSS CLI prompt"
        log_error "The HSS may have auto-booted before the script could catch the CLI prompt."
        log_error "Check if HSS has CLI enabled and has sufficient timeout."
        return 1
    fi

    # Wait for the >> prompt
    buffer=""
    start_time=$(date +%s)
    log_info "Waiting for HSS command prompt..."
    while true; do
        current_time=$(date +%s)
        if [[ $((current_time - start_time)) -ge 10 ]]; then
            break
        fi
        if read -r -t 0.5 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *">>"* ]]; then
                found_cli=1
                break
            fi
        fi
    done

    if [[ $found_cli -eq 0 ]]; then
        exec 3>&-
        log_error "Timeout waiting for HSS command prompt"
        return 1
    fi

    # Select storage device first if specified, then enter USBDMSC mode
    # The storage mode command (emmc/sdcard) selects the device, then usbdmsc
    # activates USB mass storage mode for the selected device.
    if [[ -n "$STORAGE_MODE" ]]; then
        log_info "Selecting storage device: $STORAGE_MODE"
        echo "$STORAGE_MODE" >&3
        sleep 1
        # Read response from storage selection
        while read -r -t 1 -u 3 line 2>/dev/null; do
            echo "$line"
        done
    fi

    # Send usbdmsc command to enter USB mass storage mode
    log_info "Sending usbdmsc command..."
    echo "usbdmsc" >&3
    sleep 2

    # Read any remaining output
    while read -r -t 1 -u 3 line 2>/dev/null; do
        echo "$line"
    done

    # Close file descriptor
    exec 3>&-

    log_success "USBDMSC mode activated"
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
    local found_prompt=0
    local found_cli=0
    local start_time
    local current_time
    local last_progress=0

    # Wait for "Press a key to enter CLI, ESC to skip"
    # Use longer timeout for post-flash boot (device may need to initialize)
    local boot_timeout=$((TIMEOUT_HSS * 2))
    log_info "Waiting for HSS boot prompt (timeout: ${boot_timeout}s)..."
    start_time=$(date +%s)
    while true; do
        current_time=$(date +%s)
        local elapsed=$((current_time - start_time))
        if [[ $elapsed -ge $boot_timeout ]]; then
            break
        fi

        # Read available data with short timeout
        if read -r -t 0.5 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *"Press a key to enter CLI"* ]]; then
                found_prompt=1
                break
            fi
        else
            # Show progress every 5 seconds
            if [[ $((elapsed / 5)) -gt $last_progress ]] && [[ $elapsed -gt 0 ]]; then
                echo -n "."
                last_progress=$((elapsed / 5))
            fi
        fi
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
    start_time=$(date +%s)
    log_info "Waiting for HSS command prompt..."
    while true; do
        current_time=$(date +%s)
        if [[ $((current_time - start_time)) -ge 10 ]]; then
            break
        fi
        if read -r -t 0.5 -u 3 line 2>/dev/null; then
            echo "$line"
            buffer+="$line"
            if [[ "$buffer" == *">>"* ]]; then
                found_cli=1
                break
            fi
        fi
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

Supports two modes:
  S-Mode: Traditional HSS-based boot via USB mass storage flashing
  M-Mode: Direct eNVM programming via JTAG using mpfsBootmodeProgrammer

S-Mode Commands (HSS-based):
    all             Run full S-Mode workflow (build, flash via USB, capture)
    build           Build wolfBoot with HSS payload
    flash           Flash and verify only (assumes device in USBDMSC mode)
    capture         Capture wolfBoot output only (from $WOLFBOOT_TTY)

M-Mode Commands (JTAG-based):
    mmode           Run full M-Mode workflow (build, flash via JTAG, capture)
    mmode-build     Build wolfBoot for M-Mode only
    mmode-flash     Flash via JTAG only (assumes wolfboot.elf exists)
    mmode-capture   Capture M-Mode output only (from $MMODE_TTY)

HSS Commands (Hart Software Services):
    hss             Run full HSS workflow (build, program via JTAG, capture DDR debug)
    hss-build       Build HSS only
    hss-program     Program HSS via JTAG only (assumes HSS is built)
    hss-capture     Capture HSS DDR debug output only (from $HSS_TTY_DEBUG)

Common Commands:
    power-cycle     Power cycle the target only
    power-on        Power on the target
    power-off       Power off the target

S-Mode Options:
    -c, --config FILE       Config file (default: $CONFIG_FILE)
    -d, --device DEV        Block device (default: $BLOCK_DEV)
    -H, --hss-tty TTY       HSS serial port (default: $HSS_TTY)
    -W, --wolfboot-tty TTY  wolfBoot serial port (default: $WOLFBOOT_TTY)
    -s, --storage MODE      Storage mode: 'emmc' or 'sdcard' (default: none)

M-Mode Options:
    --mmode-config FILE     M-Mode config file (default: $MMODE_CONFIG)
    --mmode-tty TTY         E51 serial port (default: $MMODE_TTY)
    --mmode-tty-u54 TTY     U54 serial port (default: $MMODE_TTY_U54)
    --die DIE               Device die (default: $MMODE_DIE)
    --package PKG           Device package (default: $MMODE_PACKAGE)

HSS Options:
    --hss-dir DIR           HSS source directory (default: $HSS_DIR)
    --hss-board BOARD       HSS board name (default: $HSS_BOARD)
    --hss-tty TTY           HSS debug serial port (default: $HSS_TTY_DEBUG)

Common Options:
    -h, --help              Show this help message
    -p, --pi-host HOST      Pi host for power control (default: $PI_HOST)
    -g, --gpio PIN          GPIO pin for power control (default: $GPIO_PIN)
    -o, --output FILE       Output file for captured log
    -t, --timeout SEC       Timeout for capture (default: $TIMEOUT_WOLFBOOT)
    --skip-build            Skip the build step in 'all' or 'mmode' command

Environment Variables:
    S-Mode:
      HSS_TTY          HSS serial port
      WOLFBOOT_TTY     wolfBoot serial port
      BLOCK_DEV        Block device for flashing
      STORAGE_MODE     Storage mode: 'emmc' or 'sdcard'
      TIMEOUT_HSS      Timeout for HSS prompt (default: 30s)
      TIMEOUT_BLOCK    Timeout for block device (default: 15s)
      TIMEOUT_WOLFBOOT Timeout for capture (default: 30s)

    M-Mode:
      SC_INSTALL_DIR   SoftConsole installation directory (REQUIRED for M-Mode)
      MMODE_TTY        E51 serial port (default: /dev/ttyUSB1)
      MMODE_TTY_U54    U54 serial port (default: /dev/ttyUSB4)
      MMODE_CONFIG     M-Mode config file
      MMODE_DIE        Device die (default: MPFS250T)
      MMODE_PACKAGE    Device package (default: FCG1152)
      TIMEOUT_MMODE    Timeout for M-Mode capture (default: 30s)

    HSS:
      HSS_DIR          HSS source directory (default: ../hart-software-services)
      HSS_BOARD        HSS board name (default: mpfs-video-kit)
      HSS_TTY_DEBUG    HSS debug serial port (default: /dev/ttyUSB1)
      HSS_CROSS_COMPILE Cross compiler prefix for HSS
      TIMEOUT_HSS_CAPTURE Timeout for HSS capture (default: 60s)

    Common:
      PI_HOST          Pi host for power control
      GPIO_PIN         GPIO pin number
      BAUD_RATE        Serial baud rate (default: 115200)

Examples:
    # S-Mode (HSS-based boot)
    $(basename "$0") all                    # Full workflow (default eMMC)
    $(basename "$0") -s sdcard all          # Full workflow using SD card
    $(basename "$0") --skip-build all       # Flash existing build
    $(basename "$0") build                  # Build only

    # M-Mode (JTAG programming)
    $(basename "$0") mmode                  # Full M-Mode workflow
    $(basename "$0") --skip-build mmode     # Flash existing wolfboot.elf
    $(basename "$0") mmode-flash            # Just flash via JTAG
    $(basename "$0") mmode-capture          # Just capture M-Mode output
    $(basename "$0") -o test.log mmode      # Save output to specific file

    # HSS (Hart Software Services - for DDR debugging)
    $(basename "$0") hss                    # Full HSS workflow (build, program, capture)
    $(basename "$0") --skip-build hss       # Program existing HSS build
    $(basename "$0") hss-build              # Build HSS only
    $(basename "$0") hss-program            # Program HSS via JTAG only
    $(basename "$0") hss-capture            # Capture HSS DDR debug output
    $(basename "$0") -o ddr.log hss         # Save DDR debug to specific file

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

    # Step 2+3: Power cycle and enter USBDMSC mode
    # NOTE: These are combined because serial port must be opened BEFORE power cycling
    # to catch the HSS CLI prompt window (which has a short timeout)
    log_info "=== Step 2: Power cycling and entering USBDMSC mode ==="
    enter_usbdmsc_mode 1  # Pass 1 to trigger power cycle
    echo ""

    # Step 3: Wait for block device
    log_info "=== Step 3: Waiting for block device ==="
    wait_for_block_device
    echo ""

    # Step 4: Flash and verify
    log_info "=== Step 4: Flashing and verifying ==="
    flash_and_verify
    echo ""

    # Step 5: Unmount and power cycle again
    log_info "=== Step 5: Unmounting and power cycling for boot ==="
    unmount_block_device
    sleep 1
    power_cycle
    echo ""

    # Step 6: Set storage mode and boot, then capture wolfBoot output
    if [[ -n "$STORAGE_MODE" ]]; then
        # When storage mode is set, set_storage_and_boot handles both boot and capture
        # to avoid missing early wolfBoot output
        log_info "=== Step 6: Setting storage mode, booting, and capturing output ==="
        set_storage_and_boot "$output_file"
        echo ""
    else
        # No storage mode - just capture output (device boots automatically)
        log_info "=== Step 6: Capturing wolfBoot output ==="
        capture_wolfboot_output_timed "$output_file"
        echo ""
    fi

    log_success "=== Workflow completed successfully! ==="
}

# Build HSS
build_hss() {
    log_info "Building HSS..."
    log_info "HSS directory: $HSS_DIR"
    log_info "Board: $HSS_BOARD"

    if [[ ! -d "$HSS_DIR" ]]; then
        log_error "HSS directory not found: $HSS_DIR"
        exit 1
    fi

    pushd "$HSS_DIR" > /dev/null

    log_info "Running make clean..."
    make clean

    log_info "Building HSS for $HSS_BOARD..."
    make BOARD="$HSS_BOARD" CROSS_COMPILE="$HSS_CROSS_COMPILE" -j8

    popd > /dev/null

    log_success "HSS build completed successfully!"
}

# Program HSS via JTAG
program_hss() {
    log_info "Programming HSS via JTAG..."
    log_info "HSS directory: $HSS_DIR"
    log_info "Board: $HSS_BOARD"

    if [[ ! -d "$HSS_DIR" ]]; then
        log_error "HSS directory not found: $HSS_DIR"
        exit 1
    fi

    pushd "$HSS_DIR" > /dev/null

    make BOARD="$HSS_BOARD" program

    popd > /dev/null

    log_success "HSS programming completed!"
}

# Capture HSS DDR debug output
capture_hss_output() {
    local output_file="${1:-hss_ddr_output_$(date +%Y%m%d_%H%M%S).log}"

    log_info "Capturing HSS DDR debug output from $HSS_TTY_DEBUG..."
    log_info "Capture will run for $TIMEOUT_HSS_CAPTURE seconds"

    # Configure serial port
    stty -F "$HSS_TTY_DEBUG" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw 2>/dev/null || true

    # Capture output with timeout
    timeout "$TIMEOUT_HSS_CAPTURE" cat "$HSS_TTY_DEBUG" 2>/dev/null | tee "$output_file" || true

    echo ""
    log_success "HSS output captured to $output_file"

    # Check for DDR training result
    if grep -q "DDR_TRAINING_PASS" "$output_file" 2>/dev/null; then
        log_success "DDR training PASSED!"
    elif grep -q "DDR_TRAINING_FAIL" "$output_file" 2>/dev/null; then
        log_error "DDR training FAILED!"
    else
        log_warn "DDR training result not detected in output"
    fi
}

# HSS workflow: build, program, and capture DDR debug output
run_hss() {
    local skip_build="${1:-0}"
    local output_file="${2:-hss_ddr_output_$(date +%Y%m%d_%H%M%S).log}"

    log_info "Starting HSS programming workflow..."
    log_info "This will build HSS, program via JTAG, and capture DDR debug output"
    echo ""

    # Step 1: Build HSS (if not skipped)
    if [[ "$skip_build" -eq 0 ]]; then
        log_info "=== Step 1: Building HSS ==="
        build_hss
        echo ""
    else
        log_info "=== Step 1: Skipping HSS build ==="
        echo ""
    fi

    # Step 2: Power on target
    log_info "=== Step 2: Powering on target ==="
    power_on
    sleep 2  # Give device time to power up
    echo ""

    # Step 3: Start capture and program HSS
    # We start capture BEFORE programming so we don't miss any output
    # The JTAG programming will reset the device and it will boot with new HSS
    log_info "=== Step 3: Starting capture and programming HSS ==="

    # Configure serial port
    stty -F "$HSS_TTY_DEBUG" "$BAUD_RATE" cs8 -cstopb -parenb -echo raw 2>/dev/null || true

    # Start capture in background
    log_info "Starting HSS output capture (output: $output_file)..."
    timeout "$TIMEOUT_HSS_CAPTURE" cat "$HSS_TTY_DEBUG" 2>/dev/null > "$output_file" &
    local capture_pid=$!

    # Give capture a moment to start
    sleep 1

    # Program HSS (this will trigger a reset and boot)
    log_info "Programming HSS..."
    program_hss
    echo ""

    # Step 4: Wait for capture to complete
    log_info "=== Step 4: Waiting for capture to complete ==="
    log_info "Capturing for up to $TIMEOUT_HSS_CAPTURE seconds..."
    wait $capture_pid 2>/dev/null || true

    echo ""
    log_success "Capture completed: $output_file"

    # Show capture statistics
    local line_count=$(wc -l < "$output_file" 2>/dev/null || echo "0")
    local byte_count=$(wc -c < "$output_file" 2>/dev/null || echo "0")
    log_info "Captured $line_count lines ($byte_count bytes)"

    # Check for DDR training result
    if grep -q "DDR_TRAINING_PASS" "$output_file" 2>/dev/null; then
        log_success "=== HSS DDR Training PASSED! ==="
    elif grep -q "DDR_TRAINING_FAIL" "$output_file" 2>/dev/null; then
        log_error "=== HSS DDR Training FAILED ==="
    else
        log_warn "=== DDR training result not detected ==="
        log_info "Check $output_file for details"
    fi
}

# M-Mode workflow
run_mmode() {
    local skip_build="${1:-0}"
    local output_file="${2:-mmode_output_$(date +%Y%m%d_%H%M%S).log}"

    log_info "Starting M-Mode wolfBoot programming workflow..."
    log_info "Mode: JTAG programming to eNVM (bootmode 1)"
    echo ""

    # Step 1: Build (if not skipped)
    if [[ "$skip_build" -eq 0 ]]; then
        log_info "=== Step 1: Building wolfBoot (M-Mode) ==="
        build_wolfboot_mmode
        echo ""
    else
        log_info "=== Step 1: Skipping build ==="
        if [[ ! -f "wolfboot.elf" ]]; then
            log_error "wolfboot.elf not found. Cannot skip build."
            exit 1
        fi
        echo ""
    fi

    # Step 2: Power on target (JTAG requires power)
    log_info "=== Step 2: Powering on target ==="
    power_on
    sleep 2  # Give device time to power up
    echo ""

    # Step 3: Flash via JTAG
    log_info "=== Step 3: Flashing via JTAG ==="
    flash_jtag
    echo ""

    # Step 4: Power cycle to boot with new firmware
    log_info "=== Step 4: Power cycling to boot new firmware ==="
    power_off
    sleep 1
    power_on
    echo ""

    # Step 5: Capture M-Mode output (should show "wolfBoot Version: " if successful)
    log_info "=== Step 5: Capturing M-Mode output ==="
    log_info "Looking for 'wolfBoot Version:' in output..."
    capture_mmode_output "$output_file"
    echo ""

    # Check if wolfBoot started successfully
    if grep -q "wolfBoot" "$output_file" 2>/dev/null; then
        log_success "=== M-Mode workflow completed successfully! ==="
        log_success "wolfBoot output detected in $output_file"
    else
        log_warn "=== M-Mode workflow completed ==="
        log_warn "No wolfBoot output detected - check $output_file for details"
    fi
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
        -p|--pi-host)
            PI_HOST="$2"
            shift 2
            ;;
        -g|--gpio)
            GPIO_PIN="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -t|--timeout)
            TIMEOUT_WOLFBOOT="$2"
            TIMEOUT_MMODE="$2"
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
        --mmode-config)
            MMODE_CONFIG="$2"
            shift 2
            ;;
        --mmode-tty)
            MMODE_TTY="$2"
            shift 2
            ;;
        --mmode-tty-u54)
            MMODE_TTY_U54="$2"
            shift 2
            ;;
        --die)
            MMODE_DIE="$2"
            shift 2
            ;;
        --package)
            MMODE_PACKAGE="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --hss-dir)
            HSS_DIR="$2"
            shift 2
            ;;
        --hss-board)
            HSS_BOARD="$2"
            shift 2
            ;;
        --hss-tty)
            HSS_TTY_DEBUG="$2"
            shift 2
            ;;
        all|build|flash|capture|power-cycle|power-on|power-off|mmode|mmode-build|mmode-flash|mmode-capture|hss|hss-build|hss-program|hss-capture)
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

# Check dependencies based on command
case "$COMMAND" in
    mmode|mmode-build|mmode-flash|mmode-capture)
        check_mmode_dependencies
        ;;
    hss|hss-build|hss-program|hss-capture)
        # HSS commands need ssh for power control and make for building
        if ! command -v ssh &>/dev/null; then
            log_error "Required tool 'ssh' not found"
            exit 1
        fi
        if ! command -v make &>/dev/null; then
            log_error "Required tool 'make' not found"
            exit 1
        fi
        ;;
    power-cycle|power-on|power-off)
        # Power commands only need ssh
        if ! command -v ssh &>/dev/null; then
            log_error "Required tool 'ssh' not found"
            exit 1
        fi
        ;;
    *)
        check_dependencies
        ;;
esac

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
    mmode)
        run_mmode "$SKIP_BUILD" "$OUTPUT_FILE"
        ;;
    mmode-build)
        build_wolfboot_mmode
        ;;
    mmode-flash)
        if [[ ! -f "wolfboot.elf" ]]; then
            log_error "wolfboot.elf not found. Run 'mmode-build' first."
            exit 1
        fi
        flash_jtag
        ;;
    mmode-capture)
        capture_mmode_output "$OUTPUT_FILE"
        ;;
    hss)
        run_hss "$SKIP_BUILD" "$OUTPUT_FILE"
        ;;
    hss-build)
        build_hss
        ;;
    hss-program)
        power_on
        sleep 2
        program_hss
        ;;
    hss-capture)
        capture_hss_output "$OUTPUT_FILE"
        ;;
    power-cycle)
        power_cycle
        ;;
    power-on)
        power_on
        ;;
    power-off)
        power_off
        ;;
    *)
        log_error "Unknown command: $COMMAND"
        usage
        exit 1
        ;;
esac

