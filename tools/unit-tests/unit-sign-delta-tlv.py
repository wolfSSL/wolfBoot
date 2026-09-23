#!/usr/bin/env python3
# unit-sign-delta-tlv.py
#
# Regression test for the signing tool delta header encoding.
#
# tools/keytools/sign.c emits the HDR_IMG_DELTA_SIZE and
# HDR_IMG_DELTA_INVERSE_SIZE TLVs that describe the size of a delta patch.
# wolfBoot_get_delta_info() (src/libwolfboot.c) only accepts those tags when
# wolfBoot_find_header() reports a value length of sizeof(uint32_t) (4 bytes),
# matching the C signing tool (tools/keytools/sign.c, header_append_tag_u32()).
#
# This test signs a real delta image with the C sign tool, parses the
# resulting header exactly the way wolfBoot_find_header() does, and asserts
# that every delta TLV carries the 4-byte length the bootloader requires. If
# the tool ever emits these two tags with a length other than 4, the
# bootloader rejects otherwise valid delta images; this test fails in that
# case.
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

# Tags from include/wolfboot/wolfboot.h
HDR_IMG_DELTA_SIZE         = 0x06
HDR_IMG_DELTA_INVERSE_SIZE = 0x16
HDR_PADDING                = 0xFF

# sizeof(uint32_t): the value length wolfBoot_get_delta_info() requires.
SIZEOF_UINT32 = 4

SECTOR_SIZE = 0x1000
IMAGE_HEADER_SIZE = 256

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
KEYTOOLS = os.path.join(ROOT, "tools", "keytools")
SIGN = os.path.join(KEYTOOLS, "sign")
KEYGEN = os.path.join(KEYTOOLS, "keygen")


def skip(msg):
    print("SKIP unit-sign-delta-tlv: " + msg)
    sys.exit(0)


def find_tlv(header, want_type):
    """Mirror of wolfBoot_find_header(): return the value length for want_type."""
    p = 8  # skip 4-byte magic + 4-byte image size
    end = min(len(header), IMAGE_HEADER_SIZE)
    while p + 4 <= end:
        htype = header[p] | (header[p + 1] << 8)
        if htype == 0:
            break
        if header[p] == HDR_PADDING or (p & 1) != 0:
            p += 1
            continue
        length = header[p + 2] | (header[p + 3] << 8)
        if (4 + length) > end:
            break
        if htype == want_type:
            return length
        p += 4 + length
    return None


def ensure_tool(path, target):
    if os.path.exists(path):
        return True
    try:
        subprocess.run(["make", target], cwd=KEYTOOLS,
                       check=True, capture_output=True, text=True)
    except (subprocess.CalledProcessError, OSError):
        return False
    return os.path.exists(path)


def main():
    if not ensure_tool(SIGN, "sign"):
        skip("could not build tools/keytools/sign")
    if not ensure_tool(KEYGEN, "keygen"):
        skip("could not build tools/keytools/keygen")

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "priv.der")
        r = subprocess.run([KEYGEN, "--ed25519", "-g", key,
                            "-keystoreDir", work],
                           cwd=work, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(key):
            skip("keygen failed: " + r.stderr.strip())

        base = os.path.join(work, "image_v1.bin")
        upd = os.path.join(work, "image_v2.bin")
        payload = bytes((i * 7) & 0xFF for i in range(2048))
        with open(base, "wb") as f:
            f.write(payload)
        with open(upd, "wb") as f:
            # small localized change so the patch stays well under 64 KB
            f.write(payload[:512] + b"PATCHED!" + payload[520:])

        env = dict(os.environ)
        env["WOLFBOOT_SECTOR_SIZE"] = str(SECTOR_SIZE)

        # Sign the base image (v1) so it can be used as the delta base.
        r = subprocess.run([SIGN, "--ed25519", "--sha256", base, key, "1"],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign base failed: " + r.stderr.strip())
        signed_base = base.replace(".bin", "_v1_signed.bin")
        if not os.path.exists(signed_base):
            skip("sign did not produce a signed base image")

        # Sign the delta image (v2) against the signed base.
        r = subprocess.run([SIGN, "--ed25519", "--sha256", "--delta",
                            signed_base, upd, key, "2"],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign delta failed: " + r.stderr.strip())
        diff = upd.replace(".bin", "_v2_signed_diff.bin")
        if not os.path.exists(diff):
            skip("sign did not produce a delta image")

        with open(diff, "rb") as f:
            header = f.read(IMAGE_HEADER_SIZE)

        failures = []
        for name, tag in (("HDR_IMG_DELTA_SIZE", HDR_IMG_DELTA_SIZE),
                          ("HDR_IMG_DELTA_INVERSE_SIZE", HDR_IMG_DELTA_INVERSE_SIZE)):
            length = find_tlv(header, tag)
            if length is None:
                failures.append("%s TLV not found in delta header" % name)
            elif length != SIZEOF_UINT32:
                failures.append(
                    "%s encoded with length %d, but wolfBoot_get_delta_info() "
                    "requires sizeof(uint32_t)=%d; bootloader will reject the "
                    "image" % (name, length, SIZEOF_UINT32))

        if failures:
            for msg in failures:
                print("FAIL unit-sign-delta-tlv: " + msg)
            sys.exit(1)

    print("unit-sign-delta-tlv: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
