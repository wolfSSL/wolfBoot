/* image.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#ifdef UNIT_TEST
#include <stdio.h>
#endif
#include <wolfssl/wolfcrypt/settings.h> /* for wolfCrypt hash/sign routines */
#ifdef WOLFBOOT_KEYTOOLS
    /* this code needs to use the Use ./include/user_settings.h, not keytools */
    #error "The wrong user_settings.h has been included."
#endif


#include <stddef.h>
#include <string.h>

#include "loader.h"
#include "image.h"
#include "wolfboot/wolfboot.h"
#include "hal.h"
#include "spi_drv.h"
#include "printf.h"
#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif
#ifdef WOLFBOOT_HASH_SHA256
#include <wolfssl/wolfcrypt/sha256.h>
#endif
#ifdef WOLFBOOT_HASH_SHA384
#include <wolfssl/wolfcrypt/sha512.h>
#endif
#ifdef WOLFBOOT_HASH_SHA3_384
#include <wolfssl/wolfcrypt/sha3.h>
#endif

/* Globals */
static uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE] XALIGNED(4);

#if defined(WOLFBOOT_CERT_CHAIN_VERIFY) && \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
     defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER))
static whKeyId g_certLeafKeyId  = WH_KEYID_ERASED;
static int     g_leafKeyIdValid = 0;
#endif

/* TPM based verify */
#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_VERIFY)
#ifdef ECC_IMAGE_SIGNATURE_SIZE
#define IMAGE_SIGNATURE_SIZE ECC_IMAGE_SIGNATURE_SIZE
#else
#define IMAGE_SIGNATURE_SIZE RSA_IMAGE_SIGNATURE_SIZE
#endif

static void wolfBoot_verify_signature_tpm(uint8_t key_slot,
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
            sig, /* Signature */
            IMAGE_SIGNATURE_SIZE, /* Signature size */
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
    VERIFY_FN(img, &res, wc_ed25519_verify_msg, sig, ED25519_IMAGE_SIGNATURE_SIZE,
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
    VERIFY_FN(img, &res, wc_ed448_verify_msg, sig, ED448_IMAGE_SIGNATURE_SIZE,
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &res, &ed, NULL, 0);
}

#endif


#if defined(WOLFBOOT_SIGN_ECC256) || \
    defined(WOLFBOOT_SIGN_ECC384) || \
    defined(WOLFBOOT_SIGN_ECC521) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC256) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC384) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC521)

#include <wolfssl/wolfcrypt/ecc.h>

#if defined(WOLFBOOT_SIGN_ECC256) || defined(WOLFBOOT_SIGN_SECONDARY_ECC256)
    #define ECC_KEY_TYPE ECC_SECP256R1
#elif defined(WOLFBOOT_SIGN_ECC384) || defined(WOLFBOOT_SIGN_SECONDARY_ECC384)
    #define ECC_KEY_TYPE ECC_SECP384R1
#elif defined(WOLFBOOT_SIGN_ECC521) || defined(WOLFBOOT_SIGN_SECONDARY_ECC521)
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
    ecc_key ecc;
    mp_int  r, s;
#if (!defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT &&   \
     !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)) || \
    (defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT &&    \
     !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID))
    uint8_t* pubkey    = keystore_get_buffer(key_slot);
    int      pubkey_sz = keystore_get_size(key_slot);
    int      point_sz  = pubkey_sz / 2;

    if (pubkey == NULL || pubkey_sz <= 0) {
        return;
    }
#endif

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP)
    ret = wc_ecc_init_ex(&ecc, NULL, RENESAS_DEVID);
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    ret = wc_ecc_init_ex(&ecc, NULL, hsmDevIdPubKey);
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
            sig, ECC_IMAGE_SIGNATURE_SIZE,
            img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res, &ecc)

    #elif defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
          defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)

        uint8_t tmpSigBuf[ECC_MAX_SIG_SIZE] = {0};
        size_t  tmpSigSz                    = sizeof(tmpSigBuf);

    #if defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID) || \
        (defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
         defined(WOLFBOOT_CERT_CHAIN_VERIFY))
        (void)key_slot;

        /* hardcoded, since not using keystore */
        const int point_sz = ECC_IMAGE_SIGNATURE_SIZE / 2;

        /* Use the public key ID to verify the signature */
    #if defined(WOLFBOOT_CERT_CHAIN_VERIFY)
        /* If using certificate chain verification and we have a verified leaf
         * key ID */
        if (g_leafKeyIdValid) {
            /* Use the leaf key ID from certificate verification */
            #if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
            ret = wh_Client_EccSetKeyId(&ecc, g_certLeafKeyId);
            #elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
            ret = wh_Server_EccKeyCacheExport(&hsmServerCtx, g_certLeafKeyId,
                                               &ecc);
            #endif
            wolfBoot_printf(
                "Using leaf cert public key (ID: %08x) for ECC verification\n",
                (unsigned int)g_certLeafKeyId);
        }
        else {
            /* Default behavior: use the pre-configured public key ID */
            #if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
            ret = wh_Client_EccSetKeyId(&ecc, hsmKeyIdPubKey);
            #endif
        }
    #else
        #if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
        ret = wh_Client_EccSetKeyId(&ecc, hsmKeyIdPubKey);
        #endif
    #endif /* WOLFBOOT_USE_WOLFHSM_PUBKEY_ID */
        if (ret != 0) {
            return;
        }
    #else
        /* First, import public key from the keystore to the local wolfCrypt
         * struct, then import into wolfHSM key cache for subsequent
         * verification */
        ret = wc_ecc_import_unsigned(&ecc, pubkey, pubkey + point_sz, NULL,
                                     ECC_KEY_TYPE);
        if (ret != 0) {
            return;
        }

    #endif /* !WOLFBOOT_USE_WOLFHSM_PUBKEY_ID */
        /* wc_ecc_verify_hash_ex() doesn't trigger a crypto callback, so we need
           to use wc_ecc_verify_hash instead. Unfortunately, that requires
           converting the signature to intermediate DER format first */
        mp_init(&r);
        mp_init(&s);
        mp_read_unsigned_bin(&r, sig, point_sz);
        mp_read_unsigned_bin(&s, sig + point_sz, point_sz);
        uint32_t rSz = mp_unsigned_bin_size(&r);
        uint32_t sSz = mp_unsigned_bin_size(&s);
        ret          = wc_ecc_rs_raw_to_sig(sig, rSz, &sig[point_sz], sSz,
                                            (byte*)&tmpSigBuf, (word32*)&tmpSigSz);
        /* Verify the (temporary) DER representation of the signature */
        if (ret == 0) {
            VERIFY_FN(img, &verify_res, wc_ecc_verify_hash, tmpSigBuf, tmpSigSz,
                      img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res,
                      &ecc);
        }
    #if defined(WOLFBOOT_CERT_CHAIN_VERIFY)
        if (g_leafKeyIdValid) {
            #if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
            (void)wh_Client_KeyEvict(&hsmClientCtx, g_certLeafKeyId);
            #elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
            (void)wh_Server_KeystoreEvictKey(&hsmServerCtx, g_certLeafKeyId);
            #endif
            g_leafKeyIdValid = 0;
        }
    #endif
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

