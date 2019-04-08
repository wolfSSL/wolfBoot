#!/usr/bin/python3

import sys,os
from wolfcrypt import ciphers

Cfile_Banner="/* Public-key file for wolfBoot, automatically generated. Do not edit.  */\n"+ \
             "/*\n" + \
             " * This file has been generated and contains the public key which is\n"+ \
             " * used by wolfBoot to verify the updates.\n"+ \
             " */" \
             "\n#include <stdint.h>\n\n" + \
             "const uint8_t ed25519_pub_key[32] = {\n" 

sign="ed25519"

argc = len(sys.argv)
argv = sys.argv

if (argc < 2) or (argc > 3):
    print("Usage: %s [--ed25519 | --ecc256 ] pub_key_file.c\n" % sys.argv[0])
    sys.exit(1)

if argc == 3:
    if argv[1] != '--ed25519' and argv[1] != '--ecc256':
        print("Usage: %s [--ed25519 | --ecc256 ] pub_key_file.c\n" % sys.argv[0])
        sys.exit(1)
    sign=argv[1][2:]
    pubkey_cfile = argv[2]
else:
    pubkey_cfile = argv[1]

if pubkey_cfile[-2:] != '.c':
    print("** Warning: generated public key cfile does not have a '.c' extension")

key_file=sign+".der"

print ("Selected cipher:      " + sign)
print ("Output Private key:   " + key_file)
print ("Output C file:        " + pubkey_cfile)

if (sign == "ed25519"):
    ed = ciphers.Ed25519Private.make_key(32)
    priv,pub = ed.encode_key()
    if os.path.exists(key_file):
        choice = input("** Warning: key file already exist! Are you sure you want to "+
                "generate a new key and overwrite the existing key? [Type 'Yes, I am sure!']: ")
        if (choice != "Yes, I am sure!"):
            print("Operation canceled.")
            sys.exit(2)

    print()
    print("Creating file " + key_file)
    with open(key_file, "wb") as f:
        f.write(priv)
        f.write(pub)
        f.close()
    print("Creating file " + pubkey_cfile)
    with open(pubkey_cfile, "w") as f:
        f.write(Cfile_Banner)
        i = 0
        for c in bytes(pub[0:-1]):
            f.write("0x%02X, " % c)
            i += 1
            if (i % 8 == 0):
                f.write('\n')
        f.write("0x%02X" % pub[-1])
        f.write("\n};\n")
        f.write("const uint32_t ed25519_pub_key_len = 32;\n")
        f.close()

if (sign == "ecc256"):
    ec = ciphers.EccPrivate.make_key(64)
    priv = ec.encode_key()
    if os.path.exists(key_file):
        choice = input("** Warning: key file already exist! Are you sure you want to "+
                "generate a new key and overwrite the existing key? [Type 'Yes, I am sure!']: ")
        if (choice != "Yes, I am sure!"):
            print("Operation canceled.")
            sys.exit(2)

    print()
    print("Creating file " + key_file)
    with open(key_file, "wb") as f:
        f.write(priv)
        f.close()


