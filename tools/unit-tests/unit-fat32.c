/* unit-fat32.c
 *
 * Interoperability tests for the read-only FAT32 backend (src/fat32.c)
 * against a volume produced by the host's real mkfs.vfat and mcopy.
 *
 * Crafted and hostile metadata is covered separately in
 * unit-fs-malicious.c, which needs no external tools and therefore always
 * runs.
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
/* fseek() takes a long, which is 32-bit on some hosts, so offsets past
 * 2 GiB need fseeko()/off_t even with large-file support enabled. */
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
#include "fat32.c"

#define IMG_PATH      "fat32-test.img"
#define PAYLOAD_PATH  "fat32-payload.bin"
#define NESTED_PATH   "fat32-nested.bin"
#define SHORT_PATH    "fat32-short.bin"

#define LFN_NAME "/boot/a-very-long-image-name-for-lfn.itb"
/* 60 characters: five LFN entries, still inside WOLFBOOT_FS_MAX_NAME. */
#define LFN5_NAME \
    "/boot/a-deliberately-long-filename-that-needs-five-lfn-entries.itb"

/* The filesystem starts this far into the fake drive, so every offset the
 * parser computes has to be partition-relative to land in the right place. */
#define PART_PAD  (1024ULL * 1024ULL)

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

/* Present the whole test image as partition 0 of drive 0. */
static void open_image(void)
{
    if (img_fp == NULL) {
        img_fp = fopen(IMG_PATH, "rb");
        ck_assert(img_fp != NULL);
        ck_assert_int_eq(fseeko(img_fp, 0, SEEK_END), 0);
        img_size = (uint64_t)ftello(img_fp);
        ck_assert(img_size > 0);
    }
    memset(Drives, 0, sizeof(Drives));
    Drives[0].drv = 0;
    Drives[0].is_open = 1;
    Drives[0].n_parts = 1;
    Drives[0].part[0].drv = 0;
    Drives[0].part[0].start = PART_PAD;
    Drives[0].part[0].end = PART_PAD + img_size - 1;
    fs_cache_invalidate();
    disk_read_count = 0;
}

static void mount_ok(struct fs_volume *vol)
{
    open_image();
    ck_assert_int_eq(fs_mount(vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol->type, FS_TYPE_FAT32);
}

/* --- mount --- */

START_TEST(test_fat32_mount_geometry)
{
    struct fs_volume vol;

    mount_ok(&vol);
    /* Below this cluster count the volume would be FAT16 by definition. */
    ck_assert_uint_ge(vol.u.fat.clus_count, 65525U);
    ck_assert_uint_eq(vol.u.fat.clus_sz, 512U);
    ck_assert_uint_ge(vol.u.fat.root_clus, 2U);
    ck_assert_uint_le(vol.u.fat.root_clus, vol.u.fat.clus_count + 1U);
    /* The FAT must be able to describe every cluster. */
    ck_assert(vol.u.fat.fat_sz >= ((uint64_t)vol.u.fat.clus_count + 2U) * 4U);
    /* The data region must lie inside the partition. */
    ck_assert(vol.u.fat.data_off +
        ((uint64_t)vol.u.fat.clus_count * vol.u.fat.clus_sz) <= vol.part_sz);
}
END_TEST

START_TEST(test_fat32_type_name)
{
    struct fs_volume vol;

    mount_ok(&vol);
    ck_assert_str_eq(fs_type_name(&vol), "fat32");
}
END_TEST

/* --- name resolution --- */

/* A name of 53-64 characters needs five LFN entries but still fits
 * WOLFBOOT_FS_MAX_NAME (64). Bounding on ord * 13 overestimates the length
 * by up to 12, which drops this whole band and makes such a BOOT_FILE_A
 * unresolvable. The per-character bound is what catches genuinely
 * over-long names. */
START_TEST(test_fat32_open_five_entry_long_name)
{
    struct fs_volume vol;
    struct fs_file f;

    open_image();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_FAT32);
    ck_assert_int_eq(fs_open(&vol, &f, LFN5_NAME, 1U << 30), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned)fs_size(&f), 4096U);
}
END_TEST

START_TEST(test_fat32_open_long_name)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)plen);
    free(payload);
}
END_TEST