#endif /* WOLFBOOT_SIGN_ECC256 || WOLFBOOT_SIGN_ECC384 || WOLFBOOT_SIGN_ECC521 ||
        * WOLFBOOT_SIGN_SECONDARY_ECC256 || WOLFBOOT_SIGN_SECONDARY_ECC384 ||
        * WOLFBOOT_SIGN_SECONDARY_ECC521 */


#if defined(WOLFBOOT_SIGN_RSA2048) || \
    defined(WOLFBOOT_SIGN_RSA3072) || \
    defined(WOLFBOOT_SIGN_RSA4096) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA2048) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA3072) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA4096)

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
    uint8_t output[RSA_IMAGE_SIGNATURE_SIZE];
    uint8_t* digest_out = NULL;
    word32 inOutIdx = 0;
    struct RsaKey rsa;

    (void)inOutIdx;

#if (!defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
     !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)) || \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) &&  \
        !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID))
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int pubkey_sz = keystore_get_size(key_slot);

    if (pubkey == NULL || pubkey_sz < 0) {
        return;
    }
#endif

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) || \
    defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP)
    ret = wc_InitRsaKey_ex(&rsa, NULL, RENESAS_DEVID);
    if (ret == 0) {
        XMEMCPY(output, sig, RSA_IMAGE_SIGNATURE_SIZE);
        RSA_VERIFY_FN(ret,
            wc_RsaSSL_Verify, img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE,
            output, RSA_IMAGE_SIGNATURE_SIZE, &rsa);
        /* The crypto callback success also verifies hash */
        if (ret == 0)
            wolfBoot_image_confirm_signature_ok(img);
    }
    (void)digest_out;
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    ret = wc_InitRsaKey_ex(&rsa, NULL, hsmDevIdPubKey);
    if (ret != 0) {
        return;
    }
#if defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID) ||  \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
     defined(WOLFBOOT_CERT_CHAIN_VERIFY))
    (void)key_slot;
    /* public key is stored on server at hsmKeyIdPubKey*/
#if defined(WOLFBOOT_CERT_CHAIN_VERIFY)
    /* If using certificate chain verification and we have a verified leaf key
     * ID */
    if (g_leafKeyIdValid) {
        /* Use the leaf key ID from certificate verification */
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
        ret = wh_Client_RsaSetKeyId(&rsa, g_certLeafKeyId);
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
        ret = wh_Server_CacheExportRsaKey(&hsmServerCtx, g_certLeafKeyId, &rsa);
#endif
        wolfBoot_printf(
            "Using leaf cert public key (ID: %08x) for RSA verification\n",
            (unsigned int)g_certLeafKeyId);
    }
    else {
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
        /* Default behavior: use the pre-configured public key ID */
        ret = wh_Client_RsaSetKeyId(&rsa, hsmKeyIdPubKey);
#endif
    }
#else
    ret = wh_Client_RsaSetKeyId(&rsa, hsmKeyIdPubKey);
#endif
    if (ret != 0) {
        return;
    }
#else
    whKeyId hsmKeyId = WH_KEYID_ERASED;
    /* Cache the public key on the server */
    ret = wh_Client_KeyCache(&hsmClientCtx, WH_NVM_FLAGS_USAGE_VERIFY, NULL, 0,
                             pubkey, pubkey_sz, &hsmKeyId);
    if (ret != WH_ERROR_OK) {
        return;
    }
    /* Associate this RSA struct with the keyId of the cached key */
    ret = wh_Client_RsaSetKeyId(&rsa, hsmKeyId);
    if (ret != WH_ERROR_OK) {
        return;
    }
#endif /* !WOLFBOOT_USE_WOLFHSM_PUBKEY_ID */
    XMEMCPY(output, sig, RSA_IMAGE_SIGNATURE_SIZE);
    RSA_VERIFY_FN(ret, wc_RsaSSL_VerifyInline, output, RSA_IMAGE_SIGNATURE_SIZE,
                  &digest_out, &rsa);
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
    !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID)
    /* evict the key after use, since we aren't using the RSA import API */
    if (WH_ERROR_OK != wh_Client_KeyEvict(&hsmClientCtx, hsmKeyId)) {
        return;
    }
#elif defined(WOLFBOOT_CERT_CHAIN_VERIFY)
    if (g_leafKeyIdValid) {
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
        (void)wh_Client_KeyEvict(&hsmClientCtx, g_certLeafKeyId);
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
        (void)wh_Server_KeystoreEvictKey(&hsmServerCtx, g_certLeafKeyId);
#endif
        g_leafKeyIdValid = 0;
    }
