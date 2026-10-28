/* unit-update-disk-failover.c
 *
 * Unit test for the disk boot failover actions (DISK_FAILOVER_ZEROIZE).
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define WOLFBOOT_UPDATE_DISK
#define WOLFBOOT_SELF_UPDATE_MONOLITHIC
#define RAM_CODE
#define WOLFBOOT_SELF_HEADER
#define EXT_ENCRYPTED
#define ENCRYPT_WITH_CHACHA
#define HAVE_CHACHA
#define IMAGE_HEADER_SIZE 256
#define BOOT_PART_A 0
#define BOOT_PART_B 1
#define BOOT_PART_GOLDEN 2
#define DISK_GOLDEN_SLOT
#define DISK_BOOT_CONFIRM
#define DISK_FAILOVER_ZEROIZE
#define MOCK_ADDRESS_BOOT 0xCD000000

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <check.h>

#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "loader.h"
#include <wolfssl/wolfcrypt/chacha.h>

#define TEST_PAYLOAD_SIZE 64
#define TEST_PART_SIZE (IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE + 1024)
#define FILL_A 0xA1
#define FILL_B 0xB2
#define FILL_G 0xC3
#define N_PARTS 3

static uint8_t load_buffer[TEST_PART_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

static uint8_t part_image[N_PARTS][TEST_PART_SIZE];
static int mock_reads[N_PARTS];
static int mock_writes[N_PARTS];
static int mock_bad_fill[N_PARTS];
static int mock_do_boot_called;
static int mock_booted_fill;
static int mock_key_erased;
static int mock_key_missing;
static int mock_key_erase_fail;
static int mock_write_fail_part;
ChaCha chacha;

static void set_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)(value >> 8);
}

static void set_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)(value >> 24);
}

static void build_image(uint8_t *image, uint32_t version, uint8_t fill)
{
    memset(image, 0, TEST_PART_SIZE);
    set_u32_le(image, WOLFBOOT_MAGIC);
    set_u32_le(image + sizeof(uint32_t), TEST_PAYLOAD_SIZE);
    set_u16_le(image + IMAGE_HEADER_OFFSET, HDR_VERSION);
    set_u16_le(image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), 4);
    set_u32_le(image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), version);
    memset(image + IMAGE_HEADER_SIZE, fill, TEST_PAYLOAD_SIZE);
}

static void reset_mocks(void)
{
    memset(load_buffer, 0, sizeof(load_buffer));
    build_image(part_image[0], 3, FILL_A);
    build_image(part_image[1], 2, FILL_B);
    build_image(part_image[2], 1, FILL_G);
    memset(mock_reads, 0, sizeof(mock_reads));
    memset(mock_writes, 0, sizeof(mock_writes));
    memset(mock_bad_fill, 0, sizeof(mock_bad_fill));
    mock_do_boot_called = 0;
    mock_booted_fill = -1;
    mock_key_erased = 0;
    mock_key_missing = 0;
    mock_key_erase_fail = 0;
    mock_write_fail_part = -1;
    wolfBoot_panicked = 0;
}

static void mark_bad(uint8_t fill)
{
    int i;
    for (i = 0; i < N_PARTS; i++) {
        if (mock_bad_fill[i] == 0) {
            mock_bad_fill[i] = fill;
            return;
        }
    }
}

int chacha_init(void)
{
    return 0;
}

int wc_Chacha_SetIV(ChaCha* ctx, const byte* inIv, word32 counter)
{
    (void)ctx; (void)inIv; (void)counter;
    return 0;
}

int wc_Chacha_Process(ChaCha* ctx, byte* output, const byte* input, word32 msglen)
{
    (void)ctx;
    memmove(output, input, msglen);
    return 0;
}

void wc_ForceZero(void* mem, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)mem;
    while (len-- > 0)
        *p++ = 0;
}

int wolfBoot_initialize_encryption(void)
{
    return mock_key_missing ? -1 : 0;
}

int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    if (mock_key_missing) {
        return -1;
    }
    memset(key, 0x5A, ENCRYPT_KEY_SIZE);
    memset(nonce, 0xC3, ENCRYPT_NONCE_SIZE);
    return 0;
}

int wolfBoot_erase_encrypt_key(void)
{
    mock_key_erased++;
    return mock_key_erase_fail ? -1 : 0;
}

int disk_init(int drv)
{
    (void)drv;
    return 0;
}

int disk_open(int drv)
{
    (void)drv;
    return 0;
}

void disk_close(int drv)
{
    (void)drv;
}

int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    (void)drv;
    if (part < 0 || part >= N_PARTS)
        return -1;
    if ((off > TEST_PART_SIZE) || (sz > (TEST_PART_SIZE - off)))
        return -1;
    mock_reads[part]++;
    memcpy(buf, part_image[part] + off, (size_t)sz);
    return (int)sz;
}

int disk_part_write(int drv, int part, uint64_t off, uint64_t sz,
    const uint8_t *buf)
{
    (void)drv;
    if (part < 0 || part >= N_PARTS)
        return -1;
    if ((off > TEST_PART_SIZE) || (sz > (TEST_PART_SIZE - off)))
        return -1;
    if (part == mock_write_fail_part) {
        mock_writes[part]++;
        return -1;
    }
    mock_writes[part]++;
    memcpy(part_image[part] + off, buf, (size_t)sz);
    return (int)sz;
}

int disk_part_size(int drv, int part, uint64_t *size)
{
    (void)drv;
    if (part < 0 || part >= N_PARTS)
        return -1;
    *size = TEST_PART_SIZE;
    return 0;
}

int wolfBoot_open_image_address(struct wolfBoot_image* img, uint8_t* image)
{
    uint32_t magic;
    uint32_t fw_size;

    memcpy(&magic, image, sizeof(magic));
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    memset(img, 0, sizeof(*img));
    img->hdr = image;
    memcpy(&fw_size, image + sizeof(uint32_t), sizeof(fw_size));
    img->fw_size = fw_size;
    img->fw_base = image + IMAGE_HEADER_SIZE;
    img->hdr_ok = 1;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image* img)
{
    int i;
    uint8_t fill = load_buffer[0];
    (void)img;
    for (i = 0; i < N_PARTS; i++) {
        if (mock_bad_fill[i] != 0 && mock_bad_fill[i] == fill)
            return -1;
    }
    img->sha_ok = 1;
    return 0;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    img->signature_ok = 1;
    return 0;
}

int wolfBoot_image_part_matches(struct wolfBoot_image* img)
{
    (void)img;
    return 0;
}

int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity)
{
    (void)capacity; (void)dts_addr;
    return -1;
}

void hal_prepare_boot(void)
{
}

void do_boot(const uint32_t *address)
{
    mock_do_boot_called++;
    mock_booted_fill = ((const uint8_t*)address)[0];
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

#include "update_disk.c"

static void set_state(int part, uint8_t state)
{
    uint64_t off = 0;
    ck_assert_int_eq(disk_trailer_offset(TEST_PART_SIZE, &off), 0);
    disk_trailer_encode(part_image[part] + off, state);
}

static uint8_t get_state(int part)
{
    uint64_t off = 0;
    ck_assert_int_eq(disk_trailer_offset(TEST_PART_SIZE, &off), 0);
    return disk_trailer_decode(part_image[part] + off);
}

static int head_is_zero(int part)
{
    int i;
    for (i = 0; i < IMAGE_HEADER_SIZE; i++) {
        if (part_image[part][i] != 0)
            return 0;
    }
    return 1;
}

START_TEST(test_no_failover_when_a_boots)
{
    reset_mocks();
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_A);
    ck_assert_int_eq(mock_key_erased, 0);
    ck_assert_int_eq(mock_writes[BOOT_PART_A], 0);
    ck_assert_int_eq(mock_writes[BOOT_PART_B], 0);
}
END_TEST

START_TEST(test_failover_erases_key_and_operational_slots)
{
    reset_mocks();
    /* B at A's version, so the flip to B is a verification failure and not
     * an anti-rollback refusal. */
    build_image(part_image[1], 3, FILL_B);
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_key_erased, 1);
    ck_assert_int_eq(failover_cause, DISK_FAILOVER_VERIFY);
    ck_assert(head_is_zero(BOOT_PART_A));
    ck_assert(head_is_zero(BOOT_PART_B));
    ck_assert(!head_is_zero(BOOT_PART_GOLDEN));
    ck_assert_int_eq(get_state(BOOT_PART_A), DISK_STATE_NEW);
    ck_assert_int_eq(mock_writes[BOOT_PART_GOLDEN], 0);
}
END_TEST

