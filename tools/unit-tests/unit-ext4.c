/* unit-ext4.c
 *
 * Interoperability tests for the read-only ext4 backend (src/ext4.c)
 * against volumes produced by the host's real mke2fs.
 *
 * Two images are used. One is built with the smallest feature set this
 * parser accepts (1 KiB blocks, 128-byte inodes, no 64bit, no
 * metadata_csum). The other is built with mke2fs's stock ext4 profile,
 * which on a current e2fsprogs means 64bit + metadata_csum + flex_bg --
 * that image is what proves a default "mkfs.ext4" volume is readable.
 *
 * Crafted and hostile metadata is covered in unit-fs-malicious.c, which
 * needs no external tools and therefore always runs.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
/* Offsets into a disk image can exceed 2 GiB, so large-file support and
 * fseeko()/off_t are required: fseek() takes a long, which is 32-bit on
 * some hosts. */
#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "gpt.c"
#include "disk.c"
#include "disk_fs.c"
#include "ext4.c"

#define MIN_IMG      "ext4-min.img"
#define STOCK_IMG    "ext4-stock.img"
#define PAYLOAD_PATH "ext4-payload.bin"
#define NESTED_PATH  "ext4-nested.bin"

#define IMG_FILE "/boot/image.itb"

/* The filesystem starts this far into the fake drive, so every offset the
 * parser computes has to be partition-relative to land in the right place. */
#define PART_PAD (1024ULL * 1024ULL)

static FILE *img_fp = NULL;
static uint64_t img_size = 0;
static unsigned int disk_read_count = 0;

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    uint64_t off;

    (void)drv;
    disk_read_count++;
    if (start < PART_PAD) {
        if ((start + count) > PART_PAD)
            return -1;
        memset(buf, 0, count);
        return 0;
    }
    off = start - PART_PAD;
    if ((off + count) > img_size)
        return -1;
    if (fseeko(img_fp, (off_t)off, SEEK_SET) != 0)
        return -1;
    if (fread(buf, 1, (size_t)count, img_fp) != (size_t)count)
        return -1;
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

int disk_init(int drv) { (void)drv; return 0; }
void disk_close(int drv) { (void)drv; }

static uint8_t *load_file(const char *path, uint64_t *len)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    off_t sz;

    ck_assert(fp != NULL);
    ck_assert_int_eq(fseeko(fp, 0, SEEK_END), 0);
    sz = ftello(fp);
    ck_assert(sz > 0);
    ck_assert_int_eq(fseeko(fp, 0, SEEK_SET), 0);
    buf = (uint8_t *)malloc((size_t)sz);
    ck_assert(buf != NULL);
    ck_assert_uint_eq(fread(buf, 1, (size_t)sz, fp), (size_t)sz);
    fclose(fp);
    *len = (uint64_t)sz;
    return buf;
}