START_TEST(test_fat32_open_short_name)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t slen;
    uint8_t *sdata = load_file(SHORT_PATH, &slen);

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/SHORT.BIN", slen), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)slen);
    free(sdata);
}
END_TEST

/* FAT names are case-insensitive, for both the 8.3 and long forms. */
START_TEST(test_fat32_open_case_insensitive)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f,
        "/BOOT/A-VERY-LONG-IMAGE-NAME-FOR-LFN.ITB", 1U << 30),
        WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/short.bin", 1U << 30),
        WOLFBOOT_FS_OK);
    /* The 8.3 alias mkfs generated for the long name. */
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/A-VERY~1.ITB", 1U << 30),
        WOLFBOOT_FS_OK);
}
END_TEST

START_TEST(test_fat32_open_nested_path)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t nlen;
    uint8_t *ndata = load_file(NESTED_PATH, &nlen);

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/sub/nested.bin", nlen),
        WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)nlen);
    free(ndata);
}
END_TEST

START_TEST(test_fat32_open_missing)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/nope.itb", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/nodir/nope.itb", 1U << 30),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

START_TEST(test_fat32_open_directory_is_not_a_file)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/boot", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
    ck_assert_int_eq(fs_open(&vol, &f, "/", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
}
END_TEST

START_TEST(test_fat32_cannot_descend_into_a_file)
{
    struct fs_volume vol;
    struct fs_file f;

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/SHORT.BIN/x", 1U << 30),
        WOLFBOOT_FS_E_NOTFILE);
}
END_TEST

/* The size in the directory entry is attacker-controlled and is about to
 * bound a load into RAM, so it must be refused before any of it is used. */
START_TEST(test_fat32_open_respects_max_size)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen - 1),
        WOLFBOOT_FS_E_TOOBIG);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);
    free(payload);
}
END_TEST

START_TEST(test_fat32_label)
{
    struct fs_volume vol;

    mount_ok(&vol);
    ck_assert_int_eq(fs_label_eq(&vol, "WBTEST"), 1);
    ck_assert_int_eq(fs_label_eq(&vol, "wbtest"), 1);
    ck_assert_int_eq(fs_label_eq(&vol, "WBTES"), 0);
    ck_assert_int_eq(fs_label_eq(&vol, "WBTESTX"), 0);
    ck_assert_int_eq(fs_label_eq(&vol, ""), 0);
}
END_TEST

/* --- reading --- */

START_TEST(test_fat32_read_whole_file)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);

    ck_assert(got != NULL);
    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, plen, got), (int)plen);
    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    free(got);
    free(payload);
}
END_TEST

/* The disk loader reads in fixed-size chunks, so the same file must come
 * back identically when it is fetched piecewise. */
START_TEST(test_fat32_read_chunked)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen, off;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);
    const uint64_t chunk = 4096;
    int ret;

    ck_assert(got != NULL);
    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);
    for (off = 0; off < plen; off += chunk) {
        ret = fs_read(&f, off, chunk, got + off);
        ck_assert_int_gt(ret, 0);
        ck_assert_uint_eq((unsigned int)ret,
            (unsigned int)((plen - off) < chunk ? (plen - off) : chunk));
    }
    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    free(got);
    free(payload);
}
END_TEST

/* Exercise every combination of unaligned start and length around the
 * cluster boundaries, where the head/tail split happens. */
START_TEST(test_fat32_read_cluster_boundaries)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t buf[4096];
    static const int deltas[] = { -3, -1, 0, 1, 3 };
    uint32_t cs;
    unsigned int b, d, l;
    static const uint32_t lens[] = { 1, 7, 511, 512, 513, 1024, 4096 };
    uint64_t off;
    int ret;

    mount_ok(&vol);
    cs = vol.u.fat.clus_sz;
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);

    for (b = 1; b <= 4; b++) {
        for (d = 0; d < sizeof(deltas) / sizeof(deltas[0]); d++) {
            off = (uint64_t)((int64_t)((uint64_t)b * cs) + deltas[d]);
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

/* A backwards seek must restart the chain walk and still be correct. */
START_TEST(test_fat32_read_backwards)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t buf[256];

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_read(&f, plen - 256, 256, buf), 256);
    ck_assert_int_eq(memcmp(buf, payload + plen - 256, 256), 0);
    ck_assert_int_eq(fs_read(&f, 0, 256, buf), 256);
    ck_assert_int_eq(memcmp(buf, payload, 256), 0);
    ck_assert_int_eq(fs_read(&f, 100000, 256, buf), 256);
    ck_assert_int_eq(memcmp(buf, payload + 100000, 256), 0);
    free(payload);
}
END_TEST

