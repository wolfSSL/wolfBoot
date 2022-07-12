/* keygen.c
 *
 * C native key generation tool
 *
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
 */

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
//#define DEBUG_SIGNTOOL

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <wolfssl/wolfcrypt/settings.h>
#ifndef NO_RSA
#include <wolfssl/wolfcrypt/rsa.h>
#endif
#ifdef HAVE_ECC
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/asn.h>

#endif
#ifdef HAVE_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>
#endif

#ifdef HAVE_ED448
#include <wolfssl/wolfcrypt/ed448.h>
#endif

#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef DEBUG_SIGNTOOL
#include <wolfssl/wolfcrypt/logging.h>
#endif

#if defined(_WIN32) && !defined(PATH_MAX)
#define PATH_MAX 256
#endif

#include "wolfboot/wolfboot.h"


#define KEYGEN_NONE    0
#define KEYGEN_ED25519 1
#define KEYGEN_ECC256  2
#define KEYGEN_RSA2048 3
#define KEYGEN_RSA4096 4
#define KEYGEN_ED448   5
#define KEYGEN_ECC384  6
#define KEYGEN_ECC521  7
#define KEYGEN_RSA3072  8

/* Globals */
static FILE *fpub, *fpub_image;
static int force = 0;
static WC_RNG rng;

#ifndef KEYSLOT_MAX_PUBKEY_SIZE
    #define KEYSLOT_MAX_PUBKEY_SIZE 2048
#endif

struct keystore_slot {
     uint32_t slot_id;
     uint32_t key_type;
     uint32_t part_id_mask;
     uint32_t pubkey_size;
     uint8_t  pubkey[KEYSLOT_MAX_PUBKEY_SIZE];
};

const char pubkeyfile[]= "src/keystore.c";
const char pubkeyimg[] = "keystore.der";

const char Cfile_Banner[]="/* Keystore file for wolfBoot, automatically generated. Do not edit.  */\n"
             "/*\n"
             " * This file has been generated and contains the public keys\n"
             " * used by wolfBoot to verify the updates.\n"
             " */" \
             "\n#include <stdint.h>\n#include \"wolfboot/wolfboot.h\"\n"
             "#ifdef WOLFBOOT_NO_SIGN\n\t#define NUM_PUBKEYS 0\n#else\n\n"
             "#if (KEYSTORE_PUBKEY_SIZE != KEYSTORE_PUBKEY_SIZE_%s)\n\t"
             "#error Key algorithm mismatch. Remove old keys via 'make distclean'\n"
             "#else\n";

const char Store_hdr[] = "#define NUM_PUBKEYS %d\nconst struct keystore_slot PubKeys[NUM_PUBKEYS] = {\n\n";
const char Slot_hdr[] = "\t /* Key associated to file '%s' */\n"
            "\t{\n\t\t.slot_id = %d,\n\t\t.key_type = %s,\n"
            "\t\t.part_id_mask = KEY_VERIFY_ALL,\n\t\t.pubkey_size = %s,\n"
            "\t\t.pubkey = {\n\t\t\t";
const char Slot_hdr_int_size[] = "\t /* Key associated to file '%s' */\n"
            "\t{\n\t\t.slot_id = %d,\n\t\t.key_type = %s,\n"
            "\t\t.part_id_mask = KEY_VERIFY_ALL,\n\t\t.pubkey_size = %u,\n"
            "\t\t.pubkey = {\n\t\t\t";
const char Pubkey_footer[] = "\n\t\t},";
const char Slot_footer[] = "\n\t},\n\n";
const char Store_footer[] = "\n};\n\n";

