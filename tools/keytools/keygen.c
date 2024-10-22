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
 * the Free Software Foundation; either version 3 of the License, or
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

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE /* unlink */
#endif
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
#ifndef _WIN32
    #include <unistd.h>
#endif

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

#if defined(WOLFSSL_HAVE_LMS)
    #include <wolfssl/wolfcrypt/lms.h>
    #ifdef HAVE_LIBLMS
        #include <wolfssl/wolfcrypt/ext_lms.h>
    #else
        #include <wolfssl/wolfcrypt/wc_lms.h>
    #endif
#endif

#if defined(WOLFSSL_HAVE_XMSS)
    #include <wolfssl/wolfcrypt/xmss.h>
    #ifdef HAVE_LIBXMSS
        #include <wolfssl/wolfcrypt/ext_xmss.h>
    #else
        #include <wolfssl/wolfcrypt/wc_xmss.h>
    #endif
#endif

#ifdef WOLFSSL_WC_DILITHIUM
    #include <wolfssl/wolfcrypt/dilithium.h>
#endif

#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef DEBUG_SIGNTOOL
#include <wolfssl/wolfcrypt/logging.h>
#endif

#if !defined(PATH_MAX)
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
#define KEYGEN_RSA3072 8
#define KEYGEN_LMS     9
#define KEYGEN_XMSS    10
#define KEYGEN_ML_DSA  11

/* Globals */
static FILE *fpub, *fpub_image;
static int force = 0;
#if defined(WOLFBOOT_RENESAS_RSIP) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_SCEPROTECT)
static int saveAsDer = 1; /* For Renesas PKA default to save as DER/ASN.1 */
#else
static int saveAsDer = 0;
#endif
static WC_RNG rng;

#ifndef KEYSLOT_MAX_PUBKEY_SIZE
    #if defined(KEYSTORE_PUBKEY_SIZE_ML_DSA)
        /* ML-DSA pub keys are big. */
        #define KEYSLOT_MAX_PUBKEY_SIZE KEYSTORE_PUBKEY_SIZE_ML_DSA
    #else
        #define KEYSLOT_MAX_PUBKEY_SIZE 576
    #endif
#endif

struct keystore_slot {
     uint32_t slot_id;
     uint32_t key_type;
     uint32_t part_id_mask;
     uint32_t pubkey_size;
     uint8_t  pubkey[KEYSLOT_MAX_PUBKEY_SIZE];
};

char pubkeyfile[PATH_MAX]= "src/keystore.c";
char pubkeyimg[PATH_MAX] = "keystore.der";

const char Cfile_Banner[]=
    "/* Keystore file for wolfBoot, automatically generated. Do not edit.  */\n"
    "/*\n"
    " * This file has been generated and contains the public keys\n"
    " * used by wolfBoot to verify the updates.\n"
    " */"
    "\n"
    "#include <stdint.h>\n"
    "#include \"wolfboot/wolfboot.h\"\n"
    "#include \"keystore.h\"\n"
#ifdef RENESAS_KEY
    "#if defined(WOLFBOOT_RENESAS_TSIP) || defined(WOLFBOOT_RENESAS_RSIP)\n"
    "    #include \"user_settings.h\"\n"
    "    #if defined(WOLFBOOT_RENESAS_TSIP)\n"
    "        #include \"key_data.h\"\n"
    "    #elif defined(WOLFBOOT_RENESAS_RSIP)\n"
    "        #include \"rsa_pub.h\"\n"
    "    #endif\n"
    "#endif\n"
#endif
    "\n"
    "#ifdef WOLFBOOT_NO_SIGN\n"
    "    #define NUM_PUBKEYS 0\n#else\n"
    "\n"
    "#if !defined(KEYSTORE_ANY) && (KEYSTORE_PUBKEY_SIZE != KEYSTORE_PUBKEY_SIZE_%s)\n"
    "    #error Key algorithm mismatch. Remove old keys via 'make keysclean'\n"
    "#else\n";

const char Store_hdr[] = "\n"
    "#if defined(__APPLE__) && defined(__MACH__)\n"
    "#define KEYSTORE_SECTION __attribute__((section (\"__KEYSTORE,__keystore\")))\n"
    "#elif defined(__CCRX__) /* Renesas RX */\n"
    "#define KEYSTORE_SECTION\n"
    "#elif defined(TARGET_x86_64_efi)\n"
    "#define KEYSTORE_SECTION\n"
    "#else\n"
    "#define KEYSTORE_SECTION __attribute__((section (\".keystore\")))\n"
    "#endif\n\n"
    "#define NUM_PUBKEYS %d\n"
    "const KEYSTORE_SECTION struct keystore_slot PubKeys[NUM_PUBKEYS] = {\n"
    "\n";
