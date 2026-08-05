/* unit-dice-token-size.c
 *
 * DICE COSE_Sign1 token sizing tests.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <check.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t test_wolfboot_region[64];

#define ARCH_FLASH_OFFSET 0x1000u
#define WOLFBOOT_PARTITION_BOOT_ADDRESS 0x1040u
#define EXT_FLASH
#define NO_XIP
#define WOLFBOOT_DICE_HW
#define WOLFBOOT_HASH_SHA256

#include "../../src/dice/dice.c"

static uint8_t test_boot_hash[WOLFBOOT_SHA_DIGEST_SIZE];
static uint8_t test_boot_signer_id[WOLFBOOT_SHA_DIGEST_SIZE];
static unsigned int test_cdi_update_calls;
static unsigned int test_key_create_calls;
static unsigned int test_sign_calls;
static int test_sign_result;

/* Local ECC signing must never be reached: this test exercises the delegated
 * hardware-signing path. These stubs keep that failure mode link-visible. */
int wc_ecc_sign_hash(const byte *in, word32 in_len, byte *out,
                     word32 *out_len, WC_RNG *rng, ecc_key *key)
{
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    (void)rng;
    (void)key;
    return -1;
}

int wc_ecc_sig_to_rs(const byte *sig, word32 sig_len, byte *r,
                     word32 *r_len, byte *s, word32 *s_len)
{
    (void)sig;
    (void)sig_len;
    (void)r;
    (void)r_len;
    (void)s;
    (void)s_len;
    return -1;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    size_t offset;

    if (address < ARCH_FLASH_OFFSET || data == NULL || len < 0) {
        return -1;
    }
    offset = (size_t)(address - ARCH_FLASH_OFFSET);
    if (offset > sizeof(test_wolfboot_region) ||
        (size_t)len > sizeof(test_wolfboot_region) - offset) {
        return -1;
    }
    memcpy(data, test_wolfboot_region + offset, (size_t)len);
    return len;
}

int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part)
{
    (void)img;
    return part == PART_BOOT ? 0 : -1;
}

uint16_t wolfBoot_get_header(struct wolfBoot_image *img, uint16_t type,
                             uint8_t **ptr)
{
    (void)img;
    if (ptr == NULL) {
        return 0;
    }
    if (type == HDR_HASH) {
        *ptr = test_boot_hash;
    }
    else if (type == HDR_PUBKEY) {
        *ptr = test_boot_signer_id;
    }
    else {
        return 0;
    }
    return (uint16_t)sizeof(test_boot_hash);
}

int hal_attestation_get_ueid(uint8_t *buf, size_t *len)
{
    size_t i;

    if (buf == NULL || len == NULL || *len < WOLFBOOT_DICE_UEID_LEN) {
        return -1;
    }
    for (i = 0; i < WOLFBOOT_DICE_UEID_LEN; i++) {
        buf[i] = (uint8_t)(i + 1u);
    }
    *len = WOLFBOOT_DICE_UEID_LEN;
    return 0;
}

int hal_attestation_get_implementation_id(uint8_t *buf, size_t *len)
{
    if (buf == NULL || len == NULL || *len < WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1;
    }
    memset(buf, 0xA5, WOLFBOOT_SHA_DIGEST_SIZE);
    *len = WOLFBOOT_SHA_DIGEST_SIZE;
    return 0;
}

int hal_attestation_get_lifecycle(uint32_t *lifecycle)
{
    if (lifecycle == NULL) {
        return -1;
    }
    *lifecycle = 0x3000u;
    return 0;
}

int hal_dice_update_cdi(const uint8_t *measurement, size_t meas_len,
                        const char *measurement_desc,
                        size_t measurement_desc_len)
{
    test_cdi_update_calls++;
    return measurement != NULL && meas_len > 0u &&
           measurement_desc != NULL && measurement_desc_len > 0u ? 0 : -1;
}

int hal_dice_create_attest_key(void)
{
    test_key_create_calls++;
    return 0;
}

