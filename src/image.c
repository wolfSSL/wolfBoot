/* image.c
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
/**
 * @file image.c
 * @brief This file contains functions related to image handling and
 * verification.
 */
#ifndef IMAGE_H_
#define IMAGE_H_
#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_drv.h"
#include <stddef.h>
#include "printf.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <string.h>

#include "image.h"

#ifdef WOLFBOOT_TPM
#include <stdlib.h>
#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_wrap.h"
#include "wolftpm/tpm2_tis.h" /* for TIS header size and wait state */
static WOLFTPM2_DEV     wolftpm_dev;
#ifdef WOLFBOOT_TPM_KEYSTORE
static WOLFTPM2_SESSION wolftpm_session;
static WOLFTPM2_KEY     wolftpm_srk;
#endif
#endif /* WOLFBOOT_TPM */

#if defined(WOLFBOOT_TPM_KEYSTORE) && !defined(WOLFBOOT_TPM)
#error For TPM keystore please make sure WOLFBOOT_TPM is also defined
#endif

/* Globals */
static uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];

/* Forward declarations */
/**
 * @brief Find the key slot ID based on the SHA hash of the key.
 *
 * @param hint The SHA hash to find the key slot ID for.
 * @return The key slot ID corresponding to the provided SHA hash.
 */
static int keyslot_id_by_sha(const uint8_t *hint);

#ifdef WOLFBOOT_SIGN_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>
static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret, res;
    ed25519_key ed;
    ret = wc_ed25519_init(&ed);
    if (ret < 0) {
        /* Failed to initialize key */
        return;
    }
    ret = wc_ed25519_import_public(keystore_get_buffer(key_slot),
            KEYSTORE_PUBKEY_SIZE, &ed);
    if (ret < 0) {
        /* Failed to import ed25519 key */
        return;
    }
    VERIFY_FN(img, &res, wc_ed25519_verify_msg, sig, IMAGE_SIGNATURE_SIZE,
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &res, &ed);
}

#endif /* WOLFBOOT_SIGN_ED25519 */

#ifdef WOLFBOOT_SIGN_ED448
#include <wolfssl/wolfcrypt/ed448.h>
static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret, res;
    ed448_key ed;
    ret = wc_ed448_init(&ed);
    if (ret < 0) {
        /* Failed to initialize key */
        return;
    }
    ret = wc_ed448_import_public(keystore_get_buffer(key_slot),
            KEYSTORE_PUBKEY_SIZE, &ed);
    if (ret < 0) {
        /* Failed to import ed448 key */
        return;
    }
    VERIFY_FN(img, &res, wc_ed448_verify_msg, sig, IMAGE_SIGNATURE_SIZE,
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &res, &ed, NULL, 0);
}


#endif


#if defined(WOLFBOOT_SIGN_ECC256) || \
    defined(WOLFBOOT_SIGN_ECC384) || \
    defined(WOLFBOOT_SIGN_ECC521)

#include <wolfssl/wolfcrypt/ecc.h>

#if defined(WOLFBOOT_SIGN_ECC256)
    #define ECC_KEY_TYPE ECC_SECP256R1
#elif defined(WOLFBOOT_SIGN_ECC384)
    #define ECC_KEY_TYPE ECC_SECP384R1
#elif defined(WOLFBOOT_SIGN_ECC521)
    #define ECC_KEY_TYPE ECC_SECP521R1
#endif

/**
 * @brief Verify the signature of the image using the provided key slot
 * and signature.
 *
 * @param key_slot The key slot ID to use for verification.
 * @param img The image to verify.
 * @param sig The signature to use for verification.
 */
static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret, verify_res = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    int point_sz = pubkey_sz/2;
#ifdef WOLFBOOT_TPM
    WOLFTPM2_KEY tpmKey;
#else
    ecc_key ecc;
    mp_int r, s;
#endif

    if (pubkey == NULL || pubkey_sz <= 0) {
        return;
    }

#ifdef WOLFBOOT_TPM
    /* Use TPM for ECC verify */
    /* Load public key into TPM */
    memset(&tpmKey, 0, sizeof(tpmKey));
    ret = wolfTPM2_LoadEccPublicKey(&wolftpm_dev, &tpmKey,
        TPM2_GetTpmCurve(ECC_KEY_TYPE), /* Curve */
        pubkey, point_sz,               /* Public X */
        pubkey + point_sz, point_sz     /* Public Y */
    );
    if (ret == 0) {
        ret = wolfTPM2_VerifyHashScheme(&wolftpm_dev, &tpmKey,
            sig, IMAGE_SIGNATURE_SIZE,               /* Signature */
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, /* Hash */
            TPM_ALG_ECDSA, WOLFBOOT_TPM_HASH_ALG);
    }
    /* unload handle regardless of result */
    wolfTPM2_UnloadHandle(&wolftpm_dev, &tpmKey.handle);

    if (ret == 0) {
        verify_res = 1; /* TPM does hash verify compare */

        if ((~(uint32_t)ret == 0xFFFFFFFF) && (verify_res == 1) &&
            (~(uint32_t)verify_res == 0xFFFFFFFE)) {
            wolfBoot_image_confirm_signature_ok(img);
        }
    }
    else {
        wolfBoot_printf("TPM ECC verify error %d (%s)\n",
            ret, wolfTPM2_GetRCString(ret));
    }
#else
    /* wolfCrypt software ECC verify */
    ret = wc_ecc_init(&ecc);
    if (ret == 0) {
        /* Import public key */
        ret = wc_ecc_import_unsigned(&ecc, pubkey,
            (byte*)(pubkey + point_sz), NULL, ECC_KEY_TYPE);
        if (ret == 0 && ecc.type == ECC_PUBLICKEY) {
            /* Import signature into r,s */
            mp_init(&r);
            mp_init(&s);
            mp_read_unsigned_bin(&r, sig, point_sz);
            mp_read_unsigned_bin(&s, sig + point_sz, point_sz);
            VERIFY_FN(img, &verify_res, wc_ecc_verify_hash_ex, &r, &s,
                img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res, &ecc);
        }
        wc_ecc_free(&ecc);
    }
#endif /* WOLFBOOT_TPM */
}

#endif /* WOLFBOOT_SIGN_ECC256 */


#if defined(WOLFBOOT_SIGN_RSA2048) || \
    defined(WOLFBOOT_SIGN_RSA3072) || \
    defined(WOLFBOOT_SIGN_RSA4096)

#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/rsa.h>

#if defined(WOLFBOOT_SIGN_RSA4096) && \
    (defined(USE_FAST_MATH) && \
     !defined(WOLFSSL_SMALL_STACK) && !defined(WOLFBOOT_HUGE_STACK))
    #error "TFM will allocate 70+ KB in the stack with this configuration." \
           "If this is OK, please compile with WOLFBOOT_HUGE_STACK=1"
