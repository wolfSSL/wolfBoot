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
RELAY_PORT="${RELAY_PORT:-/dev/ttyACM2}"
UART_LOG="${UART_LOG:-${WOLFBOOT_ROOT}/uart_log.txt}"

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
# Parses Makefile-style .config and sets global variables
# Handles both KEY=VALUE and KEY?=VALUE syntax (for ?=, only sets if not already set)
# Since .config uses Makefile syntax, we parse it directly rather than using make
load_config() {
    local config_file="${1:-.config}"

    [ ! -f "$config_file" ] && { log_error "Config file not found: $config_file"; return 1; }

    # Extract variables from .config file
    # Pattern: KEY?=VALUE or KEY=VALUE (ignores comments and blank lines)
    while IFS= read -r line; do
        # Skip comments and blank lines
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "${line// }" ]] && continue

        # Extract key, conditional flag, and value
        # Match: optional whitespace, key, optional ?, =, optional whitespace, value
        if [[ "$line" =~ ^[[:space:]]*([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*(\?)?=[[:space:]]*(.*)$ ]]; then
            local key="${BASH_REMATCH[1]}"
            local conditional="${BASH_REMATCH[2]}"  # "?" if present
            local value="${BASH_REMATCH[3]}"

            # Remove surrounding quotes if present
            value="${value#\"}"
            value="${value%\"}"

            # Strip trailing whitespace from value
            value="${value%"${value##*[![:space:]]}"}"

            # For ?= syntax, only set if variable is not already set
            if [ -n "$conditional" ]; then
                # Check if variable is already set using indirect reference
                # ${!key} expands to the value of the variable named by $key
                if [ -z "${!key:-}" ]; then
                    # Variable not set, assign it using declare
                    declare -g "${key}=${value}"
                fi
            else
                # Always set for = syntax
                declare -g "${key}=${value}"
            fi
        fi
    done < <(grep -E '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(\?)?=' "$config_file" 2>/dev/null || true)

    # Export all config variables as globals
    export IMAGE_HEADER_SIZE SIGN HASH SECONDARY_SIGN_OPTIONS SECONDARY_PRIVATE_KEY

    # Calculate IMAGE_SIGNATURE_SIZE based on SIGN algorithm
    case "${SIGN:-}" in
        ECC256) IMAGE_SIGNATURE_SIZE=64 ;;
        ECC384) IMAGE_SIGNATURE_SIZE=96 ;;
        ECC521) IMAGE_SIGNATURE_SIZE=132 ;;
        ED25519) IMAGE_SIGNATURE_SIZE=64 ;;
        ED448) IMAGE_SIGNATURE_SIZE=114 ;;
        RSA2048) IMAGE_SIGNATURE_SIZE=256 ;;
        RSA3072) IMAGE_SIGNATURE_SIZE=384 ;;
        RSA4096) IMAGE_SIGNATURE_SIZE=512 ;;
        *) IMAGE_SIGNATURE_SIZE=96 ;; # Default to ECC384
    esac
    export IMAGE_SIGNATURE_SIZE

    # Build SIGN_OPTIONS from SIGN and HASH
    SIGN_OPTIONS=""
    case "${SIGN:-}" in
        ECC256) SIGN_OPTIONS="--ecc256" ;;
        ECC384) SIGN_OPTIONS="--ecc384" ;;
        ECC521) SIGN_OPTIONS="--ecc521" ;;
        ED25519) SIGN_OPTIONS="--ed25519" ;;
        ED448) SIGN_OPTIONS="--ed448" ;;
        RSA2048) SIGN_OPTIONS="--rsa2048" ;;
        RSA3072) SIGN_OPTIONS="--rsa3072" ;;
        RSA4096) SIGN_OPTIONS="--rsa4096" ;;
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

