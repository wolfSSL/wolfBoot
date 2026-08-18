#!/usr/bin/env python3
# c2000_flashimg.py
#
# Host-side converter for the wolfBoot TI C2000 C28x (TMS320F28P550SJ) port.
#
# The C28x is word-addressed with CHAR_BIT==16.  wolfBoot stores a signed image
# in the BOOT partition as a SPLIT layout:
#
#   [ header : one octet per 16-bit flash word ][ firmware : native 16-bit words ]
#
# The host `sign` tool operates on an octet stream in which each firmware
# program word is serialized low-octet-then-high-octet.  This script bridges
# the host octet world and the C28x word world with that exact convention, so
# the digest the target computes (see image_sha256() in src/image.c) matches
# the digest `sign` computed.
#
# Two steps, matching the build/sign/flash flow:
#
#   1) fw2oct  - extract the application firmware, as it lives in flash (a raw
#                little-endian 16-bit-word image, e.g. produced from the linked
#                .out with the TI hex2000 utility), into the octet stream that
#                is handed to `sign` as the firmware payload.  Each 16-bit word
#                W becomes two octets: (W & 0xFF) then (W >> 8).
#
#   2) hdr2cells - from the signed image (`sign` output = header octets +
#                firmware octets), take the fixed-size header and expand each
#                header octet into its own 16-bit flash cell (value = octet,
#                high byte 0).  This "header blob" is flashed at the BOOT
#                partition base; the application .out is flashed natively (its
#                codestart is linked at BOOT_ADDRESS + IMAGE_HEADER_SIZE), so no
#                firmware repack is needed.
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3 - see project headers.

import argparse
import struct
import sys


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def write_file(path, data):
    with open(path, "wb") as f:
        f.write(data)


def fw2oct(args):
    """Firmware native-word image -> host octet stream (low, high per word)."""
    words = read_file(args.infile)
    if len(words) % 2 != 0:
        sys.stderr.write("error: input length %d is not a whole number of "
                         "16-bit words\n" % len(words))
        return 1
    out = bytearray()
    for i in range(0, len(words), 2):
        # Input is a little-endian 16-bit-word image (2 bytes/word).
        w = struct.unpack_from("<H", words, i)[0]
        out.append(w & 0xFF)          # low octet first
        out.append((w >> 8) & 0xFF)   # high octet
    write_file(args.outfile, out)
    sys.stderr.write("fw2oct: %d words -> %d octets -> %s\n"
                     % (len(words) // 2, len(out), args.outfile))
    return 0


def hdr2cells(args):
    """Signed image header octets -> C28x flash cells (one octet per word)."""
    signed = read_file(args.signed)
    hdr_sz = args.header_size
    if len(signed) < hdr_sz:
        sys.stderr.write("error: signed image (%d) shorter than header size "
                         "(%d)\n" % (len(signed), hdr_sz))
        return 1
    header = signed[:hdr_sz]
    out = bytearray()
    for octet in header:
        # Each header octet occupies its own 16-bit flash cell (high byte 0),
        # so wolfBoot's octet parser reads it back byte-identically.
        out += struct.pack("<H", octet & 0xFF)
    write_file(args.outfile, out)
    sys.stderr.write("hdr2cells: %d header octets -> %d-word blob for load "
                     "address 0x%X -> %s\n"
                     % (hdr_sz, hdr_sz, args.addr, args.outfile))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p1 = sub.add_parser("fw2oct",
                        help="firmware word-image -> octet stream for sign")
    p1.add_argument("infile", help="firmware native-word image (LE 16-bit words)")
    p1.add_argument("outfile", help="output octet stream to feed to sign")
    p1.set_defaults(func=fw2oct)

    p2 = sub.add_parser("hdr2cells",
                        help="signed image -> header blob (octet-per-cell)")
    p2.add_argument("signed", help="signed image (sign output)")
    p2.add_argument("outfile", help="output header cell blob (LE 16-bit words)")
    p2.add_argument("--header-size", type=lambda x: int(x, 0), default=256,
                    help="IMAGE_HEADER_SIZE in octets (default 256 for ECC256)")
    p2.add_argument("--addr", type=lambda x: int(x, 0), default=0xA0000,
                    help="BOOT partition base word address (default 0xA0000)")
    p2.set_defaults(func=hdr2cells)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
