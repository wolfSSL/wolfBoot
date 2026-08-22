/* ext4.c
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

/* Read-only ext4 backend for the wolfBoot disk filesystem layer.
 *
 * Supports extent-mapped files only. The ext2/ext3 indirect block map is
 * refused rather than implemented: every ext4 volume mkfs produces uses
 * extents, and supporting both would double the parsing surface for no
 * practical gain.
 *
 * SECURITY: the superblock, the group descriptors, the inodes, the extent
 * trees and the directory blocks are all attacker-controlled and are
 * parsed before the image signature is checked. Volume geometry is
 * validated at mount so that every later block-to-byte conversion is
 * bounded by construction, the extent tree descent is made finite by
 * requiring the depth to decrease by exactly one at each level, and the
 * directory walk is bounded by entry and block caps.
 */

#ifndef _WOLFBOOT_EXT4_C_
#define _WOLFBOOT_EXT4_C_

#if defined(WOLFBOOT_DISK_FS) && defined(WOLFBOOT_EXT4)

#include <stdint.h>
#include <string.h>

#include "disk_fs.h"
#include "ext4.h"
#include "disk.h"
#include "printf.h"

#ifdef DEBUG_FS
#define EXT_DBG(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#define EXT_DBG(_f_, ...) do{}while(0)
#endif

/* The parts of an inode this parser needs. */
struct ext4_inode_info {
    uint64_t size;
    uint32_t flags;
    uint16_t mode;
    uint8_t  iblock[EXT4_INODE_BLOCK_LEN];
};

/* ---------------------------------------------------------------------
 * Mount
 * --------------------------------------------------------------------- */

static uint64_t ext4_ceil_div(uint64_t a, uint64_t b)
{
    return (a + b - 1U) / b;
}

