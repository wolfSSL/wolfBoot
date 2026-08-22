/* ext4.h
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

/* On-disk ext4 layout constants and the ext4 backend interface.
 */

#ifndef WOLFBOOT_EXT4_H
#define WOLFBOOT_EXT4_H

#include <stdint.h>

/* The superblock always starts 1024 bytes into the volume. */
#define EXT4_SB_OFF        1024U

/* Superblock fields are read in two contiguous groups so that no single
 * stack buffer gets large; both land in the same metadata cache window. */
#define EXT4_SB_A_OFF      0x00U
#define EXT4_SB_A_LEN      0x2CU
#define EXT4_SB_B_OFF      0x38U
#define EXT4_SB_B_LEN      0x30U

/* Offsets within group A */
#define EXT4_A_INODES_COUNT      0x00U
#define EXT4_A_BLOCKS_COUNT_LO   0x04U
#define EXT4_A_FIRST_DATA_BLOCK  0x14U
#define EXT4_A_LOG_BLOCK_SIZE    0x18U
#define EXT4_A_LOG_CLUSTER_SIZE  0x1CU
#define EXT4_A_BLOCKS_PER_GROUP  0x20U
#define EXT4_A_INODES_PER_GROUP  0x28U

/* Offsets within group B (relative to EXT4_SB_B_OFF) */
#define EXT4_B_MAGIC             0x00U
#define EXT4_B_STATE             0x02U
#define EXT4_B_REV_LEVEL         0x14U
#define EXT4_B_FIRST_INO         0x1CU
#define EXT4_B_INODE_SIZE        0x20U
#define EXT4_B_FEATURE_INCOMPAT  0x28U

/* Standalone superblock fields */
#define EXT4_SB_VOLUME_NAME      0x78U
#define EXT4_SB_VOLUME_NAME_LEN    16U
#define EXT4_SB_DESC_SIZE        0xFEU
#define EXT4_SB_BLOCKS_COUNT_HI  0x150U

#define EXT4_SUPER_MAGIC   0xEF53U
#define EXT4_ROOT_INO          2U
#define EXT4_GOOD_OLD_INO_SZ 128U
#define EXT4_STATE_VALID_FS 0x0001U

/* INCOMPAT feature bits. A whitelist, tested as "any bit not in it", so
 * every unlisted bit - including ones defined after this was written - is
 * refused. That is what makes the parser fail closed: COMPRESSION, RECOVER
 * (dirty journal), JOURNAL_DEV, META_BG, MMP, DIRDATA, LARGEDIR,
 * INLINE_DATA, ENCRYPT and CASEFOLD all land outside it. */
#define EXT4_INCOMPAT_FILETYPE   0x0002U
#define EXT4_INCOMPAT_EXTENTS    0x0040U
#define EXT4_INCOMPAT_64BIT      0x0080U
#define EXT4_INCOMPAT_FLEX_BG    0x0200U
#define EXT4_INCOMPAT_EA_INODE   0x0400U
#define EXT4_INCOMPAT_CSUM_SEED  0x2000U

#define EXT4_INCOMPAT_SUPPORTED ( \
      EXT4_INCOMPAT_FILETYPE  /* dirent type byte; optional, handled */   \
    | EXT4_INCOMPAT_EXTENTS   /* required outright, see ext4_mount */     \
    | EXT4_INCOMPAT_64BIT     /* descriptor geometry only; hi refused */  \
    | EXT4_INCOMPAT_FLEX_BG   /* allocation policy, layout-neutral */     \
    | EXT4_INCOMPAT_EA_INODE  /* extended attributes, never read */       \
    | EXT4_INCOMPAT_CSUM_SEED /* checksum seed, layout-neutral */ )

/* Inode flags */
#define EXT4_FL_COMPR       0x00000004U
#define EXT4_FL_ENCRYPT     0x00000800U
#define EXT4_FL_EXTENTS     0x00080000U
#define EXT4_FL_INLINE_DATA 0x10000000U

/* Inode layout */
#define EXT4_INODE_MODE       0x00U
#define EXT4_INODE_SIZE_LO    0x04U
#define EXT4_INODE_FLAGS      0x20U
#define EXT4_INODE_BLOCK      0x28U
#define EXT4_INODE_BLOCK_LEN    60U
#define EXT4_INODE_SIZE_HIGH  0x6CU
#define EXT4_INODE_HDR_LEN    0x24U

#define EXT4_S_IFMT   0xF000U
#define EXT4_S_IFREG  0x8000U
#define EXT4_S_IFDIR  0x4000U
#define EXT4_S_IFLNK  0xA000U

/* Group descriptor */
#define EXT4_GD_INODE_TABLE_LO 0x08U
#define EXT4_GD_INODE_TABLE_HI 0x28U
#define EXT4_GD_MIN_SIZE         32U
#define EXT4_GD_64BIT_SIZE       64U

/* Extent tree */
#define EXT4_EXT_MAGIC        0xF30AU
#define EXT4_EXT_HDR_LEN         12U
#define EXT4_EXT_ENT_LEN         12U
#define EXT4_EXT_ROOT_CAP        60U
#define EXT4_EXT_INIT_MAX_LEN 32768U

/* Directory entry */
#define EXT4_DIRENT_HDR_LEN 8U
#define EXT4_FT_SYMLINK     7U
#define EXT4_NAME_MAX     255U

/* ext4 geometry, validated at mount. */
struct ext4_volume {
    uint64_t gdt_off;          /* byte offset of the group descriptor table */
    uint64_t vol_sz;           /* blocks_count * block_sz, <= partition size */
    uint32_t block_sz;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t first_data_block;
    uint32_t groups;
    uint32_t incompat;         /* validated feature word (FILETYPE is read) */
    uint16_t inode_sz;
    uint16_t desc_sz;          /* 32, or s_desc_size when INCOMPAT_64BIT */
};

struct fs_volume;
struct fs_file;

/* Backend entry points; same NOFS / UNSUPPORTED split as fat32_mount(). */
int ext4_mount(struct fs_volume *vol);
int ext4_open(struct fs_volume *vol, struct fs_file *f, const char *path,
              uint64_t max_size);
int ext4_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf);
int ext4_label_eq(struct fs_volume *vol, const char *label);

#endif /* WOLFBOOT_EXT4_H */
