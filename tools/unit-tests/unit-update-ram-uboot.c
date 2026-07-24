/* unit-update-ram-uboot.c
 *
 * Regression test for U-Boot legacy (uImage) ih_load handling under RAMBOOT.
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

/* Under WOLFBOOT_USE_RAMBOOT the generic copy-to-RAM memcpy in wolfBoot_start()
 * is compiled out, so a uImage with a non-zero ih_load that differs from the
 * RAMBOOT staging address must be relocated explicitly (via memmove) or
 * do_boot() jumps to memory that was never written. This test drives the real
 * wolfBoot_start() control flow (update_ram.c is #included below) against a
 * mocked flash + RAM and asserts the payload lands at ih_load. */

#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif
#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#include "target.h"

/* The uImage ih_load field is 32-bit, so the RAMBOOT staging region and the
 * relocation targets must live at fixed low 32-bit addresses (representable in
 * the header). These are MAP hints honored by mmap() (asserted below), the same
 * technique the flash partitions (0xCC/CD/CE...) use. */
#define RAM_STAGING_BASE  0xC8000000UL /* == WOLFBOOT_LOAD_ADDRESS - hdr */
#define RAM_STAGING_SIZE  (WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE)
#define IHLOAD_HI_BASE    0xC9000000UL /* distinct, higher relocation target */
#define IHLOAD_HI_SIZE    0x1000UL

#define WOLFBOOT_LOAD_ADDRESS (RAM_STAGING_BASE + IMAGE_HEADER_SIZE)

#define KERNEL_LEN 512 /* > UBOOT_IMG_HDR_SZ so the lower-target case overlaps */

#define NO_FORK 1 /* Set to 1 to disable fork mode (e.g. for gdb debugging) */

#include <stdio.h>
#include <stdlib.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include "update_ram.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

#ifndef WOLFBOOT_UBOOT_LEGACY
#error "unit-update-ram-uboot requires -DWOLFBOOT_UBOOT_LEGACY"
#endif
#ifndef WOLFBOOT_USE_RAMBOOT
#error "unit-update-ram-uboot requires a RAMBOOT config (EXT_FLASH + NO_XIP)"
#endif

const char *argv0;

Suite *wolfboot_suite(void);

/* do_boot mock: capture the jump target instead of asserting a fixed address
 * (unlike unit-update-ram.c, ih_load varies per case here). */
static const uint32_t *g_boot_addr;
static int g_boot_called;

void do_boot(const uint32_t *address)
{
    if (wolfBoot_panicked)
        return;
    g_boot_called++;
    g_boot_addr = address;
    printf("Called do_boot with address %p\n", (void*)address);
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

static uint8_t expected_kernel[KERNEL_LEN];
static uint8_t *g_staging;
static uint8_t *g_ihload_hi;

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    g_boot_called = 0;
    g_boot_addr = NULL;
}

