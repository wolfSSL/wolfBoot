/* dice.c
 *
 * DICE helpers and PSA attestation token builder.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "image.h"
#include "wolfboot/wolfboot.h"
#include "wolfboot/dice.h"

#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/integer.h>

#if defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>
#elif defined(WOLFBOOT_HASH_SHA3_384)
#include <wolfssl/wolfcrypt/sha3.h>
#endif

#include <wolfcose/wolfcose.h>

#ifndef PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32
#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32 (32u)
#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_48 (48u)
#define PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64 (64u)
#endif

#ifndef WOLFBOOT_DICE_MAX_PAYLOAD
#define WOLFBOOT_DICE_MAX_PAYLOAD 768
#endif

#ifndef WOLFBOOT_DICE_MAX_TBS
#define WOLFBOOT_DICE_MAX_TBS 1024
#endif

#define WOLFBOOT_DICE_CDI_LEN 32
#define WOLFBOOT_DICE_KEY_LEN 32
#define WOLFBOOT_DICE_UEID_LEN 33

#define WOLFBOOT_DICE_SUCCESS 0
#define WOLFBOOT_DICE_ERR_INVALID_ARGUMENT -1
#define WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL -2
#define WOLFBOOT_DICE_ERR_HW -3
#define WOLFBOOT_DICE_ERR_CRYPTO -4
#define WOLFBOOT_DICE_ERR_KEY_NOT_FOUND -5

static NOINLINEFUNCTION void wolfboot_dice_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0U) {
        *p++ = 0U;
    }
}

#define EAT_CLAIM_NONCE 10
#define EAT_CLAIM_UEID 256

#define PSA_IAT_CLAIM_IMPLEMENTATION_ID 2396
#define PSA_IAT_CLAIM_LIFECYCLE 2395
#define PSA_IAT_CLAIM_SW_COMPONENTS 2399

#define PSA_SW_COMPONENT_MEASUREMENT_TYPE 1
#define PSA_SW_COMPONENT_MEASUREMENT_VALUE 2
#define PSA_SW_COMPONENT_SIGNER_ID 5
#define PSA_SW_COMPONENT_MEASUREMENT_DESCRIPTION 6

#define WOLFBOOT_UEID_TYPE_RANDOM 0x01

#if defined(WOLFBOOT_HASH_SHA256)
#define WOLFBOOT_DICE_KDF_HASH_TYPE WC_HASH_TYPE_SHA256
#define WOLFBOOT_DICE_KDF_HASH_SIZE SHA256_DIGEST_SIZE
#define WOLFBOOT_MEASUREMENT_HASH_NAME "sha-256"
#elif defined(WOLFBOOT_HASH_SHA384)
#define WOLFBOOT_DICE_KDF_HASH_TYPE WC_HASH_TYPE_SHA384
#define WOLFBOOT_DICE_KDF_HASH_SIZE SHA384_DIGEST_SIZE
#define WOLFBOOT_MEASUREMENT_HASH_NAME "sha-384"
#elif defined(WOLFBOOT_HASH_SHA3_384)
#define WOLFBOOT_DICE_KDF_HASH_TYPE WC_HASH_TYPE_SHA3_384
#define WOLFBOOT_DICE_KDF_HASH_SIZE 48
#define WOLFBOOT_MEASUREMENT_HASH_NAME "sha3-384"
#else
#error "No supported hash for DICE attestation"
#endif

struct wolfboot_dice_component {
    const char *measurement_type;
    size_t measurement_type_len;
    const char *measurement_desc;
    size_t measurement_desc_len;
    uint8_t measurement[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t measurement_len;
    uint8_t signer_id[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t signer_id_len;
};

struct wolfboot_dice_claims {
    const uint8_t *challenge;
    size_t challenge_len;
    uint8_t ueid[WOLFBOOT_DICE_UEID_LEN];
    size_t ueid_len;
    uint8_t implementation_id[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t implementation_id_len;
    uint32_t lifecycle;
    int has_lifecycle;
    struct wolfboot_dice_component components[2];
    size_t component_count;
};

static int wolfboot_hash_region(uintptr_t address, uint32_t size, uint8_t *out)
{
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256 hash;
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384 hash;
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3 hash;
#endif
    uint32_t pos = 0;
    uint32_t chunk;
    int ret = 0;

#if defined(WOLFBOOT_HASH_SHA256)
    wc_InitSha256(&hash);
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_InitSha384(&hash);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_InitSha3_384(&hash, NULL, INVALID_DEVID);
#endif

    while (pos < size) {
        chunk = WOLFBOOT_SHA_BLOCK_SIZE;
        if (pos + chunk > size) {
            chunk = size - pos;
        }
#if defined(EXT_FLASH) && defined(NO_XIP)
        {
            uint8_t tmp[WOLFBOOT_SHA_BLOCK_SIZE];
            int read_sz = ext_flash_read(address + pos, tmp, chunk);
            if (read_sz != (int)chunk) {
                ret = -1;
                break;
            }
#if defined(WOLFBOOT_HASH_SHA256)
            wc_Sha256Update(&hash, tmp, chunk);
#elif defined(WOLFBOOT_HASH_SHA384)
            wc_Sha384Update(&hash, tmp, chunk);
#elif defined(WOLFBOOT_HASH_SHA3_384)
            wc_Sha3_384_Update(&hash, tmp, chunk);
#endif
        }
#else
#if defined(WOLFBOOT_HASH_SHA256)
        wc_Sha256Update(&hash, (const uint8_t *)(address + pos), chunk);
#elif defined(WOLFBOOT_HASH_SHA384)
        wc_Sha384Update(&hash, (const uint8_t *)(address + pos), chunk);
#elif defined(WOLFBOOT_HASH_SHA3_384)
        wc_Sha3_384_Update(&hash, (const uint8_t *)(address + pos), chunk);
#endif
#endif
        pos += chunk;
    }

    if (ret == 0) {
#if defined(WOLFBOOT_HASH_SHA256)
        wc_Sha256Final(&hash, out);
#elif defined(WOLFBOOT_HASH_SHA384)
        wc_Sha384Final(&hash, out);
#elif defined(WOLFBOOT_HASH_SHA3_384)
        wc_Sha3_384_Final(&hash, out);
#endif
    }

    return ret;
}

static int wolfboot_get_boot_image_claims(uint8_t *measurement,
                                          size_t *measurement_len,
                                          uint8_t *signer_id,
                                          size_t *signer_id_len)
{
    struct wolfBoot_image img;
    uint8_t *hash_ptr = NULL;
#ifndef WOLFBOOT_NO_SIGN
    uint8_t *signer_ptr = NULL;
#endif
    uint16_t hash_len = 0;
#ifndef WOLFBOOT_NO_SIGN
    uint16_t signer_len = 0;
#endif

    if (measurement == NULL || measurement_len == NULL || signer_id == NULL ||
        signer_id_len == NULL ||
        *measurement_len < WOLFBOOT_SHA_DIGEST_SIZE ||
        *signer_id_len < WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1;
    }

    if (wolfBoot_open_image(&img, PART_BOOT) != 0) {
        return -1;
    }

    hash_len = wolfBoot_get_header(&img, HDR_HASH, &hash_ptr);
    if (hash_len != WOLFBOOT_SHA_DIGEST_SIZE || hash_ptr == NULL) {
        return -1;
    }

    XMEMCPY(measurement, hash_ptr, hash_len);
    *measurement_len = hash_len;
#ifdef WOLFBOOT_NO_SIGN
    XMEMSET(signer_id, 0, WOLFBOOT_SHA_DIGEST_SIZE);
    *signer_id_len = WOLFBOOT_SHA_DIGEST_SIZE;
#else
    signer_len = wolfBoot_get_header(&img, HDR_PUBKEY, &signer_ptr);
    if (signer_len != WOLFBOOT_SHA_DIGEST_SIZE || signer_ptr == NULL) {
        return -1;
    }
    XMEMCPY(signer_id, signer_ptr, signer_len);
    *signer_id_len = signer_len;
#endif
    return 0;
}

static int wolfboot_get_wolfboot_hash(uint8_t *out, size_t *out_len)
{
#if !defined(WOLFBOOT_PARTITION_BOOT_ADDRESS) || !defined(ARCH_FLASH_OFFSET)
    (void)out;
    (void)out_len;
    return -1;
#else
    uintptr_t start = (uintptr_t)ARCH_FLASH_OFFSET;
    uintptr_t end = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t size;

    if (out == NULL || out_len == NULL || *out_len < WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1;
    }

    if (end <= start) {
        return -1;
    }

    size = (uint32_t)(end - start);
    if (wolfboot_hash_region(start, size, out) != 0) {
        return -1;
    }

    *out_len = WOLFBOOT_SHA_DIGEST_SIZE;
    return 0;
#endif
}

static int wolfboot_dice_hkdf(const uint8_t *ikm,
                              size_t ikm_len,
                              const uint8_t *salt,
                              size_t salt_len,
                              const uint8_t *info,
                              size_t info_len,
                              uint8_t *out,
                              size_t out_len)
{
    int ret = wc_HKDF_ex(WOLFBOOT_DICE_KDF_HASH_TYPE,
                         ikm, (word32)ikm_len,
                         salt, (word32)salt_len,
                         info, (word32)info_len,
                         out, (word32)out_len,
                         NULL, INVALID_DEVID);
    return ret == 0 ? 0 : -1;
}

static int wolfboot_dice_get_ueid(uint8_t *ueid, size_t *ueid_len,
                                  const uint8_t *uds, size_t uds_len)
{
    size_t len = WOLFBOOT_DICE_UEID_LEN;
    uint8_t digest[WOLFBOOT_DICE_KDF_HASH_SIZE];

    if (ueid == NULL || ueid_len == NULL) {
        return -1;
    }

    if (hal_attestation_get_ueid(ueid, &len) == 0) {
        *ueid_len = len;
        return 0;
    }

    if (uds == NULL || uds_len == 0) {
        return -1;
    }

#if defined(WOLFBOOT_HASH_SHA256)
    {
        wc_Sha256 hash;
        wc_InitSha256(&hash);
        wc_Sha256Update(&hash, uds, (word32)uds_len);
        wc_Sha256Final(&hash, digest);
        wolfboot_dice_zeroize(&hash, sizeof(hash));
    }
#elif defined(WOLFBOOT_HASH_SHA384)
    {
        wc_Sha384 hash;
        wc_InitSha384(&hash);
        wc_Sha384Update(&hash, uds, (word32)uds_len);
        wc_Sha384Final(&hash, digest);
        wolfboot_dice_zeroize(&hash, sizeof(hash));
    }
#elif defined(WOLFBOOT_HASH_SHA3_384)
    {
        wc_Sha3 hash;
        wc_InitSha3_384(&hash, NULL, INVALID_DEVID);
        wc_Sha3_384_Update(&hash, uds, (word32)uds_len);
        wc_Sha3_384_Final(&hash, digest);
        wolfboot_dice_zeroize(&hash, sizeof(hash));
    }
#endif

    ueid[0] = WOLFBOOT_UEID_TYPE_RANDOM;
    XMEMCPY(&ueid[1], digest,
            (WOLFBOOT_DICE_UEID_LEN - 1));
    *ueid_len = WOLFBOOT_DICE_UEID_LEN;
    wolfboot_dice_zeroize(digest, sizeof(digest));
    return 0;
}

static int wolfboot_dice_fixup_priv(uint8_t *priv, size_t priv_len)
{
    mp_int k;
    mp_int order;
    mp_int mod;
    const ecc_set_type *curve;
    int curve_idx;
    int ret;

    if (priv == NULL || priv_len == 0) {
        return -1;
    }

    curve_idx = wc_ecc_get_curve_idx(ECC_SECP256R1);
    curve = wc_ecc_get_curve_params(curve_idx);
    if (curve == NULL) {
        return -1;
    }

    ret = mp_init(&k);
    if (ret != MP_OKAY) {
        return -1;
    }
    ret = mp_init(&order);
    if (ret != MP_OKAY) {
        mp_clear(&k);
        return -1;
    }
    ret = mp_init(&mod);
    if (ret != MP_OKAY) {
        mp_clear(&k);
        mp_clear(&order);
        return -1;
    }

    ret = mp_read_unsigned_bin(&k, priv, (int)priv_len);
    if (ret == MP_OKAY) {
        ret = mp_read_radix(&order, curve->order, 16);
    }
    if (ret == MP_OKAY) {
        ret = mp_mod(&k, &order, &mod);
    }
    if (ret == MP_OKAY && mp_iszero(&mod) == MP_YES) {
        ret = mp_set(&mod, 1);
    }
    if (ret == MP_OKAY) {
        XMEMSET(priv, 0, priv_len);
        ret = mp_to_unsigned_bin_len(&mod, priv, (int)priv_len);
    }

    mp_clear(&mod);
    mp_clear(&order);
    mp_clear(&k);

    return ret == MP_OKAY ? 0 : -1;
}

static int wolfboot_dice_collect_claims(struct wolfboot_dice_claims *claims)
{
#ifndef WOLFBOOT_DICE_HW
    uint8_t uds[WOLFBOOT_DICE_CDI_LEN];
    size_t uds_len = sizeof(uds);
#endif
    uint8_t wb_hash[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t wb_hash_len = sizeof(wb_hash);
    uint8_t boot_hash[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t boot_hash_len = sizeof(boot_hash);
    uint8_t boot_signer_id[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t boot_signer_id_len = sizeof(boot_signer_id);

    XMEMSET(claims, 0, sizeof(*claims));

#ifdef WOLFBOOT_DICE_HW
    /* UDS stays inside platform security boundary. wolfboot_dice_get_ueid()
     * calls hal_attestation_get_ueid() first; with NULL UDS it uses the HAL path. */
    if (wolfboot_dice_get_ueid(claims->ueid, &claims->ueid_len, NULL, 0) != 0) {
        return WOLFBOOT_DICE_ERR_HW;
    }
