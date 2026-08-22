/* disk_fs.h
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

/* Read-only filesystem layer for wolfBoot disk boot.
 *
 * Sits between the partition layer (disk_part_read(), src/disk.c) and the
 * disk loader (src/update_disk.c), so a boot slot can name a file instead
 * of requiring the signed image at raw offset 0 of a partition.
 *
 * Supported: FAT32 (with VFAT long filenames) and ext4 (extent-mapped
 * files only). A partition holding neither is presented as a single file
 * spanning the whole partition (FS_TYPE_RAW), which is exactly the
 * pre-existing behaviour.
 *
 * SECURITY: every byte this layer parses is attacker-controlled and is
 * read BEFORE the image signature is verified. All metadata is validated
 * before use, and every loop that follows on-media links is bounded. See
 * the WOLFBOOT_FS_MAX_* knobs below.
 *
 * Compile with DISK_FS=fat32|ext4|both.
 */
#ifndef WOLFBOOT_DISK_FS_H
#define WOLFBOOT_DISK_FS_H

#include <stdint.h>
#include "gpt.h"

/* Per-filesystem on-disk layout and backend interfaces. Only the parsers
 * selected by DISK_FS are compiled, so each is included on its own. */
#ifdef WOLFBOOT_FAT32
#include "fat32.h"
#endif
#ifdef WOLFBOOT_EXT4
#include "ext4.h"
#endif

/* error codes */
#define WOLFBOOT_FS_OK              0
#define WOLFBOOT_FS_E_PARAM        -1  /* bad argument / path too long */
#define WOLFBOOT_FS_E_IO           -2  /* disk read failed or came up short */
#define WOLFBOOT_FS_E_NOFS         -3  /* nothing recognizable: use raw */
#define WOLFBOOT_FS_E_FORMAT       -4  /* metadata failed validation */
#define WOLFBOOT_FS_E_UNSUPPORTED  -5  /* valid filesystem, feature refused */
#define WOLFBOOT_FS_E_NOENT        -6  /* path component not found */
#define WOLFBOOT_FS_E_NOTFILE      -7  /* found, but not a regular file */
#define WOLFBOOT_FS_E_TOOBIG       -8  /* size exceeds the caller's max */
#define WOLFBOOT_FS_E_CORRUPT      -9  /* a security bound tripped */
#define WOLFBOOT_FS_E_RANGE       -10  /* offset arithmetic out of range */

/* filesystem types */
#define FS_TYPE_NONE   0
#define FS_TYPE_RAW    1
#define FS_TYPE_FAT32  2
#define FS_TYPE_EXT4   3

/* Metadata cache size. Holds ONLY metadata (FAT entries, directory
 * sectors, superblocks, group descriptors, inodes, extent nodes); bulk file
 * data streams straight to the caller's buffer. Raising it cuts disk reads
 * on a heavily fragmented file. */
#ifndef WOLFBOOT_FS_CACHE_SIZE
#define WOLFBOOT_FS_CACHE_SIZE 512
#endif
#if (WOLFBOOT_FS_CACHE_SIZE < 512) || ((WOLFBOOT_FS_CACHE_SIZE % 512) != 0)
#error "WOLFBOOT_FS_CACHE_SIZE must be a multiple of 512, minimum 512"
#endif

/* Security bounds. Each of these exists to make an on-media structure that
 * can reference other on-media structures terminate in bounded time even
 * when it is crafted. Each has a dedicated negative unit test. */

/* Longest absolute path accepted by fs_open(). */
#ifndef WOLFBOOT_FS_MAX_PATH
#define WOLFBOOT_FS_MAX_PATH 128
#endif
/* Maximum number of '/'-separated components in a path. */
#ifndef WOLFBOOT_FS_MAX_PATH_DEPTH
#define WOLFBOOT_FS_MAX_PATH_DEPTH 8
#endif
/* Longest single filename accepted. Names longer than this can never match
 * a configured path component, so they are skipped rather than assembled. */
#ifndef WOLFBOOT_FS_MAX_NAME
#define WOLFBOOT_FS_MAX_NAME 64
#endif
/* Maximum directory entries scanned before declaring the directory bad. */
#ifndef WOLFBOOT_FS_MAX_DIR_ENTRIES
#define WOLFBOOT_FS_MAX_DIR_ENTRIES 4096
#endif
/* Maximum blocks/clusters walked while scanning one directory. */
#ifndef WOLFBOOT_FS_MAX_DIR_BLOCKS
#define WOLFBOOT_FS_MAX_DIR_BLOCKS 1024
#endif
/* Absolute stop for a FAT cluster chain walk, independent of file size and
 * of volume geometry. */
#ifndef WOLFBOOT_FS_MAX_CHAIN
#define WOLFBOOT_FS_MAX_CHAIN 1048576
#endif
/* Maximum ext4 extent tree depth. This is ext4's own architectural limit;
 * the walk additionally requires depth to decrease by exactly one at every
 * level, which is what makes the descent provably finite. */
#ifndef WOLFBOOT_FS_EXT4_MAX_DEPTH
#define WOLFBOOT_FS_EXT4_MAX_DEPTH 5
#endif