#endif /* !WOLFBOOT_USE_WOLFHSM_PUBKEY_ID */
#else
    /* wolfCrypt software RSA verify */
    ret = wc_InitRsaKey(&rsa, NULL);
    if (ret == 0) {
        /* Import public key */
        ret = wc_RsaPublicKeyDecode((byte*)pubkey, &inOutIdx, &rsa, pubkey_sz);
        if (ret >= 0) {
            XMEMCPY(output, sig, RSA_IMAGE_SIGNATURE_SIZE);
            RSA_VERIFY_FN(ret,
                wc_RsaSSL_VerifyInline, output, RSA_IMAGE_SIGNATURE_SIZE,
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

#endif /* WOLFBOOT_SIGN_RSA2048 || WOLFBOOT_SIGN_RSA3072 || \
        * WOLFBOOT_SIGN_RSA4096 || WOLFBOOT_SIGN_SECONDARY_RSA2048 ||
        * WOLFBOOT_SIGN_SECONDARY_RSA3072 || WOLFBOOT_SIGN_SECONDARY_RSA4096 */

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

    ret = wc_LmsKey_Verify(&lms, sig, LMS_IMAGE_SIGNATURE_SIZE, img->sha_hash,
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

    ret = wc_XmssKey_Verify(&xmss, sig, XMSS_IMAGE_SIGNATURE_SIZE, img->sha_hash,
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
#if !defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT || \
    (defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT && \
     !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID))
    uint8_t * pubkey = NULL;
    int       pub_len = 0;
#endif
    int       sig_len = 0;
    int       verify_res = 0;

    wolfBoot_printf("info: ML-DSA %d verify_signature: pubkey %d, sig %d\n",
        ML_DSA_LEVEL, KEYSTORE_PUBKEY_SIZE, ML_DSA_IMAGE_SIGNATURE_SIZE);

#if !defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT || \
    (defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT && \
     !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID))
    pubkey = keystore_get_buffer(key_slot);

    if (pubkey == NULL) {
        wolfBoot_printf("error: ML-DSA pubkey not found\n");
        return;
    }
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    ret = wc_MlDsaKey_Init(&ml_dsa, NULL, hsmDevIdPubKey);
#else
    ret = wc_MlDsaKey_Init(&ml_dsa, NULL, INVALID_DEVID);
#endif

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

#if defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT && \
    defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID)
    /* Use key slot ID directly with wolfHSM */
#if defined(WOLFBOOT_CERT_CHAIN_VERIFY)
    /* If using certificate chain verification and we have a verified leaf key
     * ID */
    if (g_leafKeyIdValid) {
        /* Use the leaf key ID from certificate verification */
        ret = wh_Client_MlDsaSetKeyId(&ml_dsa, g_certLeafKeyId);
        wolfBoot_printf(
            "Using leaf cert public key (ID: %08x) for ML-DSA verification\n",
            (unsigned int)g_certLeafKeyId);
    }
    else {
        /* Default behavior: use the pre-configured public key ID */
        ret = wh_Client_MlDsaSetKeyId(&ml_dsa, hsmKeyIdPubKey);
    }
#else
    ret = wh_Client_MlDsaSetKeyId(&ml_dsa, hsmKeyIdPubKey);
#endif
    if (ret != 0) {
        wolfBoot_printf("error: wh_Client_MlDsaSetKeyId returned %d\n", ret);
    }
#else
    /* Make sure pub key matches parameters and import it */
    if (ret == 0) {
        ret = wc_MlDsaKey_GetPubLen(&ml_dsa, &pub_len);

        if (ret != 0 || pub_len <= 0) {
            wolfBoot_printf("error: wc_MlDsaKey_GetPubLen returned %d\n", ret);
            ret = -1;
        }
        else if (pub_len > KEYSTORE_PUBKEY_SIZE) {
            wolfBoot_printf("error: ML-DSA pub key mismatch: got %d bytes " \
                   "max %d\n", pub_len, KEYSTORE_PUBKEY_SIZE);
            ret = -1;
        }
    }

    if (ret == 0) {
        ret = wc_MlDsaKey_ImportPubRaw(&ml_dsa, pubkey, pub_len);
        if (ret != 0) {
            wolfBoot_printf("error: wc_MlDsaKey_ImportPubRaw returned: %d\n",
                            ret);
        }
    }
#endif /* !defined WOLFBOOT_ENABLE_WOLFHSM_CLIENT &&
          !defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID) */


    /* Make sure sig len matches parameters. */
    if (ret == 0) {
        ret = wc_MlDsaKey_GetSigLen(&ml_dsa, &sig_len);

        if (ret != 0 || sig_len <= 0) {
            wolfBoot_printf("error: wc_MlDsaKey_GetSigLen returned %d\n", ret);
            ret = -1;
        }
        else if (sig_len != ML_DSA_IMAGE_SIGNATURE_SIZE) {
            wolfBoot_printf("error: ML-DSA sig len mismatch: got %d bytes " \
                   "expected %d\n", sig_len, ML_DSA_IMAGE_SIGNATURE_SIZE);
            ret = -1;
        }
    }

    if (ret == 0) {
        wolfBoot_printf("info: using ML-DSA security level: %d\n",
                        ML_DSA_LEVEL);

        /* Finally verify signature. */
        ret = wc_MlDsaKey_Verify(&ml_dsa, sig, ML_DSA_IMAGE_SIGNATURE_SIZE,
                                 img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE,
                                 &verify_res);

    #ifdef WOLFBOOT_ARMORED
        if (ret == 0) {
            uint32_t v = (uint32_t)verify_res;
            uint32_t v_inv = ~v;
            if ((v == 1U) && (v_inv == 0xFFFFFFFEU) &&
                (v == (uint32_t)verify_res) &&
                (v_inv == ~(uint32_t)verify_res)) {
                wolfBoot_printf("info: wc_MlDsaKey_Verify returned OK\n");
                wolfBoot_image_confirm_signature_ok(img);
            }
            else {
                wolfBoot_printf("error: wc_MlDsaKey_Verify returned: ret=%d, "
                                "res=%d\n", ret, verify_res);
            }
        }
        else {
            wolfBoot_printf("error: wc_MlDsaKey_Verify returned: ret=%d, "
                            "res=%d\n", ret, verify_res);
        }
    #else
        if (ret == 0 && verify_res == 1) {
            wolfBoot_printf("info: wc_MlDsaKey_Verify returned OK\n");
            wolfBoot_image_confirm_signature_ok(img);
        }
        else {
            wolfBoot_printf("error: wc_MlDsaKey_Verify returned: ret=%d, "
                            "res=%d\n", ret, verify_res);
        }
    #endif
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
static uint8_t ext_hash_block[WOLFBOOT_SHA_BLOCK_SIZE] XALIGNED(4);
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
#ifdef UNIT_TEST
static uint8_t hdr_cpy[IMAGE_HEADER_SIZE] XALIGNED(4);
static int hdr_cpy_done = 0;
#else
/* use from libwolfboot.c */
extern uint8_t hdr_cpy[IMAGE_HEADER_SIZE] XALIGNED(4);
extern int hdr_cpy_done;
#endif

/**
 * @brief Get a copy of the image header.
 *
 * @param img The image to retrieve the header from.
 * @return A pointer to the copied header data.
 */
static uint8_t *fetch_hdr_cpy(struct wolfBoot_image *img)
{
    if (!hdr_cpy_done) {
        memset(hdr_cpy, 0, sizeof(hdr_cpy));
        if (ext_flash_check_read((uintptr_t)img->hdr, hdr_cpy,
                IMAGE_HEADER_SIZE) == IMAGE_HEADER_SIZE)
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

/* Initialize and hash the header part */
static int header_sha256(wc_Sha256 *sha256_ctx, struct wolfBoot_image *img)
{
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)wc_InitSha256_ex(sha256_ctx, NULL, hsmDevIdHash);
#else
    wc_InitSha256(sha256_ctx);
#endif
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha256Update(sha256_ctx, p, blksz);
        p += blksz;
    }
    return 0;
}

/**
 * @brief Calculate the SHA256 hash of the image.
 *
 * @param img The image to calculate the hash for.
 * @param hash A pointer to store the resulting SHA256 hash.
 * @return 0 on success, -1 on failure.
 */
static int image_sha256(struct wolfBoot_image *img, uint8_t *hash)
{
    uint32_t position = 0;
    uint8_t *p;
    int blksz;
    wc_Sha256 sha256_ctx;

    if (header_sha256(&sha256_ctx, img) != 0)
        return -1;
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
    wc_Sha256Free(&sha256_ctx);
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

    memset(hash, 0, SHA256_DIGEST_SIZE);
    if (!pubkey || (pubkey_sz < 0))
        return;

    wc_InitSha256(&sha256_ctx);
    wc_Sha256Update(&sha256_ctx, pubkey, (word32)pubkey_sz);
    wc_Sha256Final(&sha256_ctx, hash);
    wc_Sha256Free(&sha256_ctx);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* SHA2-256 */

#if defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>

/* Initialize and hash the header part */
static int header_sha384(wc_Sha384 *sha384_ctx, struct wolfBoot_image *img)
{
    uint16_t stored_sha_len;
    uint8_t *stored_sha, *end_sha;
    uint8_t *p;
    int blksz;
    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA384, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)wc_InitSha384_ex(sha384_ctx, NULL, hsmDevIdHash);
#else
    wc_InitSha384(sha384_ctx);
#endif
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha384Update(sha384_ctx, p, blksz);
        p += blksz;
    }
    return 0;
}


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
    uint32_t position = 0;
    uint8_t *p;
    int blksz;
    wc_Sha384 sha384_ctx;

    if (header_sha384(&sha384_ctx, img) != 0)
        return -1;
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
    wc_Sha384Free(&sha384_ctx);
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
    wc_Sha384Free(&sha384_ctx);
}
#endif /* WOLFBOOT_NO_SIGN */
#endif /* WOLFBOOT_HASH_SHA384 */

