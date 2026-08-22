/* unit-fs-probe.c
 *
 * Unit tests for the backend-independent parts of the read-only
 * filesystem layer (src/disk_fs.c): filesystem probing, the raw
 * passthrough, the metadata cache and the path splitter.
 *
 * Backend-specific probe rejection (FAT16 geometry, unsupported ext4
 * features) is covered in unit-fat32.c and unit-ext4.c, where those
 * parsers are compiled in.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "gpt.c"
#include "disk.c"
#include "disk_fs.c"

/* Fake disk backing store for mock disk_read/disk_write */
#define FAKE_DISK_SIZE (256 * 1024)
static uint8_t fake_disk[FAKE_DISK_SIZE];

/* Set to a byte offset to make disk_read fail at that address. -1 = no fail */
static int64_t mock_disk_read_fail_at = -1;

/* Number of disk_read calls since the last reset */
static unsigned int disk_read_count = 0;

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    (void)drv;
    disk_read_count++;
    if (mock_disk_read_fail_at >= 0 && (int64_t)start == mock_disk_read_fail_at)
        return -1;
    if (start + count > FAKE_DISK_SIZE)
        return -1;
    memcpy(buf, fake_disk + start, count);
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv;
    if (start + count > FAKE_DISK_SIZE)
        return -1;
    memcpy(fake_disk + start, buf, count);
    return 0;
}

int disk_init(int drv) { (void)drv; return 0; }
void disk_close(int drv) { (void)drv; }

/* Two MBR partitions, in sectors. */
#define P0_LBA   8
#define P0_SECS  64
#define P1_LBA   128
#define P1_SECS  64

#define P0_OFF   ((uint64_t)P0_LBA * 512)
#define P0_SIZE  ((uint64_t)P0_SECS * 512)
#define P1_OFF   ((uint64_t)P1_LBA * 512)
#define P1_SIZE  ((uint64_t)P1_SECS * 512)

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* Build an MBR with two type-0x83 partitions and open the drive. */
static void build_disk(void)
{
    uint8_t *e;

    memset(fake_disk, 0, sizeof(fake_disk));
    memset(Drives, 0, sizeof(Drives));

    e = fake_disk + GPT_MBR_ENTRY_START;
    e[4] = 0x83;
    put_le32(e + 8, P0_LBA);
    put_le32(e + 12, P0_SECS);

    e += 16;
    e[4] = 0x83;
    put_le32(e + 8, P1_LBA);
    put_le32(e + 12, P1_SECS);

    fake_disk[GPT_MBR_BOOTSIG_OFFSET] = 0x55;
    fake_disk[GPT_MBR_BOOTSIG_OFFSET + 1] = 0xAA;

    ck_assert_int_eq(disk_open(0), 2);
    mock_disk_read_fail_at = -1;
    disk_read_count = 0;
    fs_cache_invalidate();
}

/* Fill a partition with a deterministic, offset-dependent pattern. */
static void fill_pattern(uint64_t off, uint64_t len, uint8_t seed)
{
    uint64_t i;
    for (i = 0; i < len; i++)
        fake_disk[off + i] = (uint8_t)((i * 7U) + seed);
}

/* --- probing --- */

START_TEST(test_probe_wolfboot_magic_is_raw)
{
    struct fs_volume vol;

    build_disk();
    memcpy(fake_disk + P0_OFF, "WOLF", 4);

    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
    ck_assert_uint_eq((unsigned int)vol.part_sz, (unsigned int)P0_SIZE);
}
END_TEST

/* A raw image whose header happens to be followed by a 0x55AA at the end
 * of the sector must still take the raw short-circuit, not be probed. */
START_TEST(test_probe_magic_wins_over_boot_signature)
{
    struct fs_volume vol;

    build_disk();
    memcpy(fake_disk + P0_OFF, "WOLF", 4);
    fake_disk[P0_OFF + 0x1FE] = 0x55;
    fake_disk[P0_OFF + 0x1FF] = 0xAA;

    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
}
END_TEST

START_TEST(test_probe_empty_partition_is_raw)
{
    struct fs_volume vol;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
}
END_TEST

/* Garbage that is neither an image header nor a filesystem falls back to
 * raw, exactly as a build without DISK_FS would behave. */
START_TEST(test_probe_garbage_is_raw)
{
    struct fs_volume vol;

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x11);
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
}
END_TEST

START_TEST(test_probe_bad_partition_index)
{
    struct fs_volume vol;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 7), WOLFBOOT_FS_E_IO);
    ck_assert_int_eq(vol.type, FS_TYPE_NONE);
}
END_TEST

