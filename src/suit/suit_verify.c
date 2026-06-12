/* suit_verify.c
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
 * @file suit_verify.c
 * @brief SUIT authentication: COSE_Sign1 over the detached SUIT_Digest, then a
 * separate binding of that digest to hash(manifest). draft-ietf-suit-manifest-34
 * section 8.3. ES256 / SHA-256 trusted-bootloader profile.
 */
#include "suit.h"

#ifdef WOLFBOOT_SUIT

#include <string.h>
#include <wolfcose/wolfcose.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>

/* Provided by wolfBoot's keystore (src/keystore.c / flash_otp_keystore.c).
 * Declared locally so this file does not pull in the generated keystore sizing
 * header, keeping it buildable in minimal/host contexts. */
extern uint8_t* keystore_get_buffer(int id);

#define SUIT_P256_COORD_SZ 32
#define SUIT_SHA256_SZ     32

/* Split SUIT_Authentication = [ bstr SUIT_Digest, bstr COSE_Sign1 ] into its
 * two members, returning zero-copy pointers into the wrapper buffer. */
static int suit_split_auth(const uint8_t* aw, size_t awLen,
    const uint8_t** suitDigest, size_t* suitDigestLen,
    const uint8_t** coseSign1, size_t* coseSign1Len)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;

    ctx.buf = NULL;
    ctx.cbuf = aw;
    ctx.bufSz = awLen;
    ctx.idx = 0;

    if (wc_CBOR_DecodeArrayStart(&ctx, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    else if (count < 2u) {
        ret = SUIT_E_PARSE;
    }

    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeBstr(&ctx, suitDigest, suitDigestLen)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeBstr(&ctx, coseSign1, coseSign1Len)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }
    return ret;
}

/* Confirm the signed SUIT_Digest = [ alg, bstr digest ] matches hash(manifest).
 * This is a check distinct from the signature verification: the signature only
 * proves the signer signed the digest, not that the digest covers the manifest
 * we hold. */
static int suit_bind_digest(const uint8_t* suitDigest, size_t suitDigestLen,
    const uint8_t* manifest, size_t manifestLen)
{
    int ret = SUIT_SUCCESS;
    WOLFCOSE_CBOR_CTX ctx;
    size_t count = 0;
    int64_t alg = 0;
    const uint8_t* digBytes = NULL;
    size_t digBytesLen = 0;
    uint8_t manHash[SUIT_SHA256_SZ];

    ctx.buf = NULL;
    ctx.cbuf = suitDigest;
    ctx.bufSz = suitDigestLen;
    ctx.idx = 0;

    if (wc_CBOR_DecodeArrayStart(&ctx, &count) != WOLFCOSE_SUCCESS) {
        ret = SUIT_E_PARSE;
    }
    else if (count < 2u) {
        ret = SUIT_E_PARSE;
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeInt(&ctx, &alg) != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (alg != (int64_t)SUIT_COSE_ALG_SHA_256) {
            ret = SUIT_E_UNSUPPORTED;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CBOR_DecodeBstr(&ctx, &digBytes, &digBytesLen)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_PARSE;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (digBytesLen != (size_t)SUIT_SHA256_SZ) {
            ret = SUIT_E_DIGEST_MISMATCH;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_Hash(WC_HASH_TYPE_SHA256, manifest, (word32)manifestLen,
                manHash, (word32)sizeof(manHash)) != 0) {
            ret = SUIT_E_CRYPTO;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (XMEMCMP(manHash, digBytes, SUIT_SHA256_SZ) != 0) {
            ret = SUIT_E_DIGEST_MISMATCH;
        }
    }
    return ret;
}

int suit_verify_auth(struct suit_manifest* m)
{
    int ret = SUIT_SUCCESS;
    const uint8_t* suitDigest = NULL;
    size_t suitDigestLen = 0;
    const uint8_t* coseSign1 = NULL;
    size_t coseSign1Len = 0;
    uint8_t* pub = NULL;
    ecc_key eccKey;
    int eccInit = 0;
    WOLFCOSE_KEY key;
    int keyInit = 0;
    WOLFCOSE_HDR hdr;
    const uint8_t* payload = NULL;
    size_t payloadLen = 0;
    uint8_t scratch[SUIT_SCRATCH_SZ];

    if ((m == NULL) || (m->authWrapper == NULL) || (m->manifest == NULL)) {
        return SUIT_E_INVALID_ARG;
    }

    ret = suit_split_auth(m->authWrapper, m->authWrapperLen,
        &suitDigest, &suitDigestLen, &coseSign1, &coseSign1Len);

    if (ret == SUIT_SUCCESS) {
        pub = keystore_get_buffer(SUIT_KEY_SLOT);
        if (pub == NULL) {
            ret = SUIT_E_AUTH;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_ecc_init(&eccKey) != 0) {
            ret = SUIT_E_AUTH;
        }
        else {
            eccInit = 1;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_ecc_import_unsigned(&eccKey, pub, pub + SUIT_P256_COORD_SZ,
                NULL, ECC_SECP256R1) != 0) {
            ret = SUIT_E_AUTH;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CoseKey_Init(&key) != 0) {
            ret = SUIT_E_AUTH;
        }
        else {
            keyInit = 1;
        }
    }
    if (ret == SUIT_SUCCESS) {
        if (wc_CoseKey_SetEcc(&key, WOLFCOSE_CRV_P256, &eccKey) != 0) {
            ret = SUIT_E_AUTH;
        }
    }

    /* Step 1: signature check only. Proves the signer signed the SUIT_Digest. */
    if (ret == SUIT_SUCCESS) {
        if (wc_CoseSign1_Verify(&key, coseSign1, coseSign1Len,
                suitDigest, suitDigestLen, NULL, 0,
                scratch, sizeof(scratch), &hdr, &payload, &payloadLen)
                != WOLFCOSE_SUCCESS) {
            ret = SUIT_E_AUTH;
        }
    }

    /* Step 2: distinct binding of that digest to the manifest we hold. */
    if (ret == SUIT_SUCCESS) {
        ret = suit_bind_digest(suitDigest, suitDigestLen,
            m->manifest, m->manifestLen);
    }

    if (keyInit != 0) {
        wc_CoseKey_Free(&key);
    }
    if (eccInit != 0) {
        wc_ecc_free(&eccKey);
    }
    return ret;
}

#endif /* WOLFBOOT_SUIT */
