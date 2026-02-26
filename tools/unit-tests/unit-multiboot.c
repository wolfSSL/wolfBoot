/* unit-multiboot.c
 *
 * Unit test for multiboot2 functions
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#ifndef WOLFBOOT_MULTIBOOT2
#define WOLFBOOT_MULTIBOOT2
#endif
#ifndef WOLFBOOT_FSP
#define WOLFBOOT_FSP 1
#endif
#ifndef DEBUG_MB2
#define DEBUG_MB2 1
#endif
#include <multiboot.c>

/* ---- Mock hob_iterate_memory_map ---- */

struct mock_mem_region {
    uint64_t start;
    uint64_t length;
    uint32_t type;
};

static struct mock_mem_region *mock_regions = NULL;
static int mock_region_count = 0;

int hob_iterate_memory_map(struct efi_hob *hobList, hob_mem_map_cb cb,
                           void *ctx)
{
    int i, r;
    (void)hobList;
    for (i = 0; i < mock_region_count; i++) {
        r = cb(mock_regions[i].start, mock_regions[i].length,
               mock_regions[i].type, ctx);
        if (r != 0)
            return r;
    }
    return 0;
}

static struct stage2_parameter make_stage2(void)
{
    struct stage2_parameter p;
    memset(&p, 0, sizeof(p));
    return p;
}

#define MB2_MAGIC 0xe85250d6

/* ---- Group 1: mb2_find_header ---- */

START_TEST(test_find_header_at_offset_0)
{
    uint32_t image[16];
    memset(image, 0, sizeof(image));
    image[0] = MB2_MAGIC;
    ck_assert_ptr_eq(mb2_find_header((uint8_t *)image, sizeof(image)),
                     (uint8_t *)image);
}
END_TEST

START_TEST(test_find_header_at_8k)
{
    /* magic at byte 8192 in 16 KB image */
    int sz = 16384;
    uint8_t *image = calloc(1, sz);
    ck_assert_ptr_nonnull(image);
    *(uint32_t *)(image + 8192) = MB2_MAGIC;
    ck_assert_ptr_eq(mb2_find_header(image, sz), image + 8192);
    free(image);
}
END_TEST

START_TEST(test_find_header_at_9k)
{
    int sz = 10240;
    uint8_t *image = calloc(1, sz);
    ck_assert_ptr_nonnull(image);
    *(uint32_t *)(image + 9000) = MB2_MAGIC;
    ck_assert_ptr_eq(mb2_find_header(image, sz), image + 9000);
    free(image);
}
END_TEST

START_TEST(test_find_header_at_max_offset)
{
    /* Magic at byte 32760 (last valid scan position)*/
    int sz = 32764; /* MB2_HEADER_MAX_OFF */
    uint8_t *image = calloc(1, sz);
    ck_assert_ptr_nonnull(image);
    *(uint32_t *)(image + 32760) = MB2_MAGIC;
    ck_assert_ptr_eq(mb2_find_header(image, sz), image + 32760);
    free(image);
}
END_TEST


START_TEST(test_find_header_beyond_32k)
{
    /* Magic at byte 32764 — just past the last scanned position. */
    int sz = 32768;
    uint8_t *image = calloc(1, sz);
    ck_assert_ptr_nonnull(image);
    *(uint32_t *)(image + 32764) = MB2_MAGIC;
    ck_assert_ptr_null(mb2_find_header(image, sz));
    free(image);
}
END_TEST

START_TEST(test_find_header_no_magic)
{
    uint8_t image[256];
    memset(image, 0, sizeof(image));
    ck_assert_ptr_null(mb2_find_header(image, sizeof(image)));
}
END_TEST

START_TEST(test_find_header_size_zero)
{
    uint8_t image[4];
    ck_assert_ptr_null(mb2_find_header(image, 0));
}
END_TEST

START_TEST(test_find_header_negative_size)
{
    uint8_t image[4];
    ck_assert_ptr_null(mb2_find_header(image, -1));
}
END_TEST

