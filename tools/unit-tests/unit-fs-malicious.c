/* unit-fs-malicious.c
 *
 * Hostile-input tests for the read-only filesystem layer.
 *
 * Everything the FAT32 and ext4 parsers read is attacker-controlled and is
 * parsed BEFORE the image signature is verified, so each validation rule
 * and each loop bound in those parsers gets a test here that tries to
 * break it.
 *
 * All metadata is hand-built in C, so this suite needs no external tools
 * and is never skipped -- unlike the mkfs-based interop suites. Run it
 * under ASAN=1 as well: an out-of-bounds metadata read shows up nowhere
 * else.
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
#include "fat32.c"
#include "ext4.c"

/* ---------------------------------------------------------------------
 * Synthetic FAT32 volume
 *
 * A conformant FAT32 volume must have at least 65525 data clusters, so
 * even the smallest legal image is around 32 MB. The backing store is
 * allocated once and only the regions a test actually touches (boot
 * sector, FAT, a few directory clusters) are ever written; everything else
 * stays zero.
 * --------------------------------------------------------------------- */

#define FS_SEC        512U
#define FS_RSVD        32U
#define FS_NFATS        1U
#define FS_FATSZ32    512U     /* sectors; 262144 B, enough for 65527 slots */
#define FS_CLUSTERS 65525U     /* exactly the FAT32 minimum */
#define FS_TOTSEC   (FS_RSVD + (FS_NFATS * FS_FATSZ32) + FS_CLUSTERS)

#define FS_PART_SZ  ((uint64_t)FS_TOTSEC * FS_SEC)
#define FS_FAT_OFF  ((uint64_t)FS_RSVD * FS_SEC)
#define FS_FAT_LEN  ((uint64_t)FS_NFATS * FS_FATSZ32 * FS_SEC)
#define FS_DATA_OFF (FS_FAT_OFF + FS_FAT_LEN)
#define CLUS_OFF(c) (FS_DATA_OFF + (((uint64_t)(c) - 2U) * FS_SEC))
#define ENTS_PER_CLUS (FS_SEC / 32U)

#define ROOT_CLUS 2U

static uint8_t *fake_disk = NULL;
static unsigned int disk_read_count = 0;
static uint64_t part_size = FS_PART_SZ;

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    (void)drv;
    disk_read_count++;
    if ((start + count) > FS_PART_SZ)
        return -1;
    memcpy(buf, fake_disk + start, count);
    return 0;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

int disk_init(int drv) { (void)drv; return 0; }
void disk_close(int drv) { (void)drv; }