START_TEST(test_probe_null_volume)
{
    build_disk();
    ck_assert_int_eq(fs_mount(NULL, 0, 0), WOLFBOOT_FS_E_PARAM);
}
END_TEST

/* --- raw passthrough --- */

START_TEST(test_raw_open_size_is_partition_size)
{
    struct fs_volume vol;
    struct fs_file f;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 1), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 16), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), (unsigned int)P1_SIZE);
}
END_TEST

START_TEST(test_raw_read_matches_disk_part_read)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t via_fs[600];
    uint8_t via_disk[600];

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x5A);

    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 0), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_read(&f, 300, sizeof(via_fs), via_fs),
        (int)sizeof(via_fs));
    ck_assert_int_eq(disk_part_read(0, 0, 300, sizeof(via_disk), via_disk),
        (int)sizeof(via_disk));
    ck_assert_int_eq(memcmp(via_fs, via_disk, sizeof(via_fs)), 0);
}
END_TEST

START_TEST(test_raw_read_past_eof_returns_zero)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[16];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, P0_SIZE, sizeof(buf), buf), 0);
    ck_assert_int_eq(fs_read(&f, P0_SIZE + 1024, sizeof(buf), buf), 0);
}
END_TEST

START_TEST(test_raw_read_straddling_eof_is_clamped)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[64];

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x33);
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 0), WOLFBOOT_FS_OK);
    /* Ask for 64 bytes starting 16 before the end: 16 must come back. */
    ck_assert_int_eq(fs_read(&f, P0_SIZE - 16, sizeof(buf), buf), 16);
}
END_TEST

START_TEST(test_raw_read_zero_length)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[4];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, 0, buf), 0);
}
END_TEST

START_TEST(test_raw_label_never_matches)
{
    struct fs_volume vol;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_label_eq(&vol, "boot"), 0);
}
END_TEST

START_TEST(test_type_name)
{
    struct fs_volume vol;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_str_eq(fs_type_name(&vol), "raw");
    ck_assert_str_eq(fs_type_name(NULL), "none");
    vol.type = FS_TYPE_NONE;
    ck_assert_str_eq(fs_type_name(&vol), "none");
}
END_TEST

/* --- bounded disk access --- */

/* disk_part_read() silently clamps a read to the partition end and reports
 * the clamped count. fs_disk_read_exact() must refuse it rather than let a
 * truncation look like success. */
START_TEST(test_read_exact_refuses_partition_end_clamp)
{
    struct fs_volume vol;
    uint8_t buf[128];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);

    /* Straddles the end of the partition. disk_part_read would return 16. */
    ck_assert_int_eq(fs_disk_read_exact(&vol, P0_SIZE - 16, sizeof(buf), buf),
        WOLFBOOT_FS_E_RANGE);
    /* Entirely past the end. */
    ck_assert_int_eq(fs_disk_read_exact(&vol, P0_SIZE, 4, buf),
        WOLFBOOT_FS_E_RANGE);
    /* The last bytes of the partition are still readable. */
    ck_assert_int_eq(fs_disk_read_exact(&vol, P0_SIZE - 16, 16, buf),
        WOLFBOOT_FS_OK);
}
END_TEST

START_TEST(test_read_exact_io_error)
{
    struct fs_volume vol;
    uint8_t buf[16];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    mock_disk_read_fail_at = (int64_t)(P0_OFF + 512);
    ck_assert_int_eq(fs_disk_read_exact(&vol, 512, sizeof(buf), buf),
        WOLFBOOT_FS_E_IO);
}
END_TEST

START_TEST(test_read_exact_null_and_zero)
{
    struct fs_volume vol;
    uint8_t buf[4];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_disk_read_exact(&vol, 0, 0, buf), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_disk_read_exact(NULL, 0, 4, buf), WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_disk_read_exact(&vol, 0, 4, NULL), WOLFBOOT_FS_E_PARAM);
}
END_TEST

/* --- metadata cache --- */

START_TEST(test_meta_read_spans_cache_windows)
{
    struct fs_volume vol;
    uint8_t buf[1400];
    uint64_t i;

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x77);
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);

    /* Unaligned start, length far larger than one cache window. */
    ck_assert_int_eq(fs_meta_read(&vol, 300, sizeof(buf), buf),
        WOLFBOOT_FS_OK);
    for (i = 0; i < sizeof(buf); i++)
        ck_assert_uint_eq(buf[i], fake_disk[P0_OFF + 300 + i]);
}
END_TEST