START_TEST(test_find_header_small_size)
{
    /* size = 3: too small for even one uint32_t (3/4 == 0). */
    uint8_t image[4];
    image[0] = 0xd6; image[1] = 0x50; image[2] = 0x52;
    ck_assert_ptr_null(mb2_find_header(image, 3));
}
END_TEST

/* ---- Group 2: mb2_find_tag_by_type ---- */

START_TEST(test_find_tag_single)
{
    uint8_t buf[16] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0, sizeof(buf));
    /* Tag: type=5, size=8 */
    t = (struct mb2_tag *)buf;
    t->type = 5; t->flags = 0; t->size = 8;
    /* Terminator */
    t = (struct mb2_tag *)(buf + 8);
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_ptr_eq(mb2_find_tag_by_type(buf, sizeof(buf), 5), buf);
}
END_TEST

START_TEST(test_find_tag_multiple)
{
    uint8_t buf[24] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0, sizeof(buf));
    /* Tag 1: type=3 */
    t = (struct mb2_tag *)buf;
    t->type = 3; t->flags = 0; t->size = 8;
    /* Tag 2: type=5 */
    t = (struct mb2_tag *)(buf + 8);
    t->type = 5; t->flags = 0; t->size = 8;
    /* Terminator */
    t = (struct mb2_tag *)(buf + 16);
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_ptr_eq(mb2_find_tag_by_type(buf, sizeof(buf), 5), buf + 8);
}
END_TEST

START_TEST(test_find_tag_not_found)
{
    uint8_t buf[16] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0, sizeof(buf));
    t = (struct mb2_tag *)buf;
    t->type = 3; t->flags = 0; t->size = 8;
    t = (struct mb2_tag *)(buf + 8);
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_ptr_null(mb2_find_tag_by_type(buf, sizeof(buf), 5));
}
END_TEST

START_TEST(test_find_tag_empty_list)
{
    uint8_t buf[8] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0, sizeof(buf));
    t = (struct mb2_tag *)buf;
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_ptr_null(mb2_find_tag_by_type(buf, sizeof(buf), 5));
}
END_TEST

START_TEST(test_find_tag_zero_size)
{
    uint8_t buf[24] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0, sizeof(buf));
    t = (struct mb2_tag *)buf;
    t->type = 5; t->flags = 0; t->size = 0;
    t = (struct mb2_tag *)(buf + 8);
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_ptr_null(mb2_find_tag_by_type(buf, sizeof(buf), 99));
}
END_TEST

/* Bug 3: missing terminator must not read past buffer */
START_TEST(test_find_tag_no_terminator)
{
    uint8_t buf[16] __attribute__((aligned(8)));
    struct mb2_tag *t;
    memset(buf, 0xFF, sizeof(buf));
    /* Tag 1: type=3 (not the one we search for) */
    t = (struct mb2_tag *)buf;
    t->type = 3; t->flags = 0; t->size = 8;
    /* Tag 2: type=5 (the one we search for, but beyond the declared size) */
    t = (struct mb2_tag *)(buf + 8);
    t->type = 5; t->flags = 0; t->size = 8;

    /* Pass size=8: only covers tag 1. Tag 2 must not be found. */
    ck_assert_ptr_null(mb2_find_tag_by_type(buf, 8, 5));
}
END_TEST

/* ---- Group 3: mb2_align_address_up ---- */

START_TEST(test_align_already_aligned)
{
    uint8_t *r = mb2_align_address_up((uint8_t *)0x1000, 8);
    ck_assert_ptr_eq(r, (uint8_t *)0x1000);
}
END_TEST

START_TEST(test_align_needs_rounding)
{
    uint8_t *r = mb2_align_address_up((uint8_t *)0x1001, 8);
    ck_assert_ptr_eq(r, (uint8_t *)0x1008);
}
END_TEST

START_TEST(test_align_one_below)
{
    uint8_t *r = mb2_align_address_up((uint8_t *)0x100F, 8);
    ck_assert_ptr_eq(r, (uint8_t *)0x1010);
}
END_TEST

/* ---- Group 4: mb2_build_boot_info_header ---- */

/* Helper: build a mb2 header + info_req tag requesting one tag type,
 * followed by a terminator.  buf must be >= 48 bytes and 8-byte aligned. */