#endif

#ifndef NO_RSA_SIG_ENCODING /* option to reduce code size */
static inline int DecodeAsn1Tag(const uint8_t* input, int inputSz, int* inOutIdx,
    int* tag_len, uint8_t tag)
{
    if (input[*inOutIdx] != tag) {
        return -1;
    }
    (*inOutIdx)++;
    *tag_len = input[*inOutIdx];
    (*inOutIdx)++;
    if (*tag_len + *inOutIdx > inputSz) {
        return -1;
    }
    return 0;
}
static int RsaDecodeSignature(uint8_t** pInput, int inputSz)
{
    uint8_t* input = *pInput;
    int idx = 0;
    int digest_len = 0, algo_len, tot_len;

    /* sequence - total size */
    if (DecodeAsn1Tag(input, inputSz, &idx, &tot_len,
            ASN_SEQUENCE | ASN_CONSTRUCTED) != 0) {
        return -1;
    }

    /* sequence - algoid */
    if (DecodeAsn1Tag(input, inputSz, &idx, &algo_len,
            ASN_SEQUENCE | ASN_CONSTRUCTED) != 0) {
        return -1;
    }
    idx += algo_len; /* skip algoid */

    /* digest */
    if (DecodeAsn1Tag(input, inputSz, &idx, &digest_len,
            ASN_OCTET_STRING) != 0) {
        return -1;
    }
    /* return digest buffer pointer */
    *pInput = &input[idx];
    return digest_len;
}
#endif /* !NO_RSA_SIG_ENCODING */

#ifdef WOLFBOOT_TPM
/* RSA PKCSV15 un-padding with RSA_BLOCK_TYPE_1 (public) */
/* UnPad plaintext, set start to *output, return length of plaintext or error */
static int RsaUnPad(const byte *pkcsBlock, int pkcsBlockLen, byte **output)
{
    int ret = BAD_FUNC_ARG, i;
    if (output == NULL || pkcsBlockLen < 2 || pkcsBlockLen > 0xFFFF) {
        return BAD_FUNC_ARG;
    }
    /* First byte must be 0x00 and Second byte, block type, 0x01 */
    if (pkcsBlock[0] != 0 || pkcsBlock[1] != RSA_BLOCK_TYPE_1) {
        return RSA_PAD_E;
    }
    /* check the padding until we find the separator */
    for (i = 2; i < pkcsBlockLen && pkcsBlock[i++] == 0xFF; ) { }
    /* Minimum of 11 bytes of pre-message data and must have separator. */
    if (i < RSA_MIN_PAD_SZ || pkcsBlock[i-1] != 0) {
        return RSA_PAD_E;
    }
    *output = (byte *)(pkcsBlock + i);
    ret = pkcsBlockLen - i;
    return ret;
}
#endif /* WOLFBOOT_TPM */

static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret;
    uint8_t output[IMAGE_SIGNATURE_SIZE];
    int output_sz = (int)sizeof(output);
    uint8_t* digest_out = NULL;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    word32 inOutIdx = 0;
#ifdef WOLFBOOT_TPM
    WOLFTPM2_KEY tpmKey;
    const byte *n = NULL, *e = NULL;
    word32 nSz = 0, eSz = 0;
#else
    struct RsaKey rsa;
#endif

    if (pubkey == NULL || pubkey_sz < 0) {
        return;
    }

#ifdef WOLFBOOT_TPM
    /* Extract DER RSA key struct */
    memset(&tpmKey, 0, sizeof(tpmKey));
    ret = wc_RsaPublicKeyDecode_ex(pubkey, &inOutIdx, pubkey_sz,
        &n, &nSz, /* modulus */
        &e, &eSz  /* exponent */
    );
    if (ret == 0) {
        /* Load public key into TPM */
        memset(&tpmKey, 0, sizeof(tpmKey));
        ret = wolfTPM2_LoadRsaPublicKey_ex(&wolftpm_dev, &tpmKey,
            n, nSz, *((word32*)e),
            TPM_ALG_NULL, WOLFBOOT_TPM_HASH_ALG);
    }
    if (ret == 0) {
        /* Perform public decrypt and manually un-pad */
        ret = wolfTPM2_RsaEncrypt(&wolftpm_dev, &tpmKey,
            TPM_ALG_NULL, /* no padding */
            sig, IMAGE_SIGNATURE_SIZE,
            output, &output_sz);
    }
    if (ret == 0) {
        /* Perform PKCSv1.5 UnPadding */
        ret = RsaUnPad(output, output_sz, &digest_out);
    }

    if (ret < 0) {
        wolfBoot_printf("TPM RSA error %d (%s)\n",
            ret, wolfTPM2_GetRCString(ret));
        return;
    }

    wolfTPM2_UnloadHandle(&wolftpm_dev, &tpmKey.handle);

#else
    /* wolfCrypt software RSA verify */
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) ||\
    defined(WOLFBOOT_RENESAS_TSIP)
    ret = wc_InitRsaKey_ex(&rsa, NULL, RENESAS_DEVID);
    if (ret == 0) {
        XMEMCPY(output, sig, IMAGE_SIGNATURE_SIZE);
        RSA_VERIFY_FN(ret,
            wc_RsaSSL_Verify, img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE,
            output, IMAGE_SIGNATURE_SIZE, &rsa);
        /* SCE SignatureVerify API has verified */
        if (ret == 0)
            wolfBoot_image_confirm_signature_ok(img);
    }
    (void)digest_out;
#else
    ret = wc_InitRsaKey(&rsa, NULL);
    if (ret == 0) {
        /* Import public key */
        ret = wc_RsaPublicKeyDecode((byte*)pubkey, &inOutIdx, &rsa, pubkey_sz);
        if (ret >= 0) {
            XMEMCPY(output, sig, IMAGE_SIGNATURE_SIZE);
            RSA_VERIFY_FN(ret,
                wc_RsaSSL_VerifyInline, output, IMAGE_SIGNATURE_SIZE,
                    &digest_out, &rsa);
        }
    }
#endif /* SCE || TSIP */
    wc_FreeRsaKey(&rsa);
#endif /* WOLFBOOT_TPM */

#ifndef NO_RSA_SIG_ENCODING
    if (ret > WOLFBOOT_SHA_DIGEST_SIZE) {
        /* larger result indicates it might have an ASN.1 encoded header */
        ret = RsaDecodeSignature(&digest_out, ret);
    }
