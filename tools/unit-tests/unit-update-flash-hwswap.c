/* unit-update-flash-hwswap.c
 *
 * unit tests for the HW-assisted swap updater (update_flash_hwswap.c)
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

#define NO_FORK 1 /* Set to 1 to disable fork mode (e.g. for gdb debugging) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

/* Mocks for the symbols referenced by update_flash_hwswap.c that are not
 * provided by libwolfboot.c / unit-mock-flash.c
 */
static int do_boot_called = 0;
static int dualbank_swap_called = 0;

void hal_flash_dualbank_swap(void)
{
    dualbank_swap_called++;
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void do_boot(const uint32_t *address)
{
    (void)address;
    do_boot_called++;
}

/* update_flash_hwswap.c uses a private boot_panic() that spins forever
 * (while(1);). Neutralize only that spin so the deny path can return and be
 * observed by the test. The headers are pulled in first (with guards), so the
 * macro only rewrites the single while() inside update_flash_hwswap.c itself.
 * wolfBoot_start uses for(;;), which is unaffected.
 */
#include "loader.h"
#include "image.h"
#include "hal.h"
#include "hooks.h"
#include "spi_flash.h"
#include "printf.h"
#define while(cond) if (cond)
#include "update_flash_hwswap.c"
#undef while

const char *argv0;

Suite *wolfboot_suite(void);

static void reset_mock_stats(void)
{
    do_boot_called = 0;
    dualbank_swap_called = 0;
}

static void prepare_flash(void)
{
    int ret;
    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin",
            (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert(ret >= 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin",
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

#define TEST_SIZE_SMALL 5300
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
    ext_flash_write((uintptr_t)base + 4, (void *)&size, 4);

    /* Headers */
    word = 4 << 16 | HDR_VERSION;
    ext_flash_write((uintptr_t)base + 8, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + 12, (void *)&version, 4);

    word = 2 << 16 | HDR_IMG_TYPE;
    ext_flash_write((uintptr_t)base + 16, (void *)&word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    ext_flash_write((uintptr_t)base + 20, (void *)&word16, 2);

    /* Add 28B header to sha calculation */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    /* Payload */
    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < (int)size; i += 4) {
        uint32_t w = (random() << 16) | random();
        ext_flash_write((uintptr_t)base + i, (void *)&w, 4);
    }
    for (i = IMAGE_HEADER_SIZE; i < (int)size; i += WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;
        if (((int)size - i) < len)
            len = size - i;
        ret = wc_Sha256Update(&sha, base + i, len);
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

/* Regression test for the uint32_t->int cast in wolfBoot_start (hwswap).
 *
 * BOOT carries a higher version than UPDATE, but its image is unbootable
 * (oversize header), so wolfBoot_start falls back to the UPDATE partition,
 * which holds a LOWER version. Anti-rollback must deny booting the lower
 * version (boot_panic, no do_boot).
 *
 * Both versions are above INT_MAX: with the buggy (int)cast they were clamped
 * to 0, max_v became 0, the "(max_v > 0U)" guard was skipped, and the
 * rolled-back UPDATE image was staged for boot.
 */
START_TEST (test_hwswap_highversion_rollback_denied) {
    uint32_t oversize = WOLFBOOT_PARTITION_SIZE;

    reset_mock_stats();
    prepare_flash();
    /* BOOT: higher version, but mark it oversize so wolfBoot_open_image()
     * rejects it and the boot path falls back to UPDATE. The version TLV
     * stays readable. */
    add_payload(PART_BOOT, 0x80000002U, TEST_SIZE_SMALL);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4, (void *)&oversize, 4);
    ext_flash_lock();
    /* UPDATE: lower version, valid */
    add_payload(PART_UPDATE, 0x80000001U, TEST_SIZE_SMALL);

    ck_assert_uint_eq(wolfBoot_current_firmware_version(), 0x80000002U);
    ck_assert_uint_eq(wolfBoot_update_firmware_version(), 0x80000001U);

    wolfBoot_start();

    /* Rollback to the lower UPDATE version must be denied */
    ck_assert_int_eq(do_boot_called, 0);
    cleanup_flash();
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-hwswap");
    TCase *rollback_denied =
        tcase_create("HW-swap high-version rollback denied");

    tcase_add_test(rollback_denied, test_hwswap_highversion_rollback_denied);
    suite_add_tcase(s, rollback_denied);
    tcase_set_timeout(rollback_denied, 5);
    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    argv0 = strdup(argv[0]);
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
#if (NO_FORK == 1)
    srunner_set_fork_status(sr, CK_NOFORK);
#endif
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