static void build_header_with_info_req(uint8_t *buf, uint32_t req_type)
{
    struct mb2_header *h = (struct mb2_header *)buf;
    struct mb2_tag_info_req *info;
    struct mb2_tag *term;

    memset(buf, 0, 48);
    h->magic = MB2_MAGIC;
    h->architecture = 0;
    h->header_length = 40; /* header(16) + info_req(16 aligned) + term(8) */
    h->checksum = 0;

    /* Info request tag at offset 16 */
    info = (struct mb2_tag_info_req *)(buf + 16);
    info->type = 1; /* MB2_TAG_TYPE_INFO_REQ */
    info->flags = 0;
    info->size = 12; /* 8 + one uint32_t */
    info->mbi_tag_types[0] = req_type;

    /* Terminator at offset 32 (12 bytes rounded up to 8-byte alignment) */
    term = (struct mb2_tag *)(buf + 32);
    term->type = 0;
    term->flags = 0;
    term->size = 8;
}

START_TEST(test_build_info_max_size_too_small)
{
    uint8_t boot_info[8];
    uint8_t header[48] __attribute__((aligned(8)));
    struct mb2_header *h = (struct mb2_header *)header;
    memset(header, 0, sizeof(header));
    h->header_length = sizeof(struct mb2_header) + 8;
    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, NULL, 4), -1);
}
END_TEST

START_TEST(test_build_info_no_info_req_tag)
{
    uint8_t header[32] __attribute__((aligned(8)));
    uint8_t boot_info[64];
    struct mb2_header *h = (struct mb2_header *)header;
    struct mb2_tag *t;

    memset(header, 0, sizeof(header));
    h->magic = MB2_MAGIC;
    h->header_length = sizeof(struct mb2_header) + 8; /* header + terminator */
    /* Only a terminator tag — no info request */
    t = (struct mb2_tag *)(header + sizeof(struct mb2_header));
    t->type = 0; t->flags = 0; t->size = 8;

    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, NULL,
                                   sizeof(boot_info)), -1);
}
END_TEST

START_TEST(test_build_info_unsupported_tag)
{
    uint8_t header[48] __attribute__((aligned(8)));
    uint8_t boot_info[256];
    build_header_with_info_req(header, 99); /* unknown type */
    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, NULL,
                                   sizeof(boot_info)), -1);
}
END_TEST

/* Bug 2: info_req tag with size < sizeof(struct mb2_tag) is rejected by the
 * size guard in mb2_find_tag_by_type, so the tag is never found. */
START_TEST(test_build_info_malformed_size)
{
    uint8_t header[48] __attribute__((aligned(8)));
    uint8_t boot_info[256];
    struct mb2_header *h = (struct mb2_header *)header;
    struct mb2_tag_info_req *info;
    struct mb2_tag *term;

    memset(header, 0, sizeof(header));
    h->magic = MB2_MAGIC;
    h->architecture = 0;
    h->header_length = 32;
    h->checksum = 0;

    /* Info request tag with impossibly small size (4 < 8) */
    info = (struct mb2_tag_info_req *)(header + 16);
    info->type = 1;
    info->flags = 0;
    info->size = 4; /* smaller than struct mb2_tag */

    term = (struct mb2_tag *)(header + 24);
    term->type = 0; term->flags = 0; term->size = 8;

    /* Malformed tag is skipped, info_req not found → returns -1 */
    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, NULL,
                                    sizeof(boot_info)), -1);
}
END_TEST

/* check that header_length < sizeof(struct mb2_header) do not causes uint32_t
 * underflow in the subtraction header_length - sizeof(struct mb2_header),
 * wrapping to ~4GB and defeating the bounds check in mb2_find_tag_by_type.
 *
 * we place a fake info_req tag right after the header so
 * that the overflowed bounds check lets mb2_find_tag_by_type find it.
 * Without the fix the function processes the tag and returns 0 (success).
 * With the fix it rejects header_length < 16 and returns -1. */