#endif
    if (ret == WOLFBOOT_SHA_DIGEST_SIZE && img && digest_out) {
        RSA_VERIFY_HASH(img, digest_out);
    }
}
#endif /* WOLFBOOT_SIGN_RSA2048 || WOLFBOOT_SIGN_3072 || \
        * WOLFBOOT_SIGN_RSA4096 */

#ifdef WOLFBOOT_SIGN_LMS
#include <wolfssl/wolfcrypt/lms.h>
#ifdef HAVE_LIBLMS
    #include <wolfssl/wolfcrypt/ext_lms.h>
#endif

static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int       ret = 0;
    LmsKey    lms;
    word32    pub_len = 0;
    uint8_t * pubkey = NULL;

    wolfBoot_printf("info: LMS wolfBoot_verify_signature\n");

    pubkey = keystore_get_buffer(key_slot);
    if (pubkey == NULL) {
        wolfBoot_printf("error: Lms pubkey not found\n");
        return;
    }

    ret = wc_LmsKey_Init(&lms, NULL, INVALID_DEVID);
    if (ret != 0) {
        wolfBoot_printf("error: wc_LmsKey_Init returned %d\n", ret);
        return;
    }

    /* Set the LMS parameters. */
    ret = wc_LmsKey_SetParameters(&lms, LMS_LEVELS, LMS_HEIGHT,
                                  LMS_WINTERNITZ);
    if (ret != 0) {
        /* Something is wrong with the pub key or LMS parameters. */
        wolfBoot_printf("error: wc_LmsKey_SetParameters(%d, %d, %d)" \
                        " returned %d\n", LMS_LEVELS, LMS_HEIGHT,
                        LMS_WINTERNITZ, ret);
        return;
    }

    wolfBoot_printf("info: using LMS parameters: L%d-H%d-W%d\n", LMS_LEVELS,
                    LMS_HEIGHT, LMS_WINTERNITZ);

    /* Set the public key. */
    ret = wc_LmsKey_ImportPubRaw(&lms, pubkey, KEYSTORE_PUBKEY_SIZE);
    if (ret != 0) {
        /* Something is wrong with the pub key or LMS parameters. */
        wolfBoot_printf("error: wc_LmsKey_ImportPubRaw" \
                        " returned %d\n", ret);
        return;
    }

    ret = wc_LmsKey_Verify(&lms, sig, IMAGE_SIGNATURE_SIZE, img->sha_hash,
                           WOLFBOOT_SHA_DIGEST_SIZE);

    if (ret == 0) {
        wolfBoot_printf("info: wc_LmsKey_Verify returned OK\n");
        wolfBoot_image_confirm_signature_ok(img);
    }
    else {
        wolfBoot_printf("error: wc_LmsKey_Verify returned %d\n", ret);
    }

    wc_LmsKey_Free(&lms);
}
#endif /* WOLFBOOT_SIGN_LMS */


/**
 * @brief Get the specified header type from the external flash image.
 *
 * @param img The image to retrieve the header from.
 * @param type The type of header to retrieve.
 * @param ptr A pointer to the header data.
 * @return The size of the header if found, otherwise 0.
 */
static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type,
        uint8_t **ptr);

/**
 * @brief This function searches for the TLV entry in the header and provides
 * a pointer to the corresponding data.
 *
 * @param img The image to retrieve the data from.
 * @param type The type of header to retrieve.
 * @param ptr A pointer to store the position of the header.
 * @return The size of the data if found, otherwise 0.
 */
static uint16_t get_header(struct wolfBoot_image *img, uint16_t type,
        uint8_t **ptr)
{
    if (PART_IS_EXT(img))
        return get_header_ext(img, type, ptr);
    else
        return wolfBoot_find_header(img->hdr + IMAGE_HEADER_OFFSET, type, ptr);
}

#ifdef EXT_FLASH
static uint8_t ext_hash_block[WOLFBOOT_SHA_BLOCK_SIZE];
#endif
/**
 * @brief Get a block of data to be hashed.
 *
 * @param img The image to retrieve the data from.
 * @param offset The offset to start reading the data from.
 * @return A pointer to the data block.
 */
static uint8_t *get_sha_block(struct wolfBoot_image *img, uint32_t offset)
{
    if (offset > img->fw_size)
        return NULL;
#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        ext_flash_check_read((uintptr_t)(img->fw_base) + offset, ext_hash_block,
                WOLFBOOT_SHA_BLOCK_SIZE);
        return ext_hash_block;
    } else
#endif
        return (uint8_t *)(img->fw_base + offset);
}

#ifdef EXT_FLASH
static uint8_t hdr_cpy[IMAGE_HEADER_SIZE];
static int hdr_cpy_done = 0;

/**
 * @brief Get a copy of the image header.
 *
 * @param img The image to retrieve the header from.
 * @return A pointer to the copied header data.
 */
static uint8_t *fetch_hdr_cpy(struct wolfBoot_image *img)
{
    if (!hdr_cpy_done) {
        ext_flash_check_read((uintptr_t)img->hdr, hdr_cpy, IMAGE_HEADER_SIZE);
        hdr_cpy_done = 1;
    }
    return hdr_cpy;
}

static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type,
        uint8_t **ptr)
{
    return wolfBoot_find_header(fetch_hdr_cpy(img) + IMAGE_HEADER_OFFSET, type,
            ptr);
}

#else
#   define fetch_hdr_cpy(i) ((uint8_t *)0)
static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type,
    uint8_t **ptr)
{
    (void)img; (void)type; (void)ptr;
    return 0;
}
#endif

static uint8_t *get_img_hdr(struct wolfBoot_image *img)
{
    if (PART_IS_EXT(img))
        return fetch_hdr_cpy(img);
    else
        return (uint8_t *)(img->hdr);
}

#if defined(WOLFBOOT_HASH_SHA256)
#include <wolfssl/wolfcrypt/sha256.h>

#ifdef WOLFBOOT_MEASURED_BOOT
static int self_sha256(uint8_t *hash)
{
    uintptr_t p = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t sz = (uint32_t)WOLFBOOT_PARTITION_SIZE;
    uint32_t blksz, position = 0;
    wc_Sha256 sha256_ctx;

    wc_InitSha256(&sha256_ctx);
    do {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > sz)
            blksz = sz - position;
    #if defined(EXT_FLASH) && defined(NO_XIP)
        rc = ext_flash_read(p, ext_hash_block, WOLFBOOT_SHA_BLOCK_SIZE);
        if (rc != WOLFBOOT_SHA_BLOCK_SIZE)
            return -1;
        wc_Sha256Update(&sha256_ctx, ext_hash_block, blksz);
    #else
        wc_Sha256Update(&sha256_ctx, (uint8_t*)p, blksz);
    #endif
        position += blksz;
        p += blksz;
    } while (position < sz);
    wc_Sha256Final(&sha256_ctx, hash);

    return 0;
}
#endif /* WOLFBOOT_MEASURED_BOOT */

