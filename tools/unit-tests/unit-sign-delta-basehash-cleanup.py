#!/usr/bin/env python3
# unit-sign-delta-basehash-cleanup.py
#
# Regression test for the delta base-hash validation error path in the C
# signing tool.
#
# make_header_ex() in tools/keytools/sign.c validates the base image digest
# handed to it by base_diff() when signing a delta update (is_diff=1). If the
# base image carries no digest for the selected hash algorithm, or one whose
# size does not match, the function used to call exit(1) directly instead of
# taking its 'failure:' path. That terminates the process from deep inside the
# call chain, so none of the unwinding runs: base_diff()'s 'cleanup:' block
# never unlinks the temporary patch file, and main() never reaches
# zero_and_free(kbuf, key_buffer_sz) or the algorithm-specific key free, so the
# raw and decoded private signing key stay in memory unscrubbed.
#
# The reachable trigger is a base image signed with a different hash algorithm
# than the delta: base_diff() looks up HDR_SHA256/HDR_SHA384/HDR_SHA3_384
# according to CMD.hash_algo, finds nothing, and still calls
# make_header_delta().
#
# Key zeroization is not directly observable from outside the process, but the
# leftover temporary patch file is: it proves that the error return unwound
# through base_diff()'s cleanup instead of aborting the process. This test
# signs a base image with SHA256, asks for a SHA384 delta against it, and
# asserts that the run fails cleanly with /tmp/wolfboot-delta.bin removed.
# Before the fix the file is left behind.
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

SECTOR_SIZE = 0x1000

# wolfboot_delta_file[] in tools/keytools/sign.c
DELTA_TMP = "/tmp/wolfboot-delta.bin"

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(THIS_DIR, "..", ".."))
KEYTOOLS = os.path.join(ROOT, "tools", "keytools")
SIGN = os.path.join(KEYTOOLS, "sign")
KEYGEN = os.path.join(KEYTOOLS, "keygen")


def skip(msg):
    print("SKIP unit-sign-delta-basehash-cleanup: " + msg)
    sys.exit(0)


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
            f.write(payload[:512] + b"PATCHED!" + payload[520:])

        env = dict(os.environ)
        env["WOLFBOOT_SECTOR_SIZE"] = str(SECTOR_SIZE)

        # Sign the base image (v1) with SHA256, so it carries HDR_SHA256 only.
        r = subprocess.run([SIGN, "--ed25519", "--sha256", base, key, "1"],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            skip("sign base failed: " + r.stderr.strip())
        signed_base = base.replace(".bin", "_v1_signed.bin")
        if not os.path.exists(signed_base):
            skip("sign did not produce a signed base image")

        if os.path.exists(DELTA_TMP):
            os.unlink(DELTA_TMP)

        # Ask for a SHA384 delta: base_diff() looks up HDR_SHA384 in the base
        # image, finds nothing, and make_header_ex() must fail cleanly.
        r = subprocess.run([SIGN, "--ed25519", "--sha384", "--delta",
                            signed_base, upd, key, "2"],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        if r.returncode == 0:
            print("FAIL unit-sign-delta-basehash-cleanup: signing a delta "
                  "against a base image with no matching digest succeeded")
            sys.exit(1)

        if os.path.exists(DELTA_TMP):
            os.unlink(DELTA_TMP)
            print("FAIL unit-sign-delta-basehash-cleanup: %s was left behind, "
                  "so make_header_ex() aborted the process instead of "
                  "returning an error; base_diff() cleanup and main()'s "
                  "signing key zeroization were skipped" % DELTA_TMP)
            sys.exit(1)

    print("unit-sign-delta-basehash-cleanup: OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
