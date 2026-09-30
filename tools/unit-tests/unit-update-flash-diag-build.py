#!/usr/bin/env python3
# unit-update-flash-diag-build.py
#
# Compile check for the WOLFBOOT_PERSIST_FAILURE_STATUS path of
# wolfBoot_record_verify_failure() in src/update_flash.c. The failing
# image's version is read through wolfBoot_get_image_version(part),
# which fetches the header from external flash; no runtime unit test
# builds update_flash.c with this option, so keep the ext-flash
# configuration compiling in the host unit CI.
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation; either version 3 of the License, or
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

import subprocess
import sys


def main():
    p = subprocess.run(["make", "unit-update-flash-diag-build"],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print("FAIL: PERSIST_FAILURE_STATUS update_flash.c "
              "does not compile:\n")
        print(p.stdout[-2000:])
        print(p.stderr[-2000:])
        return 1
    print("PASS: PERSIST_FAILURE_STATUS update_flash.c compiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
