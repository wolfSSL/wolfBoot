/* unit-compressed-tlv.c
 *
 * Unit tests for the compressed-image manifest reader in image.c, which the
 * execute-in-place installer uses to read an image's compression tags.
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

#define NO_FORK 1

#include "target.h"

static __thread unsigned char wolfboot_ram[2 * WOLFBOOT_PARTITION_SIZE +
                                           IMAGE_HEADER_SIZE];
#define WOLFBOOT_LOAD_ADDRESS (((uintptr_t)wolfboot_ram + IMAGE_HEADER_SIZE))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "libwolfboot.c"
#include <check.h>
#include "unit-mock-flash.c"

const char *argv0;

Suite *wolfboot_suite(void);

int wolfBoot_staged_ok = 0;

void do_boot(const uint32_t *address)
{
    (void)address;
    wolfBoot_staged_ok++;
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

/* Manifest construction helpers */

static uint8_t test_hdr[IMAGE_HEADER_SIZE];
static struct wolfBoot_image test_img;

static uint32_t hdr_idx;

static void hdr_reset(void)
{
    memset(test_hdr, 0xFF, sizeof(test_hdr));
    memcpy(test_hdr, "WOLF", 4);
    /* image size field, unused by the reader under test */
    memset(test_hdr + 4, 0, 4);
    hdr_idx = 8;

    memset(&test_img, 0, sizeof(test_img));
    test_img.hdr = test_hdr;
    test_img.not_ext = 1;
    test_img.hdr_ok = 1;
    test_img.part = PART_BOOT;
}

static void hdr_add(uint16_t tag, uint16_t len, const uint8_t *val)
{
    test_hdr[hdr_idx++] = (uint8_t)(tag & 0xFF);
    test_hdr[hdr_idx++] = (uint8_t)(tag >> 8);
    test_hdr[hdr_idx++] = (uint8_t)(len & 0xFF);
    test_hdr[hdr_idx++] = (uint8_t)(len >> 8);
    if ((val != NULL) && (len > 0)) {
        memcpy(test_hdr + hdr_idx, val, len);
    }
    hdr_idx += len;
}

static void hdr_add_u16(uint16_t tag, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)(v >> 8);
    hdr_add(tag, 2, b);
}

static void hdr_add_u32(uint16_t tag, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    hdr_add(tag, 4, b);
}

/* wolfBoot_image_compression() */

/* No compression TLV: an uncompressed image, which must be reported as
 * "none" (0) rather than as an error, so the plain path still runs. */
START_TEST(test_compression_absent)
{
    uint32_t sz = 0xA5A5A5A5;

    hdr_reset();
    hdr_add_u32(HDR_VERSION, 1);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), 0);
}
END_TEST

/* A well-formed gzip manifest. Also pins the little-endian decode of the
 * uncompressed length, which is what bounds the inflate. */
START_TEST(test_compression_gzip_ok)
{
    uint32_t sz = 0;
    uint32_t bits = 0;

    hdr_reset();
    hdr_add_u16(HDR_IMG_COMPRESSION, HDR_COMP_GZIP);
    hdr_add_u32(HDR_IMG_UNCOMPRESSED_SIZE, 0x00029A28); /* 170536 */
    hdr_add_u16(HDR_IMG_COMP_WINDOW, 14);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, &bits), 1);
    ck_assert_uint_eq(sz, 0x00029A28);
    ck_assert_uint_eq(bits, 14);
}
END_TEST

/* Unknown algorithm must be -1, never a fall-through to booting raw:
 * that would be a jump into DEFLATE data. */
START_TEST(test_compression_unknown_algo)
{
    uint32_t sz = 0;

    hdr_reset();
    hdr_add_u16(HDR_IMG_COMPRESSION, 0x00FE);
    hdr_add_u32(HDR_IMG_UNCOMPRESSED_SIZE, 1024);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), -1);
}
END_TEST

/* Wrong length on the algorithm tag. */
START_TEST(test_compression_bad_algo_len)
{
    uint32_t sz = 0;
    uint8_t one = 1;

    hdr_reset();
    hdr_add(HDR_IMG_COMPRESSION, 1, &one);
    hdr_add_u32(HDR_IMG_UNCOMPRESSED_SIZE, 1024);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), -1);
}
END_TEST

/* Compression declared but no uncompressed size: the loader would have
 * nothing to bound the inflate with. */
START_TEST(test_compression_missing_size)
{
    uint32_t sz = 0;

    hdr_reset();
    hdr_add_u16(HDR_IMG_COMPRESSION, HDR_COMP_GZIP);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), -1);
}
END_TEST

/* Size tag present but the wrong width. */
START_TEST(test_compression_bad_size_len)
{
    uint32_t sz = 0;
    uint8_t two[2] = { 0x00, 0x04 };

    hdr_reset();
    hdr_add_u16(HDR_IMG_COMPRESSION, HDR_COMP_GZIP);
    hdr_add(HDR_IMG_UNCOMPRESSED_SIZE, 2, two);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), -1);
}
END_TEST

/* Wrong length on the window tag. Rejected whether or not the caller wants
 * the value, so a malformed tag can never pass as a usable manifest. */
START_TEST(test_compression_bad_window_len)
{
    uint32_t sz = 0;
    uint32_t bits = 0;
    uint8_t one = 14;

    hdr_reset();
    hdr_add_u16(HDR_IMG_COMPRESSION, HDR_COMP_GZIP);
    hdr_add_u32(HDR_IMG_UNCOMPRESSED_SIZE, 1024);
    hdr_add(HDR_IMG_COMP_WINDOW, 1, &one);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, &bits), -1);
    ck_assert_int_eq(wolfBoot_image_compression(&test_img, &sz, NULL), -1);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-compressed-tlv");
    TCase *tc = tcase_create("compressed manifest");

    tcase_add_test(tc, test_compression_absent);
    tcase_add_test(tc, test_compression_gzip_ok);
    tcase_add_test(tc, test_compression_unknown_algo);
    tcase_add_test(tc, test_compression_bad_algo_len);
    tcase_add_test(tc, test_compression_missing_size);
    tcase_add_test(tc, test_compression_bad_size_len);
    tcase_add_test(tc, test_compression_bad_window_len);
    suite_add_tcase(s, tc);
    return s;
}

int main(int argc, char *argv[])
{
    int number_failed;
    SRunner *sr;

    argv0 = argv[0];
    (void)argc;

    sr = srunner_create(wolfboot_suite());
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
