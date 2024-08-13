/* unit-update-ram.c
 *
 * unit tests for update procedures in update_ram.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define UNIT_TEST
#define WC_NO_HARDEN
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#include "target.h"
static __thread unsigned char wolfboot_ram[2 * WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE];

#define WOLFBOOT_LOAD_ADDRESS ((wolfboot_ram + IMAGE_HEADER_SIZE))

#define TEST_SIZE_SMALL 5300
#define TEST_SIZE_LARGE 9800

#define NO_FORK 1 /* Set to 1 to disable fork mode (e.g. for gdb debugging) */

#include <stdio.h>
#include <stdlib.h>
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
uint32_t *wolfBoot_stage_address = (uint32_t *) 0xFFFFFFFF;

void do_boot(const uint32_t *address)
{
    struct wolfBoot_image boot_image;
    /* Mock of do_boot */
    if (wolfBoot_panicked)
        return;
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    ck_assert_uint_eq(address, WOLFBOOT_LOAD_ADDRESS);
    memset(&boot_image, 0, sizeof(boot_image));
    printf("Called do_boot with address %p\n", address);
    ck_assert_uint_eq(0,wolfBoot_open_image_address(&boot_image, wolfboot_ram));
    boot_image.hdr = wolfboot_ram;
    boot_image.fw_base = WOLFBOOT_LOAD_ADDRESS;
    boot_image.part = 0;
    boot_image.not_ext = 1;
    ck_assert_uint_eq(0,wolfBoot_verify_integrity(&boot_image));

}

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
}

uint32_t get_version_ramloaded(void)
{
    return wolfBoot_get_blob_version(wolfboot_ram);
}


static void prepare_flash(void)
{
    int ret;
    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin", MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    fail_if(ret < 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin", MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    fail_if(ret < 0);
    ext_flash_unlock();
    ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_lock();
}

static void cleanup_flash(void)
{
    munmap(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    munmap(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
}


#define DIGEST_TLV_OFF_IN_HDR 28
static int add_payload(uint8_t part, uint32_t version, uint32_t size)
{
    uint32_t word;
    uint16_t word16;
    int i;
    uint8_t *base = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;


    if (part == PART_UPDATE)
        base = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    srandom(part); /* Ensure reproducible "random" image */


    ext_flash_unlock();
    ext_flash_write(base, "WOLF", 4);
    printf("Written magic: \"WOLF\"\n");

    ext_flash_write(base + 4, &size, 4);
    printf("Written size: %u\n", size);

    /* Headers */
    word = 4 << 16 | HDR_VERSION;
    ext_flash_write(base + 8, &word, 4);
    ext_flash_write(base + 12, &version, 4);
    printf("Written version: %u\n", version);

    word = 2 << 16 | HDR_IMG_TYPE;
    ext_flash_write(base + 16, &word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    ext_flash_write(base + 20, &word16, 2);
    printf("Written img_type: %04X\n", word16);

    /* Add 28B header to sha calculation */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    /* Payload */
    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < size; i+=4) {
        uint32_t word = (random() << 16) | random();
        ext_flash_write(base + i, &word, 4);
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
    ext_flash_write(base + DIGEST_TLV_OFF_IN_HDR, &word, 4);
    ext_flash_write(base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
            SHA256_DIGEST_SIZE);
    printf("SHA digest written\n");
    for (i = 0; i < 32; i++) {
        printf("%02x ", digest[i]);
    }
    printf("\n");
    ext_flash_lock();

}

START_TEST (test_empty_panic)
{
    reset_mock_stats();
    prepare_flash();
    wolfBoot_start();
    fail_if(wolfBoot_staged_ok);
    cleanup_flash();

}
END_TEST


START_TEST (test_sunnyday_noupdate)
{
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    printf("*** MEM: %p\n", WOLFBOOT_LOAD_ADDRESS);

    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();

}
END_TEST

START_TEST (test_forward_update_samesize_notrigger) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_samesize) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_tolarger) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_tosmaller) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_LARGE);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 2);
    cleanup_flash();
}
END_TEST

START_TEST (test_forward_update_sameversion_denied) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    fail_if(*(uint32_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4) != TEST_SIZE_SMALL);
    cleanup_flash();
}
END_TEST

START_TEST (test_update_oldversion_denied) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_LARGE);
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 2);
    fail_if(*(uint32_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + 4) != TEST_SIZE_SMALL);
    cleanup_flash();
}

START_TEST (test_invalid_update_type) {
    reset_mock_stats();
    prepare_flash();
    uint16_t word16 = 0xBAAD;
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 20, &word16, 2);
    ext_flash_lock();
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();
}

START_TEST (test_update_toolarge) {
    uint32_t very_large = WOLFBOOT_PARTITION_SIZE;
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_LARGE);
    /* Change the size in the header to be larger than the actual size */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + 4, &very_large, 4);
    ext_flash_lock();

    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();
}

START_TEST (test_invalid_sha) {
    uint8_t bad_digest[SHA256_DIGEST_SIZE];
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL);

    memset(bad_digest, 0xBA, SHA256_DIGEST_SIZE);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4, bad_digest, SHA256_DIGEST_SIZE);
    ext_flash_lock();
    wolfBoot_update_trigger();
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();
}

START_TEST (test_emergency_rollback) {
    uint8_t testing_flags[5] = { IMG_STATE_TESTING, 'B', 'O', 'O', 'T' };
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_SMALL);
    /* Set the testing flag in the last five bytes of the BOOT partition */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 5,
            testing_flags, 5);
    ext_flash_lock();

    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 1);
    cleanup_flash();
}

