#!/usr/bin/env python3
# unit-sign-dts.py
#
# Regression test for the C sign tool "--dts" device-tree binding
# (make_header_ex()/dts_hash_file() in tools/keytools/sign.c, Fenrir #7998).
#
# Covers:
#   1. A valid DTB is bound to the image: the HDR_DEVICE_TREE_DIGEST TLV
#      (tag 0x35) holds the image-hash (SHA256) of exactly the first
#      fdt_totalsize bytes of the .dtb.
#   2. A bad-magic file is rejected with a clean non-zero exit -- NOT a signal
#      (the dangling-FILE double free previously aborted with SIGABRT).
#   3. A truncated FDT (valid magic, shorter than the 40-byte header) is
#      rejected cleanly, matching what the bootloader's fdt_check_header()
#      would reject.
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

HDR_PADDING = 0xFF
HDR_DEVICE_TREE_DIGEST = 0x35

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
SIGN = os.path.join(ROOT, "tools", "keytools", "sign")


def skip(msg):
    print("SKIP unit-sign-dts: " + msg)
    sys.exit(0)


def fail(msg):
    print("FAIL unit-sign-dts: " + msg)
    sys.exit(1)


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


def make_fdt(total):
    """Build a minimal valid FDT blob of 'total' bytes (v17 header)."""
    hdr = bytearray(40)
    struct.pack_into(">I", hdr, 0x00, 0xd00dfeed)  # magic
    struct.pack_into(">I", hdr, 0x04, total)       # totalsize
    struct.pack_into(">I", hdr, 0x08, 0x38)        # off_dt_struct
    struct.pack_into(">I", hdr, 0x0C, 0x38)        # off_dt_strings
    struct.pack_into(">I", hdr, 0x10, 0x28)        # off_mem_rsvmap
    struct.pack_into(">I", hdr, 0x14, 0x11)        # version 17
    struct.pack_into(">I", hdr, 0x18, 0x10)        # last_comp_version 16
    blob = bytes(hdr) + b"\x00" * (total - len(hdr))
    return blob


# (hash flag, hashlib factory, digest length) for each supported algorithm.
HASHES = [
    ("--sha256", hashlib.sha256, 32),
    ("--sha384", hashlib.sha384, 48),
    ("--sha3", (lambda b: hashlib.sha3_384(b)), 48),
]


def run_dts(work, key, dtb_path, hash_flag="--sha256", image=None):
    """Run 'sign --ed25519 <hash_flag> --dts <dtb> <image> <key> 1'."""
    if image is None:
        image = os.path.join(work, "fw.bin")
        with open(image, "wb") as f:
            f.write(b"\x11" * 2048)
    cmd = [SIGN, "--ed25519", hash_flag, "--dts", dtb_path, image, key, "1"]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True), image


def main():
    if not ensure_sign():
        skip("sign tool not available")

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "ed25519.der")
        if not make_ed25519_key(key):
            skip("python 'cryptography' module unavailable")

        # 1. Valid DTB -> digest TLV present and correct, for every hash
        #    algorithm (covers dts_hash_file's sha256/sha384/sha3-384 branches).
        #    A sign failure here is a real regression, so fail() (not skip()).
        total = 128
        dtb = os.path.join(work, "good.dtb")
        blob = make_fdt(total)
        with open(dtb, "wb") as f:
            f.write(blob + b"\xAA" * 16)  # trailing padding beyond totalsize
        for hflag, hfn, hlen in HASHES:
            r, image = run_dts(work, key, dtb, hflag)
            if "unsupported" in (r.stderr or "").lower() and hflag == "--sha3":
                # SHA3 not compiled into this sign build; skip only this case.
                continue
            if r.returncode != 0:
                fail("sign %s --dts failed (rc=%d): %s"
                     % (hflag, r.returncode, r.stderr.strip()))
            signed = image.replace(".bin", "_v1_signed.bin")
            if not os.path.exists(signed):
                fail("sign %s --dts produced no signed image" % hflag)
            with open(signed, "rb") as f:
                data = f.read(2048)
            got = find_tlv_bytes(data, HDR_DEVICE_TREE_DIGEST, len(data))
            expect = hfn(blob).digest()  # first totalsize bytes only
            if got is None:
                fail("%s: HDR_DEVICE_TREE_DIGEST (0x35) TLV not found" % hflag)
            if len(got) != hlen:
                fail("%s: digest TLV len %d, expected %d"
                     % (hflag, len(got), hlen))
            if got != expect:
                fail("%s: digest TLV mismatch: got %s expected %s"
                     % (hflag, got.hex(), expect.hex()))

        # 2. Bad magic -> clean non-zero exit, never a signal (SIGABRT was the
        #    double-free symptom). subprocess returncode < 0 means killed by
        #    signal -N.
        bad = os.path.join(work, "bad.dtb")
        with open(bad, "wb") as f:
            f.write(b"not-an-fdt-file-but-long-enough-to-fill-40-bytes!!!")
        r, _ = run_dts(work, key, bad)
        if r.returncode < 0:
            fail("bad-magic --dts died with signal %d (double free?)"
                 % (-r.returncode))
        if r.returncode == 0:
            fail("bad-magic --dts unexpectedly succeeded")

        # 3. Truncated FDT (valid magic, < 40-byte header) -> clean rejection.
        trunc = os.path.join(work, "trunc.dtb")
        with open(trunc, "wb") as f:
            f.write(struct.pack(">II", 0xd00dfeed, 0x10))  # 8 bytes only
        r, _ = run_dts(work, key, trunc)
        if r.returncode < 0:
            fail("truncated --dts died with signal %d" % (-r.returncode))
        if r.returncode == 0:
            fail("truncated --dts unexpectedly succeeded")

        # 4. Unsupported FDT version (full 40B header, valid magic, version 15)
        #    -> rejected (the version/last_comp check the bootloader also does).
        badver = os.path.join(work, "badver.dtb")
        vblob = bytearray(make_fdt(128))
        struct.pack_into(">I", vblob, 0x14, 0x0F)  # version 15 < 0x10
        with open(badver, "wb") as f:
            f.write(bytes(vblob))
        r, _ = run_dts(work, key, badver)
        if r.returncode <= 0:
            fail("unsupported-version --dts should be rejected (rc=%d)"
                 % r.returncode)

        # 5. Truncated body: totalsize declares more than the file holds ->
        #    rejected up front, not hashed.
        short = os.path.join(work, "short.dtb")
        with open(short, "wb") as f:
            f.write(make_fdt(4096)[:512])  # header says 4096, file is 512
        r, _ = run_dts(work, key, short)
        if r.returncode <= 0:
            fail("short-body --dts should be rejected (rc=%d)" % r.returncode)

        # 6. Missing --dts argument -> clean usage error, never a crash.
        r = subprocess.run([SIGN, "--ed25519", "--sha256", "--dts"],
                           cwd=ROOT, capture_output=True, text=True)
        if r.returncode < 0:
            fail("--dts with no argument died with signal %d" % (-r.returncode))
        if r.returncode == 0:
            fail("--dts with no argument unexpectedly succeeded")

    print("PASS unit-sign-dts")
    sys.exit(0)


if __name__ == "__main__":
    main()
