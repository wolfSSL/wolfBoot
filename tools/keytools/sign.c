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
#include <delta.h>

#include "wolfboot/version.h"

#ifdef _WIN32
#include <io.h>
#define HAVE_MMAP 0
#define ftruncate(fd, len) _chsize(fd, len)
#else
#define HAVE_MMAP 1
#endif

#if HAVE_MMAP
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

#define HDR_VERSION     0x01
#define HDR_TIMESTAMP   0x02
#define HDR_PUBKEY      0x10
#define HDR_SIGNATURE   0x20
#define HDR_IMG_TYPE    0x04

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


/* Globals */
static const char wolfboot_delta_file[] = "/tmp/wolfboot-delta.bin";

static union {
#ifdef HAVE_ED25519
    ed25519_key ed[1];
#endif
#ifdef HAVE_ED448
    ed448_key ed4[1];
#endif
#ifdef HAVE_ECC
    ecc_key ecc[1];
#endif
#ifndef NO_RSA
    RsaKey rsa[1];
#endif
} key;

struct cmd_options {
    int manual_sign;
    int self_update;
    int sha_only;
    int encrypt;
    int hash_algo;
    int sign;
    int delta;
    int sign_wenc;
    const char *image_file;
    const char *key_file;
    const char *fw_version;
    const char *signature_file;
    const char *encrypt_key_file;
    const char *delta_base_file;
    char output_image_file[PATH_MAX];
    char output_diff_file[PATH_MAX];
    char output_encrypted_image_file[PATH_MAX];
    uint32_t pubkey_sz;
    uint32_t header_sz;
    uint32_t signature_sz;
    uint8_t partition_id;
};

