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

/* Globals */
static uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];

/* Forward declarations */
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

static void wolfBoot_verify_signature(uint8_t key_slot,
        struct wolfBoot_image *img, uint8_t *sig)
{
    int ret, verify_res = 0;
    uint8_t *pubkey = keystore_get_buffer(key_slot);
    int point_sz = keystore_get_size(key_slot)/2;
#ifdef WOLFBOOT_TPM
    WOLFTPM2_KEY tpmKey;
#else
    ecc_key ecc;
    mp_int r, s;
#endif

    if (pubkey == NULL || point_sz <= 0)
        return;

#ifdef WOLFBOOT_TPM
    /* TODO: Check ECC Root of Trust in TPM */

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
    int output_sz = sizeof(output);
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

    if ((pubkey_sz < 0) || (pubkey == NULL))
        return;

#ifdef WOLFBOOT_TPM
    /* TODO: Check RSA Root of Trust in TPM */

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
    if (ret == WOLFBOOT_SHA_DIGEST_SIZE && img && digest_out)
        RSA_VERIFY_HASH(img, digest_out);
}
#endif /* WOLFBOOT_SIGN_RSA2048 || WOLFBOOT_SIGN_3072 || \
        * WOLFBOOT_SIGN_RSA4096 */


static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type,
        uint8_t **ptr);

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
#if !defined(ARCH_SIM) && !defined(WOLFTPM_MMIO)

#ifdef WOLFTPM_DEBUG_IO
#define LINE_LEN 16
static void wolfBoot_PrintBin(const byte* buffer, word32 length)
{
    word32 i, sz;

    if (!buffer) {
        wolfBoot_printf("\tNULL");
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
#endif /* WOLFTPM_DEBUG_IO */

#ifdef WOLFTPM_ADV_IO
static int TPM2_IoCb(TPM2_CTX* ctx, int isRead, word32 addr, byte* buf,
    word16 size, void* userCtx)
#else
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
#endif

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
#endif

#ifdef WOLFTPM_ADV_IO
    if (isRead) {
        memcpy(buf, &rxBuf[TPM_TIS_HEADER_SZ], size);
    }
#endif

    return ret;
}
#endif /* !ARCH_SIM */

#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_MEASURED_BOOT)
static int measure_boot(struct wolfBoot_image *img)
{
    int rc = -1;
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
    if (rc == TPM_RC_SUCCESS) {
        rc = 0;
    }

#ifdef WOLFBOOT_DEBUG_TPM
    /* Test prcRead helps debug TPM communication and print PCR value in gdb */
    memset(&pcrReadCmd, 0, sizeof(pcrReadCmd));
    TPM2_SetupPCRSel(&pcrReadCmd.pcrSelectionIn, TPM_ALG_SHA256,
                     pcrExtend.pcrHandle);
    TPM2_PCR_Read(&pcrReadCmd, &pcrReadResp);
#endif

    return rc;
}
#endif /* WOLFBOOT_MEASURED_BOOT */

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
        wolfBoot_printf("TPM Create SRK or Start Session rrror %d (%s)!\n",
            rc, wolfTPM2_GetRCString(rc));
        wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_session.handle);
        wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_srk.handle);
    }
#endif

    return rc;
}

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

uint32_t wolfBoot_image_size(uint8_t *image)
{
    uint32_t *size = (uint32_t *)(image + sizeof (uint32_t));
    return im2n(*size);
}

int wolfBoot_open_image_address(struct wolfBoot_image *img, uint8_t *image)
{
    uint32_t *magic = (uint32_t *)(image);
    if (*magic != WOLFBOOT_MAGIC) {
        wolfBoot_printf("Boot header magic 0x%08x invalid at %p\n",
            *magic, image);
        return -1;
    }
    img->fw_size = wolfBoot_image_size(image);
    wolfBoot_printf("Image size %d\n", img->fw_size);
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if (img->fw_size > (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE)) {
        wolfBoot_printf("Image size %d > max %d\n",
            img->fw_size, (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE));
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
#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_MEASURED_BOOT)
    /*
     * TPM measurement must be performed regardless of the
     * verification outcome afterwards, because the purpose
     * of a Measured Boot is to record the current boot state
     */
    if (measure_boot(digest) != 0)
        return -1;
#endif
    if (memcmp(digest, stored_sha, stored_sha_len) != 0)
        return -1;
    img->sha_ok = 1;
    img->sha_hash = stored_sha;
    return 0;
}

#ifdef WOLFBOOT_NO_SIGN
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

/* Peek at image offset and return static pointer */
/* sz: optional and returns length of peek */
uint8_t* wolfBoot_peek_image(struct wolfBoot_image *img, uint32_t offset,
    uint32_t* sz)
{
    uint8_t* p = get_sha_block(img, offset);
    if (sz)
        *sz = WOLFBOOT_SHA_BLOCK_SIZE;
    return p;
}

#if !defined(WOLFBOOT_NO_SIGN) && !defined(WOLFBOOT_RENESAS_SCEPROTECT)
static int keyslot_id_by_sha(const uint8_t *hint)
{
#ifdef STAGE1_AUTH
    /* Override global */
    uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
#endif
    int id = 0;
    for (id = 0; id < keystore_num_pubkeys(); id++) {
        key_hash(id, digest);
        if (memcmp(digest, hint, WOLFBOOT_SHA_DIGEST_SIZE) == 0)
            return id;
    }
    return -1;
}
#endif