/**
 * @brief Calculate the SHA256 hash of the image.
 *
 * @param img The image to calculate the hash for.
 * @param hash A pointer to store the resulting SHA256 hash.
 * @return 0 on success, -1 on failure.
 */
static int image_sha256(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    wc_Sha256 sha256_ctx;

    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    wc_InitSha256(&sha256_ctx);
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        p += blksz;
    }
    do {
        p = get_sha_block(img, position);
        if (p == NULL)
            break;
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > img->fw_size)
            blksz = img->fw_size - position;
        wc_Sha256Update(&sha256_ctx, p, blksz);
        position += blksz;
    } while(position < img->fw_size);

    wc_Sha256Final(&sha256_ctx, hash);
    return 0;
}

#ifndef WOLFBOOT_NO_SIGN

/**
 * @brief Calculate the SHA256 hash of the key.
 *
 * @param key_slot The key slot ID to calculate the hash for.
 * @param hash A pointer to store the resulting SHA256 hash.
 */
static void key_sha256(uint8_t key_slot, uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha256 sha256_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;

    wc_InitSha256(&sha256_ctx);
    while (i < (uint32_t)pubkey_sz) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > (uint32_t)pubkey_sz)
            blksz = pubkey_sz - i;
        wc_Sha256Update(&sha256_ctx, (pubkey + i), blksz);
        i += blksz;
    }
    wc_Sha256Final(&sha256_ctx, hash);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* SHA2-256 */

#if defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>

#ifdef WOLFBOOT_MEASURED_BOOT
static int self_sha384(uint8_t *hash)
{
    uintptr_t p = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t sz = (uint32_t)WOLFBOOT_PARTITION_SIZE;
    uint32_t blksz, position = 0;
    wc_Sha384 sha384_ctx;

    wc_InitSha384(&sha384_ctx);
    do {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > sz)
            blksz = sz - position;
    #if defined(EXT_FLASH) && defined(NO_XIP)
        rc = ext_flash_read(p, ext_hash_block, WOLFBOOT_SHA_BLOCK_SIZE);
        if (rc != WOLFBOOT_SHA_BLOCK_SIZE)
            return -1;
        wc_Sha384Update(&sha384_ctx, ext_hash_block, blksz);
    #else
        wc_Sha384Update(&sha384_ctx, (uint8_t*)p, blksz);
    #endif
        position += blksz;
        p += blksz;
    } while (position < sz);
    wc_Sha384Final(&sha384_ctx, hash);

    return 0;
}
#endif /* WOLFBOOT_MEASURED_BOOT */

/**
 * @brief Calculate SHA-384 hash of the image.
 *
 * This function calculates the SHA-384 hash of the given image.
 *
 * @param img The pointer to the wolfBoot_image structure representing the image.
 * @param hash The buffer to store the SHA-384 hash (48 bytes).
 * @return 0 on success, -1 on error.
 */
static int image_sha384(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    wc_Sha384 sha384_ctx;

    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA384, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    wc_InitSha384(&sha384_ctx);
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha384Update(&sha384_ctx, p, blksz);
        p += blksz;
    }
    do {
        p = get_sha_block(img, position);
        if (p == NULL)
            break;
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > img->fw_size)
            blksz = img->fw_size - position;
        wc_Sha384Update(&sha384_ctx, p, blksz);
        position += blksz;
    } while(position < img->fw_size);

    wc_Sha384Final(&sha384_ctx, hash);
    return 0;
}

#ifndef WOLFBOOT_NO_SIGN

/**
 * @brief Calculate SHA-384 hash of a public key in the keystore.
 *
 * This function calculates the SHA-384 hash of the public key stored in
 * the keystore at the specified key slot.
 *
 * @param key_slot The key slot ID where the public key is stored in the
 * keystore.
 * @param hash The buffer to store the SHA-384 hash (48 bytes).
 * @return None.
 */
static void key_sha384(uint8_t key_slot, uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha384 sha384_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;

    wc_InitSha384(&sha384_ctx);
    while (i < (uint32_t)(pubkey_sz)) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > (uint32_t)pubkey_sz)
            blksz = pubkey_sz - i;
        wc_Sha384Update(&sha384_ctx, (pubkey + i), blksz);
        i += blksz;
    }
    wc_Sha384Final(&sha384_ctx, hash);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* WOLFBOOT_HASH_SHA384 */

#if defined(WOLFBOOT_HASH_SHA3_384)

#include <wolfssl/wolfcrypt/sha3.h>

/**
 * @brief Calculate SHA3-384 hash of the image.
 *
 * This function calculates the SHA3-384 hash of the given image.
 *
 * @param img The pointer to the wolfBoot_image structure representing the image.
 * @param hash The buffer to store the SHA3-384 hash (48 bytes).
 * @return 0 on success, -1 on error.
 */
static int image_sha3_384(struct wolfBoot_image *img, uint8_t *hash)
{
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    wc_Sha3 sha3_ctx;

    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA3_384, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    wc_InitSha3_384(&sha3_ctx, NULL, INVALID_DEVID);
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha3_384_Update(&sha3_ctx, p, blksz);
        p += blksz;
    }
    do {
        p = get_sha_block(img, position);
        if (p == NULL)
            break;
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > img->fw_size)
            blksz = img->fw_size - position;
        wc_Sha3_384_Update(&sha3_ctx, p, blksz);
        position += blksz;
    } while(position < img->fw_size);

    wc_Sha3_384_Final(&sha3_ctx, hash);
    return 0;
}
#ifndef WOLFBOOT_NO_SIGN

/**
 * @brief Calculate SHA3-384 hash of a public key in the keystore.
 *
 * This function calculates the SHA3-384 hash of the public key stored
 * in the keystore at the specified key slot.
 *
 * @param key_slot The key slot ID where the public key is stored in the
 * keystore.
 * @param hash The buffer to store the SHA3-384 hash (48 bytes).
 * @return None.
 */
static void key_sha3_384(uint8_t key_slot, uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha3 sha3_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;
    wc_InitSha3_384(&sha3_ctx, NULL, INVALID_DEVID);
    while (i < (uint32_t)pubkey_sz) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > (uint32_t)pubkey_sz)
            blksz = pubkey_sz - i;
        wc_Sha3_384_Update(&sha3_ctx, pubkey + i, blksz);
        i += blksz;
    }
    wc_Sha3_384_Final(&sha3_ctx, hash);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* SHA3-384 */

