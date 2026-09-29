/* unit-snvm-keystore.c
 *
 * Host unit tests for the PolarFire SoC sNVM keystore backend.
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

#define SNVM_KEYSTORE
#define KEYSTORE_PUBKEY_SIZE 64   /* model an ECC-256 keystore slot */

#include "keystore.h"
#include "hal/mpfs250.h"
#include "hal/mpfs250_snvm.h"

/* Mock sNVM: one page per module, plus a failure injector. */
static uint8_t mock_snvm[MPFS_SNVM_MODULE_MAX][MPFS_SNVM_PLAIN_DATA_LEN];
static int mock_snvm_fail_module = -1;   /* module whose read returns -1 */
static int mock_snvm_reads;

int mpfs_snvm_read(uint8_t module, const uint8_t *usk, uint8_t *admin,
    uint8_t *data, uint16_t len)
{
    (void)usk; (void)admin;
    mock_snvm_reads++;
    if (module >= MPFS_SNVM_MODULE_MAX)
        return -1;
    if ((int)module == mock_snvm_fail_module)
        return -1;
    if (len > MPFS_SNVM_PLAIN_DATA_LEN)
        return -1;
    memcpy(data, mock_snvm[module], len);
    return 0;
}

#include "../../hal/mpfs250_snvm.c"

#define PAGE SNVM_KEYSTORE_PAGE_DATA

/* Fill the mock modules with a byte pattern that encodes its logical offset,
 * so a mis-computed module/offset shows up as wrong data rather than luck. */
static void setup_pattern(void)
{
    uint32_t m, i;

    memset(mock_snvm, 0, sizeof(mock_snvm));
    mock_snvm_fail_module = -1;
    mock_snvm_reads = 0;
    for (m = 0; m < SNVM_KEYSTORE_MAX_MODULES; m++) {
        for (i = 0; i < PAGE; i++) {
            mock_snvm[SNVM_KEYSTORE_MODULE + m][i] =
                (uint8_t)((m * PAGE + i) & 0xFF);
        }
    }
}

static void expect_pattern(const uint8_t *buf, uint32_t off, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        ck_assert_uint_eq(buf[i], (uint8_t)((off + i) & 0xFF));
}

/* A read wholly inside the first page touches one module. */
START_TEST(test_read_within_one_page)
{
    uint8_t buf[16];
    setup_pattern();
    ck_assert_int_eq(snvm_keystore_read(0, buf, sizeof(buf)), 0);
    expect_pattern(buf, 0, sizeof(buf));
    ck_assert_int_eq(mock_snvm_reads, 1);
}
END_TEST

/* A read straddling the page boundary takes the tail of one module and the head
 * of the next -- the offset arithmetic the OTP sibling never has to do. */
START_TEST(test_read_spans_two_pages)
{
    uint8_t buf[8];
    setup_pattern();
    ck_assert_int_eq(snvm_keystore_read(PAGE - 2, buf, sizeof(buf)), 0);
    expect_pattern(buf, PAGE - 2, sizeof(buf));
    ck_assert_int_eq(mock_snvm_reads, 2);
}
END_TEST

/* Spanning three modules exercises the loop rather than a single carry. */
START_TEST(test_read_spans_three_pages)
{
    uint8_t buf[PAGE + 10];
    setup_pattern();
    ck_assert_int_eq(snvm_keystore_read(PAGE - 5, buf, sizeof(buf)), 0);
    expect_pattern(buf, PAGE - 5, sizeof(buf));
    ck_assert_int_eq(mock_snvm_reads, 3);
}
END_TEST

/* Starting exactly on a boundary must land at offset 0 of the next module,
 * not offset PAGE of the previous one. */
START_TEST(test_read_at_exact_boundary)
{
    uint8_t buf[4];
    setup_pattern();
    ck_assert_int_eq(snvm_keystore_read(PAGE, buf, sizeof(buf)), 0);
    expect_pattern(buf, PAGE, sizeof(buf));
    ck_assert_int_eq(mock_snvm_reads, 1);
}
END_TEST

/* Running off the end of the sNVM module range is refused, not wrapped. */
START_TEST(test_read_past_module_max)
{
    uint8_t buf[8];
    uint32_t off;
    setup_pattern();
    off = (uint32_t)(MPFS_SNVM_MODULE_MAX - SNVM_KEYSTORE_MODULE) * PAGE;
    ck_assert_int_eq(snvm_keystore_read(off, buf, sizeof(buf)), -1);
}
END_TEST

/* An sNVM read error propagates instead of leaving stale cache contents. */
START_TEST(test_read_error_propagates)
{
    uint8_t buf[8];
    setup_pattern();
    mock_snvm_fail_module = SNVM_KEYSTORE_MODULE;
    ck_assert_int_eq(snvm_keystore_read(0, buf, sizeof(buf)), -1);
}
END_TEST

/* Provision slot `id` in the mock image, after the header. */
static void setup_slot(int id, uint32_t pubkey_size, uint32_t key_type,
    uint32_t mask)
{
    struct keystore_slot slot;
    uint32_t off = SNVM_KEYSTORE_HDR_SIZE +
        ((uint32_t)id * SIZEOF_KEYSTORE_SLOT);
    uint32_t m = off / PAGE, moff = off % PAGE;
    uint8_t *p = (uint8_t *)&slot;
    uint32_t i;

    memset(&slot, 0, sizeof(slot));
    slot.slot_id = id;
    slot.key_type = key_type;
    slot.part_id_mask = mask;
    slot.pubkey_size = pubkey_size;
    for (i = 0; i < SIZEOF_KEYSTORE_SLOT; i++) {
        mock_snvm[SNVM_KEYSTORE_MODULE + m + ((moff + i) / PAGE)]
                 [(moff + i) % PAGE] = p[i];
    }
}

