#!/usr/bin/python3
'''
 * sign.py
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
HDR_SHA3_384    = 0x13
HDR_IMG_TYPE    = 0x04
HDR_PUBKEY      = 0x10
HDR_SIGNATURE   = 0x20
HDR_PADDING     = 0xFF


HDR_VERSION_LEN     = 4
HDR_TIMESTAMP_LEN   = 8
HDR_SHA256_LEN      = 32
HDR_SHA3_384_LEN    = 48
HDR_IMG_TYPE_LEN    = 2
HDR_SIGNATURE_LEN   = 64

HDR_IMG_TYPE_AUTH_ED25519 = 0x0100
HDR_IMG_TYPE_AUTH_ECC256  = 0x0200
HDR_IMG_TYPE_AUTH_RSA2048 = 0x0300
HDR_IMG_TYPE_AUTH_RSA4096 = 0x0400

HDR_IMG_TYPE_WOLFBOOT     = 0x0000
HDR_IMG_TYPE_APP          = 0x0001

WOLFBOOT_HEADER_SIZE = 256

sign="auto"
self_update=False
sha_only=False
manual_sign=False
encrypt=False


argc = len(sys.argv)
argv = sys.argv
hash_algo='sha256'

if (argc < 4) or (argc > 10):
    print("Usage: %s [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ] [--sha256 | --sha3] [--wolfboot-update] [--encrypt key.bin] image key.der fw_version\n" % sys.argv[0])
    print("  - or - ")
    print("       %s [--sha256 | --sha3] [--sha-only] [--wolfboot-update] [--encrypt key.bin] image pub_key.der fw_version\n" % sys.argv[0])
    print("  - or - ")
    print("       %s [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ] [--sha256 | --sha3] [--manual-sign] [--encrypt key.bin] image pub_key.der fw_version signature.sig\n" % sys.argv[0])
    sys.exit(1)

i = 1
while (i < len(argv)):
    if (argv[i] == '--ed25519'):
        sign='ed25519'
    elif (argv[i] == '--ecc256'):
        sign='ecc256'
    elif (argv[i] == '--rsa2048'):
        sign='rsa2048'
    elif (argv[i] == '--rsa4096'):
        sign='rsa4096'
    elif (argv[i] == '--sha256'):
        hash_algo='sha256'
    elif (argv[i] == '--sha3'):
        hash_algo='sha3'
    elif (argv[i] == '--wolfboot-update'):
        self_update = True
    elif (argv[i] == '--sha-only'):
        sha_only = True
    elif (argv[i] == '--manual-sign'):
        manual_sign = True
    elif (argv[i] == '--encrypt'):
        encrypt = True
        i += 1
        encrypt_key_file = argv[i]
    else:
        i-=1
        break
    i += 1

image_file = argv[i+1]
key_file = argv[i+2]
fw_version = int(argv[i+3])

if manual_sign:
    signature_file = argv[i+4]

if not sha_only:
    if '.' in image_file:
        tokens = image_file.split('.')
        output_image_file = image_file.rstrip('.' + tokens[-1])
        output_image_file += "_v" + str(fw_version) + "_signed.bin"
    else:
        output_image_file = image_file + "_v" + str(fw_version) + "_signed.bin"
else:
    if '.' in image_file:
        tokens = image_file.split('.')
        output_image_file = image_file.rstrip('.' + tokens[-1])
        output_image_file += "_v" + str(fw_version) + "_digest.bin"
    else:
        output_image_file = image_file + "_v" + str(fw_version) + "_digest.bin"

if encrypt:
    if '.' in image_file:
        tokens = image_file.split('.')
        encrypted_output_image_file = image_file.rstrip('.' + tokens[-1])
        encrypted_output_image_file += "_v" + str(fw_version) + "_signed_and_encrypted.bin"
    else:
        encrypted_output_image_file = image_file + "_v" + str(fw_version) + "_signed_and_encrypted.bin"

if (self_update):
    print("Update type:          wolfBoot")
else:
    print("Update type:          Firmware")

print ("Input image:          " + image_file)

print ("Selected cipher:      " + sign)
print ("Public key:           " + key_file)

if not sha_only:
    print ("Output image:         " + output_image_file)
else:
    print ("Output digest:        " + output_image_file)

if not encrypt:
    print ("Not Encrypted")
else:
    print ("Encrypted using:      " + encrypt_key_file)

kf = open(key_file, "rb")
wolfboot_key_buffer = kf.read(4096)
wolfboot_key_buffer_len = len(wolfboot_key_buffer)
if wolfboot_key_buffer_len == 32:
    if (sign != 'ed25519' and not manual_sign and not sha_only):
        print("Error: key too short for cipher")
        sys.exit(1)
    elif sign == 'auto' and (manual_sign or sha_only):
        sign = 'ed25519'
        print("'ed25519' public key autodetected.")
elif wolfboot_key_buffer_len == 64:
    if (sign == 'ecc256'):
        if not manual_sign and not sha_only:
            print("Error: key size does not match the cipher selected")
            sys.exit(1)
        else:
            print("Ecc256 public key detected")
    if sign == 'auto':
        if (manual_sign or sha_only):
            sign = 'ecc256'
            print("'ecc256' public key autodetected.")
        else:
            sign = 'ed25519'
            print("'ed25519' key autodetected.")
elif wolfboot_key_buffer_len == 96:
    if (sign == 'ed25519'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ecc256'
        print("'ecc256' key autodetected.")
elif (wolfboot_key_buffer_len > 512):
    if (sign == 'auto'):
        print("'rsa4096' key autodetected.")
elif (wolfboot_key_buffer_len > 128):
    if (sign == 'auto'):
        print("'rsa2048' key autodetected.")
    elif (sign != 'rsa2048'):
        print ("Error: key size too large for the selected cipher")
else:
    print ("Error: key size does not match any cipher")
    sys.exit(2)


if not sha_only and not manual_sign:
    ''' import (decode) private key for signing '''
    if sign == 'ed25519':
        ed = ciphers.Ed25519Private(key = wolfboot_key_buffer)
        privkey, pubkey = ed.encode_key()

    if sign == 'ecc256':
        ecc = ciphers.EccPrivate()
        ecc.decode_key_raw(wolfboot_key_buffer[0:31], wolfboot_key_buffer[32:63], wolfboot_key_buffer[64:])
        pubkey = wolfboot_key_buffer[0:64]

    if sign == 'rsa2048':
        WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 256
        rsa = ciphers.RsaPrivate(wolfboot_key_buffer)
        privkey,pubkey = rsa.encode_key()

    if sign == 'rsa4096':
        WOLFBOOT_HEADER_SIZE = 1024
        HDR_SIGNATURE_LEN = 512
        rsa = ciphers.RsaPrivate(wolfboot_key_buffer)
        privkey,pubkey = rsa.encode_key()

else:
    if sign == 'rsa2048':
        WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 256
    if sign == 'rsa4096':
        WOLFBOOT_HEADER_SIZE = 1024
        HDR_SIGNATURE_LEN = 512

    pubkey = wolfboot_key_buffer


img_size = os.path.getsize(image_file)
# Magic header (spells 'WOLF')
header = struct.pack('<L', WOLFBOOT_MAGIC)
# Image size
header += struct.pack('<L', img_size)

# No pad bytes, version is aligned

# Version field
header += struct.pack('<HH', HDR_VERSION, HDR_VERSION_LEN)
header += struct.pack('<L', fw_version)

# Four pad bytes, so timestamp is aligned
header += struct.pack('BB', 0xFF, 0xFF)
header += struct.pack('BB', 0xFF, 0xFF)

# Timestamp field
header += struct.pack('<HH', HDR_TIMESTAMP, HDR_TIMESTAMP_LEN)
header += struct.pack('<Q', int(os.path.getmtime(image_file)))

# Image type field
header += struct.pack('<HH', HDR_IMG_TYPE, HDR_IMG_TYPE_LEN)
if (sign == 'ed25519'):
    img_type = HDR_IMG_TYPE_AUTH_ED25519
if (sign == 'ecc256'):
    img_type = HDR_IMG_TYPE_AUTH_ECC256
if (sign == 'rsa2048'):
    img_type = HDR_IMG_TYPE_AUTH_RSA2048
if (sign == 'rsa4096'):
    img_type = HDR_IMG_TYPE_AUTH_RSA4096

if (not self_update):
    img_type |= HDR_IMG_TYPE_APP

header += struct.pack('<H', img_type)

# Six pad bytes, Sha-3 requires 8-byte alignment.
header += struct.pack('BB', 0xFF, 0xFF)
header += struct.pack('BB', 0xFF, 0xFF)
header += struct.pack('BB', 0xFF, 0xFF)

print("Calculating %s digest..." % hash_algo)

if hash_algo == 'sha256':
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
    header += struct.pack('<HH', HDR_SHA256, HDR_SHA256_LEN)
    header += digest

    # pubkey SHA calculation
    keysha = hashes.Sha256.new()
    keysha.update(pubkey)
    key_digest = keysha.digest()
    header += struct.pack('<HH', HDR_PUBKEY, HDR_SHA256_LEN)
    header += key_digest
    
elif hash_algo == 'sha3':
    sha = hashes.Sha3.new()
    # Sha calculation
    sha.update(header)
    img_bin = open(image_file, 'rb')
    while True:
        buf = img_bin.read(128)
        if (len(buf) == 0):
            img_bin.close()
            break
        sha.update(buf)
    digest = sha.digest()

    # Add SHA to the header
    header += struct.pack('<HH', HDR_SHA3_384, HDR_SHA3_384_LEN)
    header += digest

    # pubkey SHA calculation
    keysha = hashes.Sha3.new()
    keysha.update(pubkey)
    key_digest = keysha.digest()
    header += struct.pack('<HH', HDR_PUBKEY, HDR_SHA3_384_LEN)
    header += key_digest

#print("Image Hash %d" % len(digest))
#print([hex(j) for j in digest])

#print ("Pubkey: %d" % len(pubkey))
#print([hex(j) for j in pubkey])

#print("Pubkey Hash %d" % len(key_digest))
#print([hex(j) for j in key_digest])

if sha_only:
    outfile = open(output_image_file, 'wb')
    outfile.write(digest)
    outfile.close()
    print("Digest image " + output_image_file +" successfully created.")
    print()
    sys.exit(0)

# Sign the digest
if not manual_sign:
    print("Signing the firmware...")
    if (sign == 'ed25519'):
        signature = ed.sign(digest)
    elif (sign == 'ecc256'):
        r, s = ecc.sign_raw(digest)
        signature = r + s
    elif (sign == 'rsa2048') or (sign == 'rsa4096'):
        signature = rsa.sign(digest)
        #plain = rsa.verify(signature)
        #print("plain:%d " % len(plain))
        #print([hex(j) for j in plain])
else:
    print("Opening signature file %s" % signature_file)
    signfile = open(signature_file, 'rb')
    buf = signfile.read(1024)
    signfile.close()
    if len(buf) != HDR_SIGNATURE_LEN:
        print("Wrong signature file size %d, expected %d" % (len(buf), HDR_SIGNATURE_LEN))
        sys.exit(4)
    signature = buf

header += struct.pack('<HH', HDR_SIGNATURE, HDR_SIGNATURE_LEN)
header += signature
#print ("Signature %d" % len(signature))
#print([hex(j) for j in signature])
print ("Done.")

# Create output image. Add padded header in front
outfile = open(output_image_file, 'wb')
outfile.write(header)
sz = len(header)
while sz < WOLFBOOT_HEADER_SIZE:
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
if (encrypt):
    sz = 0
    off = 0
    outfile = open(output_image_file, 'rb')
    ekeyfile = open(encrypt_key_file, 'rb')
    key = ekeyfile.read(32)
    iv_nonce = ekeyfile.read(12)
    enc_outfile = open(encrypted_output_image_file, 'wb')
    cha = ciphers.ChaCha(key, 32)
    while(True):
        cha.set_iv(iv_nonce, off)
        buf = outfile.read(16)
        if len(buf) == 0:
            break
        enc_outfile.write(cha.encrypt(buf))
        off += 1
    outfile.close()
    ekeyfile.close()
    enc_outfile.close()


print ("Output image successfully created.")
sys.exit(0)

