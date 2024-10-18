/* image.c
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
/**
 * @file image.c
 * @brief This file contains functions related to image handling and
 * verification.
 */
#ifndef IMAGE_H_
#define IMAGE_H_

#ifdef UNIT_TEST
#include <stdio.h>
#endif
#include <wolfssl/wolfcrypt/settings.h> /* for wolfCrypt hash/sign routines */

#include <stddef.h>
#include <string.h>

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_drv.h"
#include "printf.h"
#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif

/* Globals */
static uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];

/* TPM based verify */
#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_VERIFY)
static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret = 0, verify_res = 0;
    WOLFTPM2_KEY tpmKey;
    TPM_ALG_ID alg, sigAlg;
    uint8_t *hdr;
    uint16_t hdrSz;

    /* Load public key into TPM */
    memset(&tpmKey, 0, sizeof(tpmKey));

    /* get public key for policy authorization */
    hdrSz = wolfBoot_get_header(img, HDR_PUBKEY, &hdr);
    if (hdrSz != WOLFBOOT_SHA_DIGEST_SIZE) {
        ret = -1;
    }
    if (ret == 0) {
        ret = wolfBoot_load_pubkey(hdr /* pubkey_hint */, &tpmKey, &alg);
    }
    if (ret == 0) {
        sigAlg = (alg == TPM_ALG_RSA) ? TPM_ALG_RSASSA : TPM_ALG_ECDSA;
        ret = wolfTPM2_VerifyHashScheme(&wolftpm_dev, &tpmKey,
            sig, IMAGE_SIGNATURE_SIZE,               /* Signature */
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, /* Hash */
            sigAlg, WOLFBOOT_TPM_HASH_ALG);
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
        wolfBoot_printf("TPM verify signature error %d (%s)\n",
            ret, wolfTPM2_GetRCString(ret));
    }
    (void)key_slot;
}
#else