START_TEST(test_fat32_read_eof)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t buf[512];

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, plen, sizeof(buf), buf), 0);
    ck_assert_int_eq(fs_read(&f, plen - 10, sizeof(buf), buf), 10);
    ck_assert_int_eq(memcmp(buf, payload + plen - 10, 10), 0);
    free(payload);
}
END_TEST

START_TEST(test_fat32_read_small_files)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t slen, nlen;
    uint8_t *sdata = load_file(SHORT_PATH, &slen);
    uint8_t *ndata = load_file(NESTED_PATH, &nlen);
    uint8_t buf[8192];

    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, "/SHORT.BIN", slen), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, slen, buf), (int)slen);
    ck_assert_int_eq(memcmp(buf, sdata, (size_t)slen), 0);

    ck_assert_int_eq(fs_open(&vol, &f, "/boot/sub/nested.bin", nlen),
        WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, nlen, buf), (int)nlen);
    ck_assert_int_eq(memcmp(buf, ndata, (size_t)nlen), 0);
    free(sdata);
    free(ndata);
}
END_TEST

/* An unfragmented file must coalesce into large media reads rather than
 * one read per cluster. On the slower SD targets a per-cluster read would
 * dominate boot time, so this is a performance contract, not a nicety. */
START_TEST(test_fat32_contiguous_runs_are_coalesced)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t plen;
    uint8_t *payload = load_file(PAYLOAD_PATH, &plen);
    uint8_t *got = (uint8_t *)malloc((size_t)plen);
    unsigned int reads;
    unsigned int per_cluster;

    ck_assert(got != NULL);
    mount_ok(&vol);
    ck_assert_int_eq(fs_open(&vol, &f, LFN_NAME, plen), WOLFBOOT_FS_OK);

    disk_read_count = 0;
    ck_assert_int_eq(fs_read(&f, 0, plen, got), (int)plen);
    reads = disk_read_count;
    per_cluster = (unsigned int)(plen / vol.u.fat.clus_sz);

    ck_assert_int_eq(memcmp(got, payload, (size_t)plen), 0);
    /* Far fewer than one media read per cluster: the FAT walk itself costs
     * a few cached reads, the payload should cost only a handful. */
    ck_assert_uint_lt(reads, per_cluster / 8U);
    free(got);
    free(payload);
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-fat32");
    TCase *tc_mount = tcase_create("fat32-mount");
    TCase *tc_name = tcase_create("fat32-names");
    TCase *tc_read = tcase_create("fat32-read");

    tcase_add_test(tc_mount, test_fat32_mount_geometry);
    tcase_add_test(tc_mount, test_fat32_type_name);
    tcase_add_test(tc_mount, test_fat32_label);
    suite_add_tcase(s, tc_mount);

    tcase_add_test(tc_name, test_fat32_open_long_name);
    tcase_add_test(tc_name, test_fat32_open_five_entry_long_name);
    tcase_add_test(tc_name, test_fat32_open_short_name);
    tcase_add_test(tc_name, test_fat32_open_case_insensitive);
    tcase_add_test(tc_name, test_fat32_open_nested_path);
    tcase_add_test(tc_name, test_fat32_open_missing);
    tcase_add_test(tc_name, test_fat32_open_directory_is_not_a_file);
    tcase_add_test(tc_name, test_fat32_cannot_descend_into_a_file);
    tcase_add_test(tc_name, test_fat32_open_respects_max_size);
    suite_add_tcase(s, tc_name);

    tcase_add_test(tc_read, test_fat32_read_whole_file);
    tcase_add_test(tc_read, test_fat32_read_chunked);
    tcase_add_test(tc_read, test_fat32_read_cluster_boundaries);
    tcase_add_test(tc_read, test_fat32_read_backwards);
    tcase_add_test(tc_read, test_fat32_read_eof);
    tcase_add_test(tc_read, test_fat32_read_small_files);
    tcase_add_test(tc_read, test_fat32_contiguous_runs_are_coalesced);
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
