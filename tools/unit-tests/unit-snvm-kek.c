/* unit-snvm-kek.c
 *
 * Host unit test for the PolarFire SoC PUF-derived KEK, the RFC 3394 key
 * wrap around it, and the sNVM-backed image-encryption-key provider.  The
 * System Controller services are mocked: the PUF response is a deterministic
 * function of the challenge and a per-"device" seed, and sNVM is an array.
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

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/mpfs250.h"

static uint8_t mock_puf_seed;
static int     mock_puf_fail;
static int     mock_puf_calls;
static uint8_t mock_snvm[MPFS_SNVM_MODULE_MAX][MPFS_SNVM_PLAIN_DATA_LEN];
static int     mock_snvm_read_fail;
static int     mock_snvm_writes;

int mpfs_puf_emulation(const uint8_t *challenge, uint8_t op_type,
    uint8_t *response)
{
    int i;

    (void)op_type;
    mock_puf_calls++;
    if (mock_puf_fail)
        return -1;
    for (i = 0; i < MPFS_PUF_RESPONSE_LEN; i++) {
        response[i] = (uint8_t)(challenge[i % MPFS_PUF_CHALLENGE_LEN] ^
            mock_puf_seed ^ (uint8_t)(i * 7));
    }
    return 0;
}

int mpfs_snvm_read(uint8_t module, const uint8_t *usk, uint8_t *admin,
    uint8_t *data, uint16_t data_len)
{
    (void)usk; (void)admin;
    if (mock_snvm_read_fail || module >= MPFS_SNVM_MODULE_MAX ||
        data_len > MPFS_SNVM_PLAIN_DATA_LEN)
        return -1;
    memcpy(data, mock_snvm[module], data_len);
    return 0;
}

int mpfs_snvm_write(uint8_t format, uint8_t module, const uint8_t *data,
    const uint8_t *usk)
{
    (void)format; (void)usk;
    mock_snvm_writes++;
    if (module >= MPFS_SNVM_MODULE_MAX)
        return -1;
    memcpy(mock_snvm[module], data, MPFS_SNVM_PLAIN_DATA_LEN);
    return 0;
}

#include "../../hal/mpfs250_snvm.c"

static void reset_mocks(void)
{
    mock_puf_seed = 0x5A;
    mock_puf_fail = 0;
    mock_puf_calls = 0;
    mock_snvm_read_fail = 0;
    mock_snvm_writes = 0;
    memset(mock_snvm, 0, sizeof(mock_snvm));
}

static void fill(uint8_t *buf, uint32_t len, uint8_t base)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(base + i);
}

START_TEST(test_kek_deterministic_per_device)
{
    uint8_t k1[SNVM_KEK_LEN], k2[SNVM_KEK_LEN], k3[SNVM_KEK_LEN];

    reset_mocks();
    ck_assert_int_eq(mpfs_puf_kek(k1), 0);
    ck_assert_int_eq(mpfs_puf_kek(k2), 0);
    ck_assert_int_eq(memcmp(k1, k2, SNVM_KEK_LEN), 0);
    ck_assert_int_eq(mock_puf_calls, 2);

    /* Another device (different PUF) must derive a different KEK. */
    mock_puf_seed = 0xA5;
    ck_assert_int_eq(mpfs_puf_kek(k3), 0);
    ck_assert_int_ne(memcmp(k1, k3, SNVM_KEK_LEN), 0);
}
END_TEST

START_TEST(test_kek_guards)
{
    uint8_t kek[SNVM_KEK_LEN];
    uint8_t key[SNVM_KEK_AESKEY_LEN], out[SNVM_KEK_WRAPPED_LEN];

    reset_mocks();
    ck_assert_int_eq(mpfs_puf_kek(NULL), -1);
    ck_assert_int_eq(snvm_kek_wrap(NULL, sizeof(key), out, sizeof(out)), -1);
    ck_assert_int_eq(snvm_kek_wrap(key, sizeof(key), NULL, sizeof(out)), -1);
    ck_assert_int_eq(snvm_kek_unwrap(NULL, sizeof(out), key, sizeof(key)), -1);
    ck_assert_int_eq(snvm_kek_unwrap(out, sizeof(out), NULL, sizeof(key)), -1);

    mock_puf_fail = 1;
    ck_assert_int_eq(mpfs_puf_kek(kek), -1);
    ck_assert_int_eq(snvm_kek_wrap(key, sizeof(key), out, sizeof(out)), -1);
    ck_assert_int_eq(snvm_kek_unwrap(out, sizeof(out), key, sizeof(key)), -1);
}
END_TEST

START_TEST(test_wrap_unwrap_roundtrip)
{
    uint8_t key[SNVM_KEK_AESKEY_LEN], back[SNVM_KEK_AESKEY_LEN];
    uint8_t wrapped[SNVM_KEK_WRAPPED_LEN], wrapped2[SNVM_KEK_WRAPPED_LEN];

    reset_mocks();
    fill(key, sizeof(key), 0xA0);
    memset(back, 0, sizeof(back));
    ck_assert_int_eq(snvm_kek_wrap(key, sizeof(key), wrapped, sizeof(wrapped)),
        SNVM_KEK_WRAPPED_LEN);
    ck_assert_int_ne(memcmp(wrapped, key, sizeof(key)), 0);
    /* Deterministic: same device, same key, same blob. */
    ck_assert_int_eq(snvm_kek_wrap(key, sizeof(key), wrapped2,
        sizeof(wrapped2)), SNVM_KEK_WRAPPED_LEN);
    ck_assert_int_eq(memcmp(wrapped, wrapped2, sizeof(wrapped)), 0);

    ck_assert_int_eq(snvm_kek_unwrap(wrapped, sizeof(wrapped), back,
        sizeof(back)), SNVM_KEK_AESKEY_LEN);
    ck_assert_int_eq(memcmp(key, back, sizeof(key)), 0);
}
END_TEST

