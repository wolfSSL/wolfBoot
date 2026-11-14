#!/usr/bin/env python3
# s32g2_mkimage.py - wrap wolfboot.bin for the S32G2 Boot ROM.
#
#   s32g2_mkimage.py <wolfboot.bin> <out.bin> [--entry 0x340A0000]
#                    [--qspi [--qspi-params sdr|octal]]
#
# The output is the SD/eMMC content from byte offset 0x1000 (dd it with
# seek=8): the Image Vector Table, a no-op DCD and the Application Boot Code
# Header, with the code 0x200-aligned behind it. The layout and header fields
# are the ones the stock BSP image uses (U-Boot's mkimage -T s32gen1image /
# s32ccimage), so the Boot ROM finds them where it always has.
#
# With --qspi the output is the QuadSPI NOR content from offset 0: the IVT at
# 0, the 0x200-byte QuadSPI parameter block the ROM programs the controller
# with at 0x200, then the DCD and the application. "sdr" (default) reads the
# flash in plain SPI mode, which leaves it in the mode wolfBoot's own
# driver expects; "octal" is the stock BSP block for the MX25UW51245G, which
# switches the flash to octal DTR at 200 MHz.
#
# The ROM copies code_length bytes from right after the 0x40-byte header to
# ram_start and jumps to ram_entry. Entry must land on a 0x200-aligned file
# offset, so ram_start is entry minus the pad between the header and the code.
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3.

import struct
import sys

SD_IVT_OFFSET = 0x1000          # where this file lands on the card
IVT_OFF = 0x0000                # offsets below are relative to the IVT
QSPI_PARAMS_OFF = 0x0200        # QuadSPI boot only, fixed by the ROM
DCD_OFF = 0x0200
QSPI_DCD_OFF = 0x0400
APP_OFF = 0x2200                # application boot code header
CODE_ALIGN = 0x200
APP_HDR_SIZE = 0x40

SRAM_BASE = 0x34000000
SRAM_SIZE = 0x00800000
# Boot ROM working areas while it loads from SD; the image must not be copied
# over them (U-Boot tools/s32gen1image.c).
SD_RESERVED = [(0x34008000, 0x34078000), (0x343FF000, 0x34400000)]


def ivt(dcd_ptr, app_ptr):
    b = bytearray(0x100)
    b[0] = 0xD1                                   # tag
    b[1:3] = (0x100).to_bytes(2, "big")           # length, big-endian
    b[3] = 0x60                                   # version
    struct.pack_into("<I", b, 0x10, dcd_ptr)      # DCD pointer
    struct.pack_into("<I", b, 0x20, app_ptr)      # application boot code pointer
    struct.pack_into("<I", b, 0x28, 1)            # boot configuration: A53_0
    return bytes(b)


def dcd_nop():
    # header: tag 0xD2, length 0x14 big-endian, version 0x60; four NOPs
    return bytes([0xD2, 0x00, 0x14, 0x60]) + bytes([0xC0, 0x00, 0x04, 0x00]) * 4