#if defined(WOLFBOOT_HASH_SHA3_384)

#include <wolfssl/wolfcrypt/sha3.h>

/* Initialize and hash the header part */
static int header_sha3_384(wc_Sha3 *sha3_ctx, struct wolfBoot_image *img)
{
    uint16_t stored_sha_len;
    uint8_t *stored_sha, *end_sha;
    uint8_t *p;
    int blksz;

    if (!img)
        return -1;

    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA3_384, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    wc_InitSha3_384(sha3_ctx, NULL, INVALID_DEVID);
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wc_Sha3_384_Update(sha3_ctx, p, blksz);
        p += blksz;
    }
    return 0;
}

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
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    wc_Sha3 sha3_ctx;

    if (header_sha3_384(&sha3_ctx, img) != 0)
        return -1;
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
    wc_Sha3_384_Free(&sha3_ctx);
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
    wc_Sha3_384_Free(&sha3_ctx);
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
        wolfBoot_printf("Partition %d header magic 0x%08x invalid at %p\n",
            img->part, (unsigned int)*magic, img->hdr);
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

#ifdef WOLFBOOT_SELF_HEADER
/**
 * @brief Open wolfBoot's own image for verification.
 *
 * This function initializes a wolfBoot_image structure to represent wolfBoot
 * itself, using the persisted self-header and the bootloader's flash location.
 * The resulting image can be passed to wolfBoot_verify_integrity() and
 * wolfBoot_verify_authenticity() to verify the bootloader.
 *
 * @param img Pointer to a wolfBoot_image structure to be initialized.
 *
 * @return 0 on success, -1 on failure (NULL pointer or invalid self-header).
 */
int wolfBoot_open_self(struct wolfBoot_image* img)
{
    uint8_t* hdr;
    int      ret;

    if (img == NULL) {
        return -1;
    }

    hdr = wolfBoot_get_self_header();
    if (hdr == NULL) {
        return -1;
    }

    ret = wolfBoot_open_self_address(img, hdr, (uint8_t*)ARCH_FLASH_OFFSET);
    if (ret == 0) {
        /* PART_SELF may be marked external for header storage, but wolfBoot
         * firmware bytes are always in internal flash at ARCH_FLASH_OFFSET. */
        img->not_ext = 1;
    }
    return ret;
}

/*
 * Directly accesses flash, suitable for non-internal-wolfBoot usage
 */
int wolfBoot_open_self_address(struct wolfBoot_image* img, uint8_t* hdr,
                               uint8_t* image)
{
    uint32_t magic;

    XMEMSET(img, 0, sizeof(struct wolfBoot_image));

    magic = *((uint32_t*)hdr);
    if (magic != WOLFBOOT_MAGIC) {
        return -1;
    }

    img->hdr     = hdr;
    img->fw_size = wolfBoot_image_size(hdr);
    img->fw_base = image;
    img->part    = PART_SELF;
    img->hdr_ok  = 1;

    return 0;
}
#endif

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

#ifdef WOLFBOOT_ELF_FLASH_SCATTER
#include "elf.h"

#ifdef ARCH_SIM
#define BASE_OFF ARCH_FLASH_OFFSET
#else
#define BASE_OFF 0
#endif

/* Maximum size of ELF header for any architecture */
typedef union {
    elf32_header elf32;
    elf64_header elf64;
} elfHeaderMaxBuf;

/*
 * Copies an arbitrary amount of data between two flash memory locations
 * (internal or external) using an intermediate RAM buffer.
 */
