/* sign.c
 *
 * C native signing tool
 *
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
 */

/* Option to enable sign tool debugging */
/* Must also define DEBUG_WOLFSSL in user_settings.h */
//#define DEBUG_SIGNTOOL

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/asn.h>

#ifdef HAVE_CHACHA
#include <wolfssl/wolfcrypt/chacha.h>
#endif

#ifndef NO_RSA
    #include <wolfssl/wolfcrypt/rsa.h>
#endif
#ifdef HAVE_ECC
    #include <wolfssl/wolfcrypt/ecc.h>
#endif
#ifdef HAVE_ED25519
    #include <wolfssl/wolfcrypt/ed25519.h>
#endif
#ifndef NO_SHA256
    #include <wolfssl/wolfcrypt/sha256.h>
#endif
#ifdef WOLFSSL_SHA3
    #include <wolfssl/wolfcrypt/sha3.h>
#endif
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef DEBUG_SIGNTOOL
    #include <wolfssl/wolfcrypt/logging.h>
#endif

#if defined(_WIN32) && !defined(PATH_MAX)
	#define PATH_MAX 256
#endif

#define WOLFBOOT_MAGIC          0x464C4F57 /* WOLF */

#define HDR_VERSION     0x01
#define HDR_TIMESTAMP   0x02
#define HDR_PUBKEY      0x10
#define HDR_SIGNATURE   0x20
#define HDR_IMG_TYPE    0x04

#define HDR_SHA256      0x03
#define HDR_SHA3_384    0x13

#define HDR_SHA256_LEN    32
#define HDR_SHA3_384_LEN  48
#define HDR_VERSION_LEN   4
#define HDR_TIMESTAMP_LEN 8
#define HDR_IMG_TYPE_LEN  2

#define HDR_IMG_TYPE_AUTH_ED25519 0x0100
#define HDR_IMG_TYPE_AUTH_ECC256  0x0200
#define HDR_IMG_TYPE_AUTH_RSA2048 0x0300
#define HDR_IMG_TYPE_AUTH_RSA4096 0x0400
#define HDR_IMG_TYPE_WOLFBOOT     0x0000
#define HDR_IMG_TYPE_APP          0x0001

#define HASH_SHA256    HDR_SHA256
#define HASH_SHA3      HDR_SHA3_384

#define SIGN_AUTO      0
#define SIGN_ED25519   HDR_IMG_TYPE_AUTH_ED25519
#define SIGN_ECC256    HDR_IMG_TYPE_AUTH_ECC256
#define SIGN_RSA2048   HDR_IMG_TYPE_AUTH_RSA2048
#define SIGN_RSA4096   HDR_IMG_TYPE_AUTH_RSA4096

#define ENC_BLOCK_SIZE 16

static void header_append_u32(uint8_t* header, uint32_t* idx, uint32_t tmp32)
{
    memcpy(&header[*idx], &tmp32, sizeof(tmp32));
    *idx += sizeof(tmp32);
}
static void header_append_u16(uint8_t* header, uint32_t* idx, uint16_t tmp16)
{
    memcpy(&header[*idx], &tmp16, sizeof(tmp16));
    *idx += sizeof(tmp16);
}
static void header_append_tag(uint8_t* header, uint32_t* idx, uint16_t tag,
    uint16_t len, void* data)
{
    header_append_u16(header, idx, tag);
    header_append_u16(header, idx, len);
    memcpy(&header[*idx], data, len);
    *idx += len;
}

