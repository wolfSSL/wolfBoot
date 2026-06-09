#!/usr/bin/env python3
# unit-sign-delta-cert-inv-off.py
#
# Regression test for the C signing tool delta inverse-patch offset.
#
# base_diff() in tools/keytools/sign.c captures
#   patch_inv_off = len3 + CMD.header_sz
# BEFORE calling make_header_delta(), which signs the delta image with
# make_header_ex(is_diff=1). When a certificate chain is present the delta
# header needs ~72 extra bytes (four delta TLVs plus the base-hash TLV), so for
# a window of cert-chain sizes header_required_size(is_diff=0) still fits the
# current CMD.header_sz while header_required_size(is_diff=1) does not. In that
# window make_header_ex(is_diff=1) grows CMD.header_sz to the next power of two
# AFTER patch_inv_off was captured, so the HDR_IMG_DELTA_INVERSE TLV stored in
# the signed delta image encodes a stale, too-small offset. The bootloader
# (src/update_flash.c) uses that value as a raw byte offset into the update
# partition to locate the inverse patch for rollback, so a wrong value makes
# rollback read garbage and fail.
#
# This test signs a real delta image with the C sign tool using an ed25519 key
# and a 300-byte certificate chain (a value inside the triggering window for
# ed25519+sha256, where CMD.header_sz starts at 512 but the delta header needs
# 1024). It then checks the self-consistency invariant: the inverse patch is the
# trailing HDR_IMG_DELTA_INVERSE_SIZE bytes of the file, so
#   HDR_IMG_DELTA_INVERSE == filesize - HDR_IMG_DELTA_INVERSE_SIZE
# must hold. Before the fix the stored offset is one power-of-two short and this
# assertion fails.
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

# Tags from include/wolfboot/wolfboot.h
HDR_IMG_DELTA_INVERSE      = 0x15
HDR_IMG_DELTA_INVERSE_SIZE = 0x16
HDR_PADDING                = 0xFF

SECTOR_SIZE = 0x1000
# 300-byte chain: inside the ed25519+sha256 window where the is_diff=0 header
# fits 512 bytes but the is_diff=1 (delta) header needs 1024.
CERT_CHAIN_SZ = 300

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
SIGN = os.path.join(ROOT, "tools", "keytools", "sign")


def skip(msg):
    print("SKIP unit-sign-delta-cert-inv-off: " + msg)
    sys.exit(0)


def find_tlv_u32(data, want_type, scan_end):
    """Mirror of wolfBoot_find_header(): return the u32 value for want_type."""
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
            if length != 4 or p + 4 + length > scan_end:
                return None
            return struct.unpack("<I", data[p + 4:p + 4 + length])[0]
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
    """Write a 64-byte raw ed25519 key (32-byte private seed + 32-byte public),
    the layout the C sign tool expects (ED25519_PRV_KEY_SIZE)."""
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


def main():
    if not ensure_sign():
        skip("could not build tools/keytools/sign")

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "priv.der")
        if not make_ed25519_key(key):
            skip("python cryptography module not available")

        chain = os.path.join(work, "chain.bin")
        with open(chain, "wb") as f:
            f.write(b"\xAA" * CERT_CHAIN_SZ)

        base = os.path.join(work, "image_v1.bin")
        upd = os.path.join(work, "image_v2.bin")
        payload = bytes((i * 7) & 0xFF for i in range(2048))
        with open(base, "wb") as f:
            f.write(payload)
        with open(upd, "wb") as f:
            f.write(payload[:512] + b"PATCHED!" + payload[520:])

        env = dict(os.environ)
        env["WOLFBOOT_SECTOR_SIZE"] = str(SECTOR_SIZE)

        # Sign the base image (v1) so it can be used as the delta base.
        r = subprocess.run(
            [SIGN, "--ed25519", "--sha256", "--cert-chain", chain,
             base, key, "1"],
            cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign base failed: " + r.stderr.strip())
        signed_base = base.replace(".bin", "_v1_signed.bin")
        if not os.path.exists(signed_base):
            skip("sign did not produce a signed base image")

        # Sign the delta image (v2) against the signed base, with the chain.
        r = subprocess.run(
            [SIGN, "--ed25519", "--sha256", "--delta", signed_base,
             "--cert-chain", chain, upd, key, "2"],
            cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign delta failed: " + r.stderr.strip())
        diff = upd.replace(".bin", "_v2_signed_diff.bin")
        if not os.path.exists(diff):
            skip("sign did not produce a delta image")

        with open(diff, "rb") as f:
            data = f.read()
        filesize = len(data)

        inv_off = find_tlv_u32(data, HDR_IMG_DELTA_INVERSE, filesize)
        inv_sz = find_tlv_u32(data, HDR_IMG_DELTA_INVERSE_SIZE, filesize)
        if inv_off is None or inv_sz is None:
            print("FAIL unit-sign-delta-cert-inv-off: delta TLVs not found")
            sys.exit(1)

        expected = filesize - inv_sz
        if inv_off != expected:
            print("FAIL unit-sign-delta-cert-inv-off: HDR_IMG_DELTA_INVERSE=%d "
                  "but the inverse patch (%d bytes) ends the %d-byte file at "
                  "offset %d; stale value is %d bytes short and rollback would "
                  "read from the wrong offset"
                  % (inv_off, inv_sz, filesize, expected, expected - inv_off))
            sys.exit(1)

    print("unit-sign-delta-cert-inv-off: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
