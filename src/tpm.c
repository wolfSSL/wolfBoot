/* tpm.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 * @file tpm.c
 * @brief This file contains functions related to TPM handling.
 */

#ifdef WOLFBOOT_TPM

#include <stdlib.h>

#include "image.h"
#include "printf.h"
#include "spi_drv.h"
#include "tpm.h"
#include "wolftpm/tpm2_tis.h" /* for TIS header size and wait state */

WOLFTPM2_DEV     wolftpm_dev;
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
WOLFTPM2_SESSION wolftpm_session;
WOLFTPM2_KEY     wolftpm_srk;
#endif

#if defined(WOLFBOOT_TPM_KEYSTORE) && !defined(WOLFBOOT_TPM)
#error For TPM keystore please make sure WOLFBOOT_TPM is also defined
#endif

#if defined(WOLFBOOT_TPM_SEAL) || defined(WOLFBOOT_TPM_KEYSTORE)
void wolfBoot_print_hexstr(const unsigned char* bin, unsigned long sz,
    unsigned long maxLine)
{
    unsigned long i;
    if (maxLine == 0) maxLine = sz;
    for (i = 0; i < sz; i++) {
        wolfBoot_printf("%02x", bin[i]);
        if (((i+1) % maxLine) == 0 && i+1 != sz)
            wolfBoot_printf("\n");
    }
    wolfBoot_printf("\n");
}
#endif

#if defined(DEBUG_WOLFTPM) || defined(WOLFTPM_DEBUG_IO) || \
    defined(WOLFBOOT_DEBUG_TPM)
