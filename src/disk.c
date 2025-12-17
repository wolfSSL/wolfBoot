/* disk.c
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
 *
 */
/**
 * @file disk.c
 * @brief GPT disk driver implementation.
 *
 * This file contains the GPT disk driver that uses disk I/O operations.
 * It uses the generic GPT parsing functions from src/gpt.c for partition
 * table parsing.
 */
#ifndef _WOLFBOOT_DISK_C_
#define _WOLFBOOT_DISK_C_

#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "printf.h"

/**
 * @brief This array holds instances of the `struct disk_drive` representing
 * multiple disk drives (up to `MAX_DISKS`).
 */
static struct disk_drive Drives[MAX_DISKS] = {0};

/**
 * @brief Opens a disk drive and initializes its partitions.
 *
 * This function opens a disk drive with the specified drive number, reads its
 * MBR (Master Boot Record) to identify GPT partitions, and initializes the
 * disk_drive structure for further operations.
 *
 * @param[in] drv The drive number to open (0 to `MAX_DISKS - 1`).
 *
 * @return The number of partitions found and initialized on success, or -1 if
 * the drive cannot be opened or no valid GPT partition table is found.
 */
int disk_open(int drv)
{
    int r;
    uint32_t i;
    uint32_t n_parts = 0;
    uint32_t gpt_lba = 0;
    struct guid_ptable ptable;
    uint32_t sector[GPT_SECTOR_SIZE/sizeof(uint32_t)];

    if ((drv < 0) || (drv > MAX_DISKS)) {
        wolfBoot_printf("Attempting to access invalid drive %d\r\n", drv);
        return -1;
    }

    wolfBoot_printf("Reading MBR...\r\n");

    /* Read MBR sector */
    r = disk_read(drv, 0, GPT_SECTOR_SIZE, sector);
    if (r < 0) {
        wolfBoot_printf("Failed to read MBR\r\n");
        return -1;
    }

    /* Check for protective MBR and get GPT header location */
    if (gpt_check_mbr_protective((uint8_t*)sector, &gpt_lba) != 0) {
        wolfBoot_printf("Cannot find valid partition table entry for GPT\r\n");
        return -1;
    }
    wolfBoot_printf("Found GPT PTE at sector %u\r\n", gpt_lba);
    wolfBoot_printf("Found valid boot signature in MBR\r\n");

    Drives[drv].is_open = 1;
    Drives[drv].drv = drv;
    Drives[drv].n_parts = 0;

    /* Read GPT header */
    r = disk_read(drv, GPT_SECTOR_SIZE * gpt_lba, GPT_SECTOR_SIZE, sector);
    if (r < 0) {
        wolfBoot_printf("Disk read failed\r\n");
        return -1;
    }

    /* Parse and validate GPT header */
    if (gpt_parse_header((uint8_t*)sector, &ptable) != 0) {
        wolfBoot_printf("Invalid partition table\r\n");
        return -1;
    }

    wolfBoot_printf("Valid GPT partition table\r\n");
    wolfBoot_printf("Current LBA: 0x%llx \r\n", ptable.main_lba);
    wolfBoot_printf("Backup LBA: 0x%llx \r\n", ptable.backup_lba);
    wolfBoot_printf("Max number of partitions: %d\r\n", ptable.n_part);

    n_parts = ptable.n_part;
    if (ptable.n_part > MAX_PARTITIONS) {
        n_parts = MAX_PARTITIONS;
        wolfBoot_printf("Software limited: only allowing up to %d partitions "
                        "per disk.\r\n", n_parts);
    }
    wolfBoot_printf("Disk size: %d\r\n",
        (1 + ptable.last_usable - ptable.first_usable) * GPT_SECTOR_SIZE);

    /* Read and parse partition entries */
    for (i = 0; i < n_parts; i++) {
        struct gpt_part_info part_info;
        uint64_t address = ptable.start_array * GPT_SECTOR_SIZE +
            i * ptable.array_sz;
        uint32_t entry_buf[GPT_PART_ENTRY_SIZE/sizeof(uint32_t)]; /* Max partition entry size */

        if (ptable.array_sz > sizeof(entry_buf)) {
            wolfBoot_printf("Partition entry size too large\r\n");
            break;
        }

        r = disk_read(drv, address, ptable.array_sz, entry_buf);
        if (r < 0) {
            return -1;
        }

        /* Parse partition entry using generic function */
        if (gpt_parse_partition((uint8_t*)entry_buf, ptable.array_sz, &part_info) == 0) {
            uint64_t size;
            uint32_t part_count;

            size = part_info.end - part_info.start + 1;
            part_count = Drives[drv].n_parts;
            Drives[drv].n_parts++;
            Drives[drv].part[part_count].drv = drv;
            Drives[drv].part[part_count].start = part_info.start;
            Drives[drv].part[part_count].end = part_info.end;
            memcpy(&Drives[drv].part[part_count].name, part_info.name,
                   sizeof(part_info.name));

            wolfBoot_printf("disk%d.p%u ", drv, part_count);
            wolfBoot_printf("(%x_%xh", (uint32_t)(size >> 32), (uint32_t)size);
            wolfBoot_printf("@ %x_%x)\r\n",
                            (uint32_t)(part_info.start >> 32),
                            (uint32_t)(part_info.start));
        } else {
            /* Empty partition entry - end of used entries */
            break;
        }
    }

    wolfBoot_printf("Total partitions on disk%u: %u\r\n", drv,
                    Drives[drv].n_parts);
    return Drives[drv].n_parts;
}

