/* unit-update-ram-noramboot.c
 *
 * unit tests for update_ram.c wolfBoot_start on the non-RAMBOOT path
 * (WOLFBOOT_NO_RAMBOOT), where wolfBoot_open_image() is used instead of
 * wolfBoot_ramboot(). This is the configuration in which the anti-rollback
 * guard in wolfBoot_start is the decisive check.
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

const char *argv0;

Suite *wolfboot_suite(void);

int wolfBoot_staged_ok = 0;
const uint32_t *wolfBoot_stage_address = (uint32_t *) 0xFFFFFFFF;

void do_boot(const uint32_t *address)
{
    /* Mock of do_boot: count successful boots */
    if (wolfBoot_panicked)
        return;
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    printf("Called do_boot with address %p\n", address);
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
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

    /* Calculate final digest */
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

/* Sanity: a single valid image boots on the non-RAMBOOT path. */
START_TEST (test_noramboot_sunnyday) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    wolfBoot_start();
    ck_assert(wolfBoot_staged_ok);
    ck_assert_int_eq(wolfBoot_panicked, 0);
    cleanup_flash();
}
END_TEST

/* Regression test for F-4410: firmware versions with the high bit set
 * (>= 0x80000000) must still feed the anti-rollback guard in wolfBoot_start.
 *
 * BOOT carries the higher version but is marked oversize so wolfBoot_open_image()
 * rejects it and the boot path falls back to the lower-versioned (but valid)
 * UPDATE partition. That downgrade must be denied. Before the fix the versions
 * were cast through a signed int and clamped to 0, collapsing max_v to 0 and
 * silently bypassing the "(max_v > 0U)" guard, so the lower UPDATE image was
 * staged for boot. */
START_TEST (test_noramboot_highversion_rollback_denied) {
    uint32_t oversize = WOLFBOOT_PARTITION_SIZE;

    reset_mock_stats();
    prepare_flash();
    /* BOOT: higher version, but oversize so its open is rejected. The version
     * TLV stays readable. */
    add_payload(PART_BOOT, 0x80000005U, TEST_SIZE_SMALL);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4, (void *)&oversize, 4);
    ext_flash_lock();
    /* UPDATE: lower version, valid */
    add_payload(PART_UPDATE, 0x80000003U, TEST_SIZE_SMALL);

    ck_assert_uint_eq(wolfBoot_current_firmware_version(), 0x80000005U);
    ck_assert_uint_eq(wolfBoot_update_firmware_version(), 0x80000003U);

    wolfBoot_start();

    /* Rollback to the lower UPDATE version must be denied: wolfBoot panics and
     * stages nothing. */
    ck_assert(!wolfBoot_staged_ok);
    ck_assert_int_eq(wolfBoot_panicked, 1);
    cleanup_flash();
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-noramboot");
    TCase *sunnyday = tcase_create("Non-RAMBOOT sunny day");
    TCase *rollback_denied =
        tcase_create("Non-RAMBOOT high-version rollback denied");

    tcase_add_test(sunnyday, test_noramboot_sunnyday);
    tcase_add_test(rollback_denied, test_noramboot_highversion_rollback_denied);
    suite_add_tcase(s, sunnyday);
    suite_add_tcase(s, rollback_denied);
    tcase_set_timeout(sunnyday, 5);
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