#else
    if (hal_uds_derive_key(uds, uds_len) != 0) {
        /* Buffer may be partially filled, zero it to be sure */
        wc_ForceZero(uds, sizeof(uds));
        return WOLFBOOT_DICE_ERR_HW;
    }

    if (wolfboot_dice_get_ueid(claims->ueid, &claims->ueid_len,
                               uds, uds_len) != 0) {
        wc_ForceZero(uds, sizeof(uds));
        return WOLFBOOT_DICE_ERR_HW;
    }
#endif

    {
        size_t impl_len = sizeof(claims->implementation_id);
        if (hal_attestation_get_implementation_id(claims->implementation_id,
                                                  &impl_len) == 0) {
            claims->implementation_id_len = impl_len;
        }
    }

    if (claims->implementation_id_len == 0) {
        if (wolfboot_get_wolfboot_hash(wb_hash, &wb_hash_len) == 0) {
            XMEMCPY(claims->implementation_id, wb_hash, wb_hash_len);
            claims->implementation_id_len = wb_hash_len;
        }
    }

    if (hal_attestation_get_lifecycle(&claims->lifecycle) == 0) {
        claims->has_lifecycle = 1;
    }

    /* A measurement that silently vanishes leaves a token a verifier cannot
     * tell from one for a device with nothing to measure. */
    if (wolfboot_get_wolfboot_hash(wb_hash, &wb_hash_len) != 0) {
#ifndef WOLFBOOT_DICE_HW
        wc_ForceZero(uds, sizeof(uds));
#endif
        return WOLFBOOT_DICE_ERR_HW;
    }
    claims->components[claims->component_count].measurement_type =
        WOLFBOOT_MEASUREMENT_HASH_NAME;
    claims->components[claims->component_count].measurement_type_len =
        XSTRLEN(WOLFBOOT_MEASUREMENT_HASH_NAME);
    claims->components[claims->component_count].measurement_desc =
        WOLFBOOT_DICE_COMPONENT_WOLFBOOT;
    claims->components[claims->component_count].measurement_desc_len =
        XSTRLEN(WOLFBOOT_DICE_COMPONENT_WOLFBOOT);
    XMEMCPY(claims->components[claims->component_count].measurement,
            wb_hash, wb_hash_len);
    claims->components[claims->component_count].measurement_len = wb_hash_len;
    claims->components[claims->component_count].signer_id_len =
        WOLFBOOT_SHA_DIGEST_SIZE;
    claims->component_count++;

    if (wolfboot_get_boot_image_claims(boot_hash, &boot_hash_len,
                                       boot_signer_id,
                                       &boot_signer_id_len) != 0) {
#ifndef WOLFBOOT_DICE_HW
        wc_ForceZero(uds, sizeof(uds));
#endif
        return WOLFBOOT_DICE_ERR_HW;
    }
    claims->components[claims->component_count].measurement_type =
        WOLFBOOT_MEASUREMENT_HASH_NAME;
    claims->components[claims->component_count].measurement_type_len =
        XSTRLEN(WOLFBOOT_MEASUREMENT_HASH_NAME);
    claims->components[claims->component_count].measurement_desc =
        WOLFBOOT_DICE_COMPONENT_BOOTIMAGE;
    claims->components[claims->component_count].measurement_desc_len =
        XSTRLEN(WOLFBOOT_DICE_COMPONENT_BOOTIMAGE);
    XMEMCPY(claims->components[claims->component_count].measurement,
            boot_hash, boot_hash_len);
    claims->components[claims->component_count].measurement_len = boot_hash_len;
    XMEMCPY(claims->components[claims->component_count].signer_id,
            boot_signer_id, boot_signer_id_len);
    claims->components[claims->component_count].signer_id_len =
        boot_signer_id_len;
    claims->component_count++;
