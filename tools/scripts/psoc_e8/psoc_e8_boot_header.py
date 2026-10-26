#!/usr/bin/env python3
"""Add the extended-boot image header to wolfboot.bin for PSOC Edge E8.

The boot ROM only launches a CM33 image that carries a 0x400-byte header and a
trailing SHA-256 of header and image (an MCUboot-compatible, unsigned format).

Usage: psoc_e8_boot_header.py wolfboot.bin wolfboot_boot.bin [slot_size]

With slot_size, fails if header, image and trailer do not fit the boot slot.
"""

import hashlib
import struct
import sys

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC = 0x6907
TLV_SHA256 = 0x10
HDR_SIZE = 0x400
# TLV info header, SHA-256 TLV header and digest.
TRAILER_SIZE = 4 + 4 + 32


def add_header(img, slot_size=None):
    # magic, load_addr, hdr_size, protect_tlv_size, img_size, flags,
    # version (major, minor, revision, build), pad
    hdr = struct.pack('<IIHHIIBBHII', IMAGE_MAGIC, 0, HDR_SIZE, 0, len(img),
                      0, 0, 0, 0, 0, 0xFFFFFFFF)
    hdr += b'\xff' * (HDR_SIZE - len(hdr))
    digest = hashlib.sha256(hdr + img).digest()
    tlv = struct.pack('<HH', TLV_SHA256, len(digest)) + digest
    info = struct.pack('<HH', TLV_INFO_MAGIC, 4 + len(tlv))
    out = hdr + img + info + tlv
    if slot_size is not None and len(out) > slot_size:
        raise ValueError('boot image is 0x%x bytes, slot is 0x%x'
                         % (len(out), slot_size))
    return out


def main():
    if len(sys.argv) not in (3, 4):
        sys.stderr.write(__doc__)
        return 1
    slot_size = int(sys.argv[3], 0) if len(sys.argv) == 4 else None
    with open(sys.argv[1], 'rb') as f:
        img = f.read()
    try:
        out = add_header(img, slot_size)
    except ValueError as e:
        sys.stderr.write('psoc_e8_boot_header: %s\n' % e)
        return 1
    with open(sys.argv[2], 'wb') as f:
        f.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