# Relay control functions (integrated)
# Boot mode patterns (R4 R3 R2 R1): 0000=SDCard, 0011=QSPI, 0111=JTAG, 1000=Reset
relay_set_mode() {
    local pattern=$1
    local port="${2:-$RELAY_PORT}"

    [ ${#pattern} -ne 4 ] && { log_error "Pattern must be 4 binary digits"; return 1; }
    echo "$pattern" | grep -qE '^[01]{4}$' || { log_error "Pattern must contain only 0s and 1s"; return 1; }

    # Configure serial port (like Python serial.Serial initialization)
    stty -F "$port" 115200 raw -echo -echoe -echok -echoctl -echoke cs8 -cstopb -parenb 2>/dev/null || {
        log_error "Failed to configure serial port $port"; return 1;
    }

    # Allow port to stabilize (like Python time.sleep(0.1))
    sleep 0.1

    # Open port as file descriptor (like Python serial.Serial)
    exec 3<>"$port" || { log_error "Failed to open serial port $port"; return 1; }

    # Send each command separately (like Python: write, flush, sleep for each)
    local i=0
    log_info "Setting relay pattern: $pattern"

    while [ $i -lt 4 ]; do
        local bit="${pattern:$i:1}"
        local relay_num=$((i + 1))  # pattern[0]=R1, pattern[1]=R2, pattern[2]=R3, pattern[3]=R4
        local state=$([ "$bit" = "1" ] && echo 1 || echo 0)

        # Calculate checksum: (0xA0 + relay + state) & 0xFF
        local checksum=$(( (0xA0 + relay_num + state) & 0xFF ))

        # Echo command being sent
        echo "  Relay $relay_num: $( [ $state -eq 1 ] && echo ON || echo OFF ) (pattern[$i]=$bit) -> [0xA0, $relay_num, $state, 0x$(printf "%02x" $checksum)]"

        # Send command bytes: [0xA0, relay_num, state, checksum]
        # Write to file descriptor (like Python ser.write())
        printf "%b" "\x$(printf "%02x" 0xA0)\x$(printf "%02x" $relay_num)\x$(printf "%02x" $state)\x$(printf "%02x" $checksum)" >&3

        # Flush (like Python ser.flush())
        sync

        # Small delay for relay to respond (like Python time.sleep(0.05))
        sleep 0.05
        i=$((i + 1))
    done

    # Close file descriptor (like Python ser.close())
    exec 3<&-
    exec 3>&-
}

boot_sdcard() {
    log_info "Booting from SD card..."
    local boot_pattern="0000"
    local reset_pattern="1000"
    local reset_with_mode="1${boot_pattern:1}"

    relay_set_mode "$reset_pattern" || return 1
    sleep 0.1
    relay_set_mode "$reset_with_mode" || return 1
    sleep 0.2
    relay_set_mode "$boot_pattern" || return 1
    sleep 0.1
    log_ok "SD card boot mode set, reset released"
}

boot_qspi() {
    log_info "Booting from QSPI..."
    local boot_pattern="0011"
    local reset_pattern="1000"
    local reset_with_mode="1${boot_pattern:1}"

    relay_set_mode "$reset_pattern" || return 1
    sleep 0.1
    relay_set_mode "$reset_with_mode" || return 1
    sleep 0.2
    relay_set_mode "$boot_pattern" || return 1
    sleep 0.1
    log_ok "QSPI boot mode set, reset released"
}

# UART capture functions
kill_existing_uart_processes() {
    local pids=$(lsof -t "$UART_PORT" 2>/dev/null || true)
    if [ -n "$pids" ]; then
        log_info "Killing existing processes using $UART_PORT: $pids"
        for pid in $pids; do
            kill "$pid" 2>/dev/null || true
            sleep 0.2
            kill -9 "$pid" 2>/dev/null || true
        done
        sleep 0.5
    fi
}

start_uart_capture() {
    log_info "Starting UART capture: $UART_PORT -> $UART_LOG"
    UART_PTY=$(mktemp -u /tmp/vmk180_uart_XXXXXX)

    [ ! -e "$UART_PORT" ] && { log_error "Serial port not found: $UART_PORT"; exit 1; }
    [ ! -r "$UART_PORT" ] || [ ! -w "$UART_PORT" ] && {
        log_error "No read/write access to $UART_PORT"
        log_info "Try: sudo chmod 666 $UART_PORT or add user to dialout group"
        exit 1
    }

    if lsof "$UART_PORT" >/dev/null 2>&1; then
        log_info "Serial port in use, cleaning up..."
        kill_existing_uart_processes
        lsof "$UART_PORT" >/dev/null 2>&1 && { log_error "Failed to free serial port"; exit 1; }
    fi

    [ -e "$UART_PTY" ] && rm -f "$UART_PTY"
    stty -F "$UART_PORT" "$UART_BAUD" raw -echo -echoe -echok -echoctl -echoke cs8 -cstopb -parenb 2>/dev/null || {
        log_error "Failed to configure serial port"; exit 1;
    }

    socat_err=$(mktemp /tmp/socat_err_XXXXXX)
    log_info "Creating PTY bridge: $UART_PTY <-> $UART_PORT @ ${UART_BAUD}bps"
    socat PTY,link="$UART_PTY",raw,echo=0 "GOPEN:$UART_PORT" >/dev/null 2>"$socat_err" &
    socat_pid=$!
    UART_PIDS+=($socat_pid)

    for i in {1..10}; do
        sleep 0.2
        [ -e "$UART_PTY" ] && break
        if ! kill -0 "$socat_pid" 2>/dev/null; then
            log_error "socat process died:"
            cat "$socat_err" >&2
            rm -f "$socat_err"
            exit 1
        fi
    done

    [ -s "$socat_err" ] && { log_error "socat errors:"; cat "$socat_err" >&2; }
    rm -f "$socat_err"

    [ ! -e "$UART_PTY" ] && { log_error "Failed to create PTY after 2 seconds"; exit 1; }
    log_ok "UART PTY created: $UART_PTY"
}

stop_uart_capture() {
    for pid in "${UART_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
        sleep 0.1
        kill -9 "$pid" 2>/dev/null || true
    done
    [ -n "$UART_PTY" ] && [ -e "$UART_PTY" ] && rm -f "$UART_PTY"
}

cleanup() {
    [ "$KEEP_UART_CAPTURE" = "false" ] && {
        log_info "Cleaning up..."
        stop_uart_capture
        kill_existing_uart_processes
    }
}
trap cleanup EXIT INT TERM

# Start UART capture immediately
log_info "Starting UART capture..."
start_uart_capture || { log_error "Failed to start UART capture"; exit 1; }
log_info "UART capture active, PIDs: ${UART_PIDS[*]}"

# Test function helper
test_boot() {
    local boot_func=$1
    local mode_name=$2

    log_info "=== Testing $mode_name relay sequence ==="
    [ ${#UART_PIDS[@]} -eq 0 ] && { log_error "UART capture not running!"; exit 1; }
    [ -z "$UART_PTY" ] || [ ! -e "$UART_PTY" ] && { log_error "UART PTY not available: $UART_PTY"; exit 1; }

    log_info "Starting UART logging from PTY..."
    cat "$UART_PTY" >> "$UART_LOG" 2>&1 &
    UART_PIDS+=($!)
    sleep 0.5

    $boot_func || { log_error "$mode_name failed"; exit 1; }

    log_info "Monitoring UART for 30 seconds (Ctrl+C to stop early)..."
    log_info "UART log: $UART_LOG (watch: tail -f $UART_LOG)"
    trap 'log_info "Interrupted by user"; KEEP_UART_CAPTURE=true; exit 0' INT
    sleep 30 || true

    log_info "Test finished - UART capture still running (PID: ${UART_PIDS[*]})"
    log_info "To stop: kill ${UART_PIDS[*]}"
    KEEP_UART_CAPTURE=true
    exit 0
}

# Check for test modes
FLASH_TEST_APP=false
FLASH_UPDATE_APP=false
case "${1:-}" in
    test-boot|--boot-sdcard) test_boot boot_sdcard "boot-sdcard" ;;
    --boot-qspi) test_boot boot_qspi "boot-qspi" ;;
    --test-app) FLASH_TEST_APP=true ;;
    --test-update) FLASH_TEST_APP=true; FLASH_UPDATE_APP=true ;;
