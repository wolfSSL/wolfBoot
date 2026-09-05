#!/usr/bin/env python3
# unit-elf-scatter-db-build.py
#
# Compile check for the DISABLE_BACKUP + WOLFBOOT_ELF_FLASH_SCATTER +
# EXT_FLASH combination: the ELF-scatter restore block in
# wolfBoot_update() (src/update_flash.c) used to pass the boot struct by
# value to the pointer-taking PART_IS_EXT macro, so this configuration
# failed to build and the branch could never be exercised. It must now
# compile, with the load result checked like in wolfBoot_start().
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

import subprocess
import sys


def main():
    p = subprocess.run(["make", "unit-update-flash-elf-scatter-db"],
                       capture_output=True, text=True)
    if p.returncode != 0:
        print("FAIL: DISABLE_BACKUP + ELF scatter + EXT_FLASH "
              "does not compile:\n")
        print(p.stdout[-2000:])
        print(p.stderr[-2000:])
        return 1
    print("PASS: DISABLE_BACKUP + ELF scatter + EXT_FLASH compiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