#define LINE_LEN 16
void wolfBoot_print_bin(const uint8_t* buffer, uint32_t length)
{
    uint32_t i, sz;

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
static int TPM2_IoCb(TPM2_CTX* ctx, int isRead, uint32_t addr, uint8_t* buf,
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
static int TPM2_IoCb(TPM2_CTX* ctx, const uint8_t* txBuf, uint8_t* rxBuf,
    word16 xferSz, void* userCtx)
#endif
{
    int ret;
#ifdef WOLFTPM_CHECK_WAIT_STATE
    int timeout = TPM_SPI_WAIT_RETRY;
#endif
#ifdef WOLFTPM_ADV_IO
    uint8_t txBuf[MAX_SPI_FRAMESIZE+TPM_TIS_HEADER_SZ];
    uint8_t rxBuf[MAX_SPI_FRAMESIZE+TPM_TIS_HEADER_SZ];
    int xferSz = TPM_TIS_HEADER_SZ + size;

#ifdef WOLFTPM_DEBUG_IO
    wolfBoot_printf("TPM2_IoCb (Adv): Read %d, Addr %x, Size %d\n",
        isRead ? 1 : 0, addr, size);
    if (!isRead) {
        wolfBoot_print_bin(buf, size);
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
        /* clock additional uint8_t until 0x01 LSB is set (keep CS asserted) */
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
    wolfBoot_print_bin(txBuf, xferSz);
    wolfBoot_print_bin(rxBuf, xferSz);
    #endif
#endif /* !WOLFTPM_CHECK_WAIT_STATE */

#ifdef WOLFTPM_ADV_IO
    if (isRead) {
        memcpy(buf, &rxBuf[TPM_TIS_HEADER_SZ], size);
    #ifdef WOLFTPM_DEBUG_IO
        wolfBoot_print_bin(buf, size);
    #endif
    }
#endif

    return ret;
}
#endif /* !ARCH_SIM && !WOLFTPM_MMIO */

#ifdef WOLFBOOT_MEASURED_BOOT

#ifdef WOLFBOOT_HASH_SHA256
#include <wolfssl/wolfcrypt/sha256.h>
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

#elif defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>
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
#endif /* HASH type */

/**
 * @brief Extends a PCR in the TPM with a hash.
 *
 * Extends a specified PCR's value in the TPM with a given hash. Uses
 * TPM2_PCR_Extend. Optionally, if DEBUG_WOLFTPM or WOLFBOOT_DEBUG_TPM defined,
 * prints debug info.
 *
 * @param[in] pcrIndex The PCR Index (0-24 is valid range).
 * @param[in] hash Pointer to the hash value to extend into the PCR.
 * @param[in] line Line number where the function is called (for debugging).
 * @return 0 on success, an error code on failure.
 *
 */
int wolfBoot_tpm2_extend(uint8_t pcrIndex, uint8_t* hash, int line)
{
    int rc;
#ifdef WOLFBOOT_DEBUG_TPM
    uint8_t digest[WOLFBOOT_TPM_PCR_DIG_SZ];
    int     digestSz = 0;
#endif

    /* clear auth session for PCR */
    wolfTPM2_SetAuthPassword(&wolftpm_dev, 0, NULL);

#ifdef ARCH_SIM
    if (pcrIndex >= 16) {
        /* reset the PCR for testing */
        wolfTPM2_ResetPCR(&wolftpm_dev, pcrIndex);
    }
#endif

    rc = wolfTPM2_ExtendPCR(&wolftpm_dev, pcrIndex, WOLFBOOT_TPM_PCR_ALG, hash,
        TPM2_GetHashDigestSize(WOLFBOOT_TPM_PCR_ALG));
#ifdef WOLFBOOT_DEBUG_TPM
    if (rc == 0) {
        wolfBoot_printf("Measured boot: Index %d, Line %d\n", pcrIndex, line);

        rc = wolfTPM2_ReadPCR(&wolftpm_dev, pcrIndex, WOLFBOOT_TPM_PCR_ALG,
            digest, &digestSz);

        wolfBoot_printf("PCR %d: Res %d, Digest Sz %d\n",
            pcrIndex, rc, digestSz);
        wolfBoot_print_bin(digest, digestSz);
    }
    else {
        wolfBoot_printf("Measure boot failed! Index %d, %x (%s)\n",
            pcrIndex, rc, wolfTPM2_GetRCString(rc));
    }
#endif
    (void)line;

    return rc;
}
#endif /* WOLFBOOT_MEASURED_BOOT */

#if defined(WOLFBOOT_TPM_VERIFY) || defined(WOLFBOOT_TPM_SEAL)
int wolfBoot_load_pubkey(struct wolfBoot_image* img, WOLFTPM2_KEY* pubKey,
    TPM_ALG_ID* pAlg)
{
    int rc = 0;
    uint32_t key_type;
    int key_slot = -1;
    uint8_t *hdr;
    uint16_t hdrSz;

    *pAlg = TPM_ALG_NULL;

    /* get public key */
    hdrSz = wolfBoot_get_header(img, HDR_PUBKEY, &hdr);
    if (hdrSz == WOLFBOOT_SHA_DIGEST_SIZE)
        key_slot = keyslot_id_by_sha(hdr);
    if (key_slot < 0)
        rc = -1;

    if (rc == 0) {
        key_type = keystore_get_key_type(key_slot);
        hdr = keystore_get_buffer(key_slot);
        hdrSz = keystore_get_size(key_slot);
        if (hdr == NULL || hdrSz <= 0)
            rc = -1;
    }
    /* Parse public key to TPM public key. Note: this loads as temp handle,
     *   however we don't use the handle. We still need to unload it. */
    if (rc == 0) {
    #if defined(WOLFBOOT_SIGN_ECC256) || \
        defined(WOLFBOOT_SIGN_ECC384) || \
        defined(WOLFBOOT_SIGN_ECC521)
        int tpmcurve;
        int point_sz = hdrSz/2;
        if (     key_type == AUTH_KEY_ECC256) tpmcurve = TPM_ECC_NIST_P256;
        else if (key_type == AUTH_KEY_ECC384) tpmcurve = TPM_ECC_NIST_P384;
        else if (key_type == AUTH_KEY_ECC521) tpmcurve = TPM_ECC_NIST_P521;
        else rc = -1; /* not supported algorithm */
        if (rc == 0) {
            *pAlg = TPM_ALG_ECC;
            rc = wolfTPM2_LoadEccPublicKey(&wolftpm_dev, pubKey,
                tpmcurve,                   /* Curve */
                hdr, point_sz,           /* Public X */
                hdr + point_sz, point_sz /* Public Y */
            );
        }
    #elif defined(WOLFBOOT_SIGN_RSA2048) || \
          defined(WOLFBOOT_SIGN_RSA3072) || \
          defined(WOLFBOOT_SIGN_RSA4096)
        uint32_t inOutIdx = 0;
        const uint8_t*n = NULL, *e = NULL;
        uint32_t nSz = 0, eSz = 0;
        if (key_type != AUTH_KEY_RSA2048 && key_type != AUTH_KEY_RSA3072 &&
            key_type != AUTH_KEY_RSA4096) {
            rc = -1;
        }
        if (rc == 0) {
            *pAlg = TPM_ALG_RSA;
            rc = wc_RsaPublicKeyDecode_ex(hdr, &inOutIdx, hdrSz,
                &n, &nSz, /* modulus */
                &e, &eSz  /* exponent */
            );
        }
        if (rc == 0) {
            /* Load public key into TPM */
            rc = wolfTPM2_LoadRsaPublicKey_ex(&wolftpm_dev, pubKey,
                n, nSz, *((uint32_t*)e),
                TPM_ALG_NULL, WOLFBOOT_TPM_HASH_ALG);
        }
    #else
        rc = -1; /* not supported */
    #endif
    }
    return rc;
}
#endif /* WOLFBOOT_TPM_VERIFY || WOLFBOOT_TPM_SEAL */

#ifdef WOLFBOOT_TPM_SEAL
int wolfBoot_get_random(uint8_t* buf, int sz)
{
    return wolfTPM2_GetRandom(&wolftpm_dev, buf, sz);
}

static int is_zero_digest(uint8_t* buf, size_t sz)
{
    while (sz--) {
        if (*buf++)
            return 0;
    }
    return 1;
}

int wolfBoot_get_pcr_active(uint8_t pcrAlg, uint32_t* pcrMask, uint8_t pcrMax)
{
    int rc = 0;
    PCR_Read_In  pcrReadIn;
    PCR_Read_Out pcrReadOut;
    uint8_t pcrIndex, count = 0;

    /* PCR0-15 are best for policy because they cannot be reset manually */
    if (pcrMax == 0) {
    #ifdef ARCH_SIM /* allow use of testing PCR's on simulator */
        pcrMax = 16;
    #else
        pcrMax = 15;
    #endif
    }
    *pcrMask = 0;

#ifdef WOLFBOOT_DEBUG_TPM
    wolfBoot_printf("Getting active PCR's (0-%d)\n", pcrMax);
#endif
    for (pcrIndex=0; pcrIndex<=pcrMax; pcrIndex++) {
        memset(&pcrReadIn, 0, sizeof(pcrReadIn));
        memset(&pcrReadOut, 0, sizeof(pcrReadOut));
        wolfTPM2_SetupPCRSel(&pcrReadIn.pcrSelectionIn, pcrAlg, pcrIndex);
        rc = TPM2_PCR_Read(&pcrReadIn, &pcrReadOut);
        if (rc == 0 && !is_zero_digest(
                pcrReadOut.pcrValues.digests[0].buffer,
                pcrReadOut.pcrValues.digests[0].size))
        {
            *pcrMask |= (1 << pcrIndex);
            count++;
        #ifdef WOLFBOOT_DEBUG_TPM
            wolfBoot_printf("PCR %d (counter %d)\n",
                pcrIndex, pcrReadOut.pcrUpdateCounter);
            wolfBoot_print_hexstr(pcrReadOut.pcrValues.digests[0].buffer,
                                  pcrReadOut.pcrValues.digests[0].size, 0);
        #endif
        }
    }
#ifdef WOLFBOOT_DEBUG_TPM
    wolfBoot_printf("Found %d active PCR's (mask 0x%08x)\n", count, *pcrMask);
#endif
    return rc;
}

uint32_t wolfBoot_tpm_pcrmask_sel(uint32_t pcrMask, uint8_t* pcrArray,
    uint32_t pcrArraySz)
{
    int i;
    uint32_t pcrArraySzAct = 0;
    for (i=0; i<IMPLEMENTATION_PCR; i++) {
        if (pcrMask & (1 << i)) {
            pcrArray[pcrArraySzAct++] = i;
            if (pcrArraySzAct < pcrArraySz) { /* make sure we have room */
                break;
            }
        }
    }
    return pcrArraySzAct;
}

int wolfBoot_build_policy(uint8_t pcrAlg, uint32_t pcrMask,
    uint8_t* policy, uint32_t* policySz,
    uint8_t* policyRef, uint32_t policyRefSz)
{
    int rc, i;
    uint8_t  pcrArray[PCR_SELECT_MAX*2];
    uint32_t pcrArraySz;
    uint8_t  digest[WOLFBOOT_TPM_PCR_DIG_SZ];
    uint32_t digestSz = 0;
    uint8_t  pcrDigest[WOLFBOOT_TPM_PCR_DIG_SZ];
    uint32_t pcrDigestSz = 0;

    /* populate PCR selection array */
    memset(pcrArray, 0, sizeof(pcrArray));
    pcrArraySz = wolfBoot_tpm_pcrmask_sel(pcrMask, pcrArray, sizeof(pcrArray));

    /* Create a Policy PCR digest to sign externally */
    memcpy(policy, (uint8_t*)&pcrMask, sizeof(pcrMask));
    *policySz = (uint32_t)sizeof(pcrMask);

    rc = wolfTPM2_PCRGetDigest(&wolftpm_dev, pcrAlg,
        pcrArray, pcrArraySz, pcrDigest, &pcrDigestSz);
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("PCR Digest (%d bytes):\n", pcrDigestSz);
        wolfBoot_print_hexstr(pcrDigest, pcrDigestSz, 0);
    #endif

        /* Build PCR Policy */
        memset(digest, 0, sizeof(digest));
        digestSz = TPM2_GetHashDigestSize(pcrAlg);
        rc = wolfTPM2_PolicyPCRMake(pcrAlg, pcrArray, pcrArraySz,
            pcrDigest, pcrDigestSz, digest, &digestSz);
    }
    if (rc == 0) {
        /* Add policyRef (if blank just re-hash) */
        rc = wolfTPM2_PolicyRefMake(pcrAlg, digest, &digestSz,
            policyRef, policyRefSz);
    }
    if (rc == 0) {
        memcpy(&policy[*policySz], digest, digestSz);
        *policySz += digestSz;
    }
    return rc;
}

int wolfBoot_get_policy(struct wolfBoot_image* img,
    uint8_t** policy, uint16_t* policySz)
{
    int rc = 0;
    *policySz = wolfBoot_get_header(img, HDR_POLICY_SIGNATURE, policy);
    if (*policySz <= 0) {
        uint32_t pcrMask = 0;
        TPM_ALG_ID pcrAlg = WOLFBOOT_TPM_PCR_ALG;

        /* Report the status of the PCR's for signing externally */
        wolfBoot_printf("Policy header not found!\n");
        wolfBoot_printf("Generating policy based on active PCR's!\n");

        /* Discover and print active PCR's */
        rc = wolfBoot_get_pcr_active(pcrAlg, &pcrMask, 0);
        if (rc == 0 && pcrMask > 0) {
            uint8_t  newPolicy[sizeof(pcrMask) + WOLFBOOT_TPM_PCR_DIG_SZ];
            uint32_t newPolicySz = 0;
            rc = wolfBoot_build_policy(pcrAlg, pcrMask, newPolicy, &newPolicySz,
                NULL, 0);
            if (rc == 0) {
                wolfBoot_printf("PCR Mask (0x%08x) and "
                                "PCR Policy Digest (%d bytes):\n",
                                pcrMask, newPolicySz);
                wolfBoot_print_hexstr(newPolicy, newPolicySz, 0);

                wolfBoot_printf("Use this policy with the sign tool "
                                "(--policy arg) or POLICY_FILE config\n");
            }
            else {
                wolfBoot_printf("Error building policy! %d\n", rc);
            }
        }
        else {
            wolfBoot_printf("No PCR's have been extended!\n");
        }
        rc = -TPM_RC_POLICY_FAIL; /* failure */
    }
    return rc;
}

/* authHandle = TPM_RH_PLATFORM or TPM_RH_OWNER */
/* auth is optional */
int wolfBoot_store_blob(TPMI_RH_NV_AUTH authHandle, uint32_t nvIndex,
    word32 nvAttributes, WOLFTPM2_KEYBLOB* blob,
    const uint8_t* auth, uint32_t authSz)
{
    int rc;
    WOLFTPM2_HANDLE parent;
    WOLFTPM2_NV nv;
    uint8_t  pubAreaBuffer[sizeof(TPM2B_PUBLIC)]; /* oversized buffer */
    int      nvSz, pos, pubAreaSize;

    memset(&parent, 0, sizeof(parent));
    memset(&nv, 0, sizeof(nv));

    nv.handle.hndl = nvIndex;
    nv.handle.auth.size = authSz;
    memcpy(nv.handle.auth.buffer, auth, authSz);

    parent.hndl = authHandle;

    /* encode public for smaller storage */
    rc = TPM2_AppendPublic(pubAreaBuffer, (word32)sizeof(pubAreaBuffer),
        &pubAreaSize, &blob->pub);
    if (rc == 0) {
        blob->pub.size = pubAreaSize;

        nvSz  = (uint32_t)sizeof(blob->pub.size)  + blob->pub.size;
        nvSz += (uint32_t)sizeof(blob->priv.size) + blob->priv.size;

        /* Create NV - no auth required, blob encrypted by TPM already */
        rc = wolfTPM2_NVCreateAuth(&wolftpm_dev, &parent, &nv,
            nv.handle.hndl, nvAttributes, nvSz, NULL, 0);
        if (rc == TPM_RC_NV_DEFINED) {
            /* allow use of existing handle - ignore this error */
            rc = 0;
        }
    }
    /* write sealed blob to NV */
    if (rc == 0) {
        pos = 0;
        /* write pub size */
        rc = wolfTPM2_NVWriteAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            (uint8_t*)&blob->pub.size,
            (uint32_t)sizeof(blob->pub.size), pos);
    }
    if (rc == 0) {
        pos += sizeof(blob->pub.size);
        /* write pub */
        rc = wolfTPM2_NVWriteAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            pubAreaBuffer, blob->pub.size, pos);
    }
    if (rc == 0) {
        pos += blob->pub.size;
        /* write priv size */
        rc = wolfTPM2_NVWriteAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            (uint8_t*)&blob->priv.size,
            (uint32_t)sizeof(blob->priv.size), pos);
    }
    if (rc == 0) {
        pos += sizeof(blob->priv.size);
        /* write priv */
        rc = wolfTPM2_NVWriteAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            blob->priv.buffer, blob->priv.size, pos);
    }
    if (rc == 0) {
        pos += blob->priv.size;
    }
    if (rc == 0) {
        wolfBoot_printf("Wrote %d bytes to NV index 0x%x\n",
            pos, nv.handle.hndl);
    }
    else {
        wolfBoot_printf("Error %d writing blob to NV index %x (error %s)\n",
            rc, nv.handle.hndl, wolfTPM2_GetRCString(rc));
    }
    return rc;
}

