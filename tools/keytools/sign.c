/* sign.c
 *
 * C native signing tool
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

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE /* unlink */
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stddef.h>
/* target.h is a generated file based on .config (see target.h.in)
 * Provides: WOLFBOOT_SECTOR_SIZE */
#include <target.h>
#include <delta.h>

#include "wolfboot/version.h"

#ifdef _WIN32
#include <io.h>
#define HAVE_MMAP 0
#define ftruncate(fd, len) _chsize(fd, len)
static inline int fp_truncate(FILE *f, size_t len)
{
    int fd;
    if (f == NULL)
        return -1;
    fd = _fileno(f);
    return _chsize_s(fd, len);
}
#else
#define HAVE_MMAP 1
#include <sys/mman.h>
#include <unistd.h>
#endif

#define MAX_SRC_SIZE (1 << 24)

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/aes.h>

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
#ifdef HAVE_ED448
    #include <wolfssl/wolfcrypt/ed448.h>
#endif
#ifndef NO_SHA256
    #include <wolfssl/wolfcrypt/sha256.h>
#endif
#ifndef NO_SHA384
    #include <wolfssl/wolfcrypt/sha512.h>
#endif
#ifdef WOLFSSL_SHA3
    #include <wolfssl/wolfcrypt/sha3.h>
#endif
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#ifdef WOLFSSL_HAVE_LMS
    #include <wolfssl/wolfcrypt/lms.h>
    #ifdef HAVE_LIBLMS
        #include <wolfssl/wolfcrypt/ext_lms.h>
    #endif
#endif

#if defined(WOLFSSL_HAVE_XMSS)
    #include <wolfssl/wolfcrypt/xmss.h>
    #ifdef HAVE_LIBXMSS
        #include <wolfssl/wolfcrypt/ext_xmss.h>
    #endif
#endif

#ifdef DEBUG_SIGNTOOL
    #include <wolfssl/wolfcrypt/logging.h>
#endif

#include "wolfboot/wolfboot.h"

#if defined(_WIN32) && !defined(PATH_MAX)
    #define PATH_MAX 256
#endif

#ifndef IMAGE_HEADER_SIZE
    #define IMAGE_HEADER_SIZE 256
#endif

#define WOLFBOOT_MAGIC          0x464C4F57 /* WOLF */

#define HDR_VERSION           0x01
#define HDR_TIMESTAMP         0x02
#define HDR_PUBKEY            0x10
#define HDR_SIGNATURE         0x20
#define HDR_POLICY_SIGNATURE  0x21
#define HDR_IMG_TYPE          0x04

#define HDR_SHA256      0x03
#define HDR_SHA3_384    0x13
#define HDR_SHA384      0x14

#define HDR_SHA256_LEN    32
#define HDR_SHA384_LEN    48

#define HDR_SHA3_384_LEN  48
#define HDR_VERSION_LEN   4
#define HDR_TIMESTAMP_LEN 8
#define HDR_IMG_TYPE_LEN  2

#define HDR_IMG_DELTA_BASE 0x05
#define HDR_IMG_DELTA_SIZE 0x06
#define HDR_IMG_DELTA_INVERSE 0x15
#define HDR_IMG_DELTA_INVERSE_SIZE 0x16

#define HDR_IMG_TYPE_AUTH_MASK    0xFF00
#define HDR_IMG_TYPE_AUTH_NONE    0xFF00
#define HDR_IMG_TYPE_WOLFBOOT     0x0000
#define HDR_IMG_TYPE_APP          0x0001
#define HDR_IMG_TYPE_DIFF         0x00D0

#define HASH_SHA256    HDR_SHA256
#define HASH_SHA384    HDR_SHA384
#define HASH_SHA3      HDR_SHA3_384

#define SIGN_AUTO      0
#define NO_SIGN        HDR_IMG_TYPE_AUTH_NONE
#define SIGN_ED25519   HDR_IMG_TYPE_AUTH_ED25519
#define SIGN_ECC256    HDR_IMG_TYPE_AUTH_ECC256
#define SIGN_RSA2048   HDR_IMG_TYPE_AUTH_RSA2048
#define SIGN_RSA3072   HDR_IMG_TYPE_AUTH_RSA3072
#define SIGN_RSA4096   HDR_IMG_TYPE_AUTH_RSA4096
#define SIGN_ED448     HDR_IMG_TYPE_AUTH_ED448
#define SIGN_ECC384    HDR_IMG_TYPE_AUTH_ECC384
#define SIGN_ECC521    HDR_IMG_TYPE_AUTH_ECC521
#define SIGN_LMS       HDR_IMG_TYPE_AUTH_LMS
#define SIGN_XMSS      HDR_IMG_TYPE_AUTH_XMSS


#define ENC_OFF 0
#define ENC_CHACHA 1
#define ENC_AES128 2
#define ENC_AES256 3

#define ENC_BLOCK_SIZE 16
#define ENC_MAX_KEY_SZ 32
#define ENC_MAX_IV_SZ  16

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

#ifdef WOLFSSL_HAVE_LMS
#include "../lms/lms_common.h"
#endif

#ifdef WOLFSSL_HAVE_XMSS
#include "../xmss/xmss_common.h"
#endif

/* Globals */
static const char wolfboot_delta_file[] = "/tmp/wolfboot-delta.bin";

static union {
#ifdef HAVE_ED25519
    ed25519_key ed;
#endif
#ifdef HAVE_ED448
    ed448_key ed4;
#endif
#ifdef HAVE_ECC
    ecc_key ecc;
#endif
#ifndef NO_RSA
    RsaKey rsa;
#endif
#ifdef WOLFSSL_HAVE_LMS
    LmsKey lms;
#endif
#ifdef WOLFSSL_HAVE_XMSS
    XmssKey xmss;
#endif
} key;

struct cmd_options {
    int manual_sign;
    int policy_sign;
    int self_update;
    int sha_only;
    int encrypt;
    int hash_algo;
    int sign;
    int delta;
    int no_ts;
    int sign_wenc;
    const char *image_file;
    const char *key_file;
    const char *fw_version;
    const char *signature_file;
    const char *policy_file;
    const char *encrypt_key_file;
    const char *delta_base_file;
    char output_image_file[PATH_MAX];
    char output_diff_file[PATH_MAX];
    char output_encrypted_image_file[PATH_MAX];
    uint32_t pubkey_sz;
    uint32_t header_sz;
    uint32_t signature_sz;
    uint32_t policy_sz;
    uint8_t partition_id;
};

static struct cmd_options CMD = {
    .sign = SIGN_AUTO,
    .encrypt  = ENC_OFF,
    .hash_algo = HASH_SHA256,
    .header_sz = IMAGE_HEADER_SIZE,
    .partition_id = HDR_IMG_TYPE_APP
};

