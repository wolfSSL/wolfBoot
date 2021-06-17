/* image.c
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

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_drv.h"

#include <wolfssl/wolfcrypt/settings.h>

#ifdef WOLFBOOT_TPM
#include <stdlib.h>
#include <string.h>
#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_wrap.h"
static WOLFTPM2_DEV wolftpm_dev;
#endif /* WOLFBOOT_TPM */

#ifdef WOLFBOOT_SIGN_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>

static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret, res;
    ed25519_key ed;
    ret = wc_ed25519_init(&ed);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }
    ret = wc_ed25519_import_public(KEY_BUFFER, KEY_LEN, &ed);
    if (ret < 0) {
        /* Failed to import ed25519 key */
        return -1;
    }
    ret = wc_ed25519_verify_msg(sig, IMAGE_SIGNATURE_SIZE, hash, WOLFBOOT_SHA_DIGEST_SIZE, &res, &ed);
    if ((ret < 0) || (res == 0)) {
        return -1;
    }
    return 0;
}

#endif /* WOLFBOOT_SIGN_ED25519 */

#ifdef WOLFBOOT_SIGN_ECC256
#include <wolfssl/wolfcrypt/ecc.h>
#define ECC_KEY_SIZE  32
static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret, verify_res = 0;
#ifdef WOLFBOOT_TPM
    WOLFTPM2_KEY tpmKey;
    #ifdef DEBUG
    const char* errStr;
    #endif

    /* Load public key into TPM */
    memset(&tpmKey, 0, sizeof(tpmKey));
    ret = wolfTPM2_LoadEccPublicKey(&wolftpm_dev, &tpmKey, TPM_ECC_NIST_P256,
            KEY_BUFFER, ECC_KEY_SIZE,
            KEY_BUFFER + ECC_KEY_SIZE, ECC_KEY_SIZE);
    if (ret < 0)
        return -1;
    ret = wolfTPM2_VerifyHashScheme(&wolftpm_dev, &tpmKey, sig, IMAGE_SIGNATURE_SIZE,
        hash, WOLFBOOT_SHA_DIGEST_SIZE, TPM_ALG_ECDSA, TPM_ALG_SHA256);
    wolfTPM2_UnloadHandle(&wolftpm_dev, &tpmKey.handle);
    if (ret == 0) {
        verify_res = 1; /* TPM does hash verify compare */
    }
    else {
    #ifdef DEBUG
        /* retrieve error string (for debugging) */
        errStr = wolfTPM2_GetRCString(ret);
        (void)errStr;
    #endif
        ret = -1;
    }
#else
    /* wolfCrypt software ECC verify */
    mp_int r, s;
    ecc_key ecc;

    ret = wc_ecc_init(&ecc);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }

    /* Import public key */
    ret = wc_ecc_import_unsigned(&ecc, (byte*)KEY_BUFFER,
        (byte*)(KEY_BUFFER + ECC_KEY_SIZE), NULL, ECC_SECP256R1);
    if ((ret < 0) || ecc.type != ECC_PUBLICKEY) {
        /* Failed to import ecc key */
        return -1;
    }

    /* Import signature into r,s */
    mp_init(&r);
    mp_init(&s);
    mp_read_unsigned_bin(&r, sig, ECC_KEY_SIZE);
    mp_read_unsigned_bin(&s, sig + ECC_KEY_SIZE, ECC_KEY_SIZE);
    ret = wc_ecc_verify_hash_ex(&r, &s, hash, WOLFBOOT_SHA_DIGEST_SIZE, &verify_res, &ecc);
#endif /* WOLFBOOT_TPM */
    if (ret < 0 || verify_res == 0)
        ret = -1;
    else 
        ret = 0;
    return ret;
}
#endif /* WOLFBOOT_SIGN_ECC256 */

#if defined(WOLFBOOT_SIGN_RSA2048) || defined (WOLFBOOT_SIGN_RSA4096)
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/rsa.h>

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

