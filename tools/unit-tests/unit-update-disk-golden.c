/* unit-update-disk-golden.c
 *
 * Golden slot of the disk boot path (DISK_GOLDEN_SLOT): tried once after the
 * A/B attempts are spent, exempt from anti-rollback, never written.
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
static int mock_bad_fill[N_PARTS];      /* payload fills that fail integrity */
static int mock_do_boot_called;
static int mock_booted_fill;
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
    return 0;
}

int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    memset(key, 0x5A, ENCRYPT_KEY_SIZE);
    memset(nonce, 0xC3, ENCRYPT_NONCE_SIZE);
    return 0;
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

/* The loader lands the payload itself at WOLFBOOT_LOAD_ADDRESS (the header
 * stays in its own buffer), so the fill byte of whatever was loaded is the
 * first byte of load_buffer. */
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

/* Trailer in the partition tail, as include/disk_trailer.h lays it out. */
static void set_state(int part, uint8_t state)
{
    uint64_t off = 0;
    ck_assert_int_eq(disk_trailer_offset(TEST_PART_SIZE, &off), 0);
    disk_trailer_encode(part_image[part] + off, state);
}

START_TEST(test_golden_untouched_when_a_boots)
{
    reset_mocks();
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(mock_booted_fill, FILL_A);
    ck_assert_int_eq(wolfBoot_disk_boot_part(), BOOT_PART_A);
    ck_assert_int_eq(mock_reads[BOOT_PART_GOLDEN], 0);
    ck_assert_int_eq(mock_writes[BOOT_PART_GOLDEN], 0);
}
END_TEST

START_TEST(test_golden_boots_after_a_and_b_fail_despite_lower_version)
{
    reset_mocks();
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(wolfBoot_disk_boot_part(), BOOT_PART_GOLDEN);
    /* A was tried; B, older than the failed A, is refused by anti-rollback:
     * only its header and its state trailer were read, never its payload. */
    ck_assert_int_ge(mock_reads[BOOT_PART_A], 3);
    ck_assert_int_le(mock_reads[BOOT_PART_B], 2);
}
END_TEST

START_TEST(test_golden_boots_when_both_slots_are_blank)
{
    reset_mocks();
    memset(part_image[0], 0, TEST_PART_SIZE);
    memset(part_image[1], 0, TEST_PART_SIZE);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
}
END_TEST

START_TEST(test_golden_boots_when_a_is_version_zero)
{
    reset_mocks();
    /* A valid, signed image whose version parses as 0 next to a blank B is
     * "no valid image", not a bootable slot with the version guard relaxed. */
    build_image(part_image[0], 0, FILL_A);
    memset(part_image[1], 0, TEST_PART_SIZE);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(wolfBoot_disk_boot_part(), BOOT_PART_GOLDEN);
}
END_TEST

START_TEST(test_golden_is_verified_and_a_bad_golden_panics)
{
    reset_mocks();
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    mark_bad(FILL_G);
    wolfBoot_start();
    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_ge(mock_reads[BOOT_PART_GOLDEN], 1);
}
END_TEST

START_TEST(test_golden_is_never_written_and_skips_confirmation)
{
    reset_mocks();
    set_state(BOOT_PART_A, DISK_STATE_UPDATING);
    set_state(BOOT_PART_GOLDEN, DISK_STATE_UPDATING);
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_writes[BOOT_PART_GOLDEN], 0);
}
END_TEST

START_TEST(test_golden_boots_when_a_is_unconfirmed_and_b_bad)
{
    reset_mocks();
    set_state(BOOT_PART_A, DISK_STATE_TESTING);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_writes[BOOT_PART_A], 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-golden");

    tcase_add_test(tc, test_golden_untouched_when_a_boots);
    tcase_add_test(tc, test_golden_boots_after_a_and_b_fail_despite_lower_version);
    tcase_add_test(tc, test_golden_boots_when_both_slots_are_blank);
    tcase_add_test(tc, test_golden_boots_when_a_is_version_zero);
    tcase_add_test(tc, test_golden_is_verified_and_a_bad_golden_panics);
    tcase_add_test(tc, test_golden_is_never_written_and_skips_confirmation);
    tcase_add_test(tc, test_golden_boots_when_a_is_unconfirmed_and_b_bad);

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
