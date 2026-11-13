/* unit-boot-x86-fsp-measure.c
 *
 * Unit tests for the stage 1 measurement of stage 2 (wolfBoot_image_measure()
 * in src/boot_x86_fsp.c): the SHA-384 image digest is re-hashed with
 * SHA-256 and extended into PCR 0, the value compute_pcr.py predicts.
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

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "../../src/boot_x86_fsp.c"

static uint8_t g_image_digest[WOLFBOOT_SHA_DIGEST_SIZE];
static int g_verify_ret;
static int g_extend_calls;
static uint8_t g_extend_pcr;
static uint8_t g_extended[SHA256_DIGEST_SIZE];

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus; (void)dev; (void)fun; (void)off;
    return 0xff;
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus; (void)dev; (void)fun; (void)off;
    return 0xffff;
}

int wolfBoot_open_image_address(struct wolfBoot_image *img, uint8_t *image)
{
    (void)image;
    img->sha_hash = NULL;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image *img)
{
    if (g_verify_ret == 0)
        img->sha_hash = g_image_digest;
    return g_verify_ret;
}

int wolfBoot_tpm2_extend(uint8_t pcrIndex, uint8_t *hash, int line)
{
    (void)line;
    g_extend_calls++;
    g_extend_pcr = pcrIndex;
    memcpy(g_extended, hash, sizeof(g_extended));
    return 0;
}

void wolfBoot_print_hexstr(const unsigned char *bin, unsigned long sz,
    unsigned long maxLine)
{
    (void)bin; (void)sz; (void)maxLine;
}

static void reset_mocks(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(g_image_digest); i++)
        g_image_digest[i] = (uint8_t)i;
    g_verify_ret = 0;
    g_extend_calls = 0;
    g_extend_pcr = 0xff;
    memset(g_extended, 0, sizeof(g_extended));
}

START_TEST(test_measure_extends_sha256_of_the_digest)
{
    /* sha256(bytes(range(48))) */
    static const uint8_t want[SHA256_DIGEST_SIZE] = {
        0x4d, 0xbd, 0xc2, 0xb2, 0xb6, 0x2c, 0xb0, 0x07,
        0x49, 0x78, 0x5b, 0xc8, 0x42, 0x02, 0x23, 0x6d,
        0xbc, 0x37, 0x77, 0xd7, 0x46, 0x60, 0x61, 0x1b,
        0x8e, 0x58, 0x81, 0x2f, 0x0c, 0xfd, 0xe6, 0xc3
    };
    uint8_t image[16];

    reset_mocks();
    ck_assert_int_eq(wolfBoot_image_measure(image), 0);
    ck_assert_int_eq(g_extend_calls, 1);
    ck_assert_uint_eq(g_extend_pcr, WOLFBOOT_MEASURED_PCR_A);
    ck_assert_int_eq(memcmp(g_extended, want, sizeof(want)), 0);
}
END_TEST

START_TEST(test_measure_skips_an_unverified_image)
{
    uint8_t image[16];

    reset_mocks();
    g_verify_ret = -1;
    ck_assert_int_ne(wolfBoot_image_measure(image), 0);
    ck_assert_int_eq(g_extend_calls, 0);
}
END_TEST

static Suite *measure_suite(void)
{
    Suite *s = suite_create("boot-x86-fsp-measure");
    TCase *tc = tcase_create("boot-x86-fsp-measure");

    tcase_add_test(tc, test_measure_extends_sha256_of_the_digest);
    tcase_add_test(tc, test_measure_skips_an_unverified_image);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = measure_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