static void put16(uint64_t off, uint16_t v)
{
    fake_disk[off] = (uint8_t)(v & 0xFF);
    fake_disk[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put32(uint64_t off, uint32_t v)
{
    put16(off, (uint16_t)(v & 0xFFFF));
    put16(off + 2, (uint16_t)((v >> 16) & 0xFFFF));
}

/* Write a boot sector describing the geometry above. Tests then corrupt
 * exactly one field to show that the corresponding check catches it. */
static void build_bs(void)
{
    memset(fake_disk, 0, FS_SEC);
    fake_disk[0] = 0xEB;
    fake_disk[1] = 0x58;
    fake_disk[2] = 0x90;
    memcpy(fake_disk + 3, "MSWIN4.1", 8);
    put16(FAT_BPB_BYTS_PER_SEC, (uint16_t)FS_SEC);
    fake_disk[FAT_BPB_SEC_PER_CLUS] = 1;
    put16(FAT_BPB_RSVD_SEC_CNT, (uint16_t)FS_RSVD);
    fake_disk[FAT_BPB_NUM_FATS] = FS_NFATS;
    put16(FAT_BPB_ROOT_ENT_CNT, 0);
    put16(FAT_BPB_TOT_SEC16, 0);
    fake_disk[FAT_BPB_MEDIA] = 0xF8;
    put16(FAT_BPB_FAT_SZ16, 0);
    put32(FAT_BPB_TOT_SEC32, FS_TOTSEC);
    put32(FAT_BPB_FAT_SZ32, FS_FATSZ32);
    put16(FAT_BPB_EXT_FLAGS, 0);
    put16(FAT_BPB_FS_VER, 0);
    put32(FAT_BPB_ROOT_CLUS, ROOT_CLUS);
    memcpy(fake_disk + FAT_BS_VOL_LAB, "WBMAL      ", 11);
    put16(FAT_BOOT_SIG_OFF, 0xAA55);
}

static void fat_set(uint32_t clus, uint32_t val)
{
    put32(FS_FAT_OFF + ((uint64_t)clus * 4U), val);
}

/* Reset to a pristine volume: valid boot sector, empty FAT, empty root. */
static void build_fs(void)
{
    if (fake_disk == NULL) {
        fake_disk = (uint8_t *)calloc(1, (size_t)FS_PART_SZ);
        ck_assert(fake_disk != NULL);
    }
    build_bs();
    memset(fake_disk + FS_FAT_OFF, 0, (size_t)FS_FAT_LEN);
    /* Media descriptor and end-of-chain markers in FAT slots 0 and 1. */
    fat_set(0, 0x0FFFFFF8U);
    fat_set(1, 0x0FFFFFFFU);
    /* Root directory: one cluster, end of chain, and empty. */
    fat_set(ROOT_CLUS, 0x0FFFFFFFU);
    memset(fake_disk + CLUS_OFF(ROOT_CLUS), 0, FS_SEC);

    part_size = FS_PART_SZ;
    memset(Drives, 0, sizeof(Drives));
    Drives[0].drv = 0;
    Drives[0].is_open = 1;
    Drives[0].n_parts = 1;
    Drives[0].part[0].drv = 0;
    Drives[0].part[0].start = 0;
    Drives[0].part[0].end = part_size - 1;
    fs_cache_invalidate();
    disk_read_count = 0;
}

/* Write a short-name directory entry. name11 must be the raw, space
 * padded 11-byte on-media form, e.g. "IMAGE   ITB". */
static void put_sfn(uint64_t off, const char *name11, uint8_t attr,
                    uint32_t clus, uint32_t size)
{
    memset(fake_disk + off, 0, 32);
    memcpy(fake_disk + off, name11, 11);
    fake_disk[off + FAT_DIR_ATTR] = attr;
    put16(off + FAT_DIR_FST_CLUS_HI, (uint16_t)(clus >> 16));
    put16(off + FAT_DIR_FST_CLUS_LO, (uint16_t)(clus & 0xFFFF));
    put32(off + FAT_DIR_FILE_SIZE, size);
}

/* Write one long filename entry holding up to 13 ASCII characters. */
static void put_lfn(uint64_t off, uint8_t ord, int last, uint8_t cksum,
                    const char *chunk)
{
    static const uint8_t chr_off[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    int i;
    int done = 0;

    memset(fake_disk + off, 0, 32);
    fake_disk[off] = (uint8_t)(ord | (last ? 0x40U : 0U));
    fake_disk[off + FAT_DIR_ATTR] = FAT_ATTR_LFN;
    fake_disk[off + FAT_LFN_CKSUM_OFF] = cksum;
    for (i = 0; i < 13; i++) {
        if (done != 0) {
            put16(off + chr_off[i], 0xFFFF);
        }
        else if (chunk[i] == '\0') {
            put16(off + chr_off[i], 0x0000);
            done = 1;
        }
        else {
            put16(off + chr_off[i], (uint16_t)(uint8_t)chunk[i]);
        }
    }
}

static uint8_t sfn_cksum(const char *name11)
{
    return fat32_sfn_checksum((const uint8_t *)name11);
}

/* A single-cluster file named IMAGE.ITB in the root, of the given size.
 * Used by the tests that deliberately make the chain disagree with the
 * declared size; use add_chained_file() when a well-formed file is
 * wanted. */
static void add_simple_file(uint32_t clus, uint32_t size)
{
    put_sfn(CLUS_OFF(ROOT_CLUS), "IMAGE   ITB", 0x20, clus, size);
    fat_set(clus, 0x0FFFFFFFU);
}

/* A well-formed IMAGE.ITB whose chain holds exactly as many clusters as
 * its length needs, ending in an end-of-chain marker. */
static void add_chained_file(uint32_t first, uint32_t size)
{
    uint32_t need = (size + FS_SEC - 1U) / FS_SEC;
    uint32_t i;

    if (need == 0U)
        need = 1U;
    put_sfn(CLUS_OFF(ROOT_CLUS), "IMAGE   ITB", 0x20, first, size);
    for (i = 0; i < (need - 1U); i++)
        fat_set(first + i, first + i + 1U);
    fat_set(first + need - 1U, 0x0FFFFFFFU);
}

static int mount_result(struct fs_volume *vol)
{
    fs_cache_invalidate();
    return fs_mount(vol, 0, 0);
}

/* ---------------------------------------------------------------------
 * Boot sector / geometry validation
 * --------------------------------------------------------------------- */

/* The pristine volume must mount, or every rejection test below would
 * pass for the wrong reason. */
START_TEST(test_fat_baseline_mounts)
{
    struct fs_volume vol;

    build_fs();
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_FAT32);
    ck_assert_uint_eq(vol.u.fat.clus_count, FS_CLUSTERS);
}
END_TEST

/* Fields shared by every FAT variant. A bad value means "this is not FAT",
 * so fs_mount() must fall back to the raw path rather than report an
 * error -- that is what keeps a non-filesystem partition behaving exactly
 * as it does in a build without DISK_FS. */
START_TEST(test_fat_stage1_rejects_fall_back_to_raw)
{
    struct fs_volume vol;

    build_fs();
    put16(FAT_BOOT_SIG_OFF, 0x1234);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    fake_disk[FAT_BPB_MEDIA] = 0x00;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    put16(FAT_BPB_BYTS_PER_SEC, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    put16(FAT_BPB_BYTS_PER_SEC, 3);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    put16(FAT_BPB_BYTS_PER_SEC, 8192);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    fake_disk[FAT_BPB_NUM_FATS] = 0;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    fake_disk[FAT_BPB_NUM_FATS] = 3;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);

    build_fs();
    put16(FAT_BPB_RSVD_SEC_CNT, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
}
END_TEST

/* The partition layer works in 512-byte sectors throughout, so a 4Kn
 * volume must be refused outright rather than misread. */
START_TEST(test_fat_rejects_non_512_sectors)
{
    struct fs_volume vol;

    build_fs();
    put16(FAT_BPB_BYTS_PER_SEC, 1024);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);

    build_fs();
    put16(FAT_BPB_BYTS_PER_SEC, 4096);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* A FAT12/FAT16 volume must never fall through to the raw path, where its
 * boot sector would be parsed as a wolfBoot image header. */
START_TEST(test_fat_rejects_fat12_fat16_markers)
{
    struct fs_volume vol;

    build_fs();
    put16(FAT_BPB_ROOT_ENT_CNT, 512);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
    ck_assert_int_ne(vol.type, FS_TYPE_RAW);

    build_fs();
    put16(FAT_BPB_TOT_SEC16, 4096);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);

    build_fs();
    put16(FAT_BPB_FAT_SZ16, 12);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* One cluster below the FAT32 floor the volume is FAT16 by definition,
 * whatever the rest of the BPB claims. */
START_TEST(test_fat_rejects_below_cluster_floor)
{
    struct fs_volume vol;

    build_fs();
    put32(FAT_BPB_TOT_SEC32, FS_TOTSEC - 1U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);

    /* Exactly at the floor it is still FAT32. */
    build_fs();
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_uint_eq(vol.u.fat.clus_count, 65525U);
}
END_TEST

START_TEST(test_fat_rejects_fs_version)
{
    struct fs_volume vol;

    build_fs();
    put16(FAT_BPB_FS_VER, 0x0100);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* With mirroring disabled and a FAT other than #0 active, the chain in
 * FAT #0 is stale -- following it would be a silent wrong read. */
START_TEST(test_fat_rejects_non_mirrored_active_fat)
{
    struct fs_volume vol;

    build_fs();
    put16(FAT_BPB_EXT_FLAGS, 0x0081);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_UNSUPPORTED);

    /* Mirroring disabled but FAT 0 active is fine. */
    build_fs();
    put16(FAT_BPB_EXT_FLAGS, 0x0080);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);

    /* Mirrored, active nibble ignored. */
    build_fs();
    put16(FAT_BPB_EXT_FLAGS, 0x0001);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
}
END_TEST

START_TEST(test_fat_rejects_bad_cluster_size)
{
    struct fs_volume vol;

    build_fs();
    fake_disk[FAT_BPB_SEC_PER_CLUS] = 0;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    fake_disk[FAT_BPB_SEC_PER_CLUS] = 3;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    fake_disk[FAT_BPB_SEC_PER_CLUS] = 255;
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

START_TEST(test_fat_rejects_zero_sizes)
{
    struct fs_volume vol;

    build_fs();
    put32(FAT_BPB_TOT_SEC32, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    put32(FAT_BPB_FAT_SZ32, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* Claiming more sectors than the partition holds would let every derived
 * byte offset run past the end of the partition. */
START_TEST(test_fat_rejects_volume_larger_than_partition)
{
    struct fs_volume vol;

    build_fs();
    put32(FAT_BPB_TOT_SEC32, FS_TOTSEC + 1U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    put32(FAT_BPB_TOT_SEC32, 0xFFFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* The metadata region must not swallow the whole volume. */
START_TEST(test_fat_rejects_metadata_exceeding_volume)
{
    struct fs_volume vol;

    build_fs();
    put32(FAT_BPB_FAT_SZ32, FS_TOTSEC);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* A FAT too small to hold an entry per cluster would let a chain name a
 * cluster whose "link" is really adjacent file data. */
START_TEST(test_fat_rejects_undersized_fat)
{
    struct fs_volume vol;
    uint32_t short_fat = 128U;   /* 65536 B, room for 16384 slots only */

    build_fs();
    put32(FAT_BPB_FAT_SZ32, short_fat);
    /* Keep the cluster count identical so only the FAT size differs. */
    put32(FAT_BPB_TOT_SEC32,
        FS_RSVD + (FS_NFATS * short_fat) + FS_CLUSTERS);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

START_TEST(test_fat_rejects_bad_root_cluster)
{
    struct fs_volume vol;

    build_fs();
    put32(FAT_BPB_ROOT_CLUS, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    put32(FAT_BPB_ROOT_CLUS, 1);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    put32(FAT_BPB_ROOT_CLUS, FS_CLUSTERS + 2U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);

    build_fs();
    put32(FAT_BPB_ROOT_CLUS, 0xFFFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* ---------------------------------------------------------------------
 * Cluster chain bounds
 * --------------------------------------------------------------------- */

/* A cluster whose FAT entry points at itself is the classic infinite
 * chain. It must terminate, and it must do so without an unbounded number
 * of media reads. */
START_TEST(test_fat_self_referential_chain)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[FS_SEC * 4];
    int ret;

    build_fs();
    add_simple_file(10, FS_SEC * 8U);
    fat_set(10, 10);

    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    disk_read_count = 0;
    /* Caught at open, before a single byte of payload is read. */
    ret = fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20);
    ck_assert_int_eq(ret, WOLFBOOT_FS_E_CORRUPT);
    ck_assert_uint_lt(disk_read_count, 2000U);
    (void)buf;
}
END_TEST

START_TEST(test_fat_two_cluster_cycle)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[FS_SEC];

    build_fs();
    add_simple_file(10, FS_SEC * 64U);
    fat_set(10, 11);
    fat_set(11, 10);

    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    disk_read_count = 0;
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_CORRUPT);
    ck_assert_uint_lt(disk_read_count, 2000U);
    (void)buf;
}
END_TEST

/* A chain must never point at a free, reserved, bad or out-of-range
 * cluster. */
START_TEST(test_fat_rejects_invalid_chain_links)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[FS_SEC];
    static const uint32_t bad[] = {
        0U, 1U, FS_CLUSTERS + 2U, 0x0FFFFFF7U, 0x0FFFFFF6U, 0x0FFFFFFFU
    };
    unsigned int i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_fs();
        add_simple_file(10, FS_SEC * 4U);
        fat_set(10, bad[i]);
        ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
        /* 0x0FFFFFFF is a legitimate end-of-chain marker, so a file whose
         * declared length needs a second cluster is corrupt either way. */
        ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
            WOLFBOOT_FS_E_CORRUPT);
    }
    (void)buf;
}
END_TEST

/* A file whose first cluster is out of range must be caught at open, not
 * followed. */
START_TEST(test_fat_rejects_bad_first_cluster)
{
    struct fs_volume vol;
    struct fs_file f;

    build_fs();
    add_simple_file(FS_CLUSTERS + 2U, 16U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_CORRUPT);

    build_fs();
    add_simple_file(1U, 16U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* ---------------------------------------------------------------------
 * File size bounds
 * --------------------------------------------------------------------- */

/* The size in the directory entry is attacker-controlled and would drive a
 * load into the RAM region before any signature check runs. */
START_TEST(test_fat_size_bounds)
{
    struct fs_volume vol;
    struct fs_file f;

    build_fs();
    add_simple_file(10, 0xFFFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    /* Larger than the partition: corrupt, regardless of the caller's max. */
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 0xFFFFFFFFFFULL),
        WOLFBOOT_FS_E_CORRUPT);

    /* The size cap is applied before the chain is walked, so a file that
     * is too big is rejected without any further metadata work. */
    build_fs();
    add_chained_file(10, 4U * 1024U * 1024U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1024U * 1024U),
        WOLFBOOT_FS_E_TOOBIG);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 4U * 1024U * 1024U),
        WOLFBOOT_FS_OK);

    /* A chain shorter than the declared length is a mismatch between the
     * directory entry and the FAT, and must not be readable. */
    build_fs();
    add_simple_file(10, 4U * 1024U * 1024U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 4U * 1024U * 1024U),
        WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* A zero-length file legitimately has no first cluster. */
START_TEST(test_fat_zero_length_file)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[16];

    build_fs();
    put_sfn(CLUS_OFF(ROOT_CLUS), "EMPTY   BIN", 0x20, 0, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/EMPTY.BIN", 1U << 20),
        WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), 0);
    ck_assert_int_eq(fs_read(&f, 0, sizeof(buf), buf), 0);
}
END_TEST

/* ---------------------------------------------------------------------
 * Directory walk bounds
 * --------------------------------------------------------------------- */

/* A directory whose cluster chain loops must terminate. */
START_TEST(test_fat_directory_chain_loop)
{
    struct fs_volume vol;
    struct fs_file f;
    uint32_t i;
    uint64_t base;

    build_fs();
    /* Root spans clusters 2 and 3, which point at each other. */
    fat_set(2, 3);
    fat_set(3, 2);
    for (i = 0; i < ENTS_PER_CLUS; i++) {
        base = CLUS_OFF(2) + ((uint64_t)i * 32U);
        put_sfn(base, "FILLER  BIN", 0x20, 10, 16);
        base = CLUS_OFF(3) + ((uint64_t)i * 32U);
        put_sfn(base, "FILLER  BIN", 0x20, 10, 16);
    }

    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    disk_read_count = 0;
    ck_assert_int_eq(fs_open(&vol, &f, "/NOPE.BIN", 1U << 20),
        WOLFBOOT_FS_E_CORRUPT);
    ck_assert_uint_lt(disk_read_count, 20000U);
}
END_TEST

/* Scanning a directory must stop after the configured entry cap. */
START_TEST(test_fat_directory_entry_cap)
{
    struct fs_volume vol;
    struct fs_file f;
    uint32_t clus, i;
    uint32_t needed = ((uint32_t)WOLFBOOT_FS_MAX_DIR_ENTRIES /
        ENTS_PER_CLUS) + 2U;

    build_fs();
    /* Chain the root over enough clusters to exceed the entry cap, all
     * full of entries that do not match. */
    for (clus = ROOT_CLUS; clus < (ROOT_CLUS + needed); clus++) {
        fat_set(clus, clus + 1U);
        for (i = 0; i < ENTS_PER_CLUS; i++) {
            put_sfn(CLUS_OFF(clus) + ((uint64_t)i * 32U), "FILLER  BIN",
                0x20, 60000, 16);
        }
    }
    fat_set(ROOT_CLUS + needed - 1U, 0x0FFFFFFFU);

    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/NOPE.BIN", 1U << 20),
        WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* ---------------------------------------------------------------------
 * Long filename handling
 * --------------------------------------------------------------------- */

/* The rotate-and-add checksum is the ONLY thing binding a long filename
 * set to the short entry that follows it. If it were not verified, a
 * crafted directory could attach an attacker-chosen long name to an
 * unrelated file's cluster and size, so wolfBoot would open a different
 * file than the one configured -- before any signature check runs. */
START_TEST(test_fat_lfn_checksum_must_match)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);

    build_fs();
    /* A correct set first, to show the long name does resolve. */
    put_lfn(base, 1, 1, sfn_cksum("IMAGE   ITB"), "decoy.itb");
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/decoy.itb", 1U << 20),
        WOLFBOOT_FS_OK);

    /* Now break only the checksum. The long name must stop resolving,
     * while the short name still works. */
    build_fs();
    put_lfn(base, 1, 1, (uint8_t)(sfn_cksum("IMAGE   ITB") ^ 0xFFU),
        "decoy.itb");
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/decoy.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_OK);
}
END_TEST

/* Multi-entry sets are stored in reverse ordinal order, terminated by the
 * short entry. Any break in that sequence discards the set. */
START_TEST(test_fat_lfn_ordinal_sequence)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint8_t ck = sfn_cksum("IMAGE   ITB");

    /* Correct three-entry set. */
    build_fs();
    put_lfn(base, 3, 1, ck, "me-here.itb");
    put_lfn(base + 32, 2, 0, ck, "-long-file-na");
    put_lfn(base + 64, 1, 0, ck, "a-really-very");
    put_sfn(base + 96, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol,
        &f, "/a-really-very-long-file-name-here.itb", 1U << 20),
        WOLFBOOT_FS_OK);

    /* Ordinals out of order. */
    build_fs();
    put_lfn(base, 3, 1, ck, "me-here.itb");
    put_lfn(base + 32, 1, 0, ck, "a-really-very");
    put_lfn(base + 64, 2, 0, ck, "-long-file-na");
    put_sfn(base + 96, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol,
        &f, "/a-really-very-long-file-name-here.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);

    /* A gap in the sequence. */
    build_fs();
    put_lfn(base, 3, 1, ck, "me-here.itb");
    put_lfn(base + 32, 1, 0, ck, "a-really-very");
    put_sfn(base + 64, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol,
        &f, "/a-really-very-long-file-name-here.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* Ordinal 0, or one above the 20-entry maximum, must be discarded rather
 * than used to index the name buffer. */
/* The ordinal bound was loosened so that 53-64 character names (five LFN
 * entries) resolve. These pin the other side of that change.
 *
 * Note a caller can never ASK for a name longer than WOLFBOOT_FS_MAX_NAME:
 * fs_path_next() rejects such a path component with E_PARAM before any
 * directory is read. So what matters on media is that an over-long LFN set
 * is absorbed harmlessly - it must not overflow s->name (ASAN covers that),
 * and it must not corrupt the accumulator for the entry that follows. */
START_TEST(test_fat_lfn_name_too_long_for_buffer)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint8_t ck = sfn_cksum("IMAGE   ITB");

    /* Six entries: the first character of the last one sits at index 65,
     * past the 64-byte buffer. Dropped by the ordinal bound. */
    build_fs();
    put_lfn(base +   0, 6, 1, ck, "zzzzzzzzzzzz");
    put_lfn(base +  32, 5, 0, ck, "yyyyyyyyyyyyy");
    put_lfn(base +  64, 4, 0, ck, "xxxxxxxxxxxxx");
    put_lfn(base +  96, 3, 0, ck, "wwwwwwwwwwwww");
    put_lfn(base + 128, 2, 0, ck, "vvvvvvvvvvvvv");
    put_lfn(base + 160, 1, 0, ck, "uuuuuuuuuuuuu");
    put_sfn(base + 192, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    /* The 8.3 alias still resolves, so the discarded set left no residue. */
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_OK);
    /* And no prefix of it matches by accident. */
    ck_assert_int_eq(fs_open(&vol, &f, "/uuuuuuuuuuuuu", 1U << 20),
        WOLFBOOT_FS_E_NOENT);

    /* Five entries whose last one is full: 65 characters, one past the
     * buffer. The ordinal bound accepts this set, so the per-character
     * bound is what has to stop the write. */
    build_fs();
    put_lfn(base +   0, 5, 1, ck, "zzzzzzzzzzzzz");
    put_lfn(base +  32, 4, 0, ck, "xxxxxxxxxxxxx");
    put_lfn(base +  64, 3, 0, ck, "wwwwwwwwwwwww");
    put_lfn(base +  96, 2, 0, ck, "vvvvvvvvvvvvv");
    put_lfn(base + 128, 1, 0, ck, "uuuuuuuuuuuuu");
    put_sfn(base + 160, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/uuuuuuuuuuuuu", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

START_TEST(test_fat_lfn_bad_ordinals)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint8_t ck = sfn_cksum("IMAGE   ITB");

    build_fs();
    put_lfn(base, 0, 1, ck, "zero.itb");
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/zero.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_OK);

    build_fs();
    put_lfn(base, 21, 1, ck, "toobig.itb");
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/toobig.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);

    /* A continuation entry with no preceding "last" entry. */
    build_fs();
    put_lfn(base, 1, 0, ck, "orphan.itb");
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/orphan.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* A deleted entry in the middle of a set breaks it. */
START_TEST(test_fat_lfn_deleted_entry_breaks_set)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint8_t ck = sfn_cksum("IMAGE   ITB");

    build_fs();
    put_lfn(base, 2, 1, ck, "e-here.itb");
    put_lfn(base + 32, 1, 0, ck, "a-long-nam");
    fake_disk[base + 32] = FAT_DIR_FREE;
    put_sfn(base + 64, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/a-long-name-here.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* A set that runs off the end of the directory must not be committed, and
 * must not read past the directory either. */
START_TEST(test_fat_lfn_unterminated_set)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint32_t i;

    build_fs();
    for (i = 0; i < ENTS_PER_CLUS; i++) {
        put_lfn(base + ((uint64_t)i * 32U), 1, 1, 0x42, "dangling.itb");
    }
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/dangling.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* Non-ASCII names cannot match an ASCII configured path, and must not be
 * mapped lossily into one that does. */
START_TEST(test_fat_lfn_non_ascii_dropped)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t base = CLUS_OFF(ROOT_CLUS);
    uint8_t ck = sfn_cksum("IMAGE   ITB");

    build_fs();
    /* The long name must differ from the 8.3 alias, or the fallback
     * short-name match would resolve it regardless. */
    put_lfn(base, 1, 1, ck, "longname.itb");
    /* Replace the first character with a non-ASCII code unit. */
    put16(base + 1, 0x00E9);
    put_sfn(base + 32, "IMAGE   ITB", 0x20, 10, 16);
    fat_set(10, 0x0FFFFFFFU);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/longname.itb", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
    /* The short name is unaffected. */
    ck_assert_int_eq(fs_open(&vol, &f, "/IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_OK);
}
END_TEST

/* The volume label lives in a directory entry, but is not a name that any
 * path can refer to. */
START_TEST(test_fat_volume_label_is_not_a_file)
{
    struct fs_volume vol;
    struct fs_file f;

    build_fs();
    put_sfn(CLUS_OFF(ROOT_CLUS), "VOLNAME    ", FAT_ATTR_VOLUME_ID, 0, 0);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_open(&vol, &f, "/VOLNAME", 1U << 20),
        WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* ---------------------------------------------------------------------
 * Path validation reached through fs_open()
 * --------------------------------------------------------------------- */

START_TEST(test_fat_open_rejects_hostile_paths)
{
    struct fs_volume vol;
    struct fs_file f;
    char deep[WOLFBOOT_FS_MAX_PATH + 16];
    char longp[WOLFBOOT_FS_MAX_PATH + 16];
    int i;

    build_fs();
    add_simple_file(10, 16U);
    ck_assert_int_eq(mount_result(&vol), WOLFBOOT_FS_OK);

    ck_assert_int_eq(fs_open(&vol, &f, "IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_open(&vol, &f, "/../IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_open(&vol, &f, "/./IMAGE.ITB", 1U << 20),
        WOLFBOOT_FS_E_PARAM);
    ck_assert_int_eq(fs_open(&vol, &f, NULL, 1U << 20),
        WOLFBOOT_FS_E_PARAM);

    /* The depth cap is enforced as the path is walked, so a path this
     * deep fails either on the cap or on the first missing component.
     * The cap itself is pinned directly in unit-fs-probe. */
    deep[0] = '\0';
    for (i = 0; i <= WOLFBOOT_FS_MAX_PATH_DEPTH; i++)
        strcat(deep, "/a");
    ck_assert_int_lt(fs_open(&vol, &f, deep, 1U << 20), 0);

    longp[0] = '/';
    for (i = 1; i <= WOLFBOOT_FS_MAX_PATH; i++)
        longp[i] = 'x';
    longp[WOLFBOOT_FS_MAX_PATH + 1] = '\0';
    ck_assert_int_eq(fs_open(&vol, &f, longp, 1U << 20),
        WOLFBOOT_FS_E_PARAM);
}
END_TEST


/* ---------------------------------------------------------------------
 * Synthetic ext4 volume
 *
 * 256 KiB, 1 KiB blocks, a single block group, 128-byte inodes and no
 * 64bit or metadata_csum. It shares the backing store allocated for the
 * FAT tests; only the first E4_PART_SZ bytes are ever used.
 * --------------------------------------------------------------------- */

#define E4_BLK              1024U
#define E4_BLOCKS            256U
#define E4_PART_SZ          ((uint64_t)E4_BLOCKS * E4_BLK)
#define E4_SB               1024U
#define E4_FIRST_DATA_BLOCK    1U
#define E4_BPG              8192U
#define E4_IPG                32U
#define E4_INODES             32U
#define E4_INODE_SZ          128U
#define E4_GDT_OFF          ((uint64_t)(E4_FIRST_DATA_BLOCK + 1U) * E4_BLK)
#define E4_ITBL_BLK           10U
#define E4_ROOT_DIR_BLK       20U
#define E4_FILE_BLK           30U
#define E4_FILE_INO           12U
#define E4_FILE_SIZE         512U
#define E4_INCOMPAT         (EXT4_INCOMPAT_EXTENTS | EXT4_INCOMPAT_FILETYPE)

static void e4_sb16(uint32_t off, uint16_t v)
{
    put16((uint64_t)E4_SB + off, v);
}

static void e4_sb32(uint32_t off, uint32_t v)
{
    put32((uint64_t)E4_SB + off, v);
}

static uint64_t e4_inode_off(uint32_t ino)
{
    return ((uint64_t)E4_ITBL_BLK * E4_BLK) +
        ((uint64_t)(ino - 1U) * E4_INODE_SZ);
}

/* Write a depth-0 extent tree into an inode's i_block, mapping logical
 * block 0 onwards to a single run of physical blocks. */
static void e4_extent_root(uint32_t ino, uint32_t lblk, uint32_t len,
                           uint32_t pblk)
{
    uint64_t off = e4_inode_off(ino) + EXT4_INODE_BLOCK;

    memset(fake_disk + off, 0, EXT4_INODE_BLOCK_LEN);
    put16(off + 0, EXT4_EXT_MAGIC);
    put16(off + 2, 1);      /* eh_entries */
    put16(off + 4, 4);      /* eh_max: (60 - 12) / 12 */
    put16(off + 6, 0);      /* eh_depth */
    put32(off + 12, lblk);  /* ee_block */
    put16(off + 16, (uint16_t)len);
    put16(off + 18, 0);     /* ee_start_hi */
    put32(off + 20, pblk);  /* ee_start_lo */
}

static void e4_inode(uint32_t ino, uint16_t mode, uint32_t size,
                     uint32_t flags)
{
    uint64_t off = e4_inode_off(ino);

    memset(fake_disk + off, 0, E4_INODE_SZ);
    put16(off + EXT4_INODE_MODE, mode);
    put32(off + EXT4_INODE_SIZE_LO, size);
    put32(off + EXT4_INODE_FLAGS, flags);
}

static void e4_dirent(uint64_t off, uint32_t ino, uint32_t rec_len,
                      const char *name, uint8_t ftype)
{
    uint32_t nlen = (uint32_t)strlen(name);

    memset(fake_disk + off, 0, rec_len);
    put32(off, ino);
    put16(off + 4, (uint16_t)rec_len);
    fake_disk[off + 6] = (uint8_t)nlen;
    fake_disk[off + 7] = ftype;
    memcpy(fake_disk + off + 8, name, nlen);
}

/* A pristine volume holding /image.itb. */
static void build_ext4(void)
{
    uint64_t dir = (uint64_t)E4_ROOT_DIR_BLK * E4_BLK;
    uint32_t i;

    if (fake_disk == NULL) {
        fake_disk = (uint8_t *)calloc(1, (size_t)FS_PART_SZ);
        ck_assert(fake_disk != NULL);
    }
    memset(fake_disk, 0, (size_t)E4_PART_SZ);

    e4_sb32(0x00, E4_INODES);            /* s_inodes_count      */
    e4_sb32(0x04, E4_BLOCKS);            /* s_blocks_count_lo   */
    e4_sb32(0x14, E4_FIRST_DATA_BLOCK);  /* s_first_data_block  */
    e4_sb32(0x18, 0);                    /* s_log_block_size    */
    e4_sb32(0x1C, 0);                    /* s_log_cluster_size  */
    e4_sb32(0x20, E4_BPG);               /* s_blocks_per_group  */
    e4_sb32(0x28, E4_IPG);               /* s_inodes_per_group  */
    e4_sb16(0x38, EXT4_SUPER_MAGIC);
    e4_sb16(0x3A, EXT4_STATE_VALID_FS);
    e4_sb32(0x4C, 1);                    /* s_rev_level         */
    e4_sb32(0x54, 11);                   /* s_first_ino         */
    e4_sb16(0x58, E4_INODE_SZ);
    e4_sb32(0x60, E4_INCOMPAT);
    memcpy(fake_disk + E4_SB + EXT4_SB_VOLUME_NAME, "WBEXT", 5);
    e4_sb32(0x150, 0);                   /* s_blocks_count_hi   */

    /* Group descriptor 0: only the inode table pointer matters here. */
    put32(E4_GDT_OFF + EXT4_GD_INODE_TABLE_LO, E4_ITBL_BLK);

    /* Root directory. */
    e4_inode(EXT4_ROOT_INO, 0x41EDU, E4_BLK, EXT4_FL_EXTENTS);
    e4_extent_root(EXT4_ROOT_INO, 0, 1, E4_ROOT_DIR_BLK);
    e4_dirent(dir, EXT4_ROOT_INO, 12, ".", 2);
    e4_dirent(dir + 12, EXT4_ROOT_INO, 12, "..", 2);
    e4_dirent(dir + 24, E4_FILE_INO, E4_BLK - 24U, "image.itb", 1);

    /* The file itself. */
    e4_inode(E4_FILE_INO, 0x81A4U, E4_FILE_SIZE, EXT4_FL_EXTENTS);
    e4_extent_root(E4_FILE_INO, 0, 1, E4_FILE_BLK);
    for (i = 0; i < E4_FILE_SIZE; i++)
        fake_disk[((uint64_t)E4_FILE_BLK * E4_BLK) + i] = (uint8_t)(i & 0xFF);

    memset(Drives, 0, sizeof(Drives));
    Drives[0].drv = 0;
    Drives[0].is_open = 1;
    Drives[0].n_parts = 1;
    Drives[0].part[0].drv = 0;
    Drives[0].part[0].start = 0;
    Drives[0].part[0].end = E4_PART_SZ - 1U;
    fs_cache_invalidate();
    disk_read_count = 0;
}

static int e4_mount(struct fs_volume *vol)
{
    fs_cache_invalidate();
    return fs_mount(vol, 0, 0);
}

/* Mount, then open /image.itb, returning whichever step failed. */
static int e4_open(struct fs_volume *vol, struct fs_file *f, uint64_t max)
{
    int ret = e4_mount(vol);

    if (ret != WOLFBOOT_FS_OK)
        return ret;
    if (vol->type != FS_TYPE_EXT4)
        return WOLFBOOT_FS_E_NOFS;
    return fs_open(vol, f, "/image.itb", max);
}

/* --- superblock --- */

/* The pristine volume must mount and read, or every rejection test below
 * would pass for the wrong reason. */
START_TEST(test_ext_baseline)
{
    struct fs_volume vol;
    struct fs_file f;
    uint8_t buf[E4_FILE_SIZE];
    uint32_t i;

    build_ext4();
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
    ck_assert_uint_eq((unsigned int)fs_size(&f), E4_FILE_SIZE);
    ck_assert_int_eq(fs_read(&f, 0, sizeof(buf), buf), (int)sizeof(buf));
    for (i = 0; i < sizeof(buf); i++)
        ck_assert_uint_eq(buf[i], (uint8_t)(i & 0xFF));
    ck_assert_int_eq(fs_label_eq(&vol, "WBEXT"), 1);
}
END_TEST

/* A wrong magic means "not ext4", so the probe falls back to raw rather
 * than reporting an error. */
START_TEST(test_ext_bad_magic_falls_back_to_raw)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb16(0x38, 0x1234);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_OK);
    ck_assert_int_eq(vol.type, FS_TYPE_RAW);
}
END_TEST

START_TEST(test_ext_rejects_bad_revision_and_state)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x4C, 2);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);

    /* A dirty filesystem may have a journal that has not been replayed,
     * so its metadata cannot be trusted. */
    build_ext4();
    e4_sb16(0x3A, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

START_TEST(test_ext_rejects_bad_block_size)
{
    struct fs_volume vol;
    static const uint32_t bad[] = { 3U, 6U, 0xFFFFFFFFU };
    unsigned int i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_ext4();
        e4_sb32(0x18, bad[i]);
        ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
    }

    /* bigalloc gives clusters a size of their own. */
    build_ext4();
    e4_sb32(0x1C, 1);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* Exactly one first_data_block is correct for a given block size.
 * Anything else moves the group descriptor table out from under every
 * offset derived from it. */
START_TEST(test_ext_rejects_bad_first_data_block)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x14, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    build_ext4();
    e4_sb32(0x14, 2);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

START_TEST(test_ext_rejects_bad_group_geometry)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x20, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    /* Not a multiple of 8: a block bitmap cannot describe it. */
    build_ext4();
    e4_sb32(0x20, 12);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    /* Larger than one block's worth of bitmap bits. */
    build_ext4();
    e4_sb32(0x20, (E4_BLK * 8U) + 8U);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    build_ext4();
    e4_sb32(0x28, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    build_ext4();
    e4_sb32(0x00, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    build_ext4();
    e4_sb32(0x04, 0);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* The superblock states its geometry twice over, once in blocks and once
 * in inodes. If the two disagree, one of them was forged. */
START_TEST(test_ext_rejects_group_count_mismatch)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x28, 8);   /* inodes_per_group: now 4 groups, not 1 */
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

START_TEST(test_ext_rejects_bad_inode_size)
{
    struct fs_volume vol;
    static const uint16_t bad[] = { 0U, 64U, 96U, 300U, 2048U };
    unsigned int i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_ext4();
        e4_sb16(0x58, bad[i]);
        ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
    }

    build_ext4();
    e4_sb32(0x54, 5);   /* s_first_ino below the reserved range */
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

/* Claiming more blocks than the partition holds would let every physical
 * block number reach outside it. */
START_TEST(test_ext_rejects_volume_larger_than_partition)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x04, E4_BLOCKS + 1U);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);

    build_ext4();
    e4_sb32(0x04, 0x00FFFFFFU);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_FORMAT);
}
END_TEST

