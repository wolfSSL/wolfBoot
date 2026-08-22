/* fat32.h
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

/* On-disk FAT32 layout constants and the FAT32 backend interface.
 */

#ifndef WOLFBOOT_FAT32_H
#define WOLFBOOT_FAT32_H

#include <stdint.h>

/* The partition layer works in 512-byte sectors throughout (GPT_SECTOR_SIZE
 * in include/gpt.h, and the lba*512 arithmetic in src/disk.c), so a volume
 * with any other logical sector size is refused rather than misread. */
#define FAT_SECTOR_SIZE   512U

/* BIOS Parameter Block field offsets within the boot sector */
#define FAT_BPB_SIZE            64U
#define FAT_BPB_MEDIA         0x15U
#define FAT_BPB_BYTS_PER_SEC  0x0BU
#define FAT_BPB_SEC_PER_CLUS  0x0DU
#define FAT_BPB_RSVD_SEC_CNT  0x0EU
#define FAT_BPB_NUM_FATS      0x10U
#define FAT_BPB_ROOT_ENT_CNT  0x11U
#define FAT_BPB_TOT_SEC16     0x13U
#define FAT_BPB_FAT_SZ16      0x16U
#define FAT_BPB_TOT_SEC32     0x20U
#define FAT_BPB_FAT_SZ32      0x24U
#define FAT_BPB_EXT_FLAGS     0x28U
#define FAT_BPB_FS_VER        0x2AU
#define FAT_BPB_ROOT_CLUS     0x2CU
#define FAT_BS_VOL_LAB        0x47U
#define FAT_BS_VOL_LAB_LEN      11U
#define FAT_BOOT_SIG_OFF     0x1FEU
#define FAT_BOOT_SIG_VALUE  0xAA55U

/* BPB_ExtFlags: bit 7 clear means all FATs are mirrored, and the low
 * nibble selects the active FAT when it is set. */
#define FAT_EXT_NO_MIRROR     0x0080U
#define FAT_EXT_ACTIVE_MASK   0x000FU

/* FAT entry values, after masking off the reserved high nibble */
#define FAT_CLUS_MASK     0x0FFFFFFFU
#define FAT_CLUS_MAX      0x0FFFFFF5U  /* highest usable cluster number */
#define FAT_CLUS_BAD      0x0FFFFFF7U
#define FAT_CLUS_EOC_MIN  0x0FFFFFF8U

/* A volume with fewer data clusters than this is FAT16 or FAT12 by
 * definition, whatever its BPB claims. */
#define FAT_MIN_CLUSTERS    65525U
#define FAT_MAX_CLUS_SIZE   65536U

/* Directory entry layout */
#define FAT_DIR_ENTRY_SIZE      32U
#define FAT_DIR_ATTR            11U
#define FAT_DIR_FST_CLUS_HI     20U
#define FAT_DIR_FST_CLUS_LO     26U
#define FAT_DIR_FILE_SIZE       28U
#define FAT_DIR_FREE          0xE5U
#define FAT_DIR_END           0x00U
#define FAT_DIR_KANJI_E5      0x05U

#define FAT_ATTR_VOLUME_ID    0x08U
#define FAT_ATTR_DIRECTORY    0x10U
#define FAT_ATTR_LFN          0x0FU
#define FAT_ATTR_LFN_MASK     0x3FU

/* Long filename entries */
#define FAT_LFN_LAST          0x40U
#define FAT_LFN_ORD_MASK      0x3FU
#define FAT_LFN_MAX_ORD         20U  /* 20 * 13 == 260, the FAT maximum */
#define FAT_LFN_CHARS           13U
#define FAT_LFN_CKSUM_OFF       13U

/* An 8.3 short name occupies 11 bytes ON DISK: 8 name + 3 extension, space
 * padded, with no stored '.'. FAT_SFN_MAX is the different, larger number:
 * the length of the same name once rendered as "NAME.EXT" for comparison. */
#define FAT_SFN_NAME_LEN         8U
#define FAT_SFN_EXT_LEN          3U
#define FAT_SFN_LEN             (FAT_SFN_NAME_LEN + FAT_SFN_EXT_LEN)  /* 11 */
#define FAT_SFN_MAX             (FAT_SFN_LEN + 1U)                    /* 12 */


/* FAT32 geometry, validated at mount. Offsets are partition-relative so
 * every read inherits disk_part_read()'s bounds checking. */
struct fat32_volume {
    uint64_t fat_off;     /* byte offset of FAT #0 within the partition */
    uint64_t data_off;    /* byte offset of cluster 2 */
    uint64_t fat_sz;      /* bytes in one FAT */
    uint32_t clus_sz;     /* bytes per cluster */
    uint32_t clus_count;  /* data clusters; highest valid is clus_count + 1 */
    uint32_t root_clus;   /* first cluster of the root directory */
};

struct fs_volume;
struct fs_file;

/* Backend entry points. fat32_mount() returns WOLFBOOT_FS_E_NOFS when the
 * partition simply is not FAT (so probing continues), and
 * WOLFBOOT_FS_E_UNSUPPORTED when it IS FAT but uses something this
 * implementation refuses to read, such as FAT12 or FAT16. */
int fat32_mount(struct fs_volume *vol);
int fat32_open(struct fs_volume *vol, struct fs_file *f, const char *path,
               uint64_t max_size);
int fat32_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf);
int fat32_label_eq(struct fs_volume *vol, const char *label);

#endif /* WOLFBOOT_FAT32_H */