int ext4_mount(struct fs_volume *vol)
{
    uint8_t sa[EXT4_SB_A_LEN];
    uint8_t sb[EXT4_SB_B_LEN];
    uint8_t tmp[4];
    struct ext4_volume *ev = &vol->u.ext;
    uint32_t log_block_size, block_sz, first_data_block;
    uint32_t blocks_count, inodes_count, blocks_per_group, inodes_per_group;
    uint32_t incompat, rev_level, first_ino;
    uint16_t magic, state, inode_sz, desc_sz;
    uint64_t groups, gdt_off, vol_sz;
    int ret;

    /* Everything read below lies within the first 0x154 bytes of the
     * superblock, which starts 1024 bytes in. */
    if (vol->part_sz < (uint64_t)(EXT4_SB_OFF + EXT4_SB_BLOCKS_COUNT_HI + 4U)) {
        return WOLFBOOT_FS_E_NOFS;
    }

    ret = fs_meta_read(vol, EXT4_SB_OFF + EXT4_SB_B_OFF, EXT4_SB_B_LEN, sb);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    magic = fs_le16(sb + EXT4_B_MAGIC);
    if (magic != EXT4_SUPER_MAGIC) {
        return WOLFBOOT_FS_E_NOFS;
    }

    /* From here on the volume IS ext-family, so a failure is an error and
     * never a fall back to the raw path. */

    ret = fs_meta_read(vol, EXT4_SB_OFF + EXT4_SB_A_OFF, EXT4_SB_A_LEN, sa);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    state     = fs_le16(sb + EXT4_B_STATE);
    rev_level = fs_le32(sb + EXT4_B_REV_LEVEL);
    first_ino = fs_le32(sb + EXT4_B_FIRST_INO);
    inode_sz  = fs_le16(sb + EXT4_B_INODE_SIZE);
    incompat  = fs_le32(sb + EXT4_B_FEATURE_INCOMPAT);

    /* s_feature_compat and s_feature_ro_compat are deliberately ignored.
     * COMPAT bits are safe to ignore by definition; RO_COMPAT bits are safe
     * for a read-only mount - that is what the flag class means - and
     * ignoring them is what lets metadata_csum, huge_file, dir_nlink,
     * extra_isize, sparse_super and large_file work with no code at all.
     * This looks like a missing check and is not one. */

    if (rev_level > 1U) {
        EXT_DBG("ext4: revision %u unsupported\r\n", rev_level);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if ((state & EXT4_STATE_VALID_FS) == 0U) {
#ifndef WOLFBOOT_FS_EXT4_ALLOW_DIRTY
        EXT_DBG("ext4: filesystem not cleanly unmounted\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
#endif
    }
    if ((incompat & ~((uint32_t)EXT4_INCOMPAT_SUPPORTED)) != 0U) {
        EXT_DBG("ext4: unsupported incompat features %x\r\n", incompat);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if ((incompat & EXT4_INCOMPAT_EXTENTS) == 0U) {
        EXT_DBG("ext4: indirect block map (ext2/ext3) unsupported\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }

    log_block_size   = fs_le32(sa + EXT4_A_LOG_BLOCK_SIZE);
    first_data_block = fs_le32(sa + EXT4_A_FIRST_DATA_BLOCK);
    blocks_count     = fs_le32(sa + EXT4_A_BLOCKS_COUNT_LO);
    inodes_count     = fs_le32(sa + EXT4_A_INODES_COUNT);
    blocks_per_group = fs_le32(sa + EXT4_A_BLOCKS_PER_GROUP);
    inodes_per_group = fs_le32(sa + EXT4_A_INODES_PER_GROUP);

    if (log_block_size > 2U) {
        EXT_DBG("ext4: block size 2^%u unsupported\r\n", log_block_size);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    /* bigalloc gives clusters a different size from blocks. */
    if (fs_le32(sa + EXT4_A_LOG_CLUSTER_SIZE) != log_block_size) {
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    block_sz = 1024U << log_block_size;

    /* Exactly one value is correct for each block size. Accepting anything
     * else would put the group descriptor table somewhere other than where
     * every later offset assumes it is. */
    if (first_data_block != ((block_sz == 1024U) ? 1U : 0U)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    if ((blocks_per_group == 0U) || (blocks_per_group > (block_sz * 8U)) ||
            ((blocks_per_group % 8U) != 0U)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    if ((inodes_per_group == 0U) || (inodes_per_group > (block_sz * 8U))) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    if ((inodes_count == 0U) || (blocks_count == 0U)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    if (blocks_count <= first_data_block) {
        return WOLFBOOT_FS_E_FORMAT;
    }

    /* Refuse volumes over 16 TiB. Keeping the block count inside 32 bits
     * means every block-to-byte conversion below stays well inside 64. */
    ret = fs_meta_read(vol, EXT4_SB_OFF + EXT4_SB_BLOCKS_COUNT_HI, 4, tmp);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    if (fs_le32(tmp) != 0U) {
        EXT_DBG("ext4: volumes larger than 16TiB unsupported\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }

    /* The volume must fit in its partition. This single check is what
     * bounds every physical block number used from here on. */
    vol_sz = (uint64_t)blocks_count * (uint64_t)block_sz;
    if (vol_sz > vol->part_sz) {
        EXT_DBG("ext4: volume larger than partition\r\n");
        return WOLFBOOT_FS_E_FORMAT;
    }

    if (rev_level == 0U) {
        inode_sz = EXT4_GOOD_OLD_INO_SZ;
    }
    else {
        if ((inode_sz < EXT4_GOOD_OLD_INO_SZ) ||
                (fs_is_pow2(inode_sz) == 0) ||
                ((uint32_t)inode_sz > block_sz)) {
            return WOLFBOOT_FS_E_FORMAT;
        }
        if ((first_ino < 11U) || (first_ino >= inodes_count)) {
            return WOLFBOOT_FS_E_FORMAT;
        }
    }

    desc_sz = EXT4_GD_MIN_SIZE;
    if ((incompat & EXT4_INCOMPAT_64BIT) != 0U) {
        ret = fs_meta_read(vol, EXT4_SB_OFF + EXT4_SB_DESC_SIZE, 2, tmp);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        desc_sz = fs_le16(tmp);
        if ((desc_sz < EXT4_GD_64BIT_SIZE) ||
                (fs_is_pow2(desc_sz) == 0) ||
                ((uint32_t)desc_sz > block_sz)) {
            return WOLFBOOT_FS_E_FORMAT;
        }
    }

    groups = ext4_ceil_div((uint64_t)blocks_count - (uint64_t)first_data_block,
        (uint64_t)blocks_per_group);
    if ((groups == 0U) || (groups > 0xFFFFFFFFULL)) {
        return WOLFBOOT_FS_E_FORMAT;
    }
    /* The superblock states the geometry twice over. If the two statements
     * disagree, one of them was forged. */
    if (groups != ext4_ceil_div((uint64_t)inodes_count,
            (uint64_t)inodes_per_group)) {
        EXT_DBG("ext4: block and inode group counts disagree\r\n");
        return WOLFBOOT_FS_E_FORMAT;
    }

    gdt_off = ((uint64_t)first_data_block + 1U) * (uint64_t)block_sz;
    if ((gdt_off + (groups * (uint64_t)desc_sz)) > vol->part_sz) {
        return WOLFBOOT_FS_E_FORMAT;
    }

    ev->block_sz = block_sz;
    ev->blocks_count = blocks_count;
    ev->inodes_count = inodes_count;
    ev->inodes_per_group = inodes_per_group;
    ev->blocks_per_group = blocks_per_group;
    ev->first_data_block = first_data_block;
    ev->groups = (uint32_t)groups;
    ev->incompat = incompat;
    ev->inode_sz = inode_sz;
    ev->desc_sz = desc_sz;
    ev->gdt_off = gdt_off;
    ev->vol_sz = vol_sz;

    EXT_DBG("ext4: %u blocks of %u bytes, %u groups\r\n",
        blocks_count, block_sz, (uint32_t)groups);
    return WOLFBOOT_FS_OK;
}

/* ---------------------------------------------------------------------
 * Inodes
 * --------------------------------------------------------------------- */

static int ext4_inode_table(struct fs_volume *vol, uint32_t grp,
                            uint64_t *itbl)
{
    struct ext4_volume *ev = &vol->u.ext;
    uint8_t d[4];
    uint64_t desc_off;
    uint64_t val;
    int ret;

    if (grp >= ev->groups) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    desc_off = ev->gdt_off + ((uint64_t)grp * (uint64_t)ev->desc_sz);

    ret = fs_meta_read(vol, desc_off + EXT4_GD_INODE_TABLE_LO, 4, d);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    val = (uint64_t)fs_le32(d);

    if (ev->desc_sz >= EXT4_GD_64BIT_SIZE) {
        ret = fs_meta_read(vol, desc_off + EXT4_GD_INODE_TABLE_HI, 4, d);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        val |= ((uint64_t)fs_le32(d) << 32);
    }
    if ((val == 0U) || (val >= (uint64_t)ev->blocks_count)) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    *itbl = val;
    return WOLFBOOT_FS_OK;
}

/* Read and validate one inode, rejecting every file type and inode flag
 * this parser will not read, so an unsupported layout can never be mistaken
 * for a readable one. */
static int ext4_read_inode(struct fs_volume *vol, uint32_t ino,
                           struct ext4_inode_info *out)
{
    struct ext4_volume *ev = &vol->u.ext;
    uint8_t hdr[EXT4_INODE_HDR_LEN];
    uint8_t hi[4];
    uint64_t itbl, byte_off;
    uint32_t grp, idx;
    uint16_t fmt;
    int ret;

    if ((ino == 0U) || (ino > ev->inodes_count)) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    grp = (ino - 1U) / ev->inodes_per_group;
    idx = (ino - 1U) % ev->inodes_per_group;

    ret = ext4_inode_table(vol, grp, &itbl);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    byte_off = (itbl * (uint64_t)ev->block_sz) +
        ((uint64_t)idx * (uint64_t)ev->inode_sz);
    if ((byte_off + (uint64_t)ev->inode_sz) > ev->vol_sz) {
        return WOLFBOOT_FS_E_CORRUPT;
    }

    ret = fs_meta_read(vol, byte_off + EXT4_INODE_MODE, EXT4_INODE_HDR_LEN,
        hdr);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    ret = fs_meta_read(vol, byte_off + EXT4_INODE_BLOCK,
        EXT4_INODE_BLOCK_LEN, out->iblock);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    out->mode = fs_le16(hdr + EXT4_INODE_MODE);
    out->flags = fs_le32(hdr + EXT4_INODE_FLAGS);
    out->size = (uint64_t)fs_le32(hdr + EXT4_INODE_SIZE_LO);

    /* i_size_high only exists in the large inode format. For a directory
     * the same field is i_dir_acl, so it is only folded in for files. */
    fmt = (uint16_t)(out->mode & EXT4_S_IFMT);
    if ((ev->inode_sz > EXT4_GOOD_OLD_INO_SZ) && (fmt == EXT4_S_IFREG)) {
        ret = fs_meta_read(vol, byte_off + EXT4_INODE_SIZE_HIGH, 4, hi);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        out->size |= ((uint64_t)fs_le32(hi) << 32);
    }

    if (fmt == EXT4_S_IFLNK) {
        EXT_DBG("ext4: symbolic links unsupported\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if ((fmt != EXT4_S_IFREG) && (fmt != EXT4_S_IFDIR)) {
        return WOLFBOOT_FS_E_NOTFILE;
    }

    if ((out->flags & EXT4_FL_INLINE_DATA) != 0U) {
        EXT_DBG("ext4: inline data unsupported\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if ((out->flags & EXT4_FL_ENCRYPT) != 0U) {
        EXT_DBG("ext4: encrypted inode unsupported\r\n");
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    if ((out->flags & EXT4_FL_COMPR) != 0U) {
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }
    /* A volume can advertise extents while an individual inode still uses
     * the old indirect block map, so the per-inode flag is checked too. */
    if ((out->flags & EXT4_FL_EXTENTS) == 0U) {
        EXT_DBG("ext4: inode %u has no extent tree\r\n", ino);
        return WOLFBOOT_FS_E_UNSUPPORTED;
    }

    if (out->size > ev->vol_sz) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    return WOLFBOOT_FS_OK;
}

/* ---------------------------------------------------------------------
 * Extent tree
 * --------------------------------------------------------------------- */

/* Byte offset of extent entry idx within a node. */
#define EXT4_ENT_OFF(idx) \
    (EXT4_EXT_HDR_LEN + ((uint32_t)(idx) * EXT4_EXT_ENT_LEN))

/* Fetch the 12 bytes at byte offset off within an extent node: the header
 * (off 0) or entry i (EXT4_ENT_OFF(i)). Header and entry are both 12 bytes.
 * The node is either the 60-byte root held in the inode or a block on
 * media. */
static int ext4_node_read(struct fs_volume *vol, const uint8_t *root,
                          uint64_t node_off, uint32_t off, uint8_t *out)
{
    if (root != NULL) {
        if ((off + EXT4_EXT_ENT_LEN) > EXT4_EXT_ROOT_CAP) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
        memcpy(out, root + off, EXT4_EXT_ENT_LEN);
        return WOLFBOOT_FS_OK;
    }
    return fs_meta_read(vol, node_off + off, EXT4_EXT_ENT_LEN, out);
}

/**
 * @brief Load an extent node header and validate it.
 *
 * @param cap        Byte capacity of the node: the 60-byte inode root, or
 *                   one block for a node on media.
 * @param want_depth Expected eh_depth, or -1 for the root, whose depth sets
 *                   the starting level and is only range-checked.
 */
static int ext4_node_load(struct fs_volume *vol, const uint8_t *root,
                          uint64_t node_off, uint32_t cap, int want_depth,
                          uint16_t *entries, uint16_t *depth)
{
    uint8_t hdr[EXT4_EXT_HDR_LEN];
    uint16_t max;
    int ret;

    ret = ext4_node_read(vol, root, node_off, 0, hdr);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    *entries = fs_le16(hdr + 2);
    max      = fs_le16(hdr + 4);
    *depth   = fs_le16(hdr + 6);

    if (fs_le16(hdr) != EXT4_EXT_MAGIC) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    if (want_depth < 0) {
        if (*depth > (uint16_t)WOLFBOOT_FS_EXT4_MAX_DEPTH) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
    }
    /* Depth must fall by exactly one on the way down. That is what bounds
     * the descent without a visited set: an index entry pointing back at an
     * ancestor cannot satisfy it. */
    else if ((int)*depth != want_depth) {
        EXT_DBG("ext4: extent depth does not decrease\r\n");
        return WOLFBOOT_FS_E_CORRUPT;
    }
    /* Both halves are required: eh_max must fit the node, and eh_entries
     * must fit eh_max. Checking only one is the classic ext4 out-of-bounds
     * read. */
    if ((EXT4_EXT_HDR_LEN + ((uint32_t)max * EXT4_EXT_ENT_LEN)) > cap) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    if (*entries > max) {
        return WOLFBOOT_FS_E_CORRUPT;
    }
    return WOLFBOOT_FS_OK;
}

/**
 * @brief Map a logical block to a physical one, plus the contiguous run.
 *
 * The descent is iterative and holds no per-level state, so it cannot grow
 * the stack; ext4_node_load()'s depth-decreases-by-one rule is what makes it
 * finite, so no cycle detection is needed.
 *
 * @param run     Contiguous blocks left in the extent from lblk, so the
 *                caller can coalesce them into one media read.
 * @param is_hole Set when no extent covers lblk. ext4 defines a hole as
 *                reading zeros, so zeros ARE the file's content there and
 *                the hash still covers what the file holds. Holes are
 *                routine: mke2fs -d and cp leave one wherever the source has
 *                a run of zeros, as signed-image header padding always does.
 */
static int ext4_map_run(struct fs_volume *vol, const uint8_t *iroot,
                        uint64_t lblk, uint64_t *pblk, uint32_t *run,
                        int *is_hole)
{
    struct ext4_volume *ev = &vol->u.ext;
    uint8_t ent[EXT4_EXT_ENT_LEN];
    const uint8_t *root = iroot;
    uint64_t node_off = 0;
    uint64_t leaf = 0;
    uint64_t ee_start;
    uint64_t next_start = 0;
    uint32_t cap = EXT4_EXT_ROOT_CAP;
    uint32_t i, prev_block, ent_block;
    uint32_t ee_len;
    uint16_t entries, depth;
    int have_next = 0;
    int level;
    int found;
    int ret;

    *is_hole = 0;

    ret = ext4_node_load(vol, root, node_off, cap, -1, &entries, &depth);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    level = (int)depth;

    while (level > 0) {
        found = -1;
        prev_block = 0;
        for (i = 0; i < entries; i++) {
            ret = ext4_node_read(vol, root, node_off, EXT4_ENT_OFF(i), ent);
            if (ret != WOLFBOOT_FS_OK) {
                return ret;
            }
            ent_block = fs_le32(ent);
            /* Strictly increasing. One test that simultaneously rejects
             * backward, duplicate and overlapping index entries. */
            if ((i > 0U) && (ent_block <= prev_block)) {
                return WOLFBOOT_FS_E_CORRUPT;
            }
            prev_block = ent_block;
            if ((uint64_t)ent_block <= lblk) {
                leaf = (uint64_t)fs_le32(ent + 4) |
                    ((uint64_t)fs_le16(ent + 8) << 32);
                found = (int)i;
            }
        }
        if (found < 0) {
            /* Nothing in this index covers the block, so it lies in a hole
             * ahead of the tree's first mapped block. */
            *is_hole = 1;
            *pblk = 0;
            *run = 1;
            return WOLFBOOT_FS_OK;
        }
        if ((leaf < (uint64_t)ev->first_data_block) ||
                (leaf >= (uint64_t)ev->blocks_count)) {
            return WOLFBOOT_FS_E_CORRUPT;
        }

        node_off = leaf * (uint64_t)ev->block_sz;
        root = NULL;
        cap = ev->block_sz;

        ret = ext4_node_load(vol, root, node_off, cap, level - 1,
            &entries, &depth);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        level = (int)depth;
    }

    prev_block = 0;
    for (i = 0; i < entries; i++) {
        ret = ext4_node_read(vol, root, node_off, EXT4_ENT_OFF(i), ent);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        ent_block = fs_le32(ent);
        ee_len = fs_le16(ent + 4);
        ee_start = (uint64_t)fs_le32(ent + 8) |
            ((uint64_t)fs_le16(ent + 6) << 32);

        /* Strictly increasing, checked up to and including the entry that
         * matches. That is the range that determines the result: the scan
         * stops at the first covering extent, so an entry beyond it cannot
         * influence this lookup. A later entry that IS reachable is caught
         * when the lookup for its own logical block runs. */
        if ((i > 0U) && (ent_block <= prev_block)) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
        prev_block = ent_block;

        if (ee_len == 0U) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
        /* A length above the initialized maximum marks an uninitialized
         * extent, which reads as zeros. Returning those silently would
         * hand the verifier different bytes than the file really holds,
         * so it is refused instead. */
        if (ee_len > EXT4_EXT_INIT_MAX_LEN) {
            EXT_DBG("ext4: uninitialized extent unsupported\r\n");
            return WOLFBOOT_FS_E_UNSUPPORTED;
        }
        /* Computed in 64-bit so the sum cannot wrap. */
        if ((ee_start < (uint64_t)ev->first_data_block) ||
                ((ee_start + (uint64_t)ee_len) >
                    (uint64_t)ev->blocks_count)) {
            return WOLFBOOT_FS_E_CORRUPT;
        }

        if ((lblk >= (uint64_t)ent_block) &&
                (lblk < ((uint64_t)ent_block + (uint64_t)ee_len))) {
            *pblk = ee_start + (lblk - (uint64_t)ent_block);
            *run = (uint32_t)(((uint64_t)ent_block + (uint64_t)ee_len) -
                lblk);
            return WOLFBOOT_FS_OK;
        }
        if (((uint64_t)ent_block > lblk) &&
                ((have_next == 0) || ((uint64_t)ent_block < next_start))) {
            next_start = (uint64_t)ent_block;
            have_next = 1;
        }
    }

    /* A hole. The run stops at the next extent in THIS leaf; with none
     * known it stays at one block, since a later leaf may map data sooner
     * and zero-filling mapped bytes would corrupt the image. */
    *is_hole = 1;
    *pblk = 0;
    if (have_next != 0) {
        *run = (uint32_t)(next_start - lblk);
    }
    else {
        *run = 1;
    }
    return WOLFBOOT_FS_OK;
}

/* Walk the whole extent map at open time, so a malformed tree (bad header,
 * out-of-range or non-monotonic extent, uninitialized extent) becomes a clean
 * error before any payload loads. Steps by extent, not by block, so cost is
 * one descent per extent. */
static int ext4_validate_map(struct fs_volume *vol, const uint8_t *iroot,
                             uint64_t size)
{
    struct ext4_volume *ev = &vol->u.ext;
    uint64_t blocks, lblk, pblk, steps;
    uint32_t run;
    int is_hole;
    int ret;

    if (size == 0U) {
        return WOLFBOOT_FS_OK;
    }
    blocks = ext4_ceil_div(size, (uint64_t)ev->block_sz);
    lblk = 0;
    steps = 0;

    while (lblk < blocks) {
        ret = ext4_map_run(vol, iroot, lblk, &pblk, &run, &is_hole);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        /* ext4_map_run never reports a zero run, but the loop's
         * termination depends on it, so do not take that on trust. */
        if (run == 0U) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
        lblk += (uint64_t)run;
        steps++;
        if (steps > blocks) {
            return WOLFBOOT_FS_E_CORRUPT;
        }
    }
    return WOLFBOOT_FS_OK;
}

/* ---------------------------------------------------------------------
 * Directory lookup
 * --------------------------------------------------------------------- */

static int ext4_lookup(struct fs_volume *vol,
                       const struct ext4_inode_info *dir,
                       const char *name, uint32_t name_len,
                       uint32_t *out_ino, uint8_t *out_type)
{
    struct ext4_volume *ev = &vol->u.ext;
    uint8_t de[EXT4_DIRENT_HDR_LEN];
    char dname[WOLFBOOT_FS_MAX_NAME + 1];
    uint64_t blocks, lblk, pblk, base;
    uint32_t entries = 0;
    uint32_t scanned_blocks = 0;
    uint32_t pos, rec_len, dname_len, ino;
    uint32_t run;
    uint8_t ftype;
    int is_hole;
    int ret;

    blocks = ext4_ceil_div(dir->size, (uint64_t)ev->block_sz);

    for (lblk = 0; lblk < blocks; lblk++) {
        if (scanned_blocks >= (uint32_t)WOLFBOOT_FS_MAX_DIR_BLOCKS) {
            EXT_DBG("ext4: directory block bound tripped\r\n");
            return WOLFBOOT_FS_E_CORRUPT;
        }
        scanned_blocks++;

        ret = ext4_map_run(vol, dir->iblock, lblk, &pblk, &run, &is_hole);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        if (is_hole != 0) {
            /* An unmapped directory block reads as zeros, which is not a
             * valid dirent stream. It holds no entries, so skip it rather
             * than reject the whole directory. */
            continue;
        }
        base = pblk * (uint64_t)ev->block_sz;

        pos = 0;
        while ((pos + EXT4_DIRENT_HDR_LEN) <= ev->block_sz) {
            if (entries >= (uint32_t)WOLFBOOT_FS_MAX_DIR_ENTRIES) {
                EXT_DBG("ext4: directory entry bound tripped\r\n");
                return WOLFBOOT_FS_E_CORRUPT;
            }
            entries++;

            ret = fs_meta_read(vol, base + pos, EXT4_DIRENT_HDR_LEN, de);
            if (ret != WOLFBOOT_FS_OK) {
                return ret;
            }
            ino = fs_le32(de);
            rec_len = fs_le16(de + 4);

            /* A zero or misaligned rec_len is the standard way to make a
             * directory walk loop forever or step outside its block. */
            if ((rec_len < EXT4_DIRENT_HDR_LEN) || ((rec_len & 3U) != 0U) ||
                    ((pos + rec_len) > ev->block_sz)) {
                EXT_DBG("ext4: bad dirent rec_len %u\r\n", rec_len);
                return WOLFBOOT_FS_E_CORRUPT;
            }

            if ((ev->incompat & EXT4_INCOMPAT_FILETYPE) != 0U) {
                dname_len = de[6];
                ftype = de[7];
            }
            else {
                dname_len = fs_le16(de + 6);
                ftype = 0;
            }
            if ((dname_len > EXT4_NAME_MAX) ||
                    ((EXT4_DIRENT_HDR_LEN + dname_len) > rec_len)) {
                return WOLFBOOT_FS_E_CORRUPT;
            }

            /* inode 0 marks an unused slot, and is also how an htree
             * interior node hides itself from a linear scan. Skip it
             * without looking at the name. */
            if (ino != 0U) {
                if (ino > ev->inodes_count) {
                    return WOLFBOOT_FS_E_CORRUPT;
                }
                if ((dname_len == name_len) &&
                        (dname_len <= (uint32_t)WOLFBOOT_FS_MAX_NAME)) {
                    ret = fs_meta_read(vol,
                        base + pos + EXT4_DIRENT_HDR_LEN, dname_len,
                        (uint8_t *)dname);
                    if (ret != WOLFBOOT_FS_OK) {
                        return ret;
                    }
                    dname[dname_len] = '\0';
                    /* ext4 names are case-sensitive. */
                    if (memcmp(dname, name, (size_t)dname_len) == 0) {
                        *out_ino = ino;
                        *out_type = ftype;
                        return WOLFBOOT_FS_OK;
                    }
                }
            }
            pos += rec_len;
        }
    }
    return WOLFBOOT_FS_E_NOENT;
}

/* ---------------------------------------------------------------------
 * Public backend entry points
 * --------------------------------------------------------------------- */

int ext4_open(struct fs_volume *vol, struct fs_file *f, const char *path,
              uint64_t max_size)
{
    struct ext4_inode_info info;
    char comp[WOLFBOOT_FS_MAX_NAME + 1];
    const char *p = path;
    uint32_t comp_len;
    uint32_t child;
    uint8_t ftype;
    int depth = 0;
    int have = 0;
    int ret;

    ret = ext4_read_inode(vol, EXT4_ROOT_INO, &info);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    for (;;) {
        ret = fs_path_next(&p, comp, &comp_len, &depth);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            break;
        }
        if ((info.mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
            return WOLFBOOT_FS_E_NOTFILE;
        }
        ret = ext4_lookup(vol, &info, comp, comp_len, &child, &ftype);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        /* The dirent's type byte is a hint only; the authoritative type
         * comes from the inode, which is checked right below. */
        if (ftype == EXT4_FT_SYMLINK) {
            EXT_DBG("ext4: symbolic links unsupported\r\n");
            return WOLFBOOT_FS_E_UNSUPPORTED;
        }
        ret = ext4_read_inode(vol, child, &info);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }
        have = 1;
    }

    if (have == 0) {
        return WOLFBOOT_FS_E_NOTFILE;
    }
    if ((info.mode & EXT4_S_IFMT) != EXT4_S_IFREG) {
        return WOLFBOOT_FS_E_NOTFILE;
    }
    /* The inode-reported size is attacker-controlled and is about to bound
     * a load into RAM, so it is checked before any of it is used. */
    if (info.size > max_size) {
        EXT_DBG("ext4: file size exceeds max\r\n");
        return WOLFBOOT_FS_E_TOOBIG;
    }

    ret = ext4_validate_map(vol, info.iblock, info.size);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }

    f->size = info.size;
    memcpy(f->iblock, info.iblock, EXT4_INODE_BLOCK_LEN);
    f->cur_off = 0;
    return WOLFBOOT_FS_OK;
}

int ext4_read(struct fs_file *f, uint64_t off, uint64_t len, uint8_t *buf)
{
    struct fs_volume *vol = f->vol;
    struct ext4_volume *ev = &vol->u.ext;
    uint64_t done = 0;
    uint64_t lblk, pblk, byte_off, avail, this_len;
    uint32_t in_blk, run;
    int is_hole;
    int ret;

    while (done < len) {
        lblk = (off + done) / (uint64_t)ev->block_sz;
        in_blk = (uint32_t)((off + done) % (uint64_t)ev->block_sz);

        ret = ext4_map_run(vol, f->iblock, lblk, &pblk, &run, &is_hole);
        if (ret != WOLFBOOT_FS_OK) {
            return ret;
        }

        /* An extent is contiguous by definition, so a single-extent file
         * costs one media read per caller request rather than one per
         * block. */
        avail = ((uint64_t)run * (uint64_t)ev->block_sz) - in_blk;
        this_len = len - done;
        if (this_len > avail) {
            this_len = avail;
        }
        if (this_len > (uint64_t)DISK_IO_MAX_SIZE) {
            this_len = (uint64_t)DISK_IO_MAX_SIZE;
        }

        if (is_hole != 0) {
            /* Unmapped blocks read as zeros by definition, so this is the
             * file's real content and the hash below still covers it. */
            memset(buf + done, 0, (size_t)this_len);
            done += this_len;
            continue;
        }

        byte_off = (pblk * (uint64_t)ev->block_sz) + in_blk;
        /* Defence in depth: mount and the extent walk already bound this,
         * but the cost of being wrong is a read from outside the partition
         * straight into the RAM load region. */
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

int ext4_label_eq(struct fs_volume *vol, const char *label)
{
    char vl[EXT4_SB_VOLUME_NAME_LEN + 1];
    uint32_t i;
    int ret;

    ret = fs_meta_read(vol, EXT4_SB_OFF + EXT4_SB_VOLUME_NAME,
        EXT4_SB_VOLUME_NAME_LEN, (uint8_t *)vl);
    if (ret != WOLFBOOT_FS_OK) {
        return ret;
    }
    vl[EXT4_SB_VOLUME_NAME_LEN] = '\0';

    if (vl[0] == '\0') {
        return 0;
    }
    for (i = 0; i < (uint32_t)WOLFBOOT_FS_MAX_NAME; i++) {
        if (label[i] == '\0') {
            break;
        }
    }
    if (i > EXT4_SB_VOLUME_NAME_LEN) {
        return 0;
    }
    return (strncmp(vl, label, EXT4_SB_VOLUME_NAME_LEN) == 0) ? 1 : 0;
}

#endif /* WOLFBOOT_DISK_FS && WOLFBOOT_EXT4 */

#endif /* _WOLFBOOT_EXT4_C_ */