START_TEST(test_meta_read_is_cached)
{
    struct fs_volume vol;
    uint8_t buf[4];
    unsigned int first;

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x22);
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);

    /* fs_mount() already warmed the window covering offset 0, so start
     * from a cold cache to observe the first miss. */
    fs_cache_invalidate();
    disk_read_count = 0;
    ck_assert_int_eq(fs_meta_read(&vol, 16, sizeof(buf), buf), WOLFBOOT_FS_OK);
    first = disk_read_count;
    ck_assert_uint_gt(first, 0);

    /* Same window: must not hit the media again. */
    ck_assert_int_eq(fs_meta_read(&vol, 20, sizeof(buf), buf), WOLFBOOT_FS_OK);
    ck_assert_uint_eq(disk_read_count, first);

    /* A different window must miss. */
    ck_assert_int_eq(fs_meta_read(&vol, WOLFBOOT_FS_CACHE_SIZE, sizeof(buf),
        buf), WOLFBOOT_FS_OK);
    ck_assert_uint_gt(disk_read_count, first);
}
END_TEST

/* The cache window is keyed by drive and partition as well as by offset.
 * Slot A and slot B can be different partitions, and an offset-only key
 * would hand one partition's reads the other partition's bytes. */
START_TEST(test_meta_cache_keyed_by_partition)
{
    struct fs_volume v0, v1;
    uint8_t b0[8], b1[8];

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x01);
    fill_pattern(P1_OFF, P1_SIZE, 0xF0);

    ck_assert_int_eq(fs_mount(&v0, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_meta_read(&v0, 0, sizeof(b0), b0), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_mount(&v1, 0, 1), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_meta_read(&v1, 0, sizeof(b1), b1), WOLFBOOT_FS_OK);

    ck_assert_int_eq(memcmp(b0, fake_disk + P0_OFF, sizeof(b0)), 0);
    ck_assert_int_eq(memcmp(b1, fake_disk + P1_OFF, sizeof(b1)), 0);
    ck_assert_int_ne(memcmp(b0, b1, sizeof(b0)), 0);
}
END_TEST

/* A window at the tail of the partition is shorter than the cache size.
 * The short window must still satisfy reads that fall inside it. */
START_TEST(test_meta_read_short_tail_window)
{
    struct fs_volume vol;
    uint8_t buf[8];

    build_disk();
    fill_pattern(P0_OFF, P0_SIZE, 0x44);
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_meta_read(&vol, P0_SIZE - sizeof(buf), sizeof(buf),
        buf), WOLFBOOT_FS_OK);
    ck_assert_int_eq(memcmp(buf, fake_disk + P0_OFF + P0_SIZE - sizeof(buf),
        sizeof(buf)), 0);
    ck_assert_int_eq(fs_meta_read(&vol, P0_SIZE - 4, 8, buf),
        WOLFBOOT_FS_E_RANGE);
}
END_TEST

START_TEST(test_meta_read_out_of_range)
{
    struct fs_volume vol;
    uint8_t buf[8];

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_meta_read(&vol, P0_SIZE, 8, buf),
        WOLFBOOT_FS_E_RANGE);
    ck_assert_int_eq(fs_meta_read(&vol, 0xFFFFFFFFFFFFFF00ULL, 8, buf),
        WOLFBOOT_FS_E_RANGE);
    ck_assert_int_eq(fs_meta_read(&vol, 0, 0, buf), WOLFBOOT_FS_OK);
}
END_TEST

/* --- partition accessors added for the filesystem layer --- */

START_TEST(test_disk_part_size)
{
    uint64_t sz = 0;

    build_disk();
    ck_assert_int_eq(disk_part_size(0, 0, &sz), 0);
    ck_assert_uint_eq((unsigned int)sz, (unsigned int)P0_SIZE);
    ck_assert_int_eq(disk_part_size(0, 1, &sz), 0);
    ck_assert_uint_eq((unsigned int)sz, (unsigned int)P1_SIZE);

    /* Past the end of the parsed table, and past MAX_PARTITIONS. */
    ck_assert_int_eq(disk_part_size(0, 2, &sz), -1);
    ck_assert_int_eq(disk_part_size(0, MAX_PARTITIONS, &sz), -1);
    ck_assert_int_eq(disk_part_size(0, -1, &sz), -1);
    ck_assert_int_eq(disk_part_size(MAX_DISKS, 0, &sz), -1);
    ck_assert_int_eq(disk_part_size(0, 0, NULL), -1);
}
END_TEST

