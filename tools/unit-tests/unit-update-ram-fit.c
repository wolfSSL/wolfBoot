/* unit-update-ram-fit.c
 *
 * loader-level tests for the FIT device-tree fallback and the deferred
 * initrd fixup in update_ram.c
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
#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#include "target.h"
static __thread unsigned char wolfboot_ram[2 * WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE];

#define WOLFBOOT_LOAD_ADDRESS (((uintptr_t)wolfboot_ram + IMAGE_HEADER_SIZE))

#define TEST_SIZE_SMALL 5300
#define TEST_SIZE_LARGE 9800

#define NO_FORK 1 /* Set to 1 to disable fork mode (e.g. for gdb debugging) */

#include <stdio.h>
#include <stdlib.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
/* FDT/FIT stubs, declared before update_ram.c is pulled in. The payload
 * does not have to be a real FIT: fdt_open() below accepts anything, so
 * wolfBoot_start() always takes the FIT branch. */
static const char *mock_kernel = "kernel-1";
static const char *mock_flat_dt;        /* NULL = kernel-only FIT */
static const char *mock_ramdisk = "ramdisk-1";
static int mock_get_dts_calls;
static int mock_ramdisk_calls;
static int mock_ramdisk_saw_get_dts;
static void *mock_ramdisk_dts;
/* The device tree the boot firmware hands us, as a bootgen raw partition
 * would. Big enough for the staging copy update_ram.c makes. */
#include "fdt.h"
static uint8_t firmware_dtb[WOLFBOOT_DTS_MAX_SIZE];
static uint8_t dts_stage[WOLFBOOT_DTS_MAX_SIZE];
#define WOLFBOOT_LOAD_DTS_ADDRESS ((uintptr_t)dts_stage)

void* hal_get_dts_address(void)
{
    mock_get_dts_calls++;
    return firmware_dtb;
}

int hal_dts_fixup(void *dts_addr, uint32_t capacity)
{
    (void)dts_addr;
    (void)capacity;
    return 0;
}

int fdt_open(fdt_ctx* ctx, void* blob, uint32_t capacity)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->blob = blob;
    ctx->capacity = capacity;
    ctx->totalsize = 64;
    return 0;
}

uint32_t fdt_size(const fdt_ctx* ctx)
{
    return (ctx != NULL) ? ctx->totalsize : 0;
}

int fdt_peek_size(const void* hdr, uint32_t hdr_len, uint32_t* totalsize)
{
    (void)hdr;
    (void)hdr_len;
    if (totalsize != NULL)
        *totalsize = 64;
    return 0;
}

void fdt_set_dtb_authenticated(int authenticated)
{
    (void)authenticated;
}

const char* fit_find_images(fdt_ctx* ctx, const char** pkernel,
    const char** pflat_dt, const char** pramdisk, const char** pfpga)
{
    (void)ctx;
    if (pkernel != NULL)
        *pkernel = mock_kernel;
    if (pflat_dt != NULL)
        *pflat_dt = mock_flat_dt;
    if (pramdisk != NULL)
        *pramdisk = mock_ramdisk;
    if (pfpga != NULL)
        *pfpga = NULL;
    return "conf-1";
}

void* fit_load_image(fdt_ctx* ctx, const char* image, int* lenp)
{
    (void)ctx;
    (void)image;
    if (lenp != NULL)
        *lenp = 64;
    return firmware_dtb;
}

void* fit_load_kernel(fdt_ctx* ctx, const char* kernel_node, int* lenp)
{
    (void)ctx;
    (void)kernel_node;
    if (lenp != NULL)
        *lenp = 64;
    /* Any in-range address; the test only cares about the DTB path. */
    return (void*)WOLFBOOT_LOAD_ADDRESS;
}

int fit_load_ramdisk(fdt_ctx* ctx, const char* ramdisk_node, fdt_ctx* dts)
{
    (void)ctx;
    (void)ramdisk_node;
    mock_ramdisk_calls++;
    mock_ramdisk_saw_get_dts = mock_get_dts_calls;
    mock_ramdisk_dts = dts;
    return 0;
}

#include "update_ram.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

const char *argv0;

Suite *wolfboot_suite(void);

int wolfBoot_staged_ok = 0;
const uint32_t *wolfBoot_stage_address = (uint32_t *) 0xFFFFFFFF;

void do_boot(const uint32_t *address, const uint32_t *dts)
{
    struct wolfBoot_image boot_image;
    (void)dts;
    /* Mock of do_boot */
    if (wolfBoot_panicked)
        return;
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    ck_assert_uint_eq((uintptr_t)address, WOLFBOOT_LOAD_ADDRESS);
    memset(&boot_image, 0, sizeof(boot_image));
    printf("Called do_boot with address %p\n", address);
    ck_assert_uint_eq(0,wolfBoot_open_image_address(&boot_image, wolfboot_ram));
    boot_image.hdr = wolfboot_ram;
    boot_image.fw_base = (void *)(uintptr_t)WOLFBOOT_LOAD_ADDRESS;
    boot_image.part = 0;
    boot_image.not_ext = 1;
    ck_assert_uint_eq(0,wolfBoot_verify_integrity(&boot_image));

}

static int mock_flash_protect_called = 0;
static haladdr_t mock_flash_protect_addr = 0;
static int mock_flash_protect_len = 0;

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
    mock_flash_protect_called = 0;
    mock_flash_protect_addr = 0;
    mock_flash_protect_len = 0;
}