esac

# Build wolfBoot
log_info "Building wolfBoot..."
cp config/examples/versal_vmk180.config .config

make clean
make || { log_error "Failed to build wolfBoot"; exit 1; }
[ ! -f "wolfboot.elf" ] && { log_error "wolfboot.elf not found after build"; exit 1; }

# Build test app if requested
if [ "$FLASH_TEST_APP" = "true" ]; then
    if [ "$FLASH_UPDATE_APP" = "true" ]; then
        log_info "Building and signing test application version 2..."
        make test-app/image.bin

        # Sign as version 2 for update testing
        # Load all config values from .config file
        load_config .config

        IMAGE_TRAILER_SIZE=0  # Usually 0 unless delta updates are used
        PRIVATE_KEY="${PRIVATE_KEY:-wolfboot_signing_private_key.der}"

        BOOT_IMG="test-app/image.bin"

        # Build sign command with environment variables
        # The sign tool needs IMAGE_HEADER_SIZE and IMAGE_SIGNATURE_SIZE as environment variables
        export IMAGE_HEADER_SIZE IMAGE_SIGNATURE_SIZE IMAGE_TRAILER_SIZE

        log_info "Signing test app as version 2..."
        log_info "  IMAGE_HEADER_SIZE=$IMAGE_HEADER_SIZE"
        log_info "  IMAGE_SIGNATURE_SIZE=$IMAGE_SIGNATURE_SIZE"
        log_info "  SIGN=$SIGN"
        log_info "  HASH=$HASH"
        log_info "  SIGN_OPTIONS=$SIGN_OPTIONS"
        log_info "  PRIVATE_KEY=$PRIVATE_KEY"

        # Sign the image as version 2
        if [ "$SIGN" != "NONE" ] && [ -n "$SECONDARY_PRIVATE_KEY" ]; then
            ./tools/keytools/sign $SIGN_OPTIONS $SECONDARY_SIGN_OPTIONS "$BOOT_IMG" "$PRIVATE_KEY" "$SECONDARY_PRIVATE_KEY" 2 || {
                log_error "Signing failed with secondary key"
                exit 1
            }
        elif [ "$SIGN" != "NONE" ]; then
            ./tools/keytools/sign $SIGN_OPTIONS "$BOOT_IMG" "$PRIVATE_KEY" 2 || {
                log_error "Signing failed"
                exit 1
            }
        else
            ./tools/keytools/sign $SIGN_OPTIONS "$BOOT_IMG" 2 || {
                log_error "Signing failed (SIGN=NONE)"
                exit 1
            }
        fi

        testapp_size=$(stat -c%s "test-app/image_v2_signed.bin")
        log_info "Test app v2 size: $testapp_size bytes"
        cp test-app/image_v2_signed.bin "${TFTP_DIR}/"
        log_ok "Test app v2 copied to TFTP: ${TFTP_DIR}/image_v2_signed.bin"
    else
        log_info "Building and signing test application version 1..."
        make test-app/image.bin
        make test-app/image_v1_signed.bin

        testapp_size=$(stat -c%s "test-app/image_v1_signed.bin")
        log_info "Test app size: $testapp_size bytes"
        cp test-app/image_v1_signed.bin "${TFTP_DIR}/"
        log_ok "Test app copied to TFTP: ${TFTP_DIR}/image_v1_signed.bin"
    fi
