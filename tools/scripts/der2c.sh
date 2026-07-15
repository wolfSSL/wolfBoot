#!/bin/sh
#
# der2c.sh <file.der> <symbol> - emit a DER blob as a C array on stdout.
#
# Used to embed wolfBoot's exported signing public key into a HAL that must
# hand it to an HSM at boot
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

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <file.der> <symbol>" >&2
    exit 1
fi

DER="$1"
SYM="$2"

if [ ! -r "$DER" ]; then
    echo "$0: cannot read $DER" >&2
    exit 1
fi

echo "/* Generated from $(basename "$DER") by tools/scripts/der2c.sh. Do not edit. */"
echo "#ifndef WOLFBOOT_GEN_$(echo "$SYM" | tr '[:lower:]' '[:upper:]')_H"
echo "#define WOLFBOOT_GEN_$(echo "$SYM" | tr '[:lower:]' '[:upper:]')_H"
echo ""
echo "#include <stdint.h>"
echo ""
echo "static const uint8_t ${SYM}[] = {"
od -An -v -tx1 "$DER" | awk '
{
    for (i = 1; i <= NF; i++) {
        if (n % 12 == 0) printf "   "
        printf " 0x%s,", $i
        n++
        if (n % 12 == 0) printf "\n"
    }
}
END { if (n % 12 != 0) printf "\n" }'
echo "};"
echo ""
echo "#endif"