#ifdef WOLFBOOT_TPM
#if defined(DEBUG_WOLFTPM) || defined(WOLFTPM_DEBUG_IO) || \
    defined(WOLFBOOT_DEBUG_TPM)
#define LINE_LEN 16
static void wolfBoot_PrintBin(const byte* buffer, word32 length)
{
    word32 i, sz;

    if (!buffer) {
        wolfBoot_printf("\tNULL\n");
        return;
    }

    while (length > 0) {
        sz = length;
        if (sz > LINE_LEN)
            sz = LINE_LEN;

        wolfBoot_printf("\t");
        for (i = 0; i < LINE_LEN; i++) {
            if (i < length)
                wolfBoot_printf("%02x ", buffer[i]);
            else
                wolfBoot_printf("   ");
        }
        wolfBoot_printf("| ");
        for (i = 0; i < sz; i++) {
            if (buffer[i] > 31 && buffer[i] < 127)
                wolfBoot_printf("%c", buffer[i]);
            else
                wolfBoot_printf(".");
        }
        wolfBoot_printf("\r\n");

        buffer += sz;
        length -= sz;
    }
}
#endif /* WOLFTPM_DEBUG_IO || WOLFBOOT_DEBUG_TPM */

#if !defined(ARCH_SIM) && !defined(WOLFTPM_MMIO)
#ifdef WOLFTPM_ADV_IO
static int TPM2_IoCb(TPM2_CTX* ctx, int isRead, word32 addr, byte* buf,
    word16 size, void* userCtx)
#else

/**
 * @brief TPM2 I/O callback function for communication with TPM2 device.
 *
 * This function is used as the I/O callback function for communication
 * with the TPM2 device. It is called during TPM operations to send and
 * receive data from the TPM2 device.
 *
 * @param ctx The pointer to the TPM2 context.
 * @param txBuf The buffer containing data to be sent to the TPM2 device.
 * @param rxBuf The buffer to store the received data from the TPM2 device.
 * @param xferSz The size of the data to be transferred.
 * @param userCtx The user context (not used in this implementation).
 * @return The return code from the TPM2 device operation.
 */
static int TPM2_IoCb(TPM2_CTX* ctx, const byte* txBuf, byte* rxBuf,
    word16 xferSz, void* userCtx)
#endif
{
    int ret;
#ifdef WOLFTPM_CHECK_WAIT_STATE
    int timeout = TPM_SPI_WAIT_RETRY;
#endif
#ifdef WOLFTPM_ADV_IO
    byte txBuf[MAX_SPI_FRAMESIZE+TPM_TIS_HEADER_SZ];
    byte rxBuf[MAX_SPI_FRAMESIZE+TPM_TIS_HEADER_SZ];
    int xferSz = TPM_TIS_HEADER_SZ + size;

#ifdef WOLFTPM_DEBUG_IO
    wolfBoot_printf("TPM2_IoCb (Adv): Read %d, Addr %x, Size %d\n",
        isRead ? 1 : 0, addr, size);
    if (!isRead) {
        wolfBoot_PrintBin(buf, size);
    }
#endif

    /* Build TPM header */
    txBuf[1] = (addr>>16) & 0xFF;
    txBuf[2] = (addr>>8)  & 0xFF;
    txBuf[3] = (addr)     & 0xFF;
    if (isRead) {
        txBuf[0] = TPM_TIS_READ | ((size & 0xFF) - 1);
        memset(&txBuf[TPM_TIS_HEADER_SZ], 0, size);
    }
    else {
        txBuf[0] = TPM_TIS_WRITE | ((size & 0xFF) - 1);
        memcpy(&txBuf[TPM_TIS_HEADER_SZ], buf, size);
    }
    memset(rxBuf, 0, sizeof(rxBuf));
#endif /* WOLFTPM_ADV_IO */

#ifdef WOLFTPM_CHECK_WAIT_STATE /* Handle TIS wait states */
    /* Send header - leave CS asserted */
    ret = spi_xfer(SPI_CS_TPM, txBuf, rxBuf, TPM_TIS_HEADER_SZ,
        0x1 /* 1=SPI_XFER_FLAG_CONTINUE */
    );

    /* Handle wait states */
    while (ret == 0 &&
        --timeout > 0 &&
        (rxBuf[TPM_TIS_HEADER_SZ-1] & TPM_TIS_READY_MASK) == 0)
    {
        /* clock additional byte until 0x01 LSB is set (keep CS asserted) */
        ret = spi_xfer(SPI_CS_TPM,
            &txBuf[TPM_TIS_HEADER_SZ-1],
            &rxBuf[TPM_TIS_HEADER_SZ-1], 1,
            0x1 /* 1=SPI_XFER_FLAG_CONTINUE */
        );
    }
    /* Check for timeout */
    if (ret == 0 && timeout <= 0) {
        ret = TPM_RC_FAILURE;
    }

    /* Transfer remainder of payload (command / response) */
    if (ret == 0) {
        ret = spi_xfer(SPI_CS_TPM,
            &txBuf[TPM_TIS_HEADER_SZ],
            &rxBuf[TPM_TIS_HEADER_SZ],
            xferSz-TPM_TIS_HEADER_SZ,
            0 /* de-assert CS*/ );
    }
    /* On error make sure SPI is de-asserted */
    else {
        spi_xfer(SPI_CS_TPM, NULL, NULL, 0, 0);
        return ret;
    }
#else /* Send Entire Message - no wait states */
    ret = spi_xfer(SPI_CS_TPM, txBuf, rxBuf, xferSz, 0);

    #ifdef WOLFTPM_DEBUG_IO
    wolfBoot_printf("TPM2_IoCb: Ret %d, Sz %d\n", ret, xferSz);
    wolfBoot_PrintBin(txBuf, xferSz);
    wolfBoot_PrintBin(rxBuf, xferSz);
    #endif
#endif /* !WOLFTPM_CHECK_WAIT_STATE */

#ifdef WOLFTPM_ADV_IO
    if (isRead) {
        memcpy(buf, &rxBuf[TPM_TIS_HEADER_SZ], size);
    #ifdef WOLFTPM_DEBUG_IO
        wolfBoot_PrintBin(buf, size);
    #endif
    }
#endif

    return ret;
}
#endif /* !ARCH_SIM && !WOLFTPM_MMIO */

#ifdef WOLFBOOT_MEASURED_BOOT
#define measure_boot(hash) wolfBoot_tpm2_extend((hash), __LINE__)
/**
 * @brief Extends a PCR in the TPM with a hash.
 *
 * Extends a specified PCR's value in the TPM with a given hash. Uses
 * TPM2_PCR_Extend. Optionally, if DEBUG_WOLFTPM or WOLFBOOT_DEBUG_TPM defined,
 * prints debug info.
 *
 * @param[in] hash Pointer to the hash value to extend into the PCR.
 * @param[in] line Line number where the function is called (for debugging).
 * @return 0 on success, an error code on failure.
 *
 */