int main(int argc, char** argv)
{
    int ret = 0;
    int i;
    int self_update = 0;
    int sha_only = 0;
    int manual_sign = 0;
    int encrypt = 0;
    int hash_algo = HASH_SHA256;
    int sign = SIGN_AUTO;
    const char* image_file = NULL;
    const char* key_file = NULL;
    const char* fw_version = NULL;
    const char* signature_file = NULL;
    char output_image_file[PATH_MAX];
    char output_encrypted_image_file[PATH_MAX];
    char* tmpstr;
    char *encrypt_key_file = NULL;
    const char* sign_str = "AUTO";
    const char* hash_str = "SHA256";
    FILE *f, *f2, *fek, *fef;
    uint8_t* key_buffer = NULL;
    size_t   key_buffer_sz = 0;
    uint8_t* header = NULL;
    uint32_t header_sz = 0;
    uint32_t header_idx = 0;
    uint8_t* signature = NULL;
    uint32_t signature_sz = 0;
    uint8_t* pubkey = NULL;
    uint32_t pubkey_sz = 0;
    size_t   image_sz = 0;
    uint8_t  digest[48]; /* max digest */
    uint32_t digest_sz = 0;
    uint8_t  buf[1024];
    uint32_t idx, read_sz, pos;
    uint16_t image_type;
    uint32_t fw_version32;
    uint32_t sign_wenc = 0;
    struct stat attrib;
    union {
#ifdef HAVE_ED25519
    ed25519_key ed;
#endif
#ifdef HAVE_ECC
    ecc_key ecc;
#endif
#ifndef NO_RSA
    RsaKey rsa;
#endif
    } key;
    WC_RNG rng;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    /* Check arguments and print usage */
    if (argc < 4 || argc > 10) {
        printf("Usage: %s [--ed25519 | --ecc256 | --rsa2048 | --rsa2048enc | --rsa4096 | --rsa4096enc ] [--sha256 | --sha3] [--wolfboot-update] [--encrypt enc_key.bin] image key.der fw_version\n", argv[0]);
        printf("  - or - ");
        printf("       %s [--sha256 | --sha3] [--sha-only] [--wolfboot-update] image pub_key.der fw_version\n", argv[0]);
        printf("  - or - ");
        printf("       %s [--ed25519 | --ecc256 | --rsa2048 | --rsa4096 ] [--sha256 | --sha3] [--manual-sign] image pub_key.der fw_version signature.sig\n", argv[0]);
        return 0;
    }

    /* Parse Arguments */
    for (i=1; i<argc; i++) {
        if (strcmp(argv[i], "--ed25519") == 0) {
            sign = SIGN_ED25519;
            sign_str = "ED25519";
        }
        else if (strcmp(argv[i], "--ecc256") == 0) {
            sign = SIGN_ECC256;
            sign_str = "ECC256";
        }
        else if (strcmp(argv[i], "--rsa2048enc") == 0) {
            sign = SIGN_RSA2048;
            sign_str = "RSA2048ENC";
            sign_wenc = 1;
        }
        else if (strcmp(argv[i], "--rsa2048") == 0) {
            sign = SIGN_RSA2048;
            sign_str = "RSA2048";
        }
        else if (strcmp(argv[i], "--rsa4096enc") == 0) {
            sign = SIGN_RSA4096;
            sign_str = "RSA4096ENC";
            sign_wenc = 1;
        }
        else if (strcmp(argv[i], "--rsa4096") == 0) {
            sign = SIGN_RSA4096;
            sign_str = "RSA4096";
        }
        else if (strcmp(argv[i], "--sha256") == 0) {
            hash_algo = HASH_SHA256;
            hash_str = "SHA256";
        }
        else if (strcmp(argv[i], "--sha3") == 0) {
            hash_algo = HASH_SHA3;
            hash_str = "SHA3";
        }
        else if (strcmp(argv[i], "--wolfboot-update") == 0) {
            self_update = 1;
        }
        else if (strcmp(argv[i], "--sha-only") == 0) {
            sha_only = 1;
        }
        else if (strcmp(argv[i], "--manual-sign") == 0) {
            manual_sign = 1;
        }
        else if (strcmp(argv[i], "--encrypt") == 0) {
            encrypt = 1;
            encrypt_key_file = argv[++i];
        } else {
            i--;
            break;
        }
    }

    image_file = argv[i+1];
    key_file = argv[i+2];
    fw_version = argv[i+3];
    if (manual_sign) {
        signature_file = argv[i+4];
    }

    strncpy((char*)buf, image_file, sizeof(buf)-1);
    tmpstr = strrchr((char*)buf, '.');
    if (tmpstr) {
        *tmpstr = '\0'; /* null terminate at last "." */
    }
    snprintf(output_image_file, sizeof(output_image_file), "%s_v%s_%s.bin",
        (char*)buf, fw_version, sha_only ? "digest" : "signed");

    snprintf(output_encrypted_image_file, sizeof(output_encrypted_image_file), "%s_v%s_signed_and_encrypted.bin",
        (char*)buf, fw_version);

    printf("Update type:          %s\n", self_update ? "wolfBoot" : "Firmware");
    printf("Input image:          %s\n", image_file);
    printf("Selected cipher:      %s\n", sign_str);
    printf("Selected hash  :      %s\n", hash_str);
    printf("Public key:           %s\n", key_file);
    printf("Output %6s:        %s\n",    sha_only ? "digest" : "image", output_image_file);
    if (encrypt) {
        printf ("Encrypted output: %s\n", output_encrypted_image_file);
    }

    /* open and load key buffer */
    f = fopen(key_file, "rb");
    if (f == NULL) {
        printf("Open key file %s failed\n", key_file);
        goto exit;
    }
    fseek(f, 0, SEEK_END);
    key_buffer_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    key_buffer = malloc(key_buffer_sz);
    if (key_buffer)
        fread(key_buffer, 1, key_buffer_sz, f);
    fclose(f);
    if (key_buffer == NULL) {
        printf("Key buffer malloc error!\n");
        goto exit;
    }

    /* key type "auto" selection */
    if (key_buffer_sz == 32) {
        if ((sign != SIGN_ED25519) && !manual_sign && !sha_only ) {
            printf("Error: key too short for cipher\n");
            goto exit;
        }
        if (sign == SIGN_AUTO && (manual_sign || sha_only)) {
            printf("ed25519 public key autodetected\n");
            sign = SIGN_ED25519;
        }

    }
    else if (key_buffer_sz == 64) {
        if (sign == SIGN_ECC256) {
            if (!manual_sign && !sha_only) {
                printf("Error: key size does not match the cipher selected\n");
                goto exit;
            } else {
                printf("ECC256 public key detected\n");
            }
        }
        if (sign == SIGN_AUTO) {
            if (!manual_sign && !sha_only) {
                sign = SIGN_ED25519;
                printf("ed25519 key autodetected\n");
            } else {
                sign = SIGN_ECC256;
                printf("ecc256 public key autodetected\n");
            }
        }
    }
    else if (key_buffer_sz == 96) {
        if (sign == SIGN_ED25519) {
            printf("Error: key size does not match the cipher selected\n");
            goto exit;
        }
        if (sign == SIGN_AUTO) {
            sign = SIGN_ECC256;
            printf("ecc256 key autodetected\n");
        }
    }
    else if (key_buffer_sz > 512) {
        if (sign == SIGN_AUTO) {
            sign = SIGN_RSA4096;
            printf("rsa4096 key autodetected\n");
        }
    }
    else if (key_buffer_sz > 128) {
        if (sign == SIGN_AUTO) {
            sign = SIGN_RSA2048;
            printf("rsa2048 key autodetected\n");
        }
        if (sign != SIGN_RSA2048) {
            printf("Error: key size too large for the selected cipher\n");
            goto exit;
        }
    }
    else {
        printf("Error: key size does not match any cipher\n");
        goto exit;
    }

    /* get header and signature sizes */
    if (sign == SIGN_ED25519) {
        header_sz = 256;
        signature_sz = 64;
    }
    else if (sign == SIGN_ECC256) {
        header_sz = 256;
        signature_sz = 64;
    }
    else if (sign == SIGN_RSA2048) {
        header_sz = 512;
        signature_sz = 256;
    }
    else if (sign == SIGN_RSA4096) {
        header_sz = 1024;
        signature_sz = 512;
    }
    if (signature_sz == 0 || header_sz == 0) {
        printf("Invalid hash or signature type!\n");
        goto exit;
    }

    /* import (decode) private key for signing */
    if (!sha_only && !manual_sign) {
        /* import (decode) private key for signing */
        if (sign == SIGN_ED25519) {
        #ifdef HAVE_ED25519
            ret = wc_ed25519_init(&key.ed);
            if (ret == 0) {
                pubkey = key_buffer + ED25519_KEY_SIZE;
                pubkey_sz = ED25519_PUB_KEY_SIZE;
                ret = wc_ed25519_import_private_key(key_buffer, ED25519_KEY_SIZE, pubkey, pubkey_sz, &key.ed);
            }
        #endif
        }
        else if (sign == SIGN_ECC256) {
        #ifdef HAVE_ECC
            ret = wc_ecc_init(&key.ecc);
            if (ret == 0) {
                ret = wc_ecc_import_unsigned(&key.ecc, &key_buffer[0], &key_buffer[32],
                    &key_buffer[64], ECC_SECP256R1);
                if (ret == 0) {
                    pubkey = key_buffer; /* first 64 bytes is public portion */
                    pubkey_sz = 64;
                }
            }
        #endif
        }
        else if (sign == SIGN_RSA2048 || sign == SIGN_RSA4096) {
        #ifndef NO_RSA
            idx = 0;
            ret = wc_InitRsaKey(&key.rsa, NULL);
            if (ret == 0) {
                ret = wc_RsaPrivateKeyDecode(key_buffer, &idx, &key.rsa, key_buffer_sz);
                if (ret == 0) {
                    ret = wc_RsaKeyToPublicDer(&key.rsa, key_buffer, key_buffer_sz);
                    if (ret > 0) {
                        pubkey = key_buffer;
                        pubkey_sz = ret;
                        ret = 0;
                    }
                }
            }
        #endif
        }
        if (ret != 0) {
            printf("Error %d loading key\n", ret);
            goto exit;
        }
    }
    else {
        /* using external key to sign, so only public portion is used */
        pubkey = key_buffer;
        pubkey_sz = key_buffer_sz;
    }
#ifdef DEBUG_SIGNTOOL
    printf("Pubkey %d\n", pubkey_sz);
    WOLFSSL_BUFFER(pubkey, pubkey_sz);
#endif

    /* Get size of image */
    f = fopen(image_file, "rb");
    if (f == NULL) {
        printf("Open image file %s failed\n", image_file);
        goto exit;
    }
    fseek(f, 0, SEEK_END);
    image_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    fclose(f);

    header_idx = 0;
    header = malloc(header_sz);
    if (header == NULL) {
        printf("Header malloc error!\n");
        goto exit;
    }
    memset(header, 0xFF, header_sz);

    /* Append Magic header (spells 'WOLF') */
    header_append_u32(header, &header_idx, WOLFBOOT_MAGIC);
    /* Append Image size */
    header_append_u32(header, &header_idx, image_sz);

    /* No pad bytes, version is aligned */

    /* Append Version field */
    fw_version32 = strtol(fw_version, NULL, 10);
    header_append_tag(header, &header_idx, HDR_VERSION, HDR_VERSION_LEN,
        &fw_version32);

    /* Append Four pad bytes, so timestamp is aligned */
    header_idx += 4; /* memset 0xFF above handles value */

    /* Append Timestamp field */
    stat(image_file, &attrib);
    header_append_tag(header, &header_idx, HDR_TIMESTAMP, HDR_TIMESTAMP_LEN,
        &attrib.st_ctime);

    /* Append Image type field */
    image_type = (uint16_t)sign;
    if (!self_update)
        image_type |= HDR_IMG_TYPE_APP;
    header_append_tag(header, &header_idx, HDR_IMG_TYPE, HDR_IMG_TYPE_LEN,
        &image_type);

    /* Six pad bytes, Sha-3 requires 8-byte alignment. */
    header_idx += 6; /* memset 0xFF above handles value */

    /* Calculate hashes */
    if (hash_algo == HASH_SHA256)
    {
    #ifndef NO_SHA256
        wc_Sha256 sha;

        printf("Calculating SHA256 digest...\n");
        ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha256Update(&sha, header, header_idx);

            /* Hash image file */
            f = fopen(image_file, "rb");
            pos = 0;
            while (ret == 0 && pos < image_sz) {
                read_sz = image_sz - pos;
                if (read_sz > 32)
                    read_sz = 32;
                fread(buf, read_sz, 1, f);
                ret = wc_Sha256Update(&sha, buf, read_sz);
                pos += read_sz;
            }
            fclose(f);
            if (ret == 0)
                wc_Sha256Final(&sha, digest);
            wc_Sha256Free(&sha);
        }

        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
            if (ret == 0) {
                ret = wc_Sha256Update(&sha, pubkey, pubkey_sz);
                if (ret == 0)
                    wc_Sha256Final(&sha, buf);
                wc_Sha256Free(&sha);
            }
        }
        if (ret == 0)
            digest_sz = HDR_SHA256_LEN;
    #endif
    }
    else if (hash_algo == HASH_SHA3)
    {
    #ifdef WOLFSSL_SHA3
        wc_Sha3 sha;

        printf("Calculating SHA3 digest...\n");

        ret = wc_InitSha3_384(&sha, NULL, INVALID_DEVID);
        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha3_384_Update(&sha, header, header_idx);

            /* Hash image file */
            f = fopen(image_file, "rb");
            pos = 0;
            while (ret == 0 && pos < image_sz) {
                read_sz = image_sz - pos;
                if (read_sz > 128)
                    read_sz = 128;
                fread(buf, read_sz, 1, f);
                ret = wc_Sha3_384_Update(&sha, buf, read_sz);
                pos += read_sz;
            }
            fclose(f);
            if (ret == 0)
                ret = wc_Sha3_384_Final(&sha, digest);
            wc_Sha3_384_Free(&sha);
        }

        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha3_384(&sha, NULL, INVALID_DEVID);
            if (ret == 0) {
                ret = wc_Sha3_384_Update(&sha, pubkey, pubkey_sz);
                if (ret == 0)
                    ret = wc_Sha3_384_Final(&sha, buf);
                wc_Sha3_384_Free(&sha);
            }
        }
        if (ret == 0)
            digest_sz = HDR_SHA3_384_LEN;
    #endif
    }
    if (digest_sz == 0) {
        printf("Hash algorithm error %d\n", ret);
        goto exit;
    }
