/* unit-parser-large-header.c
 *
 * Unit test for wolfBoot_find_header() with a manifest header at or above
 * the 64 KiB boundary: the per-field budget check must compare in a
 * 32-bit domain, since a uint16_t cast of (IMAGE_HEADER_SIZE -
 * IMAGE_HEADER_OFFSET) wraps for headers >= 64 KiB and rejects every
 * field, breaking the pack/parse roundtrip for large signed TLVs.
 *
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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/* Must also define DEBUG_WOLFSSL in user_settings.h */
#define WOLFBOOT_HASH_SHA256
/* Exactly at the wrap boundary: the payload budget is 0x10000, which a
 * uint16_t cast of the pre-fix check turned into 0 (rejecting everything). */
#define IMAGE_HEADER_SIZE 0x10008
#define WC_RSA_BLINDING
#define ECC_TIMING_RESISTANT
#include <stdio.h>
/* Consume the unit target header up front so its include guard keeps
 * libwolfboot.c's later include from running. image.h sanity-checks
 * WOLFBOOT_SECTOR_SIZE > IMAGE_HEADER_SIZE, which the shared unit sector
 * (0x400) cannot satisfy for a 0x10008 header; override it here. The
 * parser under test does no flash I/O, so the sector size is otherwise
 * unused. */
#include "target.h"
#undef WOLFBOOT_SECTOR_SIZE
#define WOLFBOOT_SECTOR_SIZE 0x20000
#include "libwolfboot.c"
#include <check.h>
static int locked = 0;

/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}
void hal_flash_unlock(void)
{
    ck_assert_msg(locked, "Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    ck_assert_msg(!locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}
/* End Mocks */

/* A 300-byte TLV payload: comfortably inside the 0x10000 budget, but far
 * beyond the 244 bytes the wrapped uint16 budget would have allowed for a
 * slightly smaller oversized header. */
#define BIG_TLV_LEN 300

static uint8_t big_hdr[IMAGE_HEADER_SIZE];

static void build_big_header(void)
{
    uint32_t magic = WOLFBOOT_MAGIC;
    uint32_t fw_size = 0x100;
    int i;

    memset(big_hdr, 0xFF, sizeof(big_hdr));
    memcpy(big_hdr + 0, &magic, sizeof(magic));
    memcpy(big_hdr + 4, &fw_size, sizeof(fw_size));

    /* Single TLV at IMAGE_HEADER_OFFSET: HDR_VERSION with a 300-byte
     * payload, followed by the end-of-options zero word. */
    big_hdr[8]  = (uint8_t)(HDR_VERSION & 0xFF);
    big_hdr[9]  = (uint8_t)(HDR_VERSION >> 8);
    big_hdr[10] = (uint8_t)(BIG_TLV_LEN & 0xFF);
    big_hdr[11] = (uint8_t)(BIG_TLV_LEN >> 8);
    for (i = 0; i < BIG_TLV_LEN; i++)
        big_hdr[12 + i] = (uint8_t)(0xA0 + i);
    big_hdr[12 + BIG_TLV_LEN] = 0x00;
    big_hdr[13 + BIG_TLV_LEN] = 0x00;
}

START_TEST (test_parser_large_header_finds_big_tlv)
{
    uint8_t *p;
    int i;

    build_big_header();

    /* The version field must be located despite the header being at the
     * uint16 budget wrap boundary. */
    ck_assert_msg(wolfBoot_find_header(big_hdr + 8, HDR_VERSION, &p) ==
                  BIG_TLV_LEN,
        "Parser error: cannot locate version field in a >= 64 KiB header");

    for (i = 0; i < BIG_TLV_LEN; i++)
        ck_assert_msg(p[i] == (uint8_t)(0xA0 + i),
            "Parser error: version payload does not match");

    /* A non-existing field must still report not-found. */
    ck_assert_msg(wolfBoot_find_header(big_hdr + 8, HDR_SHA3_384, &p) == 0,
        "Parser error: found a non-existing field");
}
END_TEST

START_TEST (test_parser_large_header_blobs)
{
    uint32_t ver;

    build_big_header();

    /* The blob accessor must read through the large header as well. */
    ver = wolfBoot_get_blob_version(big_hdr);
    ck_assert_uint_eq(ver, 0); /* 300-byte version is not a uint32: no field */

    /* Rebuild with a 4-byte version payload: the accessor must return it. */
    big_hdr[10] = 4;
    big_hdr[11] = 0;
    big_hdr[12] = 0x0d;
    big_hdr[13] = 0x0c;
    big_hdr[14] = 0x0b;
    big_hdr[15] = 0x0a;
    ver = wolfBoot_get_blob_version(big_hdr);
    ck_assert_uint_eq(ver, 0x0a0b0c0d);
}
END_TEST

Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot");

    /* Test cases */
    TCase *parser_big = tcase_create("Parser large header");

    /* Test function <-> Test case */
    tcase_add_test(parser_big, test_parser_large_header_finds_big_tlv);
    tcase_add_test(parser_big, test_parser_large_header_blobs);

    /* Set parameters + add to suite */
    tcase_set_timeout(parser_big, 20);
    suite_add_tcase(s, parser_big);

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