static int copy_flash_buffered(uintptr_t src_addr, uintptr_t dst_addr,
                               size_t total_size, int is_src_ext,
                               int is_dst_ext)
{
    size_t  bytes_copied = 0;

#ifndef BUFFER_DECLARED
#define BUFFER_DECLARED
    static uint8_t buffer[FLASHBUFFER_SIZE] XALIGNED(4);
#endif

#ifdef WOLFBOOT_FLASH_MULTI_SECTOR_ERASE
/* Mass erase destination flash in one go before writing */
#ifdef EXT_FLASH
    if (is_dst_ext) {
        ext_flash_unlock();
        ext_flash_erase(dst_addr, total_size);
        ext_flash_lock();
    }
    else
#endif
    {
        hal_flash_unlock();
        hal_flash_erase(dst_addr, total_size);
        hal_flash_lock();
    }
#endif /* WOLFBOOT_FLASH_MULTI_SECTOR_ERASE */

    /* Loop until all requested bytes are copied */
    while (bytes_copied < total_size) {
        /* Determine the size of the next chunk to copy */
        size_t remaining_bytes = total_size - bytes_copied;
        size_t chunk_size      = (remaining_bytes > FLASHBUFFER_SIZE)
                                     ? FLASHBUFFER_SIZE
                                     : remaining_bytes;

        /* Read a chunk from the source flash into the RAM buffer */
#ifdef EXT_FLASH
        if (is_src_ext) {
            ext_flash_unlock();
            ext_flash_read(src_addr + bytes_copied, buffer, chunk_size);
            ext_flash_lock();
        }
        else
#endif
        {
            memcpy(buffer, (const void*)(src_addr + bytes_copied), chunk_size);
        }

        /* Write the chunk from the RAM buffer to the destination flash */
#ifdef EXT_FLASH
        if (is_dst_ext) {
            ext_flash_unlock();
#ifndef WOLFBOOT_FLASH_MULTI_SECTOR_ERASE
            ext_flash_erase(dst_addr + bytes_copied, chunk_size);
#endif
            ext_flash_write(dst_addr + bytes_copied, buffer, chunk_size);
            ext_flash_lock();
        }
        else
#endif
        {
            hal_flash_unlock();
#ifndef WOLFBOOT_FLASH_MULTI_SECTOR_ERASE
            hal_flash_erase(dst_addr + bytes_copied, chunk_size);
#endif
            hal_flash_write(dst_addr + bytes_copied, buffer, chunk_size);
            hal_flash_lock();
        }

        /* Update the count of bytes successfully copied */
        bytes_copied += chunk_size;
    }

    /* All bytes copied successfully */
    return 0;
}

/*
 * Reads data from a given wolfBoot partition's firmware image, properly
 * handling internal/external flash.
 */
static int read_flash_fwimage(struct wolfBoot_image* img, uint32_t offset,
                              void* buffer, uint32_t size)
{
    if (img == NULL || buffer == NULL) {
        return -1;
    }
    /* Prevent reading past the end of the image */
    if ((uint64_t)offset + size > img->fw_size) {
        wolfBoot_printf(
            "ERROR: read_flash_fwimage attempt to read past fw_size! "
            "Offset %lu, Size %u, TotalSize %lu\n",
            (unsigned long)offset, size, (unsigned long)img->fw_size);
        return -1;
    }

#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        if (ext_flash_check_read((uintptr_t)img->fw_base + offset, buffer,
                                 size) < 0) {
            wolfBoot_printf(
                "ERROR: ext_flash_check_read failed at offset %lu, size %u\n",
                (unsigned long)offset, size);
            return -1;
        }
    }
    else
#endif
    {
        /* Internal flash: Direct memory access */
        memcpy(buffer, (uint8_t*)img->fw_base + offset, size);
    }
    return 0;
}

/*
 * Reads data from a raw flash address (no offset) into a RAM buffer,
 * properly handling internal/external flash.
 */
static int read_flash_addr(void* src, void* buffer, uint32_t size, int src_ext)
{
    if (src == NULL || buffer == NULL) {
        return -1;
    }

#ifdef EXT_FLASH
    if (src_ext) {
        if (ext_flash_check_read((uintptr_t)src, buffer, size) < 0) {
            wolfBoot_printf(
                "ERROR: ext_flash_check_read failed at address %p, size %u\n",
                src, size);
            return -1;
        }
    }
    else
#endif
    {
        /* Internal flash: Direct memory access */
        memcpy(buffer, src, size);
    }
    return 0;
}

/*
 * Hashes a chunk of the firmware image one SHA block at a time, properly
 * handling internal/external flash
 */
static int update_hash_flash_fwimg(wolfBoot_hash_t*       ctx,
                                   struct wolfBoot_image* img, uint32_t offset,
                                   uint32_t size)
{
    uint32_t current_offset = offset;
    uint32_t remaining_size = size;
    uint8_t read_buf[WOLFBOOT_SHA_BLOCK_SIZE] XALIGNED_STACK(4); /* Use local buffer */

    while (remaining_size > 0) {
        uint32_t read_size = (remaining_size > WOLFBOOT_SHA_BLOCK_SIZE)
                                 ? WOLFBOOT_SHA_BLOCK_SIZE
                                 : remaining_size;

        if (read_flash_fwimage(img, current_offset, read_buf, read_size) != 0) {
            wolfBoot_printf("ERROR: Failed to read image data for hashing. "
                            "Offset: %lu, Size: %u\n",
                            (unsigned long)current_offset, read_size);
            return -1;
        }

        update_hash(ctx, read_buf, read_size);

        remaining_size -= read_size;
        current_offset += read_size;
    }
    return 0;
}

/*
 * Hashes a chunk of flash memory at a given absolute address, reading one
 * SHA block at a time, properly handling internal/external flash
 */
static int update_hash_flash_addr(wolfBoot_hash_t* ctx, uintptr_t addr,
                                  uint32_t size, int src_ext)
{
    uint8_t   buffer[WOLFBOOT_SHA_BLOCK_SIZE] XALIGNED_STACK(4);
    uint32_t  remaining_size = size;
    uintptr_t current_addr   = addr;

    while (remaining_size > 0) {
        uint32_t read_size = (remaining_size > WOLFBOOT_SHA_BLOCK_SIZE)
                                 ? WOLFBOOT_SHA_BLOCK_SIZE
                                 : remaining_size;

        if (read_flash_addr((void*)current_addr, buffer, read_size, src_ext) !=
            0) {
            wolfBoot_printf(
                "ERROR: Failed to read data from address %p, size %u\n",
                (void*)current_addr, read_size);
            return -1;
        }

        update_hash(ctx, buffer, read_size);

        remaining_size -= read_size;
        current_addr += read_size;
    }

    return 0;
}

