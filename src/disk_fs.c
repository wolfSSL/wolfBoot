/* disk_fs.c
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
 * Holds what is common to every backend: probe order, type dispatch, the
 * RAW (no filesystem) backend, the shared metadata cache, the little-endian
 * accessors and the path splitter. The parsers live in src/fat32.c and
 * src/ext4.c.
 *
 * Dispatch is a switch on vol->type, not a function-pointer table: taking
 * the address of a backend entry point defeats --gc-sections, so an
 * ext4-only build would still link the FAT32 code. With a switch the
 * unselected arm is preprocessed away and the linker drops it.
 */

#ifndef _WOLFBOOT_DISK_FS_C_
#define _WOLFBOOT_DISK_FS_C_

#ifdef WOLFBOOT_DISK_FS

#include <stdint.h>
#include <string.h>

#include "disk_fs.h"
#include "disk.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

#ifdef DEBUG_FS
#define FS_DBG(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#define FS_DBG(_f_, ...) do{}while(0)
#endif

/* wolfBoot image header magic as it appears on media. Compared byte-wise,
 * not as a word: WOLFBOOT_MAGIC has the opposite byte order on big-endian
 * builds, so a word compare would only be right on one of them. */
static const uint8_t fs_wolfboot_magic[4] = { 'W', 'O', 'L', 'F' };

/* ---------------------------------------------------------------------
 * Metadata cache: one statically allocated window (several disk targets
 * build with WOLFBOOT_SMALL_STACK=1, so the stack is not an option). Keyed
 * by drive and partition as well as offset - slot A and slot B can be
 * independent partitions, and an offset-only key would hand slot B's reads
 * slot A's cached bytes.
 * --------------------------------------------------------------------- */
static uint8_t  fs_cache[WOLFBOOT_FS_CACHE_SIZE] XALIGNED(4);
static uint64_t fs_cache_off;
static uint32_t fs_cache_len;
static int      fs_cache_drv;
static int      fs_cache_part;
static int      fs_cache_valid;

void fs_cache_invalidate(void)
{
    fs_cache_valid = 0;
    fs_cache_off = 0;
    fs_cache_len = 0;
    fs_cache_drv = -1;
    fs_cache_part = -1;
}

/* ---------------------------------------------------------------------
 * Little-endian accessors
 * --------------------------------------------------------------------- */