const char Keystore_API[] =
                "int keystore_num_pubkeys(void)\n"
                "{\n"
                "    return NUM_PUBKEYS;\n"
                "}\n\n"
                "uint8_t *keystore_get_buffer(int id)\n"
                "{\n"
                "    if (id >= keystore_num_pubkeys())\n"
                "        return (uint8_t *)0;\n"
                "    return (uint8_t *)PubKeys[id].pubkey;\n"
                "}\n\n"
                "int keystore_get_size(int id)\n"
                "{\n"
                "    if (id >= keystore_num_pubkeys())\n"
                "        return -1;\n"
                "    return (int)PubKeys[id].pubkey_size;\n"
                "}\n\n"
                "uint32_t keystore_get_mask(int id)\n"
                "{\n"
                "    if (id >= keystore_num_pubkeys())\n"
                "        return -1;\n"
                "    return (int)PubKeys[id].part_id_mask;\n"
                "}\n\n"
                "#endif /* Keystore public key size check */\n"
                "#endif /* WOLFBOOT_NO_SIGN */\n";



static void usage(const char *pname) /* implies exit */
{
    printf("Usage: %s [--ed25519 | --ed448 | --ecc256 | --ecc384 "
           "| --ecc521 | --rsa2048 | --rsa3072 "
           "| --rsa4096 ] [-g privkey] [-i pubkey] \n", pname);
    exit(125);
}

static void fwritekey(uint8_t *key, int len, FILE *f)
{
    int i;
    for (i = 0; i < len; i++)
    {
        if ((i % 8) == 0) {
            if (i != 0)
                fprintf(f, ",");
            fprintf(f, "\n\t\t\t");
        }
        else {
            fprintf(f, ", ");
        }
        fprintf(f, "0x%02x", key[i]);
    }
}

const char KType[][17] = {
    "AUTH_KEY_NONE",
    "AUTH_KEY_ED25519",
    "AUTH_KEY_ECC256",
    "AUTH_KEY_RSA2048",
    "AUTH_KEY_RSA4096",
    "AUTH_KEY_ED448",
    "AUTH_KEY_ECC384",
    "AUTH_KEY_ECC521",
    "AUTH_KEY_RSA3072"
};

const char KSize[][29] = {
    "KEYSTORE_PUBKEY_SIZE_NONE",
    "KEYSTORE_PUBKEY_SIZE_ED25519",
    "KEYSTORE_PUBKEY_SIZE_ECC256",
    "KEYSTORE_PUBKEY_SIZE_RSA2048",
    "KEYSTORE_PUBKEY_SIZE_RSA4096",
    "KEYSTORE_PUBKEY_SIZE_ED448",
    "KEYSTORE_PUBKEY_SIZE_ECC384",
    "KEYSTORE_PUBKEY_SIZE_ECC521",
    "KEYSTORE_PUBKEY_SIZE_RSA3072"
};

const char KName[][8] = {
    "NONE",
    "ED25519",
    "ECC256",
    "RSA2048",
    "RSA4096",
    "ED448",
    "ECC384",
    "ECC521",
    "RSA3072"
};

void keystore_add(uint32_t ktype, uint8_t *key, uint32_t sz, const char *keyfile)
{
    static int id_slot = 0;
    struct keystore_slot sl;
    if (ktype == KEYGEN_RSA2048 || ktype == KEYGEN_RSA3072 || ktype == KEYGEN_RSA4096)
        fprintf(fpub, Slot_hdr_int_size,  keyfile, id_slot, KType[ktype], sz);
    else
        fprintf(fpub, Slot_hdr,  keyfile, id_slot, KType[ktype], KSize[ktype]);
    fwritekey(key, sz, fpub);
    fprintf(fpub, Pubkey_footer);
    fprintf(fpub, Slot_footer);
    printf("Associated key file:   %s\n", keyfile);
    printf("Key type   :           %s\n", KName[ktype]);
    printf("Public key slot:       %u\n", id_slot);

    memset(&sl, 0, sizeof(sl));
    sl.slot_id = id_slot;
    sl.key_type = ktype;
    sl.part_id_mask = 0xFFFFFFFF;
    switch (ktype){
        case KEYGEN_ED25519:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_ED25519;
            break;
        case KEYGEN_ED448:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_ED448;
            break;
        case KEYGEN_ECC256:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_ECC256;
            break;
        case KEYGEN_ECC384:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_ECC384;
            break;
        case KEYGEN_RSA2048:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_RSA2048;
            break;
        case KEYGEN_RSA3072:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_RSA3072;
            break;
        case KEYGEN_RSA4096:
            sl.pubkey_size = KEYSTORE_PUBKEY_SIZE_RSA4096;
            break;
        default:
            sl.pubkey_size = 0;
    }

    memcpy(sl.pubkey, key, sz);
    fwrite(&sl, sl.pubkey_size, 1, fpub_image);
    sl.pubkey_size = sz;
    id_slot++;
}


