/* gpt.c
 *
 * Generic GPT (GUID Partition Table) parsing implementation.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
    const struct gpt_mbr_part_entry *pte;
    const uint16_t *boot_sig;
    uint32_t i;
    int found = 0;
    uint32_t lba = 0;

    if (mbr_sector == NULL) {
        return -1;
    }

    /* Check boot signature at offset 0x1FE */
    boot_sig = (const uint16_t *)(mbr_sector + GPT_MBR_BOOTSIG_OFFSET);
    if (*boot_sig != GPT_MBR_BOOTSIG_VALUE) {
        return -1;
    }

    /* Scan all 4 MBR partition entries for protective GPT type (0xEE) */
    for (i = 0; i < 4; i++) {
        pte = (const struct gpt_mbr_part_entry *)(mbr_sector +
                GPT_MBR_ENTRY_START + (i * sizeof(struct gpt_mbr_part_entry)));
        if (pte->ptype == GPT_PTYPE_PROTECTIVE) {
            lba = pte->lba_first;
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
    const struct guid_ptable *src;

    if (sector == NULL || hdr == NULL) {
        return -1;
    }

    src = (const struct guid_ptable *)sector;

    /* Validate GPT signature */
    if (src->signature != GPT_SIGNATURE) {
        return -1;
    }

    /* Copy header to output */
    memcpy(hdr, src, sizeof(struct guid_ptable));

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
    const struct gpt_part_entry *pe;

    if (entry_data == NULL || part == NULL) {
        return -1;
    }

    if (entry_size < sizeof(struct gpt_part_entry)) {
        return -1;
    }

    pe = (const struct gpt_part_entry *)entry_data;

    /* Check if partition entry is empty (type GUID is all zeros) */
    if (pe->type[0] == 0 && pe->type[1] == 0) {
        return -1;
    }

    /* Validate geometry */
    if (pe->first > pe->last) {
        return -1;
    }
    /* LBA 0 is the protective MBR; no valid partition can end there */
    if (pe->last == 0) {
        return -1;
    }

    /* Extract partition info (convert LBA to byte offsets) */
    part->start = pe->first * GPT_SECTOR_SIZE;
    part->end = ((pe->last + 1) * GPT_SECTOR_SIZE) - 1;
    memcpy(part->name, pe->name, sizeof(part->name));

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