int hal_dice_sign_hash(const uint8_t *hash, size_t hash_len,
                       uint8_t *sig, size_t *sig_len)
{
    test_sign_calls++;
    if (hash == NULL || hash_len != SHA256_DIGEST_SIZE || sig == NULL ||
        sig_len == NULL || *sig_len < 64u) {
        return -1;
    }
    if (test_sign_result != 0) {
        return test_sign_result;
    }
    memset(sig, 0x5A, 64u);
    *sig_len = 64u;
    return 0;
}

int hal_dice_get_attest_pubkey(uint8_t *buf, size_t *len)
{
    (void)buf;
    (void)len;
    return -1;
}

static void check_component_signer_ids(const uint8_t *token, size_t token_size)
{
    WOLFCOSE_CBOR_CTX token_ctx;
    WOLFCOSE_CBOR_CTX payload_ctx;
    const uint8_t *payload;
    const uint8_t *value;
    size_t array_count;
    size_t map_count;
    size_t payload_len;
    size_t value_len;
    size_t i;
    size_t j;
    uint8_t unknown_signer[WOLFBOOT_SHA_DIGEST_SIZE];
    int64_t label;
    int found_components = 0;
    int found_signer;

    memset(&token_ctx, 0, sizeof(token_ctx));
    memset(unknown_signer, 0, sizeof(unknown_signer));
    token_ctx.cbuf = token;
    token_ctx.bufSz = token_size;

    ck_assert_int_eq(wc_CBOR_DecodeArrayStart(&token_ctx, &array_count), 0);
    ck_assert_uint_eq(array_count, 4u);
    ck_assert_int_eq(wc_CBOR_DecodeBstr(&token_ctx, &value, &value_len), 0);
    ck_assert_int_eq(wc_CBOR_DecodeMapStart(&token_ctx, &map_count), 0);
    ck_assert_uint_eq(map_count, 0u);
    ck_assert_int_eq(wc_CBOR_DecodeBstr(&token_ctx, &payload, &payload_len), 0);
    ck_assert_int_eq(wc_CBOR_DecodeBstr(&token_ctx, &value, &value_len), 0);
    ck_assert_uint_eq(token_ctx.idx, token_ctx.bufSz);

    memset(&payload_ctx, 0, sizeof(payload_ctx));
    payload_ctx.cbuf = payload;
    payload_ctx.bufSz = payload_len;
    ck_assert_int_eq(wc_CBOR_DecodeMapStart(&payload_ctx, &map_count), 0);
    for (i = 0; i < map_count; i++) {
        ck_assert_int_eq(wc_CBOR_DecodeInt(&payload_ctx, &label), 0);
        if (label != PSA_IAT_CLAIM_SW_COMPONENTS) {
            ck_assert_int_eq(wc_CBOR_Skip(&payload_ctx), 0);
            continue;
        }

        found_components = 1;
        ck_assert_int_eq(wc_CBOR_DecodeArrayStart(&payload_ctx,
                                                  &array_count), 0);
        ck_assert_uint_eq(array_count, 2u);
        for (j = 0; j < array_count; j++) {
            size_t component_fields;
            size_t k;

            found_signer = 0;
            ck_assert_int_eq(wc_CBOR_DecodeMapStart(&payload_ctx,
                                                    &component_fields), 0);
            ck_assert_uint_eq(component_fields, 4u);
            for (k = 0; k < component_fields; k++) {
                ck_assert_int_eq(wc_CBOR_DecodeInt(&payload_ctx, &label), 0);
                if (label != PSA_SW_COMPONENT_SIGNER_ID) {
                    ck_assert_int_eq(wc_CBOR_Skip(&payload_ctx), 0);
                    continue;
                }

                ck_assert_int_eq(wc_CBOR_DecodeBstr(&payload_ctx, &value,
                                                     &value_len), 0);
                ck_assert_uint_eq(value_len, WOLFBOOT_SHA_DIGEST_SIZE);
                if (j == 0u) {
                    ck_assert_mem_eq(value, unknown_signer,
                                     sizeof(unknown_signer));
                }
                else {
#ifdef WOLFBOOT_NO_SIGN
                    ck_assert_mem_eq(value, unknown_signer,
                                     sizeof(unknown_signer));
#else
                    ck_assert_mem_eq(value, test_boot_signer_id,
                                     sizeof(test_boot_signer_id));
#endif
                }
                found_signer = 1;
            }
            ck_assert_int_eq(found_signer, 1);
        }
    }
    ck_assert_int_eq(found_components, 1);
    ck_assert_uint_eq(payload_ctx.idx, payload_ctx.bufSz);
}