int fs_is_pow2(uint32_t v)
{
    return ((v != 0U) && ((v & (v - 1U)) == 0U)) ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * Bounded disk access
 * --------------------------------------------------------------------- */

uint16_t fs_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t fs_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int fs_disk_read_exact(struct fs_volume *v, uint64_t off, uint32_t len,
                       uint8_t *buf)
{
    int ret;

    if ((v == NULL) || (buf == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    if (len == 0U) {
        return WOLFBOOT_FS_OK;
    }
    /* Bound against the partition before issuing the read, so an offset
     * derived from crafted metadata is rejected here rather than being
     * quietly truncated by disk_part_read(). */
    if ((off > v->part_sz) || ((uint64_t)len > (v->part_sz - off))) {
        FS_DBG("fs: read out of range off %x_%x len %u part %x_%x\r\n",
            (uint32_t)(off >> 32), (uint32_t)off, len,
            (uint32_t)(v->part_sz >> 32), (uint32_t)v->part_sz);
        return WOLFBOOT_FS_E_RANGE;
    }
    ret = disk_part_read(v->drv, v->part, off, (uint64_t)len, buf);
    if (ret < 0) {
        return WOLFBOOT_FS_E_IO;
    }
    /* disk_part_read() clamps to the partition end and reports the clamped
     * count. Anything short of what was asked for is an I/O error here. */
    if ((uint32_t)ret != len) {
        FS_DBG("fs: short read %d of %u\r\n", ret, len);
        return WOLFBOOT_FS_E_IO;
    }
    return WOLFBOOT_FS_OK;
}

int fs_meta_read(struct fs_volume *v, uint64_t off, uint32_t len,
                 uint8_t *buf)
{
    uint64_t pos, win, avail;
    uint32_t in_win, chunk, rdlen, done;
    int ret;

    if ((v == NULL) || (buf == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    if (len == 0U) {
        return WOLFBOOT_FS_OK;
    }
    if ((off > v->part_sz) || ((uint64_t)len > (v->part_sz - off))) {
        return WOLFBOOT_FS_E_RANGE;
    }

    done = 0U;
    while (done < len) {
        pos = off + (uint64_t)done;
        win = pos - (pos % (uint64_t)WOLFBOOT_FS_CACHE_SIZE);

        if ((fs_cache_valid == 0) || (fs_cache_off != win) ||
                (fs_cache_drv != v->drv) || (fs_cache_part != v->part)) {
            avail = v->part_sz - win;
            rdlen = WOLFBOOT_FS_CACHE_SIZE;
            if ((uint64_t)rdlen > avail) {
                rdlen = (uint32_t)avail;
            }
            fs_cache_valid = 0;
            ret = fs_disk_read_exact(v, win, rdlen, fs_cache);
            if (ret != WOLFBOOT_FS_OK) {
                return ret;
            }
            fs_cache_off = win;
            fs_cache_len = rdlen;
            fs_cache_drv = v->drv;
            fs_cache_part = v->part;
            fs_cache_valid = 1;
        }

        in_win = (uint32_t)(pos - win);
        if (in_win >= fs_cache_len) {
            return WOLFBOOT_FS_E_RANGE;
        }
        chunk = fs_cache_len - in_win;
        if (chunk > (len - done)) {
            chunk = len - done;
        }
        memcpy(buf + done, fs_cache + in_win, chunk);
        done += chunk;
    }
    return WOLFBOOT_FS_OK;
}

/* ---------------------------------------------------------------------
 * Path handling
 * --------------------------------------------------------------------- */

int fs_path_next(const char **p, char *out, uint32_t *out_len, int *depth)
{
    const char *s;
    uint32_t n;

    if ((p == NULL) || (*p == NULL) || (out == NULL) || (out_len == NULL) ||
            (depth == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    s = *p;

    /* Skip separators. Repeated slashes are harmless and are collapsed. */
    while (*s == '/') {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return 0;
    }

    if (*depth >= WOLFBOOT_FS_MAX_PATH_DEPTH) {
        FS_DBG("fs: path too deep\r\n");
        return WOLFBOOT_FS_E_PARAM;
    }

    n = 0U;
    while ((*s != '/') && (*s != '\0')) {
        /* A backslash is an ordinary filename character on both FAT and
         * ext4. Treating it as a separator here would let a crafted name
         * be read as two components. */
        if (n >= WOLFBOOT_FS_MAX_NAME) {
            FS_DBG("fs: path component too long\r\n");
            return WOLFBOOT_FS_E_PARAM;
        }
        out[n] = *s;
        n++;
        s++;
    }
    out[n] = '\0';

    /* "." and ".." are never needed to name a boot image, and ".." is the
     * only way a crafted directory tree could build a traversal cycle. */
    if ((n == 1U) && (out[0] == '.')) {
        return WOLFBOOT_FS_E_PARAM;
    }
    if ((n == 2U) && (out[0] == '.') && (out[1] == '.')) {
        return WOLFBOOT_FS_E_PARAM;
    }

    *out_len = n;
    *p = s;
    (*depth)++;
    return 1;
}

/* ---------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

int fs_mount(struct fs_volume *vol, int drv, int part)
{
    uint8_t magic[4];
    uint64_t sz = 0;
    int ret;

    if (vol == NULL) {
        return WOLFBOOT_FS_E_PARAM;
    }
    memset(vol, 0, sizeof(*vol));
    vol->drv = drv;
    vol->part = part;
    vol->type = FS_TYPE_NONE;

    if (disk_part_size(drv, part, &sz) != 0) {
        return WOLFBOOT_FS_E_IO;
    }
    if (sz == 0U) {
        return WOLFBOOT_FS_E_IO;
    }
    vol->part_sz = sz;
    fs_cache_invalidate();

    /* A partition that starts with a wolfBoot image header is a raw slot.
     * Short-circuiting here means an existing raw layout is never handed
     * to a filesystem probe at all. */
    if (sz >= 4U) {
        ret = fs_meta_read(vol, 0, 4, magic);
        if ((ret == WOLFBOOT_FS_OK) &&
                (memcmp(magic, fs_wolfboot_magic, 4) == 0)) {
            vol->type = FS_TYPE_RAW;
            FS_DBG("fs: drv %d part %d: wolfBoot image at offset 0\r\n",
                drv, part);
            return WOLFBOOT_FS_OK;
        }
    }

#ifdef WOLFBOOT_FAT32
    ret = fat32_mount(vol);
    if (ret == WOLFBOOT_FS_OK) {
        vol->type = FS_TYPE_FAT32;
        return WOLFBOOT_FS_OK;
    }
    /* Anything other than "this is not FAT" stops the probe. A volume we
     * recognize but refuse must not fall through to raw, where its boot
     * sector would be parsed as an image header. */
    if (ret != WOLFBOOT_FS_E_NOFS) {
        return ret;
    }
#endif

#ifdef WOLFBOOT_EXT4
    ret = ext4_mount(vol);
    if (ret == WOLFBOOT_FS_OK) {
        vol->type = FS_TYPE_EXT4;
        return WOLFBOOT_FS_OK;
    }
    if (ret != WOLFBOOT_FS_E_NOFS) {
        return ret;
    }
#endif

    vol->type = FS_TYPE_RAW;
    FS_DBG("fs: drv %d part %d: no filesystem, using raw\r\n", drv, part);
    return WOLFBOOT_FS_OK;
}

int fs_open(struct fs_volume *vol, struct fs_file *f, const char *path,
            uint64_t max_size)
{
#if defined(WOLFBOOT_FAT32) || defined(WOLFBOOT_EXT4)
    size_t plen;
#endif

    if ((vol == NULL) || (f == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    memset(f, 0, sizeof(*f));
    f->vol = vol;

    if (vol->type == FS_TYPE_RAW) {
        /* The whole partition is the file. No max_size check here: the raw
         * path has always read a header first and bounded the payload from
         * it, and changing that would alter existing behaviour. */
        (void)path;
        (void)max_size;
        f->size = vol->part_sz;
        return WOLFBOOT_FS_OK;
    }

#if defined(WOLFBOOT_FAT32) || defined(WOLFBOOT_EXT4)
    if (path == NULL) {
        return WOLFBOOT_FS_E_PARAM;
    }
    if (path[0] != '/') {
        FS_DBG("fs: path must be absolute\r\n");
        return WOLFBOOT_FS_E_PARAM;
    }
    plen = strlen(path);
    if (plen > (size_t)WOLFBOOT_FS_MAX_PATH) {
        FS_DBG("fs: path too long\r\n");
        return WOLFBOOT_FS_E_PARAM;
    }
#endif

#ifdef WOLFBOOT_FAT32
    if (vol->type == FS_TYPE_FAT32) {
        return fat32_open(vol, f, path, max_size);
    }
#endif
#ifdef WOLFBOOT_EXT4
    if (vol->type == FS_TYPE_EXT4) {
        return ext4_open(vol, f, path, max_size);
    }
#endif
    return WOLFBOOT_FS_E_PARAM;
}

int fs_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf)
{
    int ret;

    if ((f == NULL) || (f->vol == NULL) || (buf == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    if (len == 0U) {
        return 0;
    }
    if (off >= f->size) {
        return 0;
    }
    if (len > (f->size - off)) {
        len = f->size - off;
    }
    if (len > (uint64_t)DISK_IO_MAX_SIZE) {
        len = (uint64_t)DISK_IO_MAX_SIZE;
    }

    switch (f->vol->type) {
        case FS_TYPE_RAW:
            ret = disk_part_read(f->vol->drv, f->vol->part, off, len, buf);
            break;
#ifdef WOLFBOOT_FAT32
        case FS_TYPE_FAT32:
            ret = fat32_read(f, off, len, buf);
            break;
#endif
#ifdef WOLFBOOT_EXT4
        case FS_TYPE_EXT4:
            ret = ext4_read(f, off, len, buf);
            break;
#endif
        default:
            ret = WOLFBOOT_FS_E_PARAM;
            break;
    }
    return ret;
}

uint64_t fs_size(const struct fs_file *f)
{
    if (f == NULL) {
        return 0;
    }
    return f->size;
}

const char *fs_type_name(const struct fs_volume *vol)
{
    if (vol == NULL) {
        return "none";
    }
    switch (vol->type) {
        case FS_TYPE_RAW:
            return "raw";
        case FS_TYPE_FAT32:
            return "fat32";
        case FS_TYPE_EXT4:
            return "ext4";
        default:
            return "none";
    }
}

int fs_label_eq(struct fs_volume *vol, const char *label)
{
    if ((vol == NULL) || (label == NULL)) {
        return WOLFBOOT_FS_E_PARAM;
    }
    switch (vol->type) {
#ifdef WOLFBOOT_FAT32
        case FS_TYPE_FAT32:
            return fat32_label_eq(vol, label);
#endif
#ifdef WOLFBOOT_EXT4
        case FS_TYPE_EXT4:
            return ext4_label_eq(vol, label);
#endif
        default:
            /* A raw partition has no filesystem label. */
            return 0;
    }
}

#endif /* WOLFBOOT_DISK_FS */

#endif /* _WOLFBOOT_DISK_FS_C_ */