const char Slot_hdr[] =
    "    /* Key associated to file '%s' */\n"
    "    {\n"
    "        .slot_id = %d,\n"
    "        .key_type = %s,\n"
    "        .part_id_mask = 0x%08X,\n"
    "        .pubkey_size = %u,\n"
    "        .pubkey = {\n"
#ifdef RENESAS_KEY
    "#if !defined(WOLFBOOT_RENESAS_RSIP) && \\\n"
    "    !defined(WOLFBOOT_RENESAS_TSIP) && \\\n"
    "    !defined(WOLFBOOT_RENESAS_SCEPROTECT)\n"
#endif
    "            ";
const char Pubkey_footer[] =
    "\n"
#ifdef RENESAS_KEY
    "#endif"
#endif
    "\n"
    "\n"
    "        },";
const char Slot_footer[] = "\n"
    "    },\n"
    "\n";
const char Store_footer[] = "\n"
    "};"
    "\n"
    "\n";

const char Keystore_API[] =
"int keystore_num_pubkeys(void)\n"
    "{\n"
    "    return NUM_PUBKEYS;\n"
    "}\n"
    "\n"
    "uint8_t *keystore_get_buffer(int id)\n"
    "{\n"
    "    (void)id;\n"
#ifdef RENESAS_KEY
    "#if defined(WOLFBOOT_RENESAS_SCEPROTECT)\n"
    "    return (uint8_t*)RENESAS_SCE_INSTALLEDKEY_ADDR;\n"
    "#elif defined(WOLFBOOT_RENESAS_TSIP)\n"
    "    return (uint8_t*)RENESAS_TSIP_INSTALLEDKEY_ADDR;\n"
    "#elif defined(WOLFBOOT_RENESAS_RSIP)\n"
    "    return (uint8_t*)RENESAS_RSIP_INSTALLEDKEY_RAM_ADDR;\n"
    "#else\n"
#endif
    "    if (id >= keystore_num_pubkeys())\n"
    "        return (uint8_t *)0;\n"
    "    return (uint8_t *)PubKeys[id].pubkey;\n"
#ifdef RENESAS_KEY
    "#endif\n"
#endif
    "}\n"
    "\n"
    "int keystore_get_size(int id)\n"
    "{\n"
    "    (void)id;\n"
#ifdef RENESAS_KEY
    "#if defined(WOLFBOOT_RENESAS_SCEPROTECT)\n"
    "    return (int)260;\n"
    "#elif defined(WOLFBOOT_RENESAS_TSIP)\n"
    "    return (int)ENCRYPTED_KEY_BYTE_SIZE;\n"
    "#elif defined(WOLFBOOT_RENESAS_RSIP)\n"
    "    return (int)sizeof(rsa_public_t);\n"
    "#else\n"
#endif
    "    if (id >= keystore_num_pubkeys())\n"
    "        return -1;\n"
    "    return (int)PubKeys[id].pubkey_size;\n"
#ifdef RENESAS_KEY
    "#endif\n"
#endif
    "}\n"
    "\n"
    "uint32_t keystore_get_mask(int id)\n"
    "{\n"
    "    if (id >= keystore_num_pubkeys())\n"
    "        return 0;\n"
    "    return (int)PubKeys[id].part_id_mask;\n"
    "}\n"
    "\n"
    "uint32_t keystore_get_key_type(int id)\n"
    "{\n"
    "    return PubKeys[id].key_type;\n"
    "}\n"
    "\n"
    "#endif /* Keystore public key size check */\n"
    "#endif /* WOLFBOOT_NO_SIGN */\n";


static void usage(const char *pname) /* implies exit */
{
    printf("Usage: %s [--ed25519 | --ed448 | --ecc256 | --ecc384 "
           "| --ecc521 | --rsa2048 | --rsa3072 | --rsa4096 ] "
           "[-g privkey] [-i pubkey] [-keystoreDir dir] "
           "[--id {list}] [--der]\n", pname);
    exit(125);
}