START_TEST(test_ext_rejects_64bit_block_count)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x150, 1);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* Every INCOMPAT bit outside the whitelist must be refused, including
 * bits that did not exist when this was written. */
START_TEST(test_ext_incompat_whitelist_is_fail_closed)
{
    struct fs_volume vol;
    static const uint32_t bad[] = {
        0x0001U,  /* COMPRESSION */
        0x0004U,  /* RECOVER: journal needs replay   */
        0x0008U,  /* JOURNAL_DEV  */
        0x0010U,  /* META_BG      */
        0x0100U,  /* MMP          */
        0x1000U,  /* DIRDATA      */
        0x4000U,  /* LARGEDIR     */
        0x8000U,  /* INLINE_DATA  */
        0x10000U, /* ENCRYPT      */
        0x20000U, /* CASEFOLD     */
        0x40000U  /* not yet assigned */
    };
    unsigned int i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_ext4();
        e4_sb32(0x60, E4_INCOMPAT | bad[i]);
        ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
    }
}
END_TEST

/* ext2 and ext3 use the indirect block map, which this parser does not
 * implement and must not pretend to. */
START_TEST(test_ext_requires_extents_feature)
{
    struct fs_volume vol;

    build_ext4();
    e4_sb32(0x60, EXT4_INCOMPAT_FILETYPE);
    ck_assert_int_eq(e4_mount(&vol), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* RO_COMPAT bits are safe to ignore on a read-only mount -- that is what
 * the flag class means -- and ignoring them is what makes metadata_csum,
 * huge_file and friends work with no code at all. */
START_TEST(test_ext_ignores_ro_compat_bits)
{
    struct fs_volume vol;
    struct fs_file f;

    build_ext4();
    e4_sb32(0x64, 0xFFFFFFFFU);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);

    /* COMPAT bits are likewise ignorable by definition. */
    build_ext4();
    e4_sb32(0x5C, 0xFFFFFFFFU);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
}
END_TEST

/* --- group descriptors and inodes --- */

START_TEST(test_ext_rejects_bad_inode_table)
{
    struct fs_volume vol;
    struct fs_file f;

    build_ext4();
    put32(E4_GDT_OFF + EXT4_GD_INODE_TABLE_LO, 0);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    build_ext4();
    put32(E4_GDT_OFF + EXT4_GD_INODE_TABLE_LO, E4_BLOCKS);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    build_ext4();
    put32(E4_GDT_OFF + EXT4_GD_INODE_TABLE_LO, 0xFFFFFFFFU);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

START_TEST(test_ext_rejects_out_of_range_inode)
{
    struct fs_volume vol;
    struct fs_file f;

    build_ext4();
    put32((uint64_t)E4_ROOT_DIR_BLK * E4_BLK + 24U, E4_INODES + 1U);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* A symlink must be refused rather than followed. */
START_TEST(test_ext_rejects_symlink)
{
    struct fs_volume vol;
    struct fs_file f;

    build_ext4();
    put16(e4_inode_off(E4_FILE_INO) + EXT4_INODE_MODE, EXT4_S_IFLNK | 0777U);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_UNSUPPORTED);

    /* Even when only the dirent claims it is one. */
    build_ext4();
    fake_disk[((uint64_t)E4_ROOT_DIR_BLK * E4_BLK) + 24U + 7U] =
        EXT4_FT_SYMLINK;
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

/* A volume can advertise extents while an individual inode still uses the
 * old indirect block map, so the per-inode flag matters too. */
START_TEST(test_ext_rejects_inode_flags)
{
    struct fs_volume vol;
    struct fs_file f;
    static const uint32_t bad[] = {
        EXT4_FL_INLINE_DATA, EXT4_FL_ENCRYPT, EXT4_FL_COMPR
    };
    unsigned int i;

    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_FLAGS, 0);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_UNSUPPORTED);

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_ext4();
        put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_FLAGS,
            EXT4_FL_EXTENTS | bad[i]);
        ck_assert_int_eq(e4_open(&vol, &f, 1U << 20),
            WOLFBOOT_FS_E_UNSUPPORTED);
    }
}
END_TEST

