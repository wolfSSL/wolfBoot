#!/usr/bin/env python3
"""
mpfs_qspi_prog.py - Program QSPI flash on PolarFire SoC MPFS250 via wolfBoot UART

Requires wolfBoot built with UART_QSPI_PROGRAM=1 and EXT_FLASH=1.

Usage:
    python3 mpfs_qspi_prog.py <serial_port> <binary_file> [qspi_offset]

Arguments:
    serial_port   Serial device, e.g. /dev/ttyUSB0
    binary_file   Signed firmware image, e.g. test-app/image_v1_signed.bin
    qspi_offset   Hex or decimal QSPI destination address (default: 0x20000)
                  Use 0x20000  for the boot partition  (WOLFBOOT_PARTITION_BOOT_ADDRESS)
                  Use 0x2000000 for the update partition (WOLFBOOT_PARTITION_UPDATE_ADDRESS)

Example:
    python3 tools/scripts/mpfs_qspi_prog.py /dev/ttyUSB0 \\
        test-app/image_v1_signed.bin 0x20000

Protocol (implemented in hal/mpfs250.c qspi_uart_program()):
    1. wolfBoot prints "QSPI-PROG: Press 'P' ..." prompt at startup
    2. This script sends 'P' to enter programming mode
    3. wolfBoot sends "READY\\r\\n"
    4. Script sends 8 bytes: [4B LE address][4B LE size]
    5. wolfBoot erases sectors and sends "ERASED\\r\\n"
    6. For each 256-byte chunk:
         wolfBoot sends ACK (0x06) -> script sends chunk
    7. wolfBoot sends "DONE\\r\\n" and continues booting

Copyright (C) 2025 wolfSSL Inc.
GPL v3
"""

import sys
import os
import time
import struct

try:
    import serial
except ImportError:
    print("Error: 'pyserial' not installed. Run: pip install pyserial")
    sys.exit(1)

BAUD_RATE  = 115200
CHUNK_SIZE = 256
ACK_BYTE   = 0x06
PROMPT_TIMEOUT  = 60   # seconds to wait for wolfBoot prompt after reset
ERASE_TIMEOUT   = 120  # seconds for sector erase (64KB sectors, ~800ms each)
DONE_TIMEOUT    = 30   # seconds to wait for DONE after last chunk


