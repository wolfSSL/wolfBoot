#!/usr/bin/env python3
# lpc54s0xx_patch_boot_block.py
#
# Patch a wolfBoot binary for the NXP LPC540xx / LPC54S0xx SPIFI (XIP) boot
# ROM. The ROM expects an "enhanced boot block":
#   - offset 0x1C:  vector table checksum (negated sum of the first 7 words)
#   - offset 0x24:  boot block marker + offset to descriptor
#   - offset 0x160: 25-word descriptor (magic, mode, image base, image size, ...)
#
# Usage: lpc54s0xx_patch_boot_block.py <wolfboot.bin>
#
# Copyright (C) 2025 wolfSSL Inc.
# This file is part of wolfBoot (GPL-2.0-or-later).

import os
import struct
import sys

HEADER_MARKER_OFFSET = 0x24
BOOT_BLOCK_OFFSET    = 0x160
VECTOR_CHECKSUM_OFFSET = 0x1C
IMAGE_BASE_ADDR      = 0x10000000  # SPIFI XIP base

HEADER_MARKER_FMT  = "<2I"          # 0xEDDC94BD, 0x160
BOOT_BLOCK_FMT     = "<25I"
VECTOR_TABLE_FMT   = "<7I"          # first 7 words covered by checksum


def patch(path):
    size = os.path.getsize(path)
    header_marker_size = struct.calcsize(HEADER_MARKER_FMT)
    boot_block_size    = struct.calcsize(BOOT_BLOCK_FMT)
    vector_table_size  = struct.calcsize(VECTOR_TABLE_FMT)

    min_size = max(
        vector_table_size,
        HEADER_MARKER_OFFSET + header_marker_size,
        BOOT_BLOCK_OFFSET + boot_block_size,
    )
    if size < min_size:
        raise SystemExit(
            "error: %s is too small for LPC54S0xx boot block patching "
            "(size=%d, need at least %d bytes)" % (path, size, min_size)
        )

    with open(path, "r+b") as f:
        f.seek(HEADER_MARKER_OFFSET)
        f.write(struct.pack(HEADER_MARKER_FMT, 0xEDDC94BD, BOOT_BLOCK_OFFSET))

        f.seek(BOOT_BLOCK_OFFSET)
        f.write(struct.pack(
            BOOT_BLOCK_FMT,
            0xFEEDA5A5,      # magic
            3,               # image type
            IMAGE_BASE_ADDR, # image base
            size - 4,        # image size (minus CRC slot)
            0, 0, 0, 0, 0,
            0xEDDC94BD,      # header marker echo
            0, 0, 0,
            0x001640EF,      # SPIFI config
            0, 0,
            0x1301001D,      # clock/flash timing word
            0, 0, 0,
            0x00000100,      # options
            0, 0,
            0x04030050,      # PLL config
            0x14110D09,      # clock divider config
        ))

        f.seek(0)
        words = struct.unpack(VECTOR_TABLE_FMT, f.read(vector_table_size))
        checksum = (0x100000000 - (sum(words) & 0xFFFFFFFF)) & 0xFFFFFFFF
        f.seek(VECTOR_CHECKSUM_OFFSET)
        f.write(struct.pack("<I", checksum))

    print("\tvector checksum: 0x%08X" % checksum)


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: %s <wolfboot.bin>" % argv[0])
    patch(argv[1])


if __name__ == "__main__":
    main(sys.argv)