static void mount_img(struct fs_volume *vol, const char *path)
{
    if (img_fp != NULL) {
        fclose(img_fp);
        img_fp = NULL;
    }
    img_fp = fopen(path, "rb");
    ck_assert(img_fp != NULL);
    ck_assert_int_eq(fseeko(img_fp, 0, SEEK_END), 0);
    img_size = (uint64_t)ftello(img_fp);
    ck_assert(img_size > 0);

    memset(Drives, 0, sizeof(Drives));
    Drives[0].drv = 0;
    Drives[0].is_open = 1;
    Drives[0].n_parts = 1;
    Drives[0].part[0].drv = 0;
    Drives[0].part[0].start = PART_PAD;
    Drives[0].part[0].end = PART_PAD + img_size - 1;
    fs_cache_invalidate();
    disk_read_count = 0;

    ck_assert_int_eq(fs_mount(vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol->type, FS_TYPE_EXT4);
}

/* --- mount --- */

START_TEST(test_ext4_mount_minimal)
{
    struct fs_volume vol;

    mount_img(&vol, MIN_IMG);
    ck_assert_uint_eq(vol.u.ext.block_sz, 1024U);
    ck_assert_uint_eq(vol.u.ext.inode_sz, 128U);
    ck_assert_uint_eq(vol.u.ext.first_data_block, 1U);
    ck_assert_uint_eq(vol.u.ext.desc_sz, 32U);
    ck_assert(vol.u.ext.vol_sz <= vol.part_sz);
    ck_assert_str_eq(fs_type_name(&vol), "ext4");
}
END_TEST

/* A volume made by a plain "mkfs.ext4" must be readable. On a current
 * e2fsprogs that means 64bit, metadata_csum and flex_bg are all set. */
START_TEST(test_ext4_mount_stock_features)
{
    struct fs_volume vol;

    mount_img(&vol, STOCK_IMG);
    ck_assert_uint_eq(vol.u.ext.block_sz, 4096U);
    ck_assert_uint_eq(vol.u.ext.inode_sz, 256U);
    ck_assert_uint_eq(vol.u.ext.first_data_block, 0U);
    /* 64bit must have been honoured, widening the group descriptors. */
    ck_assert_uint_ge(vol.u.ext.desc_sz, 64U);
    ck_assert_uint_ne(vol.u.ext.incompat & 0x0080U, 0U);
}
END_TEST

START_TEST(test_ext4_labels)
{
    struct fs_volume vol;

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_label_eq(&vol, "WBEXT1"), 1);
    ck_assert_int_eq(fs_label_eq(&vol, "WBEXT2"), 0);
    /* ext4 labels are case-sensitive. */
    ck_assert_int_eq(fs_label_eq(&vol, "wbext1"), 0);

    mount_img(&vol, STOCK_IMG);
    ck_assert_int_eq(fs_label_eq(&vol, "WBEXT2"), 1);
    ck_assert_int_eq(fs_label_eq(&vol, "WBEXT1"), 0);
}
END_TEST

/* --- name resolution --- */

START_TEST(test_ext4_open_and_size)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)plen);

    mount_img(&vol, STOCK_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)plen);
    free(payload);
}
END_TEST

START_TEST(test_ext4_open_nested)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t nlen;
    uint8_t *ndata = load_file(NESTED_PATH, &nlen);

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/sub/nested.bin", nlen),
        WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)nlen);
    free(ndata);
}
END_TEST

/* Unlike FAT, ext4 names are case-sensitive. */
START_TEST(test_ext4_names_are_case_sensitive)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/BOOT/image.itb", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/IMAGE.ITB", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, 1U << 30),
        WOLFBOOT_FS_OK);
}
END_TEST

/* A prefix of a real name must not match it. */
START_TEST(test_ext4_partial_name_does_not_match)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/image.it", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/image.itbx", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

START_TEST(test_ext4_open_missing)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/nope.itb", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/nodir/nope.itb", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

START_TEST(test_ext4_directory_is_not_a_file)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
    ck_assert_int_eq(fs_open(&vol, &f, "/", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/image.itb/x", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
}
END_TEST

/* The inode-reported size is attacker-controlled and would bound a load
 * into RAM, so it is refused before any of it is used. */
START_TEST(test_ext4_open_respects_max_size)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen - 1),
        WOLFBOOT_FS_E_TOOBIG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);
    free(payload);
}
END_TEST

/* --- reading --- */

static void check_whole_file(const char *img)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);

    ck_assert(got != NULL);
    mount_img(&vol, img);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, plen, got), (int)plen);
    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    free(got);
    free(payload);
}

START_TEST(test_ext4_read_whole_file_minimal)
{
    check_whole_file(MIN_IMG);
}
END_TEST

START_TEST(test_ext4_read_whole_file_stock)
{
    check_whole_file(STOCK_IMG);
}
END_TEST

/* The disk loader reads in fixed-size chunks. */
START_TEST(test_ext4_read_chunked)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen, off;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);
    const uint64_t chunk = 4096;
    int ret;

    ck_assert(got != NULL);
    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);
    for (off = 0; off < plen; off += chunk) {
        ret = fs_read(&f, off, chunk, got + off);
        ck_assert_int_gt(ret, 0);
    }
    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    free(got);
    free(payload);
}
END_TEST

/* Unaligned starts and lengths around block boundaries exercise the
 * head/tail split of the mapping. */
