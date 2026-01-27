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
#include <wolfssl/wolfcrypt/memory.h>

#if defined(WOLFBOOT_HASH_SHA384)
#include <wolfssl/wolfcrypt/sha512.h>
#elif defined(WOLFBOOT_HASH_SHA3_384)
#include <wolfssl/wolfcrypt/sha3.h>
#endif

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
#define WOLFBOOT_DICE_SIG_LEN 64

#define WOLFBOOT_DICE_SUCCESS 0
#define WOLFBOOT_DICE_ERR_INVALID_ARGUMENT -1
#define WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL -2
#define WOLFBOOT_DICE_ERR_HW -3
#define WOLFBOOT_DICE_ERR_CRYPTO -4

#define COSE_LABEL_ALG 1
#define COSE_ALG_ES256 (-7)

#define EAT_CLAIM_NONCE 10
#define EAT_CLAIM_UEID 256

#define PSA_IAT_CLAIM_IMPLEMENTATION_ID 2396
#define PSA_IAT_CLAIM_LIFECYCLE 2398
#define PSA_IAT_CLAIM_SW_COMPONENTS 2399

#define PSA_SW_COMPONENT_MEASUREMENT_TYPE 1
#define PSA_SW_COMPONENT_MEASUREMENT_VALUE 2
#define PSA_SW_COMPONENT_MEASUREMENT_DESCRIPTION 5

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

struct wolfboot_cbor_writer {
    uint8_t *buf;
    size_t size;
    size_t offset;
    int error;
};

static void wolfboot_cbor_init(struct wolfboot_cbor_writer *w,
                               uint8_t *buf,
                               size_t size)
{
    w->buf = buf;
    w->size = size;
    w->offset = 0;
    w->error = 0;
}

static void wolfboot_cbor_reserve(struct wolfboot_cbor_writer *w, size_t len)
{
    if (w->error != 0) {
        return;
    }
    if (w->buf == NULL || w->size == 0) {
        w->offset += len;
        return;
    }
    if (w->offset + len > w->size) {
        w->error = WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL;
        return;
    }
    w->offset += len;
}

static void wolfboot_cbor_put_type_val(struct wolfboot_cbor_writer *w,
                                       uint8_t major,
                                       uint64_t val)
{
    uint8_t tmp[9];
    size_t len = 0;

    if (val <= 23) {
        tmp[len++] = (uint8_t)((major << 5) | (uint8_t)val);
    }
    else if (val <= 0xFF) {
        tmp[len++] = (uint8_t)((major << 5) | 24);
        tmp[len++] = (uint8_t)val;
    }
    else if (val <= 0xFFFF) {
        tmp[len++] = (uint8_t)((major << 5) | 25);
        tmp[len++] = (uint8_t)(val >> 8);
        tmp[len++] = (uint8_t)(val & 0xFF);
    }
    else if (val <= 0xFFFFFFFFu) {
        tmp[len++] = (uint8_t)((major << 5) | 26);
        tmp[len++] = (uint8_t)(val >> 24);
        tmp[len++] = (uint8_t)(val >> 16);
        tmp[len++] = (uint8_t)(val >> 8);
        tmp[len++] = (uint8_t)(val & 0xFF);
    }
    else {
        tmp[len++] = (uint8_t)((major << 5) | 27);
        tmp[len++] = (uint8_t)(val >> 56);
        tmp[len++] = (uint8_t)(val >> 48);
        tmp[len++] = (uint8_t)(val >> 40);
        tmp[len++] = (uint8_t)(val >> 32);
        tmp[len++] = (uint8_t)(val >> 24);
        tmp[len++] = (uint8_t)(val >> 16);
        tmp[len++] = (uint8_t)(val >> 8);
        tmp[len++] = (uint8_t)(val & 0xFF);
    }

    wolfboot_cbor_reserve(w, len);
    if (w->error != 0) {
        return;
    }
    if (w->buf == NULL || w->size == 0) {
        return;
    }
    XMEMCPY(w->buf + (w->offset - len), tmp, len);
}

static void wolfboot_cbor_put_uint(struct wolfboot_cbor_writer *w, uint64_t val)
{
    wolfboot_cbor_put_type_val(w, 0, val);
}