int hal_flash_protect(haladdr_t address, int len)
{
    mock_flash_protect_called++;
    mock_flash_protect_addr = address;
    mock_flash_protect_len = len;
    return 0;
}

uint32_t get_version_ramloaded(void)
{
    return wolfBoot_get_blob_version(wolfboot_ram);
}

static void assert_part_state(uint8_t part, uint8_t expected)
{
    uint8_t st = 0xBB;
    ck_assert_int_eq(wolfBoot_get_partition_state(part, &st), 0);
    ck_assert_uint_eq(st, expected);
}


static void prepare_flash(void)
{
    int ret;
    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin", (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin", (void *)(uintptr_t)MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ext_flash_unlock();
    ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_lock();
}

static void cleanup_flash(void)
{
    munmap((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    munmap((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
}


#define DIGEST_TLV_OFF_IN_HDR 28
static int add_payload(uint8_t part, uint32_t version, uint32_t size)
{
    uint32_t word;
    uint16_t word16;
    int i;
    uint8_t *base = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;


    if (part == PART_UPDATE)
        base = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    srandom(part); /* Ensure reproducible "random" image */


    ext_flash_unlock();
    ext_flash_write((uintptr_t)base, "WOLF", 4);
    printf("Written magic: \"WOLF\"\n");

    ext_flash_write((uintptr_t)base + 4, (void *)&size, 4);
    printf("Written size: %u\n", size);

    /* Headers */
    word = 4 << 16 | HDR_VERSION;
    ext_flash_write((uintptr_t)base + 8, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + 12, (void *)&version, 4);
    printf("Written version: %u\n", version);

    word = 2 << 16 | HDR_IMG_TYPE;
    ext_flash_write((uintptr_t)base + 16, (void *)&word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    ext_flash_write((uintptr_t)base + 20, (void *)&word16, 2);
    printf("Written img_type: %04X\n", word16);

    /* Add 28B header to sha calculation */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    /* Payload */
    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < size; i+=4) {
        uint32_t word = (random() << 16) | random();
        ext_flash_write((uintptr_t)base + i, (void *)&word, 4);
    }
    for (i = IMAGE_HEADER_SIZE; i < size; i+= WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((size - i) < len)
            len = size - i;
        ret = wc_Sha256Update(&sha, base + i, len);
        if (ret != 0)
            return ret;
    }

    /* Calculate final digest */
    ret = wc_Sha256Final(&sha, digest);
    if (ret != 0)
        return ret;
    wc_Sha256Free(&sha);

    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
            SHA256_DIGEST_SIZE);
    printf("SHA digest written\n");
    for (i = 0; i < 32; i++) {
        printf("%02x ", digest[i]);
    }
    printf("\n");
    ext_flash_lock();

    return 0;
}

static void reset_fit_mocks(void)
{
    reset_mock_stats();
    mock_kernel = "kernel-1";
    mock_flat_dt = NULL;
    mock_ramdisk = "ramdisk-1";
    mock_get_dts_calls = 0;
    mock_ramdisk_calls = 0;
    mock_ramdisk_saw_get_dts = 0;
    mock_ramdisk_dts = NULL;
    memset(firmware_dtb, 0, sizeof(firmware_dtb));
    memset(dts_stage, 0, sizeof(dts_stage));
}

/* A kernel-only FIT - no `fdt` sub-image - has to fall back to the device
 * tree the boot firmware left for us, and the initrd fixup has to be
 * applied to THAT tree. Both only happen if the fixup runs after the
 * fallback; while it lived inside the FIT block it ran first, and a
 * kernel-only FIT reached Linux with neither a device tree nor an
 * initrd. */
START_TEST (test_ram_fit_kernel_only_uses_firmware_dtb_for_initrd)
{
    reset_fit_mocks();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);

    wolfBoot_start();

    /* The firmware DTB was consulted ... */
    ck_assert_int_gt(mock_get_dts_calls, 0);
    /* ... the fixup ran ... */
    ck_assert_int_eq(mock_ramdisk_calls, 1);
    /* ... and it ran after that fallback, not before it. */
    ck_assert_int_gt(mock_ramdisk_saw_get_dts, 0);
    /* ... against a real tree rather than NULL. */
    ck_assert_ptr_nonnull(mock_ramdisk_dts);
    cleanup_flash();
}
END_TEST

/* When the FIT does carry its own fdt, that one wins and the firmware
 * fallback is not consulted - but the fixup still runs exactly once. */
START_TEST (test_ram_fit_own_dtb_skips_firmware_fallback)
{
    reset_fit_mocks();
    mock_flat_dt = "fdt-1";
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);

    wolfBoot_start();

    ck_assert_int_eq(mock_get_dts_calls, 0);
    ck_assert_int_eq(mock_ramdisk_calls, 1);
    ck_assert_ptr_nonnull(mock_ramdisk_dts);
    cleanup_flash();
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-ram-fit");
    tcase_set_timeout(tc, 20);
    tcase_add_test(tc, test_ram_fit_kernel_only_uses_firmware_dtb_for_initrd);
    tcase_add_test(tc, test_ram_fit_own_dtb_skips_firmware_fallback);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    SRunner *sr = srunner_create(wolfboot_suite());
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails ? 1 : 0;
}
