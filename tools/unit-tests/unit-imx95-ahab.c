/* unit-imx95-ahab.c
 *
 * Unit tests for the i.MX95 AHAB container parser in hal/imx95_ahab.c. The
 * parser decides what the stage 1 loads and where it jumps, from a structure
 * read off the boot device, so the bounds and overflow guards matter as much
 * as the happy path.
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

#include "../../hal/imx95_ahab.c"

/* A boot device made of host memory. Big enough for two containers and the
 * images the second one points at. */
#define DEV_BYTES  (512U * 1024U)
static uint8_t dev[DEV_BYTES];

static int dev_read(void *ctx, uint32_t off, uint32_t len, void *buf)
{
    (void)ctx;
    if ((off % IMX95_AHAB_BLOCK) != 0U || (len % IMX95_AHAB_BLOCK) != 0U)
        return -1;
    if ((off + len) > DEV_BYTES)
        return -1;
    memcpy(buf, &dev[off], len);
    return 0;
}

/* A read that fails every time, to prove a device error is not mistaken for
 * an absent container. */
static int dev_read_fail(void *ctx, uint32_t off, uint32_t len, void *buf)
{
    (void)ctx; (void)off; (void)len; (void)buf;
    return -1;
}

static void put16(uint32_t off, uint32_t v)
{
    dev[off] = (uint8_t)(v & 0xFF);
    dev[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put32(uint32_t off, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++)
        dev[off + i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static void put64(uint32_t off, uint64_t v)
{
    put32(off, (uint32_t)v);
    put32(off + 4, (uint32_t)(v >> 32));
}

/* Lay down a container header at base with count image entries. */
static void make_container(uint32_t base, uint32_t count, uint32_t sig_off)
{
    uint32_t length = AHAB_HDR_SIZE + (count * AHAB_IMG_ENTRY_SIZE);

    memset(&dev[base], 0, IMX95_AHAB_HDR_BYTES);
    dev[base + 0] = IMX95_AHAB_VERSION;
    put16(base + 1, length);
    dev[base + 3] = IMX95_AHAB_TAG;
    dev[base + 11] = (uint8_t)count;
    put16(base + 12, sig_off);
}

static void make_image(uint32_t base, uint32_t idx, uint32_t off, uint32_t size,
                       uint64_t dst, uint64_t entry, uint32_t flags)
{
    uint32_t e = base + AHAB_HDR_SIZE + (idx * AHAB_IMG_ENTRY_SIZE);

    put32(e + 0, off);
    put32(e + 4, size);
    put64(e + 8, dst);
    put64(e + 16, entry);
    put32(e + 24, flags);
}

static void setup_valid(void)
{
    memset(dev, 0, sizeof(dev));
    make_container(IMX95_AHAB_MMC_OFFSET, 2, 0);
    /* Two images, the furthest ending at 0x3000 from the header. */
    make_image(IMX95_AHAB_MMC_OFFSET, 0, 0x1000, 0x1000,
               0x8A200000ULL, 0x8A200000ULL, 0x03U | (0x02U << 4));
    make_image(IMX95_AHAB_MMC_OFFSET, 1, 0x2000, 0x1000,
               0x90200000ULL, 0x90200000ULL, 0x03U | (0x02U << 4));
    /* Recognisable payloads for the load test. */
    memset(&dev[IMX95_AHAB_MMC_OFFSET + 0x1000], 0xA5, 0x1000);
    memset(&dev[IMX95_AHAB_MMC_OFFSET + 0x2000], 0x5A, 0x1000);
}

START_TEST(test_parse_valid)
{
    struct imx95_ahab_container c;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_uint_eq(c.base, IMX95_AHAB_MMC_OFFSET);
    ck_assert_uint_eq(c.count, 2);
    ck_assert_uint_eq(c.size, 0x3000);
    ck_assert_uint_eq(c.img[0].dst, 0x8A200000ULL);
    ck_assert_uint_eq(c.img[1].offset, 0x2000);
    ck_assert_uint_eq(IMX95_AHAB_TYPE(c.img[0].flags), IMX95_AHAB_TYPE_EXEC);
    ck_assert_uint_eq(IMX95_AHAB_CORE(c.img[0].flags), IMX95_AHAB_CORE_A55);
}
END_TEST

/* The whole point of the walk: the next container is the end of this one
 * rounded up, because nothing records where it starts. */
START_TEST(test_next_rounds_up)
{
    struct imx95_ahab_container c;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_uint_eq(imx95_ahab_next(&c), IMX95_AHAB_MMC_OFFSET + 0x3000);

    /* An end that is not already aligned must round up, not truncate. */
    c.size = 0x3001;
    ck_assert_uint_eq(imx95_ahab_next(&c),
                      (IMX95_AHAB_MMC_OFFSET + 0x3400));
}
END_TEST

/* The signature block can be the furthest thing in the container. */
START_TEST(test_size_covers_signature_block)
{
    struct imx95_ahab_container c;

    setup_valid();
    make_container(IMX95_AHAB_MMC_OFFSET, 1, 0x120);
    make_image(IMX95_AHAB_MMC_OFFSET, 0, 0x200, 0x200, 0, 0, 0);
    put16(IMX95_AHAB_MMC_OFFSET + 0x120 + 1, 0x300); /* sig block length */
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_uint_eq(c.size, 0x120 + 0x300);
}
END_TEST

START_TEST(test_reject_bad_tag_and_version)
{
    struct imx95_ahab_container c;

    setup_valid();
    dev[IMX95_AHAB_MMC_OFFSET + 3] = 0x00;
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);

    setup_valid();
    dev[IMX95_AHAB_MMC_OFFSET + 0] = 0x01;
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
}
END_TEST

START_TEST(test_reject_bad_image_count)
{
    struct imx95_ahab_container c;

    setup_valid();
    dev[IMX95_AHAB_MMC_OFFSET + 11] = 0;
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);

    setup_valid();
    dev[IMX95_AHAB_MMC_OFFSET + 11] = IMX95_AHAB_MAX_IMAGES + 1;
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
}
END_TEST

/* An entry whose offset + size wraps must not make the container look
 * smaller than it is, which would place the next container inside this one. */
START_TEST(test_reject_offset_size_overflow)
{
    struct imx95_ahab_container c;

    setup_valid();
    make_image(IMX95_AHAB_MMC_OFFSET, 0, 0xFFFFFF00U, 0x200, 0, 0, 0);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
}
END_TEST

START_TEST(test_reject_signature_block_out_of_range)
{
    struct imx95_ahab_container c;

    setup_valid();
    put16(IMX95_AHAB_MMC_OFFSET + 12, IMX95_AHAB_HDR_BYTES);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
}
END_TEST

START_TEST(test_reject_unaligned_base_and_read_failure)
{
    struct imx95_ahab_container c;

    setup_valid();
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET + 1, dev_read,
                                      NULL), 0);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read_fail,
                                      NULL), 0);
    ck_assert_int_lt(imx95_ahab_parse(NULL, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, NULL,
                                      NULL), 0);
}
END_TEST