#if !defined(NO_RSA) && defined(WOLFSSL_KEY_GEN)
static void keygen_rsa(const char *keyfile, int kbits)
{
    RsaKey k;
    uint8_t priv_der[4096], pub_der[2048];
    int privlen, publen;
    FILE *fpriv;

    if (wc_InitRsaKey(&k, NULL) != 0) {
        fprintf(stderr, "Unable to initialize RSA%d key\n", kbits);
        exit(1);
    }

    if (wc_MakeRsaKey(&k, kbits, 65537, &rng) != 0) {
        fprintf(stderr, "Unable to create RSA%d key\n", kbits);
        exit(1);
    }
    privlen = wc_RsaKeyToDer(&k, priv_der, kbits);
    if (privlen <= 0) {
        fprintf(stderr, "Unable to export private key to DER\n");
        exit(2);
    }
    publen = wc_RsaKeyToPublicDer(&k, pub_der, kbits);
    if (publen <= 0) {
        fprintf(stderr, "Unable to export public key\n");
        exit(3);
    }
    printf("RSA public key len: %d bytes\n", publen);
    fpriv = fopen(keyfile, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", keyfile, strerror(errno));
        exit(4);
    }
    fwrite(priv_der, privlen, 1, fpriv);
    fclose(fpriv);

    if (kbits == 2048)
        keystore_add(KEYGEN_RSA2048, pub_der, publen, keyfile);
    else if (kbits == 3072)
        keystore_add(KEYGEN_RSA3072, pub_der, publen, keyfile);
    else if (kbits == 4096)
        keystore_add(KEYGEN_RSA4096, pub_der, publen, keyfile);
}
#endif

#ifdef HAVE_ECC
#define MAX_ECC_KEY_SIZE 66

static void keygen_ecc(const char *priv_fname, uint16_t ecc_key_size)
{
    ecc_key k;
    uint8_t Qx[MAX_ECC_KEY_SIZE], Qy[MAX_ECC_KEY_SIZE], d[MAX_ECC_KEY_SIZE];
    uint32_t qxsize = ecc_key_size,
             qysize = ecc_key_size,
             dsize =  ecc_key_size;
    uint8_t k_buffer[2 * MAX_ECC_KEY_SIZE];
    FILE *fpriv;

    if (wc_ecc_make_key(&rng, ecc_key_size, &k) != 0) {
        fprintf(stderr, "Unable to create ecc key\n");
        exit(1);
    }

    if (wc_ecc_export_private_raw(&k, Qx, &qxsize, Qy, &qysize, d, &dsize) != 0) {
        fprintf(stderr, "Unable to export private key to DER\n");
        exit(2);
    }

    if (wc_ecc_export_public_raw(&k, Qx, &qxsize, Qy, &qysize ) != 0) {
        fprintf(stderr, "Unable to export public key\n");
        exit(3);
    }

    fpriv = fopen(priv_fname, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", priv_fname,  strerror(errno));
        exit(3);
    }


    fwrite(Qx, qxsize, 1, fpriv);
    fwrite(Qy, qysize, 1, fpriv);
    fwrite(d, dsize, 1, fpriv);
    fclose(fpriv);
    memcpy(k_buffer, Qx, ecc_key_size);
    memcpy(k_buffer + ecc_key_size, Qy, ecc_key_size);

    if (ecc_key_size == 32)
        keystore_add(KEYGEN_ECC256, k_buffer, 2 * ecc_key_size, priv_fname);
    else if (ecc_key_size == 48)
        keystore_add(KEYGEN_ECC384, k_buffer, 2 * ecc_key_size, priv_fname);
    else if (ecc_key_size == 66)
        keystore_add(KEYGEN_ECC521, k_buffer, 2 * ecc_key_size, priv_fname);
}
#endif