int wolfBoot_read_blob(uint32_t nvIndex, WOLFTPM2_KEYBLOB* blob,
    const uint8_t* auth, uint32_t authSz)
{
    int rc;
    WOLFTPM2_NV nv;
    uint8_t  pubAreaBuffer[sizeof(TPM2B_PUBLIC)];
    uint32_t readSz;
    int      nvSz, pubAreaSize = 0, pos;

    memset(&nv, 0, sizeof(nv));

    nv.handle.hndl = nvIndex;
    nv.handle.auth.size = authSz;
    memcpy(nv.handle.auth.buffer, auth, authSz);

    pos = 0;
    readSz = sizeof(blob->pub.size);
    rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, nv.handle.hndl,
        (uint8_t*)&blob->pub.size, &readSz, pos);
    if (rc == 0) {
        pos += readSz;
        readSz = blob->pub.size;
        rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            pubAreaBuffer, &readSz, pos);
    }
    if (rc == 0) {
        pos += readSz;
        rc = TPM2_ParsePublic(&blob->pub, pubAreaBuffer,
            (word32)sizeof(pubAreaBuffer), &pubAreaSize);
    }
    if (rc == 0) {
        readSz = sizeof(blob->priv.size);
        rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            (uint8_t*)&blob->priv.size, &readSz, pos);
    }
    if (rc == 0) {
        pos += sizeof(blob->priv.size);
        readSz = blob->priv.size;
        rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            blob->priv.buffer, &readSz, pos);
    }
    if (rc == 0) {
        pos += blob->priv.size;
    }
    if (rc == 0) {
        wolfBoot_printf("Read %d bytes from NV index 0x%x\n",
            pos, nv.handle.hndl);
    }
    else {
        wolfBoot_printf("Error %d reading blob from NV index %x (error %s)\n",
            rc, nv.handle.hndl, wolfTPM2_GetRCString(rc));
    }
    return rc;
}