START_TEST(test_ext_size_bounds)
{
    struct fs_volume vol;
    struct fs_file f;

    /* Larger than the volume: corrupt, whatever the caller allows. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, 0xFFFFFFFFU);
    ck_assert_int_eq(e4_open(&vol, &f, 0xFFFFFFFFFFULL),
        WOLFBOOT_FS_E_CORRUPT);

    /* Within the volume but above the caller's cap. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, 4096);
    ck_assert_int_eq(e4_open(&vol, &f, 1024), WOLFBOOT_FS_E_TOOBIG);
}
END_TEST

/* --- extent tree --- */

START_TEST(test_ext_rejects_bad_extent_header)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;

    build_ext4();
    put16(ib + 0, 0x1234);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* Deeper than ext4's own architectural limit. */
    build_ext4();
    put16(ib + 6, WOLFBOOT_FS_EXT4_MAX_DEPTH + 1);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* eh_max beyond what the 60-byte root can hold. */
    build_ext4();
    put16(ib + 4, 5);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    build_ext4();
    put16(ib + 4, 0xFFFF);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* eh_entries beyond eh_max. */
    build_ext4();
    put16(ib + 2, 5);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

START_TEST(test_ext_rejects_bad_extent_length)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;

    build_ext4();
    put16(ib + 16, 0);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* Above the initialized maximum: an uninitialized extent, which reads
     * as zeros. Returning those silently would hand the verifier bytes the
     * file does not contain. */
    build_ext4();
    put16(ib + 16, EXT4_EXT_INIT_MAX_LEN + 1U);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_UNSUPPORTED);

    build_ext4();
    put16(ib + 16, 0xFFFF);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_UNSUPPORTED);
}
END_TEST