static void fwritekey(uint8_t *key, int len, FILE *f)
{
    int i;
    for (i = 0; i < len; i++) {
        if ((i % 8) == 0) {
            if (i != 0)
                fprintf(f, ",");
            fprintf(f, "\n            ");
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
    "AUTH_KEY_RSA3072",
    "AUTH_KEY_LMS",
    "AUTH_KEY_XMSS",
    "AUTH_KEY_ML_DSA"
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
    "KEYSTORE_PUBKEY_SIZE_RSA3072",
    "KEYSTORE_PUBKEY_SIZE_LMS",
    "KEYSTORE_PUBKEY_SIZE_XMSS",
    "KEYSTORE_PUBKEY_SIZE_ML_DSA"
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
    "RSA3072",
    "LMS",
    "XMSS",
    "ML_DSA"
};

#define MAX_PUBKEYS 64
static char *imported_pubkeys[MAX_PUBKEYS];
static int imported_pubkeys_type[MAX_PUBKEYS];
static uint32_t imported_pubkeys_id_mask[MAX_PUBKEYS];
static int n_imported = 0;

#define MAX_KEYPAIRS 64
static char *generated_keypairs[MAX_KEYPAIRS];
static int generated_keypairs_type[MAX_KEYPAIRS];
static uint32_t generated_keypairs_id_mask[MAX_KEYPAIRS];
static int n_generated = 0;

static uint32_t get_pubkey_size(uint32_t keyType)
{
    uint32_t size = 0;

    switch (keyType) {
        case KEYGEN_ED25519:
            size = KEYSTORE_PUBKEY_SIZE_ED25519;
            break;
        case KEYGEN_ED448:
            size = KEYSTORE_PUBKEY_SIZE_ED448;
            break;
        case KEYGEN_ECC256:
            size = KEYSTORE_PUBKEY_SIZE_ECC256;
            break;
        case KEYGEN_ECC384:
            size = KEYSTORE_PUBKEY_SIZE_ECC384;
            break;
        case KEYGEN_RSA2048:
            size = KEYSTORE_PUBKEY_SIZE_RSA2048;
            break;
        case KEYGEN_RSA3072:
            size = KEYSTORE_PUBKEY_SIZE_RSA3072;
            break;
        case KEYGEN_RSA4096:
            size = KEYSTORE_PUBKEY_SIZE_RSA4096;
            break;
        case KEYGEN_LMS:
            size = KEYSTORE_PUBKEY_SIZE_LMS;
            break;
        case KEYGEN_XMSS:
            size = KEYSTORE_PUBKEY_SIZE_XMSS;
            break;
#ifdef KEYSTORE_PUBKEY_SIZE_ML_DSA
        case KEYGEN_ML_DSA:
            size = KEYSTORE_PUBKEY_SIZE_ML_DSA;
            break;
#endif
        default:
            size = 0;
    }

    return size;
}

void keystore_add(uint32_t ktype, uint8_t *key, uint32_t sz, const char *keyfile,
        uint32_t id_mask)
{
    static int id_slot = 0;
    struct keystore_slot sl;
    size_t slot_size;

    fprintf(fpub, Slot_hdr,  keyfile, id_slot, KType[ktype], id_mask, sz);
    fwritekey(key, sz, fpub);
    fprintf(fpub, Pubkey_footer);
    fprintf(fpub, Slot_footer);
    printf("Associated key file:   %s\n", keyfile);
    printf("Partition ids mask:   %08x\n", id_mask);
    printf("Key type   :           %s\n", KName[ktype]);
    printf("Public key slot:       %u\n", id_slot);

    memset(&sl, 0, sizeof(sl));
    sl.slot_id = id_slot;
    sl.key_type = ktype;
    sl.part_id_mask = id_mask;

    sl.pubkey_size = get_pubkey_size(ktype);

    if (sl.pubkey_size > sizeof(sl.pubkey)){
        printf("error: %s pubkey larger than keystore: %d > %zu\n",
               KName[ktype], sl.pubkey_size, sizeof(sl.pubkey));
        exit(1);
    }

    memcpy(sl.pubkey, key, sl.pubkey_size);
#ifdef WOLFBOOT_UNIVERSAL_KEYSTORE
    slot_size = sizeof(struct keystore_slot);
#else
    slot_size = sizeof(struct keystore_slot) +  sl.pubkey_size - 
        KEYSLOT_MAX_PUBKEY_SIZE;
#endif
    fwrite(&sl, slot_size, 1, fpub_image);
    id_slot++;
}


#if !defined(NO_RSA) && defined(WOLFSSL_KEY_GEN)
static void keygen_rsa(const char *keyfile, int kbits, uint32_t id_mask)
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
        keystore_add(KEYGEN_RSA2048, pub_der, publen, keyfile, id_mask);
    else if (kbits == 3072)
        keystore_add(KEYGEN_RSA3072, pub_der, publen, keyfile, id_mask);
    else if (kbits == 4096)
        keystore_add(KEYGEN_RSA4096, pub_der, publen, keyfile, id_mask);
}
#endif

#ifdef HAVE_ECC
#define MAX_ECC_KEY_SIZE 66

static void keygen_ecc(const char *priv_fname, uint16_t ecc_key_size,
        uint32_t id_mask)
{
    int ret;
    ecc_key k;
    uint8_t Qx[MAX_ECC_KEY_SIZE], Qy[MAX_ECC_KEY_SIZE], d[MAX_ECC_KEY_SIZE];
    uint32_t qxsize = ecc_key_size,
             qysize = ecc_key_size,
             dsize =  ecc_key_size;
    uint8_t priv_der[ECC_BUFSIZE];
    int privlen;
    uint8_t k_buffer[2 * MAX_ECC_KEY_SIZE];
    FILE *fpriv;

    wc_ecc_init(&k);

    if (wc_ecc_make_key(&rng, ecc_key_size, &k) != 0) {
        fprintf(stderr, "Unable to create ecc key\n");
        exit(1);
    }

    ret = wc_EccKeyToDer(&k, priv_der, (word32)sizeof(priv_der));
    if (ret <= 0) {
        fprintf(stderr, "Unable to export private key to DER\n");
        exit(2);
    }
    privlen = ret;
    ret = 0;

    if (wc_ecc_export_private_raw(&k, Qx, &qxsize, Qy, &qysize, d, &dsize) != 0)
    {
        fprintf(stderr, "Unable to export private key to raw\n");
        exit(2);
    }

    if (wc_ecc_export_public_raw(&k, Qx, &qxsize, Qy, &qysize ) != 0)
    {
        fprintf(stderr, "Unable to export public key\n");
        exit(3);
    }

    wc_ecc_free(&k);

    fpriv = fopen(priv_fname, "wb");
    if (fpriv == NULL) {
        fprintf(stderr, "Unable to open file '%s' for writing: %s", priv_fname,
                strerror(errno));
        exit(3);
    }

    if (saveAsDer) {
        /* save file as standard ASN.1 / DER */
        fwrite(priv_der, privlen, 1, fpriv);
    }
    else {
        /* save file as RAW public X/Y and private K */
        fwrite(Qx, qxsize, 1, fpriv);
        fwrite(Qy, qysize, 1, fpriv);
        fwrite(d, dsize, 1, fpriv);
    }
    fclose(fpriv);

    memcpy(k_buffer,                Qx, ecc_key_size);
    memcpy(k_buffer + ecc_key_size, Qy, ecc_key_size);

    if (ecc_key_size == 32)
        keystore_add(KEYGEN_ECC256, k_buffer, 2 * ecc_key_size, priv_fname, id_mask);
    else if (ecc_key_size == 48)
        keystore_add(KEYGEN_ECC384, k_buffer, 2 * ecc_key_size, priv_fname, id_mask);
    else if (ecc_key_size == 66)
        keystore_add(KEYGEN_ECC521, k_buffer, 2 * ecc_key_size, priv_fname, id_mask);
}
#endif


#ifdef HAVE_ED25519
static void keygen_ed25519(const char *privkey, uint32_t id_mask)
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
    keystore_add(KEYGEN_ED25519, pub, ED25519_PUB_KEY_SIZE, privkey, id_mask);
}
#endif