/* The secret is sealed based on a policy authorization from a public key. */
int wolfBoot_seal_blob(struct wolfBoot_image* img, WOLFTPM2_KEYBLOB* seal_blob,
    const uint8_t* secret, int secret_sz)
{
    int rc;
    WOLFTPM2_SESSION policy_session;
    TPM_ALG_ID pcrAlg = WOLFBOOT_TPM_PCR_ALG;
    TPM_ALG_ID alg;
    TPMT_PUBLIC template;
    WOLFTPM2_KEY authKey;
    uint8_t *hdr;
    uint16_t hdrSz;

    if (secret == NULL || secret_sz > WOLFBOOT_MAX_SEAL_SZ) {
        return -1;
    }

    /* make sure we have a HDR_POLICY_SIGNATURE defined */
    rc = wolfBoot_get_policy(img, &hdr, &hdrSz);
    if (rc != 0) {
        /* Technically we can seal a secret without the signed policy, but it
         * can't be unsealed until a signed policy exists. For now consider this
         * a failure */
        return rc;
    }

    memset(&authKey, 0, sizeof(authKey));
    memset(&template, 0, sizeof(template));
    memset(&policy_session, 0, sizeof(policy_session));

    /* get public key for policy authorization */
    rc = wolfBoot_load_pubkey(img, &authKey, &alg);

    /* The handle for the public key if not needed, so unload it.
     * For seal only a populated TPM2B_PUBLIC is required */
    wolfTPM2_UnloadHandle(&wolftpm_dev, &authKey.handle);

    if (rc == 0) {
        /* Setup a TPM session that can be used for parameter encryption */
        rc = wolfTPM2_StartSession(&wolftpm_dev, &policy_session, &wolftpm_srk,
            NULL, TPM_SE_POLICY, TPM_ALG_CFB);
    }
    if (rc == 0) {
        /* enable parameter encryption for seal */
        rc = wolfTPM2_SetAuthSession(&wolftpm_dev, 1, &policy_session,
            (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
             TPMA_SESSION_continueSession));
    }
    if (rc == 0) {
        /* build authorization policy based on public key */
        /* digest here is input and output, must be zero'd */
        uint32_t digestSz = TPM2_GetHashDigestSize(pcrAlg);
        memset(template.authPolicy.buffer, 0, digestSz);
        rc = wolfTPM2_PolicyAuthorizeMake(pcrAlg, &authKey.pub,
            template.authPolicy.buffer, &digestSz, NULL, 0);
        template.authPolicy.size = digestSz;
    }
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("Policy Authorize Digest (%d bytes):\n",
            template.authPolicy.size);
        wolfBoot_print_hexstr(template.authPolicy.buffer,
            template.authPolicy.size, 0);
    #endif
        /* Create a new key for sealing using external signing auth */
        wolfTPM2_GetKeyTemplate_KeySeal(&template, pcrAlg);
        rc = wolfTPM2_CreateKeySeal_ex(&wolftpm_dev, seal_blob,
            &wolftpm_srk.handle, &template, NULL, 0, pcrAlg, NULL, 0,
            secret, secret_sz);
    }

    wolfTPM2_UnloadHandle(&wolftpm_dev, &policy_session.handle);
    wolfTPM2_UnsetAuth(&wolftpm_dev, 1);

    return rc;
}