/* A mounted partition. FS_TYPE_RAW is the identity filesystem: the whole
 * partition is one file and fs_read() is a direct disk_part_read(), which is
 * how a DISK_FS build stays byte-identical to one without it on a partition
 * holding no filesystem. */
struct fs_volume {
    uint64_t part_sz;     /* partition size in bytes; bounds all arithmetic */
    int      drv;
    int      part;
    int      type;        /* FS_TYPE_* */
    union {
#ifdef WOLFBOOT_FAT32
        struct fat32_volume fat;
#endif
#ifdef WOLFBOOT_EXT4
        struct ext4_volume  ext;
#endif
        /* Keeps the union non-empty for a RAW-only build. */
        uint8_t raw;
    } u;
};

/* An open file. The forward-only cursor makes a sequential load one linear
 * metadata walk rather than one walk per read. */
struct fs_file {
    struct fs_volume *vol;
    uint64_t size;        /* validated <= partition size and <= max_size */
    uint64_t cur_off;     /* file offset at which the cursor position begins */
    uint32_t first_clus;  /* FAT32: first cluster of the file */
    uint32_t cur_clus;    /* FAT32: cluster covering cur_off */
    uint8_t  iblock[60];  /* ext4: raw i_block[], the extent tree root node */
};

/**
 * @brief Probe a partition and populate a volume descriptor.
 *
 * Order: wolfBoot magic at offset 0 (short-circuits to RAW), FAT32, ext4,
 * else RAW. An UNRECOGNIZED partition is not an error - it becomes RAW, so
 * raw boot behaviour is preserved. A partition that IS a filesystem we
 * refuse (FAT12/16, ext2/3, an unsupported ext4 feature) returns
 * WOLFBOOT_FS_E_UNSUPPORTED. The distinction is deliberate: falling back to
 * raw on a real filesystem would parse a boot sector as an image header.
 */
int fs_mount(struct fs_volume *vol, int drv, int part);

/**
 * @brief Resolve an absolute path to a regular file. RAW ignores the path
 *        and opens the whole partition.
 *
 * @param max_size Upper bound on file size. The filesystem-reported size is
 *                 attacker-controlled and is checked against this before it
 *                 drives any I/O.
 */
int fs_open(struct fs_volume *vol, struct fs_file *f, const char *path,
            uint64_t max_size);

/* Read from an open file. Matches disk_part_read()'s contract - returns
 * bytes read (may be short at EOF), or negative WOLFBOOT_FS_E_* - so callers
 * can switch between a file and a raw partition unchanged. */
int fs_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf);

/* Size in bytes of an open file. */
uint64_t fs_size(const struct fs_file *f);

/* Human-readable volume type, for boot logs. */
const char *fs_type_name(const struct fs_volume *vol);

/* Compare a volume's label (FAT32 BS_VolLab / ext4 s_volume_name) against
 * an ASCII string, so a partition can be selected by name on MBR disks where
 * GPT names do not exist. Case-insensitive for FAT32, case-sensitive for
 * ext4, matching each filesystem's convention.
 * Returns 1 match, 0 no match, negative WOLFBOOT_FS_E_* on error. */
int fs_label_eq(struct fs_volume *vol, const char *label);

/* ---------------------------------------------------------------------
 * Internal API. Shared between src/disk_fs.c and the filesystem backends
 * (src/fat32.c, src/ext4.c). Not intended for use outside this layer.
 * --------------------------------------------------------------------- */

/* Byte-wise little-endian accessors. Used in preference to casting packed
 * structs onto a sector buffer, which is only correct on little-endian
 * targets and trips -Wextra on packed-member address-taking. */
int fs_is_pow2(uint32_t v);

/* Deliberately out-of-line, unlike the inline readers in gpt.h. The parsers
 * call these about fifty times between them, and inlining every site costs
 * more code than the calls do. gpt.c and disk.c have only a handful of sites
 * each, so the inline form is the right trade there. */
uint16_t fs_le16(const uint8_t *p);
uint32_t fs_le32(const uint8_t *p);

/* Read exactly len bytes at a partition-relative offset, or fail.
 * disk_part_read() silently CLAMPS to the partition end and returns the
 * clamped count; treating that as success would turn a truncation into a
 * silent wrong read, so every read in this layer goes through here. */
int fs_disk_read_exact(struct fs_volume *v, uint64_t off, uint32_t len,
                       uint8_t *buf);

/* Read metadata through the shared cache, handling straddled windows so
 * callers never deal with alignment. Bulk file data must NOT come through
 * here: it would evict the metadata window and gain nothing. */
int fs_meta_read(struct fs_volume *v, uint64_t off, uint32_t len,
                 uint8_t *buf);

/* Drop the metadata cache. Called on mount, and whenever the underlying
 * media may have changed. */
void fs_cache_invalidate(void);

/* Split the next component off an absolute path: advances *p, writes the
 * component NUL-terminated to out (WOLFBOOT_FS_MAX_NAME + 1 bytes), bumps
 * *depth. Rejects "." and ".." outright - neither is needed to name a boot
 * image, and ".." is the only way a crafted tree could build a traversal
 * cycle. Returns 1 for a component, 0 at end of path, negative on error. */
int fs_path_next(const char **p, char *out, uint32_t *out_len, int *depth);

#endif /* WOLFBOOT_DISK_FS_H */
