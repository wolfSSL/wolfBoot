# build_flash_qspi.sh

All-in-one script for building wolfBoot, generating BOOT.BIN, flashing QSPI, and booting VMK180.

wolfBoot replaces U-Boot in the Versal boot flow:

```
PLM (PMC) -> PSM -> BL31 (EL3) -> wolfBoot (EL2) -> Linux (EL1)
```

## Usage

```bash
# Full build, flash, and boot from QSPI
./build_flash_qspi.sh

# Test boot mode switching only (no build/flash)
./build_flash_qspi.sh --boot-sdcard  # Test SD card boot mode
./build_flash_qspi.sh --boot-qspi    # Test QSPI boot mode
```

## What It Does

1. **Builds wolfBoot**: Compiles wolfBoot from source
2. **Generates BOOT.BIN**:
   - Copies prebuilt firmware files from `../soc-prebuilt-firmware/vmk180-versal/` to wolfBoot root
   - Runs bootgen to create BOOT.BIN
   - Copies BOOT.BIN to TFTP directory
3. **Flashes QSPI**:
   - Sets board to SD card boot mode
   - Captures UART output via PTY bridge
   - Interrupts U-Boot autoboot
   - Configures network (TFTP server/client)
   - Downloads BOOT.BIN via TFTP
   - Erases and programs QSPI flash
   - Verifies flash contents
4. **Boots from QSPI**:
   - Switches boot mode to QSPI
   - Captures UART output for 30 seconds

## Prerequisites

- **Prebuilt firmware**: Clone `soc-prebuilt-firmware` repository:
  ```bash
  git clone --branch xlnx_rel_v2024.1 https://github.com/Xilinx/soc-prebuilt-firmware.git
  ```
  Place it as a sibling directory to wolfBoot (i.e., `../soc-prebuilt-firmware/`)

- **TFTP server**: Install and configure:
  ```bash
  sudo apt install tftpd-hpa
  sudo mkdir -p /srv/tftp
  sudo chmod 777 /srv/tftp
  ```

- **Required tools**: `expect` and `socat`
  ```bash
  sudo apt install expect socat
  ```

- **Vitis 2024.1 or 2024.2**: Required for bootgen
  ```bash
  export VITIS_PATH=/opt/Xilinx/Vitis/2024.1
  ```

- **Relay board**: Connected to `/dev/ttyACM2` (configurable via `RELAY_PORT`)

- **UART connection**: VMK180 UART0 connected (default: `/dev/ttyUSB2`)

- **ARM Toolchain**: `aarch64-none-elf-gcc`

## Configuration

Environment variables can be customized:

```bash
export UART_PORT=/dev/ttyUSB2      # VMK180 UART port
export UART_BAUD=115200            # UART baud rate
export SERVER_IP=10.0.4.24         # TFTP server IP (host PC)
export BOARD_IP=10.0.4.90          # VMK180 IP address
export TFTP_DIR=/srv/tftp           # TFTP directory
export VITIS_PATH=/opt/Xilinx/Vitis/2024.1  # Vitis installation
export RELAY_PORT=/dev/ttyACM2      # Relay board serial port
export UART_LOG=./uart_log.txt     # UART log file
```

## UART Capture

The script automatically captures UART output:
- Creates a PTY bridge using `socat` for reliable capture
- Logs all output to `uart_log.txt` (default)
- Continues capturing after flash completes
- Press Ctrl+C to stop early (capture continues in background)

View live output:
```bash
tail -f uart_log.txt
```

## Troubleshooting

### Prebuilt firmware not found

Ensure `soc-prebuilt-firmware` is cloned as a sibling directory to wolfBoot:
```bash
cd ..
git clone --branch xlnx_rel_v2024.1 https://github.com/Xilinx/soc-prebuilt-firmware.git
```

### TFTP timeout

- Check network connection between host PC and VMK180
- Verify IP addresses match (`SERVER_IP` and `BOARD_IP`)
- Ensure TFTP server is running: `sudo systemctl status tftpd-hpa`

### UART capture fails

- Verify UART port permissions: `sudo chmod 666 /dev/ttyUSB*`
- Check UART port is correct: `ls -la /dev/ttyUSB*`
- Ensure no other process is using the port

### Relay control fails

- Check relay board connection and port (`RELAY_PORT`)
- Verify relay board is powered and connected
- Check serial port permissions: `sudo chmod 666 /dev/ttyACM*`

### Boot hangs or wolfBoot doesn't start

- Verify BL31 is correctly jumping to 0x8000000
- Check that wolfBoot entry point matches: `aarch64-none-elf-readelf -h wolfboot.elf`
- Check UART output for error messages
- Verify prebuilt firmware files are correct for your board revision