/* Index (0-X) determines location in NV from WOLFBOOT_TPM_SEAL_NV_BASE to
 * store sealed blob */
int wolfBoot_seal(struct wolfBoot_image* img, int index,
    const uint8_t* secret, int secret_sz)
{
    int rc;
    WOLFTPM2_KEYBLOB seal_blob;
    word32 nvAttributes;

    memset(&seal_blob, 0, sizeof(seal_blob));

    /* creates a sealed keyed hash object (not loaded to TPM) */
    rc = wolfBoot_seal_blob(img, &seal_blob, secret, secret_sz);
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("Sealed keyed hash (pub %d, priv %d bytes):\n",
            seal_blob.pub.size, seal_blob.priv.size);
    #endif

        /* Get NV attributes amd allow it to be locked (if desired) */
        wolfTPM2_GetNvAttributesTemplate(TPM_RH_PLATFORM, &nvAttributes);
        nvAttributes |= TPMA_NV_WRITEDEFINE;

        rc = wolfBoot_store_blob(TPM_RH_PLATFORM,
            WOLFBOOT_TPM_SEAL_NV_BASE + index,
            nvAttributes, &seal_blob,
            NULL, 0 /* auth is not required as sealed blob is already encrypted */
        );
    }
    if (rc != 0) {
        wolfBoot_printf("Error %d sealing secret! (%s)\n",
            rc, wolfTPM2_GetRCString(rc));
    }
    return rc;
}

