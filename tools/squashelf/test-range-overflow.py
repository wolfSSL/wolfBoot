#!/usr/bin/env python3
# test-range-overflow.py
#
# Regression test for the uint64_t overflow in squashelf's range filter
# (segmentEnd = p_paddr + p_memsz - 1). A crafted ELF64 PT_LOAD segment whose
# p_paddr + p_memsz wraps past 2^64 must NOT be smuggled past a range filter:
# its true span covers (almost) the whole address space, so it is out of range
# and must be excluded. Before the fix the wrapped end landed back inside the
# range and the segment was wrongly kept.
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

EHSIZE = 64
PHSIZE = 56
UINT64_MAX = 0xFFFFFFFFFFFFFFFF


def make_elf64(path, paddr, memsz, filesz=0x10):
    ph_off = EHSIZE
    seg_off = ph_off + PHSIZE
    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8  # ELF64, little-endian
    ehdr = ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,       # type ET_EXEC
        62,      # machine x86-64
        1,       # version
        0,       # entry
        ph_off,  # ph_offset
        0,       # sh_offset
        0,       # flags
        EHSIZE,  # header_size
        PHSIZE,  # ph_entry_size
        1,       # ph_entry_count
        0, 0, 0)
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,        # PT_LOAD
        5,        # flags R+X
        seg_off,  # offset
        paddr,    # vaddr
        paddr,    # paddr
        filesz,   # file_size
        memsz,    # mem_size
        0x1000)   # align
    with open(path, "wb") as f:
        f.write(ehdr + phdr + b"\xAA" * filesz)


def run(squashelf, infile, outfile, rng):
    return subprocess.run(
        [squashelf, "-r", rng, infile, outfile],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def main():
    squashelf = sys.argv[1] if len(sys.argv) > 1 else "./squashelf"
    rc = 0
    with tempfile.TemporaryDirectory() as d:
        # 1) Overflow segment: p_paddr + p_memsz - 1 wraps below p_paddr, so the
        #    wrapped end (~0x4fe) is inside [0, 0x1000] even though the real span
        #    covers the whole address space. It MUST be excluded (non-zero exit,
        #    no output segment).
        bad = os.path.join(d, "overflow.elf")
        make_elf64(bad, paddr=0x500, memsz=UINT64_MAX)
        if run(squashelf, bad, os.path.join(d, "bad.out"), "0x0-0x1000") == 0:
            print("FAIL: overflow segment was wrongly included by range filter")
            rc = 1
        else:
            print("PASS: overflow segment excluded")

        # 2) Regression guard: a normal in-range segment must still be kept.
        good = os.path.join(d, "ok.elf")
        make_elf64(good, paddr=0x500, memsz=0x100)
        if run(squashelf, good, os.path.join(d, "good.out"), "0x0-0x1000") != 0:
            print("FAIL: normal in-range segment was dropped")
            rc = 1
        else:
            print("PASS: normal in-range segment kept")

    sys.exit(rc)


if __name__ == "__main__":
    main()
