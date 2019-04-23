#!/usr/bin/python3
'''
 * sign.py
 *
 * Copyright (C) 2019 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
'''

import sys, os, struct, time
from wolfcrypt import ciphers, hashes


WOLFBOOT_MAGIC  = 0x464C4F57
HDR_END         = 0x00
HDR_VERSION     = 0x01
HDR_TIMESTAMP   = 0x02
HDR_SHA256      = 0x03
HDR_IMG_TYPE    = 0x04
HDR_PUBKEY      = 0x10
HDR_SIGNATURE   = 0x20
HDR_PADDING     = 0xFF


HDR_VERSION_LEN     = 4
HDR_TIMESTAMP_LEN   = 8
HDR_SHA256_LEN      = 32
HDR_IMG_TYPE_LEN    = 2
HDR_PUBKEY_LEN      = 32
HDR_SIGNATURE_LEN   = 64

HDR_IMG_TYPE_AUTH_ED25519 = 0x0100
HDR_IMG_TYPE_AUTH_ECC256  = 0x0200

HDR_IMG_TYPE_WOLFBOOT     = 0x0000
HDR_IMG_TYPE_APP          = 0x0001

sign="auto"
self_update=False

argc = len(sys.argv)
argv = sys.argv

if (argc < 4) or (argc > 6):
    print("Usage: %s [--ed25519 | --ecc256 ] [--wolfboot-update] image key.der fw_version\n" % sys.argv[0])
    sys.exit(1)
for i in range(1, len(argv)):
    if (argv[i] == '--ed25519'):
        sign='ed25519'
    elif (argv[i] == '--ecc256'):
        sign='ecc256'
    elif (argv[i] == '--wolfboot-update'):
        self_update = True
    else:
        i-=1
        break

image_file = argv[i+1]
key_file = argv[i+2]
fw_version = int(argv[i+3])

if '.' in image_file:
    tokens = image_file.split('.')
    output_image_file = image_file.rstrip('.' + tokens[-1])
    output_image_file += "_v" + str(fw_version) + "_signed.bin"
else:
    output_image_file = image_file + "_v" + str(fw_version) + "_signed.bin"

if (self_update):
    print("Update type:          wolfBoot")
else:
    print("Update type:          Firmware")

print ("Selected cipher:      " + sign)
print ("Private key:          " + key_file)
print ("Input image:          " + image_file)
print ("Output image:         " + output_image_file)


''' import (decode) private key for signing '''
kf = open(key_file, "rb")
wolfboot_private_key = kf.read(96)
wolfboot_private_key_len = len(wolfboot_private_key)
if wolfboot_private_key_len == 64:
    if (sign == 'ecc256'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ed25519'
        print("'ed25519' key autodetected.")
elif wolfboot_private_key_len == 96:
    if (sign == 'ed25519'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ecc256'
        print("'ecc256' key autodetected.")
else:
    print ("Error: key size does not match any cipher")
    sys.exit(2)


if sign == 'ed25519':
    ed = ciphers.Ed25519Private(key = wolfboot_private_key)
    privkey, pubkey = ed.encode_key()

if sign == 'ecc256':
    ecc = ciphers.EccPrivate()
    ecc.decode_key_raw(wolfboot_private_key[0:31], wolfboot_private_key[32:63], wolfboot_private_key[64:])
    pubkey = wolfboot_private_key[0:64]


img_size = os.path.getsize(image_file)
# Magic header (spells 'WOLF')
header = struct.pack('<L', WOLFBOOT_MAGIC)
# Image size 
header += struct.pack('<L', img_size)

# Two pad bytes so version is aligned
header += struct.pack('BB', 0xFF, 0xFF)

# Version field
header += struct.pack('BB', HDR_VERSION, HDR_VERSION_LEN)
header += struct.pack('<L', fw_version)

# Six pad bytes so timestamp is aligned
header += struct.pack('BB', 0xFF, 0xFF)
header += struct.pack('BB', 0xFF, 0xFF)
header += struct.pack('BB', 0xFF, 0xFF)

# Timestamp field
header += struct.pack('BB', HDR_TIMESTAMP, HDR_TIMESTAMP_LEN)
header += struct.pack('<Q', int(os.path.getmtime(image_file)))

# Image type field
header += struct.pack('BB', HDR_IMG_TYPE, HDR_IMG_TYPE_LEN)
if (sign == 'ed25519'):
    img_type = HDR_IMG_TYPE_AUTH_ED25519
if (sign == 'ecc256'):
    img_type = HDR_IMG_TYPE_AUTH_ECC256

if (not self_update):
    img_type |= HDR_IMG_TYPE_APP

header += struct.pack('<H', img_type)

sha = hashes.Sha256.new()
# Sha calculation
sha.update(header)
img_bin = open(image_file, 'rb')
while True:
    buf = img_bin.read(32)
    if (len(buf) == 0):
        img_bin.close()
        break
    sha.update(buf)
digest = sha.digest()

# Add SHA to the header
header += struct.pack('BB', HDR_SHA256, HDR_SHA256_LEN)
header += digest

# pubkey SHA calculation
keysha = hashes.Sha256.new()
keysha.update(pubkey)
header += struct.pack('BB', HDR_PUBKEY, HDR_PUBKEY_LEN)
key_digest = keysha.digest()
header += key_digest

# Sign the digest
print("Signing the firmware...")
if (sign == 'ed25519'):
    signature = ed.sign(digest)
else:
    r, s = ecc.sign_raw(digest)
    signature = r + s
header += struct.pack('BB', HDR_SIGNATURE, HDR_SIGNATURE_LEN)
header += signature
print ("Done.")

# Create output image. Add padded header in front
outfile = open(output_image_file, 'wb')
outfile.write(header)
sz = len(header)
while sz < 256:
    outfile.write(struct.pack('B',0xFF))
    sz += 1
infile = open(image_file, 'rb')
while True:
    buf = infile.read(1024)
    if len(buf) == 0:
        break
    outfile.write(buf)

infile.close()
outfile.close()
print ("Output image successfully created.")
sys.exit(0)