#ifdef HAVE_ED448
static void keygen_ed448(const char *privkey, uint32_t id_mask)
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
    keystore_add(KEYGEN_ED448, pub, ED448_PUB_KEY_SIZE, privkey, id_mask);
}
#endif

#if defined(WOLFSSL_HAVE_LMS)
#include "../lms/lms_common.h"

static void keygen_lms(const char *priv_fname, uint32_t id_mask)
{
    FILE *  fpriv;
    LmsKey  key;
    int     ret;
    byte    lms_pub[HSS_MAX_PUBLIC_KEY_LEN];
    word32  pub_len = sizeof(lms_pub);

    ret = wc_LmsKey_Init(&key, NULL, INVALID_DEVID);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_Init returned %d\n", ret);
        exit(1);
    }

    ret = wc_LmsKey_SetParameters(&key, LMS_LEVELS, LMS_HEIGHT, LMS_WINTERNITZ);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_SetParameters(%d, %d, %d)" \
                " returned %d\n", LMS_LEVELS, LMS_HEIGHT,
                LMS_WINTERNITZ, ret);
        exit(1);
    }

    printf("info: using LMS parameters: L%d-H%d-W%d\n", LMS_LEVELS,
           LMS_HEIGHT, LMS_WINTERNITZ);

    ret = wc_LmsKey_SetWriteCb(&key, lms_write_key);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_SetWriteCb returned %d\n", ret);
        exit(1);
    }

    ret = wc_LmsKey_SetReadCb(&key, lms_read_key);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_SetReadCb returned %d\n", ret);
        exit(1);
    }

    ret = wc_LmsKey_SetContext(&key, (void *) priv_fname);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_SetContext returned %d\n", ret);
        exit(1);
    }

    ret = wc_LmsKey_MakeKey(&key, &rng);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_MakeKey returned %d\n", ret);
        exit(1);
    }

    ret = wc_LmsKey_ExportPubRaw(&key, lms_pub, &pub_len);
    if (ret != 0) {
        fprintf(stderr, "error: wc_LmsKey_ExportPubRaw returned %d\n", ret);
        exit(1);
    }

    if (pub_len != sizeof(lms_pub)) {
        fprintf(stderr, "error: wc_LmsKey_ExportPubRaw returned pub_len=%d\n" \
                        ", expected %zu\n", pub_len, sizeof(lms_pub));
        exit(1);
    }

    /* Append the public key to the private keyfile. */
    fpriv = fopen(priv_fname, "rb+");
    if (!fpriv) {
        fprintf(stderr, "error: fopen(%s, \"r+\") returned %d\n", priv_fname,
                ret);
        exit(1);
    }

    fseek(fpriv, 64, SEEK_SET);
    fwrite(lms_pub, KEYSTORE_PUBKEY_SIZE_LMS, 1, fpriv);
    fclose(fpriv);

    keystore_add(KEYGEN_LMS, lms_pub, KEYSTORE_PUBKEY_SIZE_LMS, priv_fname, id_mask);

    wc_LmsKey_Free(&key);
}
#endif /* if defined(WOLFSSL_HAVE_LMS) */