START_TEST(test_failover_runs_once_and_golden_still_boots_if_it_fails)
{
    /* The key erase fails, so hal_disk_failover() returns an error: the
     * golden image must still boot and the actions must not be retried. */
    reset_mocks();
    mock_key_erase_fail = 1;
    memset(part_image[0], 0, TEST_PART_SIZE);
    memset(part_image[1], 0, TEST_PART_SIZE);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_key_erased, 1);
    ck_assert_int_eq(failover_cause, DISK_FAILOVER_NO_IMAGE);
}
END_TEST

START_TEST(test_golden_boots_when_a_slot_head_cannot_be_erased)
{
    /* Slot A rejects every write.  The key is still erased, slot B is still
     * zeroed, and the golden image still boots. */
    reset_mocks();
    mock_write_fail_part = BOOT_PART_A;
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_key_erased, 1);
    ck_assert(!head_is_zero(BOOT_PART_A));
    ck_assert(head_is_zero(BOOT_PART_B));
    ck_assert_int_eq(mock_writes[BOOT_PART_GOLDEN], 0);
}
END_TEST

START_TEST(test_missing_key_boots_plaintext_golden)
{
    reset_mocks();
    mock_key_missing = 1;
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(failover_cause, DISK_FAILOVER_NO_KEY);
}
END_TEST