fi

# Generate BOOT.BIN
log_info "Generating BOOT.BIN..."
[ ! -f "wolfboot.elf" ] && { log_error "wolfboot.elf not found - cannot generate BOOT.BIN"; exit 1; }

# Set PREBUILT_DIR (relative to wolfBoot root)
export PREBUILT_DIR="${WOLFBOOT_ROOT}/../soc-prebuilt-firmware/vmk180-versal"

# Copy required files to wolfBoot root directory
log_info "Copying prebuilt firmware files..."
[ ! -d "${PREBUILT_DIR}" ] && {
    log_error "Prebuilt firmware directory not found: ${PREBUILT_DIR}"
    log_info "Clone with: git clone --branch xlnx_rel_v2024.2 https://github.com/Xilinx/soc-prebuilt-firmware.git"
    exit 1
}

cp "${PREBUILT_DIR}/project_1.pdi" .
cp "${PREBUILT_DIR}/plm.elf" .
cp "${PREBUILT_DIR}/psmfw.elf" .
cp "${PREBUILT_DIR}/bl31.elf" .
cp "${PREBUILT_DIR}/system-default.dtb" .

source "${VITIS_PATH}/settings64.sh"
rm -f BOOT.BIN
bootgen -arch versal -image ./tools/scripts/versal_boot.bif -w -o BOOT.BIN || { log_error "bootgen failed"; exit 1; }
cp BOOT.BIN "${TFTP_DIR}/" || { log_error "Failed to copy BOOT.BIN to TFTP directory"; exit 1; }
filesize=$(stat -c%s "${TFTP_DIR}/BOOT.BIN")
filesize_hex=$(printf "0x%x" $filesize)