#if defined(WOLFSSL_HAVE_XMSS)
#include "../xmss/xmss_common.h"

static void keygen_xmss(const char *priv_fname, uint32_t id_mask)
{
    FILE *  fpriv;
    XmssKey key;
    int     ret;
    word32  priv_sz = 0;
    byte    xmss_pub[XMSS_SHA256_PUBLEN];
    word32  pub_len = sizeof(xmss_pub);

    ret = wc_XmssKey_Init(&key, NULL, INVALID_DEVID);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_Init returned %d\n", ret);
        exit(1);
    }

    ret = wc_XmssKey_SetParamStr(&key, WOLFBOOT_XMSS_PARAMS);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_SetParamStr(%s)" \
                " returned %d\n", WOLFBOOT_XMSS_PARAMS, ret);
        exit(1);
    }

    printf("info: using XMSS parameters: %s\n", WOLFBOOT_XMSS_PARAMS);

    ret = wc_XmssKey_SetWriteCb(&key, xmss_write_key);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_SetWriteCb returned %d\n", ret);
        exit(1);
    }

    ret = wc_XmssKey_SetReadCb(&key, xmss_read_key);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_SetReadCb returned %d\n", ret);
        exit(1);
    }

    ret = wc_XmssKey_SetContext(&key, (void *) priv_fname);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_SetContext returned %d\n", ret);
        exit(1);
    }

    /* Make the key pair. */
    ret = wc_XmssKey_MakeKey(&key, &rng);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_MakeKey returned %d\n", ret);
        exit(1);
    }

    /* Get the XMSS/XMSS^MT secret key length. */
    ret = wc_XmssKey_GetPrivLen(&key, &priv_sz);
    if (ret != 0 || priv_sz <= 0) {
        printf("error: wc_XmssKey_GetPrivLen returned %d\n",
                ret);
        exit(1);
    }

    ret = wc_XmssKey_ExportPubRaw(&key, xmss_pub, &pub_len);
    if (ret != 0) {
        fprintf(stderr, "error: wc_XmssKey_ExportPubRaw returned %d\n", ret);
        exit(1);
    }

    if (pub_len != sizeof(xmss_pub)) {
        fprintf(stderr, "error: wc_XmssKey_ExportPubRaw returned pub_len=%d\n" \
                        ", expected %zu\n", pub_len, sizeof(xmss_pub));
        exit(1);
    }

    /* Append the public key to the private keyfile. */
    fpriv = fopen(priv_fname, "rb+");
    if (!fpriv) {
        fprintf(stderr, "error: fopen(%s, \"r+\") returned %d\n", priv_fname,
                ret);
        exit(1);
    }

    fseek(fpriv, priv_sz, SEEK_SET);
    fwrite(xmss_pub, KEYSTORE_PUBKEY_SIZE_XMSS, 1, fpriv);
    fclose(fpriv);

    keystore_add(KEYGEN_XMSS, xmss_pub, KEYSTORE_PUBKEY_SIZE_XMSS, priv_fname, id_mask);

    wc_XmssKey_Free(&key);
}
#endif /* if defined(WOLFSSL_HAVE_XMSS) */