START_TEST(test_staged_slot_is_armed_for_testing)
{
    reset_mocks();
    set_state(BOOT_PART_A, DISK_STATE_UPDATING);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_A);
    ck_assert_int_eq(get_state(BOOT_PART_A), DISK_STATE_TESTING);
}
END_TEST

START_TEST(test_unconfirmed_slot_is_reaped)
{
    reset_mocks();
    set_state(BOOT_PART_A, DISK_STATE_TESTING);
    wolfBoot_start();

    /* A was left unconfirmed, so it is reaped and B (older) boots with no
     * failover action: one operational slot is still good. */
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_B);
    ck_assert_int_eq(mock_key_erased, 0);
    ck_assert(!head_is_zero(BOOT_PART_A));
}
END_TEST

START_TEST(test_both_unconfirmed_fail_over_with_the_unconfirmed_cause)
{
    reset_mocks();
    set_state(BOOT_PART_A, DISK_STATE_TESTING);
    set_state(BOOT_PART_B, DISK_STATE_TESTING);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_key_erased, 1);
    ck_assert_int_eq(failover_cause, DISK_FAILOVER_UNCONFIRMED);
    ck_assert(head_is_zero(BOOT_PART_A));
    ck_assert(head_is_zero(BOOT_PART_B));
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-failover");

    tcase_add_test(tc, test_no_failover_when_a_boots);
    tcase_add_test(tc, test_failover_erases_key_and_operational_slots);
    tcase_add_test(tc, test_failover_runs_once_and_golden_still_boots_if_it_fails);
    tcase_add_test(tc, test_golden_boots_when_a_slot_head_cannot_be_erased);
    tcase_add_test(tc, test_missing_key_boots_plaintext_golden);
    tcase_add_test(tc, test_staged_slot_is_armed_for_testing);
    tcase_add_test(tc, test_unconfirmed_slot_is_reaped);
    tcase_add_test(tc, test_both_unconfirmed_fail_over_with_the_unconfirmed_cause);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