/* The unseal requires a signed policy from HDR_POLICY_SIGNATURE */
int wolfBoot_unseal_blob(struct wolfBoot_image* img, WOLFTPM2_KEYBLOB* seal_blob,
    uint8_t* secret, int* secret_sz)
{
    int rc, i;
    WOLFTPM2_SESSION policy_session;
    uint32_t key_type;
    TPM_ALG_ID pcrAlg = WOLFBOOT_TPM_PCR_ALG;
    TPM_ALG_ID alg = TPM_ALG_NULL, sigAlg;
    TPMT_PUBLIC template;
    WOLFTPM2_KEY authKey;
    TPMT_TK_VERIFIED checkTicket;
    Unseal_In  unsealIn;
    Unseal_Out unsealOut;
    uint8_t *hdr;
    uint16_t hdrSz;
    uint32_t pcrMask;
    uint8_t  pcrDigest[WOLFBOOT_TPM_PCR_DIG_SZ];
    uint32_t pcrDigestSz;
    uint8_t  policyDigest[WOLFBOOT_TPM_PCR_DIG_SZ];
    uint32_t policyDigestSz;
    uint8_t  pcrArray[PCR_SELECT_MAX*2];
    uint32_t pcrArraySz = 0;
    uint8_t* policyRef = NULL; /* optional nonce */
    uint32_t policyRefSz = 0;

    if (secret == NULL || secret_sz == NULL) {
        return -1;
    }

    *secret_sz = 0; /* init */

    /* make sure we have a HDR_POLICY_SIGNATURE defined */
    rc = wolfBoot_get_policy(img, &hdr, &hdrSz);
    if (rc != 0) {
        return rc;
    }

    /* extract pcrMask and populate PCR selection array */
    memcpy(&pcrMask, hdr, sizeof(pcrMask));
    memset(pcrArray, 0, sizeof(pcrArray));
    pcrArraySz = wolfBoot_tpm_pcrmask_sel(pcrMask, pcrArray, sizeof(pcrArray));

    /* skip to signature */
    hdr += sizeof(pcrMask);
    hdrSz -= sizeof(pcrMask);

    memset(&authKey, 0, sizeof(authKey));
    memset(&template, 0, sizeof(template));
    memset(&policy_session, 0, sizeof(policy_session));
    memset(&checkTicket, 0, sizeof(checkTicket));

    /* Setup a TPM session that can be used for parameter encryption */
    rc = wolfTPM2_StartSession(&wolftpm_dev, &policy_session, &wolftpm_srk,
            NULL, TPM_SE_POLICY, TPM_ALG_CFB);
    if (rc == 0) {
        /* enable parameter encryption for unseal */
        rc = wolfTPM2_SetAuthSession(&wolftpm_dev, 1, &policy_session,
            (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
             TPMA_SESSION_continueSession));
    }
    if (rc == 0) {
        /* Get PCR policy digest */
        rc = wolfTPM2_PolicyPCR(&wolftpm_dev, policy_session.handle.hndl,
            pcrAlg, pcrArray, pcrArraySz);
    }
    if (rc == 0) {
        pcrDigestSz = (uint32_t)sizeof(pcrDigest);
        rc = wolfTPM2_GetPolicyDigest(&wolftpm_dev, policy_session.handle.hndl,
            pcrDigest, &pcrDigestSz);
    }
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("PCR Policy Digest (%d bytes):\n", pcrDigestSz);
        wolfBoot_print_hexstr(pcrDigest, pcrDigestSz, pcrDigestSz);
    #endif

        /* Add policyRef (if blank just re-hash) */
        policyDigestSz = pcrDigestSz;
        memcpy(policyDigest, pcrDigest, pcrDigestSz);
        rc = wolfTPM2_PolicyRefMake(pcrAlg, policyDigest, &policyDigestSz,
            policyRef, policyRefSz);
    }
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("PCR Policy (%d bytes):\n", policyDigestSz);
        wolfBoot_print_hexstr(policyDigest, policyDigestSz, policyDigestSz);
    #endif

        /* get public key for policy authorization */
        rc = wolfBoot_load_pubkey(img, &authKey, &alg);
    }
    if (rc == 0) {
        sigAlg = alg == TPM_ALG_RSA ? TPM_ALG_RSASSA : TPM_ALG_ECDSA;
        rc = wolfTPM2_VerifyHashTicket(&wolftpm_dev, &authKey,
            hdr, hdrSz, policyDigest, policyDigestSz,
            sigAlg, pcrAlg, &checkTicket);
    }
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("Verify ticket: tag 0x%x, hi 0x%x, digest %d\n",
            checkTicket.tag, checkTicket.hierarchy, checkTicket.digest.size);
        wolfBoot_print_hexstr(checkTicket.digest.buffer,
                              checkTicket.digest.size, 32);
    #endif
        rc = wolfTPM2_PolicyAuthorize(&wolftpm_dev, policy_session.handle.hndl,
            &authKey.pub, &checkTicket, pcrDigest, pcrDigestSz,
            policyRef, policyRefSz);
    }
    else {
        wolfBoot_printf("Policy signature failed!\n");
        wolfBoot_printf("Expected PCR Mask (0x%08x) and PCR Digest (%d)\n",
            pcrMask, pcrDigestSz);
        wolfBoot_print_hexstr(pcrDigest, pcrDigestSz, 0);

        wolfBoot_printf("PCR Policy (%d bytes):\n", policyDigestSz);
        wolfBoot_print_hexstr(policyDigest, policyDigestSz, policyDigestSz);
    }

    /* done with authorization public key */
    wolfTPM2_UnloadHandle(&wolftpm_dev, &authKey.handle);

    if (rc == 0) {
        /* load the seal blob */
        rc = wolfTPM2_LoadKey(&wolftpm_dev, seal_blob, &wolftpm_srk.handle);
    }
    if (rc == 0) {
    #ifdef WOLFBOOT_DEBUG_TPM
        wolfBoot_printf("Loaded seal blob to 0x%x\n",
            (uint32_t)seal_blob->handle.hndl);
    #endif
        wolfTPM2_SetAuthHandle(&wolftpm_dev, 0, &seal_blob->handle);

        /* unseal */
        unsealIn.itemHandle = seal_blob->handle.hndl;
        rc = TPM2_Unseal(&unsealIn, &unsealOut);
    }
    if (rc == 0) {
        *secret_sz = unsealOut.outData.size;
        memcpy(secret, unsealOut.outData.buffer, *secret_sz);
    }

    wolfTPM2_UnloadHandle(&wolftpm_dev, &seal_blob->handle);
    wolfTPM2_UnloadHandle(&wolftpm_dev, &policy_session.handle);
    wolfTPM2_UnsetAuth(&wolftpm_dev, 1);

    return rc;
}

