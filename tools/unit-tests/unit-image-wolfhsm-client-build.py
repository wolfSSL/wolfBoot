#!/usr/bin/env python3
# unit-image-wolfhsm-client-build.py
#
# Compile check for the WOLFBOOT_ENABLE_WOLFHSM_CLIENT path in
# wolfBoot_verify_signature_ecc() (src/image.c). The raw-to-DER
# signature conversion passes the output length to
# wc_ecc_rs_raw_to_sig(), which expects a word32*; this branch was
# only built by the PIC32CZ cross CI, so keep it compiling in the
# host unit CI as well.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation; either version 3 of the License,
# or (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

import subprocess
import sys


def main():
    p = subprocess.run(["make", "unit-image-wolfhsm-client-build"],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print("FAIL: WOLFBOOT_ENABLE_WOLFHSM_CLIENT image.c "
              "does not compile:\n")
        print(p.stderr[-2000:])
        return 1
    print("PASS: WOLFBOOT_ENABLE_WOLFHSM_CLIENT image.c compiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