START_TEST(test_ext4_read_block_boundaries)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t buf[8192];
    static const int deltas[] = { -3, -1, 0, 1, 3 };
    static const uint32_t lens[] = { 1, 7, 1023, 1024, 1025, 4096, 8192 };
    uint32_t bs;
    unsigned int b, d, l;
    uint64_t off;
    int ret;

    mount_img(&vol, MIN_IMG);
    bs = vol.u.ext.block_sz;
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);

    for (b = 1; b <= 4; b++) {
        for (d = 0; d < sizeof(deltas) / sizeof(deltas[0]); d++) {
            off = (uint64_t)((int64_t)((uint64_t)b * bs) + deltas[d]);
            for (l = 0; l < sizeof(lens) / sizeof(lens[0]); l++) {
                if ((off + lens[l]) > plen)
                    continue;
                ret = fs_read(&f, off, lens[l], buf);
                ck_assert_int_eq(ret, (int)lens[l]);
                ck_assert_int_eq(memcmp(buf, payload + off, lens[l]), 0);
            }
        }
    }
    free(payload);
}
END_TEST

START_TEST(test_ext4_read_backwards_and_eof)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t buf[512];

    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_read(&f, plen - 256, 256, buf), 256);
    ck_assert_int_eq(memcmp(buf, payload + plen - 256, 256), 0);
    ck_assert_int_eq(fs_read(&f, 0, 256, buf), 256);
    ck_assert_int_eq(memcmp(buf, payload, 256), 0);

    ck_assert_int_eq(fs_read(&f, plen, sizeof(buf), buf), 0);
    ck_assert_int_eq(fs_read(&f, plen - 10, sizeof(buf), buf), 10);
    ck_assert_int_eq(memcmp(buf, payload + plen - 10, 10), 0);
    free(payload);
}
END_TEST

START_TEST(test_ext4_read_nested)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t nlen;
    uint8_t *ndata = load_file(NESTED_PATH, &nlen);
    uint8_t buf[8192];

    mount_img(&vol, STOCK_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/sub/nested.bin", nlen),
        WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, nlen, buf), (int)nlen);
    ck_assert_int_eq(memcmp(buf, ndata, (size_t)nlen), 0);
    free(ndata);
}
END_TEST

/* An extent is contiguous by definition, so a freshly written file must
 * load in a handful of media reads rather than one per block. */
START_TEST(test_ext4_extents_are_coalesced)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);
    unsigned int reads;
    unsigned int per_block;

    ck_assert(got != NULL);
    mount_img(&vol, MIN_IMG);
    ck_assert_int_eq(fs_open(&vol, &f, IMG_FILE, plen), WOLFBOOT_FS_OK);

    disk_read_count = 0;
    ck_assert_int_eq(fs_read(&f, 0, plen, got), (int)plen);
    reads = disk_read_count;
    per_block = (unsigned int)(plen / vol.u.ext.block_sz);

    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    ck_assert_uint_lt(reads, per_block / 8U);
    free(got);
    free(payload);
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-ext4");
    TCase *tc_mount = tcase_create("ext4-mount");
    TCase *tc_name = tcase_create("ext4-names");
    TCase *tc_read = tcase_create("ext4-read");

    tcase_add_test(tc_mount, test_ext4_mount_minimal);
    tcase_add_test(tc_mount, test_ext4_mount_stock_features);
    tcase_add_test(tc_mount, test_ext4_labels);
    suite_add_tcase(s, tc_mount);

    tcase_add_test(tc_name, test_ext4_open_and_size);
    tcase_add_test(tc_name, test_ext4_open_nested);
    tcase_add_test(tc_name, test_ext4_names_are_case_sensitive);
    tcase_add_test(tc_name, test_ext4_partial_name_does_not_match);
    tcase_add_test(tc_name, test_ext4_open_missing);
    tcase_add_test(tc_name, test_ext4_directory_is_not_a_file);
    tcase_add_test(tc_name, test_ext4_open_respects_max_size);
    suite_add_tcase(s, tc_name);

    tcase_add_test(tc_read, test_ext4_read_whole_file_minimal);
    tcase_add_test(tc_read, test_ext4_read_whole_file_stock);
    tcase_add_test(tc_read, test_ext4_read_chunked);
    tcase_add_test(tc_read, test_ext4_read_block_boundaries);
    tcase_add_test(tc_read, test_ext4_read_backwards_and_eof);
    tcase_add_test(tc_read, test_ext4_read_nested);
    tcase_add_test(tc_read, test_ext4_extents_are_coalesced);
    suite_add_tcase(s, tc_read);

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