START_TEST(test_ext_rejects_out_of_range_extent)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;

    build_ext4();
    put32(ib + 20, E4_BLOCKS);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* Starts in range but runs past the end. */
    build_ext4();
    put32(ib + 20, E4_BLOCKS - 1U);
    put16(ib + 16, 8);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* Below first_data_block. */
    build_ext4();
    put32(ib + 20, 0);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* A high half that would wrap a 32-bit sum. */
    build_ext4();
    put16(ib + 18, 0xFFFF);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* Extents in a node must be strictly increasing. One rule that rejects
 * backward, duplicate and overlapping entries at once. */
START_TEST(test_ext_rejects_non_monotonic_extents)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;

    build_ext4();
    put16(ib + 2, 2);           /* two entries */
    put32(ib + 12, 1);          /* first covers logical block 1 */
    put32(ib + 24, 0);          /* second goes backwards */
    put16(ib + 28, 1);
    put32(ib + 32, E4_FILE_BLK);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* A duplicate logical block, reached while mapping the file's second
     * block. The scan stops at the first covering extent, so the offending
     * entry has to be one the lookup actually reaches. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, E4_BLK * 2U);
    put16(ib + 2, 2);
    put32(ib + 24, 0);          /* same ee_block as entry 0 */
    put16(ib + 28, 1);
    put32(ib + 32, E4_FILE_BLK);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* A hole reads as zeros -- that is ext4's definition, so zeros ARE the
 * file's content there and the integrity hash still covers exactly what the
 * file holds. This is not an exotic case: mke2fs -d and cp both leave a hole
 * wherever the source has a run of zeros, which the padding in a signed
 * image header always does. */