int wolfBoot_unseal(struct wolfBoot_image* img, int index, uint8_t* secret,
    int* secret_sz)
{
    int rc;
    WOLFTPM2_KEYBLOB seal_blob;

    memset(&seal_blob, 0, sizeof(seal_blob));

    rc = wolfBoot_read_blob(WOLFBOOT_TPM_SEAL_NV_BASE + index, &seal_blob,
        NULL, 0 /* auth is not required as sealed blob is already encrypted */
    );
    if (rc == 0) {
        rc = wolfBoot_unseal_blob(img, &seal_blob, secret, secret_sz);
    #ifdef WOLFBOOT_DEBUG_TPM
        if (rc == 0) {
            wolfBoot_printf("Unsealed keyed hash (pub %d, priv %d bytes):\n",
                seal_blob.pub.size, seal_blob.priv.size);
        }
    #endif
    }
    if (rc != 0) {
        wolfBoot_printf("Error %d unsealing secret! (%s)\n",
            rc, wolfTPM2_GetRCString(rc));
    }
    return rc;
}
#endif /* WOLFBOOT_TPM_SEAL */

#if (defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)) && \
     defined(WC_RNG_SEED_CB)
static int wolfRNG_GetSeedCB(OS_Seed* os, uint8_t* seed, uint32_t sz)
{
    int rc;
    (void)os;
    /* enable parameter encryption for the RNG request */
    rc = wolfTPM2_SetAuthSession(&wolftpm_dev, 0, &wolftpm_session,
        (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
        TPMA_SESSION_continueSession));
    if (rc == 0) {
        rc = wolfTPM2_GetRandom(&wolftpm_dev, seed, sz);
    }
    return rc;
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
    WOLFTPM2_CAPS caps;
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
    TPM_ALG_ID alg;
#endif
#ifdef WOLFBOOT_MEASURED_BOOT
    uint8_t digest[WOLFBOOT_SHA_DIGEST_SIZE];
#endif

#if !defined(ARCH_SIM) && !defined(WOLFTPM_MMIO)
    spi_init(0,0);
#endif

#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
    memset(&wolftpm_session, 0, sizeof(wolftpm_session));
    memset(&wolftpm_srk, 0, sizeof(wolftpm_srk));
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


    if (rc != 0) {
        wolfBoot_printf("TPM Init failed! %d\n", rc);
    }

#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
    if (rc == 0) {
    #ifdef WC_RNG_SEED_CB
        /* setup callback for RNG seed to use TPM */
        wc_SetSeed_Cb(wolfRNG_GetSeedCB);
    #endif

        /* Create a primary storage key - no auth needed for param enc to work */
        /* Prefer ECC as its faster */
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
            rc = wolfTPM2_StartSession(&wolftpm_dev, &wolftpm_session,
                &wolftpm_srk, NULL, TPM_SE_HMAC, TPM_ALG_CFB);
        }
        if (rc != 0) {
            wolfBoot_printf("TPM Create SRK or Session error %d (%s)!\n",
                rc, wolfTPM2_GetRCString(rc));
        }
    }