static uint8_t *load_key(uint8_t **key_buffer, uint32_t *key_buffer_sz,
        uint8_t **pubkey, uint32_t *pubkey_sz)
{
    int ret = -1;
    int initRet = -1;
    uint32_t idx = 0;
    int io_sz;
    FILE *f;
    uint32_t keySzOut = 0;
    uint32_t qxSz = ECC_MAXSIZE;
    uint32_t qySz = ECC_MAXSIZE;

    /* open and load key buffer */
    *key_buffer = NULL;
    f = fopen(CMD.key_file, "rb");
    if (f == NULL) {
        printf("Open key file %s failed\n", CMD.key_file);
        goto failure;
    }
    fseek(f, 0, SEEK_END);
    *key_buffer_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *key_buffer = malloc(*key_buffer_sz);
    if (*key_buffer) {
        io_sz = (int)fread(*key_buffer, 1, *key_buffer_sz, f);
        if (io_sz != (int)*key_buffer_sz) {
            printf("Key file read error!\n");
            goto failure;
        }
    }
    fclose(f);
    if (*key_buffer == NULL) {
        printf("Key buffer malloc error!\n");
        goto failure;
    }

    switch (CMD.sign) {
        /* auto, just try them all, no harm no foul */
        default:
            FALL_THROUGH;
        case SIGN_ED25519:
            ret = -1;
            initRet = -1;
            *pubkey_sz = ED25519_PUB_KEY_SIZE;
            *pubkey = malloc(*pubkey_sz);
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_ED25519) {
                    memcpy(*pubkey, *key_buffer, KEYSTORE_PUBKEY_SIZE_ED25519);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_Ed25519PublicKeyDecode(*key_buffer,
                        &keySzOut, &key.ed, *key_buffer_sz);

                    if (ret == 0) {
                        ret = wc_ed25519_export_public(&key.ed, *pubkey,
                            pubkey_sz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ed25519_free(&key.ed);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == ED25519_PRV_KEY_SIZE) {
                memcpy(*pubkey, *key_buffer + ED25519_KEY_SIZE,
                    KEYSTORE_PUBKEY_SIZE_ED25519);

                initRet = ret = wc_ed25519_init(&key.ed);

                if (ret == 0) {
                    ret = wc_ed25519_import_private_key(*key_buffer,
                            ED25519_KEY_SIZE, *pubkey, *pubkey_sz, &key.ed);
                }

                /* only free the key if we failed after allocating */
                if (ret != 0 && initRet == 0)
                    wc_ed25519_free(&key.ed);
            }

            if (ret != 0)
                free(*pubkey);

            /* break if we succeed or are not using auto */
            if (ret == 0 || CMD.sign != SIGN_AUTO) {
                CMD.header_sz = 256;
                CMD.signature_sz = 64;
                CMD.sign = SIGN_ED25519;
                printf("Found ed25519 key\n");
                break;
            }
            FALL_THROUGH;

        case SIGN_ED448:
            ret = -1;
            initRet = -1;
            *pubkey_sz = ED448_PUB_KEY_SIZE;
            *pubkey = malloc(*pubkey_sz);
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_ED448) {
                    memcpy(*pubkey, *key_buffer, KEYSTORE_PUBKEY_SIZE_ED448);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_Ed448PublicKeyDecode(*key_buffer,
                        &keySzOut, &key.ed4, *key_buffer_sz);

                    if (ret == 0) {
                        ret = wc_ed448_export_public(&key.ed4, *pubkey,
                            pubkey_sz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ed448_free(&key.ed4);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == ED448_PRV_KEY_SIZE) {
                memcpy(*pubkey, *key_buffer + ED448_KEY_SIZE,
                    ED448_PUB_KEY_SIZE);

                initRet = ret = wc_ed448_init(&key.ed4);

                if (ret == 0) {
                    ret = wc_ed448_import_private_key(*key_buffer,
                        ED448_KEY_SIZE, *pubkey, *pubkey_sz, &key.ed4);
                }

                /* only free the key if we failed after allocating */
                if (ret != 0 && initRet == 0)
                    wc_ed448_free(&key.ed4);
            }

            if (ret != 0)
                free(*pubkey);

            /* break if we succeed or are not using auto */
            if (ret == 0 || CMD.sign != SIGN_AUTO) {
                CMD.header_sz = 512;
                CMD.signature_sz = 114;
                CMD.sign = SIGN_ED448;
                break;
            }
            FALL_THROUGH;

        case SIGN_ECC256:
            ret = -1;
            initRet = -1;
            *pubkey_sz = 64;
            *pubkey = malloc(*pubkey_sz);
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 64) {
                    memcpy(*pubkey, *key_buffer, 64);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_ecc_init(&key.ecc);

                    if (ret == 0) {
                        ret = wc_EccPublicKeyDecode(*key_buffer,
                            &keySzOut, &key.ecc, *key_buffer_sz);
                    }

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc.dp->id != ECC_SECP256R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(&key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(&key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 96) {
                memcpy(*pubkey, *key_buffer, 64);

                initRet = ret = wc_ecc_init(&key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(&key.ecc, *key_buffer,
                        (*key_buffer) + 32, (*key_buffer) + 64,
                        ECC_SECP256R1);
                }

                if (ret != 0 && initRet == 0) {
                    wc_ecc_free(&key.ecc);
                }
            }

            if (ret != 0)
                free(*pubkey);

            /* break if we succeed or are not using auto */
            if (ret == 0 || CMD.sign != SIGN_AUTO) {
                CMD.sign = SIGN_ECC256;
                CMD.header_sz = 256;
                CMD.signature_sz = 64;
                break;
            }
            FALL_THROUGH;

        case SIGN_ECC384:
            ret = -1;
            initRet = -1;
            *pubkey_sz = 96;
            *pubkey = malloc(*pubkey_sz);
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 96) {
                    memcpy(*pubkey, *key_buffer, 96);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_EccPublicKeyDecode(*key_buffer,
                        &keySzOut, &key.ecc, *key_buffer_sz);

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc.dp->id != ECC_SECP384R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(&key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(&key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 144) {
                memcpy(*pubkey, *key_buffer, 96);

                initRet = ret = wc_ecc_init(&key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(&key.ecc, *key_buffer,
                        (*key_buffer) + 48, (*key_buffer) + 96,
                        ECC_SECP384R1);
                }

                if (ret != 0 && initRet == 0)
                    wc_ecc_free(&key.ecc);
            }

            if (ret != 0)
                free(*pubkey);

            /* break if we succeed or are not using auto */
            if (ret == 0 || CMD.sign != SIGN_AUTO) {
                CMD.sign = SIGN_ECC384;
                CMD.header_sz = 512;
                CMD.signature_sz = 96;
                break;
            }
            FALL_THROUGH;

        case SIGN_ECC521:
            ret = -1;
            initRet = -1;
            *pubkey_sz = 132;
            *pubkey = malloc(*pubkey_sz);
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 132) {
                    memcpy(*pubkey, *key_buffer, 132);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_EccPublicKeyDecode(*key_buffer,
                        &keySzOut, &key.ecc, *key_buffer_sz);

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc.dp->id != ECC_SECP521R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(&key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(&key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 198) {
                memcpy(*pubkey, *key_buffer, 132);

                initRet = ret = wc_ecc_init(&key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(&key.ecc, *key_buffer,
                        (*key_buffer) + 66, (*key_buffer) + 132,
                        ECC_SECP521R1);
                }

                if (ret != 0 && initRet == 0)
                    wc_ecc_free(&key.ecc);
            }

            if (ret != 0)
                free(*pubkey);

            /* break if we succeed or are not using auto */
            if (ret == 0 || CMD.sign != SIGN_AUTO) {
                CMD.sign = SIGN_ECC521;
                CMD.header_sz = 512;
                CMD.signature_sz = 132;
                break;
            }
            FALL_THROUGH;

        case SIGN_RSA2048:
            FALL_THROUGH;
        case SIGN_RSA3072:
            FALL_THROUGH;
        case SIGN_RSA4096:
            ret = -1;
            initRet = -1;
            keySzOut = 0;

            if (CMD.manual_sign || CMD.sha_only) {
                *pubkey = *key_buffer;
                *pubkey_sz = *key_buffer_sz;

                if (*pubkey_sz <= KEYSTORE_PUBKEY_SIZE_RSA2048) {
                    CMD.sign = SIGN_RSA2048;
                    if (CMD.policy_sign) {
                        CMD.header_sz = 1024;
                    }
                    else {
                        CMD.header_sz = 512;
                    }
                    CMD.signature_sz = 256;
                }
                else if (*pubkey_sz <= KEYSTORE_PUBKEY_SIZE_RSA3072) {
                    CMD.sign = SIGN_RSA3072;
                    if (CMD.hash_algo != HASH_SHA256 || CMD.policy_sign) {
                        CMD.header_sz = 1024;
                    }
                    else {
                        CMD.header_sz = 512;
                    }
                    CMD.signature_sz = 384;
                }
                else if (*pubkey_sz <= KEYSTORE_PUBKEY_SIZE_RSA4096) {
                    CMD.sign = SIGN_RSA4096;
                    CMD.header_sz = 1024;
                    CMD.signature_sz = 512;
                }

                ret = 0;
            }
            else {
                idx = 0;
                initRet = ret = wc_InitRsaKey(&key.rsa, NULL);

                if (ret == 0) {
                    ret = wc_RsaPrivateKeyDecode(*key_buffer, &idx, &key.rsa,
                            *key_buffer_sz);
                }

                if (ret == 0) {
                    ret = wc_RsaKeyToPublicDer(&key.rsa, *key_buffer,
                            *key_buffer_sz);
                }

                if (ret > 0) {
                    *pubkey = *key_buffer;
                    *pubkey_sz = ret;
                    ret = 0;
                }

                if (ret == 0)
                    keySzOut = wc_RsaEncryptSize(&key.rsa);

                if (ret != 0 && initRet == 0) {
                    wc_FreeRsaKey(&key.rsa);
                }

                /* break if we succeed or are not using auto */
                if (ret == 0 || CMD.sign != SIGN_AUTO) {
                    if (keySzOut == 512) {
                        CMD.sign = SIGN_RSA4096;
                        CMD.header_sz = 1024;
                        CMD.signature_sz = 512;
                    }
                    else if (keySzOut == 384) {
                        CMD.sign = SIGN_RSA3072;
                        if (CMD.hash_algo != HASH_SHA256 || CMD.policy_sign) {
                            CMD.header_sz = 1024;
                        }
                        else {
                            CMD.header_sz = 512;
                        }
                        CMD.signature_sz = 384;
                    }
                    else {
                        CMD.sign = SIGN_RSA2048;
                        if (CMD.policy_sign) {
                            CMD.header_sz = 1024;
                        }
                        else {
                            CMD.header_sz = 512;
                        }
                        CMD.signature_sz = 256;
                    }
                    break;
                }
            }
#ifdef WOLFSSL_HAVE_LMS
            FALL_THROUGH;
        case SIGN_LMS:
            ret = -1;

            if (CMD.sign == SIGN_AUTO) {
                /* LMS is stateful and requires additional config, and is not
                 * compatible with SIGN_AUTO. */
                printf("error: SIGN_AUTO with LMS is not supported\n");
            }
            else {
                /* The LMS file callbacks will handle writing and reading the
                 * private key. We only need to set the public key here.
                 *
                 * If both priv/pub are present:
                 *  - The first 64 bytes is the private key.
                 *  - The next 60 bytes is the public key. */

                if (*key_buffer_sz == (HSS_MAX_PRIVATE_KEY_LEN +
                    KEYSTORE_PUBKEY_SIZE_LMS)) {
                    /* priv + pub */
                    *pubkey = (*key_buffer) + HSS_MAX_PRIVATE_KEY_LEN;
                    *pubkey_sz = (*key_buffer_sz) - HSS_MAX_PRIVATE_KEY_LEN;
                    ret = 0;
                }
                else if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_LMS) {
                    /* pub only. */
                    *pubkey = (*key_buffer);
                    *pubkey_sz = KEYSTORE_PUBKEY_SIZE_LMS;
                    ret = 0;
                }
                else {
                    /* We don't recognize this as an LMS pub or private key. */
                    printf("error: unrecognized LMS key size: %d\n",
                           *key_buffer_sz );
                }
            }
#endif /* WOLFSSL_HAVE_LMS */

#ifdef WOLFSSL_HAVE_XMSS
            FALL_THROUGH;
        case SIGN_XMSS:
            ret = -1;

            if (CMD.sign == SIGN_AUTO) {
                /* XMSS is stateful and requires additional config, and is not
                 * compatible with SIGN_AUTO. */
                printf("error: SIGN_AUTO with XMSS is not supported\n");
            }
            else {
                /* The XMSS file callbacks will handle writing and reading the
                 * private key. We only need to set the public key here.
                 *
                 * If both priv/pub are present:
                 *  - The first ?? bytes is the private key.
                 *  - The next 60 bytes is the public key. */
                word32 priv_sz = 0;
                int    xmss_ret = 0;

                xmss_ret = wc_XmssKey_GetPrivLen(&key.xmss, &priv_sz);
                if (xmss_ret != 0 || priv_sz <= 0) {
                    printf("error: wc_XmssKey_GetPrivLen returned %d\n",
                            xmss_ret);
                    break;
                }

                printf("info: xmss sk len: %d\n", priv_sz);
                printf("info: xmss pk len: %d\n", KEYSTORE_PUBKEY_SIZE_XMSS);

                if (*key_buffer_sz == (priv_sz + KEYSTORE_PUBKEY_SIZE_XMSS)) {
                    /* priv + pub */
                    *pubkey = (*key_buffer) + priv_sz;
                    *pubkey_sz = (*key_buffer_sz) - priv_sz;
                    ret = 0;
                }
                else if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_XMSS) {
                    /* pub only. */
                    *pubkey = (*key_buffer);
                    *pubkey_sz = KEYSTORE_PUBKEY_SIZE_XMSS;
                    ret = 0;
                }
                else {
                    /* We don't recognize this as an XMSS pub or private key. */
                    printf("error: unrecognized XMSS key size: %d\n",
                           *key_buffer_sz );
                }
            }
#endif /* WOLFSSL_HAVE_XMSS */

            break;
    } /* end switch (CMD.sign) */

    if (ret != 0) {
        printf("Key decode error %d\n", ret);

        goto failure;
    }

    if (CMD.header_sz < IMAGE_HEADER_SIZE) {
        printf("image header size overridden by config value (%u bytes)\n", IMAGE_HEADER_SIZE);
        CMD.header_sz = IMAGE_HEADER_SIZE;
    } else {
        printf("image header size calculated at runtime (%u bytes)\n", CMD.header_sz);
    }

#ifdef DEBUG_SIGNTOOL
    printf("Pubkey %d\n", *pubkey_sz);
    WOLFSSL_BUFFER(*pubkey, *pubkey_sz);
#endif
    return *key_buffer;

failure:
    if (*key_buffer) {
        free(*key_buffer);
        *key_buffer = NULL;
    }
    return NULL;
}

/* Sign the digest */
static int sign_digest(int sign, int hash_algo,
    uint8_t* signature, uint32_t* signature_sz,
    uint8_t* digest, uint32_t digest_sz)
{
    int ret;
    WC_RNG rng;

    if ((ret = wc_InitRng(&rng)) != 0) {
        return ret;
    }

#ifdef HAVE_ED25519
    if (sign == SIGN_ED25519) {
        ret = wc_ed25519_sign_msg(digest, digest_sz, signature,
                signature_sz, &key.ed);
    }
    else
#endif
#ifdef HAVE_ED448
    if (sign == SIGN_ED448) {
        ret = wc_ed448_sign_msg(digest, digest_sz, signature,
                signature_sz, &key.ed4, NULL, 0);
    }
    else
#endif
#ifdef HAVE_ECC
    if (sign == SIGN_ECC256 ||
        sign == SIGN_ECC384 ||
        sign == SIGN_ECC521)
    {
        mp_int r, s;
        int sigSz;
        if (sign == SIGN_ECC256) sigSz = 32;
        if (sign == SIGN_ECC384) sigSz = 48;
        if (sign == SIGN_ECC521) sigSz = 66;

        mp_init(&r); mp_init(&s);
        ret = wc_ecc_sign_hash_ex(digest, digest_sz, &rng, &key.ecc,
                &r, &s);
        mp_to_unsigned_bin(&r, &signature[0]);
        mp_to_unsigned_bin(&s, &signature[sigSz]);
        mp_clear(&r); mp_clear(&s);
        *signature_sz = sigSz*2;
    }
    else
#endif
#ifndef NO_RSA
    if (sign == SIGN_RSA2048 ||
        sign == SIGN_RSA3072 ||
        sign == SIGN_RSA4096)
    {
        #ifndef WC_MAX_ENCODED_DIG_ASN_SZ
        #define WC_MAX_ENCODED_DIG_ASN_SZ 9 /* enum(bit or octet) + length(4) */
        #endif
        uint8_t  buf[WC_MAX_DIGEST_SIZE + WC_MAX_ENCODED_DIG_ASN_SZ];
        uint32_t enchash_sz = digest_sz;
        uint8_t* enchash = digest;
        if (CMD.sign_wenc) {
            /* add ASN.1 signature encoding */
            int hashOID = 0;
            if (hash_algo == HASH_SHA256)
                hashOID = SHA256h;
            else if (hash_algo == HASH_SHA384)
                hashOID = SHA384h;
            else if (hash_algo == HASH_SHA3)
                hashOID = SHA3_384h;
            enchash_sz = wc_EncodeSignature(buf, digest, digest_sz, hashOID);
            enchash = buf;
        }
        ret = wc_RsaSSL_Sign(enchash, enchash_sz, signature, *signature_sz,
                &key.rsa, &rng);
        if (ret > 0) {
            *signature_sz = ret;
            ret = 0;
        }
    }
    else
#endif
#ifdef WOLFSSL_HAVE_LMS
    if (sign == SIGN_LMS) {
        /* Set the callbacks, so LMS can update the private key while signing */
        ret = wc_LmsKey_SetWriteCb(&key.lms, lms_write_key);
        if (ret == 0) {
            ret = wc_LmsKey_SetReadCb(&key.lms, lms_read_key);
        }
        if (ret == 0) {
            ret = wc_LmsKey_SetContext(&key.lms, (void*)CMD.key_file);
        }
        if (ret == 0) {
            ret = wc_LmsKey_Reload(&key.lms);
        }
        if (ret == 0) {
            ret = wc_LmsKey_Sign(&key.lms, signature, signature_sz, digest,
                                 digest_sz);
        }
        if (ret != 0) {
            fprintf(stderr, "error signing with LMS: %d\n", ret);
        }
    }
    else
#endif /* WOLFSSL_HAVE_LMS */
#ifdef WOLFSSL_HAVE_XMSS
    if (sign == SIGN_XMSS) {
        ret = wc_XmssKey_Init(&key.xmss, NULL, INVALID_DEVID);
        /* Set the callbacks, so XMSS can update the private key while signing */
        if (ret == 0) {
            ret = wc_XmssKey_SetWriteCb(&key.xmss, xmss_write_key);
        }
        if (ret == 0) {
            ret = wc_XmssKey_SetReadCb(&key.xmss, xmss_read_key);
        }
        if (ret == 0) {
            ret = wc_XmssKey_SetContext(&key.xmss, (void*)CMD.key_file);
        }
        if (ret == 0) {
            ret = wc_XmssKey_SetParamStr(&key.xmss, XMSS_PARAMS);
        }
        if (ret == 0) {
            ret = wc_XmssKey_Reload(&key.xmss);
        }
        if (ret == 0) {
            ret = wc_XmssKey_Sign(&key.xmss, signature, signature_sz, digest,
                                 digest_sz);
        }
        if (ret != 0) {
            fprintf(stderr, "error signing with XMSS: %d\n", ret);
        }
    }
    else
#endif /* WOLFSSL_HAVE_XMSS */
    {
        ret = NOT_COMPILED_IN;
    }
    wc_FreeRng(&rng);
    return ret;
}

static int make_header_ex(int is_diff, uint8_t *pubkey, uint32_t pubkey_sz,
        const char *image_file, const char *outfile,
        uint32_t delta_base_version, uint32_t patch_len, uint32_t patch_inv_off,
        uint32_t patch_inv_len)
{
    uint32_t header_idx;
    uint8_t *header;
    FILE *f, *f2, *fek, *fef;
    uint32_t fw_version32;
    struct stat attrib;
    uint16_t image_type;
    uint8_t* signature = NULL;
    uint8_t* policy = NULL;
    int ret = -1;
    uint8_t  buf[1024];
    uint32_t read_sz, pos;
    uint8_t  digest[48]; /* max digest */
    uint32_t digest_sz = 0;
    uint32_t image_sz = 0;
    int io_sz;

    header_idx = 0;
    header = malloc(CMD.header_sz);
    if (header == NULL) {
        printf("Header malloc error!\n");
        goto failure;
    }
    memset(header, 0xFF, CMD.header_sz);

    /* Get size of image */
    f = fopen(image_file, "rb");
    if (f == NULL) {
        printf("Open image file %s failed\n", image_file);
        goto failure;
    }
    fseek(f, 0, SEEK_END);
    image_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    fclose(f);

    /* Append Magic header (spells 'WOLF') */
    header_append_u32(header, &header_idx, WOLFBOOT_MAGIC);
    /* Append Image size */
    header_append_u32(header, &header_idx, image_sz);

    /* No pad bytes, version is aligned */

    /* Append Version field */
    fw_version32 = strtol(CMD.fw_version, NULL, 10);
    header_append_tag(header, &header_idx, HDR_VERSION, HDR_VERSION_LEN,
        &fw_version32);

    /* Append pad bytes, so timestamp val field is 8-byte aligned */
    while ((header_idx % 8) != 4)
        header_idx++;

    if (!CMD.no_ts) {
        /* Append Timestamp field */
        stat(image_file, &attrib);
        header_append_tag(header, &header_idx, HDR_TIMESTAMP, HDR_TIMESTAMP_LEN,
            &attrib.st_ctime);
    }

    /* Append Image type field */
    image_type = (uint16_t)CMD.sign & HDR_IMG_TYPE_AUTH_MASK;
    image_type |= CMD.partition_id;
    if (is_diff)
        image_type |= HDR_IMG_TYPE_DIFF;
    header_append_tag(header, &header_idx, HDR_IMG_TYPE, HDR_IMG_TYPE_LEN,
        &image_type);

    if (is_diff) {
        /* Append pad bytes, so fields are 4-byte aligned */
        while ((header_idx % 4) != 0)
            header_idx++;
        header_append_tag(header, &header_idx, HDR_IMG_DELTA_BASE, 4,
                &delta_base_version);
        header_append_tag(header, &header_idx, HDR_IMG_DELTA_SIZE, 4,
                &patch_len);

        /* Append pad bytes, so fields are 4-byte aligned */
        while ((header_idx % 4) != 0)
            header_idx++;
        header_append_tag(header, &header_idx, HDR_IMG_DELTA_INVERSE, 4,
                &patch_inv_off);
        header_append_tag(header, &header_idx, HDR_IMG_DELTA_INVERSE_SIZE, 4,
                &patch_inv_len);
    }

    /* Add padding bytes. Sha-3 val field requires 8-byte alignment */
    while ((header_idx % 8) != 4)
        header_idx++;

    /* Calculate hashes */
    if (CMD.hash_algo == HASH_SHA256)
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
                io_sz = (int)fread(buf, 1, read_sz, f);
                if ((io_sz < 0) && !feof(f)) {
                    ret = -1;
                    break;
                }
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
    else if (CMD.hash_algo == HASH_SHA384)
    {
    #ifndef NO_SHA384
        wc_Sha384 sha;
        printf("Calculating SHA384 digest...\n");
        ret = wc_InitSha384_ex(&sha, NULL, INVALID_DEVID);
        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha384Update(&sha, header, header_idx);

            /* Hash image file */
            f = fopen(image_file, "rb");
            pos = 0;
            while (ret == 0 && pos < image_sz) {
                read_sz = image_sz - pos;
                if (read_sz > 32)
                    read_sz = 32;
                io_sz = (int)fread(buf, 1, read_sz, f);
                if ((io_sz < 0) && !feof(f)) {
                    ret = -1;
                    break;
                }
                ret = wc_Sha384Update(&sha, buf, read_sz);
                pos += read_sz;
            }
            fclose(f);
            if (ret == 0)
                wc_Sha384Final(&sha, digest);
            wc_Sha384Free(&sha);
        }
        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha384_ex(&sha, NULL, INVALID_DEVID);
            if (ret == 0) {
                ret = wc_Sha384Update(&sha, pubkey, pubkey_sz);
                if (ret == 0)
                    wc_Sha384Final(&sha, buf);
                wc_Sha384Free(&sha);
            }
        }
        if (ret == 0)
            digest_sz = HDR_SHA384_LEN;
    #endif
    }
    else if (CMD.hash_algo == HASH_SHA3)
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
                io_sz = (int)fread(buf, 1, read_sz, f);
                if ((io_sz < 0) && !feof(f)) {
                    ret = -1;
                    break;
                }
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
        goto failure;
    }
#ifdef DEBUG_SIGNTOOL
    printf("Image hash %d\n", digest_sz);
    WOLFSSL_BUFFER(digest, digest_sz);
    printf("Pubkey hash %d\n", digest_sz);
    WOLFSSL_BUFFER(buf, digest_sz);
#endif

    /* Add image hash to header */
    header_append_tag(header, &header_idx, CMD.hash_algo, digest_sz, digest);
    if (CMD.sign != NO_SIGN) {
        /* Add Pubkey Hash to header */
        header_append_tag(header, &header_idx, HDR_PUBKEY, digest_sz, buf);

        /* If hash only, then save digest and exit */
        if (CMD.sha_only) {
            f = fopen(outfile, "wb");
            if (f == NULL) {
                printf("Open output file %s failed\n", outfile);
                goto failure;
            }
            fwrite(digest, 1, digest_sz, f);
            fclose(f);
            printf("Digest image %s successfully created.\n", outfile);
            exit(0);
        }
        /* save max sig size */
        CMD.policy_sz = CMD.signature_sz;

        /* Signing Image */
        signature = malloc(CMD.signature_sz);
        if (signature == NULL) {
            printf("Signature malloc error!\n");
            goto failure;
        }
        memset(signature, 0, CMD.signature_sz);

        if (!CMD.manual_sign) {
            printf("Signing the digest...\n");
#ifdef DEBUG_SIGNTOOL
            printf("Digest %d\n", digest_sz);
            WOLFSSL_BUFFER(digest, digest_sz);
#endif

            /* Sign the digest */
            ret = sign_digest(CMD.sign, CMD.hash_algo,
                signature, &CMD.signature_sz, digest, digest_sz);
            if (ret != 0) {
                printf("Signing error %d\n", ret);
                goto failure;
            }
        }
        else {
            printf("Opening signature file %s\n", CMD.signature_file);

            f = fopen(CMD.signature_file, "rb");
            if (f == NULL) {
                printf("Open signature file %s failed\n", CMD.signature_file);
                goto failure;
            }
            io_sz = (int)fread(signature, 1, CMD.signature_sz, f);
            fclose(f);
            if (io_sz <= 0) {
                printf("Error reading file %s\n", CMD.signature_file);
                goto failure;
            }
            CMD.signature_sz = io_sz;
        }

        /* Signing Policy */
        if (CMD.policy_sign) {
            /* Policy is always SHA2-256 */
            digest_sz = HDR_SHA256_LEN;

            policy = malloc(CMD.policy_sz + sizeof(uint32_t));
            if (policy == NULL) {
                printf("Policy Signature malloc error!\n");
                goto failure;
            }
            memset(policy, 0, CMD.policy_sz);

            /* open policy file */
            printf("Opening policy file %s\n", CMD.policy_file);
            f = fopen(CMD.policy_file, "rb");
            if (f == NULL) {
                printf("Open policy digest file %s failed\n", CMD.policy_file);
                goto failure;
            }
            /* policy file starts with 4 byte PCR mask */
            io_sz = (int)fread(policy, 1, sizeof(uint32_t), f);
            if (io_sz != sizeof(uint32_t)) {
                printf("Error reading file %s\n", CMD.policy_file);
                fclose(f);
                goto failure;
            }

            if (!CMD.manual_sign) {
                /* in normal sign mode PCR digest (32 bytes) */
                io_sz = (int)fread(digest, 1, digest_sz, f);
                fclose(f);
                if (io_sz != (int)digest_sz) {
                    printf("Error reading file %s\n", CMD.policy_file);
                    goto failure;
                }

                printf("Signing the policy digest...\n");
#ifdef DEBUG_SIGNTOOL
                printf("Policy Digest %d\n", digest_sz);
                WOLFSSL_BUFFER(digest, digest_sz);
#endif

                /* Policy is always SHA2-256 */
                ret = sign_digest(CMD.sign, HASH_SHA256,
                    policy + sizeof(uint32_t), &CMD.policy_sz,
                    digest, digest_sz);
                if (ret != 0) {
                    printf("Signing policy error %d\n", ret);
                    goto failure;
                }
            }
            else {
                /* in manual mode remainder is PCR signature */
                io_sz = (int)fread(policy, 1, CMD.policy_sz, f);
                fclose(f);
                if (io_sz <= 0) {
                    printf("Error reading file %s\n", CMD.policy_file);
                    goto failure;
                }
                CMD.policy_sz = io_sz;
            }

            /* save copy of signed policy including 4 byte header to file */
            snprintf((char*)buf, sizeof(buf), "%s.sig", CMD.policy_file);
            printf("Saving policy signature to %s\n", (char*)buf);
            f = fopen((char*)buf, "w+b");
            if (f != NULL) {
                fwrite(policy, 1, CMD.policy_sz + sizeof(uint32_t), f);
                fclose(f);
            }
        }

#ifdef DEBUG_SIGNTOOL
        printf("Signature %d\n", CMD.signature_sz);
        WOLFSSL_BUFFER(signature, CMD.signature_sz);
        if (CMD.policy_sign) {
            printf("PCR Mask 0x%08x\n", *((uint32_t*)policy));
            printf("Policy Signature %d\n", CMD.policy_sz);
            WOLFSSL_BUFFER(policy + sizeof(uint32_t), CMD.policy_sz);
        }
#endif

        /* Add signature to header */
        header_append_tag(header, &header_idx, HDR_SIGNATURE, CMD.signature_sz,
                signature);

        if (CMD.policy_sign) {
            /* Add policy signature to header */
            header_append_tag(header, &header_idx, HDR_POLICY_SIGNATURE,
                CMD.policy_sz + sizeof(uint32_t), policy);
        }
    } /* end if(sign != NO_SIGN) */

    /* Add padded header at end */
    while (header_idx < CMD.header_sz) {
        header[header_idx++] = 0xFF;
    }

    /* Create output image */
    f = fopen(outfile, "w+b");
    if (f == NULL) {
        printf("Open output image file %s failed\n", outfile);
        goto failure;
    }
    fwrite(header, 1, header_idx, f);
    /* Copy image to output */
    f2 = fopen(image_file, "rb");
    pos = 0;
    while (pos < image_sz) {
        read_sz = image_sz;
        if (read_sz > sizeof(buf))
            read_sz = sizeof(buf);
        read_sz = (uint32_t)fread(buf, 1, read_sz, f2);
        if ((read_sz == 0) && (feof(f2)))
            break;
        fwrite(buf, 1, read_sz, f);
        pos += read_sz;
    }

    if ((CMD.encrypt != ENC_OFF) && CMD.encrypt_key_file) {
        uint8_t key[ENC_MAX_KEY_SZ], iv[ENC_MAX_IV_SZ];
        uint8_t enc_buf[ENC_BLOCK_SIZE];
        int ivSz, keySz;
        uint32_t fsize = 0;
        switch (CMD.encrypt) {
            case ENC_CHACHA:
                ivSz = CHACHA_IV_BYTES;
                keySz = CHACHA_MAX_KEY_SZ;
                break;
            case ENC_AES128:
                ivSz = 16;
                keySz = 16;
                break;
            case ENC_AES256:
                ivSz = 16;
                keySz = 32;
                break;
            default:
                printf("No valid encryption mode selected\n");
                goto failure;

        }
        fek = fopen(CMD.encrypt_key_file, "rb");
        if (fek == NULL) {
            fprintf(stderr, "Open encryption key file %s: %s\n",
                    CMD.encrypt_key_file, strerror(errno));
            exit(1);
        }
        ret = (int)fread(key, 1, keySz, fek);
        if (ret != keySz) {
            fprintf(stderr, "Error reading key from %s\n", CMD.encrypt_key_file);
            exit(1);
        }
        ret = (int)fread(iv, 1, ivSz, fek);
        if (ret != ivSz) {
            fprintf(stderr, "Error reading IV from %s\n", CMD.encrypt_key_file);
            exit(1);
        }
        fclose(fek);

        fef = fopen(CMD.output_encrypted_image_file, "wb");
        if (!fef) {
            fprintf(stderr, "Open encrypted output file %s: %s\n",
                    CMD.encrypt_key_file, strerror(errno));
        }
        fsize = ftell(f);
        fseek(f, 0, SEEK_SET); /* restart the _signed file from 0 */

        if (CMD.encrypt == ENC_CHACHA) {
            ChaCha cha;
#ifndef HAVE_CHACHA
            fprintf(stderr, "Encryption not supported: chacha support not found"
                   "in wolfssl configuration.\n");
            exit(100);
#endif
            wc_Chacha_SetKey(&cha, key, sizeof(key));
            wc_Chacha_SetIV(&cha, iv, 0);
            for (pos = 0; pos < fsize; pos += ENC_BLOCK_SIZE) {
                int fread_retval;
                fread_retval = (int)fread(buf, 1, ENC_BLOCK_SIZE, f);
                if ((fread_retval == 0) && feof(f)) {
                    break;
                }
                wc_Chacha_Process(&cha, enc_buf, buf, fread_retval);
                fwrite(enc_buf, 1, fread_retval, fef);
            }
        } else if ((CMD.encrypt == ENC_AES128) || (CMD.encrypt == ENC_AES256)) {
            Aes aes_e;
            wc_AesInit(&aes_e, NULL, 0);
            wc_AesSetKeyDirect(&aes_e, key, keySz, iv, AES_ENCRYPTION);
            for (pos = 0; pos < fsize; pos += ENC_BLOCK_SIZE) {
                int fread_retval;
                fread_retval = (int)fread(buf, 1, ENC_BLOCK_SIZE, f);
                if ((fread_retval == 0) && feof(f)) {
                    break;
                }
                /* Pad with FF if input is too short */
                while((fread_retval % ENC_BLOCK_SIZE) != 0) {
                    buf[fread_retval++] = 0xFF;
                }
                wc_AesCtrEncrypt(&aes_e, enc_buf, buf, fread_retval);
                fwrite(enc_buf, 1, fread_retval, fef);
            }
        }
        fclose(fef);
    }
    printf("Output image(s) successfully created.\n");
    ret = 0;
    fclose(f2);
    fclose(f);
failure:
    if (policy)
        free(policy);
    if (header)
        free(header);
    return ret;
}