static int wolfBoot_tpm2_extend(uint8_t* hash, int line)
{
    int rc;
    PCR_Extend_In pcrExtend;
#ifdef WOLFBOOT_DEBUG_TPM
    PCR_Read_In pcrReadCmd;
    PCR_Read_Out pcrReadResp;
#endif

    pcrExtend.pcrHandle = WOLFBOOT_MEASURED_PCR_A;
    pcrExtend.digests.count = 1;
    pcrExtend.digests.digests[0].hashAlg = TPM_ALG_SHA256;
    XMEMCPY(pcrExtend.digests.digests[0].digest.H,
        hash, TPM_SHA256_DIGEST_SIZE);

    rc = TPM2_PCR_Extend(&pcrExtend);
#ifdef DEBUG_WOLFTPM
    wolfBoot_printf("Measured boot: Res %d, Index %d, Line %d\n",
        rc, pcrExtend.pcrHandle, line);
#endif

#ifdef WOLFBOOT_DEBUG_TPM
    if (rc == 0) {
        memset(&pcrReadCmd, 0, sizeof(pcrReadCmd));
        memset(&pcrReadResp, 0, sizeof(pcrReadResp));
        TPM2_SetupPCRSel(&pcrReadCmd.pcrSelectionIn, TPM_ALG_SHA256,
                        pcrExtend.pcrHandle);
        rc = TPM2_PCR_Read(&pcrReadCmd, &pcrReadResp);

        wolfBoot_printf("PCR %d: Res %d, Digest Sz %d, Update Counter %d\n",
            pcrExtend.pcrHandle, rc,
            (int)pcrReadResp.pcrValues.digests[0].size,
            (int)pcrReadResp.pcrUpdateCounter);
        wolfBoot_PrintBin(pcrReadResp.pcrValues.digests[0].buffer,
                          pcrReadResp.pcrValues.digests[0].size);
    }
#endif
    (void)line;

    return rc;
}
#endif /* WOLFBOOT_MEASURED_BOOT */

#if defined(WOLFBOOT_TPM_KEYSTORE) && defined(WC_RNG_SEED_CB)
static int wolfRNG_GetSeedCB(OS_Seed* os, byte* seed, word32 sz)
{
    (void)os;
    return wolfTPM2_GetRandom(&wolftpm_dev, seed, sz);
}
#endif

/**
 * @brief Initialize the TPM2 device and retrieve its capabilities.
 *
 * This function initializes the TPM2 device and retrieves its capabilities.
 *
 * @return 0 on success, an error code on failure.
 */
int wolfBoot_tpm2_init(void)
{
    int rc;
    word32 idx;
    WOLFTPM2_CAPS caps;
#ifdef WOLFBOOT_TPM_KEYSTORE
    TPM_ALG_ID alg;
#endif

#if !defined(ARCH_SIM) && !defined(WOLFTPM_MMIO)
    spi_init(0,0);
#endif

    /* Init the TPM2 device */
    /* simulator should use the network connection, not spi */
#if defined(ARCH_SIM) || defined(WOLFTPM_MMIO)
    rc = wolfTPM2_Init(&wolftpm_dev, NULL, NULL);
#else
    rc = wolfTPM2_Init(&wolftpm_dev, TPM2_IoCb, NULL);
#endif
    if (rc == 0)  {
        /* Get device capabilities + options */
        rc = wolfTPM2_GetCapabilities(&wolftpm_dev, &caps);
    }
    if (rc == 0) {
        wolfBoot_printf("Mfg %s (%d), Vendor %s, Fw %u.%u (0x%x), "
            "FIPS 140-2 %d, CC-EAL4 %d\n",
            caps.mfgStr, caps.mfg, caps.vendorStr, caps.fwVerMajor,
            caps.fwVerMinor, caps.fwVerVendor, caps.fips140_2, caps.cc_eal4);
    }
    else {
        wolfBoot_printf("TPM Init failed! %d\n", rc);
    }

#ifdef WOLFBOOT_TPM_KEYSTORE
    memset(&wolftpm_session, 0, sizeof(wolftpm_session));

#ifdef WC_RNG_SEED_CB
    /* setup callback for RNG seed to use TPM */
    wc_SetSeed_Cb(wolfRNG_GetSeedCB);
#endif

    /* Create a primary storage key - no auth (used for parameter encryption) */
#ifdef HAVE_ECC
    alg = TPM_ALG_ECC;
#elif !defined(NO_RSA)
    alg = TPM_ALG_RSA;
#else
    alg = TPM_ALG_NULL;
#endif
    rc = wolfTPM2_CreateSRK(&wolftpm_dev, &wolftpm_srk, alg, NULL, 0);
    if (rc == 0) {
        /* Setup a TPM session that can be used for parameter encryption */
        rc = wolfTPM2_StartSession(&wolftpm_dev, &wolftpm_session, &wolftpm_srk,
            NULL, TPM_SE_HMAC, TPM_ALG_CFB);
    }
    if (rc != 0) {
        wolfBoot_printf("TPM Create SRK or Start Session error %d (%s)!\n",
            rc, wolfTPM2_GetRCString(rc));
        wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_session.handle);
        wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_srk.handle);
    }
#endif

#ifdef WOLFBOOT_MEASURED_BOOT
    /* hash wolfBoot and extend PCR */
    rc = self_hash(digest);
    if (rc == 0) {
        rc = measure_boot(digest);
    }
    if (rc != 0) {
        wolfBoot_printf("Error %d performing wolfBoot measurement!\n", rc);
    }
#endif

    return rc;
}

/**
 * @brief Deinitialize the TPM2 device.
 *
 * This function deinitializes the TPM2 device and cleans up any resources.
 *
 * @return None.
 */
void wolfBoot_tpm2_deinit(void)
{
#ifdef WOLFBOOT_TPM_KEYSTORE
    #if !defined(ARCH_SIM) && !defined(WOLFBOOT_TPM_NO_CHG_PLAT_AUTH)
    /* Change platform auth to random value, to prevent application from being
     * able to use platform hierarchy. This is defined in section 10 of the
     * TCG PC Client Platform specification.
     */
    int rc = wolfTPM2_ChangePlatformAuth(&wolftpm_dev, &wolftpm_session);
    if (rc != 0) {
        wolfBoot_printf("Error %d setting platform auth\n", rc);
    }
    #endif
    wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_session.handle);
    wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_srk.handle);