START_TEST(test_load_copies_image)
{
    struct imx95_ahab_container c;
    static uint8_t out[0x1000];

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    memset(out, 0, sizeof(out));
    ck_assert_int_eq(imx95_ahab_load(&c, 1, out, dev_read, NULL), 0);
    ck_assert_uint_eq(out[0], 0x5A);
    ck_assert_uint_eq(out[sizeof(out) - 1], 0x5A);
}
END_TEST

START_TEST(test_load_rejects_bad_requests)
{
    struct imx95_ahab_container c;
    static uint8_t out[0x1000];

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    /* Index past the end of the array. */
    ck_assert_int_lt(imx95_ahab_load(&c, c.count, out, dev_read, NULL), 0);
    ck_assert_int_lt(imx95_ahab_load(&c, 0, NULL, dev_read, NULL), 0);

    /* A zero-length image has nothing to load. */
    c.img[0].size = 0;
    ck_assert_int_lt(imx95_ahab_load(&c, 0, out, dev_read, NULL), 0);

    /* Offsets the image tool never emits are refused rather than loaded from
     * the wrong place. */
    c.img[0].size = 0x1000;
    c.img[0].offset = 0x1001;
    ck_assert_int_lt(imx95_ahab_load(&c, 0, out, dev_read, NULL), 0);
    c.img[0].offset = 0x1000;
    c.img[0].size = 0x101;
    ck_assert_int_lt(imx95_ahab_load(&c, 0, out, dev_read, NULL), 0);
}
END_TEST

static Suite *imx95_ahab_suite(void)
{
    Suite *s = suite_create("imx95-ahab");
    TCase *tc = tcase_create("container_parse");

    tcase_add_test(tc, test_parse_valid);
    tcase_add_test(tc, test_next_rounds_up);
    tcase_add_test(tc, test_size_covers_signature_block);
    tcase_add_test(tc, test_reject_bad_tag_and_version);
    tcase_add_test(tc, test_reject_bad_image_count);
    tcase_add_test(tc, test_reject_offset_size_overflow);
    tcase_add_test(tc, test_reject_signature_block_out_of_range);
    tcase_add_test(tc, test_reject_unaligned_base_and_read_failure);
    tcase_add_test(tc, test_load_copies_image);
    tcase_add_test(tc, test_load_rejects_bad_requests);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = imx95_ahab_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