def wait_for(port, keyword, timeout_sec, label=""):
    """Read lines until one contains keyword, printing each line received."""
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        port.timeout = min(1.0, remaining)
        line = port.readline()
        if not line:
            continue
        text = line.decode("ascii", errors="replace").rstrip()
        if text:
            tag = f"[{label}] " if label else ""
            print(f"  {tag}< {text}")
        if keyword in text:
            return True
    return False


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    port_name = sys.argv[1]
    bin_path  = sys.argv[2]
    offset    = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x20000

    if offset < 0 or offset > 0xFFFFFFFF:
        print(f"Error: offset out of range (0x0..0xFFFFFFFF, got 0x{offset:X})")
        sys.exit(1)

    if not os.path.exists(bin_path):
        print(f"Error: file not found: {bin_path}")
        sys.exit(1)

    with open(bin_path, "rb") as f:
        data = f.read()

    # Device-side rejects images > 2 MiB; fail fast here to avoid
    # waiting through prompt/erase only to have the target abort.
    MAX_IMAGE_SIZE = 0x200000  # 2 MiB, matches device-side check
    if len(data) > MAX_IMAGE_SIZE:
        print(f"Error: image too large ({len(data):,} bytes, max {MAX_IMAGE_SIZE:,})")
        sys.exit(1)

    print(f"wolfBoot QSPI programmer for PolarFire SoC MPFS250")
    print(f"  Port      : {port_name} @ {BAUD_RATE} baud")
    print(f"  Binary    : {bin_path} ({len(data):,} bytes)")
    print(f"  QSPI addr : 0x{offset:08x}")
    n_sectors = (len(data) + 0x10000 - 1) // 0x10000
    print(f"  Sectors   : {n_sectors} x 64 KB")
    print()

    try:
        port = serial.Serial(port_name, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"Error: cannot open {port_name}: {e}")
        sys.exit(1)

    # Drain stale RX data
    port.reset_input_buffer()
    time.sleep(0.1)

    # ------------------------------------------------------------------
    # Step 1: Wait for the QSPI-PROG prompt from wolfBoot
    # ------------------------------------------------------------------
    print(f"Waiting for wolfBoot 'QSPI-PROG' prompt (up to {PROMPT_TIMEOUT}s)...")
    print("  Power-cycle or reset the board now if it has already booted.")
    print()

    if not wait_for(port, "QSPI-PROG", PROMPT_TIMEOUT, "wolfBoot"):
        print("Error: timed out waiting for 'QSPI-PROG' prompt.")
        print("  Is wolfBoot built with UART_QSPI_PROGRAM=1 and EXT_FLASH=1?")
        port.close()
        sys.exit(1)

    # ------------------------------------------------------------------
    # Step 2: Send 'P' to enter programming mode
    # ------------------------------------------------------------------
    print("  Sending 'P' to enter programming mode...")
    port.write(b"P")
    port.flush()

    if not wait_for(port, "READY", 5, "wolfBoot"):
        print("Error: did not receive READY acknowledgement.")
        port.close()
        sys.exit(1)

    # ------------------------------------------------------------------
    # Step 3: Send destination address and data size (8 bytes, little-endian)
    # ------------------------------------------------------------------
    header = struct.pack("<II", offset, len(data))
    print(f"  Sending address=0x{offset:x}, size={len(data):,} bytes...")
    port.write(header)
    port.flush()

    # ------------------------------------------------------------------
    # Step 4: Wait for erase to complete
    # ------------------------------------------------------------------
    print(f"  Waiting for erase ({n_sectors} sectors, up to {ERASE_TIMEOUT}s)...")
    if not wait_for(port, "ERASED", ERASE_TIMEOUT, "wolfBoot"):
        print("Error: timed out waiting for ERASED.")
        port.close()
        sys.exit(1)

    # ------------------------------------------------------------------
    # Step 5: Chunk transfer (ACK-driven)
    # ------------------------------------------------------------------
    print(f"  Transferring {len(data):,} bytes in {CHUNK_SIZE}-byte chunks...")
    sent  = 0
    total = len(data)
    port.timeout = 5  # per-ACK receive timeout

    while sent < total:
        # Wait for ACK byte (0x06) from wolfBoot requesting the next chunk
        ack = port.read(1)
        if not ack:
            print(f"\nError: timeout waiting for ACK at offset {sent:#x}")
            port.close()
            sys.exit(1)
        if ack[0] != ACK_BYTE:
            print(f"\nError: expected ACK 0x06, got {ack!r} at offset {sent:#x}")
            port.close()
            sys.exit(1)

        chunk = data[sent : sent + CHUNK_SIZE]
        # Send in small pieces: some USB-UART bridges (e.g., PolarFire
        # Video Kit) stall on bulk writes. 8-byte pieces with 10ms
        # pauses prevent the bridge's TX FIFO from stalling.
        for ci in range(0, len(chunk), 8):
            port.write(chunk[ci:ci+8])
            port.flush()
            time.sleep(0.010)
        sent += len(chunk)

        pct = sent * 100 // total
        bar = "#" * (pct // 2) + "." * (50 - pct // 2)
        print(f"\r  [{bar}] {sent:,}/{total:,} ({pct}%)", end="", flush=True)

    print()  # newline after progress bar

    # ------------------------------------------------------------------
    # Step 6: Wait for DONE
    # ------------------------------------------------------------------
    print("  Waiting for DONE...")
    port.timeout = 1
    if not wait_for(port, "DONE", DONE_TIMEOUT, "wolfBoot"):
        print("Error: timed out waiting for DONE.")
        port.close()
        sys.exit(1)

    print()
    print(f"Programming complete!")
    print(f"  Wrote {len(data):,} bytes to QSPI offset 0x{offset:08x}")
    print(f"  wolfBoot is continuing to boot the new image.")

    # ------------------------------------------------------------------
    # Step 7: Monitor UART output after programming (boot attempt)
    # ------------------------------------------------------------------
    monitor_sec = 15
    print(f"\n  Monitoring UART output for {monitor_sec}s...")
    print("  (Press Ctrl+C to stop early)\n")
    port.timeout = 0.1
    try:
        deadline = time.monotonic() + monitor_sec
        while time.monotonic() < deadline:
            line = port.readline()
            if line:
                text = line.decode("ascii", errors="replace").rstrip()
                if text:
                    print(f"  [boot] < {text}")
    except KeyboardInterrupt:
        print("\n  (stopped)")

    port.close()


if __name__ == "__main__":
    main()
