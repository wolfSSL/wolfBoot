/* unit-otp-keystore.c
 *
 * Unit tests for keystore_get_size() in src/flash_otp_keystore.c.
 *
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

/* flash_otp_keystore.c normally pulls in wolfboot/wolfboot.h and hal.h, which
 * drag in target/HAL specifics. Compile it in isolation (guarded by
 * WOLFBOOT_UNIT_TEST_OTP_KEYSTORE) and provide the few constants and the OTP
 * read mock it needs. */
#define WOLFBOOT_UNIT_TEST_OTP_KEYSTORE
#define FLASH_OTP_KEYSTORE
#define KEYSTORE_PUBKEY_SIZE 64   /* model an ECC-256 keystore slot */
#define OTP_SIZE 1024
#define FLASH_OTP_BASE 0
#define OTP_HDR_SIZE 16

/* Mock OTP flash backing store and reader. With FLASH_OTP_BASE == 0 the address
 * passed by the driver is a plain offset into this buffer. */
static uint8_t mock_otp[OTP_SIZE];
/* Set to 1 to make slot reads (offset >= OTP_HDR_SIZE) return -1. */
static int mock_otp_slot_read_fail;

int hal_flash_otp_read(uint32_t flashAddress, void *data, uint32_t length)
{
    if (flashAddress + length > (uint32_t)OTP_SIZE)
        return -1;
    if (mock_otp_slot_read_fail && flashAddress >= OTP_HDR_SIZE)
        return -1;
    memcpy(data, mock_otp + flashAddress, length);
    return 0;
}

#include "../../src/flash_otp_keystore.c"

/* Provision the mock OTP header with n slots; slot 0 gets a specific pubkey_size. */
static void setup_otp_n(int n, uint32_t pubkey_size)
{
    struct wolfBoot_otp_hdr *hdr = (struct wolfBoot_otp_hdr *)mock_otp;
    struct keystore_slot *slot =
        (struct keystore_slot *)(mock_otp + OTP_HDR_SIZE);

    memset(mock_otp, 0, sizeof(mock_otp));
    mock_otp_slot_read_fail = 0;
    memcpy(hdr->keystore_hdr_magic, KEYSTORE_HDR_MAGIC, 8);
    hdr->item_count = (uint16_t)n;
    slot->slot_id = 0;
    slot->key_type = 1;
    slot->part_id_mask = 0xFFFFFFFF;
    slot->pubkey_size = pubkey_size;
}

/* Provision the mock OTP with a single keystore slot whose pubkey_size field is
 * set to the supplied (possibly bogus) value. */
static void setup_otp(uint32_t pubkey_size)
{
    setup_otp_n(1, pubkey_size);
}

/* A correctly provisioned slot returns its real size unchanged. */
START_TEST(test_valid_size_passthrough)
{
    setup_otp(KEYSTORE_PUBKEY_SIZE);
    ck_assert_int_eq(keystore_get_size(0), KEYSTORE_PUBKEY_SIZE);
}
END_TEST

/* Regression for F-4790: keystore_get_size() must never report a size larger
 * than the KEYSTORE_PUBKEY_SIZE-byte buffer the pubkey is cached in. Without the
 * clamp it returned the raw OTP value (here 2*KEYSTORE_PUBKEY_SIZE), which the
 * callers (key_sha256/key_sha384/key_sha3_384 and the ECC verify path) used as a
 * hash length / coordinate offset, reading past otp_slot_item_cache. */
START_TEST(test_oversize_rejected)
{
    int sz;
    setup_otp(2 * KEYSTORE_PUBKEY_SIZE);
    sz = keystore_get_size(0);
    ck_assert_int_le(sz, KEYSTORE_PUBKEY_SIZE);
    ck_assert_int_eq(sz, -1);
}
END_TEST

/* One byte over the buffer is still out of bounds and must be rejected. */
START_TEST(test_just_over_rejected)
{
    setup_otp(KEYSTORE_PUBKEY_SIZE + 1);
    ck_assert_int_eq(keystore_get_size(0), -1);
}
END_TEST

/* keystore_num_pubkeys() must return 0 when the OTP magic is corrupted. */
START_TEST(test_bad_magic_returns_zero)
{
    struct wolfBoot_otp_hdr *hdr = (struct wolfBoot_otp_hdr *)mock_otp;
    setup_otp(KEYSTORE_PUBKEY_SIZE);
    hdr->keystore_hdr_magic[0] ^= 0xFF;
    ck_assert_int_eq(keystore_num_pubkeys(), 0);
}
END_TEST

/* item_count one above the maximum must be rejected. */
START_TEST(test_item_count_over_max)
{
    setup_otp_n(KEYSTORE_MAX_PUBKEYS + 1, KEYSTORE_PUBKEY_SIZE);
    ck_assert_int_eq(keystore_num_pubkeys(), 0);
}
END_TEST

/* item_count exactly at the maximum must be accepted (catches > vs >= mutation). */
START_TEST(test_item_count_at_max)
{
    setup_otp_n(KEYSTORE_MAX_PUBKEYS, KEYSTORE_PUBKEY_SIZE);
    ck_assert_int_eq(keystore_num_pubkeys(), KEYSTORE_MAX_PUBKEYS);
}
END_TEST

/* All keystore_get_* accessors must return their failure value when the OTP
 * slot read fails (header read succeeds, slot read fails). */
START_TEST(test_slot_read_fail_propagates)
{
    setup_otp(KEYSTORE_PUBKEY_SIZE);
    mock_otp_slot_read_fail = 1;
    ck_assert_ptr_eq(keystore_get_buffer(0), (uint8_t *)0);
    ck_assert_int_eq(keystore_get_size(0), -1);
    ck_assert_uint_eq(keystore_get_mask(0), 0);
    ck_assert_uint_eq(keystore_get_key_type(0), (uint32_t)-1);
}
END_TEST

Suite *otp_keystore_suite(void)
{
    Suite *s = suite_create("otp-keystore");
    TCase *tc = tcase_create("otp-keystore");

    tcase_add_test(tc, test_valid_size_passthrough);
    tcase_add_test(tc, test_oversize_rejected);
    tcase_add_test(tc, test_just_over_rejected);
    tcase_add_test(tc, test_bad_magic_returns_zero);
    tcase_add_test(tc, test_item_count_over_max);
    tcase_add_test(tc, test_item_count_at_max);
    tcase_add_test(tc, test_slot_read_fail_propagates);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = otp_keystore_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
