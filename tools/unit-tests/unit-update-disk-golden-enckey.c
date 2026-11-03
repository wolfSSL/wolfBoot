/* unit-update-disk-golden-enckey.c
 *
 * Unit test for a golden slot encrypted under its own key
 * (DISK_GOLDEN_ENCRYPT_KEY), so a zeroize of the operational key leaves the
 * recovery image both encrypted and bootable.
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
#define DISK_FAILOVER_ZEROIZE
#define DISK_GOLDEN_ENCRYPT_KEY
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

/* The two image keys.  The cipher mock below is an XOR with the installed key
 * byte, so a slot read under the wrong key yields a wrong magic and a wrong
 * payload fill: key separation is actually exercised, not just recorded. */
#define OP_KEY_BYTE     0x5A
#define GOLDEN_KEY_BYTE 0x77

/* The region of a partition that is encrypted; the trailer is not. */
#define ENC_LEN (IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE)

static uint8_t load_buffer[TEST_PART_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

static uint8_t part_image[N_PARTS][TEST_PART_SIZE];
static int mock_reads[N_PARTS];
static int mock_writes[N_PARTS];
static int mock_bad_fill[N_PARTS];
static int mock_do_boot_called;
static int mock_booted_fill;
static int mock_op_key_gone;
static int mock_golden_key_gone;
static int mock_op_key_erased;
static int mock_golden_key_erased;
static int mock_golden_selected;
static uint8_t mock_pending_key;
static uint8_t mock_cur_key;
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

/* Build a plaintext image, then "encrypt" the image region under key_byte. */
static void build_image(uint8_t *image, uint32_t version, uint8_t fill,
    uint8_t key_byte)
{
    int i;

    memset(image, 0, TEST_PART_SIZE);
    set_u32_le(image, WOLFBOOT_MAGIC);
    set_u32_le(image + sizeof(uint32_t), TEST_PAYLOAD_SIZE);
    set_u16_le(image + IMAGE_HEADER_OFFSET, HDR_VERSION);
    set_u16_le(image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), 4);
    set_u32_le(image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), version);
    memset(image + IMAGE_HEADER_SIZE, fill, TEST_PAYLOAD_SIZE);
    for (i = 0; i < ENC_LEN; i++) {
        image[i] ^= key_byte;
    }
}

static void reset_mocks(void)
{
    memset(load_buffer, 0, sizeof(load_buffer));
    build_image(part_image[0], 3, FILL_A, OP_KEY_BYTE);
    build_image(part_image[1], 2, FILL_B, OP_KEY_BYTE);
    build_image(part_image[2], 1, FILL_G, GOLDEN_KEY_BYTE);
    memset(mock_reads, 0, sizeof(mock_reads));
    memset(mock_writes, 0, sizeof(mock_writes));
    memset(mock_bad_fill, 0, sizeof(mock_bad_fill));
    mock_do_boot_called = 0;
    mock_booted_fill = -1;
    mock_op_key_gone = 0;
    mock_golden_key_gone = 0;
    mock_op_key_erased = 0;
    mock_golden_key_erased = 0;
    mock_golden_selected = -1;
    mock_pending_key = OP_KEY_BYTE;
    mock_cur_key = OP_KEY_BYTE;
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

static int pending_key_gone(void)
{
    if (mock_pending_key == GOLDEN_KEY_BYTE) {
        return mock_golden_key_gone;
    }
    return mock_op_key_gone;
}

/* crypto_init() expands to this: it installs the selected key, the way the
 * real one re-reads it from storage. */
int chacha_init(void)
{
    if (pending_key_gone()) {
        return -1;
    }
    mock_cur_key = mock_pending_key;
    return 0;
}

int wc_Chacha_SetIV(ChaCha* ctx, const byte* inIv, word32 counter)
{
    (void)ctx; (void)inIv; (void)counter;
    return 0;
}

int wc_Chacha_Process(ChaCha* ctx, byte* output, const byte* input,
    word32 msglen)
{
    word32 i;
    (void)ctx;
    for (i = 0; i < msglen; i++) {
        output[i] = (byte)(input[i] ^ mock_cur_key);
    }
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
    return chacha_init();
}

int wolfBoot_select_golden_encrypt_key(int golden)
{
    mock_golden_selected = golden;
    mock_pending_key = golden ? GOLDEN_KEY_BYTE : OP_KEY_BYTE;
    return 0;
}

/* Returns key material only.  Installing it into the cipher is chacha_init()'s
 * job, exactly as in production: if this installed the key too, a
 * disk_crypto_rekey() that forgot crypto_init() would still pass. */
int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    if (pending_key_gone()) {
        return -1;
    }
    memset(key, mock_pending_key, ENCRYPT_KEY_SIZE);
    memset(nonce, 0xC3, ENCRYPT_NONCE_SIZE);
    return 0;
}