#ifndef WOLFBOOT_DICE_HW
    wc_ForceZero(uds, sizeof(uds));
#endif
    return WOLFBOOT_DICE_SUCCESS;
}

#ifndef WOLFBOOT_DICE_HW
/* Cached IAK public key (X9.63, 65 bytes) populated during key derivation.
 * Consumed and zeroized by wolfBoot_dice_get_attest_pubkey(). */
static uint8_t s_dice_attest_pubkey[65];
static word32  s_dice_attest_pubkey_len;
#endif /* !WOLFBOOT_DICE_HW */

#ifndef WOLFBOOT_DICE_HW
static int wolfboot_dice_derive_attestation_key(ecc_key *key,
                                                const uint8_t *uds,
                                                size_t uds_len,
                                                const struct wolfboot_dice_claims *claims)
{
    uint8_t cdi[WOLFBOOT_DICE_CDI_LEN];
    uint8_t seed[WOLFBOOT_DICE_CDI_LEN];
    uint8_t priv[WOLFBOOT_DICE_KEY_LEN];
    size_t i;
    int ret = -1;

    XMEMSET(cdi, 0, sizeof(cdi));
    XMEMSET(seed, 0, sizeof(seed));
    XMEMSET(priv, 0, sizeof(priv));

    if (claims->component_count == 0) {
        goto cleanup;
    }

    if (wolfboot_dice_hkdf(uds, uds_len,
                           claims->components[0].measurement,
                           claims->components[0].measurement_len,
                           (const uint8_t *)"WOLFBOOT-CDI-0", 14,
                           cdi, sizeof(cdi)) != 0) {
        goto cleanup;
    }

    for (i = 1; i < claims->component_count; i++) {
        if (wolfboot_dice_hkdf(cdi, sizeof(cdi),
                               claims->components[i].measurement,
                               claims->components[i].measurement_len,
                               (const uint8_t *)"WOLFBOOT-CDI", 12,
                               cdi, sizeof(cdi)) != 0) {
            goto cleanup;
        }
    }

    if (wolfboot_dice_hkdf(cdi, sizeof(cdi),
                           (const uint8_t *)"WOLFBOOT-IAK", 12,
                           (const uint8_t *)"WOLFBOOT-IAK", 12,
                           seed, sizeof(seed)) != 0) {
        goto cleanup;
    }
    /* CDI is no longer needed once the seed has been derived. */
    wolfboot_dice_zeroize(cdi, sizeof(cdi));

    if (wolfboot_dice_hkdf(seed, sizeof(seed),
                           (const uint8_t *)"WOLFBOOT-IAK", 12,
                           (const uint8_t *)"WOLFBOOT-IAK-KEY", 16,
                           priv, sizeof(priv)) != 0) {
        goto cleanup;
    }
    /* Seed is no longer needed once the private key material is derived. */
    wolfboot_dice_zeroize(seed, sizeof(seed));

    if (wolfboot_dice_fixup_priv(priv, sizeof(priv)) != 0) {
        goto cleanup;
    }

    if (wc_ecc_import_private_key_ex(priv, sizeof(priv), NULL, 0,
                                     key, ECC_SECP256R1) != 0) {
        goto cleanup;
    }

    /* Compute and cache the public key for wolfBoot_dice_get_attest_pubkey().
     * wc_ecc_import_private_key_ex with pub=NULL sets PRIVATEKEY_ONLY, so the
     * public point must be computed explicitly before export. */
    {
        word32 pub_len = sizeof(s_dice_attest_pubkey);
        /* Clear cached public key before new generation */
        XMEMSET(s_dice_attest_pubkey, 0, sizeof(s_dice_attest_pubkey));
        s_dice_attest_pubkey_len = 0;

        if (wc_ecc_make_pub(key, NULL) == 0 &&
            wc_ecc_export_x963(key, s_dice_attest_pubkey, &pub_len) == 0) {
            s_dice_attest_pubkey_len = pub_len;
        }
        else {
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    wolfboot_dice_zeroize(priv, sizeof(priv));
    wolfboot_dice_zeroize(seed, sizeof(seed));
    wolfboot_dice_zeroize(cdi, sizeof(cdi));
    return ret;
}
#endif /* !WOLFBOOT_DICE_HW */

#ifdef WOLFBOOT_DICE_HW
static int wolfboot_attest_get_private_key_hw(
    const struct wolfboot_dice_claims *claims)
{
    size_t i;
    /* Mix each boot component measurement into the platform CDI chain */
    for (i = 0; i < claims->component_count; i++) {
        if (hal_dice_update_cdi(claims->components[i].measurement,
                                claims->components[i].measurement_len,
                                claims->components[i].measurement_desc,
                                claims->components[i].measurement_desc_len) != 0) {
            return -1;
        }
    }
    /* Derive attestation keypair from current CDI state.
     * Private key stays inside the platform security boundary. */
    if (hal_dice_create_attest_key() != 0) {
        return -1;
    }
    return 0;
}
#endif /* WOLFBOOT_DICE_HW */

#ifndef WOLFBOOT_DICE_HW
static int wolfboot_attest_get_private_key(ecc_key *key,
                                           const struct wolfboot_dice_claims *claims)
{
    uint8_t uds[WOLFBOOT_DICE_CDI_LEN];
    size_t uds_len = sizeof(uds);

#ifdef WOLFBOOT_ATTESTATION_IAK
    {
        uint8_t priv[WOLFBOOT_DICE_KEY_LEN];
        size_t priv_len = sizeof(priv);
        int ret = -1;

        if (hal_attestation_get_iak_private_key(priv, &priv_len) != 0) {
            goto cleanup;
        }
        if (priv_len != WOLFBOOT_DICE_KEY_LEN) {
            goto cleanup;
        }
        if (wc_ecc_import_private_key_ex(priv, (word32)priv_len, NULL, 0,
                                         key, ECC_SECP256R1) != 0) {
            goto cleanup;
        }
        /* Import leaves the key ECC_PRIVATEKEY_ONLY; wolfCOSE treats a key as
         * sign-capable only once the public point is present. */
        if (wc_ecc_make_pub(key, NULL) != 0) {
            goto cleanup;
        }
        ret = 0;

cleanup:
        wolfboot_dice_zeroize(priv, sizeof(priv));
        return ret;
    }
#else
    int ret = -1;

    if (hal_uds_derive_key(uds, uds_len) == 0) {
        ret = wolfboot_dice_derive_attestation_key(key, uds, uds_len, claims);
    }
    wolfboot_dice_zeroize(uds, sizeof(uds));
    return ret;
#endif
}
#endif /* !WOLFBOOT_DICE_HW */

static int wolfboot_dice_encode_payload(uint8_t *buf,
                                        size_t buf_len,
                                        const struct wolfboot_dice_claims *claims,
                                        size_t *payload_len)
{
    WOLFCOSE_CBOR_CTX ctx;
    size_t map_count = 2;
    size_t i;
    int ret;

    if (claims->implementation_id_len > 0) {
        map_count++;
    }
    if (claims->has_lifecycle) {
        map_count++;
    }
    if (claims->component_count > 0) {
        map_count++;
    }

    XMEMSET(&ctx, 0, sizeof(ctx));
    ctx.buf = buf;
    ctx.bufSz = buf_len;

    ret = wc_CBOR_EncodeMapStart(&ctx, map_count);
    if (ret == 0) {
        ret = wc_CBOR_EncodeInt(&ctx, EAT_CLAIM_NONCE);
    }
    if (ret == 0) {
        ret = wc_CBOR_EncodeBstr(&ctx, claims->challenge, claims->challenge_len);
    }
    if (ret == 0) {
        ret = wc_CBOR_EncodeInt(&ctx, EAT_CLAIM_UEID);
    }
    if (ret == 0) {
        ret = wc_CBOR_EncodeBstr(&ctx, claims->ueid, claims->ueid_len);
    }

    if ((ret == 0) && (claims->implementation_id_len > 0)) {
        ret = wc_CBOR_EncodeInt(&ctx, PSA_IAT_CLAIM_IMPLEMENTATION_ID);
        if (ret == 0) {
            ret = wc_CBOR_EncodeBstr(&ctx, claims->implementation_id,
                                     claims->implementation_id_len);
        }
    }

    if ((ret == 0) && claims->has_lifecycle) {
        ret = wc_CBOR_EncodeInt(&ctx, PSA_IAT_CLAIM_LIFECYCLE);
        if (ret == 0) {
            ret = wc_CBOR_EncodeUint(&ctx, claims->lifecycle);
        }
    }

    if ((ret == 0) && (claims->component_count > 0)) {
        ret = wc_CBOR_EncodeInt(&ctx, PSA_IAT_CLAIM_SW_COMPONENTS);
        if (ret == 0) {
            ret = wc_CBOR_EncodeArrayStart(&ctx, claims->component_count);
        }
        for (i = 0; (ret == 0) && (i < claims->component_count); i++) {
            ret = wc_CBOR_EncodeMapStart(&ctx, 4);
            if (ret == 0) {
                ret = wc_CBOR_EncodeUint(&ctx,
                                         PSA_SW_COMPONENT_MEASUREMENT_TYPE);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeTstr(&ctx,
                    (const uint8_t *)claims->components[i].measurement_type,
                    claims->components[i].measurement_type_len);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeUint(&ctx,
                                         PSA_SW_COMPONENT_MEASUREMENT_VALUE);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeBstr(&ctx,
                    claims->components[i].measurement,
                    claims->components[i].measurement_len);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeUint(&ctx, PSA_SW_COMPONENT_SIGNER_ID);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeBstr(&ctx,
                    claims->components[i].signer_id,
                    claims->components[i].signer_id_len);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeUint(&ctx,
                    PSA_SW_COMPONENT_MEASUREMENT_DESCRIPTION);
            }
            if (ret == 0) {
                ret = wc_CBOR_EncodeTstr(&ctx,
                    (const uint8_t *)claims->components[i].measurement_desc,
                    claims->components[i].measurement_desc_len);
            }
        }
    }

    if (ret != 0) {
        return WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL;
    }

    *payload_len = ctx.idx;
    return 0;
}

#ifdef WOLFBOOT_DICE_HW
static int wolfboot_dice_hw_sign_cb(void *cbCtx, int32_t alg,
                                    const uint8_t *tbs, size_t tbs_len,
                                    uint8_t *sig, size_t sig_sz,
                                    size_t *sig_len)
{
    int *sign_failed = (int *)cbCtx;
    size_t out_len = sig_sz;

    (void)alg;

    /* wolfCOSE pre-hashes the Sig_structure for ES256, so tbs is the 32-byte
     * digest. hal_dice_sign_hash() outputs 64-byte raw R||S and keeps the
     * private key inside the platform boundary. */
    if (hal_dice_sign_hash(tbs, tbs_len, sig, &out_len) != 0) {
        if (sign_failed != NULL) {
            *sign_failed = 1;
        }
        return -1;
    }
    *sig_len = out_len;
    return 0;
}
#endif /* WOLFBOOT_DICE_HW */

#ifndef WOLFBOOT_DICE_HW
/* Kept out of line so the secure-world sign path does not widen the caller's
 * frame, which the size query also pays for. */
static int NOINLINEFUNCTION wolfboot_dice_sign_payload(
                                     WOLFCOSE_KEY *cose_key,
                                     struct wolfboot_dice_claims *claims,
                                     const uint8_t *payload,
                                     size_t payload_len,
                                     uint8_t *scratch,
                                     size_t scratch_len,
                                     uint8_t *token_buf,
                                     size_t token_buf_size,
                                     size_t *out_len)
{
    ecc_key key;
    WC_RNG rng;
    int key_inited = 0;
    int rng_inited = 0;
    int ret;

    wc_ecc_init(&key);
    key_inited = 1;
    if (wolfboot_attest_get_private_key(&key, claims) != 0) {
        ret = WOLFBOOT_DICE_ERR_HW;
        goto cleanup;
    }
    (void)wc_ecc_set_deterministic(&key, 1);
    if (wc_InitRng(&rng) != 0) {
        ret = WOLFBOOT_DICE_ERR_HW;
        goto cleanup;
    }
    rng_inited = 1;
    ret = wc_CoseKey_SetEcc(cose_key, WOLFCOSE_CRV_P256, &key);
    if (ret != 0) {
        ret = WOLFBOOT_DICE_ERR_CRYPTO;
        goto cleanup;
    }
    ret = wc_CoseSign1_Sign_ex(cose_key, WOLFCOSE_ALG_ES256, NULL, 0,
                               payload, payload_len, NULL, 0, NULL, 0,
                               scratch, scratch_len, token_buf, token_buf_size,
                               out_len, &rng, WOLFCOSE_SIGN1_UNTAGGED);
    if (ret != 0) {
        ret = WOLFBOOT_DICE_ERR_CRYPTO;
    }

cleanup:
    if (key_inited) {
        wc_ecc_free(&key);
        wolfboot_dice_zeroize(&key, sizeof(key));
    }
    if (rng_inited) {
        wc_FreeRng(&rng);
    }
    return ret;
}
#endif /* !WOLFBOOT_DICE_HW */

static int wolfboot_dice_build_token(uint8_t *token_buf,
                                     size_t token_buf_size,
                                     size_t *token_len,
                                     const uint8_t *challenge,
                                     size_t challenge_len)
{
    struct wolfboot_dice_claims claims;
    uint8_t payload[WOLFBOOT_DICE_MAX_PAYLOAD];
    size_t payload_len = 0;
    uint8_t scratch[WOLFBOOT_DICE_MAX_TBS];
    WOLFCOSE_KEY cose_key;
    int cose_key_inited = 0;
#ifdef WOLFBOOT_DICE_HW
    int hw_sign_failed = 0;
#endif
    size_t out_len = 0;
    int ret;

    ret = wolfboot_dice_collect_claims(&claims);
    if (ret != 0) {
        return ret;
    }

    claims.challenge = challenge;
    claims.challenge_len = challenge_len;

    ret = wolfboot_dice_encode_payload(payload, sizeof(payload), &claims,
                                       &payload_len);
    if (ret != 0) {
        goto cleanup;
    }

    /* Size query returns the exact untagged COSE_Sign1 length without signing:
     * the HW DICE engine must not run and the CDI must not advance while sizing. */
    if (token_buf == NULL) {
        ret = wc_CoseSign1_SignSize_ex(NULL, WOLFCOSE_ALG_ES256, 0,
                                       payload_len, 0,
                                       WOLFCOSE_SIGN1_UNTAGGED, &out_len);
        if (ret == 0) {
            *token_len = out_len;
        }
        else {
            ret = WOLFBOOT_DICE_ERR_CRYPTO;
        }
        goto cleanup;
    }

    ret = wc_CoseKey_Init(&cose_key);
    if (ret != 0) {
        ret = WOLFBOOT_DICE_ERR_CRYPTO;
        goto cleanup;
    }
    cose_key_inited = 1;

#ifdef WOLFBOOT_DICE_HW
    if (wolfboot_attest_get_private_key_hw(&claims) != 0) {
        ret = WOLFBOOT_DICE_ERR_HW;
        goto cleanup;
    }
    ret = wc_CoseKey_SetExtSigner(&cose_key, wolfboot_dice_hw_sign_cb,
                                  &hw_sign_failed);
    if (ret != 0) {
        ret = WOLFBOOT_DICE_ERR_CRYPTO;
        goto cleanup;
    }
    ret = wc_CoseSign1_Sign_ex(&cose_key, WOLFCOSE_ALG_ES256, NULL, 0,
                               payload, payload_len, NULL, 0, NULL, 0,
                               scratch, sizeof(scratch), token_buf,
                               token_buf_size, &out_len, NULL,
                               WOLFCOSE_SIGN1_UNTAGGED);
    if (ret != 0) {
        ret = hw_sign_failed ? WOLFBOOT_DICE_ERR_HW :
                               WOLFBOOT_DICE_ERR_CRYPTO;
        goto cleanup;
    }
#else
    ret = wolfboot_dice_sign_payload(&cose_key, &claims, payload, payload_len,
                                     scratch, sizeof(scratch),
                                     token_buf, token_buf_size, &out_len);
    if (ret != 0) {
        goto cleanup;
    }
#endif /* WOLFBOOT_DICE_HW */

    *token_len = out_len;
    ret = WOLFBOOT_DICE_SUCCESS;

cleanup:
    if (cose_key_inited) {
        wc_CoseKey_Free(&cose_key);
    }
    wolfboot_dice_zeroize(payload, sizeof(payload));
    wolfboot_dice_zeroize(scratch, sizeof(scratch));
    return ret;
}

int wolfBoot_dice_get_token(const uint8_t *challenge,
                            size_t challenge_size,
                            uint8_t *token_buf,
                            size_t token_buf_size,
                            size_t *token_size)
{
    size_t needed = 0;
    int ret;

    if (challenge == NULL || token_size == NULL) {
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;
    }

    if (challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32 &&
        challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_48 &&
        challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64) {
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;
    }

    ret = wolfboot_dice_build_token(NULL, 0, &needed, challenge, challenge_size);
    if (ret != 0) {
        return ret;
    }

    if (token_buf == NULL || token_buf_size < needed) {
        *token_size = needed;
        return WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL;
    }

    ret = wolfboot_dice_build_token(token_buf, token_buf_size, &needed,
                                    challenge, challenge_size);
    if (ret != 0) {
        return ret;
    }

    *token_size = needed;
    return WOLFBOOT_DICE_SUCCESS;
}

int wolfBoot_dice_get_token_size(size_t challenge_size, size_t *token_size)
{
    size_t needed = 0;
    int ret;
    uint8_t dummy_challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64];

    if (token_size == NULL) {
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;
    }

    if (challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32 &&
        challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_48 &&
        challenge_size != PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64) {
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;
    }

    XMEMSET(dummy_challenge, 0, sizeof(dummy_challenge));
    ret = wolfboot_dice_build_token(NULL, 0, &needed, dummy_challenge,
                                    challenge_size);
    if (ret != 0) {
        return ret;
    }

    *token_size = needed;
    return WOLFBOOT_DICE_SUCCESS;
}

#ifndef WOLFBOOT_ATTESTATION_IAK
int wolfBoot_dice_get_attest_pubkey(uint8_t *buf, size_t *len)
{
    if (buf == NULL || len == NULL || *len < 65)
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;

#ifdef WOLFBOOT_DICE_HW
    if (hal_dice_get_attest_pubkey(buf, len) != 0)
        return WOLFBOOT_DICE_ERR_KEY_NOT_FOUND; /* key not yet derived; call wolfBoot_dice_get_token() first */
    else
        return WOLFBOOT_DICE_SUCCESS;
#else
    if (s_dice_attest_pubkey_len == 0)
        return WOLFBOOT_DICE_ERR_KEY_NOT_FOUND; /* key not yet derived; call wolfBoot_dice_get_token() first */

    XMEMCPY(buf, s_dice_attest_pubkey, s_dice_attest_pubkey_len);
    *len = s_dice_attest_pubkey_len;
    wolfboot_dice_zeroize(s_dice_attest_pubkey, sizeof(s_dice_attest_pubkey));
    s_dice_attest_pubkey_len = 0;

    return WOLFBOOT_DICE_SUCCESS;
#endif
}
#endif /* !WOLFBOOT_ATTESTATION_IAK */