START_TEST(test_disk_part_count)
{
    build_disk();
    ck_assert_int_eq(disk_part_count(0), 2);

    /* Out-of-range drives, and a drive that was never opened. */
    ck_assert_int_eq(disk_part_count(-1), -1);
    ck_assert_int_eq(disk_part_count(MAX_DISKS), -1);
    memset(Drives, 0, sizeof(Drives));
    ck_assert_int_eq(disk_part_count(0), -1);
}
END_TEST

/* --- little-endian accessors --- */

START_TEST(test_le_accessors)
{
    static const uint8_t b[8] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    ck_assert_uint_eq(fs_le16(b), 0x0201U);
    ck_assert_uint_eq(fs_le32(b), 0x04030201U);
}
END_TEST

/* --- path splitter --- */

static int split_all(const char *path, char out[][WOLFBOOT_FS_MAX_NAME + 1],
                     int max)
{
    const char *p = path;
    char name[WOLFBOOT_FS_MAX_NAME + 1];
    uint32_t len;
    int depth = 0;
    int n = 0;
    int ret;

    for (;;) {
        ret = fs_path_next(&p, name, &len, &depth);
        if (ret <= 0)
            return (ret < 0) ? ret : n;
        if (n >= max)
            return -100;
        strcpy(out[n], name);
        n++;
    }
}

START_TEST(test_path_split_basic)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];

    ck_assert_int_eq(split_all("/boot/image.itb", parts, 4), 2);
    ck_assert_str_eq(parts[0], "boot");
    ck_assert_str_eq(parts[1], "image.itb");
}
END_TEST

START_TEST(test_path_split_collapses_slashes)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];

    ck_assert_int_eq(split_all("//boot///image.itb//", parts, 4), 2);
    ck_assert_str_eq(parts[0], "boot");
    ck_assert_str_eq(parts[1], "image.itb");
}
END_TEST

/* A backslash is an ordinary filename character on both FAT and ext4.
 * Treating it as a separator would let a crafted name split in two. */
START_TEST(test_path_backslash_is_not_a_separator)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];

    ck_assert_int_eq(split_all("/a\\b", parts, 4), 1);
    ck_assert_str_eq(parts[0], "a\\b");
}
END_TEST

START_TEST(test_path_rejects_dot_and_dotdot)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];

    ck_assert_int_eq(split_all("/boot/./image", parts, 4),
        WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(split_all("/boot/../image", parts, 4),
        WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(split_all("/..", parts, 4), WOLFBOOT_FS_E_PARAM);
    /* Names that merely begin with a dot are fine. */
    ck_assert_int_eq(split_all("/.config", parts, 4), 1);
    ck_assert_int_eq(split_all("/...", parts, 4), 1);
}
END_TEST

START_TEST(test_path_rejects_excess_depth)
{
    char parts[16][WOLFBOOT_FS_MAX_NAME + 1];
    char path[WOLFBOOT_FS_MAX_PATH + 8];
    int i;

    path[0] = '\0';
    for (i = 0; i < WOLFBOOT_FS_MAX_PATH_DEPTH; i++)
        strcat(path, "/a");
    ck_assert_int_eq(split_all(path, parts, 16), WOLFBOOT_FS_MAX_PATH_DEPTH);

    strcat(path, "/a");
    ck_assert_int_eq(split_all(path, parts, 16), WOLFBOOT_FS_E_PARAM);
}
END_TEST

START_TEST(test_path_rejects_long_component)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];
    char path[WOLFBOOT_FS_MAX_NAME + 8];
    int i;

    path[0] = '/';
    for (i = 0; i < WOLFBOOT_FS_MAX_NAME; i++)
        path[1 + i] = 'x';
    path[1 + WOLFBOOT_FS_MAX_NAME] = '\0';
    ck_assert_int_eq(split_all(path, parts, 4), 1);

    path[1 + WOLFBOOT_FS_MAX_NAME] = 'x';
    path[2 + WOLFBOOT_FS_MAX_NAME] = '\0';
    ck_assert_int_eq(split_all(path, parts, 4), WOLFBOOT_FS_E_PARAM);
}
END_TEST

START_TEST(test_path_empty)
{
    char parts[4][WOLFBOOT_FS_MAX_NAME + 1];

    ck_assert_int_eq(split_all("/", parts, 4), 0);
    ck_assert_int_eq(split_all("", parts, 4), 0);
}
END_TEST