int wolfBoot_check_flash_image_elf(uint8_t part, unsigned long* entry_out)
{
    /* Open the partition containing the image */
    int                   is_elf32;
    struct wolfBoot_image boot;
    uint8_t *             elf_h;
    size_t                elf_hdr_sz = 0;
    uint32_t              len;
    uint16_t              entry_count       = 0;
    size_t                entry_off         = 0;
    size_t                ph_size           = 0;
    size_t                current_ph_offset = 0;
    int64_t               final_offset      = -1;
    uint8_t               calc_digest[WOLFBOOT_SHA_DIGEST_SIZE] XALIGNED_STACK(4);
    uint8_t*              exp_digest;
    int32_t               stored_sha_len;
    int                   i;
    int32_t               entry_out_set = 0;
    uint8_t               elfHdrBuf[sizeof(elfHeaderMaxBuf)];
    uint8_t ph_buf[sizeof(elf64_program_header)]; /* Buffer for current PH */
    uint8_t ph_next_buf[sizeof(elf64_program_header)]; /* Buffer for next PH */


    wolfBoot_hash_t ctx;
    if (wolfBoot_open_image(&boot, part) < 0) {
        return -1;
    }

    /* Initialize hash, feed the manifest header to it */
    if (header_hash(&ctx, &boot) < 0) {
        return -1;
    }

    stored_sha_len = get_header(&boot, HDR_HASH, &exp_digest);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1;
    }

    /* Get the elf header from the image into a local buffer. We may overread
     * the buffer depending on architecture */
    memset(elfHdrBuf, 0, sizeof(elfHdrBuf));
    read_flash_fwimage(&boot, 0, elfHdrBuf, sizeof(elfHeaderMaxBuf));
    elf_h = elfHdrBuf;

    if (elf_open(elf_h, &is_elf32) < 0) {
        return -1;
    }

    /* Set up common variables based on ELF type */
    if (is_elf32) {
        elf32_header* eh = (elf32_header*)elf_h;
        entry_count      = eh->ph_entry_count;
        entry_off        = eh->ph_offset;
        ph_size          = sizeof(elf32_program_header);
        if (!entry_out_set) {
            *entry_out    = eh->entry;
            entry_out_set = 1;
        }
        wolfBoot_printf("ELF: [CHECK] 32-bit, entry=0x%08X, "
                        "ph_offset=0x%08X, ph_count=%u\n",
                        (unsigned int)eh->entry, (unsigned int)entry_off, entry_count);
    }
    else { /* 64-bit ELF */
        elf64_header* eh = (elf64_header*)elf_h;
        entry_count      = eh->ph_entry_count;
        entry_off        = eh->ph_offset;
        ph_size          = sizeof(elf64_program_header);
        if (!entry_out_set) {
            *entry_out    = eh->entry;
            entry_out_set = 1;
        }
        wolfBoot_printf("ELF: [CHECK] 64-bit, entry=0x%08lx, "
                        "ph_offset=0x%08lx, ph_count=%d\n",
                        (unsigned long)eh->entry, (unsigned long)entry_off, entry_count);
    }

    elf_hdr_sz = (size_t)elf_hdr_pht_combined_size(elf_h);
    wolfBoot_printf("ELF: [CHECK] Header size: %zu bytes\n", elf_hdr_sz);

    /* Hash the elf header and program header in the image, assuming the PHT
     * immediately follows the ELF header */
    update_hash_flash_fwimg(&ctx, &boot, 0, elf_hdr_sz);

    current_ph_offset = entry_off;

    /* Calculate padding between ELF+PHT header and first segment */
    if (entry_count > 0) {
        uint64_t first_offset;
        read_flash_fwimage(&boot, current_ph_offset, ph_buf, ph_size);
        if (is_elf32) {
            first_offset = ((elf32_program_header*)ph_buf)->offset;
        }
        else {
            first_offset = ((elf64_program_header*)ph_buf)->offset;
        }

        if (first_offset > elf_hdr_sz) {
            len = first_offset - elf_hdr_sz;
            wolfBoot_printf(
                "ELF: [CHECK] Adding %d bytes padding before first segment\n",
                (int32_t)len);
            update_hash_flash_fwimg(&ctx, &boot, elf_hdr_sz, len); /* Hash actual file content */
        }
    }

    /* Walk the program header table and hash each loadable segment. */
    for (i = 0; i < entry_count; i++) {
        uint64_t paddr;
        uint64_t filesz;
        uint64_t offset;
        uint32_t type;
        uint64_t next_offset = 0; /* Initialize */

        /* read the current program header into a local buffer */
        read_flash_fwimage(&boot, current_ph_offset, ph_buf, ph_size);

        /* Extract common fields based on ELF type */
        if (is_elf32) {
            elf32_program_header* ph = (elf32_program_header*)ph_buf;
            paddr                    = ph->paddr;
            offset                   = ph->offset;
            filesz                   = ph->file_size;
            type                     = ph->type;
        }
        else { /* 64-bit */
            elf64_program_header* ph = (elf64_program_header*)ph_buf;
            paddr                    = ph->paddr;
            offset                   = ph->offset;
            filesz                   = ph->file_size;
            type                     = ph->type;
        }

        /* Handle loadable segments */
        if (type == ELF_PT_LOAD) {
            uintptr_t load_addr = (uintptr_t)(paddr + BASE_OFF);
            /* Feed the loadable parts to the hash function */
            wolfBoot_printf("ELF: [CHECK] Hashing loadable segment: "
                            "paddr = 0x%08lx, loadaddr = 0x%08lx, "
                            "offset = 0x%08lx, size = %lu\n",
                            (unsigned long)paddr, (unsigned long)load_addr,
                            (unsigned long)offset, (unsigned long)filesz);
            update_hash_flash_addr(&ctx, load_addr, (uint32_t)filesz,
                                   PART_IS_EXT(&boot));
        }
        else {
            wolfBoot_printf("ELF: [CHECK] ERROR: non-loadable segment\n");
            return -1;
        }

        /* Add padding until next program header, if any. */
        if (i < entry_count - 1) {
            read_flash_fwimage(&boot, current_ph_offset + ph_size, ph_next_buf,
                               ph_size);
            if (is_elf32) {
                next_offset = ((elf32_program_header*)ph_next_buf)->offset;
            }
            else {
                next_offset = ((elf64_program_header*)ph_next_buf)->offset;
            }

            if (next_offset > (offset + filesz)) {
                uint32_t padding = next_offset - (offset + filesz);
                wolfBoot_printf("ELF: [CHECK] Adding padding: %u bytes (from "
                                "0x%08lx to 0x%08lx)\n",
                                padding, (unsigned long)(offset + filesz),
                                (unsigned long)next_offset);
                update_hash_flash_fwimg(&ctx, &boot, offset + filesz, padding); /* Hash actual file content */
            }
        }

        final_offset =
            offset + filesz; /* Track end offset of last processed segment */
        current_ph_offset += ph_size;
    } /* End of program header loop */

    if (final_offset < 0 && entry_count > 0) {
        /* Should have processed at least one segment if entry_count > 0 */
        wolfBoot_printf("ELF: [CHECK] Error determining final offset\n");
        return -1;
    }
    else if (final_offset < 0 && entry_count == 0) {
        /* No program headers, hash only ELF header + PHT */
        final_offset = elf_hdr_sz;
    }

    /* Check if final offset is valid */
    if (final_offset > (int64_t)boot.fw_size) {
        wolfBoot_printf("ELF: [CHECK] Final offset (%d) exceeds image size (%d)\n",
                        (int32_t)final_offset, (int32_t)boot.fw_size);
        return -1;
    }

    /* Hash any trailing data after the last segment/header */
    len = boot.fw_size - final_offset;
    if (len > 0) {
        wolfBoot_printf("ELF: [CHECK] Hashing %u bytes of trailing data from "
                        "offset 0x%llX\n",
                        len, (unsigned long long)final_offset);
        update_hash_flash_fwimg(&ctx, &boot, final_offset, len);
    }


    /* Finalize SHA calculation */
    final_hash(&ctx, calc_digest);
    if (memcmp(calc_digest, exp_digest, WOLFBOOT_SHA_DIGEST_SIZE) != 0) {
        wolfBoot_printf("ELF: [CHECK] SHA verification FAILED\n");
        wolfBoot_printf(
            "ELF: [CHECK] Expected   %02x%02x%02x%02x%02x%02x%02x%02x\n",
            exp_digest[0], exp_digest[1], exp_digest[2], exp_digest[3],
            exp_digest[4], exp_digest[5], exp_digest[6], exp_digest[7]);
        wolfBoot_printf(
            "ELF: [CHECK] Calculated %02x%02x%02x%02x%02x%02x%02x%02x\n",
            calc_digest[0], calc_digest[1], calc_digest[2], calc_digest[3],
            calc_digest[4], calc_digest[5], calc_digest[6], calc_digest[7]);
        return -2;
    }
    wolfBoot_printf("ELF: [CHECK] Verification successful\n");
    return 0;
}

