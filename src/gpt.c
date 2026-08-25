/* gpt.c
 *
 * Generic GPT (GUID Partition Table) parsing implementation.
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

/**
 * @file gpt.c
 * @brief Generic GPT (GUID Partition Table) parsing implementation.
 *
 * This file contains platform-independent GPT parsing functions that operate
 * on memory buffers. Platform-specific disk I/O is handled separately.
 */

#include <stdint.h>
#include <string.h>

#include "gpt.h"

#define GPT_HDR_SIGNATURE       0x00
#define GPT_HDR_REVISION        0x08
#define GPT_HDR_SIZE            0x0C
#define GPT_HDR_CRC32           0x10
#define GPT_HDR_MAIN_LBA        0x18
#define GPT_HDR_BACKUP_LBA      0x20
#define GPT_HDR_FIRST_USABLE    0x28
#define GPT_HDR_LAST_USABLE     0x30
#define GPT_HDR_START_ARRAY     0x48
#define GPT_HDR_N_PART          0x50
#define GPT_HDR_ARRAY_SZ        0x54
#define GPT_HDR_PART_CRC        0x58
#define GPT_HDR_MIN_SIZE        0x5C

#define GPT_PE_TYPE             0x00
#define GPT_PE_FIRST            0x20
#define GPT_PE_LAST             0x28
#define GPT_PE_NAME             0x38

void gpt_crc32_init(struct gpt_crc32_ctx *ctx)
{
    if (ctx != NULL) {
        ctx->value = 0xFFFFFFFFU;
    }
}

void gpt_crc32_update(struct gpt_crc32_ctx *ctx, const uint8_t *data,
                      uint32_t len)
{
    uint32_t i;
    uint32_t j;

    if (ctx == NULL || data == NULL) {
        return;
    }

    for (i = 0; i < len; i++) {
        ctx->value ^= data[i];
        for (j = 0; j < 8; j++) {
            uint32_t mask = -(ctx->value & 1U);
            ctx->value = (ctx->value >> 1) ^ (0xEDB88320U & mask);
        }
    }
}

uint32_t gpt_crc32_final(const struct gpt_crc32_ctx *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    return ~ctx->value;
}

/**
 * @brief Check MBR for protective GPT partition entry.
 *
 * Scans the MBR sector for a protective GPT partition entry (type 0xEE)
 * and validates the boot signature.
 *
 * @param[in] mbr_sector Pointer to 512-byte MBR sector data.
 * @param[out] gpt_lba If not NULL, receives the LBA of the GPT header.
 *
 * @return 0 on success (valid protective MBR found), -1 on error.
 */
int gpt_check_mbr_protective(const uint8_t *mbr_sector, uint32_t *gpt_lba)
{
    const uint8_t *pte;
    uint32_t i;
    int found = 0;
    uint32_t lba = 0;

    if (mbr_sector == NULL) {
        return -1;
    }

    /* Check boot signature at offset 0x1FE */
    if (gpt_le16(mbr_sector + GPT_MBR_BOOTSIG_OFFSET) !=
            GPT_MBR_BOOTSIG_VALUE) {
        return -1;
    }

    /* Scan all 4 MBR partition entries for protective GPT type (0xEE) */
    for (i = 0; i < 4; i++) {
        pte = mbr_sector + GPT_MBR_ENTRY_START + (i * GPT_MBR_PTE_SIZE);
        if (pte[GPT_MBR_PTE_PTYPE] == GPT_PTYPE_PROTECTIVE) {
            lba = gpt_le32(pte + GPT_MBR_PTE_LBA_FIRST);
            found = 1;
            break;
        }
    }

    if (!found) {
        return -1;
    }

    if (gpt_lba != NULL) {
        *gpt_lba = lba;
    }

    return 0;
}

/**
 * @brief Parse and validate a GPT header.
 *
 * Validates the GPT signature and copies header data to the output structure.
 *
 * @param[in] sector Pointer to 512-byte GPT header sector data.
 * @param[out] hdr Pointer to structure to receive parsed header.
 *
 * @return 0 on success (valid GPT header), -1 on error.
 */
int gpt_parse_header(const uint8_t *sector, struct guid_ptable *hdr)
{
    uint8_t tmp[GPT_SECTOR_SIZE];
    struct gpt_crc32_ctx crc;
    uint32_t hdr_size;

    if (sector == NULL || hdr == NULL) {
        return -1;
    }

    /* Validate GPT signature */
    if (gpt_le64(sector + GPT_HDR_SIGNATURE) != GPT_SIGNATURE) {
        return -1;
    }

    hdr_size = gpt_le32(sector + GPT_HDR_SIZE);
    if (hdr_size < GPT_HDR_MIN_SIZE || hdr_size > GPT_SECTOR_SIZE) {
        return -1;
    }

    /* The header CRC is computed over the on-disk bytes with its own field
     * zeroed, so it has to run on a copy of the raw sector rather than on a
     * host-order struct. Same stack cost as the struct copy it replaces. */
    memcpy(tmp, sector, sizeof(tmp));
    memset(tmp + GPT_HDR_CRC32, 0, 4);
    gpt_crc32_init(&crc);
    gpt_crc32_update(&crc, tmp, hdr_size);
    if (gpt_crc32_final(&crc) != gpt_le32(sector + GPT_HDR_CRC32)) {
        return -1;
    }

    /* Populate the output in HOST order. */
    memset(hdr, 0, sizeof(struct guid_ptable));
    hdr->signature    = gpt_le64(sector + GPT_HDR_SIGNATURE);
    hdr->revision     = gpt_le32(sector + GPT_HDR_REVISION);
    hdr->hdr_size     = hdr_size;
    hdr->hdr_crc32    = gpt_le32(sector + GPT_HDR_CRC32);
    hdr->main_lba     = gpt_le64(sector + GPT_HDR_MAIN_LBA);
    hdr->backup_lba   = gpt_le64(sector + GPT_HDR_BACKUP_LBA);
    hdr->first_usable = gpt_le64(sector + GPT_HDR_FIRST_USABLE);
    hdr->last_usable  = gpt_le64(sector + GPT_HDR_LAST_USABLE);
    hdr->start_array  = gpt_le64(sector + GPT_HDR_START_ARRAY);
    hdr->n_part       = gpt_le32(sector + GPT_HDR_N_PART);
    hdr->array_sz     = gpt_le32(sector + GPT_HDR_ARRAY_SZ);
    hdr->part_crc     = gpt_le32(sector + GPT_HDR_PART_CRC);

    return 0;
}