START_TEST(test_unwrap_rejects_tamper_and_foreign_device)
{
    uint8_t key[SNVM_KEK_AESKEY_LEN], back[SNVM_KEK_AESKEY_LEN];
    uint8_t wrapped[SNVM_KEK_WRAPPED_LEN];

    reset_mocks();
    fill(key, sizeof(key), 0x10);
    ck_assert_int_eq(snvm_kek_wrap(key, sizeof(key), wrapped, sizeof(wrapped)),
        SNVM_KEK_WRAPPED_LEN);

    /* A blob wrapped on one device does not unwrap on another. */
    mock_puf_seed = 0x3C;
    ck_assert_int_ne(snvm_kek_unwrap(wrapped, sizeof(wrapped), back,
        sizeof(back)), SNVM_KEK_AESKEY_LEN);
    mock_puf_seed = 0x5A;

    /* The RFC 3394 integrity check catches a flipped bit. */
    wrapped[SNVM_KEK_WRAPPED_LEN - 1] ^= 0x01;
    ck_assert_int_ne(snvm_kek_unwrap(wrapped, sizeof(wrapped), back,
        sizeof(back)), SNVM_KEK_AESKEY_LEN);

    /* Too short an output buffer is refused rather than truncated. */
    wrapped[SNVM_KEK_WRAPPED_LEN - 1] ^= 0x01;
    ck_assert_int_ne(snvm_kek_unwrap(wrapped, sizeof(wrapped), back, 16),
        SNVM_KEK_AESKEY_LEN);
}
END_TEST

START_TEST(test_provision_then_get_encrypt_key)
{
    uint8_t key[ENCRYPT_KEY_SIZE], nonce[ENCRYPT_NONCE_SIZE];
    uint8_t expect_key[ENCRYPT_KEY_SIZE], expect_nonce[ENCRYPT_NONCE_SIZE];

    reset_mocks();
    fill(expect_key, sizeof(expect_key), 0x00);
    fill(expect_nonce, sizeof(expect_nonce), 0x20);

    ck_assert_int_eq(snvm_enckey_provision(), 0);
    ck_assert_int_eq(mock_snvm_writes, 1);
    /* A second run finds the page already holding the content and does not
     * spend another write cycle. */
    ck_assert_int_eq(snvm_enckey_provision(), 0);
    ck_assert_int_eq(mock_snvm_writes, 1);
    /* The page holds the wrapped key, never the key itself. */
    ck_assert_int_ne(memcmp(mock_snvm[SNVM_ENCKEY_MODULE], expect_key,
        sizeof(expect_key)), 0);
    ck_assert_int_eq(memcmp(mock_snvm[SNVM_ENCKEY_MODULE] +
        SNVM_KEK_WRAPPED_LEN, expect_nonce, sizeof(expect_nonce)), 0);

    memset(key, 0, sizeof(key));
    memset(nonce, 0, sizeof(nonce));
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), 0);
    ck_assert_int_eq(memcmp(key, expect_key, sizeof(key)), 0);
    ck_assert_int_eq(memcmp(nonce, expect_nonce, sizeof(nonce)), 0);
}
END_TEST

START_TEST(test_get_encrypt_key_failures)
{
    uint8_t key[ENCRYPT_KEY_SIZE], nonce[ENCRYPT_NONCE_SIZE];

    reset_mocks();
    ck_assert_int_eq(snvm_enckey_provision(), 0);

    ck_assert_int_eq(wolfBoot_get_encrypt_key(NULL, nonce), -1);
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, NULL), -1);

    mock_snvm_read_fail = 1;
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), -1);
    mock_snvm_read_fail = 0;

    /* Blob from another device: unwrap fails, no key is returned. */
    mock_puf_seed = 0xC3;
    memset(key, 0, sizeof(key));
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), -1);
    mock_puf_seed = 0x5A;

    /* Corrupted page: integrity check fails. */
    mock_snvm[SNVM_ENCKEY_MODULE][3] ^= 0x80;
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), -1);
    mock_snvm[SNVM_ENCKEY_MODULE][3] ^= 0x80;
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), 0);

    /* PUF service down: fail closed. */
    mock_puf_fail = 1;
    ck_assert_int_eq(wolfBoot_get_encrypt_key(key, nonce), -1);
}
END_TEST

START_TEST(test_set_erase_fail_closed)
{
    uint8_t key[ENCRYPT_KEY_SIZE], nonce[ENCRYPT_NONCE_SIZE];

    reset_mocks();
    memset(key, 0x11, sizeof(key));
    memset(nonce, 0x22, sizeof(nonce));
    ck_assert_int_eq(wolfBoot_set_encrypt_key(key, nonce), -1);
    ck_assert_int_eq(wolfBoot_erase_encrypt_key(), -1);
    ck_assert_int_eq(mock_snvm_writes, 0);
}
END_TEST

Suite *snvm_kek_suite(void)
{
    Suite *s = suite_create("snvm-kek");
    TCase *tc = tcase_create("snvm-kek");

    tcase_add_test(tc, test_kek_deterministic_per_device);
    tcase_add_test(tc, test_kek_guards);
    tcase_add_test(tc, test_wrap_unwrap_roundtrip);
    tcase_add_test(tc, test_unwrap_rejects_tamper_and_foreign_device);
    tcase_add_test(tc, test_provision_then_get_encrypt_key);
    tcase_add_test(tc, test_get_encrypt_key_failures);
    tcase_add_test(tc, test_set_erase_fail_closed);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = snvm_kek_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
