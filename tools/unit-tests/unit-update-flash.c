/* unit-update-flash.c
 *
 * unit tests for update procedures in update_flash.c
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

#define TEST_SIZE_SMALL 5300
#define TEST_SIZE_BIG 9800

#include <stdio.h>
#include <stdlib.h>
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include "update_flash.c"
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
    /* Mock of do_boot */
    if (wolfBoot_panicked)
        return;
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    printf("Called do_boot with address %p\n", address);
}

static void reset_mock_stats(void)
{
    wolfBoot_staged_ok = 0;
    wolfBoot_panicked = 0;
}


static void prepare_boot_image(int version)
{
    int ret;
    uint8_t part;
    ret = mmap_file("/tmp/wolfboot-unit-ext-file.bin", MOCK_ADDRESS_UPDATE,
            WOLFBOOT_PARTITION_SIZE, NULL);
    fail_if(ret < 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin", MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE, NULL);
    fail_if(ret < 0);
    ret = mmap_file("/tmp/wolfboot-unit-swap.bin", MOCK_ADDRESS_SWAP,
            WOLFBOOT_SECTOR_SIZE, NULL);
    fail_if(ret < 0);
    hal_flash_unlock();
    hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    hal_flash_lock();

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


    hal_flash_unlock();
    hal_flash_write(base, "WOLF", 4);
    printf("Written magic: \"WOLF\"\n");

    hal_flash_write(base + 4, &size, 4);
    printf("Written size: %u\n", size);

    /* Headers */
    word = 4 << 16 | HDR_VERSION;
    hal_flash_write(base + 8, &word, 4);
    hal_flash_write(base + 12, &version, 4);
    printf("Written version: %u\n", version);

    word = 2 << 16 | HDR_IMG_TYPE;
    hal_flash_write(base + 16, &word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    hal_flash_write(base + 20, &word16, 2);
    printf("Written img_type: %04X\n", word16);

    /* Add 28B header to sha calculation */
    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    /* Payload */
    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < size; i+=4) {
        uint32_t word = (random() << 16) | random();
        hal_flash_write(base + i, &word, 4);
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
    hal_flash_write(base + DIGEST_TLV_OFF_IN_HDR, &word, 4);
    hal_flash_write(base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
            SHA256_DIGEST_SIZE);
    printf("SHA digest written\n");
    for (i = 0; i < 32; i++) {
        printf("%02x ", digest[i]);
    }
    printf("\n");
    hal_flash_lock();

}

START_TEST (test_empty_panic)
{
    reset_mock_stats();
    prepare_boot_image(1);
    wolfBoot_start();
    fail_if(wolfBoot_staged_ok);
    fail_unless(wolfBoot_panicked);

}
END_TEST


START_TEST (test_sunnyday_noupdate)
{

    reset_mock_stats();
    prepare_boot_image(1);
    add_payload(PART_BOOT, 1, TEST_SIZE_SMALL);
    wolfBoot_start();
    fail_if(wolfBoot_panicked);
    fail_unless(wolfBoot_staged_ok);

}
END_TEST


Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfboot");

    /* Test cases */
    TCase *empty_panic = tcase_create("Empty partition panic test");
    TCase *sunnyday_noupdate =
        tcase_create("Sunny day test with no update available");

    tcase_add_test(empty_panic, test_empty_panic);
    tcase_add_test(sunnyday_noupdate, test_sunnyday_noupdate);

    suite_add_tcase(s, empty_panic);
    suite_add_tcase(s, sunnyday_noupdate);

    return s;
}


int main(int argc, char *argv[])
{
    int fails;
    argv0 = strdup(argv[0]);
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
