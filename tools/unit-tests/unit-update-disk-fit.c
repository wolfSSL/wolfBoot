/* unit-update-disk-fit.c
 *
 * Regression coverage for the FIT (flattened uImage tree) exit paths of
 * wolfBoot_start() in src/update_disk.c, with DISK_ENCRYPT enabled.
 *
 * Every terminal exit of wolfBoot_start() must scrub the disk decryption
 * key/nonce before handing control away.  wolfBoot_panic() is an unbounded
 * spin on real targets, so key material left resident there stays resident
 * forever.  These tests snapshot the module statics from the panic hook,
 * which runs at the top of wolfBoot_panic(), i.e. exactly at the moment the
 * bootloader stops making progress.
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
/* A plausible DTB size: at least WOLFBOOT_DTS_MIN_SIZE (the 40-byte
 * FDT v17 header). */
#define TEST_DTS_SIZE 48
/* Staging-region stand-in. The oversized test relies on it being big
 * enough that the pre-fix unbounded copy lands fully inside it, so the
 * regression is observable as "a copy happened" rather than a crash. */
#define TEST_DTS_STAGE_SIZE (2U * 1024U * 1024U)

static uint8_t load_buffer[TEST_PAYLOAD_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

static uint8_t dts_buffer[TEST_DTS_STAGE_SIZE];
#define WOLFBOOT_LOAD_DTS_ADDRESS ((uintptr_t)dts_buffer)

static uint8_t part_a_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static uint8_t part_b_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static uint8_t fit_dts_image[TEST_DTS_STAGE_SIZE];
/* Parsed DTB size the wolfBoot_get_dts_size() stub reports, and (pre
 * fix) the FIT-declared length the fit_load_image() stub returns. */
static int mock_dts_size;
static int mock_do_boot_called;
static int mock_fit_memcpy_ret;
static int mock_fit_memcpy_called;
static int mock_panic_hook_called;
/* Snapshot of the key material taken from inside wolfBoot_panic() */
static uint8_t panic_key_snapshot[ENCRYPT_KEY_SIZE];
static uint8_t panic_nonce_snapshot[ENCRYPT_NONCE_SIZE];

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
    memset(image, 0, IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE);
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
    memset(dts_buffer, 0, sizeof(dts_buffer));
    build_image(part_a_image, 1, 0xA1);
    build_image(part_b_image, 2, 0xB2);
    memset(fit_dts_image, 0xDD, sizeof(fit_dts_image));
    mock_dts_size = TEST_DTS_SIZE;
    mock_do_boot_called = 0;
    mock_fit_memcpy_ret = 0;
    mock_fit_memcpy_called = 0;
    mock_panic_hook_called = 0;
    memset(panic_key_snapshot, 0xFF, sizeof(panic_key_snapshot));
    memset(panic_nonce_snapshot, 0xFF, sizeof(panic_nonce_snapshot));
    wolfBoot_panicked = 0;
}

int chacha_init(void)
{
    return 0;
}

int wc_Chacha_SetIV(ChaCha* ctx, const byte* inIv, word32 counter)
{
    (void)ctx;
    (void)inIv;
    (void)counter;
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
    while (len-- > 0) {
        *p++ = 0;
    }
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
    uint8_t *image;
    uint64_t max = IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE;

    (void)drv;
    image = (part == BOOT_PART_B) ? part_b_image : part_a_image;
    if ((off > max) || (sz > (max - off)))
        return -1;
    memcpy(buf, image + off, (size_t)sz);
    return (int)sz;
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
    img->sha_ok = 1;
    return 0;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    img->signature_ok = 1;
    return 0;
}

/* The loaded payload is treated as a FIT container, and the sub-image
 * returned by fit_load_image() is a flat device tree whose parsed
 * size is mock_dts_size. */
int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity)
{
    (void)dts_addr;
    (void)capacity;
    return mock_dts_size;
}

/* Accept the staged payload as a FIT. The real parser validates it; here
 * we only need update_disk.c to take the FIT branch. */
int fdt_open(fdt_ctx* ctx, void* blob, uint32_t capacity)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->blob = (uint8_t*)blob;
    ctx->capacity = capacity;
    ctx->totalsize = mock_dts_size;
    return 0;
}

