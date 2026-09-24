#!/usr/bin/env python3
# test-repeat-range.py
#
# Regression test for F-12872: repeating the -r option in squashelf
# replaced the ranges array without freeing the previous one (leak) and
# silently discarded the earlier ranges. A second -r must now be
# rejected; a single comma-separated list remains the way to pass
# multiple ranges.
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
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

import os
import struct
import subprocess
import sys
import tempfile

EHSIZE = 64
PHSIZE = 56


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


def run(squashelf, args, infile, outfile):
    return subprocess.run(
        [squashelf] + args + [infile, outfile],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def main():
    squashelf = sys.argv[1] if len(sys.argv) > 1 else "./squashelf"
    rc = 0
    with tempfile.TemporaryDirectory() as d:
        elf = os.path.join(d, "in.elf")
        make_elf64(elf, paddr=0x50, memsz=0x100)

        # 1) A second -r must be rejected: pre-fix it succeeded, leaking
        #    the first array and silently filtering on the second range
        #    only. The second range alone keeps the segment, so a
        #    pre-fix run exits 0 (accepted) while a post-fix run exits
        #    non-zero (rejected).
        if run(squashelf, ["-r", "0x400-0x500", "-r", "0x0-0x200"],
               elf, os.path.join(d, "dup.out")) == 0:
            print("FAIL: repeated -r option was accepted")
            rc = 1
        else:
            print("PASS: repeated -r option rejected")

        # 2) Regression guard: a single comma-separated range list must
        #    still work (the filter keeps a segment only when both its
        #    start and end fall inside one of the ranges).
        if run(squashelf, ["-r", "0x0-0x200,0x400-0x500"],
               elf, os.path.join(d, "list.out")) != 0:
            print("FAIL: comma-separated range list was rejected")
            rc = 1
        else:
            print("PASS: comma-separated range list accepted")

    sys.exit(rc)


if __name__ == "__main__":
    main()