START_TEST(test_path_next_null_args)
{
    char name[WOLFBOOT_FS_MAX_NAME + 1];
    uint32_t len;
    int depth = 0;

    ck_assert_int_eq(fs_path_next(NULL, name, &len, &depth),
        WOLFBOOT_FS_E_PARAM);
}
END_TEST

/* --- fs_open argument validation --- */

/* With no backend compiled in, every partition probes as raw, so these
 * checks are exercised against a synthetic non-raw type. */
START_TEST(test_open_rejects_bad_type)
{
    struct fs_volume vol;
    struct fs_file f;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    vol.type = FS_TYPE_NONE;
    ck_assert_int_eq(fs_open(&vol, &f, "/boot/x", 1024),
        WOLFBOOT_FS_E_PARAM);
}
END_TEST

START_TEST(test_open_null_args)
{
    struct fs_volume vol;
    struct fs_file f;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(NULL, &f, "/x", 16), WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_open(&vol, NULL, "/x", 16), WOLFBOOT_FS_E_PARAM);
}
END_TEST

START_TEST(test_read_null_args)
{
    uint8_t buf[4];
    struct fs_volume vol;
    struct fs_file f;

    build_disk();
    ck_assert_int_eq(fs_mount(&vol, 0, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 0), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(NULL, 0, 4, buf), WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_read(&f, 0, 4, NULL), WOLFBOOT_FS_E_PARAM);
    ck_assert_uint_eq((unsigned int)fs_size(NULL), 0);
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-fs-probe");
    TCase *tc_probe = tcase_create("probe");
    TCase *tc_raw = tcase_create("raw");
    TCase *tc_io = tcase_create("bounded-io");
    TCase *tc_cache = tcase_create("cache");
    TCase *tc_path = tcase_create("path");
    TCase *tc_cov = tcase_create("fs-coverage");

    tcase_add_test(tc_probe, test_probe_wolfboot_magic_is_raw);
    tcase_add_test(tc_probe, test_probe_magic_wins_over_boot_signature);
    tcase_add_test(tc_probe, test_probe_empty_partition_is_raw);
    tcase_add_test(tc_probe, test_probe_garbage_is_raw);
    tcase_add_test(tc_probe, test_probe_bad_partition_index);
    tcase_add_test(tc_probe, test_probe_null_volume);
    suite_add_tcase(s, tc_probe);

    tcase_add_test(tc_raw, test_raw_open_size_is_partition_size);
    tcase_add_test(tc_raw, test_raw_read_matches_disk_part_read);
    tcase_add_test(tc_raw, test_raw_read_past_eof_returns_zero);
    tcase_add_test(tc_raw, test_raw_read_straddling_eof_is_clamped);
    tcase_add_test(tc_raw, test_raw_read_zero_length);
    tcase_add_test(tc_raw, test_raw_label_never_matches);
    tcase_add_test(tc_raw, test_type_name);
    suite_add_tcase(s, tc_raw);

    tcase_add_test(tc_io, test_read_exact_refuses_partition_end_clamp);
    tcase_add_test(tc_io, test_read_exact_io_error);
    tcase_add_test(tc_io, test_read_exact_null_and_zero);
    suite_add_tcase(s, tc_io);

    tcase_add_test(tc_cache, test_meta_read_spans_cache_windows);
    tcase_add_test(tc_cache, test_meta_read_is_cached);
    tcase_add_test(tc_cache, test_meta_cache_keyed_by_partition);
    tcase_add_test(tc_cache, test_meta_read_short_tail_window);
    tcase_add_test(tc_cache, test_meta_read_out_of_range);
    suite_add_tcase(s, tc_cache);

    tcase_add_test(tc_path, test_path_split_basic);
    tcase_add_test(tc_path, test_path_split_collapses_slashes);
    tcase_add_test(tc_path, test_path_backslash_is_not_a_separator);
    tcase_add_test(tc_path, test_path_rejects_dot_and_dotdot);
    tcase_add_test(tc_path, test_path_rejects_excess_depth);
    tcase_add_test(tc_path, test_path_rejects_long_component);
    tcase_add_test(tc_path, test_path_empty);
    tcase_add_test(tc_path, test_path_next_null_args);
    suite_add_tcase(s, tc_path);

    tcase_add_test(tc_cov, test_disk_part_size);
    tcase_add_test(tc_cov, test_disk_part_count);
    tcase_add_test(tc_cov, test_le_accessors);
    tcase_add_test(tc_cov, test_open_rejects_bad_type);
    tcase_add_test(tc_cov, test_open_null_args);
    tcase_add_test(tc_cov, test_read_null_args);
    suite_add_tcase(s, tc_cov);

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
