#!/usr/bin/env python3
# s32g2-ddr-blob.py - build the LPDDR4 data blob wolfBoot needs on the S32G2.
#
#   s32g2-ddr-blob.py <nxp-u-boot-dir> <out.bin> [--soc s32g274a] [--rev rev2]
#
# wolfBoot trains the LPDDR4 itself but does not carry the board's register
# tables or the Synopsys PHY training firmware. Both are published by NXP in
# their U-Boot tree (github.com/nxp-auto-linux/u-boot, BSP 28 to 30,
# board/freescale/s32-gen1). This script reads them from a checkout of that
# tree and packs them into the binary hal/s32g2_ddr.c consumes. The tables are
# taken with CONFIG_S32GEN1_DRAM_INLINE_ECC unset.
#
# Blob layout, little-endian 32-bit words:
#   magic "S32D", version, total size, CRC-32 of everything after this word,
#   then eight {offset, count} descriptors and the data they point at:
#     0 controller table      {address, value} pairs
#     1 DQ swap table         {address, value} pairs
#     2 PHY table             {address, value} pairs
#     3 PHY init engine table {address, value} pairs
#     4 1D training code      16-bit words
#     5 1D training data      16-bit words
#     6 2D training code      16-bit words
#     7 2D training data      16-bit words
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3.

import os
import re
import struct
import sys
import zlib

MAGIC = 0x44323353      # "S32D"
VERSION = 1
NUM_SECTIONS = 8
ECC_MACRO = "CONFIG_S32GEN1_DRAM_INLINE_ECC"


def strip_conditionals(text):
    """Keep the #else side of every #ifdef ECC_MACRO block, drop comments."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    out = []
    skip = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#ifdef") and ECC_MACRO in s:
            skip.append(True)
        elif s.startswith("#else") and skip:
            skip[-1] = not skip[-1]
        elif s.startswith("#endif") and skip:
            skip.pop()
        elif s.startswith("#"):
            continue
        elif not (skip and skip[-1]):
            out.append(line)
    return "\n".join(out)


def array_numbers(path, name):
    with open(path, "r", errors="replace") as f:
        text = strip_conditionals(f.read())
    m = re.search(r"\b%s\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;" % re.escape(name),
                  text, flags=re.S)
    if m is None:
        sys.exit("%s: array %s not found" % (path, name))
    return [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b",
                                          m.group(1))]


def pairs(path, name):
    n = array_numbers(path, name)
    if len(n) == 0 or len(n) % 2 != 0:
        sys.exit("%s: %s is not a list of {address, value}" % (path, name))
    return struct.pack("<%dI" % len(n), *n), len(n) // 2


def words16(path, name):
    n = array_numbers(path, name)
    if len(n) == 0 or max(n) > 0xFFFF:
        sys.exit("%s: %s is not a list of 16-bit words" % (path, name))
    data = struct.pack("<%dH" % len(n), *n)
    return data + b"\0" * (-len(data) % 4), len(n)


def main(argv):
    soc = "s32g274a"
    rev = "rev2"
    args = []
    i = 1
    while i < len(argv):
        if argv[i] in ("--soc", "--rev") and i + 1 < len(argv):
            if argv[i] == "--soc":
                soc = argv[i + 1]
            else:
                rev = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    if len(args) != 2 or args[0].startswith("--") or args[1].startswith("--"):
        sys.stderr.write("usage: s32g2-ddr-blob.py <nxp-u-boot-dir> <out.bin>"
                         " [--soc s32g274a] [--rev rev2]\n")
        return 2
    base = os.path.join(args[0], "board", "freescale", "s32-gen1")
    socdir = os.path.join(base, soc)
    revdir = os.path.join(socdir, rev)
    sfx = "_" + rev
    if not os.path.isdir(revdir):
        sys.exit("%s: not found (is this the NXP BSP U-Boot tree?)" % revdir)

    sections = [
        pairs(os.path.join(revdir, "ddrc_cfg%s.c" % sfx), "ddrc_cfg" + sfx),
        pairs(os.path.join(socdir, "dq_swap_cfg.c"), "dq_swap_cfg"),
        pairs(os.path.join(revdir, "phy_cfg%s.c" % sfx), "phy_cfg" + sfx),
        pairs(os.path.join(revdir, "pie_cfg%s.c" % sfx), "pie_cfg" + sfx),
        words16(os.path.join(base, "imem_cfg.c"), "imem_1d"),
        words16(os.path.join(revdir, "dmem_cfg%s.c" % sfx), "dmem_1d" + sfx),
        words16(os.path.join(base, "imem_cfg.c"), "imem_2d"),
        words16(os.path.join(revdir, "dmem_cfg%s.c" % sfx), "dmem_2d" + sfx),
    ]
    assert len(sections) == NUM_SECTIONS

    off = 16 + 8 * NUM_SECTIONS
    table = b""
    body = b""
    for data, count in sections:
        table += struct.pack("<II", off + len(body), count)
        body += data
    payload = table + body
    total = 16 + len(payload)
    blob = struct.pack("<IIII", MAGIC, VERSION, total,
                       zlib.crc32(payload) & 0xFFFFFFFF) + payload
    with open(args[1], "wb") as f:
        f.write(blob)
    print("%s: %d bytes, sections %s"
          % (args[1], total, " ".join(str(c) for _, c in sections)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