static void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static void prepare_flash(void)
{
    int ret;
    ret = mmap_file("/tmp/wolfboot-unit-uboot-ext.bin",
            (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-uboot-int.bin",
            (void *)(uintptr_t)MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ext_flash_unlock();
    ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_lock();
}

static void cleanup_flash(void)
{
    munmap((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    munmap((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
}

/* Map the RAMBOOT staging region and the higher relocation target at their
 * fixed 32-bit addresses. Assert the kernel honored the hint so a wrong ih_load
 * dereference fails loudly instead of corrupting unrelated memory. */
static void prepare_ram(void)
{
    int ret;
    g_staging = NULL;
    g_ihload_hi = NULL;
    ret = mmap_file("/tmp/wolfboot-unit-uboot-ram.bin",
            (void *)(uintptr_t)RAM_STAGING_BASE, RAM_STAGING_SIZE, &g_staging);
    ck_assert(ret >= 0);
    ck_assert_uint_eq((uintptr_t)g_staging, RAM_STAGING_BASE);
    ret = mmap_file("/tmp/wolfboot-unit-uboot-hi.bin",
            (void *)(uintptr_t)IHLOAD_HI_BASE, IHLOAD_HI_SIZE, &g_ihload_hi);
    ck_assert(ret >= 0);
    ck_assert_uint_eq((uintptr_t)g_ihload_hi, IHLOAD_HI_BASE);
}

static void cleanup_ram(void)
{
    munmap((void *)(uintptr_t)RAM_STAGING_BASE, RAM_STAGING_SIZE);
    munmap((void *)(uintptr_t)IHLOAD_HI_BASE, IHLOAD_HI_SIZE);
}

#define DIGEST_TLV_OFF_IN_HDR 28
/* Write a wolfBoot image to the BOOT partition whose firmware payload is a
 * uImage: [64-byte uImage header][KERNEL_LEN kernel bytes]. ih_load is set to
 * the caller-provided value; the uImage magic/size/header-CRC are made valid so
 * uboot_legacy_header_valid() accepts it. Fills expected_kernel[] with the
 * kernel pattern for later comparison. Returns 0 on success. */
static int add_uimage_payload(uint32_t version, uint32_t ih_load)
{
    uint8_t *base = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint8_t uimg[UBOOT_IMG_HDR_SZ + KERNEL_LEN];
    struct gpt_crc32_ctx cx;
    uint32_t word;
    uint16_t word16;
    uint32_t size = UBOOT_IMG_HDR_SZ + KERNEL_LEN;
    uint32_t hcrc;
    int i;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    /* Build the uImage in a scratch buffer. */
    memset(uimg, 0, sizeof(uimg));
    uimg[0] = 0x27; uimg[1] = 0x05; uimg[2] = 0x19; uimg[3] = 0x56; /* magic */
    /* uimg[4..8] = ih_hcrc, left 0 while computing the header CRC. */
    store_be32(uimg + 0x0C, KERNEL_LEN);   /* ih_size */
    store_be32(uimg + 0x10, ih_load);      /* ih_load */
    store_be32(uimg + 0x14, ih_load);      /* ih_ep (unused by wolfBoot) */
    for (i = 0; i < KERNEL_LEN; i++) {
        uint8_t b = (uint8_t)(0xA5u ^ (uint8_t)i);
        uimg[UBOOT_IMG_HDR_SZ + i] = b;
        expected_kernel[i] = b;
    }
    /* Header CRC32 over the 64-byte header with hcrc==0 (matches the validator
     * in uboot_legacy_header_valid()). */
    gpt_crc32_init(&cx);
    gpt_crc32_update(&cx, uimg, UBOOT_IMG_HDR_SZ);
    hcrc = gpt_crc32_final(&cx);
    store_be32(uimg + 0x04, hcrc);

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;

    ext_flash_unlock();
    ext_flash_write((uintptr_t)base, "WOLF", 4);
    ext_flash_write((uintptr_t)base + 4, (void *)&size, 4);
    word = 4 << 16 | HDR_VERSION;
    ext_flash_write((uintptr_t)base + 8, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + 12, (void *)&version, 4);
    word = 2 << 16 | HDR_IMG_TYPE;
    ext_flash_write((uintptr_t)base + 16, (void *)&word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    ext_flash_write((uintptr_t)base + 20, (void *)&word16, 2);

    /* SHA256 over the first 28 header bytes + the whole firmware payload. */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;
    ext_flash_write((uintptr_t)base + IMAGE_HEADER_SIZE, uimg, size);
    for (i = 0; (uint32_t)i < size; i += WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;
        if ((size - (uint32_t)i) < (uint32_t)len)
            len = (int)(size - (uint32_t)i);
        ret = wc_Sha256Update(&sha, base + IMAGE_HEADER_SIZE + i, len);
        if (ret != 0)
            return ret;
    }
    ret = wc_Sha256Final(&sha, digest);
    if (ret != 0)
        return ret;
    wc_Sha256Free(&sha);

    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
            SHA256_DIGEST_SIZE);
    ext_flash_lock();
    return 0;
}

/* Checked fixtures: run before/after EACH test, including after an assertion
 * failure (even in CK_NOFORK mode). This guarantees the fixed-address mmaps are
 * torn down between tests so a failing case cannot leak a mapping and steal the
 * next case's mmap hint. */
static void fixture_setup(void)
{
    reset_mock_stats();
    prepare_flash();
    prepare_ram();
}

static void fixture_teardown(void)
{
    cleanup_ram();
    cleanup_flash();
}

/* Run the full wolfBoot_start() flow for a given uImage ih_load and check that
 * do_boot() was reached with expect_addr and the kernel payload is present
 * there. (mmap setup/teardown is handled by the checked fixture.) */
static void run_and_check(uint32_t ih_load, uintptr_t expect_addr)
{
    ck_assert_int_eq(add_uimage_payload(1, ih_load), 0);

    wolfBoot_start();

    ck_assert_int_eq(g_boot_called, 1);
    ck_assert_uint_eq((uintptr_t)g_boot_addr, expect_addr);
    ck_assert_int_eq(memcmp((void *)expect_addr, expected_kernel, KERNEL_LEN),
            0);
}

/* Case 1: ih_load coincides with the staged kernel address -> no relocation
 * needed; the payload is already there. */
START_TEST (test_uboot_ihload_coincident)
{
    run_and_check((uint32_t)(WOLFBOOT_LOAD_ADDRESS + UBOOT_IMG_HDR_SZ),
            (uintptr_t)(WOLFBOOT_LOAD_ADDRESS + UBOOT_IMG_HDR_SZ));
}
END_TEST

/* Case 2: ih_load is a distinct, higher address -> the fix must memmove the
 * payload there. Without the fix do_boot() would jump to unwritten memory. */
START_TEST (test_uboot_ihload_higher)
{
    run_and_check((uint32_t)IHLOAD_HI_BASE, (uintptr_t)IHLOAD_HI_BASE);
}
END_TEST

/* Case 3: ih_load is a distinct, lower address that overlaps the source range
 * -> exercises memmove backward-overlap safety. */
START_TEST (test_uboot_ihload_lower_overlap)
{
    run_and_check((uint32_t)WOLFBOOT_LOAD_ADDRESS,
            (uintptr_t)WOLFBOOT_LOAD_ADDRESS);
}
END_TEST

/* Case 4: ih_load == 0 (Linux/PPC convention) -> load_address is only advanced
 * past the 64-byte header; behavior is unchanged by the fix. */
START_TEST (test_uboot_ihload_zero)
{
    run_and_check(0u, (uintptr_t)(WOLFBOOT_LOAD_ADDRESS + UBOOT_IMG_HDR_SZ));
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-uboot-ramboot");
    TCase *coincident = tcase_create("uImage ih_load coincident");
    TCase *higher = tcase_create("uImage ih_load higher");
    TCase *lower = tcase_create("uImage ih_load lower overlap");
    TCase *zero = tcase_create("uImage ih_load zero");

    tcase_add_checked_fixture(coincident, fixture_setup, fixture_teardown);
    tcase_add_checked_fixture(higher, fixture_setup, fixture_teardown);
    tcase_add_checked_fixture(lower, fixture_setup, fixture_teardown);
    tcase_add_checked_fixture(zero, fixture_setup, fixture_teardown);

    tcase_add_test(coincident, test_uboot_ihload_coincident);
    tcase_add_test(higher, test_uboot_ihload_higher);
    tcase_add_test(lower, test_uboot_ihload_lower_overlap);
    tcase_add_test(zero, test_uboot_ihload_zero);

    suite_add_tcase(s, coincident);
    suite_add_tcase(s, higher);
    suite_add_tcase(s, lower);
    suite_add_tcase(s, zero);

    tcase_set_timeout(coincident, 5);
    tcase_set_timeout(higher, 5);
    tcase_set_timeout(lower, 5);
    tcase_set_timeout(zero, 5);

    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;
    argv0 = strdup(argv[0]);
    (void)argc;
    s = wolfboot_suite();
    sr = srunner_create(s);
#if (NO_FORK == 1)
    srunner_set_fork_status(sr, CK_NOFORK);
#endif
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
