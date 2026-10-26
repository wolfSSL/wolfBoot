#!/usr/bin/env python3
# unit-psoc-e8-boot-header.py
#
# Parses images produced by tools/scripts/psoc_e8/psoc_e8_boot_header.py and
# checks every header and TLV field, the padding, the digest and the boot-slot
# size limit.
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

import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'scripts', 'psoc_e8', 'psoc_e8_boot_header.py')
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(TOOL))
import psoc_e8_boot_header as bh  # noqa: E402

HDR_SIZE = 0x400
TRAILER_SIZE = 40


def parse(out):
    (magic, load_addr, hdr_size, protect_tlv, img_size, flags, v_major,
     v_minor, v_rev, v_build, pad) = struct.unpack_from('<IIHHIIBBHII', out)
    hdr_end = struct.calcsize('<IIHHIIBBHII')
    return {
        'magic': magic, 'load_addr': load_addr, 'hdr_size': hdr_size,
        'protect_tlv': protect_tlv, 'img_size': img_size, 'flags': flags,
        'version': (v_major, v_minor, v_rev, v_build), 'pad': pad,
        'fill': out[hdr_end:HDR_SIZE],
    }


class BootHeaderTest(unittest.TestCase):

    def check(self, img):
        out = bh.add_header(img)
        h = parse(out)
        self.assertEqual(h['magic'], 0x96F3B83D)
        self.assertEqual(h['load_addr'], 0)
        self.assertEqual(h['hdr_size'], HDR_SIZE)
        self.assertEqual(h['protect_tlv'], 0)
        self.assertEqual(h['img_size'], len(img))
        self.assertEqual(h['flags'], 0)
        self.assertEqual(h['version'], (0, 0, 0, 0))
        self.assertEqual(h['pad'], 0xFFFFFFFF)
        self.assertEqual(h['fill'], b'\xff' * len(h['fill']))
        self.assertEqual(out[HDR_SIZE:HDR_SIZE + len(img)], img)
        self.assertEqual(len(out), HDR_SIZE + len(img) + TRAILER_SIZE)

        t = HDR_SIZE + len(img)
        info_magic, info_len = struct.unpack_from('<HH', out, t)
        self.assertEqual(info_magic, 0x6907)
        self.assertEqual(info_len, TRAILER_SIZE)
        tlv_type, tlv_len = struct.unpack_from('<HH', out, t + 4)
        self.assertEqual(tlv_type, 0x10)
        self.assertEqual(tlv_len, 32)
        self.assertEqual(out[t + 8:t + 40],
                         hashlib.sha256(out[:t]).digest())
        return out

    def test_payload(self):
        self.check(bytes(range(256)) * 5 + b'\x01\x02\x03')

    def test_empty_payload(self):
        self.check(b'')

    def test_slot_boundary(self):
        img = b'\xa5' * 100
        fit = HDR_SIZE + len(img) + TRAILER_SIZE
        self.assertEqual(len(bh.add_header(img, fit)), fit)
        with self.assertRaises(ValueError):
            bh.add_header(img, fit - 1)

    def test_cli_rejects_oversize(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'in.bin')
            dst = os.path.join(d, 'out.bin')
            with open(src, 'wb') as f:
                f.write(b'\x5a' * 64)
            fit = HDR_SIZE + 64 + TRAILER_SIZE
            ok = subprocess.run([sys.executable, TOOL, src, dst, hex(fit)])
            self.assertEqual(ok.returncode, 0)
            with open(dst, 'rb') as f:
                self.assertEqual(f.read(), bh.add_header(b'\x5a' * 64))
            os.remove(dst)
            bad = subprocess.run([sys.executable, TOOL, src, dst,
                                  hex(fit - 1)], stderr=subprocess.DEVNULL)
            self.assertNotEqual(bad.returncode, 0)
            self.assertFalse(os.path.exists(dst))


if __name__ == '__main__':
    unittest.main()
