/* fat32.c
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

/* Read-only FAT32 backend for the wolfBoot disk filesystem layer.
 *
 * Supports FAT32 only, with VFAT long filename reads. FAT12 and FAT16 are
 * refused rather than misread, as are volumes with a logical sector size
 * other than 512 (the partition layer assumes 512 throughout).
 *
 * SECURITY: the boot sector, the FAT and the directory entries are all
 * attacker-controlled and are parsed before the image signature is
 * checked. Every geometry field is validated at mount time so that later
 * cluster-to-byte arithmetic cannot leave the partition, and every walk
 * over an on-media link is bounded so a crafted cycle terminates.
 */

#ifndef _WOLFBOOT_FAT32_C_
#define _WOLFBOOT_FAT32_C_

#if defined(WOLFBOOT_DISK_FS) && defined(WOLFBOOT_FAT32)

#include <stdint.h>
#include <string.h>

#include "disk_fs.h"
#include "fat32.h"
#include "disk.h"
#include "printf.h"

#ifdef DEBUG_FS
#define FAT_DBG(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#define FAT_DBG(_f_, ...) do{}while(0)
#endif

/* Accumulator for one long filename set. LFN entries precede their short
 * entry in reverse ordinal order, so the set is assembled as it is walked
 * and only committed once the short entry passes the checksum test. */
struct fat_lfn_state {
    char    name[WOLFBOOT_FS_MAX_NAME + 1];
    uint8_t expect_ord;
    uint8_t cksum;
    uint8_t have;
};

/* ---------------------------------------------------------------------
 * Name handling
 * --------------------------------------------------------------------- */