/* wolfCrypt software verify */
#ifdef WOLFBOOT_SIGN_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>
static void wolfBoot_verify_signature_ed25519(uint8_t key_slot,
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
static void wolfBoot_verify_signature_ed448(uint8_t key_slot,
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
static void wolfBoot_verify_signature_ecc(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret, verify_res = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    int point_sz = pubkey_sz/2;
    ecc_key ecc;
    mp_int r, s;

    if (pubkey == NULL || pubkey_sz <= 0) {
        return;
    }

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP)
    ret = wc_ecc_init_ex(&ecc, NULL, RENESAS_DEVID);
#else
    ret = wc_ecc_init(&ecc);
#endif
    if (ret == 0) {
    #if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
        defined(WOLFBOOT_RENESAS_TSIP) || \
        defined(WOLFBOOT_RENESAS_RSIP)
        /* The public key is wrapped and cannot be imported.
         * Key must be loaded to TSIP and unwrapped.
         * Then ECDSA crypto callback will perform verify on TSIP hardware */
        wc_ecc_set_curve(&ecc, 0, ECC_KEY_TYPE);

        /* The wc_ecc_verify_hash must be used since _ex version does not
         * trigger crypto callback. Building with NO_ASN allows us to send R+S
         * directly without ASN.1 encoded DSA header */
        VERIFY_FN(img, &verify_res, wc_ecc_verify_hash,
            sig, IMAGE_SIGNATURE_SIZE,
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res, &ecc)
    #else
        /* Import public key */
        ret = wc_ecc_import_unsigned(&ecc, pubkey, pubkey + point_sz, NULL,
            ECC_KEY_TYPE);
        if (ret == 0 && ecc.type == ECC_PUBLICKEY) {
            /* Import signature into r,s */
            mp_init(&r);
            mp_init(&s);
            mp_read_unsigned_bin(&r, sig, point_sz);
            mp_read_unsigned_bin(&s, sig + point_sz, point_sz);
            VERIFY_FN(img, &verify_res, wc_ecc_verify_hash_ex, &r, &s,
                img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res, &ecc);
        }
    #endif
    }
    wc_ecc_free(&ecc);
}

#endif /* WOLFBOOT_SIGN_ECC256 || WOLFBOOT_SIGN_ECC384 || WOLFBOOT_SIGN_ECC521 */


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

static void wolfBoot_verify_signature_rsa(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret;
    uint8_t output[IMAGE_SIGNATURE_SIZE];
    uint8_t* digest_out = NULL;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    word32 inOutIdx = 0;
    struct RsaKey rsa;

    if (pubkey == NULL || pubkey_sz < 0) {
        return;
    }

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP)
    ret = wc_InitRsaKey_ex(&rsa, NULL, RENESAS_DEVID);
    if (ret == 0) {
        XMEMCPY(output, sig, IMAGE_SIGNATURE_SIZE);
        RSA_VERIFY_FN(ret,
            wc_RsaSSL_Verify, img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE,
            output, IMAGE_SIGNATURE_SIZE, &rsa);
        /* The crypto callback success also verifies hash */
        if (ret == 0)
            wolfBoot_image_confirm_signature_ok(img);
    }
    (void)digest_out;
#else
    /* wolfCrypt software RSA verify */
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
#else
    #include <wolfssl/wolfcrypt/wc_lms.h>
#endif

static void wolfBoot_verify_signature_lms(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int       ret = 0;
    LmsKey    lms;
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

#ifdef WOLFBOOT_SIGN_XMSS
#include <wolfssl/wolfcrypt/xmss.h>
#ifdef HAVE_LIBXMSS
    #include <wolfssl/wolfcrypt/ext_xmss.h>
#else
    #include <wolfssl/wolfcrypt/wc_xmss.h>
#endif

static void wolfBoot_verify_signature_xmss(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int       ret = 0;
    XmssKey   xmss;
    uint8_t * pubkey = NULL;

    wolfBoot_printf("info: XMSS wolfBoot_verify_signature\n");

    pubkey = keystore_get_buffer(key_slot);
    if (pubkey == NULL) {
        wolfBoot_printf("error: Xmss pubkey not found\n");
        return;
    }

    ret = wc_XmssKey_Init(&xmss, NULL, INVALID_DEVID);
    if (ret != 0) {
        wolfBoot_printf("error: wc_XmssKey_Init returned %d\n", ret);
        return;
    }

    /* Set the XMSS parameters. */
    ret = wc_XmssKey_SetParamStr(&xmss, WOLFBOOT_XMSS_PARAMS);
    if (ret != 0) {
        /* Something is wrong with the pub key or XMSS parameters. */
        wolfBoot_printf("error: wc_XmssKey_SetParamStr(%s)" \
                        " returned %d\n", WOLFBOOT_XMSS_PARAMS, ret);
        return;
    }

    wolfBoot_printf("info: using XMSS parameters: %s\n", WOLFBOOT_XMSS_PARAMS);

    /* Set the public key. */
    ret = wc_XmssKey_ImportPubRaw(&xmss, pubkey, KEYSTORE_PUBKEY_SIZE);
    if (ret != 0) {
        /* Something is wrong with the pub key or LMS parameters. */
        wolfBoot_printf("error: wc_XmssKey_ImportPubRaw" \
                        " returned %d\n", ret);
        return;
    }

    ret = wc_XmssKey_Verify(&xmss, sig, IMAGE_SIGNATURE_SIZE, img->sha_hash,
                           WOLFBOOT_SHA_DIGEST_SIZE);

    if (ret == 0) {
        wolfBoot_printf("info: wc_XmssKey_Verify returned OK\n");
        wolfBoot_image_confirm_signature_ok(img);
    }
    else {
        wolfBoot_printf("error: wc_XmssKey_Verify returned %d\n", ret);
    }

    wc_XmssKey_Free(&xmss);
}

#endif /* WOLFBOOT_SIGN_XMSS */

#ifdef WOLFBOOT_SIGN_ML_DSA
#include <wolfssl/wolfcrypt/dilithium.h>
static void wolfBoot_verify_signature_ml_dsa(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int       ret = 0;
    MlDsaKey  ml_dsa;
    uint8_t * pubkey = NULL;
    int       pub_len = 0;
    int       sig_len = 0;
    int       verify_res = 0;

    wolfBoot_printf("info: ML-DSA wolfBoot_verify_signature\n");

    pubkey = keystore_get_buffer(key_slot);

    if (pubkey == NULL) {
        wolfBoot_printf("error: ML-DSA pubkey not found\n");
        return;
    }

    ret = wc_MlDsaKey_Init(&ml_dsa, NULL, INVALID_DEVID);

    if (ret != 0) {
        wolfBoot_printf("error: wc_MlDsaKey_Init returned %d\n", ret);
    }

    if (ret == 0) {
        /* Set the ML-DSA security level. */
        ret = wc_MlDsaKey_SetParams(&ml_dsa, ML_DSA_LEVEL);

        if (ret != 0) {
            wolfBoot_printf("error: wc_MlDsaKey_SetParams(%d)" \
                            " returned %d\n", ML_DSA_LEVEL, ret);
        }
    }

    /* Make sure pub key matches parameters. */
    if (ret == 0) {
        ret = wc_MlDsaKey_GetPubLen(&ml_dsa, &pub_len);

        if (ret != 0 || pub_len <= 0) {
            wolfBoot_printf("error: wc_MlDsaKey_GetPubLen returned %d\n", ret);
            ret = -1;
        }
        else if (pub_len != KEYSTORE_PUBKEY_SIZE_ML_DSA) {
            wolfBoot_printf("error: ML-DSA pub key mismatch: got %d bytes " \
                   "expected %d\n", pub_len, KEYSTORE_PUBKEY_SIZE_ML_DSA);
            ret = -1;
        }
    }

    /* Make sure sig len matches parameters. */
    if (ret == 0) {
        ret = wc_MlDsaKey_GetSigLen(&ml_dsa, &sig_len);

        if (ret != 0 || sig_len <= 0) {
            wolfBoot_printf("error: wc_MlDsaKey_GetPubLen returned %d\n", ret);
            ret = -1;
        }
        else if (sig_len != IMAGE_SIGNATURE_SIZE) {
            wolfBoot_printf("error: ML-DSA sig len mismatch: got %d bytes " \
                   "expected %d\n", sig_len, IMAGE_SIGNATURE_SIZE);
            ret = -1;
        }
    }

    if (ret == 0) {
        /* Now import pub key. */
        ret = wc_MlDsaKey_ImportPubRaw(&ml_dsa, pubkey, pub_len);

        if (ret != 0) {
            wolfBoot_printf("error: wc_MlDsaKey_ImportPubRaw returned: %d\n",
                            ret);
        }
    }

    if (ret == 0) {
        wolfBoot_printf("info: using ML-DSA security level: %d\n",
                        ML_DSA_LEVEL);

        /* Finally verify signagure. */
        ret = wc_MlDsaKey_Verify(&ml_dsa, sig, IMAGE_SIGNATURE_SIZE,
                                 img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE,
                                 &verify_res);

        if (ret == 0 && verify_res == 1) {
            wolfBoot_printf("info: wc_MlDsaKey_Verify returned OK\n");
            wolfBoot_image_confirm_signature_ok(img);
        }
        else {
            wolfBoot_printf("error: wc_MlDsaKey_Verify returned: ret=%d, "
                            "res=%d\n", ret, verify_res);
        }
    }

    wc_MlDsaKey_Free(&ml_dsa);
}

#endif /* WOLFBOOT_SIGN_ML_DSA */

#endif /* WOLFBOOT_TPM && WOLFBOOT_TPM_VERIFY */


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
#define get_header wolfBoot_get_header /* internal reference to function */
uint16_t wolfBoot_get_header(struct wolfBoot_image *img, uint16_t type,
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
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha256 sha256_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;

    wc_InitSha256(&sha256_ctx);
    wc_Sha256Update(&sha256_ctx, pubkey, (word32)pubkey_sz);
    wc_Sha256Final(&sha256_ctx, hash);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* SHA2-256 */

#if defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>

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
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha384 sha384_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;

    wc_InitSha384(&sha384_ctx);
    wc_Sha384Update(&sha384_ctx, pubkey, (word32)pubkey_sz);
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
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);
    wc_Sha3 sha3_ctx;

    if (!pubkey || (pubkey_sz < 0))
        return;
    wc_InitSha3_384(&sha3_ctx, NULL, INVALID_DEVID);
    wc_Sha3_384_Update(&sha3_ctx, pubkey, (word32)pubkey_sz);
    wc_Sha3_384_Final(&sha3_ctx, hash);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* SHA3-384 */

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

#ifdef WOLFBOOT_FIXED_PARTITIONS
    if (img->fw_size > (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE)) {
        wolfBoot_printf("Image size %d > max %d\n",
            (unsigned int)img->fw_size,
            (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE));
        img->fw_size = WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE;
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
#ifdef EXT_FLASH
    img->hdr_cache = image;
#endif

    wolfBoot_printf("%s partition: %p (sz %d, ver 0x%x, type 0x%x)\n",
        (img->part == PART_BOOT) ? "Boot" : "Update",
        img->hdr, (unsigned int)img->fw_size,
        wolfBoot_get_blob_version(image),
        wolfBoot_get_blob_type(image));

    return 0;
}

#ifdef MMU

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
    int ret = fdt_check_header(dts_addr);
    if (ret == 0) {
        ret = fdt_totalsize(dts_addr);
    }
    return ret;
}

#endif /* MMU */

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
        img->hdr = (part == PART_DTS_BOOT) ?
            (void*)WOLFBOOT_DTS_BOOT_ADDRESS :
            (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
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
    }
    else if (part == PART_UPDATE) {
        img->hdr = (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
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


#ifdef EXT_FLASH
int wolfBoot_open_image_external(struct wolfBoot_image* img, uint8_t part,
    uint8_t* addr)
{
    uint8_t* image;
    if (img == NULL)
        return -1;

    memset(img, 0, sizeof(struct wolfBoot_image));
    img->part = part;
    img->hdr = addr;
    img->hdr_ok = 1;
    hdr_cpy_done = 0; /* reset hdr "open" flag */
    image = fetch_hdr_cpy(img);
    return wolfBoot_open_image_address(img, image);
}
#endif /* EXT_FLASH */

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

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
    if (stored_signature_size != IMAGE_SIGNATURE_SIZE)
       return -1;
    pubkey_hint_size = get_header(img, HDR_PUBKEY, &pubkey_hint);
    if (pubkey_hint_size == WOLFBOOT_SHA_DIGEST_SIZE) {
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP)
        /* SCE wrapped key is installed at
         *    RENESAS_SCE_INSTALLEDKEY_ADDR
         * TSIP encrypted key is installed at
         *    RENESAS_TSIP_INSTALLEDKEY_ADDR
         */
        key_slot = 0;
#else
        key_slot = keyslot_id_by_sha(pubkey_hint);
        if (key_slot < 0) {
            return -1; /* Key was not found */
        }

    #ifdef WOLFBOOT_TPM_KEYSTORE
        if (wolfBoot_check_rot(key_slot, pubkey_hint) != 0) {
            return -1; /* TPM root of trust failed! */
        }
    #endif
#endif
    }
    else {
        return -1; /* Invalid hash size for public key hint */
    }
    image_type_size = get_header(img, HDR_IMG_TYPE, &image_type_buf);
    if (image_type_size != sizeof(uint16_t))
        return -1;
    image_type = (uint16_t)(image_type_buf[0] + (image_type_buf[1] << 8));
    if ((image_type & HDR_IMG_TYPE_AUTH_MASK) != HDR_IMG_TYPE_AUTH)
        return -1;
    if (img->sha_hash == NULL) {
        if (image_hash(img, digest) != 0)
            return -1;
        img->sha_hash = digest;
    }
    key_mask = keystore_get_mask(key_slot);
    image_part = image_type & HDR_IMG_TYPE_PART_MASK;

    /* Check if the key permission mask matches the current partition id */
    if (((1U << image_part) & key_mask) != (1U << image_part)) {
        return -1; /* Key not allowed to verify this partition id */
    }

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
#ifdef SIGN_HYBRID
    {
        /* Invalidate the signature_ok flag */
        wolfBoot_image_clear_signature_ok(img);
        /* Load the pubkey hint for the secondary key */
        pubkey_hint_size = get_header(img, HDR_SECONDARY_PUBKEY, &pubkey_hint);
        if (pubkey_hint_size == WOLFBOOT_SHA_DIGEST_SIZE) {
            key_slot = keyslot_id_by_sha(pubkey_hint);
            if (key_slot < 0) {
                return -1; /* Key was not found */
            }
            key_mask = keystore_get_mask(key_slot);
            if (((1U << image_part) & key_mask) != (1U << image_part)) {
                return -1; /* Key not allowed to verify this partition id */
            }
            CONFIRM_MASK_VALID(image_part, key_mask);
            wolfBoot_verify_signature(key_slot, img, stored_signature);
        }
    }
    if (img->signature_ok == 1)
#endif
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
int keyslot_id_by_sha(const uint8_t *hint)
{
    int id;

    for (id = 0; id < keystore_num_pubkeys(); id++) {
        key_hash(id, digest);
        if (memcmp(digest, hint, WOLFBOOT_SHA_DIGEST_SIZE) == 0) {
            return id;
        }
    }
    return -1;
}
#endif /* !WOLFBOOT_NO_SIGN && !WOLFBOOT_RENESAS_SCEPROTECT */

#endif /* IMAGE_H_ */