static void wolfboot_cbor_put_int(struct wolfboot_cbor_writer *w, int64_t val)
{
    if (val >= 0) {
        wolfboot_cbor_put_uint(w, (uint64_t)val);
    }
    else {
        uint64_t n = (uint64_t)(-1 - val);
        wolfboot_cbor_put_type_val(w, 1, n);
    }
}

static void wolfboot_cbor_put_bstr(struct wolfboot_cbor_writer *w,
                                   const uint8_t *data,
                                   size_t len)
{
    wolfboot_cbor_put_type_val(w, 2, len);
    wolfboot_cbor_reserve(w, len);
    if (w->error != 0) {
        return;
    }
    if (w->buf == NULL || w->size == 0) {
        return;
    }
    XMEMCPY(w->buf + (w->offset - len), data, len);
}

static void wolfboot_cbor_put_tstr(struct wolfboot_cbor_writer *w,
                                   const char *data,
                                   size_t len)
{
    wolfboot_cbor_put_type_val(w, 3, len);
    wolfboot_cbor_reserve(w, len);
    if (w->error != 0) {
        return;
    }
    if (w->buf == NULL || w->size == 0) {
        return;
    }
    XMEMCPY(w->buf + (w->offset - len), data, len);
}

static void wolfboot_cbor_put_array_start(struct wolfboot_cbor_writer *w,
                                          size_t count)
{
    wolfboot_cbor_put_type_val(w, 4, count);
}

static void wolfboot_cbor_put_map_start(struct wolfboot_cbor_writer *w,
                                        size_t count)
{
    wolfboot_cbor_put_type_val(w, 5, count);
}

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

static int wolfboot_get_boot_image_hash(uint8_t *out, size_t *out_len)
{
    struct wolfBoot_image img;
    uint8_t *hash_ptr = NULL;
    uint16_t hash_len = 0;

    if (out == NULL || out_len == NULL || *out_len < WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1;
    }

    if (wolfBoot_open_image(&img, PART_BOOT) != 0) {
        return -1;
    }

    hash_len = wolfBoot_get_header(&img, HDR_HASH, &hash_ptr);
    if (hash_len != WOLFBOOT_SHA_DIGEST_SIZE || hash_ptr == NULL) {
        return -1;
    }

    XMEMCPY(out, hash_ptr, hash_len);
    *out_len = hash_len;
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
    }
#elif defined(WOLFBOOT_HASH_SHA384)
    {
        wc_Sha384 hash;
        wc_InitSha384(&hash);
        wc_Sha384Update(&hash, uds, (word32)uds_len);
        wc_Sha384Final(&hash, digest);
    }
#elif defined(WOLFBOOT_HASH_SHA3_384)
    {
        wc_Sha3 hash;
        wc_InitSha3_384(&hash, NULL, INVALID_DEVID);
        wc_Sha3_384_Update(&hash, uds, (word32)uds_len);
        wc_Sha3_384_Final(&hash, digest);
    }