static int wolfBoot_verify_signature(uint8_t *hash, uint8_t *sig)
{
    int ret;
    uint8_t output[IMAGE_SIGNATURE_SIZE];
    int output_sz = sizeof(output);
    uint8_t* digest_out = NULL;
#ifdef WOLFBOOT_TPM
    WOLFTPM2_KEY tpmKey;
    const byte *n = NULL, *e = NULL;
    word32 nSz = 0, eSz = 0, inOutIdx = 0;
    #ifdef DEBUG
        const char* errStr;
    #endif

    /* Extract DER RSA key struct */
    ret = wc_RsaPublicKeyDecode_ex(KEY_BUFFER, &inOutIdx, KEY_LEN, &n, &nSz, &e, &eSz);
    if (ret < 0)
        return -1;

    /* Load public key into TPM */
    memset(&tpmKey, 0, sizeof(tpmKey));
    ret = wolfTPM2_LoadRsaPublicKey_ex(&wolftpm_dev, &tpmKey, n, nSz, 
        *((word32*)e), TPM_ALG_NULL, TPM_ALG_SHA256);
    if (ret != 0) {
    #ifdef DEBUG
        /* retrieve error string (for debugging) */
        errStr = wolfTPM2_GetRCString(ret);
        (void)errStr;
    #endif
        return -1;
    }
    
    /* Perform public decrypt and manually un-pad */
    ret = wolfTPM2_RsaEncrypt(&wolftpm_dev, &tpmKey,
        TPM_ALG_NULL, /* no padding */
        sig, IMAGE_SIGNATURE_SIZE,
        output, &output_sz);
    if (ret != 0) {
    #ifdef DEBUG
        /* retrieve error string (for debugging) */
        errStr = wolfTPM2_GetRCString(ret);
        (void)errStr;
    #endif
        ret = -1;
    }
    else {
        /* Perform PKCSv1.5 UnPadding */
        ret = RsaUnPad(output, output_sz, &digest_out);
    }
    wolfTPM2_UnloadHandle(&wolftpm_dev, &tpmKey.handle);

#else
    /* wolfCrypt software RSA verify */
    struct RsaKey rsa;
    word32 in_out = 0;

    ret = wc_InitRsaKey(&rsa, NULL);
    if (ret < 0) {
        /* Failed to initialize key */
        return -1;
    }
    /* Import public key */
    ret = wc_RsaPublicKeyDecode((byte*)KEY_BUFFER, &in_out, &rsa, KEY_LEN);
    if (ret < 0) {
        /* Failed to import rsa key */
        wc_FreeRsaKey(&rsa);
        return -1;
    }

    XMEMCPY(output, sig, IMAGE_SIGNATURE_SIZE);
    ret = wc_RsaSSL_VerifyInline(output, IMAGE_SIGNATURE_SIZE, &digest_out, &rsa);
#endif /* WOLFBOOT_TPM */

#ifndef NO_RSA_SIG_ENCODING
    if (ret > WOLFBOOT_SHA_DIGEST_SIZE) {
        /* larger result indicates it might have an ASN.1 encoded header */
        ret = RsaDecodeSignature(&digest_out, ret);
    }
#endif
    if (ret == WOLFBOOT_SHA_DIGEST_SIZE && digest_out &&
            memcmp(digest_out, hash, ret) == 0) {
        return 0;
    }

    return ret;
}
#endif /* WOLFBOOT_SIGN_RSA2048 || WOLFBOOT_SIGN_RSA4096 */


static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr);

static uint16_t get_header(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr)
{
    if (PART_IS_EXT(img))
        return get_header_ext(img, type, ptr);
    else
        return wolfBoot_find_header(img->hdr + IMAGE_HEADER_OFFSET, type, ptr);
}

#ifdef EXT_FLASH
static uint8_t ext_hash_block[WOLFBOOT_SHA_BLOCK_SIZE];
#endif
static uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
static uint8_t *get_sha_block(struct wolfBoot_image *img, uint32_t offset)
{
    if (offset > img->fw_size)
        return NULL;
#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        ext_flash_check_read((uintptr_t)(img->fw_base) + offset, ext_hash_block, WOLFBOOT_SHA_BLOCK_SIZE);
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

static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr)
{
    return wolfBoot_find_header(fetch_hdr_cpy(img) + IMAGE_HEADER_OFFSET, type, ptr);
}