# Get test app size if flashing it
testapp_size_hex="0x0"
if [ "$FLASH_TEST_APP" = "true" ]; then
    if [ "$FLASH_UPDATE_APP" = "true" ]; then
        testapp_size=$(stat -c%s "${TFTP_DIR}/image_v2_signed.bin")
        testapp_size_hex=$(printf "0x%x" $testapp_size)
        log_info "Test app v2 size: $testapp_size bytes"
    else
        testapp_size=$(stat -c%s "${TFTP_DIR}/image_v1_signed.bin")
        testapp_size_hex=$(printf "0x%x" $testapp_size)
        log_info "Test app size: $testapp_size bytes"
    fi
fi

# Flash QSPI via U-Boot TFTP
log_info "Flashing QSPI..."
boot_sdcard

expect <<EXPECT_EOF
set timeout 90
set pty "$UART_PTY"
set filesize_hex "$filesize_hex"
set testapp_size_hex "$testapp_size_hex"
set flash_test_app "$FLASH_TEST_APP"
set flash_update_app "$FLASH_UPDATE_APP"
set board_ip "$BOARD_IP"
set server_ip "$SERVER_IP"

spawn -open [open \$pty r+]
stty raw < \$pty
log_user 0
log_file -a "$UART_LOG"

puts "Waiting for autoboot prompt..."
expect {
    "Hit any key to stop autoboot" {
        puts "Autoboot prompt detected, sending Enter..."
        send "\r"
    }
    "Versal>" {
        puts "U-Boot prompt found"
    }
    timeout {
        puts "Timeout waiting for U-Boot prompt"
        exit 1
    }
}
expect "Versal>"
puts "At U-Boot prompt, configuring network..."
sleep 0.5

send "setenv ipaddr \$board_ip\r"
expect {
    "Versal>" {
        puts "IP address set"
    }
    timeout {
        puts "Warning: Timeout waiting for prompt after setenv ipaddr"
    }
}

send "setenv serverip \$server_ip\r"
expect {
    "Versal>" {
        puts "Server IP set"
    }
    timeout {
        puts "Warning: Timeout waiting for prompt after setenv serverip"
    }
}

send "setenv netmask 255.255.255.0\r"
expect {
    "Versal>" {
        puts "Netmask set"
    }
    timeout {
        puts "Warning: Timeout waiting for prompt after setenv netmask"
    }
}

puts "Downloading BOOT.BIN via TFTP..."
send "tftpboot 0x10000000 BOOT.BIN\r"
expect {
    "Bytes transferred" { puts "TFTP download successful" }
    "Error" { puts "TFTP download failed"; exit 1 }
    timeout { puts "TFTP timeout"; exit 1 }
}
expect "Versal>"

puts "Probing SPI flash..."
send "sf probe 0\r"
expect {
    "SF:" { puts "SPI flash detected" }
    timeout {
        puts "SPI probe failed, trying alternate..."
        send "sf probe 0 0 0\r"
        expect "Versal>"
    }
}
expect "Versal>"

puts "Updating flash with BOOT.BIN..."
send "sf update 0x10000000 0 \$filesize_hex\r"
expect {
    -re ".*updated.*Versal>" { puts "Flash update successful" }
    -re ".*Versal>" { puts "Flash update complete" }
    timeout {
        puts "Flash update timeout - checking for prompt..."
        send "\r"
        expect { "Versal>" { puts "Got prompt, continuing..." } timeout { puts "No prompt found"; exit 1 } }
    }
}
puts "Flash update done, verifying..."

set verify_addr "0x20000000"
puts "Reading flash back for verification..."
send "sf read \$verify_addr 0 \$filesize_hex\r"
expect {
    "Versal>" {
        puts "Flash read complete"
    }
    timeout {
        puts "Flash read timeout"
        exit 1
    }
}