static int make_header(uint8_t *pubkey, uint32_t pubkey_sz,
        const char *image_file, const char *outfile)
{
    return make_header_ex(0, pubkey, pubkey_sz, image_file, outfile, 0, 0, 0, 0);
}

static int make_header_delta(uint8_t *pubkey, uint32_t pubkey_sz,
        const char *image_file, const char *outfile,
        uint32_t delta_base_version, uint32_t patch_len,
        uint32_t patch_inv_off, uint32_t patch_inv_len)
{
    return make_header_ex(1, pubkey, pubkey_sz, image_file, outfile,
            delta_base_version, patch_len,
            patch_inv_off, patch_inv_len);
}

static int base_diff(const char *f_base, uint8_t *pubkey, uint32_t pubkey_sz, int padding)
{
#if HAVE_MMAP
    int fd1 = -1, fd2 = -1, fd3 = -1;
#else
    FILE *f1 = NULL, *f2 = NULL, *f3 = NULL;
#endif
    int len1 = 0, len2 = 0, len3 = 0;
    struct stat st;
    void *base = NULL;
    void *buffer = NULL;
    uint8_t dest[WOLFBOOT_SECTOR_SIZE];
    uint8_t ff = 0xff;
    int r;
    uint32_t blksz = WOLFBOOT_SECTOR_SIZE;
    uint32_t patch_sz, patch_inv_sz;
    uint32_t patch_inv_off;
    uint32_t delta_base_version = 0;
    char *base_ver_p, *base_ver_e;
    WB_DIFF_CTX diff_ctx;
    int ret = -1;
    int io_sz;

    /* Get source file size */
    if (stat(f_base, &st) < 0) {
        printf("Cannot stat %s\n", f_base);
        goto cleanup;
    }
    len1 = st.st_size;

    if (len1 > MAX_SRC_SIZE) {
        printf("%s: file too large\n", f_base);
        goto cleanup;
    }

#if HAVE_MMAP
    /* Open base image */
    fd1 = open(f_base, O_RDWR);
    if (fd1 < 0) {
        printf("Cannot open file %s\n", f_base);
        goto cleanup;
    }
    base = mmap(NULL, len1, PROT_READ|PROT_WRITE, MAP_SHARED, fd1, 0);
    if (base == (void *)(-1)) {
        perror("mmap");
        goto cleanup;
    }
#else
    f1 = fopen(f_base, "wb");
    if (f1 == NULL) {
        printf("Cannot open file %s\n", f_base);
        goto cleanup;
    }
    base = malloc(len1);
    if (base == NULL) {
        fprintf(stderr, "Error malloc for base %d\n", len1);
        goto cleanup;
    }
    if (len1 != (int)fread(base, len1, 1, f1)) {
        perror("read of base");
        goto cleanup;
    }
#endif

    /* Check base image version */
    base_ver_p = strstr(f_base, "_v");
    if (base_ver_p) {
        base_ver_p += 2;
        base_ver_e = strchr(base_ver_p, '_');
        if (base_ver_e) {
            long long retval;
            retval = strtoll(base_ver_p, NULL, 10);
            if (retval < 0)
                delta_base_version = 0;
            else
                delta_base_version = (uint32_t)(retval&0xFFFFFFFF);
        }
    }

    if (delta_base_version == 0) {
        printf("Could not read firmware version from base file %s\n", f_base);
        goto cleanup;
    } else {
        printf("Delta base version: %u\n", delta_base_version);
    }

#if HAVE_MMAP
    /* Open second image file */
    fd2 = open(CMD.output_image_file, O_RDONLY);
    if (fd2 < 0) {
        printf("Cannot open file %s\n", CMD.output_image_file);
        goto cleanup;
    }
    /* Get second file size */
    if (stat(CMD.output_image_file, &st) < 0) {
        printf("Cannot stat %s\n", CMD.output_image_file);
        goto cleanup;
    }
    len2 = st.st_size;
    buffer = mmap(NULL, len2, PROT_READ, MAP_SHARED, fd2, 0);
    if (buffer == (void *)(-1)) {
        perror("mmap");
        goto cleanup;
    }

    /* Open output file */
    fd3 = open(wolfboot_delta_file, O_RDWR|O_CREAT|O_TRUNC, 0660);
    if (fd3 < 0) {
        printf("Cannot open file %s for writing\n", wolfboot_delta_file);
        goto cleanup;
    }
    if (len2 <= 0) {
        printf("Invalid file size: %d\n", len2);
        goto cleanup;
    }
    lseek(fd3, MAX_SRC_SIZE -1, SEEK_SET);
    io_sz = write(fd3, &ff, 1);
    if (io_sz != 1) {
        printf("Could not write to output file: %s\n", strerror(errno));
        goto cleanup;
    }
    lseek(fd3, 0, SEEK_SET);
    len3 = 0;
#else
    /* Open second image file */
    f2 = fopen(CMD.output_image_file, "rb");
    if (f2 == NULL) {
        printf("Cannot open file %s\n", CMD.output_image_file);
        goto cleanup;
    }
    /* Get second file size */
    fseek(f2, 0L, SEEK_END);
    len2 = ftell(f2);
    fseek(f2, 0L, SEEK_SET);
    buffer = malloc(len2);
    if (buffer == NULL) {
        fprintf(stderr, "Error malloc for buffer %d\n", len2);
        goto cleanup;
    }
    if (len2 != (int)fread(buffer, len2, 1, f2)) {
        perror("fread of buffer");
        goto cleanup;
    }
    /* Open output file */
    f3 = fopen(wolfboot_delta_file, "wb");
    if (f3 == NULL) {
        printf("Cannot open file %s for writing\n", wolfboot_delta_file);
        goto cleanup;
    }
    if (len2 <= 0) {
        goto cleanup;
    }
    fseek(f3, MAX_SRC_SIZE -1, SEEK_SET);
    io_sz = (int)fwrite(&ff, 1, 1, f3);
    if (io_sz != 1) {
        goto cleanup;
    }
    fseek(f3, 0, SEEK_SET);
    len3 = 0;
#endif

    /* Direct base->second patch */
    if (wb_diff_init(&diff_ctx, base, len1, buffer, len2) < 0) {
        goto cleanup;
    }
    do {
        r = wb_diff(&diff_ctx, dest, blksz);
        if (r < 0)
            goto cleanup;
#if HAVE_MMAP
        io_sz = write(fd3, dest, r);
#else
        io_sz = (int)fwrite(dest, r, 1, f3);
#endif
        if (io_sz != r) {
            goto cleanup;
        }
        len3 += r;
    } while (r > 0);
    patch_sz = len3;
    while ((len3 % padding) != 0) {
        uint8_t zero = 0;
#if HAVE_MMAP
        io_sz = write(fd3, &zero, 1);
#else
        io_sz = (int)fwrite(&zero, 1, 1, f3);
#endif
        if (io_sz != 1) {
            goto cleanup;
        }
        len3++;
    }
    patch_inv_off = (uint32_t)len3 + CMD.header_sz;
    patch_inv_sz = 0;

    /* Inverse second->base patch */
    if (wb_diff_init(&diff_ctx, buffer, len2, base, len1) < 0) {
        goto cleanup;
    }
    do {
        r = wb_diff(&diff_ctx, dest, blksz);
        if (r < 0)
            goto cleanup;
#if HAVE_MMAP
        io_sz = write(fd3, dest, r);
#else
        io_sz = (int)fwrite(dest, r, 1, f3);
#endif
        if (io_sz != r) {
            goto cleanup;
        }
        patch_inv_sz += r;
        len3 += r;
    } while (r > 0);
#if HAVE_MMAP
    if (fd3 >= 0) {
        if (len3 > 0) {
            ret = ftruncate(fd3, len3);
        }
        close(fd3);
        fd3 = -1;
    }
#else
    if (f3 != NULL) {
        if (len3 > 0) {
            ret = fp_truncate(f3, len3);
        }
        fclose(f3);
        f3 = NULL;
    }
#endif


    if (ret != 0) {
        goto cleanup;
    }
    printf("Successfully created output file %s\n", wolfboot_delta_file);
    /* Create delta file, with header, from the resulting patch */

    ret = make_header_delta(pubkey, pubkey_sz, wolfboot_delta_file, CMD.output_diff_file,
            delta_base_version, patch_sz, patch_inv_off, patch_inv_sz);

cleanup:
    /* Unlink output file */
    unlink(wolfboot_delta_file);
#if HAVE_MMAP
    /* Cleanup/close */
    if (fd2 >= 0) {
        if (len2 > 0) {
            munmap(buffer, len2);
        }
        close(fd2);
    }
    if (fd1 >= 0) {
        if (len1 > 0) {
            munmap(base, len1);
        }
        close(fd1);
    }
#else
    if (f2 != NULL) {
        if (len2 > 0) {
            free(buffer);
        }
        fclose(f2);
    }
    if (f1 != NULL) {
        if (len1 > 0) {
            free(base);
        }
        fclose(f1);
    }
#endif
    return ret;
}

