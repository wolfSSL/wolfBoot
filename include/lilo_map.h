/* lilo_map.h
 *
 * On-disk format shared by tools/lilo and the x86_64_lilo target.
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

/* Disk layout written by tools/lilo, all of it in front of the first
 * partition (the "MBR gap"):
 *
 *   LBA 0                 MBR: boot code + lilo_mbr_desc + partition table
 *   LBA map_lba           map: lilo_map_hdr sector, then the entries
 *   LBA stage_lba         wolfBoot (wolfboot.bin minus its first sector)
 *
 * Unlike the original LILO map, entries name files on a FAT32 partition
 * instead of listing their sectors, so replacing a kernel file does not
 * invalidate the map. All fields are little-endian.
 */

#ifndef WOLFBOOT_LILO_MAP_H
#define WOLFBOOT_LILO_MAP_H

#include <stdint.h>

#define LILO_SECTOR_SIZE        512

/* Boot descriptor inside the MBR, between the code and the disk signature. */
#define LILO_MBR_DESC_OFFSET    0x190
#define LILO_MBR_DESC_MAGIC     0x4C49574CUL /* "LWIL" */
#define LILO_MBR_SIG_OFFSET     0x1B8        /* NT disk signature */
#define LILO_MBR_PTABLE_OFFSET  0x1BE

struct lilo_mbr_desc {
    uint32_t magic;
    uint16_t stage_sectors; /* set by the linker */
    uint16_t map_sectors;   /* the rest is set by tools/lilo */
    uint64_t stage_lba;
    uint64_t map_lba;
} __attribute__((packed));

#define LILO_MAP_MAGIC          0x50414D4CUL /* "LMAP" */
#define LILO_MAP_VERSION        1
#define LILO_MAP_MAX_ENTRIES    16
#define LILO_LABEL_LEN          16  /* LILO's limit: 15 characters */
#define LILO_PATH_LEN           128 /* WOLFBOOT_FS_MAX_PATH */
#define LILO_CMDLINE_LEN        704
#define LILO_ENTRY_SIZE         1024
#define LILO_MAP_SECTORS \
    (1 + (LILO_MAP_MAX_ENTRIES * LILO_ENTRY_SIZE) / LILO_SECTOR_SIZE)

/* lilo_map_hdr.flags */
#define LILO_MAP_PROMPT         0x0001
#define LILO_MAP_SERIAL         0x0002

/* lilo_map_hdr.timeout: no timeout, wait at the prompt forever */
#define LILO_TIMEOUT_NONE       0xFFFF

/* lilo_map_entry.type */
#define LILO_ENTRY_LINUX        1
#define LILO_ENTRY_OTHER        2   /* chain-load a partition boot sector */

/* lilo_map_entry.vga, as in the kernel boot protocol vid_mode field */
#define LILO_VGA_DEFAULT        0   /* leave the kernel's own default */
#define LILO_VGA_NORMAL         0xFFFF
#define LILO_VGA_EXTENDED       0xFFFE
#define LILO_VGA_ASK            0xFFFD

struct lilo_map_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t n_entries;
    uint16_t default_entry;
    uint16_t flags;
    uint16_t timeout;       /* tenths of a second at the prompt */
    uint16_t delay;         /* tenths of a second to wait for a shift key */
    uint16_t serial_port;   /* 0..3: COM1..COM4 */
    uint16_t entry_size;
    uint32_t serial_baud;
    uint32_t crc32;         /* over the header (crc32 = 0) and the entries */
    uint8_t  reserved[LILO_SECTOR_SIZE - 28];
} __attribute__((packed));

struct lilo_map_entry {
    char     label[LILO_LABEL_LEN];
    char     alias[LILO_LABEL_LEN];
    uint8_t  type;
    uint8_t  part;          /* LINUX: FAT32 partition index (0-based)
                             * OTHER: MBR partition slot (1..4) */
    uint16_t vga;
    uint32_t reserved0;
    char     kernel[LILO_PATH_LEN];
    char     initrd[LILO_PATH_LEN];
    char     cmdline[LILO_CMDLINE_LEN];
    uint8_t  reserved1[LILO_ENTRY_SIZE - (2 * LILO_LABEL_LEN) - 8 -
                       (2 * LILO_PATH_LEN) - LILO_CMDLINE_LEN];
} __attribute__((packed));

#endif /* WOLFBOOT_LILO_MAP_H */