#ifdef HAVE_ED25519
static void keygen_ed25519(const char *privkey)
{
    ed25519_key k;
    uint8_t priv[32], pub[32];
    FILE *fpriv;
    uint32_t outlen = ED25519_KEY_SIZE;
    if (wc_ed25519_make_key(&rng, ED25519_KEY_SIZE, &k) != 0) {
        fprintf(stderr, "Unable to create ed25519 key\n");
        exit(1);
    }
    if (wc_ed25519_export_private_only(&k, priv, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed25519 private key\n");
        exit(2);
    }
    if (wc_ed25519_export_public(&k, pub, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed25519 public key\n");
        exit(2);
    }
    fpriv = fopen(privkey, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", privkey, strerror(errno));
        exit(3);
    }
    fwrite(priv, 32, 1, fpriv);
    fwrite(pub, 32, 1, fpriv);
    fclose(fpriv);
    keystore_add(KEYGEN_ED25519, pub, ED25519_PUB_KEY_SIZE, privkey);
}
#endif

#ifdef HAVE_ED448
static void keygen_ed448(const char *privkey)
{
    ed448_key k;
    uint8_t priv[ED448_KEY_SIZE], pub[ED448_PUB_KEY_SIZE];
    FILE *fpriv;
    uint32_t outlen = ED448_KEY_SIZE;
    if (wc_ed448_make_key(&rng, ED448_KEY_SIZE, &k) != 0) {
        fprintf(stderr, "Unable to create ed448 key\n");
        exit(1);
    }
    if (wc_ed448_export_private_only(&k, priv, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed448 private key\n");
        exit(2);
    }
    if (wc_ed448_export_public(&k, pub, &outlen) != 0) {
        fprintf(stderr, "Unable to export ed448 public key\n");
        exit(2);
    }
    fpriv = fopen(privkey, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file 'ed448.der' for writing: %s", strerror(errno));
        exit(3);
    }
    fwrite(priv, ED448_KEY_SIZE, 1, fpriv);
    fwrite(pub, ED448_PUB_KEY_SIZE, 1, fpriv);
    fclose(fpriv);
    keystore_add(KEYGEN_ED448, pub, ED448_PUB_KEY_SIZE, privkey);
}
#endif


static void key_generate(uint32_t ktype, const char *kfilename)
{
    FILE *f;
    f = fopen(kfilename, "rb");
    if (!force && (f != NULL)) {
        char reply[40];
        int replySz;
        printf("** Warning: key file already exist! Are you sure you want to  generate a new key and overwrite the existing key? [Type 'Yes']: ");
        fflush(stdout);
        replySz = scanf("%s", reply);
        printf("Reply is [%s]\n", reply);
        fclose(f);
        if (replySz < 0 || strcmp(reply, "Yes") != 0) {
            printf("Operation aborted by user.");
            exit(5);
        } else {
            unlink(kfilename);
        }
    }
    printf("Generating key (type: %s)\n", KName[ktype]);
    fflush(stdout);

    switch (ktype) {
#ifdef HAVE_ED25519
        case KEYGEN_ED25519:
            keygen_ed25519(kfilename);
            break;
#endif

#ifdef HAVE_ED448
        case KEYGEN_ED448:
            keygen_ed448(kfilename);
            break;
#endif

#ifdef HAVE_ECC
        case KEYGEN_ECC256:
            keygen_ecc(kfilename, 32);
            break;
        case KEYGEN_ECC384:
            keygen_ecc(kfilename, 48);
            break;
        case KEYGEN_ECC521:
            keygen_ecc(kfilename, 66);
            break;
#endif

#ifndef NO_RSA
        case KEYGEN_RSA2048:
            keygen_rsa(kfilename, 2048);
            break;
        case KEYGEN_RSA3072:
            keygen_rsa(kfilename, 3072);
            break;
        case KEYGEN_RSA4096:
            keygen_rsa(kfilename, 4096);
            break;
#endif
    } /* end switch */
}

static void key_import(uint32_t ktype, const char *fname)
{
    uint8_t buf[KEYSLOT_MAX_PUBKEY_SIZE];
    FILE *f;
    int r;
    f = fopen(fname, "rb");
    if (f == NULL) {
        fprintf(stderr, "Fatal error: could not open file %s to import public key\n", fname);
        exit(6);
    }
    r = fread(buf, sizeof(buf), 1, f);
    keystore_add(ktype, buf, r, fname);
}

int main(int argc, char** argv)
{
    int i;
    int  keytype = 0;
    uint32_t n_pubkeys = 0;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    /* Check arguments and print usage */
    if (argc < 2)
        usage(argv[0]);

    for (i = 1; i < argc; i++) {
        /* Parse Arguments */
        if (strcmp(argv[i], "--ed25519") == 0) {
            keytype = KEYGEN_ED25519;
        }
        else if (strcmp(argv[i], "--ed448") == 0) {
            keytype = KEYGEN_ED448;
        }
        else if (strcmp(argv[i], "--ecc256") == 0) {
            keytype = KEYGEN_ECC256;
        }
        else if (strcmp(argv[i], "--ecc384") == 0) {
            keytype = KEYGEN_ECC384;
        }
        else if (strcmp(argv[i], "--ecc521") == 0) {
            keytype = KEYGEN_ECC521;
        }
        else if (strcmp(argv[i], "--rsa2048") == 0) {
            keytype = KEYGEN_RSA2048;
        }
        else if (strcmp(argv[i], "--rsa3072") == 0) {
            keytype = KEYGEN_RSA3072;
        }
        else if (strcmp(argv[i], "--rsa4096") == 0) {
            keytype = KEYGEN_RSA4096;
        }
        else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        }
        else if (strcmp(argv[i], "-g") == 0) {
            i++;
            n_pubkeys++;
            continue;
        }
        else if (strcmp(argv[i], "-i") == 0) {
            i++;
            n_pubkeys++;
            continue;
        }
        else {
            fprintf(stderr, "Invalid argument '%s'.", argv[i]);
            usage(argv[0]);
        }
    }
    printf("Keytype: %s\n", KName[keytype]);
    if (keytype == 0)
        exit(0);
    fpub = fopen(pubkeyfile, "w");
    if (fpub == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", pubkeyfile, strerror(errno));
        exit(4);
    }
    fpub_image = fopen(pubkeyimg, "wb");
    if (fpub_image == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", pubkeyimg, strerror(errno));
        exit(4);
    }
    wc_InitRng(&rng);
    fprintf(fpub, Cfile_Banner, KName[keytype]);
    fprintf(fpub, Store_hdr, n_pubkeys);
    for (i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            printf("Imp %s\n", argv[i + 1]);
            key_import(keytype, argv[i + 1]);
            i++;
        }
        else if (strcmp(argv[i], "-g") == 0) {
            printf("Gen %s\n", argv[i + 1]);
            key_generate(keytype, argv[i + 1]);
            i++;
        }
    }
    wc_FreeRng(&rng);
    fprintf(fpub, Store_footer);
    fprintf(fpub, Keystore_API);
    if (fpub)
        fclose(fpub);
    if (fpub_image)
        fclose(fpub_image);
    printf("Done.\n");
    return 0;
}