#endif /* WOLFBOOT_TPM_KEYSTORE */

    wolfTPM2_Cleanup(&wolftpm_dev);
}

#endif /* WOLFBOOT_TPM */

/**
 * @brief Convert a 32-bit integer from little-endian to native byte order.
 *
 * This function converts a 32-bit integer from little-endian byte order to
 * the native byte order of the machine.
 *
 * @param val The 32-bit integer value in little-endian byte order.
 * @return The 32-bit integer value in native byte order.
 */
static inline uint32_t im2n(uint32_t val)
{
#ifdef BIG_ENDIAN_ORDER
    val = (((val & 0x000000FF) << 24) |
           ((val & 0x0000FF00) <<  8) |
           ((val & 0x00FF0000) >>  8) |
           ((val & 0xFF000000) >> 24));
#endif
  return val;
}

/**
 * @brief Get the size of the image from the image header.
 *
 * This function retrieves the size of the image from the image header.
 *
 * @param image The pointer to the image header.
 * @return The size of the image in bytes.
 */
uint32_t wolfBoot_image_size(uint8_t *image)
{
    uint32_t *size = (uint32_t *)(image + sizeof (uint32_t));
    return im2n(*size);
}

/**
 * @brief Open an image using the provided image address.
 *
 * This function opens an image using the provided image address and initializes
 * the wolfBoot_image structure.
 *
 * @param img The pointer to the wolfBoot_image structure to be initialized.
 * @param image The pointer to the image address.
 * @return 0 on success, -1 on error.
 */
int wolfBoot_open_image_address(struct wolfBoot_image *img, uint8_t *image)
{
    uint32_t *magic = (uint32_t *)(image);
    if (*magic != WOLFBOOT_MAGIC) {
        wolfBoot_printf("Boot header magic 0x%08x invalid at %p\n",
            (unsigned int)*magic, image);
        return -1;
    }
    img->fw_size = wolfBoot_image_size(image);
    wolfBoot_printf("Image size %d\n", (unsigned int)img->fw_size);
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if (img->fw_size > (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE)) {
        wolfBoot_printf("Image size %d > max %d\n",
            (unsigned int)img->fw_size, (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE));
        img->fw_size = 0;
        return -1;
    }
    if (!img->hdr_ok) {
        img->hdr = image;
    }
    img->trailer = img->hdr + WOLFBOOT_PARTITION_SIZE;
#else
    if (img->hdr == NULL) {
        img->hdr = image;
    }
#endif
    img->hdr_ok = 1;
    img->fw_base = img->hdr + IMAGE_HEADER_SIZE;

    return 0;
}

#ifdef MMU
/* Inline use of ByteReverseWord32 */
#define WOLFSSL_MISC_INCLUDED
#include <wolfcrypt/src/misc.c>
static uint32_t wb_reverse_word32(uint32_t x)
{
    return ByteReverseWord32(x);
}

/**
 * @brief Get the size of the Device Tree Blob (DTB).
 *
 * This function retrieves the size of the Device Tree Blob (DTB) from
 * the given DTB address.
 *
 * @param dts_addr The pointer to the Device Tree Blob (DTB) address.
 * @return The size of the DTB in bytes, or -1 if the magic number is invalid.
 */
int wolfBoot_get_dts_size(void *dts_addr)
{
    uint32_t hdr[2];
    uint32_t magic;
    uint32_t size;
    memcpy(hdr, dts_addr, 2 * sizeof(uint32_t));

#ifdef BIG_ENDIAN_ORDER
    magic = wb_reverse_word32(hdr[0]);
    size = hdr[1];
#else
    magic = hdr[0];
    size = wb_reverse_word32(hdr[1]);
#endif
    if (magic != UBOOT_FDT_MAGIC)
        return -1;
    else
        return (int)size;
}
#endif

#ifdef WOLFBOOT_FIXED_PARTITIONS

/**
 * @brief Open an image in a specified partition.
 *
 * This function opens an image in the specified partition and initializes
 * the wolfBoot_image structure.
 *
 * @param img The pointer to the wolfBoot_image structure to be initialized.
 * @param part The partition ID (PART_BOOT, PART_UPDATE, PART_SWAP, etc.).
 * @return 0 on success, -1 on error.
 */