# QuadSPI parameter block (U-Boot tools/s32cc_image_params.h): the controller
# registers the ROM loads, its read LUT sequence (command_seq, sequence 0) and
# up to ten flash register writes it performs first.
def qspi_params(kind):
    lut = lambda ins, pad, opr: ((ins << 10) | (pad << 8) | (opr & 0xFF))
    writes = []
    if kind == "octal":
        # arch/arm/mach-s32/s32-cc/tools/macronix_params.c, verbatim
        regs = [0x5a5a5a5a, 0x030f00cc, 0x00010303, 0x00000000, 0xc280000c,
                0x00000000, 0x00020000, 0x44000000, 0x40ff40ff, 0x20000000,
                0x20000000, 0xaa553443, 0x00000000, 0x00000000, 0x00000000]
        flags = [0x00, 0x00, 0x01, 0x00, 200]
        seq = [0x471147ee, 0x0f142b20, 0x00003b10]
        # write enable, then WRCR2 address 0 = 2 (DTR OPI)
        writes = [(0x06, 0, 0, 0, 0, 0), (0x72, 1, 1, 32, 0x0, 0x2)]
    else:
        # SDR, one data line, 4-byte fast read (0x0C, 8 dummy cycles), DQS
        # from the internal pad loopback, DLL slave bypassed: the
        # configuration hal/s32g2_qspi.c uses.
        regs = [0x5a5a5a5a, 0x010f004c, 0x00000303, 0x00000000, 0x00000006,
                0x00000000, 0x00000000, 0x00000020, 0x40ff40ff, 0x04000000,
                0x04000000, 0xaa553443, 0x00000000, 0x00000000, 0x00000000]
        flags = [0x01, 0x00, 0x00, 0x00, 100]
        e = [lut(1, 0, 0x0C), lut(2, 0, 0x20), lut(3, 0, 8), lut(7, 0, 0x80), 0]
        seq = [e[0] | (e[1] << 16), e[2] | (e[3] << 16), e[4]]
    b = bytearray(0x200)
    struct.pack_into("<15I", b, 0, *regs)
    struct.pack_into("<5B", b, 60, *flags)
    for i, w in enumerate(seq):
        struct.pack_into("<I", b, 68 + 4 * i, w)
    for i, (op, valid, cdata, asz, addr, data) in enumerate(writes):
        cfg = op | (asz << 18) | (cdata << 24) | (valid << 31)
        struct.pack_into("<III", b, 388 + 12 * i, cfg, addr, data)
    return bytes(b)


def app_header(ram_start, ram_entry, code_length):
    b = bytearray(APP_HDR_SIZE)
    b[0] = 0xD5
    b[3] = 0x60
    struct.pack_into("<I", b, 0x04, ram_start)
    struct.pack_into("<I", b, 0x08, ram_entry)
    struct.pack_into("<I", b, 0x0C, code_length)
    return bytes(b)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__ or "usage: s32g2_mkimage.py in out [--entry addr]\n")
        return 2
    entry = 0x340A0000
    if "--entry" in argv:
        entry = int(argv[argv.index("--entry") + 1], 0)
    qspi = "--qspi" in argv
    params = "sdr"
    if "--qspi-params" in argv:
        params = argv[argv.index("--qspi-params") + 1]
    base = 0 if qspi else SD_IVT_OFFSET
    dcd_off = QSPI_DCD_OFF if qspi else DCD_OFF
    with open(argv[1], "rb") as f:
        code = f.read()

    code_off = APP_OFF + APP_HDR_SIZE
    code_aligned = (code_off + CODE_ALIGN - 1) & ~(CODE_ALIGN - 1)
    pad = code_aligned - code_off
    ram_start = entry - pad
    # Partial 64-byte SRAM lines fault on ECC, then whole 512-byte sectors.
    length = (pad + len(code) + 63) & ~63
    length = (length + 511) & ~511

    end = ram_start + length
    if ram_start < SRAM_BASE or end > SRAM_BASE + SRAM_SIZE:
        sys.stderr.write("image does not fit in SRAM\n")
        return 1
    for lo, hi in SD_RESERVED:
        if ram_start < hi and end > lo:
            sys.stderr.write("image overlaps the Boot ROM reserved SRAM %#x-%#x\n"
                             % (lo, hi))
            return 1

    out = bytearray(code_aligned + length - pad)
    out[IVT_OFF:IVT_OFF + 0x100] = ivt(base + dcd_off, base + APP_OFF)
    if qspi:
        out[QSPI_PARAMS_OFF:QSPI_PARAMS_OFF + 0x200] = qspi_params(params)
    out[dcd_off:dcd_off + 0x14] = dcd_nop()
    out[APP_OFF:APP_OFF + APP_HDR_SIZE] = app_header(ram_start, entry, length)
    out[code_aligned:code_aligned + len(code)] = code
    with open(argv[2], "wb") as f:
        f.write(out)
    print("%s: %d bytes, ram_start %#x entry %#x code_length %#x (%s offset %#x)"
          % (argv[2], len(out), ram_start, entry, length,
             "QSPI %s" % params if qspi else "SD", base))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