#endif

    ueid[0] = WOLFBOOT_UEID_TYPE_RANDOM;
    XMEMCPY(&ueid[1], digest,
            (WOLFBOOT_DICE_UEID_LEN - 1));
    *ueid_len = WOLFBOOT_DICE_UEID_LEN;
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
        ret = mp_set_int(&mod, 1);
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
    uint8_t uds[WOLFBOOT_DICE_CDI_LEN];
    size_t uds_len = sizeof(uds);
    uint8_t wb_hash[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t wb_hash_len = sizeof(wb_hash);
    uint8_t boot_hash[WOLFBOOT_SHA_DIGEST_SIZE];
    size_t boot_hash_len = sizeof(boot_hash);

    XMEMSET(claims, 0, sizeof(*claims));

    if (hal_uds_derive_key(uds, uds_len) != 0) {
        return WOLFBOOT_DICE_ERR_HW;
    }

    if (wolfboot_dice_get_ueid(claims->ueid, &claims->ueid_len,
                               uds, uds_len) != 0) {
        return WOLFBOOT_DICE_ERR_HW;
    }

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

    if (wolfboot_get_wolfboot_hash(wb_hash, &wb_hash_len) == 0) {
        claims->components[claims->component_count].measurement_type =
            WOLFBOOT_MEASUREMENT_HASH_NAME;
        claims->components[claims->component_count].measurement_type_len =
            XSTRLEN(WOLFBOOT_MEASUREMENT_HASH_NAME);
        claims->components[claims->component_count].measurement_desc =
            "wolfboot";
        claims->components[claims->component_count].measurement_desc_len =
            XSTRLEN("wolfboot");
        XMEMCPY(claims->components[claims->component_count].measurement,
                wb_hash, wb_hash_len);
        claims->components[claims->component_count].measurement_len = wb_hash_len;
        claims->component_count++;
    }

    if (wolfboot_get_boot_image_hash(boot_hash, &boot_hash_len) == 0) {
        claims->components[claims->component_count].measurement_type =
            WOLFBOOT_MEASUREMENT_HASH_NAME;
        claims->components[claims->component_count].measurement_type_len =
            XSTRLEN(WOLFBOOT_MEASUREMENT_HASH_NAME);
        claims->components[claims->component_count].measurement_desc =
            "boot-image";
        claims->components[claims->component_count].measurement_desc_len =
            XSTRLEN("boot-image");
        XMEMCPY(claims->components[claims->component_count].measurement,
                boot_hash, boot_hash_len);
        claims->components[claims->component_count].measurement_len = boot_hash_len;
        claims->component_count++;
    }

    return WOLFBOOT_DICE_SUCCESS;
}

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
    wc_ForceZero(cdi, sizeof(cdi));

    if (wolfboot_dice_hkdf(seed, sizeof(seed),
                           (const uint8_t *)"WOLFBOOT-IAK", 12,
                           (const uint8_t *)"WOLFBOOT-IAK-KEY", 16,
                           priv, sizeof(priv)) != 0) {
        goto cleanup;
    }
    /* Seed is no longer needed once the private key material is derived. */
    wc_ForceZero(seed, sizeof(seed));

    if (wolfboot_dice_fixup_priv(priv, sizeof(priv)) != 0) {
        goto cleanup;
    }

    if (wc_ecc_import_private_key_ex(priv, sizeof(priv), NULL, 0,
                                     key, ECC_SECP256R1) != 0) {
        goto cleanup;
    }

    ret = 0;

cleanup:
    wc_ForceZero(priv, sizeof(priv));
    wc_ForceZero(seed, sizeof(seed));
    wc_ForceZero(cdi, sizeof(cdi));
    return ret;
}

static int wolfboot_attest_get_private_key(ecc_key *key,
                                           const struct wolfboot_dice_claims *claims)
{
    uint8_t uds[WOLFBOOT_DICE_CDI_LEN];
    size_t uds_len = sizeof(uds);

#ifdef WOLFBOOT_ATTESTATION_IAK
    {
        uint8_t priv[WOLFBOOT_DICE_KEY_LEN];
        size_t priv_len = sizeof(priv);

        if (hal_attestation_get_iak_private_key(priv, &priv_len) != 0) {
            return -1;
        }
        if (priv_len != WOLFBOOT_DICE_KEY_LEN) {
            return -1;
        }
        if (wc_ecc_import_private_key_ex(priv, (word32)priv_len, NULL, 0,
                                         key, ECC_SECP256R1) != 0) {
            return -1;
        }
        return 0;
    }
#else
    if (hal_uds_derive_key(uds, uds_len) != 0) {
        return -1;
    }
    return wolfboot_dice_derive_attestation_key(key, uds, uds_len, claims);
#endif
}