#endif /* WOLFBOOT_TPM_KEYSTORE | WOLFBOOT_TPM_SEAL */

#ifdef WOLFBOOT_MEASURED_BOOT
    /* hash wolfBoot and extend PCR */
    if (rc == 0) {
        rc = self_hash(digest);
        if (rc == 0) {
            rc = measure_boot(digest);
        }
        if (rc != 0) {
            wolfBoot_printf("Error %d performing wolfBoot measurement!\n", rc);
        }
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
    /* Enable parameter encryption for session */
    int rc = wolfTPM2_SetAuthSession(&wolftpm_dev, 0, &wolftpm_session,
            (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
             TPMA_SESSION_continueSession));
    if (rc == 0) {
        /* Change platform auth to random value, to prevent application
            * from being able to use platform hierarchy. This is defined in
            * section 10 of the TCG PC Client Platform specification. */
        rc = wolfTPM2_ChangePlatformAuth(&wolftpm_dev, &wolftpm_session);
    }
    if (rc != 0) {
        wolfBoot_printf("Error %d setting platform auth\n", rc);
    }
    #endif
    wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_session.handle);
    wolfTPM2_UnloadHandle(&wolftpm_dev, &wolftpm_srk.handle);
#endif /* WOLFBOOT_TPM_KEYSTORE */

    wolfTPM2_Cleanup(&wolftpm_dev);
}


#ifdef WOLFBOOT_TPM_KEYSTORE
int wolfBoot_check_rot(int key_slot, uint8_t* pubkey_hint)
{
    int rc;
    uint8_t  digest[WOLFBOOT_SHA_DIGEST_SIZE];
    uint32_t digestSz = WOLFBOOT_SHA_DIGEST_SIZE;
    WOLFTPM2_NV nv;

    memset(&nv, 0, sizeof(nv));

    #ifdef WOLFBOOT_TPM_KEYSTORE_AUTH
    nv.handle.auth.size = (UINT16)strlen(WOLFBOOT_TPM_KEYSTORE_AUTH);
    memcpy(nv.handle.auth.buffer, WOLFBOOT_TPM_KEYSTORE_AUTH, nv.handle.auth.size);
    wolfTPM2_SetAuthHandle(&wolftpm_dev, 0, &nv.handle);
    #endif

    /* Enable parameter encryption for session - to protect auth */
    rc = wolfTPM2_SetAuthSession(&wolftpm_dev, 1, &wolftpm_session,
            (TPMA_SESSION_decrypt | TPMA_SESSION_encrypt |
             TPMA_SESSION_continueSession));
    if (rc == 0) {
        /* find index with matching digest */
        nv.handle.hndl = WOLFBOOT_TPM_KEYSTORE_NV_BASE + key_slot;
        rc = wolfTPM2_NVReadAuth(&wolftpm_dev, &nv, nv.handle.hndl,
            digest, &digestSz, 0);
        if (rc == 0) {
            if (digestSz == WOLFBOOT_SHA_DIGEST_SIZE &&
                memcmp(digest, pubkey_hint, WOLFBOOT_SHA_DIGEST_SIZE) == 0) {
                wolfBoot_printf("TPM Root of Trust valid (id %d)\n", key_slot);
            }
            else {
                rc = -1; /* digest match failure */
            }
        }
        if (rc != 0) {
            wolfBoot_printf("TPM Root of Trust failed! %d (%s)\n",
                rc, wolfTPM2_GetRCString(rc));
            wolfBoot_printf("Expected Hash %d\n", digestSz);
            wolfBoot_print_hexstr(pubkey_hint, digestSz, 0);
        }
    }
    wolfTPM2_UnsetAuth(&wolftpm_dev, 1);

    return rc;
}
#endif

#endif /* WOLFBOOT_TPM */