START_TEST (test_emergency_rollback_failure_due_to_bad_update) {
    uint8_t testing_flags[5] = { IMG_STATE_TESTING, 'B', 'O', 'O', 'T' };
    uint8_t wrong_update_magic[4] = { 'G', 'O', 'L', 'F' };
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_BOOT, 2, TEST_SIZE_SMALL);
    add_payload(PART_UPDATE, 1, TEST_SIZE_SMALL);
    /* Set the testing flag in the last five bytes of the BOOT partition */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 5,
            testing_flags, 5);
    ext_flash_lock();

    /* Corrupt the update */
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS, wrong_update_magic, 4);
    ext_flash_lock();

    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 2);
    cleanup_flash();
}

START_TEST (test_empty_boot_partition_update) {
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_UPDATE, 5, TEST_SIZE_SMALL);
    wolfBoot_start();
    fail_unless(wolfBoot_staged_ok);
    fail_if(get_version_ramloaded() != 5);
    cleanup_flash();
}

START_TEST (test_empty_boot_but_update_sha_corrupted_denied) {
    uint8_t bad_digest[SHA256_DIGEST_SIZE];
    reset_mock_stats();
    prepare_flash();
    add_payload(PART_UPDATE, 5, TEST_SIZE_SMALL);
    memset(bad_digest, 0xBA, SHA256_DIGEST_SIZE);
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4, bad_digest, SHA256_DIGEST_SIZE);
    ext_flash_lock();
    wolfBoot_start();
    /* We expect to panic */
    fail_if(wolfBoot_staged_ok);
    cleanup_flash();
}


Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfboot");

    /* Test cases */
    TCase *empty_panic = tcase_create("Empty partition panic test");
    TCase *sunnyday_noupdate =
        tcase_create("Sunny day test with no update available");
    TCase *forward_update_samesize =
        tcase_create("Forward update with same size");
    TCase *forward_update_tolarger =
        tcase_create("Forward update to larger size");
    TCase *forward_update_tosmaller = tcase_create("Forward update to smaller size");
    TCase *forward_update_sameversion_denied =
        tcase_create("Forward update to same version denied");
    TCase *update_oldversion_denied =
        tcase_create("Update to older version denied");
    TCase *invalid_update_type =
        tcase_create("Invalid update type");
    TCase *update_toolarge = tcase_create("Update too large");
    TCase *invalid_sha = tcase_create("Invalid SHA digest");
    TCase *emergency_rollback = tcase_create("Emergency rollback");
    TCase *emergency_rollback_failure_due_to_bad_update = tcase_create("Emergency rollback failure due to bad update");
    TCase *empty_boot_partition_update = tcase_create("Empty boot partition update");
    TCase *empty_boot_but_update_sha_corrupted_denied = tcase_create("Empty boot partition but update SHA corrupted");



    tcase_add_test(empty_panic, test_empty_panic);
    tcase_add_test(sunnyday_noupdate, test_sunnyday_noupdate);
    tcase_add_test(forward_update_samesize, test_forward_update_samesize);
    tcase_add_test(forward_update_tolarger, test_forward_update_tolarger);
    tcase_add_test(forward_update_tosmaller, test_forward_update_tosmaller);
    tcase_add_test(forward_update_sameversion_denied, test_forward_update_sameversion_denied);
    tcase_add_test(update_oldversion_denied, test_update_oldversion_denied);
    tcase_add_test(invalid_update_type, test_invalid_update_type);
    tcase_add_test(update_toolarge, test_update_toolarge);
    tcase_add_test(invalid_sha, test_invalid_sha);
    tcase_add_test(emergency_rollback, test_emergency_rollback);
    tcase_add_test(emergency_rollback_failure_due_to_bad_update, test_emergency_rollback_failure_due_to_bad_update);
    tcase_add_test(empty_boot_partition_update, test_empty_boot_partition_update);
    tcase_add_test(empty_boot_but_update_sha_corrupted_denied, test_empty_boot_but_update_sha_corrupted_denied);



    suite_add_tcase(s, empty_panic);
    suite_add_tcase(s, sunnyday_noupdate);
    suite_add_tcase(s, forward_update_samesize);
    suite_add_tcase(s, forward_update_tolarger);
    suite_add_tcase(s, forward_update_tosmaller);
    suite_add_tcase(s, forward_update_sameversion_denied);
    suite_add_tcase(s, update_oldversion_denied);
    suite_add_tcase(s, invalid_update_type);
    suite_add_tcase(s, update_toolarge);
    suite_add_tcase(s, invalid_sha);
    suite_add_tcase(s, emergency_rollback);
    suite_add_tcase(s, emergency_rollback_failure_due_to_bad_update);
    suite_add_tcase(s, empty_boot_partition_update);
    suite_add_tcase(s, empty_boot_but_update_sha_corrupted_denied);


    /* Set timeout for tests */
    tcase_set_timeout(empty_panic, 5);
    tcase_set_timeout(sunnyday_noupdate, 5);
    tcase_set_timeout(forward_update_samesize, 5);
    tcase_set_timeout(forward_update_tolarger, 5);
    tcase_set_timeout(forward_update_tosmaller, 5);
    tcase_set_timeout(forward_update_sameversion_denied, 5);
    tcase_set_timeout(update_oldversion_denied, 5);
    tcase_set_timeout(invalid_update_type, 5);
    tcase_set_timeout(update_toolarge, 5);
    tcase_set_timeout(invalid_sha, 5);
    tcase_set_timeout(emergency_rollback, 5);
    tcase_set_timeout(emergency_rollback_failure_due_to_bad_update, 5);
    tcase_set_timeout(empty_boot_partition_update, 5);
    tcase_set_timeout(empty_boot_but_update_sha_corrupted_denied, 5);


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