START_TEST(test_ext_reads_holes_as_zeros)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;
    uint8_t buf[E4_BLK * 4U];
    uint32_t i;

    /* Leading hole: a two-block file whose only extent covers block 1. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, E4_BLK * 2U);
    put32(ib + 12, 1);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, E4_BLK * 2U, buf), (int)(E4_BLK * 2U));
    for (i = 0; i < E4_BLK; i++)
        ck_assert_uint_eq(buf[i], 0);
    for (i = 0; i < E4_FILE_SIZE; i++)
        ck_assert_uint_eq(buf[E4_BLK + i], (uint8_t)(i & 0xFF));

    /* Trailing hole: a four-block file whose only extent covers block 0. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, E4_BLK * 4U);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, E4_BLK * 4U, buf), (int)(E4_BLK * 4U));
    for (i = 0; i < E4_FILE_SIZE; i++)
        ck_assert_uint_eq(buf[i], (uint8_t)(i & 0xFF));
    for (i = E4_BLK; i < (E4_BLK * 4U); i++)
        ck_assert_uint_eq(buf[i], 0);

    /* Interior hole -- the shape a real signed image produces: block 0 and
     * block 2 mapped, block 1 a hole. */
    build_ext4();
    put32(e4_inode_off(E4_FILE_INO) + EXT4_INODE_SIZE_LO, E4_BLK * 3U);
    put16(ib + 2, 2);              /* two extents */
    put32(ib + 24, 2);             /* second covers logical block 2 */
    put16(ib + 28, 1);
    put32(ib + 32, E4_FILE_BLK + 1U);
    for (i = 0; i < E4_BLK; i++) {
        fake_disk[((uint64_t)(E4_FILE_BLK + 1U) * E4_BLK) + i] =
            (uint8_t)(0xC0 ^ (i & 0xFF));
    }
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, E4_BLK * 3U, buf), (int)(E4_BLK * 3U));
    for (i = 0; i < E4_FILE_SIZE; i++)
        ck_assert_uint_eq(buf[i], (uint8_t)(i & 0xFF));
    for (i = E4_BLK; i < (E4_BLK * 2U); i++)
        ck_assert_uint_eq(buf[i], 0);
    for (i = 0; i < E4_BLK; i++)
        ck_assert_uint_eq(buf[(E4_BLK * 2U) + i], (uint8_t)(0xC0 ^ (i & 0xFF)));
}
END_TEST

