#!/usr/bin/env python3
# unit-sign-custom-tlv-pubkey-der.py
#
# Tests --custom-tlv-pubkey-der in the C sign tool (tools/keytools/sign.c):
# a DER SubjectPublicKeyInfo is parsed and the public key is stored as a
# custom TLV in the keystore format (X||Y for ECC, raw bytes for
# Ed25519/Ed448, public key DER for RSA). Also checks that a SEC1 EC
# private key file embeds only the public point and never the private
# scalar, and that unparseable, PKCS#8 private, empty or missing files
# are rejected.
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

HDR_PADDING = 0xFF

TAG_ECC256 = 0x0040
TAG_ECC521 = 0x0041
TAG_ED25519 = 0x0042
TAG_ED448 = 0x0043
TAG_RSA = 0x0044
TAG_SEC1 = 0x0045

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
SIGN = os.path.join(ROOT, "tools", "keytools", "sign")

failures = []


def skip(msg):
    print("SKIP unit-sign-custom-tlv-pubkey-der: " + msg)
    sys.exit(0)


def fail(msg):
    failures.append(msg)


def parse_tlvs(data, scan_end):
    """Walk the header like wolfBoot_find_header(): {tag: value bytes}."""
    tlvs = {}
    p = 8  # skip 4-byte magic + 4-byte image size
    while p + 4 <= scan_end:
        htype = data[p] | (data[p + 1] << 8)
        if htype == 0:
            break
        if data[p] == HDR_PADDING or (p & 1) != 0:
            p += 1
            continue
        length = data[p + 2] | (data[p + 3] << 8)
        if p + 4 + length > scan_end:
            break
        if htype not in tlvs:
            tlvs[htype] = bytes(data[p + 4:p + 4 + length])
        p += 4 + length
    return tlvs


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


def run_sign(args, image, key, version):
    cmd = [SIGN, "--ed25519", "--sha256"] + args + [image, key, version]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def signed_name(image, version):
    return image.replace(".bin", "_v%s_signed.bin" % version)


def make_image(path, payload):
    with open(path, "wb") as f:
        f.write(payload)


def check_signed_layout(name, signed, payload):
    """Return (data, header_size) or None; header must be a power of two
    holding the payload right after it."""
    with open(signed, "rb") as f:
        data = f.read()
    hdr = len(data) - len(payload)
    if hdr <= 0 or (hdr & (hdr - 1)) != 0:
        fail("%s: file size %d - payload %d = %d is not a power-of-two "
             "header" % (name, len(data), len(payload), hdr))
        return None
    if data[hdr:] != payload:
        fail("%s: payload is not stored at header size %d" % (name, hdr))
        return None
    return data, hdr


def check_value(name, tlvs, tag, expected):
    got = tlvs.get(tag)
    if got is None:
        fail("%s: tag 0x%04x not found in header" % (name, tag))
    elif got != expected:
        fail("%s: tag 0x%04x value mismatch (%d bytes, want %d)" %
             (name, tag, len(got), len(expected)))


def expect_reject(name, args, image, key, version, needle):
    signed = signed_name(image, version)
    if os.path.exists(signed):
        os.unlink(signed)
    r = run_sign(args, image, key, version)
    if r.returncode == 0:
        fail("%s: sign succeeded, expected rejection" % name)
        return
    if needle not in r.stderr:
        fail("%s: expected '%s' in stderr, got: %s" %
             (name, needle, r.stderr.strip()[:200]))
    if os.path.exists(signed):
        fail("%s: rejected sign still produced an output image" % name)


def ecc_xy(pub, size):
    n = pub.public_numbers()
    return n.x.to_bytes(size, "big") + n.y.to_bytes(size, "big")