static char fat32_upper(char c)
{
    if ((c >= 'a') && (c <= 'z')) {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

/* FAT filenames are case-insensitive. */
static int fat32_name_eq(const char *a, const char *b)
{
    uint32_t i = 0;

    while ((a[i] != '\0') && (b[i] != '\0')) {
        if (fat32_upper(a[i]) != fat32_upper(b[i])) {
            return 0;
        }
        i++;
        if (i > WOLFBOOT_FS_MAX_NAME) {
            return 0;
        }
    }
    return (a[i] == b[i]) ? 1 : 0;
}

/* Rotate-and-add checksum of an 8.3 name, per the FAT spec. This is the
 * ONLY thing binding an LFN set to the short entry after it: unverified, a
 * crafted directory could pair an attacker-chosen long name with an
 * unrelated file's cluster and size, opening a different file than the one
 * configured - before any signature check runs. */
static uint8_t fat32_sfn_checksum(const uint8_t *sfn)
{
    uint8_t sum = 0;
    uint32_t i;

    for (i = 0; i < FAT_SFN_LEN; i++) {
        sum = (uint8_t)((uint8_t)((sum >> 1) | (uint8_t)(sum << 7)) + sfn[i]);
    }
    return sum;
}

/* Render the 8.3 name of a directory entry as "NAME.EXT". */
static void fat32_sfn_to_name(const uint8_t *e, char *out)
{
    uint32_t i;
    uint32_t n = 0;
    uint8_t c;

    for (i = 0; i < FAT_SFN_NAME_LEN; i++) {
        c = e[i];
        if (c == ' ') {
            break;
        }
        /* 0x05 stands in for a leading 0xE5, which marks a free entry. */
        if ((i == 0U) && (c == FAT_DIR_KANJI_E5)) {
            c = FAT_DIR_FREE;
        }
        out[n] = (char)c;
        n++;
    }
    if (e[FAT_SFN_NAME_LEN] != ' ') {
        out[n] = '.';
        n++;
        for (i = FAT_SFN_NAME_LEN; i < FAT_SFN_LEN; i++) {
            c = e[i];
            if (c == ' ') {
                break;
            }
            out[n] = (char)c;
            n++;
        }
    }
    out[n] = '\0';
}

/* Fold one LFN entry into the accumulator. Any inconsistency (bad or
 * out-of-sequence ordinal, changed checksum, non-ASCII, over-long name)
 * discards the set. Not fatal: the entry just becomes unmatchable by its
 * long name and the short name is still tried. */
static void fat32_lfn_accum(struct fat_lfn_state *s, const uint8_t *e)
{
    /* Byte offsets of the 13 UCS-2 units within a long filename entry. */
    static const uint8_t chr_off[FAT_LFN_CHARS] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    uint8_t ord = (uint8_t)(e[0] & FAT_LFN_ORD_MASK);
    uint32_t base;
    uint32_t i;
    uint16_t uc;

    if ((e[0] & FAT_LFN_LAST) != 0U) {
        memset(s, 0, sizeof(*s));
        if ((ord == 0U) || (ord > FAT_LFN_MAX_ORD)) {
            return;
        }
        /* Drop a set whose FIRST character already lies past the buffer:
         * that name can never match a configured path component.
         *
         * The bound is on the first index, not ord * FAT_LFN_CHARS. The last
         * entry holds 1 to 13 characters, so multiplying overestimates the
         * length by up to 12 and would discard every 5-entry set, i.e. every
         * name of 53 to 64 characters, even though those fit. The
         * per-character check below rejects the ones that genuinely do not. */
        if ((((uint32_t)ord - 1U) * FAT_LFN_CHARS) >=
                (uint32_t)WOLFBOOT_FS_MAX_NAME) {
            return;
        }
        s->cksum = e[FAT_LFN_CKSUM_OFF];
        s->expect_ord = ord;
        s->have = 1;
    }
    else {
        if ((s->have == 0U) || (ord != s->expect_ord) ||
                (e[FAT_LFN_CKSUM_OFF] != s->cksum)) {
            memset(s, 0, sizeof(*s));
            return;
        }
    }

    base = ((uint32_t)ord - 1U) * FAT_LFN_CHARS;
    for (i = 0; i < FAT_LFN_CHARS; i++) {
        uc = fs_le16(e + chr_off[i]);
        if ((uc == 0x0000U) || (uc == 0xFFFFU)) {
            break;
        }
        /* Configured paths are ASCII, so a name that is not cannot match.
         * Dropping the set avoids having to define a lossy mapping. */
        if (uc > 0x7FU) {
            memset(s, 0, sizeof(*s));
            return;
        }
        if ((base + i) >= (uint32_t)WOLFBOOT_FS_MAX_NAME) {
            memset(s, 0, sizeof(*s));
            return;
        }
        s->name[base + i] = (char)uc;
    }
    s->expect_ord--;
}

/* ---------------------------------------------------------------------
 * Mount
 * --------------------------------------------------------------------- */

int fat32_mount(struct fs_volume *vol)
{
    uint8_t bpb[FAT_BPB_SIZE];
    uint8_t sig[2];
    struct fat32_volume *fv;
    uint32_t byts_per_sec, sec_per_clus, rsvd, num_fats;
    uint32_t tot_sec32, fat_sz32, root_clus, clus_count, clus_sz;
    uint16_t ext_flags;
    uint64_t fat_off, fat_sz, data_off, meta_secs, data_secs;
    uint8_t media;
    int ret;

    fv = &vol->u.fat;

    if (vol->part_sz < (uint64_t)FAT_SECTOR_SIZE) {
        return WOLFBOOT_FS_E_NOFS;
    }
    ret = fs_meta_read(vol, 0, FAT_BPB_SIZE, bpb);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    ret = fs_meta_read(vol, FAT_BOOT_SIG_OFF, 2, sig);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    byts_per_sec = fs_le16(bpb + FAT_BPB_BYTS_PER_SEC);
    sec_per_clus = bpb[FAT_BPB_SEC_PER_CLUS];
    rsvd         = fs_le16(bpb + FAT_BPB_RSVD_SEC_CNT);
    num_fats     = bpb[FAT_BPB_NUM_FATS];
    media        = bpb[FAT_BPB_MEDIA];

    /* Stage 1: is this a FAT volume at all? These are the fields every FAT
     * variant shares, so failing any of them means "not FAT" and the probe
     * moves on to the next filesystem rather than reporting an error. */
    if (fs_le16(sig) != FAT_BOOT_SIG_VALUE) {
        return WOLFBOOT_FS_E_NOFS;
    }
    if ((media != 0xF0U) && (media < 0xF8U)) {
        return WOLFBOOT_FS_E_NOFS;
    }
    if ((fs_is_pow2(byts_per_sec) == 0) || (byts_per_sec < 512U) ||
            (byts_per_sec > 4096U)) {
        return WOLFBOOT_FS_E_NOFS;
    }
    if ((num_fats < 1U) || (num_fats > 2U)) {
        return WOLFBOOT_FS_E_NOFS;
    }
    if (rsvd == 0U) {
        return WOLFBOOT_FS_E_NOFS;
    }

    /* Stage 2: it is FAT. From here on a failure is an error, never a
     * fallback to raw -- reading a real boot sector as an image header
     * would be a silent wrong result. */

    if (byts_per_sec != FAT_SECTOR_SIZE) {
        FAT_DBG("fat32: sector size %u unsupported\r\n", byts_per_sec);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    /* A non-zero root entry count, 16-bit total sector count or 16-bit FAT
     * size all mean FAT12/FAT16. */
    if (fs_le16(bpb + FAT_BPB_ROOT_ENT_CNT) != 0U) {
        FAT_DBG("fat32: FAT12/FAT16 volume\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if (fs_le16(bpb + FAT_BPB_TOT_SEC16) != 0U) {
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if (fs_le16(bpb + FAT_BPB_FAT_SZ16) != 0U) {
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if (fs_le16(bpb + FAT_BPB_FS_VER) != 0U) {
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }

    /* With mirroring disabled, only FAT #0 being the active one is safe:
     * otherwise the chain we would follow is a stale copy. */
    ext_flags = fs_le16(bpb + FAT_BPB_EXT_FLAGS);
    if (((ext_flags & FAT_EXT_NO_MIRROR) != 0U) &&
            ((ext_flags & FAT_EXT_ACTIVE_MASK) != 0U)) {
        FAT_DBG("fat32: active FAT is not FAT 0\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }

    if ((fs_is_pow2(sec_per_clus) == 0) || (sec_per_clus > 128U)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    clus_sz = FAT_SECTOR_SIZE * sec_per_clus;
    if (clus_sz > FAT_MAX_CLUS_SIZE) {
        return WOLFBOOT_FS_E_FORMAT;
    }

    tot_sec32 = fs_le32(bpb + FAT_BPB_TOT_SEC32);
    fat_sz32  = fs_le32(bpb + FAT_BPB_FAT_SZ32);
    root_clus = fs_le32(bpb + FAT_BPB_ROOT_CLUS);

    if ((tot_sec32 == 0U) || (fat_sz32 == 0U)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    /* The volume must fit inside its partition; every byte offset derived
     * below is then bounded by construction. */
    if (((uint64_t)tot_sec32 * FAT_SECTOR_SIZE) > vol->part_sz) {
        FAT_DBG("fat32: volume larger than partition\r\n");
        return WOLFBOOT_FS_E_FORMAT;
    }

    fat_off = (uint64_t)rsvd * FAT_SECTOR_SIZE;
    fat_sz  = (uint64_t)fat_sz32 * FAT_SECTOR_SIZE;
    data_off = fat_off + ((uint64_t)num_fats * fat_sz);

    meta_secs = (uint64_t)rsvd + ((uint64_t)num_fats * (uint64_t)fat_sz32);
    if (meta_secs >= (uint64_t)tot_sec32) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    data_secs = (uint64_t)tot_sec32 - meta_secs;
    clus_count = (uint32_t)(data_secs / (uint64_t)sec_per_clus);

    /* Fewer than 65525 data clusters is the definition of FAT16/FAT12.
     * Checking it here makes the "no FAT12/FAT16" guarantee hold even if
     * the individual BPB fields were forged to look like FAT32. */
    if (clus_count < FAT_MIN_CLUSTERS) {
        FAT_DBG("fat32: %u clusters, below the FAT32 minimum\r\n",
            clus_count);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if (clus_count > FAT_CLUS_MAX) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    /* The FAT must be large enough to hold an entry for every cluster.
     * Without this, a chain could name a cluster whose FAT entry lies past
     * the end of the FAT, so the "link" read would be adjacent file data. */
    if (fat_sz < (((uint64_t)clus_count + 2U) * 4U)) {
        FAT_DBG("fat32: FAT too small for %u clusters\r\n", clus_count);
        return WOLFBOOT_FS_E_FORMAT;
    }
    if ((data_off + ((uint64_t)clus_count * (uint64_t)clus_sz)) >
            vol->part_sz) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    if ((root_clus < 2U) || (root_clus > (clus_count + 1U))) {
        return WOLFBOOT_FS_E_FORMAT;
    }

    fv->fat_off = fat_off;
    fv->fat_sz = fat_sz;
    fv->data_off = data_off;
    fv->clus_sz = clus_sz;
    fv->clus_count = clus_count;
    fv->root_clus = root_clus;

    FAT_DBG("fat32: %u clusters of %u bytes, data at %x_%x\r\n",
        clus_count, clus_sz, (uint32_t)(data_off >> 32), (uint32_t)data_off);
    return WOLFBOOT_FS_OK;
}

/* ---------------------------------------------------------------------
 * Cluster chain
 * --------------------------------------------------------------------- */

/* A cluster number is usable only if it names a real data cluster: 0 and 1
 * are reserved, and the volume's clus_count clusters are numbered from 2. */
static int fat32_clus_ok(const struct fat32_volume *fv, uint32_t clus)
{
    return ((clus >= 2U) && (clus <= (fv->clus_count + 1U))) ? 1 : 0;
}

/* Follow one link of a cluster chain. Returns 0 with *next set, 1 at end
 * of chain, or negative WOLFBOOT_FS_E_* on error. */
static int fat32_next_clus(struct fs_volume *vol, uint32_t clus,
                           uint32_t *next)
{
    struct fat32_volume *fv = &vol->u.fat;
    uint8_t ent[4];
    uint64_t off;
    uint32_t val;
    int ret;

    if (fat32_clus_ok(fv, clus) == 0) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    /* clus is bounded by FAT_CLUS_MAX, so this cannot overflow. */
    off = fv->fat_off + ((uint64_t)clus * 4U);
    if ((off + 4U) > (fv->fat_off + fv->fat_sz)) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    ret = fs_meta_read(vol, off, 4, ent);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    val = fs_le32(ent) & FAT_CLUS_MASK;

    if (val >= FAT_CLUS_EOC_MIN) {
        *next = 0;
        return 1;
    }
    if (val == FAT_CLUS_BAD) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    /* Free or reserved, or past the end of the volume: a chain must never
     * point at one of these. */
    if (fat32_clus_ok(fv, val) == 0) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    *next = val;
    return 0;
}

/* Position the cursor on the cluster holding a file offset. Forward-only, so
 * a sequential load walks the chain once; a backwards seek restarts at the
 * head. Bounded three ways: by the clusters the length can occupy, by the
 * volume's cluster count (longer means a cycle), and by an absolute stop. A
 * visited-set is unaffordable at 2^28 clusters. */
static int fat32_seek(struct fs_file *f, uint64_t off)
{
    struct fs_volume *vol = f->vol;
    struct fat32_volume *fv = &vol->u.fat;
    uint64_t target, cur_idx, max_clusters, steps;
    uint32_t next;
    int ret;

    target = off / (uint64_t)fv->clus_sz;

    if ((f->cur_clus < 2U) ||
            (target < (f->cur_off / (uint64_t)fv->clus_sz))) {
        f->cur_clus = f->first_clus;
        f->cur_off = 0;
    }
    if (fat32_clus_ok(fv, f->cur_clus) == 0) {
        return WOLFBOOT_FS_E_CORRUPT;
    }

    max_clusters = (f->size + (uint64_t)fv->clus_sz - 1U) /
        (uint64_t)fv->clus_sz;
    cur_idx = f->cur_off / (uint64_t)fv->clus_sz;
    steps = 0;

    while (cur_idx < target) {
        ret = fat32_next_clus(vol, f->cur_clus, &next);
        if (ret < 0) {
            return ret;
        }
        if (ret == 1) {
            /* The chain ended before the file's declared length. */
            return WOLFBOOT_FS_E_CORRUPT;
        }
        f->cur_clus = next;
        f->cur_off += (uint64_t)fv->clus_sz;
        cur_idx++;
        steps++;
        if ((steps > max_clusters) || (steps > (uint64_t)fv->clus_count) ||
                (steps > (uint64_t)WOLFBOOT_FS_MAX_CHAIN)) {
            FAT_DBG("fat32: cluster chain bound tripped\r\n");
            return WOLFBOOT_FS_E_CORRUPT;
        }
    }
    return WOLFBOOT_FS_OK;
}

/* Walk the whole chain at open time, so a malformed one errors before any
 * payload loads. A cycle never hits end-of-chain, so it overruns the cluster
 * count and trips that bound; no visited-set needed. A chain LONGER than the
 * length needs is fine (preallocating writers do this); only short is an
 * error. */
static int fat32_validate_chain(struct fs_volume *vol, uint32_t first,
                                uint64_t size)
{
    struct fat32_volume *fv = &vol->u.fat;
    uint64_t need, i, limit;
    uint32_t clus = first;
    uint32_t next;
    int ret;

    if (size == 0U) {
        return WOLFBOOT_FS_OK;
    }
    need = (size + (uint64_t)fv->clus_sz - 1U) / (uint64_t)fv->clus_sz;
    if (need > (uint64_t)fv->clus_count) {
        return WOLFBOOT_FS_E_CORRUPT;
    }

    limit = (uint64_t)fv->clus_count;
    if (limit > (uint64_t)WOLFBOOT_FS_MAX_CHAIN) {
        limit = (uint64_t)WOLFBOOT_FS_MAX_CHAIN;
    }

    for (i = 1; i <= limit; i++) {
        ret = fat32_next_clus(vol, clus, &next);
        if (ret < 0) {
            return ret;
        }
        if (ret == 1) {
            /* The chain has i clusters. Fewer than the declared length
             * needs means the directory entry and the FAT disagree. */
            if (i < need) {
                FAT_DBG("fat32: chain shorter than file size\r\n");
                return WOLFBOOT_FS_E_CORRUPT;
            }
            return WOLFBOOT_FS_OK;
        }
        clus = next;
    }
    FAT_DBG("fat32: cluster chain never terminates\r\n");
    return WOLFBOOT_FS_E_CORRUPT;
}

/* ---------------------------------------------------------------------
 * Directory lookup
 * --------------------------------------------------------------------- */

static int fat32_lookup(struct fs_volume *vol, uint32_t dir_clus,
                        const char *name, uint32_t *out_clus,
                        uint32_t *out_size, uint8_t *out_attr)
{
    struct fat32_volume *fv = &vol->u.fat;
    struct fat_lfn_state lfn;
    uint8_t e[FAT_DIR_ENTRY_SIZE];
    char sfn[FAT_SFN_MAX + 1];
    uint64_t base;
    uint32_t clus = dir_clus;
    uint32_t blocks = 0;
    uint32_t entries = 0;
    uint32_t ents_per_clus;
    uint32_t idx;
    uint32_t next;
    uint8_t attr;
    int matched;
    int ret;

    ents_per_clus = fv->clus_sz / FAT_DIR_ENTRY_SIZE;
    memset(&lfn, 0, sizeof(lfn));

    for (;;) {
        /* A directory chain has no declared length, so it is bounded by
         * the volume's cluster count (anything longer is a cycle) and by
         * the configurable block cap. */
        if ((blocks >= (uint32_t)WOLFBOOT_FS_MAX_DIR_BLOCKS) ||
                (blocks >= fv->clus_count)) {
            FAT_DBG("fat32: directory chain bound tripped\r\n");
            return WOLFBOOT_FS_E_CORRUPT;
        }
        if (fat32_clus_ok(fv, clus) == 0) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
        base = fv->data_off + (((uint64_t)clus - 2U) * (uint64_t)fv->clus_sz);

        for (idx = 0; idx < ents_per_clus; idx++) {
            if (entries >= (uint32_t)WOLFBOOT_FS_MAX_DIR_ENTRIES) {
                FAT_DBG("fat32: directory entry bound tripped\r\n");
                return WOLFBOOT_FS_E_CORRUPT;
            }
            entries++;

            ret = fs_meta_read(vol,
                base + ((uint64_t)idx * FAT_DIR_ENTRY_SIZE),
                FAT_DIR_ENTRY_SIZE, e);
            if (ret != WOLFBOOT_FS_OK) {
                return ret;
            }

            if (e[0] == FAT_DIR_END) {
                return WOLFBOOT_FS_E_NOENT;
            }
            if (e[0] == FAT_DIR_FREE) {
                memset(&lfn, 0, sizeof(lfn));
                continue;
            }

            attr = e[FAT_DIR_ATTR];
            if ((attr & FAT_ATTR_LFN_MASK) == FAT_ATTR_LFN) {
                fat32_lfn_accum(&lfn, e);
                continue;
            }
            /* The volume label lives in a directory entry but is not a
             * name any path can refer to. */
            if ((attr & FAT_ATTR_VOLUME_ID) != 0U) {
                memset(&lfn, 0, sizeof(lfn));
                continue;
            }

            matched = 0;
            /* A complete long name set, whose checksum matches this short
             * entry, is the file's real name. */
            if ((lfn.have != 0U) && (lfn.expect_ord == 0U) &&
                    (fat32_sfn_checksum(e) == lfn.cksum)) {
                matched = fat32_name_eq(lfn.name, name);
            }
            /* The accumulator must never survive a short entry, or a
             * discarded set could be paired with the following file. */
            memset(&lfn, 0, sizeof(lfn));

            if (matched == 0) {
                fat32_sfn_to_name(e, sfn);
                matched = fat32_name_eq(sfn, name);
            }
            if (matched == 0) {
                continue;
            }

            *out_attr = attr;
            *out_clus = ((uint32_t)fs_le16(e + FAT_DIR_FST_CLUS_HI) << 16) |
                        (uint32_t)fs_le16(e + FAT_DIR_FST_CLUS_LO);
            *out_size = fs_le32(e + FAT_DIR_FILE_SIZE);
            return WOLFBOOT_FS_OK;
        }

        blocks++;
        ret = fat32_next_clus(vol, clus, &next);
        if (ret < 0) {
            return ret;
        }
        if (ret == 1) {
            return WOLFBOOT_FS_E_NOENT;
        }
        clus = next;
    }
}

/* ---------------------------------------------------------------------
 * Public backend entry points
 * --------------------------------------------------------------------- */

int fat32_open(struct fs_volume *vol, struct fs_file *f, const char *path,
               uint64_t max_size)
{
    struct fat32_volume *fv = &vol->u.fat;
    char comp[WOLFBOOT_FS_MAX_NAME + 1];
    const char *p = path;
    uint32_t comp_len;
    uint32_t clus;
    uint32_t ent_clus = 0;
    uint32_t ent_size = 0;
    uint8_t attr = 0;
    int depth = 0;
    int have = 0;
    int ret;

    clus = fv->root_clus;

    for (;;) {
        ret = fs_path_next(&p, comp, &comp_len, &depth);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            break;
        }
        if (have != 0) {
            /* Descend into whatever the previous component named. */
            if ((attr & FAT_ATTR_DIRECTORY) == 0U) {
                return WOLFBOOT_FS_E_NOTFILE;
            }
            if (fat32_clus_ok(fv, ent_clus) == 0) {
                return WOLFBOOT_FS_E_CORRUPT;
            }
            clus = ent_clus;
        }
        ret = fat32_lookup(vol, clus, comp, &ent_clus, &ent_size, &attr);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        have = 1;
    }

    if (have == 0) {
        /* The path named the root directory itself. */
        return WOLFBOOT_FS_E_NOTFILE;
    }
    if ((attr & (FAT_ATTR_DIRECTORY | FAT_ATTR_VOLUME_ID)) != 0U) {
        return WOLFBOOT_FS_E_NOTFILE;
    }
    /* A file cannot be larger than the partition holding it. */
    if ((uint64_t)ent_size > vol->part_sz) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    /* The directory-reported size is attacker-controlled and is about to
     * bound a load into RAM, so it is checked before any of it is used. */
    if ((uint64_t)ent_size > max_size) {
        FAT_DBG("fat32: file size %u exceeds max\r\n", ent_size);
        return WOLFBOOT_FS_E_TOOBIG;
    }
    if (ent_size > 0U) {
        if (fat32_clus_ok(fv, ent_clus) == 0) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
    }

    ret = fat32_validate_chain(vol, ent_clus, (uint64_t)ent_size);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    f->size = (uint64_t)ent_size;
    f->first_clus = ent_clus;
    f->cur_clus = ent_clus;
    f->cur_off = 0;
    return WOLFBOOT_FS_OK;
}

int fat32_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf)
{
    struct fs_volume *vol = f->vol;
    struct fat32_volume *fv = &vol->u.fat;
    uint64_t done = 0;
    uint64_t byte_off, avail, this_len;
    uint32_t in_clus, run, clus, next;
    int ret;

    while (done < len) {
        ret = fat32_seek(f, off + done);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        clus = f->cur_clus;
        in_clus = (uint32_t)((off + done) % (uint64_t)fv->clus_sz);

        /* Coalesce physically adjacent clusters into a single media read.
         * An unfragmented file then loads with the same number of disk
         * transfers as a raw partition would, which matters on the slower
         * SD targets where a per-cluster read would dominate boot time. */
        run = 1;
        while ((((uint64_t)run * fv->clus_sz) - in_clus) < (len - done)) {
            if (((uint64_t)clus + run) > ((uint64_t)fv->clus_count + 1U)) {
                break;
            }
            ret = fat32_next_clus(vol, clus + run - 1U, &next);
            if (ret != 0) {
                /* End of chain, or a bad link. Either way stop extending
                 * the run; a real error is reported by the next seek. */
                break;
            }
            if (next != (clus + run)) {
                break;
            }
            run++;
        }

        avail = ((uint64_t)run * fv->clus_sz) - in_clus;
        this_len = len - done;
        if (this_len > avail) {
            this_len = avail;
        }
        if (this_len > (uint64_t)DISK_IO_MAX_SIZE) {
            this_len = (uint64_t)DISK_IO_MAX_SIZE;
        }

        byte_off = fv->data_off +
            (((uint64_t)clus - 2U) * (uint64_t)fv->clus_sz) + in_clus;
        /* Defence in depth: mount validated the geometry, so this cannot
         * fire, but the cost of being wrong here is an out-of-partition
         * read straight into the RAM load region. */
        if ((byte_off > vol->part_sz) ||
                (this_len > (vol->part_sz - byte_off))) {
            return WOLFBOOT_FS_E_RANGE;
        }

        ret = fs_disk_read_exact(vol, byte_off, (uint32_t)this_len,
            buf + done);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        done += this_len;
    }
    return (int)done;
}

int fat32_label_eq(struct fs_volume *vol, const char *label)
{
    char vl[FAT_BS_VOL_LAB_LEN + 1];
    uint8_t raw[FAT_BS_VOL_LAB_LEN];
    uint32_t n;
    uint32_t i;
    int ret;

    ret = fs_meta_read(vol, FAT_BS_VOL_LAB, FAT_BS_VOL_LAB_LEN, raw);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    for (i = 0; i < FAT_BS_VOL_LAB_LEN; i++) {
        vl[i] = (char)raw[i];
    }
    vl[FAT_BS_VOL_LAB_LEN] = '\0';

    /* The label is space-padded to a fixed width on media. */
    n = FAT_BS_VOL_LAB_LEN;
    while ((n > 0U) && (vl[n - 1U] == ' ')) {
        n--;
    }
    vl[n] = '\0';

    if (n == 0U) {
        return 0;
    }
    return fat32_name_eq(vl, label);
}

#endif /* WOLFBOOT_DISK_FS && WOLFBOOT_FAT32 */

#endif /* _WOLFBOOT_FAT32_C_ */