static void check_token_size(size_t challenge_size)
{
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64];
    uint8_t token[WOLFBOOT_DICE_MAX_PAYLOAD + 128u];
    size_t queried_size = 0;
    size_t actual_size = 0;
    size_t short_size = 0;
    int ret;

    memset(challenge, 0x3C, sizeof(challenge));
    memset(test_boot_signer_id, 0xA7, sizeof(test_boot_signer_id));
    test_cdi_update_calls = 0;
    test_key_create_calls = 0;
    test_sign_calls = 0;
    test_sign_result = 0;

    ret = wolfBoot_dice_get_token_size(challenge_size, &queried_size);
    ck_assert_int_eq(ret, WOLFBOOT_DICE_SUCCESS);
    ck_assert_uint_gt(queried_size, 0u);
    ck_assert_uint_le(queried_size, sizeof(token));
    ck_assert_uint_eq(test_cdi_update_calls, 0u);
    ck_assert_uint_eq(test_key_create_calls, 0u);
    ck_assert_uint_eq(test_sign_calls, 0u);

    ret = wolfBoot_dice_get_token(challenge, challenge_size, token,
                                  queried_size - 1u, &short_size);
    ck_assert_int_eq(ret, WOLFBOOT_DICE_ERR_BUFFER_TOO_SMALL);
    ck_assert_uint_eq(short_size, queried_size);
    ck_assert_uint_eq(test_cdi_update_calls, 0u);
    ck_assert_uint_eq(test_key_create_calls, 0u);
    ck_assert_uint_eq(test_sign_calls, 0u);

    ret = wolfBoot_dice_get_token(challenge, challenge_size, token,
                                  queried_size, &actual_size);
    ck_assert_int_eq(ret, WOLFBOOT_DICE_SUCCESS);
    ck_assert_uint_eq(actual_size, queried_size);
    ck_assert_uint_eq(test_cdi_update_calls, 2u);
    ck_assert_uint_eq(test_key_create_calls, 1u);
    ck_assert_uint_eq(test_sign_calls, 1u);
    check_component_signer_ids(token, actual_size);
}

START_TEST(test_dice_token_size_32)
{
    check_token_size(PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32);
}
END_TEST

START_TEST(test_dice_hw_sign_failure)
{
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32];
    uint8_t token[WOLFBOOT_DICE_MAX_PAYLOAD + 128u];
    size_t token_size = 0;
    int ret;

    memset(challenge, 0x3C, sizeof(challenge));
    memset(test_boot_signer_id, 0xA7, sizeof(test_boot_signer_id));
    test_cdi_update_calls = 0;
    test_key_create_calls = 0;
    test_sign_calls = 0;
    test_sign_result = -1;

    ret = wolfBoot_dice_get_token(challenge, sizeof(challenge), token,
                                  sizeof(token), &token_size);
    ck_assert_int_eq(ret, WOLFBOOT_DICE_ERR_HW);
    ck_assert_uint_eq(test_sign_calls, 1u);
}
END_TEST

START_TEST(test_dice_token_size_48)
{
    check_token_size(PSA_INITIAL_ATTEST_CHALLENGE_SIZE_48);
}
END_TEST

START_TEST(test_dice_token_size_64)
{
    check_token_size(PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64);
}
END_TEST

static Suite *dice_token_size_suite(void)
{
    Suite *suite = suite_create("dice-token-size");
    TCase *test_case = tcase_create("size-consistency");

    tcase_add_test(test_case, test_dice_token_size_32);
    tcase_add_test(test_case, test_dice_token_size_48);
    tcase_add_test(test_case, test_dice_token_size_64);
    tcase_add_test(test_case, test_dice_hw_sign_failure);
    suite_add_tcase(suite, test_case);
    return suite;
}

int main(void)
{
    int failures;
    Suite *suite = dice_token_size_suite();
    SRunner *runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);
    failures = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failures;
}
