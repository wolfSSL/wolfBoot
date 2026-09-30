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

    uint32_t next;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_int_eq(imx95_ahab_next(&c, &next), 0);
    ck_assert_uint_eq(next, IMX95_AHAB_MMC_OFFSET + 0x3000);

    /* An end that is not already aligned must round up, not truncate. */
    c.size = 0x3001;
    ck_assert_int_eq(imx95_ahab_next(&c, &next), 0);
    ck_assert_uint_eq(next, (IMX95_AHAB_MMC_OFFSET + 0x3400));

    /* The round-up must not wrap. An end inside the last IMX95_AHAB_ALIGN-1
     * bytes of the address space used to come back as 0, which is aligned and
     * would have been accepted as the next container's offset. */
    c.base = 0xFFFFF800U;
    c.size = 0x7FFU;
    ck_assert_int_lt(imx95_ahab_next(&c, &next), 0);

    c.base = 0U;
    c.size = 0xFFFFFFFFU;
    ck_assert_int_lt(imx95_ahab_next(&c, &next), 0);

    /* A container that does not advance would loop the walk forever. */
    c.base = IMX95_AHAB_MMC_OFFSET;
    c.size = 0U;
    ck_assert_int_lt(imx95_ahab_next(&c, &next), 0);

    ck_assert_int_lt(imx95_ahab_next(NULL, &next), 0);
    ck_assert_int_lt(imx95_ahab_next(&c, NULL), 0);
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

    /* A non-block-multiple size is read rounded up, as U-Boot's
     * read_auth_image() does. Those extra bytes are why the destination checks
     * bound the rounded-up extent rather than size. */
    c.img[0].offset = 0x1000;
    c.img[0].size = 0x101;
    ck_assert_int_eq(imx95_ahab_load(&c, 0, out, dev_read, NULL), 0);
    ck_assert_uint_eq(imx95_ahab_load_len(0x101), 0x200);
    ck_assert_uint_eq(imx95_ahab_load_len(0x200), 0x200);
    ck_assert_uint_eq(imx95_ahab_load_len(0), 0);
    /* Rounding up must not wrap. */
    ck_assert_uint_eq(imx95_ahab_load_len(0xFFFFFF01U), 0U);
}
END_TEST

/* The self-overlap test has to use the length that is actually written, not the
 * recorded size: imx95_ahab_load() block-pads the transfer, so an image ending
 * just below a protected region can still have its tail land inside it. */
START_TEST(test_overlaps_uses_padded_length)
{
    const uint64_t lo = 0x20480000UL;   /* stand-ins for _start_text .. */
    const uint64_t hi = 0x204D6000UL;   /* .. END_STACK */

    /* Ends exactly at lo with an already-aligned size: no overlap, and the
     * padding changes nothing because there is none. */
    ck_assert_int_eq(imx95_ahab_overlaps(lo - 0x200UL, 0x200U, lo, hi), 0);
    /* The case that matters. Each of these ends exactly at lo when the recorded
     * size is taken literally - which an unrounded check reads as "clear" - but
     * the padded transfer runs 0x100 bytes past it, into the loader. */
    ck_assert_int_eq(imx95_ahab_overlaps(lo - 0x100UL, 0x100U, lo, hi), 1);
    ck_assert_int_eq(imx95_ahab_overlaps(lo - 0x300UL, 0x300U, lo, hi), 1);
    ck_assert_int_eq(imx95_ahab_overlaps(lo - 0x500UL, 0x500U, lo, hi), 1);
    /* Wholly below, and wholly above. */
    ck_assert_int_eq(imx95_ahab_overlaps(lo - 0x10000UL, 0x200U, lo, hi), 0);
    ck_assert_int_eq(imx95_ahab_overlaps(hi, 0x200U, lo, hi), 0);
    /* Inside. */
    ck_assert_int_eq(imx95_ahab_overlaps(lo, 0x200U, lo, hi), 1);
    /* A size whose round-up overflows is refused rather than wrapped. */
    ck_assert_int_eq(imx95_ahab_overlaps(lo, 0xFFFFFF01U, lo, hi), 1);
    /* So is a destination whose padded end would wrap 64 bits. */
    ck_assert_int_eq(imx95_ahab_overlaps(0xFFFFFFFFFFFFFF00ULL, 0x200U,
                                         lo, hi), 1);
}
END_TEST

/* The image table has to live inside the bytes a signature covers. */
START_TEST(test_parse_rejects_table_outside_header)
{
    struct imx95_ahab_container c;

    setup_valid();
    /* Shrink the header's own length so the two entries no longer fit. */
    put16(IMX95_AHAB_MMC_OFFSET + 1, AHAB_HDR_SIZE + AHAB_IMG_ENTRY_SIZE);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);

    /* A signature block starting inside the table is equally wrong: the
     * entries would overlap the bytes that authenticate them. */
    setup_valid();
    put16(IMX95_AHAB_MMC_OFFSET + 12, AHAB_HDR_SIZE + 4U);
    ck_assert_int_lt(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
}
END_TEST

/* The span handed to the ELE covers the header, table and signature block, and
 * never the image payloads. */