#if defined(WOLFSSL_WC_DILITHIUM)

static void keygen_ml_dsa(const char *priv_fname, uint32_t id_mask)
{
    FILE *   fpriv = NULL;
    MlDsaKey key;
    int      ret;
    byte *   priv = NULL;
    byte     pub[KEYSTORE_PUBKEY_SIZE_ML_DSA];
    word32   priv_len = 0;
    word32   pub_len = 0;
    int      ml_dsa_priv_len = 0;
    int      ml_dsa_pub_len = 0;

    ret = wc_MlDsaKey_Init(&key, NULL, INVALID_DEVID);
    if (ret != 0) {
        fprintf(stderr, "error: wc_MlDsaKey_Init returned %d\n", ret);
        exit(1);
    }

    ret = wc_MlDsaKey_SetParams(&key, ML_DSA_LEVEL);
    if (ret != 0) {
        fprintf(stderr, "error: wc_MlDsaKey_SetParams(%d) returned %d\n",
                ML_DSA_LEVEL, ret);
        exit(1);
    }

    /* Make the key pair. */
    ret = wc_MlDsaKey_MakeKey(&key, &rng);
    if (ret != 0) {
        fprintf(stderr, "error: wc_MlDsaKey_MakeKey returned %d\n", ret);
        exit(1);
    }

    /* Get the ML-DSA public key length. */
    ret = wc_MlDsaKey_GetPubLen(&key, &ml_dsa_pub_len);
    if (ret != 0 || ml_dsa_pub_len <= 0) {
        printf("error: wc_MlDsaKey_GetPrivLen returned %d\n",
                ret);
        exit(1);
    }

    /* Get the ML-DSA private key length. This API returns
     * the public + private length. */
    ret = wc_MlDsaKey_GetPrivLen(&key, &ml_dsa_priv_len);
    if (ret != 0 || ml_dsa_priv_len <= 0) {
        printf("error: wc_MlDsaKey_GetPrivLen returned %d\n",
                ret);
        exit(1);
    }

    if (ml_dsa_priv_len <= ml_dsa_pub_len) {
        printf("error: ml-dsa: unexpected key lengths: %d, %d",
               ml_dsa_priv_len, ml_dsa_pub_len);
        exit(1);
    }
    else {
        ml_dsa_priv_len -= ml_dsa_pub_len;
    }

    priv = malloc(ml_dsa_priv_len);
    if (priv == NULL) {
        fprintf(stderr, "error: malloc(%d) failed\n", ml_dsa_priv_len);
        exit(1);
    }

    /* Set the expected key lengths. */
    pub_len = (word32) ml_dsa_pub_len;
    priv_len = (word32) ml_dsa_priv_len;

    ret = wc_MlDsaKey_ExportPubRaw(&key, pub, &pub_len);
    if (ret != 0) {
        fprintf(stderr, "error: wc_MlDsaKey_ExportPubRaw returned %d\n", ret);
        exit(1);
    }

    ret = wc_MlDsaKey_ExportPrivRaw(&key, priv, &priv_len);
    if (ret != 0) {
        fprintf(stderr, "error: wc_MlDsaKey_ExportPrivRaw returned %d\n", ret);
        exit(1);
    }

    if (pub_len != sizeof(pub)) {
        fprintf(stderr, "error: wc_MlDsaKey_ExportPubRaw returned pub_len=%d, " \
                        "expected %zu\n", pub_len, sizeof(pub));
        exit(1);
    }

    if ((int) priv_len != ml_dsa_priv_len) {
        fprintf(stderr, "error: ml_dsa priv key mismatch: got %d " \
                        "bytes, expected %d\n", priv_len, ml_dsa_priv_len);
        exit(1);
    }

    fpriv = fopen(priv_fname, "wb");

    if (fpriv == NULL) {
        fprintf(stderr, "error: fopen(%s) failed: %s",
                priv_fname, strerror(errno));
        exit(1);
    }

    fwrite(priv, priv_len, 1, fpriv);
    fwrite(pub, pub_len, 1, fpriv);
    fclose(fpriv);

    keystore_add(KEYGEN_ML_DSA, pub, KEYSTORE_PUBKEY_SIZE_ML_DSA,
                 priv_fname, id_mask);

    wc_MlDsaKey_Free(&key);
    free(priv);
    priv = NULL;
}
#endif /* if defined(WOLFSSL_WC_DILITHIUM) */

