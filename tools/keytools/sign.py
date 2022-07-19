#!/usr/bin/python3
'''
 * sign.py
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

import sys, os, struct, time, re

try:
    import wolfcrypt
except:
    print ("No wolfcrypt support found. Try 'pip install wolfcrypt'")
    sys.exit(1)

from wolfcrypt import ciphers, hashes


WOLFBOOT_MAGIC              = 0x464C4F57
HDR_END                     = 0x00
HDR_VERSION                 = 0x01
HDR_TIMESTAMP               = 0x02
HDR_SHA256                  = 0x03
HDR_IMG_DELTA_BASE          = 0x05
HDR_IMG_DELTA_SIZE          = 0x06
HDR_SHA3_384                = 0x13
HDR_SHA384                  = 0x14
HDR_IMG_DELTA_INVERSE       = 0x15
HDR_IMG_DELTA_INVERSE_SIZE  = 0x16
HDR_IMG_TYPE                = 0x04
HDR_PUBKEY                  = 0x10
HDR_SIGNATURE               = 0x20
HDR_PADDING                 = 0xFF


HDR_VERSION_LEN     = 4
HDR_TIMESTAMP_LEN   = 8
HDR_SHA256_LEN      = 32
HDR_SHA384_LEN      = 48
HDR_SHA3_384_LEN    = 48
HDR_IMG_TYPE_LEN    = 2
HDR_SIGNATURE_LEN   = 64

HDR_IMG_TYPE_AUTH_NONE    = 0xFF00
HDR_IMG_TYPE_AUTH_ED25519 = 0x0100
HDR_IMG_TYPE_AUTH_ECC256  = 0x0200
HDR_IMG_TYPE_AUTH_RSA2048 = 0x0300
HDR_IMG_TYPE_AUTH_RSA4096 = 0x0400
HDR_IMG_TYPE_AUTH_ED448   = 0x0500
HDR_IMG_TYPE_AUTH_ECC384  = 0x0600
HDR_IMG_TYPE_AUTH_ECC521  = 0x0700
HDR_IMG_TYPE_AUTH_RSA3072 = 0x0800
HDR_IMG_TYPE_DIFF         = 0x00D0

HDR_IMG_TYPE_WOLFBOOT     = 0x0000
HDR_IMG_TYPE_APP          = 0x0001

WOLFBOOT_HEADER_SIZE = 256

sign="auto"
self_update=False
sha_only=False
manual_sign=False
encrypt=False
chacha=True
aes128=False
aes256=False
delta=False
encrypt_key_file=None
delta_base_file=None
partition_id = HDR_IMG_TYPE_APP


argc = len(sys.argv)
argv = sys.argv
hash_algo='sha256'


def make_header(image_file, fw_version, extra_fields=[]):
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
    header += struct.pack('BB', HDR_PADDING, HDR_PADDING)
    header += struct.pack('BB', HDR_PADDING, HDR_PADDING)

    # Timestamp field
    header += struct.pack('<HH', HDR_TIMESTAMP, HDR_TIMESTAMP_LEN)
    header += struct.pack('<Q', int(os.path.getmtime(image_file)))

    # Image type field
    header += struct.pack('<HH', HDR_IMG_TYPE, HDR_IMG_TYPE_LEN)
    if (sign == 'none'):
        img_type = HDR_IMG_TYPE_AUTH_NONE
    if (sign == 'ed25519'):
        img_type = HDR_IMG_TYPE_AUTH_ED25519
    if (sign == 'ed448'):
        img_type = HDR_IMG_TYPE_AUTH_ED448
    if (sign == 'ecc256'):
        img_type = HDR_IMG_TYPE_AUTH_ECC256
    if (sign == 'ecc384'):
        img_type = HDR_IMG_TYPE_AUTH_ECC384
    if (sign == 'ecc521'):
        img_type = HDR_IMG_TYPE_AUTH_ECC521
    if (sign == 'rsa2048'):
        img_type = HDR_IMG_TYPE_AUTH_RSA2048
    if (sign == 'rsa3072'):
        img_type = HDR_IMG_TYPE_AUTH_RSA3072
    if (sign == 'rsa4096'):
        img_type = HDR_IMG_TYPE_AUTH_RSA4096

    img_type |= partition_id

    if (delta and len(extra_fields) > 0):
        img_type |= HDR_IMG_TYPE_DIFF

    header += struct.pack('<H', img_type)

    for t in extra_fields:
        tag = t[0]
        sz = t[1]
        if sz == 4:
            while (len(header) % 4) != 0:
                header += struct.pack('B', HDR_PADDING)
        elif sz == 8:
            while (len(header) % 8) != 4:
                header += struct.pack('B', HDR_PADDING)

        payload = t[2]
        header += struct.pack('<HH', tag, sz)
        header += payload

    # Pad bytes. Sha-3 field requires 8-byte alignment
    while (len(header) % 8) != 4:
        header += struct.pack('B', HDR_PADDING)

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

        if sign != 'none':
            # pubkey SHA calculation
            keysha = hashes.Sha256.new()
            keysha.update(pubkey)
            key_digest = keysha.digest()
            header += struct.pack('<HH', HDR_PUBKEY, HDR_SHA256_LEN)
            header += key_digest

    elif hash_algo == 'sha384':
        sha = hashes.Sha384.new()

        # Sha calculation
        sha.update(header)
        img_bin = open(image_file, 'rb')
        while True:
            buf = img_bin.read(48)
            if (len(buf) == 0):
                img_bin.close()
                break
            sha.update(buf)
        digest = sha.digest()

        # Add SHA to the header
        header += struct.pack('<HH', HDR_SHA384, HDR_SHA384_LEN)
        header += digest

        if sign != 'none':
            # pubkey SHA calculation
            keysha = hashes.Sha384.new()
            keysha.update(pubkey)
            key_digest = keysha.digest()
            header += struct.pack('<HH', HDR_PUBKEY, HDR_SHA384_LEN)
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

        if sign != 'none':
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

    if sign != 'none':
        # Sign the digest
        if not manual_sign:
            print("Signing the firmware...")
            if (sign == 'ed25519'):
                signature = ed.sign(digest)
            elif (sign == 'ed448'):
                signature = ed.sign(digest)
            elif (sign[0:3] == 'ecc'):
                r, s = ecc.sign_raw(digest)
                signature = r + s
            elif (sign == 'rsa2048') or (sign == 'rsa4096') or (sign == 'rsa3072'):
                signature = rsa.sign(digest)
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
    return header


#### MAIN ####

print("wolfBoot KeyTools (Python version)")
print("wolfcrypt-py version: " + wolfcrypt.__version__)




if (argc < 4) or (argc > 12):
    print("Usage: "+argv[0]+" [options] image key version");
    print("For full usage manual, see 'docs/Signing.md'");
    sys.exit(1)

i = 1
while (i < len(argv)):
    if (argv[i] == '--no-sign'):
        sign='none'
    elif (argv[i] == '--ed25519'):
        sign='ed25519'
    elif (argv[i] == '--ed448'):
        sign='ed448'
    elif (argv[i] == '--ecc256'):
        sign='ecc256'
    elif (argv[i] == '--ecc384'):
        sign='ecc384'
    elif (argv[i] == '--ecc521'):
        sign='ecc521'
    elif (argv[i] == '--rsa2048'):
        sign='rsa2048'
    elif (argv[i] == '--rsa3072'):
        sign='rsa3072'
    elif (argv[i] == '--rsa4096'):
        sign='rsa4096'
    elif (argv[i] == '--sha256'):
        hash_algo='sha256'
    elif (argv[i] == '--sha384'):
        hash_algo='sha384'
    elif (argv[i] == '--sha3'):
        hash_algo='sha3'
    elif (argv[i] == '--wolfboot-update'):
        self_update = True
        partition_id = HDR_IMG_TYPE_WOLFBOOT
    elif (argv[i] == '--id'):
        i+=1
        partition_id = int(argv[i])
        if partition_id < 0 or partition_id > 15:
            print("Invalid partition id: " + argv[i])
            sys.exit(16)
        if partition_id == 0:
            self_update = True
    elif (argv[i] == '--sha-only'):
        sha_only = True
    elif (argv[i] == '--manual-sign'):
        manual_sign = True
    elif (argv[i] == '--encrypt'):
        encrypt = True
        i += 1
        encrypt_key_file = argv[i]
    elif (argv[i] == '--chacha'):
        encrypt = True
    elif (argv[i] == '--aes128'):
        encrypt = True
        chacha = False
        aes128 = True
    elif (argv[i] == '--aes256'):
        encrypt = True
        chacha = False
        aes256 = True
    elif (argv[i] == '--delta'):
        delta = True
        i += 1
        delta_base_file = argv[i]
    else:
        i-=1
        break
    i += 1


if (encrypt and delta):
    print("Encryption of delta image")

try:
    cfile = open(".config", "r")
except:
    cfile = None
    pass

if cfile:
    l = cfile.readline()
    while l != '':
        if "IMAGE_HEADER_SIZE" in l:
            val=l.split('=')[1].rstrip('\n')
            WOLFBOOT_HEADER_SIZE = int(val,0)
            print("IMAGE_HEADER_SIZE (from .config): " + str(WOLFBOOT_HEADER_SIZE))

        l = cfile.readline()
    cfile.close()


image_file = argv[i+1]
if sign != 'none':
    key_file = argv[i+2]
    fw_version = int(argv[i+3])
else:
    key_file = ''
    fw_version = int(argv[i+2])

if manual_sign:
    signature_file = argv[i+4]

if not sha_only:
    if '.' in image_file:
        tokens = image_file.split('.')
        output_image_file = ''
        for x in tokens[0:-1]:
            output_image_file+=x
        output_image_file += "_v" + str(fw_version) + "_signed.bin"
    else:
        output_image_file = image_file + "_v" + str(fw_version) + "_signed.bin"
else:
    if '.' in image_file:
        tokens = image_file.split('.')
        output_image_file = ''
        for x in tokens[0:-1]:
            output_image_file+=x
        output_image_file += "_v" + str(fw_version) + "_digest.bin"
    else:
        output_image_file = image_file + "_v" + str(fw_version) + "_digest.bin"

if delta and encrypt:
    if '.' in image_file:
        tokens = image_file.split('.')
        encrypted_image_file = ''
        for x in tokens[0:-1]:
            encrypted_image_file += x
        encrypted_output_image_file += "_v" + str(fw_version) + "_signed_diff_encrypted.bin"
    else:
        encrypted_output_image_file = image_file + "_v" + str(fw_version) + "_signed_diff_encrypted.bin"

elif encrypt:
    if '.' in image_file:
        tokens = image_file.split('.')
        encrypted_output_file = ''
        for x in tokens[0:-1]:
            encrypted_output_file += x
        encrypted_output_image_file += "_v" + str(fw_version) + "_signed_and_encrypted.bin"
    else:
        encrypted_output_image_file = image_file + "_v" + str(fw_version) + "_signed_and_encrypted.bin"

if delta:
    if '.' in image_file:
        tokens = image_file.split('.')
        delta_output_image_file = ''
        for x in tokens[0:-1]:
            delta_output_image_file += x
        delta_output_image_file += "_v" + str(fw_version) + "_signed_diff.bin"
    else:
        delta_output_image_file = image_file + "_v" + str(fw_version) + "_signed_diff.bin"

if (self_update):
    print("Update type:          wolfBoot")
else:
    print("Update type:          Firmware")

print ("Input image:          " + image_file)

print ("Selected cipher:      " + sign)
print ("Private key:          " + key_file)

if not sha_only:
    print ("Output image:         " + output_image_file)
else:
    print ("Output digest:        " + output_image_file)

if not encrypt:
    print ("Not Encrypted")
else:
    print ("Encrypted using:      " + encrypt_key_file)
nickname = ""
if partition_id == 0:
    nickname = "(bootloader)"
print ("Target partition id:  " + str(partition_id) +" "+ nickname)

if sign == 'none':
    kf = None
    wolfboot_key_buffer=''
    wolfboot_key_buffer_len = 0
else:
    kf = open(key_file, "rb")
    wolfboot_key_buffer = kf.read(4096)
    wolfboot_key_buffer_len = len(wolfboot_key_buffer)

if wolfboot_key_buffer_len == 0:
    if (sign != 'none'):
        print("Error. Key size is zero but cipher is " + sign)
        sys.exit(3)
    print("*** WARNING: cipher 'none' selected.")
    print("*** Image will not be authenticated!")
    print("*** SECURE BOOT DISABLED.")

elif wolfboot_key_buffer_len == 32:
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
elif wolfboot_key_buffer_len == 114:
    if (sign != 'ed448' and not manual_sign and not sha_only):
        print("Error: key size incorrect for cipher")
        sys.exit(1)
    elif sign == 'auto' and (manual_sign or sha_only):
        sign = 'ed448'
        print("'ed448' public key autodetected.")
elif wolfboot_key_buffer_len == 96:
    if (sign == 'ed25519'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ecc256'
        print("'ecc256' key autodetected.")
elif wolfboot_key_buffer_len == 144:
    if (sign != 'auto' and sign != 'ecc384'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ecc384'
        print("'ecc384' key autodetected.")
elif wolfboot_key_buffer_len == 198:
    if (sign != 'auto' and sign != 'ecc521'):
        print("Error: key size does not match the cipher selected")
        sys.exit(1)
    if sign == 'auto':
        sign = 'ecc521'
        print("'ecc521' key autodetected.")
elif (wolfboot_key_buffer_len > 512):
    if (sign == 'auto'):
        sign = 'rsa4096'
        print("'rsa4096' key autodetected.")
elif (wolfboot_key_buffer_len > 256):
    if (sign == 'auto'):
        sign = 'rsa3072'
        print("'rsa3072' key autodetected.")
elif (wolfboot_key_buffer_len > 128):
    if (sign == 'auto'):
        sign = 'rsa2048'
        print("'rsa2048' key autodetected.")
    elif (sign != 'rsa2048'):
        print ("Error: key size %d too large for the selected cipher" % wolfboot_key_buffer_len)
else:
    print ("Error: key size does not match any cipher")
    sys.exit(2)

if sign == 'none':
    privkey = None
    pubkey = None
elif not sha_only and not manual_sign:
    ''' import (decode) private key for signing '''
    if sign == 'ed25519':
        ed = ciphers.Ed25519Private(key = wolfboot_key_buffer)
        privkey, pubkey = ed.encode_key()

    if sign == 'ed448':
        HDR_SIGNATURE_LEN = 114
        if WOLFBOOT_HEADER_SIZE < 512:
            print("Ed448: header size increased to 512")
            WOLFBOOT_HEADER_SIZE = 512
        ed = ciphers.Ed448Private(key = wolfboot_key_buffer)
        privkey, pubkey = ed.encode_key()

    if sign == 'ecc256':
        ecc = ciphers.EccPrivate()
        ecc.decode_key_raw(wolfboot_key_buffer[0:32], wolfboot_key_buffer[32:64], wolfboot_key_buffer[64:])
        pubkey = wolfboot_key_buffer[0:64]

    if sign == 'ecc384':
        HDR_SIGNATURE_LEN = 96
        if WOLFBOOT_HEADER_SIZE < 512:
            print("Ecc384: header size increased to 512")
            WOLFBOOT_HEADER_SIZE = 512
        ecc = ciphers.EccPrivate()
        ecc.decode_key_raw(wolfboot_key_buffer[0:48], wolfboot_key_buffer[48:96], wolfboot_key_buffer[96:],
                curve_id = ciphers.ECC_SECP384R1)
        pubkey = wolfboot_key_buffer[0:96]

    if sign == 'ecc521':
        HDR_SIGNATURE_LEN = 132
        ecc = ciphers.EccPrivate()
        ecc.decode_key_raw(wolfboot_key_buffer[0:66], wolfboot_key_buffer[66:132], wolfboot_key_buffer[132:],
                curve_id = ciphers.ECC_SECP521R1)
        pubkey = wolfboot_key_buffer[0:132]
        if WOLFBOOT_HEADER_SIZE < 512:
            print("Ecc521: header size increased to 512")
            WOLFBOOT_HEADER_SIZE = 512

    if sign == 'rsa2048':
        if WOLFBOOT_HEADER_SIZE < 512:
            print("Rsa2048: header size increased to 512")
            WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 256
        rsa = ciphers.RsaPrivate(wolfboot_key_buffer)
        privkey,pubkey = rsa.encode_key()

    if sign == 'rsa3072':
        if hash_algo != 'sha256':
            if WOLFBOOT_HEADER_SIZE < 1024:
                print("Rsa3072: header size increased to 1024")
                WOLFBOOT_HEADER_SIZE = 1024
        if WOLFBOOT_HEADER_SIZE < 512:
            print("Rsa3072: header size increased to 512")
            WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 384
        rsa = ciphers.RsaPrivate(wolfboot_key_buffer)
        privkey,pubkey = rsa.encode_key()

    if sign == 'rsa4096':
        if WOLFBOOT_HEADER_SIZE < 1024:
            print("Rsa4096: header size increased to 1024")
            WOLFBOOT_HEADER_SIZE = 1024
        HDR_SIGNATURE_LEN = 512
        rsa = ciphers.RsaPrivate(wolfboot_key_buffer)
        privkey,pubkey = rsa.encode_key()

else:
    if sign == 'rsa2048':
        if WOLFBOOT_HEADER_SIZE < 512:
            WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 256
    if sign == 'rsa3072':
        if WOLFBOOT_HEADER_SIZE < 512:
            WOLFBOOT_HEADER_SIZE = 512
        HDR_SIGNATURE_LEN = 384
    if sign == 'rsa4096':
        if WOLFBOOT_HEADER_SIZE < 1024:
            WOLFBOOT_HEADER_SIZE = 1024
        HDR_SIGNATURE_LEN = 512

    pubkey = wolfboot_key_buffer

header = make_header(image_file, fw_version)

# Create output image. Add padded header in front
outfile = open(output_image_file, 'wb')
outfile.write(header)
sz = len(header)
while sz < WOLFBOOT_HEADER_SIZE:
    outfile.write(struct.pack('B',HDR_PADDING))
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
    delta_align=64
else:
    delta_align=16

if (delta):
    tmp_outfile='/tmp/delta.bin'
    tmp_inv_outfile='/tmp/delta-1.bin'
    os.system('tools/delta/bmdiff ' + delta_base_file + ' ' + output_image_file + ' ' + tmp_outfile)
    os.system('tools/delta/bmdiff ' + output_image_file + ' ' + delta_base_file + ' ' + tmp_inv_outfile)

    delta_size = os.path.getsize(tmp_outfile)
    delta_inv_size = os.path.getsize(tmp_inv_outfile)
    delta_file = open(tmp_outfile, 'ab+')
    delta_inv_file = open(tmp_inv_outfile, 'rb')
    while delta_file.tell() % delta_align != 0:
        delta_file.write(struct.pack('B', 0x00))
    inv_off = delta_file.tell()
    while True:
        cpbuf = delta_inv_file.read(1024)
        if len(cpbuf) == 0:
            break
        delta_file.write(cpbuf)
    delta_file.close()
    delta_inv_file.close()
    base_version = re.split("_", (re.split("_v", delta_base_file)[1]))[0]
    header = make_header(tmp_outfile, fw_version,
            [[HDR_IMG_DELTA_BASE, 4, struct.pack("<L", int(base_version))],
                [HDR_IMG_DELTA_SIZE, 2, struct.pack("<H", delta_size)],
                [HDR_IMG_DELTA_INVERSE, 4, struct.pack("<L", inv_off + WOLFBOOT_HEADER_SIZE)],
                [HDR_IMG_DELTA_INVERSE_SIZE, 2, struct.pack("<H", delta_inv_size)]
                ])
    outfile = open(delta_output_image_file, 'wb')
    outfile.write(header)
    sz = len(header)
    while sz < WOLFBOOT_HEADER_SIZE:
        outfile.write(struct.pack('B', HDR_PADDING))
        sz += 1
    infile = open(tmp_outfile, 'rb')
    while True:
        buf = infile.read(1024)
        if len(buf) == 0:
            break
        outfile.write(buf)
    infile.close()
    outfile.close()
    os.remove(tmp_outfile)
    os.remove(tmp_inv_outfile)
    output_image_file = delta_output_image_file

if (encrypt):
    sz = 0
    off = 0
    outfile = open(output_image_file, 'rb')
    ekeyfile = open(encrypt_key_file, 'rb')
    enc_outfile = open(encrypted_output_image_file, 'wb')
    if chacha:
        print("Encryption algorithm: ChaCha20")
        key = ekeyfile.read(32)
        iv_nonce = ekeyfile.read(12)
        cha = ciphers.ChaCha(key, 32)
        cha.set_iv(iv_nonce, 0)
        while True:
            buf = outfile.read(16)
            if len(buf) == 0:
                break
            enc_outfile.write(cha.encrypt(buf))
    elif aes128:
        print("Encryption algorithm: AES128-CTR")
        key = ekeyfile.read(16)
        iv = ekeyfile.read(16)
        aesctr = ciphers.Aes(key, ciphers.MODE_CTR, iv)
        while True:
            buf = outfile.read(16)
            if len(buf) == 0:
                break
            while (len(buf) % 16) != 0:
                buf += struct.pack('B', HDR_PADDING)
            enc_outfile.write(aesctr.encrypt(buf))
    elif aes256:
        print("Encryption algorithm: AES256-CTR")
        key = ekeyfile.read(32)
        iv = ekeyfile.read(16)
        aesctr = ciphers.Aes(key, ciphers.MODE_CTR, iv)
        while True:
            buf = outfile.read(16)
            if len(buf) == 0:
                break
            while (len(buf) % 16) != 0:
                buf += struct.pack('B', HDR_PADDING)
            enc_outfile.write(aesctr.encrypt(buf))
    outfile.close()
    ekeyfile.close()
    enc_outfile.close()


print ("Output image successfully created.")
sys.exit(0)