int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part)
{
#ifdef MMU
    int ret;
#endif
    uint8_t *image;
    if (!img)
        return -1;

#ifdef EXT_FLASH
    hdr_cpy_done = 0; /* reset hdr "open" flag */
#endif

    memset(img, 0, sizeof(struct wolfBoot_image));
    img->part = part;
    if (part == PART_SWAP) {
        img->hdr = (void*)WOLFBOOT_PARTITION_SWAP_ADDRESS;
        img->hdr_ok = 1;
        img->fw_base = img->hdr;
        img->fw_size = WOLFBOOT_SECTOR_SIZE;
        return 0;
    }
#ifdef MMU
    if (part == PART_DTS_BOOT || part == PART_DTS_UPDATE) {
        img->hdr = (void *)WOLFBOOT_LOAD_DTS_ADDRESS;
        wolfBoot_printf("%s partition: %p\n",
            (part == PART_DTS_BOOT) ? "DTB boot" : "DTB update", img->hdr);
        if (PART_IS_EXT(img))
            image = fetch_hdr_cpy(img);
        else
            image = (uint8_t*)img->hdr;
        ret = wolfBoot_get_dts_size(image);
        if (ret < 0)
            return -1;
        img->hdr_ok = 1;
        img->fw_base = img->hdr;
        img->fw_size = (uint32_t)ret;
        return 0;
    }
#endif
    if (part == PART_BOOT) {
        img->hdr = (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        wolfBoot_printf("Boot partition: %p\n", img->hdr);
    }
    else if (part == PART_UPDATE) {
        img->hdr = (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        wolfBoot_printf("Update partition: %p\n", img->hdr);
    }
    else {
        return -1;
    }

    /* fetch header address
     * (or copy from external device to a local buffer via fetch_hdr_cpy)
     */
    if (PART_IS_EXT(img))
        image = fetch_hdr_cpy(img);
    else
        image = (uint8_t *)img->hdr;
    img->hdr_ok = 1;

    return wolfBoot_open_image_address(img, image);
}
#endif /* WOLFBOOT_FIXED_PARTITIONS */

/**
 * @brief Verify the integrity of the image using the stored SHA hash.
 *
 * This function verifies the integrity of the image by calculating its SHA hash
 * and comparing it with the stored hash.
 *
 * @param img The pointer to the wolfBoot_image structure representing the image.
 * @return 0 on success, -1 on error.
 */
int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    uint8_t *stored_sha;
    uint16_t stored_sha_len;
#ifdef STAGE1_AUTH
    /* Override global */
    uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
#endif
    stored_sha_len = get_header(img, WOLFBOOT_SHA_HDR, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    if (image_hash(img, digest) != 0)
        return -1;
    if (memcmp(digest, stored_sha, stored_sha_len) != 0)
        return -1;
    img->sha_ok = 1;
    img->sha_hash = stored_sha;
    return 0;
}

#ifdef WOLFBOOT_NO_SIGN
/**
 * @brief Verify the authenticity of the image using a digital signature.
 *
 * This function verifies the authenticity of the image by verifying its digital
 * signature.
 *
 * @param img The pointer to the wolfBoot_image structure representing the image.
 * @return 0 on success, -1 on error, -2 if the signature verification fails.
 */
int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    wolfBoot_image_confirm_signature_ok(img);
    return 0;
}
#else
int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    uint8_t *stored_signature;
    uint16_t stored_signature_size;
    uint8_t *pubkey_hint;
    uint16_t pubkey_hint_size;
    uint8_t *image_type_buf;
    uint16_t image_type;
    uint16_t image_type_size;
    uint32_t key_mask = 0U;
    uint32_t image_part = 1U;
    int key_slot;
#ifdef STAGE1_AUTH
    /* Override global */
    uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
#endif

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
    if (stored_signature_size != IMAGE_SIGNATURE_SIZE)
       return -1;
    pubkey_hint_size = get_header(img, HDR_PUBKEY, &pubkey_hint);
    if (pubkey_hint_size == WOLFBOOT_SHA_DIGEST_SIZE) {
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) ||\
    defined(WOLFBOOT_RENESAS_TSIP)
        /* SCE wrapped key is installed at
         *    RENESAS_SCE_INSTALLEDKEY_ADDR
         * TSIP encrypted key is installed ad
         *    RENESAS_TSIP_INSTALLEDKEY_ADDR
         */
        key_slot = 0;
#else
        key_slot = keyslot_id_by_sha(pubkey_hint);
        if (key_slot < 0) {
            return -1; /* Key was not found */
        }
#endif
    } else {
        return -1; /* Invalid hash size for public key hint */
    }
    image_type_size = get_header(img, HDR_IMG_TYPE, &image_type_buf);
    if (image_type_size != sizeof(uint16_t))
        return -1;
    image_type = (uint16_t)(image_type_buf[0] + (image_type_buf[1] << 8));
    if ((image_type & 0xFF00) != HDR_IMG_TYPE_AUTH)
        return -1;
    if (img->sha_hash == NULL) {
        if (image_hash(img, digest) != 0)
            return -1;
        img->sha_hash = digest;
    }
    key_mask = keystore_get_mask(key_slot);
    image_part = image_type & HDR_IMG_TYPE_PART_MASK;

    /* Check if the key permission mask matches the current partition id */
    if (((1U << image_part) & key_mask) != (1U << image_part))
        return -1; /* Key not allowed to verify this partition id */

    CONFIRM_MASK_VALID(image_part, key_mask);

    /* wolfBoot_verify_signature() does not return the result directly.
     * A call to wolfBoot_image_confirm_signature_ok() is required in order to
     * confirm that the signature verification is OK.
     *
     * only a call to wolfBoot_image_confirm_signature_ok() sets
     * img->signature_ok to 1.
     *
     */
    wolfBoot_verify_signature(key_slot, img, stored_signature);
    if (img->signature_ok == 1)
        return 0;
    return -2;
}
#endif

/**
 * @brief Peek at the content of the image at a specific offset.
 *
 * This function allows peeking at the content of the image at a specific offset
 * without modifying the image.
 *
 * @param img The pointer to the wolfBoot_image structure representing the image.
 * @param offset The offset within the image to peek at.
 * @param sz Optional pointer to store the size of the peeked data.
 * @return The pointer to the peeked data, or NULL if the offset is out of bounds.
 */
uint8_t* wolfBoot_peek_image(struct wolfBoot_image *img, uint32_t offset,
    uint32_t* sz)
{
    uint8_t* p = get_sha_block(img, offset);
    if (sz)
        *sz = WOLFBOOT_SHA_BLOCK_SIZE;
    return p;
}

#if !defined(WOLFBOOT_NO_SIGN) && !defined(WOLFBOOT_RENESAS_SCEPROTECT)

/**
 * @brief Get the key slot ID by SHA hash.
 *
 * This function retrieves the key slot ID from the keystore that matches the
 * provided SHA hash.
 *
 * @param hint The SHA hash of the public key to search for.
 * @return The key slot ID if found, -1 if the key was not found.
 */
static int keyslot_id_by_sha(const uint8_t *hint)
{
#ifdef STAGE1_AUTH
    /* Override global */
    uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
#endif

#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_KEYSTORE)
    /* use public key hash (hint) */
    int rc;
    WOLFTPM2_NV nv;
    word32 digestSz = (word32)TPM2_GetHashDigestSize(WOLFBOOT_TPM_HASH_ALG);
    XMEMSET(&nv, 0, sizeof(nv));
    nv.handle.hndl = WOLFBOOT_TPM_KEYSTORE_NV_INDEX;

#ifdef WOLFBOOT_TPM_KEYSTORE_AUTH
    nv.handle.auth.size = (UINT16)strlen(WOLFBOOT_TPM_KEYSTORE_AUTH);
    memcpy(nv.handle.auth.buffer, WOLFBOOT_TPM_KEYSTORE_AUTH, nv.handle.auth.size);
#endif

    rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, WOLFBOOT_TPM_KEYSTORE_NV_INDEX,
        digest, &digestSz, 0);
    if (rc == 0 && memcmp(digest, hint, WOLFBOOT_SHA_DIGEST_SIZE) == 0) {
    #ifdef DEBUG_WOLFTPM
        wolfBoot_printf("TPM Root of Trust valid\n");
    #endif
        return 0;
    }
    else {
    #ifdef DEBUG_WOLFTPM
        wolfBoot_printf("TPM Root of Trust failed! %d (%s)\n",
            rc, wolfTPM2_GetRCString(rc));
        wolfBoot_printf("Expected Hash %d\n", WOLFBOOT_SHA_DIGEST_SIZE);
        wolfBoot_PrintBin(hint, WOLFBOOT_SHA_DIGEST_SIZE);
    #endif
    }
#else
    int id = 0;
    for (id = 0; id < keystore_num_pubkeys(); id++) {
        key_hash(id, digest);
        if (memcmp(digest, hint, WOLFBOOT_SHA_DIGEST_SIZE) == 0)
            return id;
    }
#endif
    return -1;
}
#endif
#endif /* IMAGE_H_ */