int wolfBoot_load_flash_image_elf(int part, unsigned long* entry_out, int ext_flash)
{
    const unsigned char*  image;
    int                   is_elf32;
    uint16_t              entry_count;
    size_t                entry_off;
    size_t                ph_size;
    int                   i;
    const void*           eh;
    struct wolfBoot_image boot;
    uint8_t               elfHdrBuf[sizeof(elfHeaderMaxBuf)];

    if (wolfBoot_open_image(&boot, part) < 0) {
        return -1;
    }
    image = boot.fw_base;

    /* Get the elf header from the image into a local buffer. We may overread
     * the buffer depending on architecture */
    memset(elfHdrBuf, 0, sizeof(elfHdrBuf));
    read_flash_fwimage(&boot, 0, elfHdrBuf, sizeof(elfHeaderMaxBuf));
    if (elf_open(elfHdrBuf, &is_elf32) != 0) {
        return -1;
    }

    /* Set up header pointers based on ELF type */
    if (is_elf32) {
        eh          = (const elf32_header*)elfHdrBuf;
        entry_count = ((const elf32_header*)eh)->ph_entry_count;
        entry_off   = ((const elf32_header*)eh)->ph_offset;
        *entry_out  = (unsigned long)((const elf32_header*)eh)->entry;

        wolfBoot_printf("ELF: [STORE] 32-bit, entry=0x%08lx, "
                        "ph_offset=0x%08lx, ph_count=%d\n",
                        (unsigned long)((const elf32_header*)eh)->entry,
                        (unsigned long)entry_off, entry_count);
    }
    else {
        eh          = (const elf64_header*)elfHdrBuf;
        entry_count = ((const elf64_header*)eh)->ph_entry_count;
        entry_off   = ((const elf64_header*)eh)->ph_offset;
        *entry_out  = (unsigned long)((const elf64_header*)eh)->entry;

        wolfBoot_printf("ELF: [STORE] 64-bit, entry=0x%08lx, "
                        "ph_offset=0x%08lx, ph_count=%d\n",
                        (unsigned long)((const elf64_header*)eh)->entry,
                        (unsigned long)entry_off, entry_count);
    }

    /* Walk the program header table and store each loadable segment */
    for (i = 0; i < entry_count; ++i) {
        unsigned long paddr, filesz, offset;
        int           is_loadable;
        uintptr_t     load_addr;

        /* Read the current program header into a local buffer */
        if (is_elf32) {
            elf32_program_header p32;
            read_flash_fwimage(&boot, entry_off, &p32, sizeof(p32));
            is_loadable = (p32.type == ELF_PT_LOAD);
            paddr       = (unsigned long)p32.paddr;
            offset      = (unsigned long)p32.offset;
            filesz      = (unsigned long)p32.file_size;
            ph_size     = sizeof(p32);
        }
        else {
            elf64_program_header p64;
            read_flash_fwimage(&boot, entry_off, &p64, sizeof(p64));
            is_loadable = (p64.type == ELF_PT_LOAD);
            paddr       = (unsigned long)p64.paddr;
            offset      = (unsigned long)p64.offset;
            filesz      = (unsigned long)p64.file_size;
            ph_size     = sizeof(p64);
        }
        /* Skip non-loadable segments */
        if (!is_loadable) {
            wolfBoot_printf("ELF: [STORE] ERROR: non-loadable segment\n");
            return -1;
        }

        load_addr = (uintptr_t)(paddr + BASE_OFF);
        wolfBoot_printf("ELF: [STORE] Writing loadable segment: "
                        "loadaddr=0x%08lx, offset=0x%08lx, size=%lu\n",
                        (unsigned long)load_addr, offset, filesz);
        copy_flash_buffered((uintptr_t)(image + offset), load_addr, filesz,
                            ext_flash, ext_flash);

        entry_off += ph_size;
    }

    wolfBoot_printf("ELF: [STORE] Image loading complete\n");
    return 0;
}

#undef BASE_OFF

#endif

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
#if defined(WOLFBOOT_CERT_CHAIN_VERIFY) && \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
     defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER))
    uint8_t* cert_chain;
    uint16_t cert_chain_size;
    int32_t  cert_verify_result;
    int hsm_ret;

    /* Reset certificate chain usage for this verification */
    g_leafKeyIdValid = 0;