#ifdef DEBUG_SIGNTOOL
    printf("Image hash %d\n", digest_sz);
    WOLFSSL_BUFFER(digest, digest_sz);
    printf("Pubkey hash %d\n", digest_sz);
    WOLFSSL_BUFFER(buf, digest_sz);
#endif

    /* Add image hash to header */
    header_append_tag(header, &header_idx, hash_algo, digest_sz, digest);

    /* Add Pubkey Hash to header */
    header_append_tag(header, &header_idx, HDR_PUBKEY, digest_sz, buf);

    /* If hash only, then save digest and exit */
    if (sha_only) {
        f = fopen(output_image_file, "wb");
        if (f == NULL) {
            printf("Open output file %s failed\n", output_image_file);
            goto exit;
        }
        fwrite(digest, digest_sz, 1, f);
        fclose(f);
        printf("Digest image %s successfully created.\n", output_image_file);
        ret = 0;
        goto exit;
    }

    /* Sign the digest */
    ret = NOT_COMPILED_IN; /* default error */
    signature = malloc(signature_sz);
    if (signature == NULL) {
        printf("Signature malloc error!\n");
        goto exit;
    }
    memset(signature, 0, signature_sz);
    if (!manual_sign) {
        printf("Signing the firmware...\n");

        wc_InitRng(&rng);
        if (sign == SIGN_ED25519) {
        #ifdef HAVE_ED25519
            ret = wc_ed25519_sign_msg(digest, digest_sz, signature, &signature_sz, &key.ed);
        #endif
        }
        else if (sign == SIGN_ECC256) {
        #ifdef HAVE_ECC
            mp_int r, s;
            mp_init(&r); mp_init(&s);
            ret = wc_ecc_sign_hash_ex(digest, digest_sz, &rng, &key.ecc, &r, &s);
            mp_to_unsigned_bin(&r, &signature[0]);
            mp_to_unsigned_bin(&s, &signature[32]);
            mp_clear(&r); mp_clear(&s);
            wc_ecc_free(&key.ecc);
        #endif
        }
        else if (sign == SIGN_RSA2048 || sign == SIGN_RSA4096) {
        #ifndef NO_RSA
            uint32_t enchash_sz = digest_sz;
            uint8_t* enchash = digest;
            if (sign_wenc) {
                /* add ASN.1 signature encoding */
                int hashOID = 0;
                if (hash_algo == HASH_SHA256)
                    hashOID = SHA256h;
                else if (hash_algo == HASH_SHA3)
                    hashOID = SHA3_384h;
                enchash_sz = wc_EncodeSignature(buf, digest, digest_sz, hashOID);
                enchash = buf;
            }
            ret = wc_RsaSSL_Sign(enchash, enchash_sz, signature, signature_sz, 
                &key.rsa, &rng);
            wc_FreeRsaKey(&key.rsa);
            if (ret > 0) {
                signature_sz = ret;
                ret = 0;
            }
        #endif
        }
        wc_FreeRng(&rng);

        if (ret != 0) {
            printf("Signing error %d\n", ret);
            goto exit;
        }
    }
    else {
        printf("Opening signature file %s\n", signature_file);

        f = fopen(signature_file, "rb");
        if (f == NULL) {
            printf("Open signature file %s failed\n", signature_file);
            goto exit;
        }
        fread(signature, signature_sz, 1, f);
        fclose(f);
    }