/**
 * @brief Opens a disk partition and returns a pointer to its structure.
 *
 * This function opens a disk partition with the specified drive number and
 * partition number and returns a pointer to its disk_partition structure.
 * It is a static helper function used internally by the disk_part_read and disk_part_write
 * functions to validate the partition before performing read/write operations.
 *
 * @param[in] drv The drive number of the disk containing the partition (0 to `MAX_DISKS - 1`).
 * @param[in] part The partition number on the disk (0 to `MAX_PARTITIONS - 1`).
 *
 * @return A pointer to the disk_partition structure on success, or NULL if an error occurs.
 */
static struct disk_partition *open_part(int drv, int part)
{
    if ((drv < 0) || (drv > MAX_DISKS)) {
        wolfBoot_printf("Attempting to access invalid drive %d\r\n", drv);
        return NULL;
    }
    if ((part < 0) || (part > MAX_PARTITIONS)) {
        wolfBoot_printf("Attempting to access invalid partition %d\r\n", part);
        return NULL;
    }
    if (Drives[drv].is_open == 0) {
        wolfBoot_printf("Drive %d not yet initialized\r\n", drv);
        return NULL;
    }
    if (part >= Drives[drv].n_parts) {
        wolfBoot_printf("No such partition %d on drive %d\r\n", part, drv);
        return NULL;
    }
    return &Drives[drv].part[part];
}

/**
 * @brief Reads data from a disk partition into the provided buffer.
 *
 * This function reads data from the specified disk partition starting from the
 * given offset and copies it into the provided buffer.
 *
 * @param[in] drv The drive number of the disk containing the partition (0 to `MAX_DISKS - 1`).
 * @param[in] part The partition number on the disk (0 to `MAX_PARTITIONS - 1`).
 * @param[in] off The offset in bytes from the start of the partition to read from.
 * @param[in] sz The size of the data to read in bytes.
 * @param[out] buf The buffer to store the read data.
 *
 * @return The number of bytes read into the buffer on success, or -1 if an error occurs.
 */
int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint32_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    int ret;
    if (p == NULL) {
        return -1;
    }
    if ((p->end - (p->start + off)) < sz) {
        len = p->end - (p->start + off);
    }
    if (len < 0) {
        return -1;
    }
    ret = disk_read(drv, p->start + off, len, buf);
    if (ret == 0) {
        ret = len;
    }
    return ret;
}

/**
 * @brief Writes data to a disk partition from the provided buffer.
 *
 * This function writes data from the provided buffer to the specified disk
 * partition starting from the given offset.
 *
 * @param[in] drv The drive number of the disk containing the partition (0 to `MAX_DISKS - 1`).
 * @param[in] part The partition number on the disk (0 to `MAX_PARTITIONS - 1`).
 * @param[in] off The offset in bytes from the start of the partition to write to.
 * @param[in] sz The size of the data to write in bytes.
 * @param[in] buf The buffer containing the data to write.
 *
 * @return The number of bytes written to the partition on success, or -1 if an error occurs.
 */
int disk_part_write(int drv, int part, uint64_t off, uint64_t sz, const uint32_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    int ret;
    if (p == NULL) {
        return -1;
    }
    if ((p->end - (p->start + off)) < sz) {
        len = p->end - (p->start + off);
    }
    if (len < 0) {
        return -1;
    }
    ret = disk_write(drv, p->start + off, len, buf);
    return ret;
}

/**
 * @brief Find a partition by its label.
 *
 * Searches for a partition with the specified label on the given drive.
 *
 * @param[in] drv The drive number to search (0 to `MAX_DISKS - 1`).
 * @param[in] label The ASCII label to search for.
 *
 * @return The partition number if found, or -1 if not found.
 */
int disk_find_partition_by_label(int drv, const char *label)
{
    struct disk_partition *p;
    int i;

    if ((drv < 0) || (drv > MAX_DISKS)) {
        return -1;
    }
    if (Drives[drv].is_open == 0) {
        return -1;
    }
    for (i = 0; i < Drives[drv].n_parts; i++) {
        p = open_part(drv, i);
        if (gpt_part_name_eq(p->name, label) == 1)
            return i;
    }
    return -1;
}

#endif /* _WOLFBOOT_DISK_C_ */