static int wolfboot_dice_encode_payload(uint8_t *buf,
                                        size_t buf_len,
                                        const struct wolfboot_dice_claims *claims,
                                        size_t *payload_len)
{
    struct wolfboot_cbor_writer w;
    size_t map_count = 2;
    size_t i;

    if (claims->implementation_id_len > 0) {
        map_count++;
    }
    if (claims->has_lifecycle) {
        map_count++;
    }
    if (claims->component_count > 0) {
        map_count++;
    }

    wolfboot_cbor_init(&w, buf, buf_len);
    wolfboot_cbor_put_map_start(&w, map_count);

    wolfboot_cbor_put_int(&w, EAT_CLAIM_NONCE);
    wolfboot_cbor_put_bstr(&w, claims->challenge, claims->challenge_len);

    wolfboot_cbor_put_int(&w, EAT_CLAIM_UEID);
    wolfboot_cbor_put_bstr(&w, claims->ueid, claims->ueid_len);

    if (claims->implementation_id_len > 0) {
        wolfboot_cbor_put_int(&w, PSA_IAT_CLAIM_IMPLEMENTATION_ID);
        wolfboot_cbor_put_bstr(&w,
                               claims->implementation_id,
                               claims->implementation_id_len);
    }

    if (claims->has_lifecycle) {
        wolfboot_cbor_put_int(&w, PSA_IAT_CLAIM_LIFECYCLE);
        wolfboot_cbor_put_uint(&w, claims->lifecycle);
    }

    if (claims->component_count > 0) {
        wolfboot_cbor_put_int(&w, PSA_IAT_CLAIM_SW_COMPONENTS);
        wolfboot_cbor_put_array_start(&w, claims->component_count);
        for (i = 0; i < claims->component_count; i++) {
            wolfboot_cbor_put_map_start(&w, 3);
            wolfboot_cbor_put_uint(&w, PSA_SW_COMPONENT_MEASUREMENT_TYPE);
            wolfboot_cbor_put_tstr(&w,
                                   claims->components[i].measurement_type,
                                   claims->components[i].measurement_type_len);
            wolfboot_cbor_put_uint(&w, PSA_SW_COMPONENT_MEASUREMENT_VALUE);
            wolfboot_cbor_put_bstr(&w,
                                   claims->components[i].measurement,
                                   claims->components[i].measurement_len);
            wolfboot_cbor_put_uint(&w, PSA_SW_COMPONENT_MEASUREMENT_DESCRIPTION);
            wolfboot_cbor_put_tstr(&w,
                                   claims->components[i].measurement_desc,
                                   claims->components[i].measurement_desc_len);
        }
    }

    if (w.error != 0) {
        return w.error;
    }

    *payload_len = w.offset;
    return 0;
}

static int wolfboot_dice_encode_protected(uint8_t *buf,
                                          size_t buf_len,
                                          size_t *prot_len)
{
    struct wolfboot_cbor_writer w;

    wolfboot_cbor_init(&w, buf, buf_len);
    wolfboot_cbor_put_map_start(&w, 1);
    wolfboot_cbor_put_uint(&w, COSE_LABEL_ALG);
    wolfboot_cbor_put_int(&w, COSE_ALG_ES256);

    if (w.error != 0) {
        return w.error;
    }

    *prot_len = w.offset;
    return 0;
}

static int wolfboot_dice_build_sig_structure(uint8_t *buf,
                                             size_t buf_len,
                                             const uint8_t *prot,
                                             size_t prot_len,
                                             const uint8_t *payload,
                                             size_t payload_len,
                                             size_t *tbs_len)
{
    struct wolfboot_cbor_writer w;

    wolfboot_cbor_init(&w, buf, buf_len);
    wolfboot_cbor_put_array_start(&w, 4);
    wolfboot_cbor_put_tstr(&w, "Signature1", 10);
    wolfboot_cbor_put_bstr(&w, prot, prot_len);
    wolfboot_cbor_put_bstr(&w, (const uint8_t *)"", 0);
    wolfboot_cbor_put_bstr(&w, payload, payload_len);

    if (w.error != 0) {
        return w.error;
    }

    *tbs_len = w.offset;
    return 0;
}