/* A valid depth-1 tree, which mke2fs will not produce on an image this
 * small, so it is built by hand. */
START_TEST(test_ext_depth_one_tree)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;
    uint64_t node = (uint64_t)40 * E4_BLK;
    uint8_t buf[E4_FILE_SIZE];
    uint32_t i;

    build_ext4();
    /* Root becomes an index node pointing at a leaf in block 40. */
    memset(fake_disk + ib, 0, EXT4_INODE_BLOCK_LEN);
    put16(ib + 0, EXT4_EXT_MAGIC);
    put16(ib + 2, 1);
    put16(ib + 4, 4);
    put16(ib + 6, 1);            /* eh_depth */
    put32(ib + 12, 0);           /* ei_block */
    put32(ib + 16, 40);          /* ei_leaf_lo */
    put16(ib + 20, 0);           /* ei_leaf_hi */

    memset(fake_disk + node, 0, E4_BLK);
    put16(node + 0, EXT4_EXT_MAGIC);
    put16(node + 2, 1);
    put16(node + 4, (uint16_t)((E4_BLK - 12U) / 12U));
    put16(node + 6, 0);          /* eh_depth */
    put32(node + 12, 0);         /* ee_block */
    put16(node + 16, 1);         /* ee_len */
    put16(node + 18, 0);
    put32(node + 20, E4_FILE_BLK);

    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_OK);
    ck_assert_int_eq(fs_read(&f, 0, sizeof(buf), buf), (int)sizeof(buf));
    for (i = 0; i < sizeof(buf); i++)
        ck_assert_uint_eq(buf[i], (uint8_t)(i & 0xFF));

    /* Now break the child's depth. Requiring it to fall by exactly one at
     * every level is what makes the descent finite without a visited set,
     * so an index entry that points back at an ancestor is caught here. */
    put16(node + 6, 1);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    /* A child claiming to be a deeper index node than its parent. */
    build_ext4();
    memset(fake_disk + ib, 0, EXT4_INODE_BLOCK_LEN);
    put16(ib + 0, EXT4_EXT_MAGIC);
    put16(ib + 2, 1);
    put16(ib + 4, 4);
    put16(ib + 6, 1);
    put32(ib + 16, 40);
    memset(fake_disk + node, 0, E4_BLK);
    put16(node + 0, EXT4_EXT_MAGIC);
    put16(node + 2, 1);
    put16(node + 4, 4);
    put16(node + 6, 2);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* An index entry pointing outside the volume must not be descended into. */
START_TEST(test_ext_rejects_bad_index_leaf)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t ib = e4_inode_off(E4_FILE_INO) + EXT4_INODE_BLOCK;

    build_ext4();
    put16(ib + 6, 1);            /* make the root an index node */
    put32(ib + 16, E4_BLOCKS);   /* ei_leaf past the end of the volume */
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);

    build_ext4();
    put16(ib + 6, 1);
    put32(ib + 16, 0);           /* below first_data_block */
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* --- directory entries --- */

/* A zero or misaligned rec_len is the standard way to make a directory
 * walk loop forever or step outside its block. */
START_TEST(test_ext_rejects_bad_rec_len)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t dir = (uint64_t)E4_ROOT_DIR_BLK * E4_BLK;
    static const uint16_t bad[] = { 0U, 4U, 7U, 13U, 0xFFFFU };
    unsigned int i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        build_ext4();
        put16(dir + 4, bad[i]);
        ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
    }

    /* Runs past the end of the block. */
    build_ext4();
    put16(dir + 24 + 4, (uint16_t)(E4_BLK - 24U + 4U));
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