int main(int argc, char** argv)
{
    int ret = 0;
    int i;
    char* tmpstr;
    const char* sign_str = "AUTO";
    const char* hash_str = "SHA256";
    uint8_t  buf[PATH_MAX-32]; /* leave room to avoid "directive output may be truncated" */
    uint8_t *pubkey = NULL;
    uint32_t pubkey_sz = 0;
    uint8_t *kbuf=NULL, *key_buffer;
    uint32_t key_buffer_sz;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    printf("wolfBoot KeyTools (Compiled C version)\n");
    printf("wolfBoot version %X\n", WOLFBOOT_VERSION);

    /* Check arguments and print usage */
    if (argc < 4 || argc > 14) {
        printf("Usage: %s [options] image key version\n", argv[0]);
        printf("For full usage manual, see 'docs/Signing.md'\n");
        exit(1);
    }

    /* Parse Arguments */
    for (i=1; i<argc; i++) {
        if (strcmp(argv[i], "--no-sign") == 0) {
            CMD.sign = NO_SIGN;
            sign_str = "NONE";
        } else if (strcmp(argv[i], "--ed25519") == 0) {
            CMD.sign = SIGN_ED25519;
            sign_str = "ED25519";
        } else if (strcmp(argv[i], "--ed448") == 0) {
            CMD.sign = SIGN_ED448;
            sign_str = "ED448";
        }
        else if (strcmp(argv[i], "--ecc256") == 0) {
            CMD.sign = SIGN_ECC256;
            sign_str = "ECC256";
        }
        else if (strcmp(argv[i], "--ecc384") == 0) {
            CMD.sign = SIGN_ECC384;
            sign_str = "ECC384";
        }
        else if (strcmp(argv[i], "--ecc521") == 0) {
            CMD.sign = SIGN_ECC521;
            sign_str = "ECC521";
        }
        else if (strcmp(argv[i], "--rsa2048enc") == 0) {
            CMD.sign = SIGN_RSA2048;
            sign_str = "RSA2048ENC";
            CMD.sign_wenc = 1;
        }
        else if (strcmp(argv[i], "--rsa2048") == 0) {
            CMD.sign = SIGN_RSA2048;
            sign_str = "RSA2048";
        }
        else if (strcmp(argv[i], "--rsa3072enc") == 0) {
            CMD.sign = SIGN_RSA3072;
            sign_str = "RSA3072ENC";
            CMD.sign_wenc = 1;
        }
        else if (strcmp(argv[i], "--rsa3072") == 0) {
            CMD.sign = SIGN_RSA3072;
            sign_str = "RSA3072";
        }
        else if (strcmp(argv[i], "--rsa4096enc") == 0) {
            CMD.sign = SIGN_RSA4096;
            sign_str = "RSA4096ENC";
            CMD.sign_wenc = 1;
        }
        else if (strcmp(argv[i], "--rsa4096") == 0) {
            CMD.sign = SIGN_RSA4096;
            sign_str = "RSA4096";
        }
#ifdef WOLFSSL_HAVE_LMS
        else if (strcmp(argv[i], "--lms") == 0) {
            CMD.sign = SIGN_LMS;
            sign_str = "LMS";
        }
#endif
#ifdef WOLFSSL_HAVE_XMSS
        else if (strcmp(argv[i], "--xmss") == 0) {
            CMD.sign = SIGN_XMSS;
            sign_str = "XMSS";
        }
#endif
        else if (strcmp(argv[i], "--sha256") == 0) {
            CMD.hash_algo = HASH_SHA256;
            hash_str = "SHA256";
        }
        else if (strcmp(argv[i], "--sha384") == 0) {
            CMD.hash_algo = HASH_SHA384;
            hash_str = "SHA384";
        }
        else if (strcmp(argv[i], "--sha3") == 0) {
            CMD.hash_algo = HASH_SHA3;
            hash_str = "SHA3";
        }
        else if (strcmp(argv[i], "--wolfboot-update") == 0) {
            CMD.self_update = 1;
            CMD.partition_id = 0;
        }
        else if (strcmp(argv[i], "--id") == 0) {
            long id = strtol(argv[++i], NULL, 10);
            if ((id < 0 || id > 15) || ((id == 0) && (argv[i][0] != '0'))) {
                fprintf(stderr, "Invalid partition id: %s\n", argv[i]);
                exit(16);
            }
            CMD.partition_id = (uint8_t)id;
            if (id == 0)
                CMD.self_update = 1;
        }
        else if (strcmp(argv[i], "--sha-only") == 0) {
            CMD.sha_only = 1;
        }
        else if (strcmp(argv[i], "--manual-sign") == 0) {
            CMD.manual_sign = 1;
        }
        else if (strcmp(argv[i], "--encrypt") == 0) {
            if (CMD.encrypt == ENC_OFF)
                CMD.encrypt = ENC_CHACHA;
            CMD.encrypt_key_file = argv[++i];
        }
        else if (strcmp(argv[i], "--aes128") == 0) {
            CMD.encrypt = ENC_AES128;
        }
        else if (strcmp(argv[i], "--aes256") == 0) {
            CMD.encrypt = ENC_AES256;
        }
        else if (strcmp(argv[i], "--chacha") == 0) {
            CMD.encrypt = ENC_CHACHA;
        }
        else if (strcmp(argv[i], "--delta") == 0) {
            CMD.delta = 1;
            CMD.delta_base_file = argv[++i];
        }
        else if (strcmp(argv[i], "--no-ts") == 0) {
            CMD.no_ts = 1;
        }
        else if (strcmp(argv[i], "--policy") == 0) {
            CMD.policy_sign = 1;
            CMD.policy_file = argv[++i];
        }
        else {
            i--;
            break;
        }
    }

    if (CMD.sign != NO_SIGN) {
        CMD.image_file = argv[i+1];
        CMD.key_file = argv[i+2];
        CMD.fw_version = argv[i+3];
        if (CMD.manual_sign) {
            CMD.signature_file = argv[i+4];
        }
    } else {
        CMD.image_file = argv[i+1];
        CMD.key_file = NULL;
        CMD.fw_version = argv[i+2];
    }

    strncpy((char*)buf, CMD.image_file, sizeof(buf)-1);
    tmpstr = strrchr((char*)buf, '.');
    if (tmpstr) {
        *tmpstr = '\0'; /* null terminate at last "." */
    }
    snprintf(CMD.output_image_file, sizeof(CMD.output_image_file),
            "%s_v%s_%s.bin", (char*)buf, CMD.fw_version,
            CMD.sha_only ? "digest" : "signed");

    snprintf(CMD.output_encrypted_image_file,
            sizeof(CMD.output_encrypted_image_file),
            "%s_v%s_signed_and_encrypted.bin",
            (char*)buf, CMD.fw_version);

    printf("Update type:          %s\n",
            CMD.self_update ? "wolfBoot" : "Firmware");
    switch(CMD.encrypt) {
        case ENC_OFF:
                break;
        case ENC_CHACHA:
                printf("Encryption Algorithm: ChaCha20\n");
                break;
        case ENC_AES128:
                printf("Encryption Algorithm: AES128-CTR\n");
                break;
        case ENC_AES256:
                printf("Encryption Algorithm: AES256-CTR\n");
                break;
    }
    printf("Input image:          %s\n", CMD.image_file);
    printf("Selected cipher:      %s\n", sign_str);
    printf("Selected hash  :      %s\n", hash_str);
    if (CMD.sign != NO_SIGN) {
        printf("Public key:           %s\n", CMD.key_file);
    }
    if (CMD.delta) {
        printf("Delta Base file:      %s\n", CMD.delta_base_file);
        snprintf(CMD.output_diff_file, sizeof(CMD.output_image_file),
                "%s_v%s_signed_diff.bin",
                (char*)buf, CMD.fw_version);
        snprintf(CMD.output_encrypted_image_file,
                sizeof(CMD.output_encrypted_image_file),
                "%s_v%s_signed_diff_encrypted.bin",
                (char*)buf, CMD.fw_version);
    }
    printf("Output %6s:        %s\n",    CMD.sha_only ? "digest" : "image",
            CMD.output_image_file);
    if (CMD.encrypt) {
        printf("Encrypted output:     %s\n", CMD.output_encrypted_image_file);
    }
    printf("Target partition id : %hu ", CMD.partition_id);
    if (CMD.partition_id == HDR_IMG_TYPE_WOLFBOOT)
        printf("(bootloader)");
    printf("\n");

    /* get header and signature sizes */
    if (CMD.sign == SIGN_ED25519) {
        if (CMD.header_sz < 256)
            CMD.header_sz = 256;
        CMD.signature_sz = 64;
    }
    else if (CMD.sign == SIGN_ED448) {
        if (CMD.header_sz < 512)
            CMD.header_sz = 512;
        CMD.signature_sz = 114;
    }
    else if (CMD.sign == SIGN_ECC256) {
        if (CMD.header_sz < 256)
            CMD.header_sz = 256;
        CMD.signature_sz = 64;
    }
    else if (CMD.sign == SIGN_ECC384) {
        if (CMD.header_sz < 512)
            CMD.header_sz = 512;
        CMD.signature_sz = 96;
    }
    else if (CMD.sign == SIGN_ECC521) {
        if (CMD.header_sz < 512)
            CMD.header_sz = 512;
        CMD.signature_sz = 132;
    }
    else if (CMD.sign == SIGN_RSA2048) {
        if (CMD.header_sz < 512)
            CMD.header_sz = 512;
        CMD.signature_sz = 256;
    }
    else if (CMD.sign == SIGN_RSA3072) {
        if ((CMD.header_sz < 1024) && (CMD.hash_algo != HASH_SHA256))
            CMD.header_sz = 1024;
        if (CMD.header_sz < 512)
            CMD.header_sz = 512;
        CMD.signature_sz = 384;
    }
    else if (CMD.sign == SIGN_RSA4096) {
        if (CMD.header_sz < 1024)
            CMD.header_sz = 1024;
        CMD.signature_sz = 512;
    }
#ifdef WOLFSSL_HAVE_LMS
    else if (CMD.sign == SIGN_LMS) {
        int    lms_ret = 0;
        word32 sig_sz = 0;

        lms_ret = wc_LmsKey_Init(&key.lms, NULL, INVALID_DEVID);
        if (lms_ret != 0) {
            fprintf(stderr, "error: wc_LmsKey_Init returned %d\n", lms_ret);
            exit(1);
        }

        lms_ret = wc_LmsKey_SetParameters(&key.lms, LMS_LEVELS,
                                          LMS_HEIGHT, LMS_WINTERNITZ);
        if (lms_ret != 0) {
            fprintf(stderr, "error: wc_LmsKey_SetParameters(%d, %d, %d)" \
                    " returned %d\n", LMS_LEVELS, LMS_HEIGHT,
                    LMS_WINTERNITZ, ret);
            exit(1);
        }

        printf("info: using LMS parameters: L%d-H%d-W%d\n", LMS_LEVELS,
               LMS_HEIGHT, LMS_WINTERNITZ);

        lms_ret = wc_LmsKey_GetSigLen(&key.lms, &sig_sz);
        if (lms_ret != 0) {
            fprintf(stderr, "error: wc_LmsKey_GetSigLen returned %d\n",
                    lms_ret);
            exit(1);
        }

        printf("info: LMS signature size: %d\n", sig_sz);

        CMD.header_sz = 2 * sig_sz;
        CMD.signature_sz = sig_sz;
    }
#endif /* WOLFSSL_HAVE_LMS */
#ifdef WOLFSSL_HAVE_XMSS
    else if (CMD.sign == SIGN_XMSS) {
        int    xmss_ret = 0;
        word32 sig_sz = 0;

        xmss_ret = wc_XmssKey_Init(&key.xmss, NULL, INVALID_DEVID);
        if (xmss_ret != 0) {
            fprintf(stderr, "error: wc_XmssKey_Init returned %d\n", xmss_ret);
            exit(1);
        }

        xmss_ret = wc_XmssKey_SetParamStr(&key.xmss, XMSS_PARAMS);
        if (xmss_ret != 0) {
            fprintf(stderr, "error: wc_XmssKey_SetParamStr(%s)" \
                    " returned %d\n", XMSS_PARAMS, ret);
            exit(1);
        }

        printf("info: using XMSS parameters: %s\n", XMSS_PARAMS);

        xmss_ret = wc_XmssKey_GetSigLen(&key.xmss, &sig_sz);
        if (xmss_ret != 0) {
            fprintf(stderr, "error: wc_XmssKey_GetSigLen returned %d\n",
                    xmss_ret);
            exit(1);
        }

        printf("info: XMSS signature size: %d\n", sig_sz);

        CMD.header_sz = 2 * sig_sz;
        CMD.signature_sz = sig_sz;
    }
#endif /* WOLFSSL_HAVE_XMSS */

    if (((CMD.sign != NO_SIGN) && (CMD.signature_sz == 0)) ||
            CMD.header_sz == 0) {
        printf("Invalid hash or signature type!\n");
        exit(2);
    }

    if (CMD.sign == NO_SIGN) {
        printf ("*** WARNING: cipher 'none' selected.\n"
                "*** Image will not be authenticated!\n"
                "*** SECURE BOOT DISABLED.\n");
    } else {
        kbuf = load_key(&key_buffer, &key_buffer_sz, &pubkey, &pubkey_sz);
        if (!kbuf) {
            exit(1);
        }
    } /* CMD.sign != NO_SIGN */
    make_header(pubkey, pubkey_sz, CMD.image_file, CMD.output_image_file);

    if (CMD.delta) {
        if (CMD.encrypt)
            ret = base_diff(CMD.delta_base_file, pubkey, pubkey_sz, 64);
        else
            ret = base_diff(CMD.delta_base_file, pubkey, pubkey_sz, 16);
    }


    if (kbuf)
        free(kbuf);
    if (CMD.sign == SIGN_ED25519) {
#ifdef HAVE_ED25519
        wc_ed25519_free(&key.ed);
#endif
    } else if (CMD.sign == SIGN_ED448) {
#ifdef HAVE_ED448
        wc_ed448_free(&key.ed4);
#endif
    } else if (CMD.sign == SIGN_ECC256) {
#ifdef HAVE_ECC
        wc_ecc_free(&key.ecc);
#endif
    } else if (CMD.sign == SIGN_RSA4096 || CMD.sign == SIGN_RSA4096) {
#ifndef NO_RSA
        wc_FreeRsaKey(&key.rsa);
#endif
    } else if (CMD.sign == SIGN_LMS) {
#ifdef WOLFSSL_HAVE_LMS
        wc_LmsKey_Free(&key.lms);
#endif
    }
    return ret;
}