#else
#   define fetch_hdr_cpy(i) ((uint8_t *)0)
static uint16_t get_header_ext(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr) { return 0; }
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
#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_HASH_TPM)
    const char usageAuth[] = "wolfBoot TPM Usage Auth";
    uint8_t *stored_sha, *end_sha;
    uint16_t stored_sha_len;
    uint8_t *p;
    int blksz;
    uint32_t position = 0;
    WOLFTPM2_HASH tpmHash;
    uint32_t hashSz = WOLFBOOT_SHA_DIGEST_SIZE;
    int rc;
    if (!img)
        return -1;
    p = get_img_hdr(img);
    stored_sha_len = get_header(img, HDR_SHA256, &stored_sha);
    if (stored_sha_len != WOLFBOOT_SHA_DIGEST_SIZE)
        return -1;
    memset(&tpmHash, 0, sizeof(tpmHash));
    rc = wolfTPM2_HashStart(&wolftpm_dev, &tpmHash, TPM_ALG_SHA256,
        (const byte*)usageAuth, sizeof(usageAuth)-1);
    if (rc != 0)
        return -1;
    end_sha = stored_sha - (2 * sizeof(uint16_t)); /* Subtract 2 Type + 2 Len */
    while (p < end_sha) {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (end_sha - p < blksz)
            blksz = end_sha - p;
        wolfTPM2_HashUpdate(&wolftpm_dev, &tpmHash, p, blksz);
        p += blksz;
    }
    do {
        p = get_sha_block(img, position);
        if (p == NULL)
            break;
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if (position + blksz > img->fw_size)
            blksz = img->fw_size - position;
        wolfTPM2_HashUpdate(&wolftpm_dev, &tpmHash, p, blksz);
        position += blksz;
    } while(position < img->fw_size);
    return wolfTPM2_HashFinish(&wolftpm_dev, &tpmHash, hash, (word32*)&hashSz);
#else
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
#endif /* WOLFBOOT_TPM && WOLFBOOT_HASH_TPM */
}

static void key_sha256(uint8_t *hash)
{
#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_HASH_TPM)
    int blksz, rc;
    unsigned int i = 0;
    const char usageAuth[] = "wolfBoot TPM Usage Auth";
    uint32_t hashSz = WOLFBOOT_SHA_DIGEST_SIZE;
    WOLFTPM2_HASH tpmHash;
    memset(&tpmHash, 0, sizeof(tpmHash));
    rc = wolfTPM2_HashStart(&wolftpm_dev, &tpmHash, TPM_ALG_SHA256,
        (const byte*)usageAuth, sizeof(usageAuth)-1);
    if (rc != 0)
        return;
    while(i < KEY_LEN)
    {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > KEY_LEN)
            blksz = KEY_LEN - i;
        wolfTPM2_HashUpdate(&wolftpm_dev, &tpmHash, KEY_BUFFER + i, blksz);
        i += blksz;
    }
    wolfTPM2_HashFinish(&wolftpm_dev, &tpmHash, hash, (word32*)&hashSz);
#else
    int blksz;
    unsigned int i = 0;
    wc_Sha256 sha256_ctx;
    wc_InitSha256(&sha256_ctx);
    while(i < KEY_LEN)
    {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > KEY_LEN)
            blksz = KEY_LEN - i;
        wc_Sha256Update(&sha256_ctx, (KEY_BUFFER + i), blksz);
        i += blksz;
    }
    wc_Sha256Final(&sha256_ctx, hash);
#endif /* WOLFBOOT_TPM && WOLFBOOT_HASH_TPM */
}
#endif /* SHA2-256 */

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

static void key_sha3_384(uint8_t *hash)
{
    int blksz;
    unsigned int i = 0;
    wc_Sha3 sha3_ctx;
    wc_InitSha3_384(&sha3_ctx, NULL, INVALID_DEVID);
    while(i < KEY_LEN)
    {
        blksz = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((i + blksz) > KEY_LEN)
            blksz = KEY_LEN - i;
        wc_Sha3_384_Update(&sha3_ctx, (KEY_BUFFER + i), blksz);
        i += blksz;
    }
    wc_Sha3_384_Final(&sha3_ctx, hash);
}
#endif /* SHA3-384 */

#ifdef WOLFBOOT_TPM

static int TPM2_IoCb(TPM2_CTX* ctx, const byte* txBuf, byte* rxBuf,
    word16 xferSz, void* userCtx)
{
    (void)userCtx;
    (void)ctx;
    word16 i;
    spi_cs_on(SPI_CS_TPM);
    memset(rxBuf, 0, xferSz);
    for (i = 0; i < xferSz; i++)
    {
        spi_write(txBuf[i]);
        rxBuf[i] = spi_read();
    }
    spi_cs_off(SPI_CS_TPM);
    /*
    printf("\r\nSPI TX: ");
    printbin(txBuf, xferSz);
    printf("SPI RX: ");
    printbin(rxBuf, xferSz);
    printf("\r\n");
    */
    return 0;
}

