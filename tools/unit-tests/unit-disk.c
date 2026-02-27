/* unit-disk.c
 *
 * Unit tests for disk.c and gpt.c
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

/* Fake disk backing store for mock disk_read/disk_write */
#define FAKE_DISK_SIZE (128 * 1024) /* 128 KB */
static uint8_t fake_disk[FAKE_DISK_SIZE];

/* Set to a byte offset to make disk_read fail at that address. -1 = no fail */
static int64_t mock_disk_read_fail_at = -1;

/* Mock disk I/O — copies to/from fake_disk buffer */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    (void)drv;
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

/* GPT partition layout (LBA offsets) */
static const int PART0_OFF = 40;
static const int PART0_END = 100;
static const int PART1_OFF = 101;
static const int PART1_END = 200;

/* --- Helpers to build fake disk layouts --- */

/* Write a UTF-16LE string into a buffer (no BOM).
 * Uses memcpy to avoid unaligned-pointer warnings on packed structs. */
static void write_utf16(void *dst, const char *ascii, unsigned int max)
{
    uint16_t *p = (uint16_t *)dst;
    unsigned int i;
    uint16_t val;
    memset(dst, 0, max * sizeof(uint16_t));
    for (i = 0; i < max && ascii[i]; i++) {
        val = (uint16_t)ascii[i];
        memcpy(&p[i], &val, sizeof(val));
    }
}

/* Populate fake_disk with a valid protective-MBR + GPT header + N entries. */
static void build_gpt_disk(void)
{
    struct gpt_mbr_part_entry *mbr_entry;
    uint16_t *boot_sig;
    struct guid_ptable *gpt_hdr;
    struct gpt_part_entry *pe;

    memset(fake_disk, 0, FAKE_DISK_SIZE);

    /* --- Sector 0: MBR --- */
    mbr_entry = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START);
    mbr_entry->ptype = GPT_PTYPE_PROTECTIVE; /* 0xEE */
    mbr_entry->lba_first = 1;
    mbr_entry->lba_size = 0xFFFFFFFF;

    boot_sig = (uint16_t *)(fake_disk + GPT_MBR_BOOTSIG_OFFSET);
    *boot_sig = GPT_MBR_BOOTSIG_VALUE; /* 0xAA55 */

    /* --- Sector 1: GPT header --- */
    gpt_hdr = (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
    gpt_hdr->signature = GPT_SIGNATURE;
    gpt_hdr->revision = 0x00010000;
    gpt_hdr->hdr_size = 92;
    gpt_hdr->start_array = 2; /* partition entries start at LBA 2 */
    gpt_hdr->n_part = 2;
    gpt_hdr->array_sz = 128; /* bytes per entry */

    /* --- Sector 2: Partition entries --- */
    /* Entry 0: name "boot" */
    pe = (struct gpt_part_entry *)(fake_disk + 2 * GPT_SECTOR_SIZE);
    pe->type[0] = 0x0001020304050607ULL; /* non-zero type GUID */
    pe->type[1] = 0x08090A0B0C0D0E0FULL;
    pe->first = PART0_OFF;
    pe->last = PART0_END;
    write_utf16(pe->name, "boot", GPT_PART_NAME_SIZE);

    /* Entry 1: name "rootfs" */
    pe = (struct gpt_part_entry *)(fake_disk + 2 * GPT_SECTOR_SIZE + 128);
    pe->type[0] = 0x1011121314151617ULL;
    pe->type[1] = 0x18191A1B1C1D1E1FULL;
    pe->first = PART1_OFF;
    pe->last = PART1_END;
    write_utf16(pe->name, "rootfs", GPT_PART_NAME_SIZE);

    /* Fill partition data areas with known patterns for read tests.
     * GPT last LBA is inclusive, so partition spans
     * (PART_END - PART_OFF + 1) sectors. */
    memset(fake_disk + PART0_OFF * GPT_SECTOR_SIZE, 0xAA,
        (PART0_END - PART0_OFF + 1) * GPT_SECTOR_SIZE);
    memset(fake_disk + PART1_OFF * GPT_SECTOR_SIZE, 0xBB,
        (PART1_END - PART1_OFF + 1) * GPT_SECTOR_SIZE);
}

