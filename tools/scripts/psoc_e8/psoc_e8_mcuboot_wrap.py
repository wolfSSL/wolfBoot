#!/usr/bin/env python3
"""Wrap wolfboot.bin for the PSOC Edge E8 extended boot.

Extended boot expects the first CM33 image in MCUboot format: a 0x400-byte
header, the image, and a TLV area with the SHA-256 of both. No key is used.
The output matches the factory image byte for byte.

Usage: psoc_e8_mcuboot_wrap.py wolfboot.bin wolfboot_mcuboot.bin
"""

import hashlib
import struct
import sys

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC = 0x6907
TLV_SHA256 = 0x10
HDR_SIZE = 0x400


def wrap(img):
    # magic, load_addr, hdr_size, protect_tlv_size, img_size, flags,
    # version (major, minor, revision, build), pad
    hdr = struct.pack('<IIHHIIBBHII', IMAGE_MAGIC, 0, HDR_SIZE, 0, len(img),
                      0, 0, 0, 0, 0, 0xFFFFFFFF)
    hdr += b'\xff' * (HDR_SIZE - len(hdr))
    digest = hashlib.sha256(hdr + img).digest()
    tlv = struct.pack('<HH', TLV_SHA256, len(digest)) + digest
    info = struct.pack('<HH', TLV_INFO_MAGIC, 4 + len(tlv))
    return hdr + img + info + tlv


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 1
    with open(sys.argv[1], 'rb') as f:
        img = f.read()
    with open(sys.argv[2], 'wb') as f:
        f.write(wrap(img))
    return 0


if __name__ == '__main__':
    sys.exit(main())