#ifdef DEBUG_SIGNTOOL
    printf("Signature %d\n", signature_sz);
    WOLFSSL_BUFFER(signature, signature_sz);
#endif

    /* Add signature to header */
    header_append_tag(header, &header_idx, HDR_SIGNATURE, signature_sz, signature);

    /* Add padded header at end */
    while (header_idx < header_sz) {
        header[header_idx++] = 0xFF;
    }

    /* Create output image */
    f = fopen(output_image_file, "w+b");
    if (f == NULL) {
        printf("Open output image file %s failed\n", output_image_file);
        goto exit;
    }
    fwrite(header, header_idx, 1, f);
    /* Copy image to output */
    f2 = fopen(image_file, "rb");
    pos = 0;
    while (pos < image_sz) {
        read_sz = image_sz;
        if (read_sz > sizeof(buf))
            read_sz = sizeof(buf);
        read_sz = fread(buf, 1, read_sz, f2);
        if ((read_sz == 0) && (feof(f2)))
            break;
        fwrite(buf, 1, read_sz, f);
        pos += read_sz;
    }

    if (encrypt && encrypt_key_file) {
        uint8_t key[32], iv[12];
        uint8_t enc_buf[ENC_BLOCK_SIZE];
        uint32_t fsize = 0;
        ChaCha cha;
#ifndef HAVE_CHACHA
        fprintf(stderr, "Encryption not supported: chacha support not found in wolfssl configuration.\n");
        exit(100);
#endif
        fek = fopen(encrypt_key_file, "rb");
        if (fek == NULL) {
            fprintf(stderr, "Open encryption key file %s: %s\n", encrypt_key_file, strerror(errno));
            exit(1);
        }
        fread(key, 32, 1, fek);
        fread(iv, 12, 1, fek);
        fclose(fek);
        fef = fopen(output_encrypted_image_file, "wb");
        if (!fef) {
            fprintf(stderr, "Open encrypted output file %s: %s\n", encrypt_key_file, strerror(errno));
        }
        fsize = ftell(f);
        fseek(f, 0, SEEK_SET); /* restart the _signed file from 0 */

        wc_Chacha_SetKey(&cha, key, 32);
        for (pos = 0; pos < fsize; pos += ENC_BLOCK_SIZE) {
            int fread_retval;
            fread_retval = fread(buf, 1, ENC_BLOCK_SIZE, f);
            if ((fread_retval == 0) && feof(f)) {
                break;
            }
            wc_Chacha_SetIV(&cha, iv, (pos >> 4));
            wc_Chacha_Process(&cha, enc_buf, buf, fread_retval);
            fwrite(enc_buf, 1, fread_retval, fef);
        }
        fclose(fef);
    }
    printf("Output image(s) successfully created.\n");
    ret = 0;

    fclose(f2);
    fclose(f);

exit:
    if (header)
        free(header);
    if (key_buffer)
        free(key_buffer);
    if (signature)
        free(signature);

    return ret;
}
