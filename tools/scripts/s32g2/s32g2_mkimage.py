#!/usr/bin/env python3
# s32g2_mkimage.py - wrap wolfboot.bin for the S32G2 Boot ROM.
#
#   s32g2_mkimage.py <wolfboot.bin> <out.bin> [--qspi [--qspi-params sdr|octal]]
#                    [--hse-fw <firmware>]
#
# Default: the SD/eMMC content from byte 0x1000 (dd with seek=8), in the stock
# BSP layout: IVT, no-op DCD, application header, code 0x200-aligned.
# --qspi: the NOR content from offset 0, with the ROM's QuadSPI parameter
# block at 0x200. "sdr" (default) reads the flash in plain SPI mode, as
# wolfBoot's driver does; "octal" is the stock MX25UW51245G block, which
# switches the flash to octal DTR.
#
# --hse-fw: place the NXP HSE firmware image (from NXP's HSE package, not
# part of wolfBoot) behind the code and point the IVT at it, so the ROM
# starts it. Secure boot stays off: the ROM still loads the code itself.
# --hse-sign: append a P-256 public key and an ECDSA signature over the code
# the ROM loads, for HSE to verify it.
# --hse-sysimg: point the IVT at the HSE system image stored at this offset
# of the medium. --hse-efuse: switch on the fuse programming supply in the
# DCD, which HSE needs the first time it loads a new system image.
# --hse-boot-seq: set the secure boot bit, so HSE verifies the code and
# releases the A53 instead of the ROM starting it.
#
# The ROM copies code_length bytes from behind the 0x40-byte header to
# ram_start, which is the entry minus the pad between header and code.
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3.

import struct
import sys

SD_IVT_OFFSET = 0x1000          # where this file lands on the card
ENTRY = 0x340A0000              # WOLFBOOT_ORIGIN
QSPI_PARAMS_OFF = 0x0200        # QuadSPI boot only, fixed by the ROM
DCD_OFF = 0x0200
QSPI_DCD_OFF = 0x0400
APP_OFF = 0x2200                # application boot code header
CODE_ALIGN = 0x200
APP_HDR_SIZE = 0x40
HSE_FW_ALIGN = 0x1000
HSE_FW_MAX = 0x80000
HSE_FW_TAGS = (0xD4, 0xD6)       # NXP-encrypted, device-encrypted
HSE_SIG_SIZE = 136
HSE_SYSIMG_PAGE = 0x1000
HSE_MEDIUM_QSPI = 0
HSE_MEDIUM_SD = 2

SRAM_BASE = 0x34000000
SRAM_SIZE = 0x00800000
# Boot ROM working areas while it loads from SD; the image must not be copied
# over them (U-Boot tools/s32gen1image.c).
SD_RESERVED = [(0x34008000, 0x34078000), (0x343FF000, 0x34400000)]


def ivt(dcd_ptr, app_ptr, hse_ptr=0, sysimg_ptr=0, sysimg_medium=0,
        boot_seq=False):
    b = bytearray(0x100)
    b[0] = 0xD1                                   # tag
    b[1:3] = (0x100).to_bytes(2, "big")           # length, big-endian
    b[3] = 0x60                                   # version
    struct.pack_into("<I", b, 0x10, dcd_ptr)      # DCD pointer
    struct.pack_into("<I", b, 0x18, hse_ptr)      # HSE firmware pointer
    struct.pack_into("<I", b, 0x20, app_ptr)      # application boot code pointer
    # boot configuration: A53_0; bit 3 (BOOT_SEQ) hands the boot to HSE
    struct.pack_into("<I", b, 0x28, 1 | (8 if boot_seq else 0))
    if sysimg_ptr:
        struct.pack_into("<I", b, 0x34, sysimg_ptr)   # HSE system image
        struct.pack_into("<I", b, 0x3C, sysimg_medium)
        struct.pack_into("<I", b, 0x40, HSE_SYSIMG_PAGE)
    return bytes(b)


# Two writes that switch on the fuse programming supply of the RDB2 and
# GoldBox (GPIO25: SIUL2_0 MSCR25 output, GPDO25 high). HSE needs it at the
# first boot with a new system image.
def dcd_efuse():
    w32 = bytes([0xCC, 0x00, 0x0C, 0x14]) + struct.pack(">II", 0x4009C2A4,
                                                        0x0021C000)
    w8 = bytes([0xCC, 0x00, 0x0C, 0x11]) + struct.pack(">II", 0x4009D31A, 1)
    return bytes([0xD2, 0x00, 0x1C, 0x60]) + w32 + w8


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
        # configuration hal/s32g2.c uses.
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