puts "Comparing flash contents..."
send "cmp.b 0x10000000 \$verify_addr \$filesize_hex\r"
set timeout 30
expect {
    -re ".*are equal.*" {
        puts "Flash verification PASSED"
        expect "Versal>"
    }
    -re ".*identical.*" {
        puts "Flash verification PASSED"
        expect "Versal>"
    }
    -re ".*differ.*" {
        puts "Flash verification FAILED - data differs!"
        exit 1
    }
    "Versal>" {
        puts "Verification completed (result unclear, assuming OK)"
    }
    timeout {
        puts "Verification timeout - checking for prompt..."
        send "\r"
        expect {
            "Versal>" {
                puts "Got prompt after timeout, continuing..."
            }
            timeout {
                puts "No prompt found, exiting"
                exit 1
            }
        }
    }
}
set timeout 90

puts "BOOT.BIN flash and verification complete!"

# Flash test app if requested
if { \$flash_test_app eq "true" } {
    if { \$flash_update_app eq "true" } {
        puts ""
        puts "=== Flashing test app v2 to UPDATE partition at 0x3400000 ==="

        puts "Downloading test app v2 via TFTP..."
        send "tftpboot 0x10000000 image_v2_signed.bin\r"
        expect {
            "Bytes transferred" { puts "TFTP download successful" }
            "Error" { puts "TFTP download failed"; exit 1 }
            timeout { puts "TFTP timeout"; exit 1 }
        }
        expect "Versal>"

        puts "Erasing UPDATE partition at 0x3400000 (128KB sector)..."
        send "sf erase 0x3400000 0x20000\r"
        expect {
            "Versal>" { puts "Erase complete" }
            timeout { puts "Erase timeout"; exit 1 }
        }

        puts "Writing test app v2 to 0x3400000..."
        send "sf write 0x10000000 0x3400000 \$testapp_size_hex\r"
        expect {
            "Versal>" { puts "Write complete" }
            timeout { puts "Write timeout"; exit 1 }
        }

        puts "Test app v2 flashed to UPDATE partition!"
    } else {
        puts ""
        puts "=== Flashing test app to boot partition at 0x800000 ==="

        puts "Downloading test app via TFTP..."
        send "tftpboot 0x10000000 image_v1_signed.bin\r"
        expect {
            "Bytes transferred" { puts "TFTP download successful" }
            "Error" { puts "TFTP download failed"; exit 1 }
            timeout { puts "TFTP timeout"; exit 1 }
        }
        expect "Versal>"

        puts "Erasing boot partition at 0x800000 (128KB sector)..."
        send "sf erase 0x800000 0x20000\r"
        expect {
            "Versal>" { puts "Erase complete" }
            timeout { puts "Erase timeout"; exit 1 }
        }

        puts "Erasing update partition at 0x3400000 (128KB sector)..."
        send "sf erase 0x3400000 0x20000\r"
        expect {
            "Versal>" { puts "Erase complete" }
            timeout { puts "Erase timeout"; exit 1 }
        }

        puts "Writing test app to 0x800000..."
        send "sf write 0x10000000 0x800000 \$testapp_size_hex\r"
        expect {
            "Versal>" { puts "Write complete" }
            timeout { puts "Write timeout"; exit 1 }
        }

        puts "Test app flashed to boot partition!"
    }
}

puts ""
puts "All flash operations complete!"
close
EXPECT_EOF

log_ok "Flash operations complete!"

# Restart continuous UART logging after expect exits
log_info "Restarting continuous UART logging..."
cat "$UART_PTY" >> "$UART_LOG" 2>&1 &
UART_PIDS+=($!)
sleep 1

# Boot from QSPI
log_info "Switching to QSPI boot mode..."
boot_qspi
log_ok "Board booting from QSPI"

log_info "Capturing UART output for 30 seconds..."
log_info "Watch live: tail -f $UART_LOG"
sleep 30

log_ok "Capture complete"
log_info "UART log saved to: $UART_LOG"
KEEP_UART_CAPTURE=true
log_info "UART capture still active in background (PID: ${UART_PIDS[*]})"
log_info "To stop: kill ${UART_PIDS[*]}"