START_TEST(test_ext_rejects_name_beyond_record)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t dir = (uint64_t)E4_ROOT_DIR_BLK * E4_BLK;

    build_ext4();
    fake_disk[dir + 6] = 250;    /* name_len far beyond a rec_len of 12 */
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

/* inode 0 marks an unused slot, and is how an htree interior node hides
 * itself from a linear scan. It must be skipped, and the walk must still
 * terminate. */
START_TEST(test_ext_skips_unused_dirents)
{
    struct fs_volume vol;
    struct fs_file f;
    uint64_t dir = (uint64_t)E4_ROOT_DIR_BLK * E4_BLK;

    build_ext4();
    e4_dirent(dir, 0, E4_BLK, "", 0);
    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_NOENT);
}
END_TEST

/* Scanning a directory must stop after the configured entry cap. */
START_TEST(test_ext_directory_entry_cap)
{
    struct fs_volume vol;
    struct fs_file f;
    uint32_t blocks = 60;
    uint32_t b, pos;
    uint64_t base;

    build_ext4();
    /* A root directory spanning enough blocks to exceed the entry cap,
     * every entry unused so the scan never finds a match. */
    put32(e4_inode_off(EXT4_ROOT_INO) + EXT4_INODE_SIZE_LO,
        blocks * E4_BLK);
    e4_extent_root(EXT4_ROOT_INO, 0, blocks, 100);
    for (b = 0; b < blocks; b++) {
        base = (uint64_t)(100U + b) * E4_BLK;
        for (pos = 0; pos < E4_BLK; pos += 12U)
            e4_dirent(base + pos, 0, 12, "", 0);
    }

    ck_assert_int_eq(e4_open(&vol, &f, 1U << 20), WOLFBOOT_FS_E_CORRUPT);
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-fs-malicious");
    TCase *tc_bpb = tcase_create("fat32-geometry");
    TCase *tc_chain = tcase_create("fat32-chain");
    TCase *tc_dir = tcase_create("fat32-directory");
    TCase *tc_lfn = tcase_create("fat32-lfn");
    TCase *tc_esb = tcase_create("ext4-superblock");
    TCase *tc_eino = tcase_create("ext4-inode");
    TCase *tc_eext = tcase_create("ext4-extents");
    TCase *tc_edir = tcase_create("ext4-directory");

    /* The directory-cap test walks thousands of entries under ASAN. */
    tcase_set_timeout(tc_dir, 60);
    tcase_set_timeout(tc_edir, 60);

    tcase_add_test(tc_bpb, test_fat_baseline_mounts);
    tcase_add_test(tc_bpb, test_fat_stage1_rejects_fall_back_to_raw);
    tcase_add_test(tc_bpb, test_fat_rejects_non_512_sectors);
    tcase_add_test(tc_bpb, test_fat_rejects_fat12_fat16_markers);
    tcase_add_test(tc_bpb, test_fat_rejects_below_cluster_floor);
    tcase_add_test(tc_bpb, test_fat_rejects_fs_version);
    tcase_add_test(tc_bpb, test_fat_rejects_non_mirrored_active_fat);
    tcase_add_test(tc_bpb, test_fat_rejects_bad_cluster_size);
    tcase_add_test(tc_bpb, test_fat_rejects_zero_sizes);
    tcase_add_test(tc_bpb, test_fat_rejects_volume_larger_than_partition);
    tcase_add_test(tc_bpb, test_fat_rejects_metadata_exceeding_volume);
    tcase_add_test(tc_bpb, test_fat_rejects_undersized_fat);
    tcase_add_test(tc_bpb, test_fat_rejects_bad_root_cluster);
    suite_add_tcase(s, tc_bpb);

    tcase_add_test(tc_chain, test_fat_self_referential_chain);
    tcase_add_test(tc_chain, test_fat_two_cluster_cycle);
    tcase_add_test(tc_chain, test_fat_rejects_invalid_chain_links);
    tcase_add_test(tc_chain, test_fat_rejects_bad_first_cluster);
    tcase_add_test(tc_chain, test_fat_size_bounds);
    tcase_add_test(tc_chain, test_fat_zero_length_file);
    suite_add_tcase(s, tc_chain);

    tcase_add_test(tc_dir, test_fat_directory_chain_loop);
    tcase_add_test(tc_dir, test_fat_directory_entry_cap);
    tcase_add_test(tc_dir, test_fat_volume_label_is_not_a_file);
    tcase_add_test(tc_dir, test_fat_open_rejects_hostile_paths);
    suite_add_tcase(s, tc_dir);

    tcase_add_test(tc_lfn, test_fat_lfn_checksum_must_match);
    tcase_add_test(tc_lfn, test_fat_lfn_ordinal_sequence);
    tcase_add_test(tc_lfn, test_fat_lfn_name_too_long_for_buffer);
    tcase_add_test(tc_lfn, test_fat_lfn_bad_ordinals);
    tcase_add_test(tc_lfn, test_fat_lfn_deleted_entry_breaks_set);
    tcase_add_test(tc_lfn, test_fat_lfn_unterminated_set);
    tcase_add_test(tc_lfn, test_fat_lfn_non_ascii_dropped);
    suite_add_tcase(s, tc_lfn);

    tcase_add_test(tc_esb, test_ext_baseline);
    tcase_add_test(tc_esb, test_ext_bad_magic_falls_back_to_raw);
    tcase_add_test(tc_esb, test_ext_rejects_bad_revision_and_state);
    tcase_add_test(tc_esb, test_ext_rejects_bad_block_size);
    tcase_add_test(tc_esb, test_ext_rejects_bad_first_data_block);
    tcase_add_test(tc_esb, test_ext_rejects_bad_group_geometry);
    tcase_add_test(tc_esb, test_ext_rejects_group_count_mismatch);
    tcase_add_test(tc_esb, test_ext_rejects_bad_inode_size);
    tcase_add_test(tc_esb, test_ext_rejects_volume_larger_than_partition);
    tcase_add_test(tc_esb, test_ext_rejects_64bit_block_count);
    tcase_add_test(tc_esb, test_ext_incompat_whitelist_is_fail_closed);
    tcase_add_test(tc_esb, test_ext_requires_extents_feature);
    tcase_add_test(tc_esb, test_ext_ignores_ro_compat_bits);
    suite_add_tcase(s, tc_esb);

    tcase_add_test(tc_eino, test_ext_rejects_bad_inode_table);
    tcase_add_test(tc_eino, test_ext_rejects_out_of_range_inode);
    tcase_add_test(tc_eino, test_ext_rejects_symlink);
    tcase_add_test(tc_eino, test_ext_rejects_inode_flags);
    tcase_add_test(tc_eino, test_ext_size_bounds);
    suite_add_tcase(s, tc_eino);

    tcase_add_test(tc_eext, test_ext_rejects_bad_extent_header);
    tcase_add_test(tc_eext, test_ext_rejects_bad_extent_length);
    tcase_add_test(tc_eext, test_ext_rejects_out_of_range_extent);
    tcase_add_test(tc_eext, test_ext_rejects_non_monotonic_extents);
    tcase_add_test(tc_eext, test_ext_reads_holes_as_zeros);
    tcase_add_test(tc_eext, test_ext_depth_one_tree);
    tcase_add_test(tc_eext, test_ext_rejects_bad_index_leaf);
    suite_add_tcase(s, tc_eext);

    tcase_add_test(tc_edir, test_ext_rejects_bad_rec_len);
    tcase_add_test(tc_edir, test_ext_rejects_name_beyond_record);
    tcase_add_test(tc_edir, test_ext_skips_unused_dirents);
    tcase_add_test(tc_edir, test_ext_directory_entry_cap);
    suite_add_tcase(s, tc_edir);

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
