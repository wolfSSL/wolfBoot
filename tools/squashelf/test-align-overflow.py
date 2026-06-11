#!/usr/bin/env python3
# test-align-overflow.py
#
# Regression test for the integer overflow in squashelf's segment-offset
# alignment round-up (current_offset = (current_offset + p_align - 1) &
# ~(p_align - 1)). p_align comes straight from a possibly crafted program
# header. A value near UINT64_MAX (ELF64) wraps the sum to a tiny value, and a
# value that rounds the offset past 2^32 (ELF32) is silently truncated to a
# uint32_t. Either way the segment data lands at a wrong (often zero) file
# offset, clobbering the ELF header while squashelf still reports success.
# After the fix squashelf must reject such inputs with a non-zero exit instead
# of writing a corrupt output ELF.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import os
import struct
import subprocess
import sys
import tempfile

EHSIZE64 = 64
PHSIZE64 = 56
EHSIZE32 = 52
PHSIZE32 = 32
UINT64_MAX = 0xFFFFFFFFFFFFFFFF
UINT32_MAX = 0xFFFFFFFF


def make_elf64(path, align, filesz=0x10):
    ph_off = EHSIZE64
    seg_off = ph_off + PHSIZE64
    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8  # ELF64, little-endian
    ehdr = ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,         # type ET_EXEC
        62,        # machine x86-64
        1,         # version
        0,         # entry
        ph_off,    # ph_offset
        0,         # sh_offset
        0,         # flags
        EHSIZE64,  # header_size
        PHSIZE64,  # ph_entry_size
        1,         # ph_entry_count
        0, 0, 0)
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,        # PT_LOAD
        5,        # flags R+X
        seg_off,  # offset
        0x1000,   # vaddr
        0x1000,   # paddr
        filesz,   # file_size
        filesz,   # mem_size
        align)    # align
    with open(path, "wb") as f:
        f.write(ehdr + phdr + b"\xAA" * filesz)


def make_elf32(path, align, filesz=0x10):
    ph_off = EHSIZE32
    seg_off = ph_off + PHSIZE32
    ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\x00" * 8  # ELF32, little-endian
    ehdr = ident + struct.pack(
        "<HHIIIIIHHHHHH",
        2,         # type ET_EXEC
        3,         # machine x86
        1,         # version
        0,         # entry
        ph_off,    # ph_offset
        0,         # sh_offset
        0,         # flags
        EHSIZE32,  # header_size
        PHSIZE32,  # ph_entry_size
        1,         # ph_entry_count
        0, 0, 0)
    phdr = struct.pack(
        "<IIIIIIII",
        1,        # PT_LOAD
        seg_off,  # offset
        0x1000,   # vaddr
        0x1000,   # paddr
        filesz,   # file_size
        filesz,   # mem_size
        5,        # flags R+X
        align)    # align
    with open(path, "wb") as f:
        f.write(ehdr + phdr + b"\xAA" * filesz)


def run(squashelf, infile, outfile):
    return subprocess.run(
        [squashelf, infile, outfile],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def main():
    squashelf = sys.argv[1] if len(sys.argv) > 1 else "./squashelf"
    rc = 0
    with tempfile.TemporaryDirectory() as d:
        # 1) ELF64 alignment near UINT64_MAX wraps the round-up to a tiny
        #    offset. squashelf MUST reject it (non-zero exit), not write a
        #    corrupt ELF over its own header and report success.
        bad64 = os.path.join(d, "align64.elf")
        out64 = os.path.join(d, "align64.out")
        make_elf64(bad64, align=UINT64_MAX)
        if run(squashelf, bad64, out64) == 0:
            print("FAIL: ELF64 overflowing alignment was accepted")
            rc = 1
        else:
            print("PASS: ELF64 overflowing alignment rejected")

        # 2) ELF32 alignment that rounds the offset past 2^32; the uint32_t
        #    offset field would silently truncate (to 0 here). Must be rejected.
        bad32 = os.path.join(d, "align32.elf")
        out32 = os.path.join(d, "align32.out")
        make_elf32(bad32, align=UINT32_MAX)
        if run(squashelf, bad32, out32) == 0:
            print("FAIL: ELF32 truncating alignment was accepted")
            rc = 1
        else:
            print("PASS: ELF32 truncating alignment rejected")

        # 3) Regression guard: normal page-aligned segments must still succeed.
        ok64 = os.path.join(d, "ok64.elf")
        ok64o = os.path.join(d, "ok64.out")
        make_elf64(ok64, align=0x1000)
        if run(squashelf, ok64, ok64o) != 0:
            print("FAIL: normal ELF64 segment was rejected")
            rc = 1
        else:
            print("PASS: normal ELF64 segment kept")

        ok32 = os.path.join(d, "ok32.elf")
        ok32o = os.path.join(d, "ok32.out")
        make_elf32(ok32, align=0x1000)
        if run(squashelf, ok32, ok32o) != 0:
            print("FAIL: normal ELF32 segment was rejected")
            rc = 1
        else:
            print("PASS: normal ELF32 segment kept")

    sys.exit(rc)


if __name__ == "__main__":
    main()