uint32_t fdt_size(const fdt_ctx* ctx)
{
    return (ctx != NULL) ? ctx->totalsize : 0;
}

const char* fit_find_images(fdt_ctx* ctx, const char** pkernel,
    const char** pflat_dt, const char** pramdisk, const char** pfpga)
{
    (void)ctx;
    if (pkernel != NULL)
        *pkernel = NULL;
    if (pflat_dt != NULL)
        *pflat_dt = "fdt";
    if (pramdisk != NULL)
        *pramdisk = NULL;
    if (pfpga != NULL)
        *pfpga = NULL;
    return "conf";
}

void* fit_load_image(fdt_ctx* ctx, const char* image, int* lenp)
{
    (void)ctx;
    (void)image;
    if (lenp != NULL)
        *lenp = mock_dts_size;
    return fit_dts_image;
}

int wolfBoot_fit_memcpy(void *dst, const void *src, uint32_t len)
{
    mock_fit_memcpy_called++;
    if (mock_fit_memcpy_ret != 0)
        return mock_fit_memcpy_ret;
    memcpy(dst, src, len);
    return 0;
}

void hal_prepare_boot(void)
{
}

void do_boot(const uint32_t *address, const uint32_t *dts_address)
{
    (void)dts_address;
    (void)address;
    mock_do_boot_called++;
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

#include "update_disk.c"

/* Runs from inside wolfBoot_panic(), before it spins forever on target. */
void wolfBoot_hook_panic(void)
{
    mock_panic_hook_called++;
    memcpy(panic_key_snapshot, disk_encrypt_key, sizeof(panic_key_snapshot));
    memcpy(panic_nonce_snapshot, disk_encrypt_nonce,
        sizeof(panic_nonce_snapshot));
}

static void assert_snapshot_zeroized(void)
{
    size_t i;

    for (i = 0; i < sizeof(panic_key_snapshot); i++) {
        ck_assert_uint_eq(panic_key_snapshot[i], 0);
    }
    for (i = 0; i < sizeof(panic_nonce_snapshot); i++) {
        ck_assert_uint_eq(panic_nonce_snapshot[i], 0);
    }
}

START_TEST(test_update_disk_fit_dts_copy_failure_zeroizes_key_material)
{
    reset_mocks();
    mock_fit_memcpy_ret = -1;

    wolfBoot_start();

    ck_assert_int_gt(mock_fit_memcpy_called, 0);
    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_gt(mock_panic_hook_called, 0);
    assert_snapshot_zeroized();
}
END_TEST

START_TEST(test_update_disk_fit_dts_copy_success_boots)
{
    reset_mocks();

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(dts_buffer, fit_dts_image, TEST_DTS_SIZE), 0);
}
END_TEST

/* A parsed DTB larger than the staging bound (WOLFBOOT_DTS_MAX_SIZE)
 * must be rejected rather than copied: before the fix the copy length
 * came from the FIT-declared property length, unbounded against the
 * staging region. */
START_TEST(test_update_disk_fit_dts_oversized_rejected)
{
    reset_mocks();
    mock_dts_size = (1024 * 1024) + 4; /* > WOLFBOOT_DTS_MAX_SIZE */

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    /* nothing may have been copied into the staging region */
    ck_assert_uint_eq(dts_buffer[0], 0);
}
END_TEST

/* A parsed DTB smaller than the FDT header size is a partial tree and
 * must be rejected. */
START_TEST(test_update_disk_fit_dts_below_min_rejected)
{
    reset_mocks();
    mock_dts_size = 32; /* < WOLFBOOT_DTS_MIN_SIZE (40) */

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_uint_eq(dts_buffer[0], 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-fit");

    tcase_add_test(tc, test_update_disk_fit_dts_copy_failure_zeroizes_key_material);
    tcase_add_test(tc, test_update_disk_fit_dts_oversized_rejected);
    tcase_add_test(tc, test_update_disk_fit_dts_below_min_rejected);
    tcase_add_test(tc, test_update_disk_fit_dts_copy_success_boots);
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