typedef struct Partition {
    uint8_t* headerBuf;
    uint8_t* imageBuf;
    uint8_t* digestBuf;
    uint8_t* baseBuf;
    char* baseVerP;
    int headerSz;
    int imageSz;
    int digestSz;
    int baseSz;
    time_t timestamp;
} Partition;

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
    uint32_t keySzOut;
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

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_ED25519) {
                    memcpy(*pubkey, *key_buffer, KEYSTORE_PUBKEY_SIZE_ED25519);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_Ed25519PublicKeyDecode(*key_buffer,
                        &keySzOut, key.ed, *key_buffer_sz);

                    if (ret == 0) {
                        ret = wc_ed25519_export_public(key.ed, *pubkey,
                            pubkey_sz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ed25519_free(key.ed);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == ED25519_PRV_KEY_SIZE) {
                memcpy(*pubkey, *key_buffer + ED25519_KEY_SIZE,
                    KEYSTORE_PUBKEY_SIZE_ED25519);

                initRet = ret = wc_ed25519_init(key.ed);

                if (ret == 0) {
                    ret = wc_ed25519_import_private_key(*key_buffer,
                            ED25519_KEY_SIZE, *pubkey, *pubkey_sz, key.ed);
                }

                /* only free the key if we failed after allocating */
                if (ret != 0 && initRet == 0)
                    wc_ed25519_free(key.ed);
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

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == KEYSTORE_PUBKEY_SIZE_ED448) {
                    memcpy(*pubkey, *key_buffer, KEYSTORE_PUBKEY_SIZE_ED448);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_Ed448PublicKeyDecode(*key_buffer,
                        &keySzOut, key.ed4, *key_buffer_sz);

                    if (ret == 0) {
                        ret = wc_ed448_export_public(key.ed4, *pubkey,
                            pubkey_sz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ed448_free(key.ed4);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == ED448_PRV_KEY_SIZE) {
                memcpy(*pubkey, *key_buffer + ED448_KEY_SIZE,
                    ED448_PUB_KEY_SIZE);

                initRet = ret = wc_ed448_init(key.ed4);

                if (ret == 0) {
                    ret = wc_ed448_import_private_key(*key_buffer,
                        ED448_KEY_SIZE, *pubkey, *pubkey_sz, key.ed4);
                }

                /* only free the key if we failed after allocating */
                if (ret != 0 && initRet == 0)
                    wc_ed448_free(key.ed4);
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

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 64) {
                    memcpy(*pubkey, *key_buffer, 64);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_EccPublicKeyDecode(*key_buffer,
                        &keySzOut, key.ecc, *key_buffer_sz);

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc->dp->id != ECC_SECP256R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 96) {
                memcpy(*pubkey, *key_buffer, 64);

                initRet = ret = wc_ecc_init(key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(key.ecc, *key_buffer,
                        (*key_buffer) + 32, (*key_buffer) + 64,
                        ECC_SECP256R1);
                }

                if (ret != 0 && initRet == 0) {
                    wc_ecc_free(key.ecc);
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

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 96) {
                    memcpy(*pubkey, *key_buffer, 96);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_EccPublicKeyDecode(*key_buffer,
                        &keySzOut, key.ecc, *key_buffer_sz);

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc->dp->id != ECC_SECP384R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 144) {
                memcpy(*pubkey, *key_buffer, 96);

                initRet = ret = wc_ecc_init(key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(key.ecc, *key_buffer,
                        (*key_buffer) + 48, (*key_buffer) + 96,
                        ECC_SECP384R1);
                }

                if (ret != 0 && initRet == 0)
                    wc_ecc_free(key.ecc);
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

            if (CMD.manual_sign || CMD.sha_only) {
                /* raw */
                if (*key_buffer_sz == 132) {
                    memcpy(*pubkey, *key_buffer, 132);

                    ret = 0;
                }
                else {
                    initRet = ret = wc_EccPublicKeyDecode(*key_buffer,
                        &keySzOut, key.ecc, *key_buffer_sz);

                    /* we could decode another type of key in auto so check */
                    if (ret == 0 && key.ecc->dp->id != ECC_SECP521R1)
                        ret = -1;

                    if (ret == 0) {
                        ret = wc_ecc_export_public_raw(key.ecc, *pubkey, &qxSz,
                            *pubkey + *pubkey_sz / 2, &qySz);
                    }

                    /* free key no matter what */
                    if (initRet == 0)
                        wc_ecc_free(key.ecc);
                }
            }
            /* raw only */
            else if (*key_buffer_sz == 198) {
                memcpy(*pubkey, *key_buffer, 132);

                initRet = ret = wc_ecc_init(key.ecc);

                if (ret == 0) {
                    ret = wc_ecc_import_unsigned(key.ecc, *key_buffer,
                        (*key_buffer) + 66, (*key_buffer) + 132,
                        ECC_SECP521R1);
                }

                if (ret != 0 && initRet == 0)
                    wc_ecc_free(key.ecc);
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

            if (CMD.manual_sign || CMD.sha_only) {
                *pubkey = *key_buffer;
                *pubkey_sz = *key_buffer_sz;

                if (*pubkey_sz <= KEYSTORE_PUBKEY_SIZE_RSA2048) {
                    CMD.sign = SIGN_RSA2048;
                    CMD.header_sz = 512;
                    CMD.signature_sz = 256;
                }
                else if (*pubkey_sz <= KEYSTORE_PUBKEY_SIZE_RSA3072) {
                    CMD.sign = SIGN_RSA3072;

                    if(CMD.hash_algo != HASH_SHA256) {
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
                initRet = ret = wc_InitRsaKey(key.rsa, NULL);

                if (ret == 0) {
                    ret = wc_RsaPrivateKeyDecode(*key_buffer, &idx, key.rsa,
                            *key_buffer_sz);
                }

                if (ret == 0) {
                    ret = wc_RsaKeyToPublicDer(key.rsa, *key_buffer,
                            *key_buffer_sz);
                }

                if (ret > 0) {
                    *pubkey = *key_buffer;
                    *pubkey_sz = ret;
                    ret = 0;
                }

                if (ret == 0)
                    keySzOut = wc_RsaEncryptSize(key.rsa);

                if (ret != 0 && initRet == 0) {
                    wc_FreeRsaKey(key.rsa);
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

                        if(CMD.hash_algo != HASH_SHA256) {
                            CMD.header_sz = 1024;
                        }
                        else {
                            CMD.header_sz = 512;
                        }

                        CMD.signature_sz = 384;
                    }
                    else {
                        CMD.sign = SIGN_RSA2048;
                        CMD.header_sz = 512;
                        CMD.signature_sz = 256;
                    }

                    break;
                }
            }

            break;
    }

    if (ret != 0) {
        printf("Key decode error %d\n", ret);

        goto failure;
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

static int write_signed_image(Partition* partition)
{
    int i;
    int ret = -1;
    int outFd = -1;
    int encFd = -1;
    uint8_t key[ENC_MAX_KEY_SZ], iv[ENC_MAX_IV_SZ];
    int ivSz, keySz;
    uint32_t encSize;
    uint8_t encBuf[ENC_BLOCK_SIZE];
    uint8_t aesPadding[ENC_BLOCK_SIZE] = {0xff};
    Aes aes[1];
#ifdef HAVE_CHACHA
    ChaCha cha[1];
#endif

    if (partition == NULL || partition->headerBuf == NULL || partition->headerSz == 0 ||
        partition->imageBuf == NULL || partition->imageSz == 0 ||
        partition->digestBuf == NULL || partition->digestSz == 0) {
        return BAD_FUNC_ARG;
    }

    /* Create output image */
    outFd = open(CMD.output_image_file, O_CREAT | O_TRUNC | O_WRONLY, 0666);

    if (outFd < 0) {
        printf("Failed to open signed image output file!\n");
        return outFd;
    }

    if (CMD.sha_only) {
        /* just write the digest */
        ret = write(outFd, partition->digestBuf, partition->digestSz);

        if (ret == partition->digestSz)
            ret = 0;

        close(outFd);

        return ret;
    }

    /* write the header */
    ret = write(outFd, partition->headerBuf, partition->headerSz);

    if (ret == partition->headerSz)
        ret = 0;

    /* write the image */
    if (ret == 0) {
        ret = write(outFd, partition->imageBuf, partition->imageSz);

        if (ret == partition->imageSz)
            ret = 0;
    }

    if (ret == 0 && (CMD.encrypt != ENC_OFF) && CMD.encrypt_key_file) {
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
                ret = -1;

        }

        /* open encrypt key */
        if (ret == 0) {
            encFd = open(CMD.encrypt_key_file, O_RDONLY, 0666);

            if (encFd < 0)
                ret = encFd;
        }

        /* read the key */
        if (ret == 0)
            ret = read(encFd, key, keySz);

        if (ret == keySz)
            ret = 0;

        /* read the iv */
        if (ret == 0)
            ret = read(encFd, iv, ivSz);

        if (ret == ivSz)
            ret = 0;

        if (encFd > 0)
            close(encFd);

        /* open encrypted output file */
        if (ret == 0) {
            encFd = open(CMD.output_encrypted_image_file,
                O_CREAT | O_TRUNC | O_WRONLY, 0666);

            if (encFd < 0)
                ret = encFd;
        }

        if (ret == 0) {
            if (CMD.encrypt == ENC_CHACHA) {
#ifndef HAVE_CHACHA
                fprintf(stderr, "Encryption not supported: chacha support not found"
                       "in wolfssl configuration.\n");
                exit(100);
#endif
                wc_Chacha_SetKey(cha, key, sizeof(key));
                wc_Chacha_SetIV(cha, iv, 0);
            }
            else if (CMD.encrypt == ENC_AES128 ||
                CMD.encrypt == ENC_AES256) {
                wc_AesInit(aes, NULL, 0);
                wc_AesSetKeyDirect(aes, key, keySz, iv, AES_ENCRYPTION);
            }

            for (i = 0; i < partition->imageSz; i += ENC_BLOCK_SIZE) {
                /* make sure we don't exceed the buffer size */
                if (i + ENC_BLOCK_SIZE > partition->imageSz)
                    encSize = partition->imageSz - i;
                else
                    encSize = ENC_BLOCK_SIZE;

                if (CMD.encrypt == ENC_CHACHA)
                    ret = wc_Chacha_Process(cha, encBuf, partition->imageBuf + i,
                        encSize);
                else if (CMD.encrypt == ENC_AES128 ||
                    CMD.encrypt == ENC_AES256) {

                    ret = wc_AesCtrEncrypt(aes, encBuf, partition->imageBuf + i,
                        encSize);

                    /* Pad with FF if input is too short */
                    if (ret == 0 && encSize % ENC_BLOCK_SIZE != 0) {
                        ret = wc_AesCtrEncrypt(aes, encBuf + encSize,
                            aesPadding, ENC_BLOCK_SIZE - encSize);

                        encSize = ENC_BLOCK_SIZE;
                    }
                }


                if (ret == 0)
                    ret = write(encFd, encBuf, encSize);

                if (ret != (int)encSize) {
                    ret = -1;
                    break;
                }
                else {
                    ret = 0;
                }
            }
        }

        close(encFd);
    }

    if (ret == 0)
        printf("Output image(s) successfully created.\n");

    close(outFd);

    return ret;
}

/*
static int make_header_ex(int is_diff, uint8_t *pubkey, uint32_t pubkey_sz,
        const char *image_file, const char *outfile,
        uint32_t delta_base_version, uint16_t patch_len, uint32_t patch_inv_off,
        uint16_t patch_inv_len)
*/
static int make_header_ex(int is_diff, uint8_t *pubkey, uint32_t pubkeySz,
        Partition* partition, uint32_t delta_base_version, uint16_t patch_len,
        uint32_t patch_inv_off, uint16_t patch_inv_len)
{
    int ret = 0;
    uint32_t headerIdx;
    uint32_t fw_version32;
    uint16_t image_type;
    uint8_t* signature = NULL;
    uint8_t  buf[1024];
    uint8_t  digest[48]; /* max digest */
    uint32_t digestSz = 0;
    int sigFd;

    if (pubkey == NULL || pubkeySz == 0 || partition == NULL ||
        partition->imageBuf == NULL || partition->imageSz == 0) {
        printf("Invalid header input!\n");
        ret = BAD_FUNC_ARG;
    }

    if (ret == 0) {
        headerIdx = 0;
        partition->headerBuf = malloc(partition->headerSz);

        if (partition->headerBuf == NULL) {
            printf("Failed to allocate header!\n");
            ret = MEMORY_E;
        }
    }

    if (ret != 0)
        return ret;

    memset(partition->headerBuf, 0xFF, partition->headerSz);

    /* Append Magic header (spells 'WOLF') */
    header_append_u32(partition->headerBuf, &headerIdx, WOLFBOOT_MAGIC);
    /* Append Image size */
    header_append_u32(partition->headerBuf, &headerIdx, partition->imageSz);

    /* No pad bytes, version is aligned */

    /* Append Version field */
    fw_version32 = strtol(CMD.fw_version, NULL, 10);
    header_append_tag(partition->headerBuf, &headerIdx, HDR_VERSION, HDR_VERSION_LEN,
        &fw_version32);

    /* Append pad bytes, so timestamp val field is 8-byte aligned */
    while ((headerIdx % 8) != 4)
        headerIdx++;

    /* Append Timestamp field */
    header_append_tag(partition->headerBuf, &headerIdx, HDR_TIMESTAMP, HDR_TIMESTAMP_LEN,
        &partition->timestamp);

    /* Append Image type field */
    image_type = (uint16_t)CMD.sign & HDR_IMG_TYPE_AUTH_MASK;
    image_type |= CMD.partition_id;
    if (is_diff)
        image_type |= HDR_IMG_TYPE_DIFF;
    header_append_tag(partition->headerBuf, &headerIdx, HDR_IMG_TYPE, HDR_IMG_TYPE_LEN,
        &image_type);

    if (is_diff) {
        /* Append pad bytes, so fields are 4-byte aligned */
        while ((headerIdx % 4) != 0)
            headerIdx++;
        header_append_tag(partition->headerBuf, &headerIdx, HDR_IMG_DELTA_BASE, 4,
                &delta_base_version);
        header_append_tag(partition->headerBuf, &headerIdx, HDR_IMG_DELTA_SIZE, 2,
                &patch_len);

        /* Append pad bytes, so fields are 4-byte aligned */
        while ((headerIdx % 4) != 0)
            headerIdx++;
        header_append_tag(partition->headerBuf, &headerIdx, HDR_IMG_DELTA_INVERSE, 4,
                &patch_inv_off);
        header_append_tag(partition->headerBuf, &headerIdx, HDR_IMG_DELTA_INVERSE_SIZE, 2,
                &patch_inv_len);
    }

    /* Calculate hashes */
    if (CMD.hash_algo == HASH_SHA256)
    {
#ifndef NO_SHA256
        wc_Sha256 sha[1];
        printf("Calculating SHA256 digest...\n");
        ret = wc_InitSha256_ex(sha, NULL, INVALID_DEVID);

        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha256Update(sha, partition->headerBuf, headerIdx);

            /* Hash image */
            if (ret == 0)
                ret = wc_Sha256Update(sha, partition->imageBuf, partition->imageSz);

            if (ret == 0)
                ret = wc_Sha256Final(sha, digest);

            wc_Sha256Free(sha);
        }
        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha256_ex(sha, NULL, INVALID_DEVID);

            if (ret == 0) {
                ret = wc_Sha256Update(sha, pubkey, pubkeySz);

                if (ret == 0)
                    wc_Sha256Final(sha, buf);

                wc_Sha256Free(sha);
            }
        }

        if (ret == 0)
            digestSz = HDR_SHA256_LEN;
    #endif
    }
    else if (CMD.hash_algo == HASH_SHA384)
    {
    #ifndef NO_SHA384
        wc_Sha384 sha[1];
        printf("Calculating SHA384 digest...\n");
        ret = wc_InitSha384_ex(sha, NULL, INVALID_DEVID);

        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha384Update(sha, partition->headerBuf, headerIdx);

            /* Hash image */
            if (ret == 0)
                ret = wc_Sha384Update(sha, partition->imageBuf, partition->imageSz);

            if (ret == 0)
                ret = wc_Sha384Final(sha, digest);

            wc_Sha384Free(sha);
        }
        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha384_ex(sha, NULL, INVALID_DEVID);

            if (ret == 0) {
                ret = wc_Sha384Update(sha, pubkey, pubkeySz);

                if (ret == 0)
                    ret = wc_Sha384Final(sha, buf);

                wc_Sha384Free(sha);
            }
        }

        if (ret == 0)
            digestSz = HDR_SHA384_LEN;
    #endif
    }
    else if (CMD.hash_algo == HASH_SHA3)
    {
    #ifdef WOLFSSL_SHA3
        wc_Sha3 sha[1];

        printf("Calculating SHA3 digest...\n");

        ret = wc_InitSha3_384(sha, NULL, INVALID_DEVID);

        if (ret == 0) {
            /* Hash Header */
            ret = wc_Sha3_384_Update(sha, partition->headerBuf, headerIdx);

            /* Hash image */
            if (ret == 0)
                ret = wc_Sha3_384_Update(sha, partition->imageBuf, partition->imageSz);


            if (ret == 0)
                ret = wc_Sha3_384_Final(sha, digest);

            wc_Sha3_384_Free(sha);
        }

        /* pubkey hash calculation */
        if (ret == 0) {
            ret = wc_InitSha3_384(sha, NULL, INVALID_DEVID);

            if (ret == 0) {
                ret = wc_Sha3_384_Update(sha, pubkey, pubkeySz);

                if (ret == 0)
                    ret = wc_Sha3_384_Final(sha, buf);

                wc_Sha3_384_Free(sha);
            }
        }

        if (ret == 0)
            digestSz = HDR_SHA3_384_LEN;
    #endif
    }

    if (digestSz == 0) {
        printf("Hash algorithm error %d\n", ret);
        ret = -1;
    }
    else {
        partition->digestBuf = malloc(digestSz);

        if (partition->digestBuf != NULL) {
            memcpy(partition->digestBuf, digest, digestSz);
            partition->digestSz = digestSz;
        }
        else {
            ret = MEMORY_E;
        }
    }

#ifdef DEBUG_SIGNTOOL
    printf("Image hash %d\n", digestSz);
    WOLFSSL_BUFFER(digest, digestSz);
    printf("Pubkey hash %d\n", digestSz);
    WOLFSSL_BUFFER(buf, digestSz);
#endif

    /* Add image hash to header */
    if (ret == 0)
        header_append_tag(partition->headerBuf, &headerIdx, CMD.hash_algo, digestSz,
            digest);

    /* Add padding bytes. Sha-3 val field requires 8-byte alignment */
    while ((headerIdx % 8) != 4)
        headerIdx++;

    if (ret == 0 && CMD.sign != NO_SIGN) {
        WC_RNG rng[1];
        /* Add Pubkey Hash to header */
        header_append_tag(partition->headerBuf, &headerIdx, HDR_PUBKEY, digestSz, buf);

        /* If hash only, then save digest and exit */
        if (CMD.sha_only) {
            goto finish;
        }

        /* Sign the digest */
        signature = malloc(CMD.signature_sz);

        ret = signature == NULL;

        if (ret == 0) {
            memset(signature, 0, CMD.signature_sz);

            ret = NOT_COMPILED_IN; /* default error */

            if (!CMD.manual_sign) {
                printf("Signing the digest...\n");
#ifdef DEBUG_SIGTOOL
                printf("Digest %d\n", digestSz);
                WOLFSSL_BUFFER(digest, digestSz);
#endif
                wc_InitRng(rng);
                if (CMD.sign == SIGN_ED25519) {
#ifdef HAVE_ED25519
                    ret = wc_ed25519_sign_msg(digest, digestSz, signature,
                            &CMD.signature_sz, key.ed);
#endif
                }
                else if (CMD.sign == SIGN_ED448) {
#ifdef HAVE_ED448
                    ret = wc_ed448_sign_msg(digest, digestSz, signature,
                            &CMD.signature_sz, key.ed4, NULL, 0);
#endif
                }
#ifdef HAVE_ECC
                else if (CMD.sign == SIGN_ECC256) {
                    mp_int r, s;
                    mp_init(&r); mp_init(&s);
                    ret = wc_ecc_sign_hash_ex(digest, digestSz, rng, key.ecc,
                            &r, &s);
                    mp_to_unsigned_bin(&r, &signature[0]);
                    mp_to_unsigned_bin(&s, &signature[32]);
                    mp_clear(&r); mp_clear(&s);
                }
                else if (CMD.sign == SIGN_ECC384) {
                    mp_int r, s;
                    mp_init(&r); mp_init(&s);
                    ret = wc_ecc_sign_hash_ex(digest, digestSz, rng, key.ecc,
                            &r, &s);
                    mp_to_unsigned_bin(&r, &signature[0]);
                    mp_to_unsigned_bin(&s, &signature[48]);
                    mp_clear(&r); mp_clear(&s);
                }
                else if (CMD.sign == SIGN_ECC521) {
                    mp_int r, s;
                    mp_init(&r); mp_init(&s);
                    ret = wc_ecc_sign_hash_ex(digest, digestSz, rng, key.ecc,
                            &r, &s);
                    mp_to_unsigned_bin(&r, &signature[0]);
                    mp_to_unsigned_bin(&s, &signature[66]);
                    mp_clear(&r); mp_clear(&s);
                }
#endif
                else if (CMD.sign == SIGN_RSA2048 || 
                        CMD.sign == SIGN_RSA3072 ||
                        CMD.sign == SIGN_RSA4096) {

#ifndef NO_RSA
                    uint32_t enchash_sz = digestSz;
                    uint8_t* enchash = digest;
                    if (CMD.sign_wenc) {
                        /* add ASN.1 signature encoding */
                        int hashOID = 0;
                        if (CMD.hash_algo == HASH_SHA256)
                            hashOID = SHA256h;
                        else if (CMD.hash_algo == HASH_SHA3)
                            hashOID = SHA3_384h;
                        enchash_sz = wc_EncodeSignature(buf, digest, digestSz,
                                hashOID);
                        enchash = buf;
                    }
                    ret = wc_RsaSSL_Sign(enchash, enchash_sz, signature,
                            CMD.signature_sz,
                            key.rsa, rng);
                    if (ret > 0) {
                        CMD.signature_sz = ret;
                        ret = 0;
                    }
#endif
                }

                wc_FreeRng(rng);

                if (ret != 0) {
                    printf("Signing error %d\n", ret);
                }
            }
            else {
                printf("Opening signature file %s\n", CMD.signature_file);

                sigFd = open(CMD.signature_file, O_RDONLY, 0666);

                if (sigFd < 0) {
                    printf("Open signature file %s failed\n", CMD.signature_file);
                    ret = -1;
                }

                ret = read(sigFd, signature, CMD.signature_sz);

                close(sigFd);

                if (ret != (int)CMD.signature_sz) {
                    printf("Error reading file %s\n", CMD.signature_file);
                    ret = -1;
                }
            }
        }

#ifdef DEBUG_SIGNTOOL
        printf("Signature %d\n", CMD.signature_sz);
        WOLFSSL_BUFFER(signature, CMD.signature_sz);
#endif

        /* Add signature to header */
        header_append_tag(partition->headerBuf, &headerIdx, HDR_SIGNATURE, CMD.signature_sz,
                signature);
    }

    /* Add padded header at end */
    while ((int)headerIdx < partition->headerSz) {
        partition->headerBuf[headerIdx++] = 0xFF;
    }

finish:
    if (ret != 0) {
        if (partition->headerBuf)
            free(partition->headerBuf);
        if (partition->digestBuf)
            free(partition->digestBuf);
    }

    if (signature)
        free(signature);

    return ret;
}

static int make_header(uint8_t *pubkey, uint32_t pubkeySz,
    Partition* partition)
{
    return make_header_ex(0, pubkey, pubkeySz, partition, 0, 0, 0, 0);
}

static int make_header_delta(uint8_t *pubkey, uint32_t pubkeySz,
  Partition* partition, uint32_t delta_base_version, uint16_t patch_len,
  uint32_t patch_inv_off, uint16_t patch_inv_len)
{
    return make_header_ex(1, pubkey, pubkeySz, partition, delta_base_version,
        patch_len, patch_inv_off, patch_inv_len);
}

static int base_diff(uint8_t *pubkey, uint32_t pubkey_sz, int padding, Partition* partition)
{
    int fd1 = -1, fd2 = -1, fd3 = -1;
    int len1 = 0, len2 = 0, len3 = 0;
    struct stat st;
    void *base = NULL;
    void *buffer = NULL;
    uint8_t dest[WOLFBOOT_SECTOR_SIZE];
    uint8_t ff = 0xff;
    int r;
    uint32_t blksz = WOLFBOOT_SECTOR_SIZE;
    uint16_t patch_sz, patch_inv_sz;
    uint32_t patch_inv_off;
    uint32_t delta_base_version = 0;
    char *base_ver_e;
    WB_DIFF_CTX diff_ctx;
    int ret = -1;
    int io_sz;

    /* Get source file size */
    /*
    if (stat(f_base, &st) < 0) {
        printf("Cannot stat %s\n", f_base);
        goto cleanup;
    }
    len1 = st.st_size;
    */
    len1 = partition->baseSz;

    if (len1 > MAX_SRC_SIZE) {
        printf("file too large\n");
        goto cleanup;
    }

    /* Open base image */
    /*
    fd1 = open(f_base, O_RDWR);
    if (fd1 < 0) {
        printf("Cannot open file %s\n", f_base);
        goto cleanup;
    }
#if HAVE_MMAP
    base = mmap(NULL, len1, PROT_READ|PROT_WRITE, MAP_SHARED, fd1, 0);
    if (base == (void *)(-1)) {
        perror("mmap");
        goto cleanup;
    }
#else
    base = malloc(len1);
    if (base == NULL) {
        fprintf(stderr, "Error malloc for base %d\n", len1);
        goto cleanup;
    }
    if (len1 != read(fd1, base, len1)) {
        perror("read of base");
        goto cleanup;
    }
#endif
    */
    base = partition->baseBuf;

    /* Check base image version */
    if (partition->baseVerP) {
        partition->baseVerP += 2;
        base_ver_e = strchr(partition->baseVerP, '_');
        if (base_ver_e) {
            long long retval;
            retval = strtoll(partition->baseVerP, NULL, 10);
            if (retval < 0)
                delta_base_version = 0;
            else
                delta_base_version = (uint32_t)(retval&0xFFFFFFFF);
        }
    }

    if (delta_base_version == 0) {
        printf("Could not read firmware version from base file\n");
        goto cleanup;
    } else {
        printf("Delta base version: %u\n", delta_base_version);
    }

    /* Open second image file */
    fd2 = open(CMD.output_image_file, O_RDONLY, 0666);
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
#if HAVE_MMAP
    buffer = mmap(NULL, len2, PROT_READ, MAP_SHARED, fd2, 0);
    if (buffer == (void *)(-1)) {
        perror("mmap");
        goto cleanup;
    }
#else
    buffer = malloc(len2);
    if (buffer == NULL) {
        fprintf(stderr, "Error malloc for buffer %d\n", len2);
        goto cleanup;
    }
    if (len2 != read(fd2, buffer, len2)) {
        perror("fread of buffer");
        goto cleanup;
    }
#endif

    /* Open output file */
    fd3 = open(wolfboot_delta_file, O_RDWR|O_CREAT|O_TRUNC, 0660);
    if (fd3 < 0) {
        printf("Cannot open file %s for writing\n", wolfboot_delta_file);
        goto cleanup;
    }
    if (len2 <= 0) {
        goto cleanup;
    }
    lseek(fd3, MAX_SRC_SIZE -1, SEEK_SET);
    io_sz = write(fd3, &ff, 1);
    if (io_sz != 1) {
        goto cleanup;
    }
    lseek(fd3, 0, SEEK_SET);
    len3 = 0;

    /* Direct base->second patch */
    if (wb_diff_init(&diff_ctx, base, len1, buffer, len2) < 0) {
        goto cleanup;
    }
    do {
        r = wb_diff(&diff_ctx, dest, blksz);
        if (r < 0)
            goto cleanup;
        io_sz = write(fd3, dest, r);
        if (io_sz != r) {
            goto cleanup;
        }
        len3 += r;
    } while (r > 0);
    patch_sz = len3;
    while ((len3 % padding) != 0) {
        uint8_t zero = 0;
        io_sz = write(fd3, &zero, 1);
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
        io_sz = write(fd3, dest, r);
        if (io_sz != r) {
            goto cleanup;
        }
        patch_inv_sz += r;
        len3 += r;
    } while (r > 0);
    ret = ftruncate(fd3, len3);
    if (ret != 0) {
        goto cleanup;
    }
    close(fd3);
    fd3 = -1;
    printf("Successfully created output file %s\n", wolfboot_delta_file);

    /* Create delta file, with header, from the resulting patch */
    ret = make_header_delta(pubkey, pubkey_sz, partition,
            delta_base_version, patch_sz, patch_inv_off, patch_inv_sz);

cleanup:
    if (fd3 >= 0) {
        if (len3 > 0) {
            io_sz = ftruncate(fd3, len3);
            (void)io_sz; /* ignore failure */
        }
        close(fd3);
        fd3 = -1;
    }
    /* Unlink output file */
    unlink(wolfboot_delta_file);
    /* Cleanup/close */
    if (fd2 >= 0) {
        if (len2 > 0) {
#if HAVE_MMAP
            munmap(buffer, len2);
#else
            free(buffer);
#endif
        }
        close(fd2);
    }
    if (fd1 >= 0) {
        if (len1 > 0) {
#if HAVE_MMAP
            munmap(base, len1);
#else
            free(base);
#endif
        }
        close(fd1);
    }
    return ret;
}

int main(int argc, char** argv)
{
    int ret = 0;
    int i;
    char* tmpstr;
    const char* sign_str = "AUTO";
    const char* hash_str = "SHA256";
    uint8_t  buf[1024];
    uint8_t *pubkey = NULL;
    uint32_t pubkeySz = 0;
    uint8_t *kbuf=NULL, *key_buffer;
    uint32_t key_buffer_sz;
    Partition partition[1];
    int imageFd;
    int baseFd;
    struct stat attrib;

#ifdef DEBUG_SIGNTOOL
    wolfSSL_Debugging_ON();
#endif

    printf("wolfBoot KeyTools (Compiled C version)\n");
    printf("wolfBoot version %X\n", WOLFBOOT_VERSION);

    /* Check arguments and print usage */
    if (argc < 4 || argc > 12) {
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

    if (CMD.sign == NO_SIGN) {
        printf ("*** WARNING: cipher 'none' selected.\n"
                "*** Image will not be authenticated!\n"
                "*** SECURE BOOT DISABLED.\n");
    } else {
        kbuf = load_key(&key_buffer, &key_buffer_sz, &pubkey, &pubkeySz);
        if (!kbuf) {
            exit(1);
        }
    } /* CMD.sign != NO_SIGN */

    if (((CMD.sign != NO_SIGN) && (CMD.signature_sz == 0)) ||
            CMD.header_sz == 0) {
        printf("Invalid hash or signature type!\n");
        exit(2);
    }

    memset(partition, 0, sizeof(partition));
    partition->headerSz = CMD.header_sz;

    /* get the last modified time of the image */
    stat(CMD.image_file, &attrib);
    partition->timestamp = attrib.st_ctime;

    /* read in the image */
    imageFd = open(CMD.image_file, O_RDONLY, 0666);

    if (imageFd < 0)
        ret = imageFd;

    if (ret == 0) {
        partition->imageSz = lseek(imageFd, 0, SEEK_END);

        if (partition->imageSz <= 0)
            ret = -1;
    }

    if (ret == 0)
        ret = lseek(imageFd, 0, SEEK_SET);

    if (ret == 0) {
        partition->imageBuf = malloc(partition->imageSz);

        if (partition->imageBuf == NULL)
            ret = MEMORY_E;
    }

    if (ret == 0) {
        ret = read(imageFd, partition->imageBuf, partition->imageSz);

        if (ret == partition->imageSz)
            ret = 0;
    }

    if (imageFd > 0)
        close(imageFd);

    /* make the header from the image */
    if (ret == 0)
        ret = make_header(pubkey, pubkeySz, partition);

    /* write the image to file */
    if (ret == 0)
        ret = write_signed_image(partition);

    if (ret == 0 && CMD.delta) {
        /* read the base file */
        baseFd = open(CMD.delta_base_file, O_RDONLY, 0666);

        /* we should get version from tags, filename seems like a bad idea */
        partition->baseVerP = strstr(CMD.delta_base_file, "_v");

        if (baseFd < 0)
            ret = -1;

        if (ret == 0) {
            partition->baseSz = lseek(baseFd, 0, SEEK_END);

            if (partition->baseSz <= 0)
                ret = -1;
        }

        if (ret == 0)
            ret = lseek(baseFd, 0, SEEK_SET);

        if (ret == 0) {
            partition->baseBuf = malloc(partition->baseSz);

            if (partition->baseBuf == NULL)
                ret = MEMORY_E;
        }

        if (ret == 0) {
            ret = read(baseFd, partition->baseBuf, partition->baseSz);

            if (ret == partition->baseSz)
                ret = 0;
        }

        if (ret == 0) {
            if (CMD.encrypt)
                ret = base_diff(pubkey, pubkeySz, 64, partition);
            else
                ret = base_diff(pubkey, pubkeySz, 16, partition);
        }
    }

    if (ret != 0)
        printf("Signing process failed, error %d\n", ret);

    if (kbuf)
        free(kbuf);
    if (pubkey && pubkey != kbuf)
        free(pubkey);
    if (partition->headerBuf)
        free(partition->headerBuf);
    if (partition->imageBuf)
        free(partition->imageBuf);
    if (partition->digestBuf)
        free(partition->digestBuf);
    if (partition->baseBuf)
        free(partition->baseBuf);

    if (CMD.sign == SIGN_ED25519) {
#ifdef HAVE_ED25519
        wc_ed25519_free(key.ed);
#endif
    } else if (CMD.sign == SIGN_ED448) {
#ifdef HAVE_ED448
        wc_ed448_free(key.ed4);
#endif
    } else if (CMD.sign == SIGN_ECC256) {
#ifdef HAVE_ECC
        wc_ecc_free(key.ecc);
#endif
    } else if (CMD.sign == SIGN_RSA4096 || CMD.sign == SIGN_RSA4096) {
#ifndef NO_RSA
        wc_FreeRsaKey(key.rsa);
#endif
    }
    return ret;
}
