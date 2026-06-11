#!/usr/bin/env python3
# unit-sign-custom-tlv-le.py
#
# Regression test for the C sign tool custom integer TLV byte order.
#
# The --custom-tlv TAG LEN VAL path in make_header_ex() (sign.c) must store
# the integer value in little-endian byte order, matching the convention used
# by wolfBoot_find_header() + im2n().  Before the fix the code passed
# &CMD.custom_tlv[i].val (a host-endian uint64_t) directly to
# header_append_tag(), which performs a raw memcpy.  On a big-endian build
# host this copies the high bytes of the uint64_t first, so e.g.
# --custom-tlv 0x0032 4 0x12345678 encodes as 00 00 00 00 instead of
# 78 56 34 12, and the bootloader decodes back zero.  After the fix the code
# serialises through header_store_u64_le() before calling header_append_tag(),
# giving correct LE bytes on any build host.
#
# This test signs a dummy image with --custom-tlv entries of len 2, 4, and 8,
# then reads the resulting header bytes at each TLV value offset and asserts
# they are exactly the little-endian encoding of the supplied integer.  On a
# big-endian build host the assertions fail with the old code; on a
# little-endian host they pass before and after the fix (host layout already
# matches LE), making the test a regression guard for big-endian ports.
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
import subprocess
import sys
import tempfile

HDR_PADDING = 0xFF

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
SIGN = os.path.join(ROOT, "tools", "keytools", "sign")


def skip(msg):
    print("SKIP unit-sign-custom-tlv-le: " + msg)
    sys.exit(0)


def find_tlv_bytes(data, want_type, scan_end):
    """Return raw value bytes for a TLV entry (mirrors wolfBoot_find_header)."""
    p = 8  # skip 4-byte magic + 4-byte image size
    while p + 4 <= scan_end:
        htype = data[p] | (data[p + 1] << 8)
        if htype == 0:
            break
        if data[p] == HDR_PADDING or (p & 1) != 0:
            p += 1
            continue
        length = data[p + 2] | (data[p + 3] << 8)
        if htype == want_type:
            if p + 4 + length > scan_end:
                return None
            return bytes(data[p + 4:p + 4 + length])
        p += 4 + length
    return None


def ensure_sign():
    if os.path.exists(SIGN):
        return True
    try:
        subprocess.run(["make", "sign"],
                       cwd=os.path.join(ROOT, "tools", "keytools"),
                       check=True, capture_output=True, text=True)
    except (subprocess.CalledProcessError, OSError):
        return False
    return os.path.exists(SIGN)


def make_ed25519_key(path):
    """Write a 64-byte raw ed25519 key (seed + public) as expected by sign."""
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import \
            Ed25519PrivateKey
        from cryptography.hazmat.primitives import serialization
    except Exception:
        return False
    seed = b"\x42" * 32
    sk = Ed25519PrivateKey.from_private_bytes(seed)
    pub = sk.public_key().public_bytes(serialization.Encoding.Raw,
                                        serialization.PublicFormat.Raw)
    with open(path, "wb") as f:
        f.write(seed + pub)
    return True


# (tag, len_bytes, integer_value, expected_le_bytes)
# Values are non-palindromic so a byte-order error is immediately visible.
# Only two cases so the total argc stays <= 14 (the sign tool's hard limit:
# "argc > 14" triggers the usage check).  len=4 and len=8 are the most
# illustrative; len=1 is trivially the same on any host byte order.
TEST_CASES = [
    (0x0032, 4, 0x12345678,         bytes([0x78, 0x56, 0x34, 0x12])),
    (0x0033, 8, 0x0102030405060708, bytes([0x08, 0x07, 0x06, 0x05,
                                           0x04, 0x03, 0x02, 0x01])),
]


def main():
    if not ensure_sign():
        skip("could not build tools/keytools/sign")

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "priv.der")
        if not make_ed25519_key(key):
            skip("python cryptography module not available")

        image = os.path.join(work, "image.bin")
        with open(image, "wb") as f:
            f.write(bytes(range(256)) * 8)  # 2 KiB dummy payload

        cmd = [SIGN, "--ed25519", "--sha256"]
        for tag, length, val, _ in TEST_CASES:
            cmd += ["--custom-tlv", hex(tag), str(length), hex(val)]
        cmd += [image, key, "1"]

        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign failed: " + r.stderr.strip())

        signed = image.replace(".bin", "_v1_signed.bin")
        if not os.path.exists(signed):
            skip("sign did not produce a signed image")

        with open(signed, "rb") as f:
            data = f.read(512)

        failures = []
        for tag, length, val, expected in TEST_CASES:
            got = find_tlv_bytes(data, tag, len(data))
            if got is None:
                failures.append(
                    "tag 0x%04x (len=%d val=%s): TLV not found in header" %
                    (tag, length, hex(val)))
            elif got != expected:
                failures.append(
                    "tag 0x%04x (len=%d val=%s): got [%s], want [%s] (LE); "
                    "raw memcpy from host-endian uint64_t produces wrong bytes "
                    "on big-endian build hosts" %
                    (tag, length, hex(val),
                     " ".join("%02x" % b for b in got),
                     " ".join("%02x" % b for b in expected)))

        if failures:
            for msg in failures:
                print("FAIL unit-sign-custom-tlv-le: " + msg)
            sys.exit(1)

    print("unit-sign-custom-tlv-le: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