/**
 * @brief Parse a GPT partition entry.
 *
 * Parses a single partition entry and extracts partition information.
 * Returns success only if the partition entry is valid (non-zero type GUID).
 *
 * @param[in] entry_data Pointer to partition entry data.
 * @param[in] entry_size Size of the partition entry in bytes.
 * @param[out] part Pointer to structure to receive parsed partition info.
 *
 * @return 0 on success (valid partition entry), -1 if entry is empty/invalid.
 */
int gpt_parse_partition(const uint8_t *entry_data, uint32_t entry_size,
                        struct gpt_part_info *part)
{
    uint64_t first, last;
    uint32_t i;

    if (entry_data == NULL || part == NULL) {
        return -1;
    }

    if (entry_size < sizeof(struct gpt_part_entry)) {
        return -1;
    }

    /* Check if partition entry is empty (type GUID is all zeros). The GUID
     * is compared as raw bytes, so its byte order does not matter. */
    if ((gpt_le64(entry_data + GPT_PE_TYPE) == 0) &&
            (gpt_le64(entry_data + GPT_PE_TYPE + 8) == 0)) {
        return -1;
    }

    first = gpt_le64(entry_data + GPT_PE_FIRST);
    last  = gpt_le64(entry_data + GPT_PE_LAST);

    /* Validate geometry */
    if (first > last) {
        return -1;
    }
    /* LBA 0 is the protective MBR; no valid partition can end there */
    if (last == 0) {
        return -1;
    }
    /* Reject extents whose byte offset would overflow uint64_t: (last + 1)
     * must not wrap and (last + 1) * GPT_SECTOR_SIZE must stay representable.
     * Without this, last = UINT64_MAX yields part->end = UINT64_MAX, which
     * defeats the bounds check in disk_part_read(). first <= last, so
     * bounding last also bounds the part->start multiply. */
    if (last >= (UINT64_MAX / GPT_SECTOR_SIZE)) {
        return -1;
    }

    /* Extract partition info (convert LBA to byte offsets) */
    part->start = first * GPT_SECTOR_SIZE;
    part->end = ((last + 1) * GPT_SECTOR_SIZE) - 1;
    /* The name is UTF-16LE on disk; store it in host order so
     * gpt_part_name_eq() can compare code units directly. */
    for (i = 0; i < GPT_PART_NAME_SIZE; i++) {
        part->name[i] = gpt_le16(entry_data + GPT_PE_NAME + (i * 2U));
    }

    return 0;
}

/**
 * @brief Compare UTF-16 partition name with ASCII string.
 *
 * Compares a GPT partition name (UTF-16LE) with an ASCII string label.
 * Handles optional BOM prefix in UTF-16 string.
 *
 * @param[in] utf16_name UTF-16LE partition name from GPT entry.
 * @param[in] ascii_label ASCII string to compare against.
 *
 * @return 1 if names match, 0 if they don't match.
 */
int gpt_part_name_eq(const uint16_t *utf16_name, const char *ascii_label)
{
    unsigned int utf16_idx;
    unsigned int i;
    unsigned int ascii_len;

    if (utf16_name == NULL || ascii_label == NULL) {
        return 0;
    }

    ascii_len = strlen(ascii_label);
    if (ascii_len > GPT_PART_NAME_SIZE) {
        return 0;
    }

    utf16_idx = 0;
    /* Skip BOM if present */
    if (utf16_name[utf16_idx] == 0xfeff) {
        utf16_idx = 1;
        /* Ensure label + BOM offset fits in name array */
        if (ascii_len + utf16_idx > GPT_PART_NAME_SIZE) {
            return 0;
        }
    }

    /* Compare each character */
    for (i = 0; i < ascii_len; i++, utf16_idx++) {
        /* Non-ASCII character or mismatch */
        if (utf16_name[utf16_idx] != (uint16_t)ascii_label[i]) {
            return 0;
        }
    }

    /* Check that UTF-16 string is null-terminated after the match */
    if (utf16_idx < GPT_PART_NAME_SIZE && utf16_name[utf16_idx] != 0x0) {
        return 0;
    }

    return 1;
}
