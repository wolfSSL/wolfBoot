/* gpt.h
 *
 * Generic GPT (GUID Partition Table) parsing support.
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

#ifndef GPT_H
#define GPT_H

#include <stdint.h>

/* GPT Constants */
#define GPT_SECTOR_SIZE        0x200
#define GPT_SIGNATURE          0x5452415020494645ULL /* "EFI PART" */
#define GPT_PTYPE_PROTECTIVE   0xEE
#define GPT_PART_NAME_SIZE     36
#define GPT_MBR_ENTRY_START    0x01BE
#define GPT_MBR_BOOTSIG_OFFSET 0x01FE
#define GPT_MBR_BOOTSIG_VALUE  0xAA55

/**
 * @brief MBR partition table entry structure.
 *
 * This packed structure defines the layout of an MBR partition table entry
 * used to identify GPT partitions (protective MBR).
 */
struct __attribute__((packed)) gpt_mbr_part_entry {
    uint8_t stat;
    uint8_t chs_first[3];
    uint8_t ptype;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t lba_size;
};

/**
 * @brief GPT (GUID Partition Table) header structure.
 */
struct __attribute__((packed)) guid_ptable {
    uint64_t signature;
    uint32_t revision;
    uint32_t hdr_size;
    uint32_t hdr_crc32;
    uint32_t res0;
    uint64_t main_lba;
    uint64_t backup_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t disk_guid[2];
    uint64_t start_array;
    uint32_t n_part;
    uint32_t array_sz;
    uint32_t part_crc;
    uint8_t  res1[GPT_SECTOR_SIZE - 0x5C];
};

/**
 * @brief GPT partition entry structure.
 *
 * This packed structure defines the layout of a GPT partition entry
 * used to describe individual partitions on the disk.
 */
struct __attribute__((packed)) gpt_part_entry {
    uint64_t type[2];
    uint64_t uuid[2];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    uint16_t name[GPT_PART_NAME_SIZE];
};

/**
 * @brief Parsed partition information.
 *
 * This structure holds parsed information about a partition extracted
 * from a GPT partition entry.
 */
struct gpt_part_info {
    uint64_t start;  /* Start offset in bytes */
    uint64_t end;    /* End offset in bytes */
    uint16_t name[GPT_PART_NAME_SIZE];
};

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
int gpt_check_mbr_protective(const uint8_t *mbr_sector, uint32_t *gpt_lba);

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
int gpt_parse_header(const uint8_t *sector, struct guid_ptable *hdr);

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
                        struct gpt_part_info *part);

/**
 * @brief Compare UTF-16 partition name with ASCII string.
 *
 * Compares a GPT partition name (UTF-16LE) with an ASCII string label.
 *
 * @param[in] utf16_name UTF-16LE partition name from GPT entry.
 * @param[in] ascii_label ASCII string to compare against.
 *
 * @return 1 if names match, 0 if they don't match.
 */
int gpt_part_name_eq(const uint16_t *utf16_name, const char *ascii_label);

#endif /* GPT_H */