#if defined(WOLFBOOT_TPM) && defined(WOLFBOOT_MEASURED_BOOT)
static int measure_boot(uint8_t *hash)
{
    int rc = -1;
    PCR_Extend_In pcrExtend;
#ifdef DEBUG
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

#ifdef DEBUG
    /* Test prcRead helps debug TPM communication and print PCR value in gdb */
    XMEMSET(&pcrReadCmd, 0, sizeof(pcrReadCmd));
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
    spi_init(0,0);

    /* Init the TPM2 device */
    rc = wolfTPM2_Init(&wolftpm_dev, TPM2_IoCb, NULL);
    if (rc != 0)  {
        return rc;
    }

    /* Get device capabilities + options */
    rc = wolfTPM2_GetCapabilities(&wolftpm_dev, &caps);
    if (rc != 0)  {
        return rc;
    }
    return 0;
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

int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part)
{
    uint32_t *magic;
    uint32_t *size;
    uint8_t *image;
    if (!img)
        return -1;

#ifdef EXT_FLASH
    hdr_cpy_done = 0; /* reset hdr "open" flag */
#endif

    memset(img, 0, sizeof(struct wolfBoot_image));
    img->part = part;
    if (part == PART_SWAP) {
        img->hdr_ok = 1;
        img->hdr = (void*)WOLFBOOT_PARTITION_SWAP_ADDRESS;
        img->fw_base = img->hdr;
        img->fw_size = WOLFBOOT_SECTOR_SIZE;
        return 0;
    }
#ifdef MMU
    if (part == PART_DTS_BOOT || part == PART_DTS_UPDATE) {
        img->hdr = (part == PART_DTS_BOOT) ? (void*)WOLFBOOT_DTS_BOOT_ADDRESS
                                           : (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
        if (PART_IS_EXT(img))
            image = fetch_hdr_cpy(img);
        else
            image = (uint8_t*)img->hdr;
        if (*((uint32_t*)image) != UBOOT_FDT_MAGIC)
            return -1;
        img->hdr_ok = 1;
        img->fw_base = img->hdr;
        /* DTS data is big endian */
        size = (uint32_t*)(image + sizeof(uint32_t));
        img->fw_size = (((*size & 0x000000FF) << 24) |
                        ((*size & 0x0000FF00) <<  8) |
                        ((*size & 0x00FF0000) >>  8) |
                        ((*size & 0xFF000000) >> 24));
        return 0;
    }
#endif
    if (part == PART_BOOT) {
        img->hdr = (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    } else if (part == PART_UPDATE) {
        img->hdr = (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    } else
        return -1;

    /* fetch header address
     * (or copy from external device to a local buffer via fetch_hdr_cpy)
     */
    if (PART_IS_EXT(img))
        image = fetch_hdr_cpy(img);
    else
        image = (uint8_t *)img->hdr;

    magic = (uint32_t *)(image);
    if (*magic != WOLFBOOT_MAGIC)
        return -1;
    size = (uint32_t *)(image + sizeof (uint32_t));

    if (im2n(*size) > (WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE))
       return -1;
    img->hdr_ok = 1;
    img->fw_size = im2n(*size);
    img->fw_base = img->hdr + IMAGE_HEADER_SIZE;
    img->trailer = img->hdr + WOLFBOOT_PARTITION_SIZE;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    uint8_t *stored_sha;
    uint16_t stored_sha_len;
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

int wolfBoot_verify_authenticity(struct wolfBoot_image *img)
{
    int ret;
    uint8_t *stored_signature;
    uint16_t stored_signature_size;
    uint8_t *pubkey_hint;
    uint16_t pubkey_hint_size;
    uint8_t *image_type_buf;
    uint16_t image_type;
    uint16_t image_type_size;

    stored_signature_size = get_header(img, HDR_SIGNATURE, &stored_signature);
    if (stored_signature_size != IMAGE_SIGNATURE_SIZE)
       return -1;
    pubkey_hint_size = get_header(img, HDR_PUBKEY, &pubkey_hint);
    if (pubkey_hint_size == WOLFBOOT_SHA_DIGEST_SIZE) {
        key_hash(digest);
        if (memcmp(digest, pubkey_hint, WOLFBOOT_SHA_DIGEST_SIZE) != 0)
            return -1;
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
    if ((ret = wolfBoot_verify_signature(img->sha_hash, stored_signature)) != 0)
        return ret;
    img->signature_ok = 1;
    return 0;
}

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