# Signature block for HSE, 16-byte aligned behind the code: "HSES", the
# length of the signed range (the bytes the ROM copies to SRAM), the P-256
# public key X and Y, then the ECDSA SHA-256 signature R and S, all raw.
def hse_sign(key_file, data):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, utils
    with open(key_file, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(key.curve, ec.SECP256R1):
        raise SystemExit("--hse-sign needs a P-256 key")
    r, s = utils.decode_dss_signature(key.sign(data, ec.ECDSA(hashes.SHA256())))
    pub = key.public_key().public_numbers()
    return b"HSES" + struct.pack("<I", len(data)) + \
        b"".join(v.to_bytes(32, "big") for v in (pub.x, pub.y, r, s))


def main(argv):
    opts = argv[3:]
    hse_fw = None
    if "--hse-fw" in opts:
        i = opts.index("--hse-fw")
        if i + 1 >= len(opts) or opts[i + 1].startswith("--"):
            opts = ["--bad"]
        else:
            hse_fw = opts[i + 1]
            del opts[i:i + 2]
    sysimg = 0
    if "--hse-sysimg" in opts:
        i = opts.index("--hse-sysimg")
        try:
            sysimg = int(opts[i + 1], 0)
            del opts[i:i + 2]
        except (IndexError, ValueError):
            opts = ["--bad"]
    efuse = "--hse-efuse" in opts
    if efuse:
        opts.remove("--hse-efuse")
    boot_seq = "--hse-boot-seq" in opts
    if boot_seq:
        opts.remove("--hse-boot-seq")
    hse_key = None
    if "--hse-sign" in opts:
        i = opts.index("--hse-sign")
        if i + 1 >= len(opts) or opts[i + 1].startswith("--"):
            opts = ["--bad"]
        else:
            hse_key = opts[i + 1]
            del opts[i:i + 2]
    if len(argv) < 3 or argv[1].startswith("--") or argv[2].startswith("--") \
            or opts not in ([], ["--qspi"], ["--qspi", "--qspi-params", "sdr"],
                            ["--qspi", "--qspi-params", "octal"]):
        sys.stderr.write("usage: s32g2_mkimage.py <wolfboot.bin> <out.bin> "
                         "[--qspi [--qspi-params sdr|octal]] "
                         "[--hse-fw <firmware>] [--hse-sign <P-256 pem>] "
                         "[--hse-sysimg <offset>] [--hse-efuse] "
                         "[--hse-boot-seq]\n")
        return 2
    if (sysimg or efuse or boot_seq) and hse_fw is None:
        sys.stderr.write("--hse-sysimg, --hse-efuse and --hse-boot-seq need "
                         "--hse-fw\n")
        return 2
    if boot_seq and (not sysimg or hse_key is None):
        sys.stderr.write("--hse-boot-seq needs --hse-sysimg and --hse-sign\n")
        return 2
    entry = ENTRY
    qspi = len(opts) > 0
    params = opts[2] if len(opts) == 3 else "sdr"
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
    sig_off = 0
    if hse_key is not None:
        sig_off = (len(out) + 15) & ~15
        out += bytes(sig_off + HSE_SIG_SIZE - len(out))
    hse_off = 0
    if hse_fw is not None:
        with open(hse_fw, "rb") as f:
            fw = f.read()
        if len(fw) == 0 or len(fw) > HSE_FW_MAX or fw[0] not in HSE_FW_TAGS:
            sys.stderr.write("%s is not an HSE firmware image\n" % hse_fw)
            return 1
        hse_off = (len(out) + HSE_FW_ALIGN - 1) & ~(HSE_FW_ALIGN - 1)
        out += bytes(hse_off - len(out)) + fw
    if sysimg and (sysimg % HSE_FW_ALIGN != 0 or sysimg < len(out)):
        sys.stderr.write("--hse-sysimg must be %#x-aligned and behind the "
                         "image (%#x)\n" % (HSE_FW_ALIGN, len(out)))
        return 1
    out[0:0x100] = ivt(base + dcd_off, base + APP_OFF,
                       base + hse_off if hse_off else 0,
                       base + sysimg if sysimg else 0,
                       HSE_MEDIUM_QSPI if qspi else HSE_MEDIUM_SD, boot_seq)
    if qspi:
        out[QSPI_PARAMS_OFF:QSPI_PARAMS_OFF + 0x200] = qspi_params(params)
    dcd = dcd_efuse() if efuse else dcd_nop()
    out[dcd_off:dcd_off + len(dcd)] = dcd
    out[APP_OFF:APP_OFF + APP_HDR_SIZE] = app_header(ram_start, entry, length)
    out[code_aligned:code_aligned + len(code)] = code
    if sig_off:
        smr = bytes(out[APP_OFF + APP_HDR_SIZE:APP_OFF + APP_HDR_SIZE + length])
        out[sig_off:sig_off + HSE_SIG_SIZE] = hse_sign(hse_key, smr)
    with open(argv[2], "wb") as f:
        f.write(out)
    print("%s: %d bytes, ram_start %#x entry %#x code_length %#x (%s offset %#x)"
          % (argv[2], len(out), ram_start, entry, length,
             "QSPI %s" % params if qspi else "SD", base))
    if hse_off:
        print("HSE firmware at %#x, image ends at %#x"
              % (base + hse_off, base + len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