/* The failover only ever erases the operational key.  The golden counter is
 * here so a regression that erased it would fail the suite. */
int wolfBoot_erase_encrypt_key(void)
{
    mock_op_key_erased++;
    mock_op_key_gone = 1;
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

START_TEST(test_operational_boots_under_its_own_key)
{
    reset_mocks();
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_A);
    ck_assert_int_eq(mock_op_key_erased, 0);
    ck_assert_int_eq(mock_golden_key_erased, 0);
}
END_TEST

START_TEST(test_encrypted_golden_boots_after_the_zeroize)
{
    /* The case that is impossible without this option: both operational
     * images are given up on, the failover zeroizes their key, and the
     * golden image still decrypts because its key is a different one. */
    reset_mocks();
    build_image(part_image[1], 3, FILL_B, OP_KEY_BYTE);
    mark_bad(FILL_A);
    mark_bad(FILL_B);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_op_key_erased, 1);
    ck_assert_int_eq(mock_golden_selected, 1);
    ck_assert_int_eq(mock_writes[BOOT_PART_GOLDEN], 0);
}
END_TEST

START_TEST(test_failover_leaves_the_golden_key_alone)
{
    reset_mocks();
    memset(part_image[0], 0, TEST_PART_SIZE);
    memset(part_image[1], 0, TEST_PART_SIZE);
    wolfBoot_start();
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_booted_fill, FILL_G);
    ck_assert_int_eq(mock_op_key_erased, 1);
    /* The whole point: the recovery key survives the zeroize. */
    ck_assert_int_eq(mock_golden_key_erased, 0);
    ck_assert_int_eq(mock_golden_key_gone, 0);
}
END_TEST

START_TEST(test_golden_fails_closed_without_its_own_key)
{
    /* No silent plaintext fallback: with the golden key gone there is
     * nothing left to boot. */
    reset_mocks();
    mock_op_key_gone = 1;
    mock_golden_key_gone = 1;
    wolfBoot_start();
    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_eq(wolfBoot_panicked, 1);
}
END_TEST

START_TEST(test_golden_under_the_operational_key_does_not_boot)
{
    /* A golden image encrypted with the operational key must not boot once
     * the golden slot is read under the golden key. */
    reset_mocks();
    build_image(part_image[2], 1, FILL_G, OP_KEY_BYTE);
    memset(part_image[0], 0, TEST_PART_SIZE);
    memset(part_image[1], 0, TEST_PART_SIZE);
    wolfBoot_start();
    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_eq(wolfBoot_panicked, 1);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-golden-enckey");

    tcase_add_test(tc, test_operational_boots_under_its_own_key);
    tcase_add_test(tc, test_encrypted_golden_boots_after_the_zeroize);
    tcase_add_test(tc, test_failover_leaves_the_golden_key_alone);
    tcase_add_test(tc, test_golden_fails_closed_without_its_own_key);
    tcase_add_test(tc, test_golden_under_the_operational_key_does_not_boot);

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