static void key_gen_check(const char *kfilename)
{
    FILE *f;
    f = fopen(kfilename, "rb");
    if (!force && (f != NULL)) {
        char reply[40];
        int replySz;
        printf("** Warning: key file already exists! Are you sure you want to generate a new key and overwrite the existing key? [Type 'Yes']: ");
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
}

static void key_generate(uint32_t ktype, const char *kfilename, uint32_t id_mask)
{
    printf("Generating key (type: %s)\n", KName[ktype]);
    fflush(stdout);

    switch (ktype) {
#ifdef HAVE_ED25519
        case KEYGEN_ED25519:
            keygen_ed25519(kfilename, id_mask);
            break;
#endif

#ifdef HAVE_ED448
        case KEYGEN_ED448:
            keygen_ed448(kfilename, id_mask);
            break;
#endif

#ifdef HAVE_ECC
        case KEYGEN_ECC256:
            keygen_ecc(kfilename, 32, id_mask);
            break;
        case KEYGEN_ECC384:
            keygen_ecc(kfilename, 48, id_mask);
            break;
        case KEYGEN_ECC521:
            keygen_ecc(kfilename, 66, id_mask);
            break;
#endif

#ifndef NO_RSA
        case KEYGEN_RSA2048:
            keygen_rsa(kfilename, 2048, id_mask);
            break;
        case KEYGEN_RSA3072:
            keygen_rsa(kfilename, 3072, id_mask);
            break;
        case KEYGEN_RSA4096:
            keygen_rsa(kfilename, 4096, id_mask);
            break;
#endif

#ifdef WOLFSSL_HAVE_LMS
        case KEYGEN_LMS:
            keygen_lms(kfilename, id_mask);
            break;
#endif

#ifdef WOLFSSL_HAVE_XMSS
        case KEYGEN_XMSS:
            keygen_xmss(kfilename, id_mask);
            break;
#endif

#ifdef WOLFSSL_WC_DILITHIUM
        case KEYGEN_ML_DSA:
            keygen_ml_dsa(kfilename, id_mask);
            break;
#endif

    } /* end switch */
}

static void key_import(uint32_t ktype, const char *fname, uint32_t id_mask)
{
    int ret = 0;
    int initKey = 0;
    uint8_t buf[KEYSLOT_MAX_PUBKEY_SIZE];
    FILE* file;
    int readLen = 0;
    uint32_t keySz = 0;
    ecc_key eccKey[1];
    ed25519_key ed25519Key[1];
    ed448_key ed448Key[1];
    uint32_t keySzOut = 0;
    uint32_t qxSz = MAX_ECC_KEY_SIZE;
    uint32_t qySz = MAX_ECC_KEY_SIZE;

    file = fopen(fname, "rb");

    if (file == NULL) {
        fprintf(stderr, "Fatal error: could not open file %s to import public key\n", fname);
        exit(6);
    }

    readLen = (int)fread(buf, 1, sizeof(buf), file);

    if (readLen <= 0) {
        printf("Fatal error: could not find valid key in file %s\n", fname);
        exit(6);
    }

    fclose(file);

    /* parse the key if it has a header */
    keySz = get_pubkey_size(ktype);

    if (readLen > (int)keySz) {
        if (ktype == KEYGEN_ECC256 || ktype == KEYGEN_ECC384 ||
            ktype == KEYGEN_ECC521) {
            initKey = ret = wc_EccPublicKeyDecode(buf, &keySzOut, eccKey, readLen);

            if (ret == 0) {
                ret = wc_ecc_export_public_raw(eccKey, buf, &qxSz,
                    buf + keySz / 2, &qySz);
            }

            if (initKey == 0)
                wc_ecc_free(eccKey);
        }
        else if (ktype == KEYGEN_ED25519) {
            initKey = ret = wc_Ed25519PublicKeyDecode(buf, &keySzOut,
                ed25519Key, readLen);
            if (ret < 0)
                printf("error: wc_Ed25519PublicKeyDecode failed on %s\n", fname);

            if (ret == 0)
                ret = wc_ed25519_export_public(ed25519Key, buf, &qxSz);
            if (ret < 0)
                printf("error: wc_ed25519_export_public failed on %s\n", fname);

            if (initKey == 0)
                wc_ed25519_free(ed25519Key);
        }
        else if (ktype == KEYGEN_ED448) {
            initKey = ret = wc_Ed448PublicKeyDecode(buf, &keySzOut,
                ed448Key, readLen);

            if (ret == 0)
                ret = wc_ed448_export_public(ed448Key, buf, &qxSz);

            if (initKey == 0)
                wc_ed448_free(ed448Key);
        }

        readLen = keySz;
    }

    if (ret != 0) {
        printf("Fatal error: could not parse public key %s\n", fname);
        exit(6);
    }

    /* needs to be r - rawOffset because rsa keys are not exactly keysize */
    keystore_add(ktype, buf, readLen, fname, id_mask);
}

static uint32_t parse_id(char *idstr)
{
    uint32_t mask = 0;
    uint32_t n;
    char *p = idstr;
    char *end = NULL;
    do {
        end = strchr(p, ',');
        if (end)
            *end = 0;
        n = strtol(p, NULL, 10);
        if (n == (uint32_t)(-1))
            return 0;
        if (n > 31)
            return 0;
        mask |= (1 << n);
        if (end)
            p = end + 1;
        else
            p = NULL;
    } while (p && *p);
    return mask;
}

int main(int argc, char** argv)
{
    int i;
    int  keytype = 0;
    uint32_t n_pubkeys = 0;
    uint32_t part_id_mask = 0xFFFFFFFF; /* Default: key verify all */

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif
    printf("Keystore size: %lu\n", sizeof(struct keystore_slot));

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
#if defined(WOLFSSL_HAVE_LMS)
        else if (strcmp(argv[i], "--lms") == 0) {
            keytype = KEYGEN_LMS;
        }
#endif
#if defined(WOLFSSL_HAVE_XMSS)
        else if (strcmp(argv[i], "--xmss") == 0) {
            keytype = KEYGEN_XMSS;
        }
#endif
#if defined(WOLFSSL_WC_DILITHIUM)
        else if (strcmp(argv[i], "--ml_dsa") == 0) {
            keytype = KEYGEN_ML_DSA;
        }
#endif
        else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        }
        else if (strcmp(argv[i], "--der") == 0) {
            saveAsDer = 1;
        }
        else if (strcmp(argv[i], "-g") == 0) {
            key_gen_check(argv[i + 1]);
            i++;
            n_pubkeys++;
            generated_keypairs[n_generated] = argv[i];
            generated_keypairs_type[n_generated] = keytype;
            generated_keypairs_id_mask[n_generated] = part_id_mask;
            n_generated++;
            continue;
        }
        else if (strcmp(argv[i], "-i") == 0) {
            i++;
            n_pubkeys++;
            imported_pubkeys[n_imported] = argv[i];
            imported_pubkeys_type[n_imported] = keytype;
            imported_pubkeys_id_mask[n_imported] = part_id_mask;
            n_imported++;
            continue;
        }
        else if (strcmp(argv[i], "-keystoreDir") == 0) {
            i++;
            sprintf(pubkeyfile,"%s%s", argv[i], "/keystore.c");
            sprintf(pubkeyimg, "%s%s", argv[i], "/keystore.der");
            i++;
            continue;
        }
        else if (strcmp(argv[i], "--id") == 0) {
            i++;
            part_id_mask = parse_id(argv[i]);
            if (part_id_mask == 0) {
                fprintf(stderr, "Invalid list of partition ids.\n");
                usage(argv[0]);
            }

        }
        else {
            fprintf(stderr, "Invalid argument '%s'.", argv[i]);
            usage(argv[0]);
        }
    }
    printf("Keytype: %s\n", KName[keytype]);
    if (keytype == 0)
        exit(0);
    fpub = fopen(pubkeyfile, "rb");
    if (!force && (fpub != NULL)) {
        char reply[40];
        int replySz;
        printf("** Warning: keystore already exists! Are you sure you want to generate a new key and overwrite the existing key? [Type 'Yes']: ");
        fflush(stdout);
        replySz = scanf("%s", reply);
        printf("Reply is [%s]\n", reply);
        fclose(fpub);
        if (replySz < 0 || strcmp(reply, "Yes") != 0) {
            printf("Operation aborted by user.");
            exit(5);
        } else {
            unlink(pubkeyfile);
        }
        fpub = NULL;
    }
    fpub = fopen(pubkeyfile, "wb");
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

    for (i = 0; i < n_imported; i++) {
        key_import(imported_pubkeys_type[i], imported_pubkeys[i],
                imported_pubkeys_id_mask[i]);
    }
    for (i = 0; i < n_generated; i++) {
        key_generate(generated_keypairs_type[i], generated_keypairs[i],
                generated_keypairs_id_mask[i]);
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