static int wolfboot_dice_sign_tbs(const uint8_t *tbs,
                                  size_t tbs_len,
                                  uint8_t *sig,
                                  size_t *sig_len,
                                  const struct wolfboot_dice_claims *claims)
{
    ecc_key key;
    WC_RNG rng;
    int ret;
    uint8_t hash[SHA256_DIGEST_SIZE];
    uint8_t der_sig[128];
    word32 der_sig_len = sizeof(der_sig);
    uint8_t r[WOLFBOOT_DICE_SIG_LEN / 2];
    uint8_t s[WOLFBOOT_DICE_SIG_LEN / 2];
    word32 r_len = sizeof(r);
    word32 s_len = sizeof(s);

    if (sig == NULL || sig_len == NULL || *sig_len < WOLFBOOT_DICE_SIG_LEN) {
        return WOLFBOOT_DICE_ERR_INVALID_ARGUMENT;
    }

    wc_ecc_init(&key);
    if (wolfboot_attest_get_private_key(&key, claims) != 0) {
        wc_ecc_free(&key);
        return WOLFBOOT_DICE_ERR_HW;
    }

    (void)wc_ecc_set_deterministic(&key, 1);
    if (wc_InitRng(&rng) != 0) {
        wc_ecc_free(&key);
        return WOLFBOOT_DICE_ERR_HW;
    }

    {
        wc_Sha256 sha;
        wc_InitSha256(&sha);
        wc_Sha256Update(&sha, tbs, (word32)tbs_len);
        wc_Sha256Final(&sha, hash);
    }

    ret = wc_ecc_sign_hash(hash, sizeof(hash), der_sig, &der_sig_len, &rng, &key);
    wc_FreeRng(&rng);
    if (ret != 0) {
        wc_ecc_free(&key);
        return WOLFBOOT_DICE_ERR_CRYPTO;
    }

    ret = wc_ecc_sig_to_rs(der_sig, der_sig_len, r, &r_len, s, &s_len);
    if (ret != 0 || r_len > sizeof(r) || s_len > sizeof(s)) {
        wc_ecc_free(&key);
        return WOLFBOOT_DICE_ERR_CRYPTO;
    }

    XMEMSET(sig, 0, WOLFBOOT_DICE_SIG_LEN);
    XMEMCPY(sig + (sizeof(r) - r_len), r, r_len);
    XMEMCPY(sig + sizeof(r) + (sizeof(s) - s_len), s, s_len);
    *sig_len = WOLFBOOT_DICE_SIG_LEN;

    wc_ecc_free(&key);
    return WOLFBOOT_DICE_SUCCESS;
}

static int wolfboot_dice_build_token(uint8_t *token_buf,
                                     size_t token_buf_size,
                                     size_t *token_len,
                                     const uint8_t *challenge,
                                     size_t challenge_len)
{
    struct wolfboot_dice_claims claims;
    uint8_t payload[WOLFBOOT_DICE_MAX_PAYLOAD];
    size_t payload_len = 0;
    uint8_t protected_hdr[32];
    size_t protected_len = 0;
    uint8_t tbs[WOLFBOOT_DICE_MAX_TBS];
    size_t tbs_len = 0;
    uint8_t sig[WOLFBOOT_DICE_SIG_LEN];
    size_t sig_len = sizeof(sig);
    struct wolfboot_cbor_writer w;
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
        return ret;
    }

    ret = wolfboot_dice_encode_protected(protected_hdr, sizeof(protected_hdr),
                                         &protected_len);
    if (ret != 0) {
        return ret;
    }

    ret = wolfboot_dice_build_sig_structure(tbs, sizeof(tbs),
                                            protected_hdr, protected_len,
                                            payload, payload_len, &tbs_len);
    if (ret != 0) {
        return ret;
    }

    if (token_buf != NULL) {
        ret = wolfboot_dice_sign_tbs(tbs, tbs_len, sig, &sig_len, &claims);
        if (ret != 0) {
            return ret;
        }
    }

    wolfboot_cbor_init(&w, token_buf, token_buf_size);
    wolfboot_cbor_put_array_start(&w, 4);
    wolfboot_cbor_put_bstr(&w, protected_hdr, protected_len);
    wolfboot_cbor_put_map_start(&w, 0);
    wolfboot_cbor_put_bstr(&w, payload, payload_len);
    if (token_buf != NULL) {
        wolfboot_cbor_put_bstr(&w, sig, sig_len);
    }
    else {
        wolfboot_cbor_put_type_val(&w, 2, WOLFBOOT_DICE_SIG_LEN);
        wolfboot_cbor_reserve(&w, WOLFBOOT_DICE_SIG_LEN);
    }

    if (w.error != 0) {
        return w.error;
    }

    *token_len = w.offset;
    return WOLFBOOT_DICE_SUCCESS;
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
