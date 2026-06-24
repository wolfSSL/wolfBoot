/* unit-update-disk-oob.c
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

/* The secure cap on the RAM load region. Kept in sync with the
 * -DWOLFBOOT_RAMBOOT_MAX_SIZE passed by the Makefile so the same value both
 * sizes the load buffer here and bounds the loader (src/update_disk.c). */
#ifdef WOLFBOOT_RAMBOOT_MAX_SIZE
#define DISK_LOAD_MAX WOLFBOOT_RAMBOOT_MAX_SIZE
#else
#define DISK_LOAD_MAX 0x1000
#endif

/* How far past the load region the malicious header drives the write, and the
 * size of the guard band used to detect it. The overshoot is intentionally
 * kept smaller than the canary so the OOB write is contained and observable
 * (rather than wandering into unrelated memory). */
#define OOB_OVERSHOOT     1024
#define CANARY_SIZE       2048
#define ATTACKER_FW_SIZE  (DISK_LOAD_MAX + OOB_OVERSHOOT)
#define CANARY_FILL       0xCC

/* Load region followed immediately by a guard band. Members of a single
 * struct are laid out in declaration order, so any write past load[] lands
 * in canary[] where we can detect it without relying on ASan. */
static struct {
    uint8_t load[DISK_LOAD_MAX];
    uint8_t canary[CANARY_SIZE];
} region;
#ifdef WOLFBOOT_LOAD_ADDRESS
#undef WOLFBOOT_LOAD_ADDRESS
#endif
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)region.load)

/* Backing store for the partitions. Partition A is sized to the attacker's
 * declared image so the mock disk serves every requested byte (modelling a
 * large attacker-written partition); partition B is left blank. */
static uint8_t part_a_image[IMAGE_HEADER_SIZE + ATTACKER_FW_SIZE];
static uint8_t part_b_image[IMAGE_HEADER_SIZE];

static int mock_disk_init_ret;
static int mock_disk_close_called;
static int mock_do_boot_called;
static const uint32_t *mock_boot_address;
static int mock_verify_integrity_ret;
static int mock_verify_authenticity_ret;
static int mock_flash_protect_called;
static haladdr_t mock_flash_protect_addr;
static int mock_flash_protect_len;

static void build_part_a(uint32_t version, uint32_t fw_size, uint8_t fill)
{
    uint32_t magic = WOLFBOOT_MAGIC;
    uint16_t hdr_version = HDR_VERSION;
    uint16_t ver_len = 4;

    memset(part_a_image, 0, sizeof(part_a_image));
    memcpy(part_a_image, &magic, sizeof(magic));
    memcpy(part_a_image + sizeof(uint32_t), &fw_size, sizeof(fw_size));
    memcpy(part_a_image + IMAGE_HEADER_OFFSET, &hdr_version,
        sizeof(hdr_version));
    memcpy(part_a_image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), &ver_len,
        sizeof(ver_len));
    memcpy(part_a_image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), &version,
        sizeof(version));
    memset(part_a_image + IMAGE_HEADER_SIZE, fill,
        sizeof(part_a_image) - IMAGE_HEADER_SIZE);
}

static void reset_mocks(void)
{
    memset(region.load, 0, sizeof(region.load));
    memset(region.canary, CANARY_FILL, sizeof(region.canary));
    memset(part_b_image, 0, sizeof(part_b_image));
    mock_disk_init_ret = 0;
    mock_disk_close_called = 0;
    mock_do_boot_called = 0;
    mock_boot_address = NULL;
    mock_verify_integrity_ret = 0;
    mock_verify_authenticity_ret = 0;
    mock_flash_protect_called = 0;
    mock_flash_protect_addr = 0;
    mock_flash_protect_len = 0;
    wolfBoot_panicked = 0;
}

static int canary_intact(void)
{
    size_t i;
    for (i = 0; i < sizeof(region.canary); i++) {
        if (region.canary[i] != CANARY_FILL)
            return 0;
    }
    return 1;
}

int disk_init(int drv)
{
    (void)drv;
    return mock_disk_init_ret;
}

int disk_open(int drv)
{
    (void)drv;
    return 0;
}

void disk_close(int drv)
{
    (void)drv;
    mock_disk_close_called++;
}

/* Serve bytes from the backing partition, clamped only to the partition's own
 * size - exactly like a real disk driver, and crucially NOT to the size of the
 * RAM destination. */
int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    const uint8_t *image;
    uint64_t max;

    (void)drv;
    if (part == BOOT_PART_A) {
        image = part_a_image;
        max = sizeof(part_a_image);
    } else {
        image = part_b_image;
        max = sizeof(part_b_image);
    }
    if (off >= max)
        return -1;
    if (sz > (max - off))
        sz = max - off;
    memcpy(buf, image + off, (size_t)sz);
    return (int)sz;
}

uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    uint32_t magic;
    uint32_t version;

    memcpy(&magic, blob, sizeof(magic));
    if (magic != WOLFBOOT_MAGIC)
        return 0;
    memcpy(&version, blob + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t),
        sizeof(version));
    return version;
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
    img->sha_ok = (mock_verify_integrity_ret == 0) ? 1 : 0;
    return mock_verify_integrity_ret;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    img->signature_ok = (mock_verify_authenticity_ret == 0) ? 1 : 0;
    return mock_verify_authenticity_ret;
}

int wolfBoot_get_dts_size(void *dts_addr)
{
    (void)dts_addr;
    return -1;
}

void hal_prepare_boot(void)
{
}

void do_boot(const uint32_t *address)
{
    mock_do_boot_called++;
    mock_boot_address = address;
}

int hal_flash_protect(haladdr_t address, int len)
{
    mock_flash_protect_called++;
    mock_flash_protect_addr = address;
    mock_flash_protect_len = len;
    return 0;
}

#include "update_disk.c"

/* A header that declares fw_size larger than the RAM load region must not be
 * copied into RAM: the write would overrun the load buffer before the image
 * is ever authenticated. The loader must refuse it (panic, no boot) and leave
 * the guard band untouched. */
START_TEST(test_update_disk_oob_rejects_oversize_fw_size)
{
    reset_mocks();
    build_part_a(7, ATTACKER_FW_SIZE, 0xEE);
    /* partition B left blank so A is the only candidate */

    wolfBoot_start();

    ck_assert_msg(canary_intact(),
        "fw_size=%u overran the %u-byte load region into the guard band",
        (unsigned)ATTACKER_FW_SIZE, (unsigned)DISK_LOAD_MAX);
    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_gt(wolfBoot_panicked, 0);
}
END_TEST

/* Boundary companion: an image exactly the size of the load region is valid
 * and must still boot. Guards the fix against an off-by-one (it must reject
 * strictly-greater-than, not greater-or-equal). */
START_TEST(test_update_disk_accepts_exact_max_fw_size)
{
    reset_mocks();
    build_part_a(7, DISK_LOAD_MAX, 0xEE);

    wolfBoot_start();

    ck_assert_msg(canary_intact(),
        "exact-max fw_size=%u overran the load region",
        (unsigned)DISK_LOAD_MAX);
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_ptr_eq(mock_boot_address, (const uint32_t *)WOLFBOOT_LOAD_ADDRESS);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-oob");

    tcase_add_test(tc, test_update_disk_oob_rejects_oversize_fw_size);
    tcase_add_test(tc, test_update_disk_accepts_exact_max_fw_size);
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