def main():
    if not ensure_sign():
        skip("could not build tools/keytools/sign")

    try:
        from cryptography.hazmat.primitives.asymmetric import (ec, rsa,
            ed25519, ed448)
        from cryptography.hazmat.primitives.serialization import (Encoding,
            PublicFormat, PrivateFormat, NoEncryption)
    except Exception:
        skip("python cryptography module not available")

    def spki(pub):
        return pub.public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo)

    def raw(pub):
        return pub.public_bytes(Encoding.Raw, PublicFormat.Raw)

    with tempfile.TemporaryDirectory() as work:
        key = os.path.join(work, "priv.der")
        if not make_ed25519_key(key):
            skip("python cryptography module not available")

        payload = bytes((i * 7) & 0xFF for i in range(2048))

        # Control: a plain sign must work, or the environment is broken and
        # every other failure below would be misleading.
        control = os.path.join(work, "control.bin")
        make_image(control, payload)
        r = run_sign([], control, key, "1")
        if r.returncode != 0 or not os.path.exists(signed_name(control, "1")):
            skip("control sign failed: " + r.stderr.strip())

        # One key per supported algorithm, all embedded in a single image.
        k_ecc256 = ec.generate_private_key(ec.SECP256R1())
        k_ecc521 = ec.generate_private_key(ec.SECP521R1())
        k_ed25519 = ed25519.Ed25519PrivateKey.generate()
        k_ed448 = ed448.Ed448PrivateKey.generate()
        k_rsa = rsa.generate_private_key(public_exponent=65537, key_size=2048)

        keys = {
            "ecc256.der": spki(k_ecc256.public_key()),
            "ecc521.der": spki(k_ecc521.public_key()),
            "ed25519.der": spki(k_ed25519.public_key()),
            "ed448.der": spki(k_ed448.public_key()),
            "rsa.der": spki(k_rsa.public_key()),
        }
        for fname, der in keys.items():
            with open(os.path.join(work, fname), "wb") as f:
                f.write(der)

        expected = {
            TAG_ECC256: ecc_xy(k_ecc256.public_key(), 32),
            TAG_ECC521: ecc_xy(k_ecc521.public_key(), 66),
            TAG_ED25519: raw(k_ed25519.public_key()),
            TAG_ED448: raw(k_ed448.public_key()),
            # RSA has no raw form: the TLV holds the public key DER, which
            # wolfCrypt re-encodes to the same canonical SPKI bytes.
            TAG_RSA: keys["rsa.der"],
        }

        image = os.path.join(work, "pubkeys.bin")
        make_image(image, payload)
        r = run_sign(["--custom-tlv-pubkey-der", hex(TAG_ECC256),
                      os.path.join(work, "ecc256.der"),
                      "--custom-tlv-pubkey-der", hex(TAG_ECC521),
                      os.path.join(work, "ecc521.der"),
                      "--custom-tlv-pubkey-der", hex(TAG_ED25519),
                      os.path.join(work, "ed25519.der"),
                      "--custom-tlv-pubkey-der", hex(TAG_ED448),
                      os.path.join(work, "ed448.der"),
                      "--custom-tlv-pubkey-der", hex(TAG_RSA),
                      os.path.join(work, "rsa.der")],
                     image, key, "1")
        if r.returncode != 0:
            fail("pubkeys: sign failed: " + r.stderr.strip()[:200])
        else:
            res = check_signed_layout("pubkeys", signed_name(image, "1"),
                                      payload)
            if res:
                data, hdr = res
                tlvs = parse_tlvs(data, hdr)
                for tag, val in expected.items():
                    check_value("pubkeys", tlvs, tag, val)

        # A SEC1 EC private key file is accepted (wolfCrypt extracts the
        # public point), but the private scalar must never reach the image.
        sec1 = os.path.join(work, "ecc256-sec1-priv.der")
        with open(sec1, "wb") as f:
            f.write(k_ecc256.private_bytes(Encoding.DER,
                    PrivateFormat.TraditionalOpenSSL, NoEncryption()))
        scalar = k_ecc256.private_numbers().private_value.to_bytes(32, "big")
        image = os.path.join(work, "sec1.bin")
        make_image(image, payload)
        r = run_sign(["--custom-tlv-pubkey-der", hex(TAG_SEC1), sec1],
                     image, key, "1")
        if r.returncode != 0:
            fail("sec1: sign failed: " + r.stderr.strip()[:200])
        else:
            res = check_signed_layout("sec1", signed_name(image, "1"),
                                      payload)
            if res:
                data, hdr = res
                check_value("sec1", parse_tlvs(data, hdr), TAG_SEC1,
                            expected[TAG_ECC256])
                if scalar in data:
                    fail("sec1: private scalar leaked into signed image")

        # Rejections.
        image = os.path.join(work, "rej.bin")
        make_image(image, payload)

        garbage = os.path.join(work, "garbage.der")
        with open(garbage, "wb") as f:
            f.write(bytes((i * 89 + 17) & 0xFF for i in range(100)))
        expect_reject("reject-garbage",
                      ["--custom-tlv-pubkey-der", hex(TAG_ECC256), garbage],
                      image, key, "1", "Unable to parse")

        pkcs8 = os.path.join(work, "ecc256-pkcs8-priv.der")
        with open(pkcs8, "wb") as f:
            f.write(k_ecc256.private_bytes(Encoding.DER, PrivateFormat.PKCS8,
                                           NoEncryption()))
        expect_reject("reject-pkcs8-private",
                      ["--custom-tlv-pubkey-der", hex(TAG_ECC256), pkcs8],
                      image, key, "1", "Unable to parse")

        empty = os.path.join(work, "empty.der")
        open(empty, "wb").close()
        expect_reject("reject-empty",
                      ["--custom-tlv-pubkey-der", hex(TAG_ECC256), empty],
                      image, key, "1", "Invalid public key DER file size")

        expect_reject("reject-missing",
                      ["--custom-tlv-pubkey-der", hex(TAG_ECC256),
                       os.path.join(work, "does-not-exist.der")],
                      image, key, "1", "Cannot open")

        expect_reject("reject-bad-tag",
                      ["--custom-tlv-pubkey-der", "0x0001",
                       os.path.join(work, "ecc256.der")],
                      image, key, "1", "Invalid custom tag")

    if failures:
        for msg in failures:
            print("FAIL unit-sign-custom-tlv-pubkey-der: " + msg)
        sys.exit(1)

    print("unit-sign-custom-tlv-pubkey-der: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