#endif

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
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
        extern int hal_renesas_init(void);
        int rc = hal_renesas_init();
        if (rc != 0) {
            wolfBoot_printf("hal_renesas_init failed! %d\n", rc);
            return rc;
        }
        key_slot = 0;

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
      defined(WOLFBOOT_USE_WOLFHSM_PUBKEY_ID)
        /* Don't care about the key slot, we are using a fixed wolfHSM keyId */
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

#if defined(WOLFBOOT_CERT_CHAIN_VERIFY) && \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
     defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER))
    /* Check for certificate chain in the image header */
    cert_chain_size = get_header(img, HDR_CERT_CHAIN, &cert_chain);
    if (cert_chain_size > 0) {
        wolfBoot_printf("Found certificate chain (%d bytes)\n",
                        cert_chain_size);

        /* Verify certificate chain using wolfHSM's verification API. Use DMA if
         * available in the wolfHSM configuration */
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)
#if defined(WOLFHSM_CFG_DMA)
        wolfBoot_printf(
            "verifying cert chain and caching leaf pubkey (using DMA)\n");
        hsm_ret = wh_Client_CertVerifyDmaAndCacheLeafPubKey(
            &hsmClientCtx, cert_chain, cert_chain_size, hsmNvmIdCertRootCA,
            WH_NVM_FLAGS_USAGE_VERIFY, &g_certLeafKeyId, &cert_verify_result);
#else
        wolfBoot_printf("verifying cert chain and caching leaf pubkey\n");
        hsm_ret = wh_Client_CertVerifyAndCacheLeafPubKey(
            &hsmClientCtx, cert_chain, cert_chain_size, hsmNvmIdCertRootCA,
            WH_NVM_FLAGS_USAGE_VERIFY, &g_certLeafKeyId, &cert_verify_result);
#endif
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
        wolfBoot_printf("verifying cert chain and caching leaf pubkey\n");
        hsm_ret = wh_Server_CertVerify(
            &hsmServerCtx, cert_chain, cert_chain_size, hsmNvmIdCertRootCA,
            WH_CERT_FLAGS_CACHE_LEAF_PUBKEY, WH_NVM_FLAGS_USAGE_VERIFY,
            &g_certLeafKeyId);
        if (hsm_ret == WH_ERROR_OK) {
            cert_verify_result = 0;
        }
        wolfBoot_printf("wh_Server_CertVerify returned %d\n", hsm_ret);
#endif

        /* Error or verification failure results in standard auth check failure
         * path */
        if (hsm_ret != 0 || cert_verify_result != 0) {
            wolfBoot_printf("Certificate chain verification failed: "
                            "hsm_ret=%d, verify_result=%d\n",
                            hsm_ret, cert_verify_result);
            return -1;
        }

        wolfBoot_printf("Certificate chain verified, using leaf key ID: %08x\n",
                        (unsigned int)g_certLeafKeyId);

        /* Set flag to use the leaf certificate's public key for signature
         * verification later */
        g_leafKeyIdValid = 1;
    }
#endif

    /* wolfBoot_verify_signature_ecc() does not return the result directly.
     * A call to wolfBoot_image_confirm_signature_ok() is required in order to
     * confirm that the signature verification is OK.
     *
     * only a call to wolfBoot_image_confirm_signature_ok() sets
     * img->signature_ok to 1.
     *
     */
    wolfBoot_verify_signature_primary(key_slot, img, stored_signature);
    (void)stored_signature_size;

#ifdef WOLFBOOT_ARMORED
#define SIG_OK(imgp) (((imgp)->signature_ok == 1) && \
                      ((imgp)->not_signature_ok == ~(uint32_t)1))
#else
#define SIG_OK(imgp) ((imgp)->signature_ok == 1)
#endif

#ifdef SIGN_HYBRID
    if (SIG_OK(img)) {
        uint8_t *stored_secondary_signature;
        uint16_t stored_secondary_signature_size;
        uint16_t expected_secondary_signature_size = 0;
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
            stored_secondary_signature_size = get_header(img,
                    HDR_SECONDARY_SIGNATURE, &stored_secondary_signature);
            if (stored_secondary_signature_size == 0 ||
                    stored_secondary_signature == NULL) {
                return -1;
            }
#if defined(WOLFBOOT_SIGN_SECONDARY_ED25519)
            expected_secondary_signature_size = ED25519_IMAGE_SIGNATURE_SIZE;
#elif defined(WOLFBOOT_SIGN_SECONDARY_ED448)
            expected_secondary_signature_size = ED448_IMAGE_SIGNATURE_SIZE;
#elif defined (WOLFBOOT_SIGN_SECONDARY_RSA2048) || \
      defined (WOLFBOOT_SIGN_SECONDARY_RSA3072) || \
      defined (WOLFBOOT_SIGN_SECONDARY_RSA4096) || \
      defined (WOLFBOOT_SIGN_SECONDARY_RSA2048ENC) || \
      defined (WOLFBOOT_SIGN_SECONDARY_RSA3072ENC) || \
      defined (WOLFBOOT_SIGN_SECONDARY_RSA4096ENC)
            expected_secondary_signature_size = RSA_IMAGE_SIGNATURE_SIZE;
#elif defined (WOLFBOOT_SIGN_SECONDARY_ECC256) || \
      defined (WOLFBOOT_SIGN_SECONDARY_ECC384) || \
      defined (WOLFBOOT_SIGN_SECONDARY_ECC521)
            expected_secondary_signature_size = ECC_IMAGE_SIGNATURE_SIZE;
#elif defined(WOLFBOOT_SIGN_SECONDARY_LMS)
            expected_secondary_signature_size = LMS_IMAGE_SIGNATURE_SIZE;
#elif defined(WOLFBOOT_SIGN_SECONDARY_XMSS)
            expected_secondary_signature_size = XMSS_IMAGE_SIGNATURE_SIZE;
#elif defined(WOLFBOOT_SIGN_SECONDARY_ML_DSA)
            expected_secondary_signature_size = ML_DSA_IMAGE_SIGNATURE_SIZE;
#endif
            if (expected_secondary_signature_size == 0 ||
                    stored_secondary_signature_size !=
                    expected_secondary_signature_size) {
                return -1;
            }
            wolfBoot_printf("Verification of hybrid signature\n");
            wolfBoot_verify_signature_secondary(key_slot, img,
                    stored_secondary_signature);
            wolfBoot_printf("Done.\n");
        }
    }
#endif
    if (SIG_OK(img)) {
        return 0;
    }
    return -2;
#undef SIG_OK
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