/* Populate fake_disk with MBR-only layout (no GPT protective entry).
 * Two partitions: part0 type=0x0C at LBA 2048 size 4096,
 *                 part1 type=0x83 at LBA 8192 size 8192
 */
static void build_mbr_disk(void)
{
    struct gpt_mbr_part_entry *pte;
    uint16_t *boot_sig;

    memset(fake_disk, 0, FAKE_DISK_SIZE);

    /* MBR entry 0: FAT32 LBA */
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START);
    pte->ptype = 0x0C; /* FAT32 LBA */
    pte->lba_first = 16;
    pte->lba_size = 32;

    /* MBR entry 1: Linux */
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START +
        sizeof(struct gpt_mbr_part_entry));
    pte->ptype = 0x83; /* Linux */
    pte->lba_first = 48;
    pte->lba_size = 64;

    boot_sig = (uint16_t *)(fake_disk + GPT_MBR_BOOTSIG_OFFSET);
    *boot_sig = GPT_MBR_BOOTSIG_VALUE;
}

/* ============================================================
 *  GPT test cases
 * ============================================================ */

START_TEST(test_gpt_check_mbr_protective)
{
    uint32_t lba = 0;

    build_gpt_disk();

    /* Valid protective MBR */
    ck_assert_int_eq(gpt_check_mbr_protective(fake_disk, &lba), 0);
    ck_assert_uint_eq(lba, 1);

    /* MBR without 0xEE entry should fail */
    build_mbr_disk();
    ck_assert_int_eq(gpt_check_mbr_protective(fake_disk, &lba), -1);

    /* NULL input */
    ck_assert_int_eq(gpt_check_mbr_protective(NULL, &lba), -1);
}
END_TEST