/* Provision the mock header with n slots. */
static void setup_hdr(int n)
{
    struct snvm_keystore_hdr hdr;
    setup_pattern();
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, SNVM_KEYSTORE_MAGIC, 8);
    hdr.item_count = (uint16_t)n;
    memcpy(mock_snvm[SNVM_KEYSTORE_MODULE], &hdr, sizeof(hdr));
}

START_TEST(test_num_pubkeys_valid)
{
    setup_hdr(3);
    ck_assert_int_eq(keystore_num_pubkeys(), 3);
}
END_TEST

START_TEST(test_num_pubkeys_bad_magic)
{
    setup_hdr(3);
    mock_snvm[SNVM_KEYSTORE_MODULE][0] = 'X';
    ck_assert_int_eq(keystore_num_pubkeys(), 0);
}
END_TEST

/* A corrupt header must not drive slot indexing past the keystore; the OTP
 * sibling bounds item_count the same way. */
START_TEST(test_num_pubkeys_over_max)
{
    setup_hdr(SNVM_KEYSTORE_MAX_PUBKEYS + 1);
    ck_assert_int_eq(keystore_num_pubkeys(), 0);
}
END_TEST

START_TEST(test_num_pubkeys_at_max)
{
    setup_hdr(SNVM_KEYSTORE_MAX_PUBKEYS);
    ck_assert_int_eq(keystore_num_pubkeys(), SNVM_KEYSTORE_MAX_PUBKEYS);
}
END_TEST

/* A correctly provisioned slot reports its real size. */
START_TEST(test_get_size_valid)
{
    setup_hdr(1);
    setup_slot(0, KEYSTORE_PUBKEY_SIZE, 1, 0xFFFFFFFF);
    ck_assert_int_eq(keystore_get_size(0), KEYSTORE_PUBKEY_SIZE);
}
END_TEST

/* A corrupt slot must not report a size past the fixed slot cache, which
 * callers use as a hash length and coordinate offset. */
START_TEST(test_get_size_oversize_rejected)
{
    setup_hdr(1);
    setup_slot(0, 2 * KEYSTORE_PUBKEY_SIZE, 1, 0xFFFFFFFF);
    ck_assert_int_eq(keystore_get_size(0), -1);
}
END_TEST

START_TEST(test_get_size_just_over_rejected)
{
    setup_hdr(1);
    setup_slot(0, KEYSTORE_PUBKEY_SIZE + 1, 1, 0xFFFFFFFF);
    ck_assert_int_eq(keystore_get_size(0), -1);
}
END_TEST

/* An out-of-range id yields the documented not-found values, not a slot. */
START_TEST(test_accessors_reject_bad_id)
{
    setup_hdr(1);
    setup_slot(0, KEYSTORE_PUBKEY_SIZE, 1, 0xFFFFFFFF);
    ck_assert_ptr_eq(keystore_get_buffer(5), NULL);
    ck_assert_int_eq(keystore_get_size(5), -1);
    ck_assert_uint_eq(keystore_get_mask(5), 0);
    ck_assert_uint_eq(keystore_get_key_type(5), (uint32_t)-1);
}
END_TEST

/* key_type and mask come back as provisioned. */
START_TEST(test_get_key_type_and_mask)
{
    setup_hdr(1);
    setup_slot(0, KEYSTORE_PUBKEY_SIZE, 7, 0x0000A5A5);
    ck_assert_uint_eq(keystore_get_key_type(0), 7);
    ck_assert_uint_eq(keystore_get_mask(0), 0x0000A5A5);
}
END_TEST

/* An sNVM failure while loading the slot propagates through every accessor
 * rather than handing back the previous slot left in the cache. */
START_TEST(test_accessors_propagate_read_error)
{
    setup_hdr(1);
    setup_slot(0, KEYSTORE_PUBKEY_SIZE, 1, 0xFFFFFFFF);
    mock_snvm_fail_module = SNVM_KEYSTORE_MODULE;
    ck_assert_ptr_eq(keystore_get_buffer(0), NULL);
    ck_assert_int_eq(keystore_get_size(0), -1);
}
END_TEST

Suite *snvm_keystore_suite(void)
{
    Suite *s = suite_create("snvm-keystore");
    TCase *tc = tcase_create("snvm-keystore");

    tcase_add_test(tc, test_read_within_one_page);
    tcase_add_test(tc, test_read_spans_two_pages);
    tcase_add_test(tc, test_read_spans_three_pages);
    tcase_add_test(tc, test_read_at_exact_boundary);
    tcase_add_test(tc, test_read_past_module_max);
    tcase_add_test(tc, test_read_error_propagates);
    tcase_add_test(tc, test_num_pubkeys_valid);
    tcase_add_test(tc, test_num_pubkeys_bad_magic);
    tcase_add_test(tc, test_num_pubkeys_over_max);
    tcase_add_test(tc, test_num_pubkeys_at_max);
    tcase_add_test(tc, test_get_size_valid);
    tcase_add_test(tc, test_get_size_oversize_rejected);
    tcase_add_test(tc, test_get_size_just_over_rejected);
    tcase_add_test(tc, test_accessors_reject_bad_id);
    tcase_add_test(tc, test_get_key_type_and_mask);
    tcase_add_test(tc, test_accessors_propagate_read_error);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = snvm_keystore_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
