#!/usr/bin/env python3
# fatput.py
#
# Copy files into a freshly formatted FAT32 volume inside a disk image,
# without mounting it or needing mtools. Test helper for the x86_64_lilo
# QEMU test: files are laid out contiguously, directories are created as
# needed, and names get VFAT long-name entries.
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
#
# Usage: fatput.py <image> <partition-offset-bytes> <fat-path>=<host-file>...

import struct
import sys

EOC = 0x0FFFFFFF


class Fat32:
    def __init__(self, f, base):
        self.f = f
        self.base = base
        bs = self.read(0, 512)
        self.bps, self.spc, self.rsvd, self.nfats = struct.unpack_from(
            "<HBHB", bs, 11)
        self.fatsz = struct.unpack_from("<I", bs, 36)[0]
        self.root = struct.unpack_from("<I", bs, 44)[0]
        if self.fatsz == 0 or bs[82:87] != b"FAT32":
            raise SystemExit("not a FAT32 volume")
        self.csize = self.bps * self.spc
        self.data = (self.rsvd + self.nfats * self.fatsz) * self.bps
        self.nclus = (self.fatsz * self.bps) // 4
        self.next = 3
        while self.fat_get(self.next) != 0:
            self.next += 1

    def read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    def write(self, off, data):
        self.f.seek(self.base + off)
        self.f.write(data)

    def fat_get(self, c):
        return struct.unpack("<I", self.read(self.rsvd * self.bps + 4 * c,
                                             4))[0] & 0x0FFFFFFF

    def fat_set(self, c, v):
        for i in range(self.nfats):
            off = (self.rsvd + i * self.fatsz) * self.bps + 4 * c
            self.write(off, struct.pack("<I", v))

    def alloc(self, n):
        first = self.next
        if first + n >= self.nclus:
            raise SystemExit("volume full")
        for i in range(n):
            self.fat_set(first + i, first + i + 1 if i + 1 < n else EOC)
        self.next += n
        return first

    def coff(self, c):
        return self.data + (c - 2) * self.csize

    def entries(self, dclus):
        return [self.read(self.coff(dclus) + i * 32, 32)
                for i in range(self.csize // 32)]

    def lookup(self, dclus, name):
        lfn = ""
        for e in self.entries(dclus):
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = ""
                continue
            if e[11] == 0x0F:
                part = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16-le")
                lfn = part.split("\0")[0] + lfn
                continue
            short = e[0:8].decode().rstrip()
            ext = e[8:11].decode().rstrip()
            full = short + ("." + ext if ext else "")
            if name.lower() in (lfn.lower(), full.lower()):
                return e
            lfn = ""
        return None

    def add_entry(self, dclus, name, attr, clus, size):
        base = "".join(ch for ch in name.upper() if ch.isalnum())[:6] or "F"
        ext = ""
        if "." in name:
            ext = "".join(ch for ch in name.rsplit(".", 1)[1].upper()
                          if ch.isalnum())[:3]
        n = 1
        while True:
            sfn = (base + "~" + str(n)).ljust(8)[:8] + ext.ljust(3)
            if all(e[0:11] != sfn.encode() for e in self.entries(dclus)):
                break
            n += 1
        sfn = sfn.encode()
        ck = 0
        for b in sfn:
            ck = (((ck & 1) << 7) + (ck >> 1) + b) & 0xFF
        chunks = [name[i:i + 13] for i in range(0, len(name), 13)]
        ents = []
        for i, ch in enumerate(chunks):
            raw = ch.encode("utf-16-le")
            if len(ch) < 13:
                raw += b"\0\0"
            raw = raw.ljust(26, b"\xff")
            ordn = i + 1 + (0x40 if i == len(chunks) - 1 else 0)
            ents.insert(0, bytes([ordn]) + raw[0:10] + bytes([0x0F, 0, ck]) +
                        raw[10:22] + b"\0\0" + raw[22:26])
        ents.append(sfn + struct.pack("<BBBHHHHHHHI", attr, 0, 0, 0, 0, 0,
                                      clus >> 16, 0, 0, clus & 0xFFFF, size))
        dir_ents = self.entries(dclus)
        for i in range(len(dir_ents) - len(ents) + 1):
            if all(dir_ents[i + j][0] in (0, 0xE5) for j in range(len(ents))):
                for j, e in enumerate(ents):
                    assert len(e) == 32
                    self.write(self.coff(dclus) + (i + j) * 32, e)
                return
        raise SystemExit("directory full")

    def mkdir(self, parent, name):
        c = self.alloc(1)
        self.write(self.coff(c), bytes(self.csize))
        pc = 0 if parent == self.root else parent
        dot = b".".ljust(11) + bytes([0x10]) + bytes(8) + \
            struct.pack("<H", c >> 16) + bytes(4) + \
            struct.pack("<HI", c & 0xFFFF, 0)
        dotdot = b"..".ljust(11) + bytes([0x10]) + bytes(8) + \
            struct.pack("<H", pc >> 16) + bytes(4) + \
            struct.pack("<HI", pc & 0xFFFF, 0)
        self.write(self.coff(c), dot + dotdot)
        self.add_entry(parent, name, 0x10, c, 0)
        return c

    def put(self, path, data):
        parts = [p for p in path.split("/") if p]
        d = self.root
        for p in parts[:-1]:
            e = self.lookup(d, p)
            if e is None:
                d = self.mkdir(d, p)
            else:
                d = struct.unpack_from("<H", e, 20)[0] << 16 | \
                    struct.unpack_from("<H", e, 26)[0]
        n = max(1, (len(data) + self.csize - 1) // self.csize)
        c = self.alloc(n)
        self.write(self.coff(c), data)
        self.add_entry(d, parts[-1], 0x20, c, len(data))


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__ or "usage: fatput.py image offset a=b...")
    with open(sys.argv[1], "r+b") as f:
        fat = Fat32(f, int(sys.argv[2], 0))
        for arg in sys.argv[3:]:
            dst, src = arg.split("=", 1)
            with open(src, "rb") as s:
                fat.put(dst, s.read())


if __name__ == "__main__":
    main()