START_TEST(test_gpt_parse_header)
{
    struct guid_ptable hdr;

    build_gpt_disk();

    /* Valid GPT header at sector 1 */
    ck_assert_int_eq(
        gpt_parse_header(fake_disk + GPT_SECTOR_SIZE, &hdr), 0);
    ck_assert_uint_eq(hdr.signature, GPT_SIGNATURE);
    ck_assert_uint_eq(hdr.n_part, 2);
    ck_assert_uint_eq(hdr.start_array, 2);
    ck_assert_uint_eq(hdr.array_sz, 128);

    /* Corrupt signature in the real header */
    {
        struct guid_ptable *gpt_hdr =
            (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
        gpt_hdr->signature = 0;
        ck_assert_int_eq(
            gpt_parse_header(fake_disk + GPT_SECTOR_SIZE, &hdr), -1);
        gpt_hdr->signature = GPT_SIGNATURE;
    }

    /* NULL inputs */
    ck_assert_int_eq(gpt_parse_header(NULL, &hdr), -1);
    ck_assert_int_eq(
        gpt_parse_header(fake_disk + GPT_SECTOR_SIZE, NULL), -1);
}
END_TEST

START_TEST(test_gpt_parse_partition)
{
    struct gpt_part_info info;

    build_gpt_disk();

    /* Valid entry 0 */
    ck_assert_int_eq(gpt_parse_partition(
        fake_disk + 2 * GPT_SECTOR_SIZE, 128, &info), 0);
    ck_assert_uint_eq(info.start, PART0_OFF * GPT_SECTOR_SIZE);
    ck_assert_uint_eq(info.end, (PART0_END + 1) * GPT_SECTOR_SIZE - 1);

    /* Valid entry 1 */
    ck_assert_int_eq(gpt_parse_partition(
        fake_disk + 2 * GPT_SECTOR_SIZE + 128, 128, &info), 0);
    ck_assert_uint_eq(info.start, PART1_OFF * GPT_SECTOR_SIZE);
    ck_assert_uint_eq(info.end, (PART1_END + 1) * GPT_SECTOR_SIZE - 1);

    /* All-zero type GUID → invalid */
    {
        uint8_t empty_entry[128];
        memset(empty_entry, 0, sizeof(empty_entry));
        ck_assert_int_eq(gpt_parse_partition(empty_entry, 128, &info), -1);
    }

    /* Entry size too small */
    ck_assert_int_eq(gpt_parse_partition(
        fake_disk + 2 * GPT_SECTOR_SIZE, 10, &info), -1);

    /* NULL inputs */
    ck_assert_int_eq(gpt_parse_partition(NULL, 128, &info), -1);
    ck_assert_int_eq(gpt_parse_partition(
        fake_disk + 2 * GPT_SECTOR_SIZE, 128, NULL), -1);
}
END_TEST

START_TEST(test_gpt_part_name_eq)
{
    uint16_t name[GPT_PART_NAME_SIZE];

    /* Simple match */
    write_utf16(name, "boot", GPT_PART_NAME_SIZE);
    ck_assert_int_eq(gpt_part_name_eq(name, "boot"), 1);

    /* Mismatch */
    ck_assert_int_eq(gpt_part_name_eq(name, "rootfs"), 0);

    /* BOM prefix */
    name[0] = 0xFEFF;
    write_utf16(name + 1, "efi", GPT_PART_NAME_SIZE - 1);
    ck_assert_int_eq(gpt_part_name_eq(name, "efi"), 1);

    /* NULL inputs */
    ck_assert_int_eq(gpt_part_name_eq(NULL, "boot"), 0);
    ck_assert_int_eq(gpt_part_name_eq(name, NULL), 0);
}
END_TEST

/* ============================================================
 *  Disk test cases
 * ============================================================ */

START_TEST(test_disk_open_gpt)
{
    int n;

    build_gpt_disk();

    n = disk_open(0);
    ck_assert_int_eq(n, 2);
}
END_TEST

START_TEST(test_disk_open_mbr)
{
    int n;

    build_mbr_disk();

    n = disk_open(0);
    ck_assert_int_eq(n, 2);
}
END_TEST

START_TEST(test_disk_part_read)
{
    uint8_t buf[GPT_SECTOR_SIZE];
    int ret;
    unsigned int i;

    build_gpt_disk();

    ck_assert_int_eq(disk_open(0), 2);

    /* Read first sector of partition 0 — should be 0xAA pattern */
    ret = disk_part_read(0, 0, 0, GPT_SECTOR_SIZE, buf);
    ck_assert_int_gt(ret, 0);
    for (i = 0; i < GPT_SECTOR_SIZE; i++)
        ck_assert_uint_eq(buf[i], 0xAA);

    /* Read first sector of partition 1 — should be 0xBB pattern */
    ret = disk_part_read(0, 1, 0, GPT_SECTOR_SIZE, buf);
    ck_assert_int_gt(ret, 0);
    for (i = 0; i < GPT_SECTOR_SIZE; i++)
        ck_assert_uint_eq(buf[i], 0xBB);

    /* Invalid partition */
    ck_assert_int_eq(disk_part_read(0, 99, 0, GPT_SECTOR_SIZE, buf), -1);
}
END_TEST

/* ============================================================
 *  Regression tests: disk.c unsigned underflow in bounds check
 * ============================================================ */

START_TEST(test_disk_part_rw_offset_past_end)
{
    uint8_t buf[GPT_SECTOR_SIZE];
    int ret;

    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    /* Partition 0 spans PART0_OFF to PART0_END LBAs.
     * Offset 40000 exceeds partition size, both read and write
     * should return -1. */
    ret = disk_part_read(0, 0, 40000, GPT_SECTOR_SIZE, buf);
    ck_assert_int_eq(ret, -1);

    memset(buf, 0x55, sizeof(buf));
    ret = disk_part_write(0, 0, 40000, GPT_SECTOR_SIZE, buf);
    ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(test_disk_part_rw_size_past_end)
{
    uint8_t buf[2 * GPT_SECTOR_SIZE];
    int ret;
    uint64_t off = 60 * GPT_SECTOR_SIZE;  /* within partition (last sector) */
    uint64_t sz = 2 * GPT_SECTOR_SIZE;    /* 1024: extends past end */

    /* Partition 0: start = PART0_OFF * 512 = 20480,
     *              end   = (PART0_END + 1) * 512 - 1 = 51711
     * p->start + off = 51200, which is < p->end (51711)
     * Remaining = 51711 - 51200 + 1 = 512, so len is clamped to 512 */
    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    ret = disk_part_read(0, 0, off, sz, buf);
    ck_assert_int_eq(ret, 512);

    memset(buf, 0x55, sizeof(buf));
    ret = disk_part_write(0, 0, off, sz, buf);
    ck_assert_int_eq(ret, 512);
}
END_TEST

START_TEST(test_disk_open_mbr_max_partitions)
{
    /* If n_parts is already at MAX_PARTITIONS, disk_open_mbr must not
     * add more entries (would be an OOB write). */
    build_mbr_disk();

    /* Pre-fill drive 0 as if it already has MAX_PARTITIONS */
    Drives[0].n_parts = MAX_PARTITIONS;
    Drives[0].is_open = 1;
    Drives[0].drv = 0;

    /* Call disk_open_mbr directly — it should not add any entries */
    disk_open_mbr(&Drives[0], fake_disk);
    ck_assert_int_eq(Drives[0].n_parts, MAX_PARTITIONS);
}
END_TEST

START_TEST(test_gpt_part_name_eq_bom_boundary)
{
    uint16_t name[GPT_PART_NAME_SIZE];
    char label35[36]; /* 35 chars + NUL */
    char label36[37]; /* 36 chars + NUL */
    unsigned int i;

    /* Build a 35-char label: fills indices 1-35 after BOM at index 0 */
    for (i = 0; i < 35; i++)
        label35[i] = 'A' + (i % 26);
    label35[35] = '\0';

    memset(name, 0, sizeof(name));
    name[0] = 0xFEFF; /* BOM */
    for (i = 0; i < 35; i++)
        name[i + 1] = (uint16_t)label35[i];

    /* Should match — BOM + 35 chars exactly fills 36 slots */
    ck_assert_int_eq(gpt_part_name_eq(name, label35), 1);

    /* Build a 36-char label: BOM + 36 would read index 37 = OOB */
    for (i = 0; i < 36; i++)
        label36[i] = 'A' + (i % 26);
    label36[36] = '\0';

    /* Should return 0 — label too long for name array with BOM */
    ck_assert_int_eq(gpt_part_name_eq(name, label36), 0);
}
END_TEST

START_TEST(test_disk_find_partition_by_label)
{
    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    /* Find existing partitions */
    ck_assert_int_eq(disk_find_partition_by_label(0, "boot"), 0);
    ck_assert_int_eq(disk_find_partition_by_label(0, "rootfs"), 1);

    /* Non-existent label */
    ck_assert_int_eq(disk_find_partition_by_label(0, "nonexistent"), -1);

    /* Invalid drive */
    ck_assert_int_eq(disk_find_partition_by_label(99, "boot"), -1);

    /* Unopened drive */
    ck_assert_int_eq(disk_find_partition_by_label(1, "boot"), -1);
}
END_TEST

START_TEST(test_gpt_partition_end_inclusive)
{
    /* GPT spec: last LBA is inclusive. A partition with first=10, last=20
     * spans 11 sectors. End byte = (20+1)*512 - 1. */
    uint8_t entry[128];
    struct gpt_part_entry *pe = (struct gpt_part_entry *)entry;
    struct gpt_part_info info;

    memset(entry, 0, sizeof(entry));
    pe->type[0] = 0x0001020304050607ULL;
    pe->type[1] = 0x08090A0B0C0D0E0FULL;
    pe->first = 10;
    pe->last = 20;

    ck_assert_int_eq(gpt_parse_partition(entry, 128, &info), 0);
    ck_assert_uint_eq(info.start, 10 * 512);
    ck_assert_uint_eq(info.end, (21 * 512) - 1);
    /* Partition size = end - start + 1 = 11 * 512 */
    ck_assert_uint_eq(info.end - info.start + 1, 11 * 512);
}
END_TEST

START_TEST(test_disk_open_failure_clears_is_open)
{
    /* If GPT header parse fails, is_open must be reset to 0. */
    struct guid_ptable *gpt_hdr;

    build_gpt_disk();

    /* Corrupt the GPT header signature */
    gpt_hdr = (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
    gpt_hdr->signature = 0xDEADBEEF;

    ck_assert_int_eq(disk_open(0), -1);
    ck_assert_int_eq(Drives[0].is_open, 0);
}
END_TEST

START_TEST(test_gpt_parse_partition_last_zero)
{
    /* If first=0, last=0 with non-zero type GUID, (last+1)*512 - 1 would
     * be 511 but LBA 0 is the protective MBR — must reject. */
    uint8_t entry[128];
    struct gpt_part_entry *pe = (struct gpt_part_entry *)entry;
    struct gpt_part_info info;

    memset(entry, 0, sizeof(entry));
    pe->type[0] = 0x0001020304050607ULL;
    pe->type[1] = 0x08090A0B0C0D0E0FULL;
    pe->first = 0;
    pe->last = 0;

    ck_assert_int_eq(gpt_parse_partition(entry, 128, &info), -1);
}
END_TEST

/* ============================================================
 *  Coverage tests: disk.c error paths and boundary conditions
 * ============================================================ */

START_TEST(test_disk_open_invalid_drive)
{
    ck_assert_int_eq(disk_open(-1), -1);
    ck_assert_int_eq(disk_open(MAX_DISKS), -1);
}
END_TEST

START_TEST(test_disk_open_mbr_bad_bootsig)
{
    /* MBR disk without valid 0xAA55 boot signature and no protective 0xEE.
     * Falls through GPT check, then fails on boot_sig validation. */
    struct gpt_mbr_part_entry *pte;

    memset(fake_disk, 0, FAKE_DISK_SIZE);
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START);
    pte->ptype = 0x0C;
    pte->lba_first = 16;
    pte->lba_size = 32;
    /* No boot signature set — 0x0000 instead of 0xAA55 */

    ck_assert_int_eq(disk_open(0), -1);
    ck_assert_int_eq(Drives[0].is_open, 0);
}
END_TEST

START_TEST(test_disk_open_gpt_excess_partitions)
{
    /* GPT header claims more partitions than MAX_PARTITIONS. disk_open
     * must cap n_parts to MAX_PARTITIONS. */
    struct guid_ptable *gpt_hdr;

    build_gpt_disk();

    gpt_hdr = (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
    gpt_hdr->n_part = MAX_PARTITIONS + 10;

    /* Only 2 actual entries on disk so loop will break after parsing them,
     * but the capping branch is exercised. */
    ck_assert_int_eq(disk_open(0), 2);
    ck_assert_int_le(Drives[0].n_parts, MAX_PARTITIONS);
}
END_TEST

START_TEST(test_disk_open_gpt_large_array_sz)
{
    /* GPT header with array_sz larger than GPT_PART_ENTRY_SIZE (256).
     * Loop must break immediately without reading entries. */
    struct guid_ptable *gpt_hdr;

    build_gpt_disk();

    gpt_hdr = (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
    gpt_hdr->array_sz = GPT_PART_ENTRY_SIZE + 1; /* 257 > 256 */

    ck_assert_int_eq(disk_open(0), 0); /* 0 partitions found */
}
END_TEST

START_TEST(test_disk_open_gpt_empty_entry_mid_table)
{
    /* GPT header says 3 partitions but entry[1] has zeroed type GUID.
     * gpt_parse_partition returns -1 → loop breaks, only 1 partition found. */
    struct guid_ptable *gpt_hdr;
    struct gpt_part_entry *pe;

    build_gpt_disk();

    gpt_hdr = (struct guid_ptable *)(fake_disk + GPT_SECTOR_SIZE);
    gpt_hdr->n_part = 3;

    /* Zero out entry 1's type GUID */
    pe = (struct gpt_part_entry *)(fake_disk + 2 * GPT_SECTOR_SIZE + 128);
    pe->type[0] = 0;
    pe->type[1] = 0;

    ck_assert_int_eq(disk_open(0), 1);
}
END_TEST

START_TEST(test_disk_open_mbr_zero_lba_entry)
{
    /* MBR entry with lba_first=0 must be skipped. */
    struct gpt_mbr_part_entry *pte;
    uint16_t *boot_sig;

    memset(fake_disk, 0, FAKE_DISK_SIZE);

    /* Entry 0: valid */
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START);
    pte->ptype = 0x0C;
    pte->lba_first = 16;
    pte->lba_size = 32;

    /* Entry 1: lba_first=0, should be skipped */
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START +
        sizeof(struct gpt_mbr_part_entry));
    pte->ptype = 0x83;
    pte->lba_first = 0;
    pte->lba_size = 64;

    /* Entry 2: lba_size=0, should also be skipped */
    pte = (struct gpt_mbr_part_entry *)(fake_disk + GPT_MBR_ENTRY_START +
        2 * sizeof(struct gpt_mbr_part_entry));
    pte->ptype = 0x83;
    pte->lba_first = 48;
    pte->lba_size = 0;

    boot_sig = (uint16_t *)(fake_disk + GPT_MBR_BOOTSIG_OFFSET);
    *boot_sig = GPT_MBR_BOOTSIG_VALUE;

    ck_assert_int_eq(disk_open(0), 1); /* only entry 0 counted */
}
END_TEST

START_TEST(test_open_part_invalid_drive)
{
    uint8_t buf[GPT_SECTOR_SIZE];

    /* open_part rejects drv < 0 and drv >= MAX_DISKS */
    ck_assert_int_eq(disk_part_read(-1, 0, 0, GPT_SECTOR_SIZE, buf), -1);
    ck_assert_int_eq(disk_part_read(MAX_DISKS, 0, 0, GPT_SECTOR_SIZE, buf), -1);
}
END_TEST

START_TEST(test_open_part_drive_not_open)
{
    uint8_t buf[GPT_SECTOR_SIZE];

    /* Drive 2 was never opened */
    memset(&Drives[2], 0, sizeof(Drives[2]));
    ck_assert_int_eq(disk_part_read(2, 0, 0, GPT_SECTOR_SIZE, buf), -1);
}
END_TEST

START_TEST(test_open_part_part_beyond_nparts)
{
    uint8_t buf[GPT_SECTOR_SIZE];

    /* Open drive 0 with 2 partitions, then request partition 5.
     * 5 < MAX_PARTITIONS so passes range check, but 5 >= n_parts. */
    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    ck_assert_int_eq(disk_part_read(0, 5, 0, GPT_SECTOR_SIZE, buf), -1);
}
END_TEST

START_TEST(test_disk_part_write_invalid_partition)
{
    uint8_t buf[GPT_SECTOR_SIZE];

    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    /* Part 5 < MAX_PARTITIONS but >= n_parts → open_part returns NULL */
    memset(buf, 0x55, sizeof(buf));
    ck_assert_int_eq(disk_part_write(0, 5, 0, GPT_SECTOR_SIZE, buf), -1);
}
END_TEST

START_TEST(test_disk_part_rw_sz_clamped_to_max)
{
    uint8_t buf[GPT_SECTOR_SIZE];
    uint64_t off = 60 * GPT_SECTOR_SIZE; /* near partition end */

    build_gpt_disk();
    ck_assert_int_eq(disk_open(0), 2);

    /* Request with sz > DISK_IO_MAX_SIZE triggers the clamping branch.
     * Use offset near end so remaining bytes (512) fit in buf. */
    ck_assert_int_eq(disk_part_read(0, 0, off,
        (uint64_t)DISK_IO_MAX_SIZE + 1, buf), 512);

    memset(buf, 0x55, sizeof(buf));
    ck_assert_int_eq(disk_part_write(0, 0, off,
        (uint64_t)DISK_IO_MAX_SIZE + 1, buf), 512);
}
END_TEST

START_TEST(test_disk_open_mbr_read_failure)
{
    build_mbr_disk();

    /* Fail reading sector 0 (MBR) */
    mock_disk_read_fail_at = 0;
    ck_assert_int_eq(disk_open(0), -1);
    mock_disk_read_fail_at = -1;
}
END_TEST

START_TEST(test_disk_open_gpt_header_read_failure)
{
    build_gpt_disk();

    /* Fail reading sector 1 (GPT header) */
    mock_disk_read_fail_at = GPT_SECTOR_SIZE;
    ck_assert_int_eq(disk_open(0), -1);
    ck_assert_int_eq(Drives[0].is_open, 0);
    mock_disk_read_fail_at = -1;
}
END_TEST

START_TEST(test_disk_open_gpt_entry_read_failure)
{
    build_gpt_disk();

    /* Fail reading first partition entry at sector 2 */
    mock_disk_read_fail_at = 2 * GPT_SECTOR_SIZE;
    ck_assert_int_eq(disk_open(0), -1);
    ck_assert_int_eq(Drives[0].is_open, 0);
    mock_disk_read_fail_at = -1;
}
END_TEST

START_TEST(test_gpt_check_mbr_bad_bootsig)
{
    uint32_t lba = 0;
    uint8_t sector[GPT_SECTOR_SIZE];

    /* Valid MBR structure but corrupt boot signature */
    memset(sector, 0, sizeof(sector));
    {
        struct gpt_mbr_part_entry *pte =
            (struct gpt_mbr_part_entry *)(sector + GPT_MBR_ENTRY_START);
        pte->ptype = GPT_PTYPE_PROTECTIVE;
        pte->lba_first = 1;
        pte->lba_size = 0xFFFFFFFF;
    }
    /* boot sig left as 0x0000 — not 0xAA55 */

    ck_assert_int_eq(gpt_check_mbr_protective(sector, &lba), -1);
}
END_TEST

START_TEST(test_gpt_parse_partition_first_gt_last)
{
    uint8_t entry[128];
    struct gpt_part_entry *pe = (struct gpt_part_entry *)entry;
    struct gpt_part_info info;

    memset(entry, 0, sizeof(entry));
    pe->type[0] = 0x0001020304050607ULL;
    pe->type[1] = 0x08090A0B0C0D0E0FULL;
    pe->first = 100;
    pe->last = 50; /* first > last → invalid */

    ck_assert_int_eq(gpt_parse_partition(entry, 128, &info), -1);
}
END_TEST

START_TEST(test_gpt_part_name_eq_label_too_long)
{
    uint16_t name[GPT_PART_NAME_SIZE];
    char label37[38]; /* 37 chars + NUL — exceeds GPT_PART_NAME_SIZE (36) */
    unsigned int i;

    memset(name, 0, sizeof(name));
    for (i = 0; i < 37; i++)
        label37[i] = 'A' + (i % 26);
    label37[37] = '\0';

    ck_assert_int_eq(gpt_part_name_eq(name, label37), 0);
}
END_TEST

START_TEST(test_gpt_part_name_eq_not_null_terminated)
{
    uint16_t name[GPT_PART_NAME_SIZE];

    /* "boot" followed by non-zero garbage — should not match "boot" */
    write_utf16(name, "boot", GPT_PART_NAME_SIZE);
    name[4] = 0x0041; /* 'A' after "boot" instead of 0x0000 */

    ck_assert_int_eq(gpt_part_name_eq(name, "boot"), 0);
}
END_TEST

/* ============================================================
 *  Suite setup
 * ============================================================ */

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-disk");
    TCase *tc_gpt = tcase_create("gpt");
    TCase *tc_disk = tcase_create("disk");
    TCase *tc_disk_bugs = tcase_create("disk-bugs");

    tcase_add_test(tc_gpt, test_gpt_check_mbr_protective);
    tcase_add_test(tc_gpt, test_gpt_parse_header);
    tcase_add_test(tc_gpt, test_gpt_parse_partition);
    tcase_add_test(tc_gpt, test_gpt_part_name_eq);
    tcase_add_test(tc_gpt, test_gpt_part_name_eq_bom_boundary);
    suite_add_tcase(s, tc_gpt);

    tcase_add_test(tc_disk, test_disk_open_gpt);
    tcase_add_test(tc_disk, test_disk_open_mbr);
    tcase_add_test(tc_disk, test_disk_part_read);
    tcase_add_test(tc_disk, test_disk_find_partition_by_label);
    suite_add_tcase(s, tc_disk);

    tcase_add_test(tc_disk_bugs, test_disk_part_rw_offset_past_end);
    tcase_add_test(tc_disk_bugs, test_disk_part_rw_size_past_end);
    tcase_add_test(tc_disk_bugs, test_disk_open_mbr_max_partitions);
    tcase_add_test(tc_disk_bugs, test_gpt_partition_end_inclusive);
    tcase_add_test(tc_disk_bugs, test_disk_open_failure_clears_is_open);
    tcase_add_test(tc_disk_bugs, test_gpt_parse_partition_last_zero);
    suite_add_tcase(s, tc_disk_bugs);

    TCase *tc_cov = tcase_create("disk-coverage");
    tcase_add_test(tc_cov, test_disk_open_invalid_drive);
    tcase_add_test(tc_cov, test_disk_open_mbr_bad_bootsig);
    tcase_add_test(tc_cov, test_disk_open_gpt_excess_partitions);
    tcase_add_test(tc_cov, test_disk_open_gpt_large_array_sz);
    tcase_add_test(tc_cov, test_disk_open_gpt_empty_entry_mid_table);
    tcase_add_test(tc_cov, test_disk_open_mbr_zero_lba_entry);
    tcase_add_test(tc_cov, test_open_part_invalid_drive);
    tcase_add_test(tc_cov, test_open_part_drive_not_open);
    tcase_add_test(tc_cov, test_open_part_part_beyond_nparts);
    tcase_add_test(tc_cov, test_disk_part_write_invalid_partition);
    tcase_add_test(tc_cov, test_disk_part_rw_sz_clamped_to_max);
    tcase_add_test(tc_cov, test_disk_open_mbr_read_failure);
    tcase_add_test(tc_cov, test_disk_open_gpt_header_read_failure);
    tcase_add_test(tc_cov, test_disk_open_gpt_entry_read_failure);
    tcase_add_test(tc_cov, test_gpt_check_mbr_bad_bootsig);
    tcase_add_test(tc_cov, test_gpt_parse_partition_first_gt_last);
    tcase_add_test(tc_cov, test_gpt_part_name_eq_label_too_long);
    tcase_add_test(tc_cov, test_gpt_part_name_eq_not_null_terminated);
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