START_TEST(test_span_bounds)
{
    struct imx95_ahab_container c;
    uint32_t span;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_int_eq(imx95_ahab_span(&c, &span), 0);
    /* Two entries plus the header, not the 0x3000 the payloads reach. */
    ck_assert_uint_eq(span, AHAB_HDR_SIZE + (2U * AHAB_IMG_ENTRY_SIZE));
    ck_assert_uint_lt(span, c.size);

    /* Anything past the staging window is refused rather than truncated. */
    c.hdr_len = IMX95_AHAB_SPAN_MAX + 1U;
    ck_assert_int_lt(imx95_ahab_span(&c, &span), 0);
    c.hdr_len = IMX95_AHAB_SPAN_MAX;
    ck_assert_int_eq(imx95_ahab_span(&c, &span), 0);

    /* A header that understates its own length must not shrink the span below
     * the table it declares, or the ELE would be given fewer bytes than the
     * entries stage 1 goes on to use. */
    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    c.hdr_len = AHAB_HDR_SIZE;
    c.sig_end = 0U;
    ck_assert_int_eq(imx95_ahab_span(&c, &span), 0);
    ck_assert_uint_eq(span, AHAB_HDR_SIZE + (c.count * AHAB_IMG_ENTRY_SIZE));

    /* The signature block can be the furthest thing, and then it sets it. */
    c.sig_end = 0x800U;
    ck_assert_int_eq(imx95_ahab_span(&c, &span), 0);
    ck_assert_uint_eq(span, 0x800U);

    ck_assert_int_lt(imx95_ahab_span(NULL, &span), 0);
    ck_assert_int_lt(imx95_ahab_span(&c, NULL), 0);
}
END_TEST

/* Destinations are checked before a single byte is written, because an image is
 * streamed straight into img->dst. */
START_TEST(test_check_dst)
{
    struct imx95_ahab_container c;
    struct imx95_ahab_region excl[1];
    const uint64_t dram = 0x80000000ULL;
    const uint64_t dram_sz = 0x80000000ULL;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);

    excl[0].base = 0x90000000ULL;   /* the staging window */
    excl[0].size = 0x10000ULL;
    ck_assert_int_eq(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* Outside DRAM, below and above. */
    c.img[0].dst = 0x20480000ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);
    c.img[0].dst = dram + dram_sz - 0x100ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* Landing on the staged container would rewrite what the ELE blessed. */
    c.img[0].dst = 0x90000000ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* Two images may not overlap, or a later load replaces a verified one. */
    c.img[0].dst = 0x8A200000ULL;
    c.img[1].dst = 0x8A200800ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* Overlap only in the block padding still counts. */
    c.img[0].dst = 0x8A200000ULL;
    c.img[0].size = 0x1001U;        /* rounds up to 0x1200 */
    c.img[1].dst = 0x8A201100ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* dst + length must not wrap 64-bit. */
    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    c.img[0].dst = 0xFFFFFFFFFFFFF000ULL;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    /* A zero-length image is not something a real container carries. */
    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    c.img[0].size = 0U;
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, excl, 1), 0);

    ck_assert_int_lt(imx95_ahab_check_dst(NULL, dram, dram_sz, excl, 1), 0);
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, 0U, excl, 1), 0);
    ck_assert_int_lt(imx95_ahab_check_dst(&c, dram, dram_sz, NULL, 1), 0);
}
END_TEST

/* The image stage 1 branches into must be an A55 executable entered inside
 * itself. */
START_TEST(test_check_exec)
{
    struct imx95_ahab_container c;

    setup_valid();
    ck_assert_int_eq(imx95_ahab_parse(&c, IMX95_AHAB_MMC_OFFSET, dev_read,
                                      NULL), 0);
    ck_assert_int_eq(imx95_ahab_check_exec(&c, 0), 0);

    /* ELE firmware is not something the A55 branches into. */
    c.img[0].flags = IMX95_AHAB_TYPE_ELE | (IMX95_AHAB_CORE_A55 << 4);
    ck_assert_int_lt(imx95_ahab_check_exec(&c, 0), 0);

    /* Nor is an M33 image. */
    c.img[0].flags = IMX95_AHAB_TYPE_EXEC | (IMX95_AHAB_CORE_M33 << 4);
    ck_assert_int_lt(imx95_ahab_check_exec(&c, 0), 0);

    /* An entry outside the image's own bytes is the interesting case: it is
     * what lets a signed-but-weird container redirect the branch. */
    c.img[0].flags = IMX95_AHAB_TYPE_EXEC | (IMX95_AHAB_CORE_A55 << 4);
    c.img[0].entry = c.img[0].dst - 4ULL;
    ck_assert_int_lt(imx95_ahab_check_exec(&c, 0), 0);
    c.img[0].entry = c.img[0].dst + c.img[0].size;
    ck_assert_int_lt(imx95_ahab_check_exec(&c, 0), 0);
    c.img[0].entry = c.img[0].dst + c.img[0].size - 4ULL;
    ck_assert_int_eq(imx95_ahab_check_exec(&c, 0), 0);

    ck_assert_int_lt(imx95_ahab_check_exec(NULL, 0), 0);
    ck_assert_int_lt(imx95_ahab_check_exec(&c, c.count), 0);
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
    tcase_add_test(tc, test_parse_rejects_table_outside_header);
    tcase_add_test(tc, test_span_bounds);
    tcase_add_test(tc, test_check_dst);
    tcase_add_test(tc, test_check_exec);
    tcase_add_test(tc, test_overlaps_uses_padded_length);
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