START_TEST(test_build_info_header_length_underflow)
{
    uint8_t header[48] __attribute__((aligned(8)));
    uint8_t boot_info[256];
    struct stage2_parameter p = make_stage2();
    struct mb2_header *h = (struct mb2_header *)header;
    struct mb2_tag_info_req *info;
    struct mb2_tag *term;
    memset(header, 0, sizeof(header));
    h->magic = MB2_MAGIC;
    h->header_length = 8; /* less than sizeof(struct mb2_header) = 16 */

    /* Place a reachable info_req tag after the header.  With the underflow
     * bug tags_len wraps to ~4GB so the tag is "in bounds". */
    info = (struct mb2_tag_info_req *)(header + sizeof(struct mb2_header));
    info->type = 1; /* MB2_TAG_TYPE_INFO_REQ */
    info->flags = 0;
    info->size = 12;
    info->mbi_tag_types[0] = 4; /* MB2_REQ_TAG_BASIC_MEM_INFO */

    term = (struct mb2_tag *)(header + 32);
    term->type = 0; term->flags = 0; term->size = 8;

    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, &p,
                                   sizeof(boot_info)), -1);
}
END_TEST

/* ---- Group 5: mb2_add_basic_mem_info ---- */

START_TEST(test_basic_mem_info_normal)
{
    uint8_t buf[64] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(buf);
    struct mb2_basic_memory_info *info;
    struct mock_mem_region regions[] = {
        {0, 640 * 1024, EFI_RESOURCE_SYSTEM_MEMORY},
        {1024 * 1024, 127ULL * 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 2;

    ck_assert_int_eq(mb2_add_basic_mem_info(&idx, &p, &max_size), 0);
    info = (struct mb2_basic_memory_info *)buf;
    ck_assert_uint_eq(info->type, 4);
    ck_assert_uint_eq(info->size, sizeof(*info));
    ck_assert_uint_eq(info->mem_lower, 640);
    ck_assert_uint_eq(info->mem_upper, 127 * 1024);
    ck_assert_ptr_eq(idx, buf + sizeof(*info));

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_basic_mem_info_no_lower)
{
    uint8_t buf[64] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(buf);
    struct mb2_basic_memory_info *info;
    struct mock_mem_region regions[] = {
        {1024 * 1024, 127ULL * 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 1;

    ck_assert_int_eq(mb2_add_basic_mem_info(&idx, &p, &max_size), 0);
    info = (struct mb2_basic_memory_info *)buf;
    ck_assert_uint_eq(info->mem_lower, 0);
    ck_assert_uint_eq(info->mem_upper, 127 * 1024);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_basic_mem_info_no_upper)
{
    uint8_t buf[64] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(buf);
    struct mb2_basic_memory_info *info;
    struct mock_mem_region regions[] = {
        {0, 640 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 1;

    ck_assert_int_eq(mb2_add_basic_mem_info(&idx, &p, &max_size), 0);
    info = (struct mb2_basic_memory_info *)buf;
    ck_assert_uint_eq(info->mem_lower, 640);
    ck_assert_uint_eq(info->mem_upper, 0);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_basic_mem_info_too_small)
{
    uint8_t buf[8];
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(struct mb2_basic_memory_info) - 1;

    mock_regions = NULL;
    mock_region_count = 0;
    ck_assert_int_eq(mb2_add_basic_mem_info(&idx, &p, &max_size), -1);
}
END_TEST

/* ---- Group 6: mb2_add_mem_map ---- */

START_TEST(test_mem_map_single_ram)
{
    uint8_t buf[128] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(buf);
    struct mb2_mem_map_header *hdr;
    struct mb2_mem_map_entry *entry;
    struct mock_mem_region regions[] = {
        {0x100000, 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 1;

    ck_assert_int_eq(mb2_add_mem_map(&idx, &p, &max_size), 0);
    hdr = (struct mb2_mem_map_header *)buf;
    ck_assert_uint_eq(hdr->type, 6);
    ck_assert_uint_eq(hdr->entry_size, sizeof(struct mb2_mem_map_entry));
    ck_assert_uint_eq(hdr->entry_version, 0);
    ck_assert_uint_eq(hdr->size,
                      sizeof(struct mb2_mem_map_header) +
                      sizeof(struct mb2_mem_map_entry));

    entry = (struct mb2_mem_map_entry *)(buf + sizeof(*hdr));
    ck_assert(entry->base_addr == 0x100000);
    ck_assert(entry->length == 1024 * 1024);
    ck_assert_uint_eq(entry->type, 1); /* MB2_MEM_INFO_MEM_RAM */

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_mem_map_mixed_types)
{
    uint8_t buf[128] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(buf);
    struct mb2_mem_map_header *hdr;
    struct mb2_mem_map_entry *e;
    struct mock_mem_region regions[] = {
        {0, 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY},
        {0xF0000000ULL, 16ULL * 1024 * 1024, EFI_RESOURCE_MEMORY_RESERVED}
    };
    mock_regions = regions;
    mock_region_count = 2;

    ck_assert_int_eq(mb2_add_mem_map(&idx, &p, &max_size), 0);
    hdr = (struct mb2_mem_map_header *)buf;
    ck_assert_uint_eq(hdr->size,
                      sizeof(struct mb2_mem_map_header) +
                      2 * sizeof(struct mb2_mem_map_entry));

    e = (struct mb2_mem_map_entry *)(buf + sizeof(*hdr));
    ck_assert_uint_eq(e->type, 1); /* RAM */
    e++;
    ck_assert_uint_eq(e->type, 2); /* RESERVED */
    ck_assert(e->base_addr == 0xF0000000ULL);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_mem_map_too_small_header)
{
    uint8_t buf[8];
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(struct mb2_mem_map_header) - 1;

    mock_regions = NULL;
    mock_region_count = 0;
    ck_assert_int_eq(mb2_add_mem_map(&idx, &p, &max_size), -1);
}
END_TEST

START_TEST(test_mem_map_too_small_entry)
{
    uint8_t buf[64] __attribute__((aligned(8)));
    uint8_t *idx = buf;
    struct stage2_parameter p = make_stage2();
    unsigned max_size = sizeof(struct mb2_mem_map_header); /* fits header only */
    struct mock_mem_region regions[] = {
        {0, 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 1;

    ck_assert_int_eq(mb2_add_mem_map(&idx, &p, &max_size), -1);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

/* ---- Group 7: mb2_build_boot_info_header with HOB ---- */

START_TEST(test_build_info_basic_mem)
{
    uint8_t header[48] __attribute__((aligned(8)));
    uint8_t boot_info[256] __attribute__((aligned(8)));
    struct stage2_parameter p = make_stage2();
    struct mb2_boot_info_header *bih;
    struct mb2_basic_memory_info *meminfo;
    struct mock_mem_region regions[] = {
        {0, 640 * 1024, EFI_RESOURCE_SYSTEM_MEMORY},
        {1024 * 1024, 127ULL * 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 2;

    build_header_with_info_req(header, 4);
    memset(boot_info, 0, sizeof(boot_info));
    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, &p,
                                   sizeof(boot_info)), 0);

    bih = (struct mb2_boot_info_header *)boot_info;
    ck_assert_uint_eq(bih->total_size,
                      sizeof(*bih) + sizeof(struct mb2_basic_memory_info));

    meminfo = (struct mb2_basic_memory_info *)(boot_info + sizeof(*bih));
    ck_assert_uint_eq(meminfo->type, 4);
    ck_assert_uint_eq(meminfo->mem_lower, 640);
    ck_assert_uint_eq(meminfo->mem_upper, 127 * 1024);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

START_TEST(test_build_info_mem_map)
{
    uint8_t header[48] __attribute__((aligned(8)));
    uint8_t boot_info[512] __attribute__((aligned(8)));
    struct stage2_parameter p = make_stage2();
    struct mb2_boot_info_header *bih;
    struct mb2_mem_map_header *map_hdr;
    struct mb2_mem_map_entry *entry;
    struct mock_mem_region regions[] = {
        {0x100000, 127ULL * 1024 * 1024, EFI_RESOURCE_SYSTEM_MEMORY}
    };
    mock_regions = regions;
    mock_region_count = 1;

    build_header_with_info_req(header, 6);
    memset(boot_info, 0, sizeof(boot_info));
    ck_assert_int_eq(
        mb2_build_boot_info_header(boot_info, header, &p,
                                   sizeof(boot_info)), 0);

    bih = (struct mb2_boot_info_header *)boot_info;
    map_hdr = (struct mb2_mem_map_header *)(boot_info + sizeof(*bih));
    ck_assert_uint_eq(map_hdr->type, 6);
    ck_assert_uint_eq(map_hdr->entry_size, sizeof(struct mb2_mem_map_entry));
    ck_assert_uint_eq(map_hdr->size,
                      sizeof(struct mb2_mem_map_header) +
                      sizeof(struct mb2_mem_map_entry));

    entry = (struct mb2_mem_map_entry *)((uint8_t *)map_hdr + sizeof(*map_hdr));
    ck_assert(entry->base_addr == 0x100000);
    ck_assert(entry->length == 127ULL * 1024 * 1024);
    ck_assert_uint_eq(entry->type, 1);

    mock_regions = NULL;
    mock_region_count = 0;
}
END_TEST

/* ---- Suite ---- */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-multiboot2");

    TCase *tc_find = tcase_create("mb2_find_header");
    tcase_add_test(tc_find, test_find_header_at_offset_0);
    tcase_add_test(tc_find, test_find_header_at_8k);
    tcase_add_test(tc_find, test_find_header_at_9k);
    tcase_add_test(tc_find, test_find_header_at_max_offset);
    tcase_add_test(tc_find, test_find_header_beyond_32k);
    tcase_add_test(tc_find, test_find_header_no_magic);
    tcase_add_test(tc_find, test_find_header_size_zero);
    tcase_add_test(tc_find, test_find_header_negative_size);
    tcase_add_test(tc_find, test_find_header_small_size);
    suite_add_tcase(s, tc_find);

    TCase *tc_tag = tcase_create("mb2_find_tag_by_type");
    tcase_add_test(tc_tag, test_find_tag_single);
    tcase_add_test(tc_tag, test_find_tag_multiple);
    tcase_add_test(tc_tag, test_find_tag_not_found);
    tcase_add_test(tc_tag, test_find_tag_empty_list);
    tcase_add_test(tc_tag, test_find_tag_zero_size);
    tcase_add_test(tc_tag, test_find_tag_no_terminator);
    suite_add_tcase(s, tc_tag);

    TCase *tc_align = tcase_create("mb2_align_address_up");
    tcase_add_test(tc_align, test_align_already_aligned);
    tcase_add_test(tc_align, test_align_needs_rounding);
    tcase_add_test(tc_align, test_align_one_below);
    suite_add_tcase(s, tc_align);

    TCase *tc_build = tcase_create("mb2_build_boot_info_header");
    tcase_add_test(tc_build, test_build_info_max_size_too_small);
    tcase_add_test(tc_build, test_build_info_no_info_req_tag);
    tcase_add_test(tc_build, test_build_info_unsupported_tag);
    tcase_add_test(tc_build, test_build_info_malformed_size);
    tcase_add_test(tc_build, test_build_info_header_length_underflow);
    suite_add_tcase(s, tc_build);

    TCase *tc_basic = tcase_create("mb2_add_basic_mem_info");
    tcase_add_test(tc_basic, test_basic_mem_info_normal);
    tcase_add_test(tc_basic, test_basic_mem_info_no_lower);
    tcase_add_test(tc_basic, test_basic_mem_info_no_upper);
    tcase_add_test(tc_basic, test_basic_mem_info_too_small);
    suite_add_tcase(s, tc_basic);

    TCase *tc_mmap = tcase_create("mb2_add_mem_map");
    tcase_add_test(tc_mmap, test_mem_map_single_ram);
    tcase_add_test(tc_mmap, test_mem_map_mixed_types);
    tcase_add_test(tc_mmap, test_mem_map_too_small_header);
    tcase_add_test(tc_mmap, test_mem_map_too_small_entry);
    suite_add_tcase(s, tc_mmap);

    TCase *tc_hob = tcase_create("mb2_build_boot_info_with_hob");
    tcase_add_test(tc_hob, test_build_info_basic_mem);
    tcase_add_test(tc_hob, test_build_info_mem_map);
    suite_add_tcase(s, tc_hob);

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
