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
 * @brief Disk driver with GPT and MBR partition table support.
 *
 * This file contains the disk driver that uses disk I/O operations.
 * It supports both GPT and MBR partition tables:
 *   - GPT: Uses protective MBR + GPT header (via src/gpt.c)
 *   - MBR: Falls back to standard MBR partition entries
 *
 * MBR fallback is needed for platforms like Versal where the boot ROM
 * requires MBR but wolfBoot needs to read data partitions.
 */
#ifndef _WOLFBOOT_DISK_C_
#define _WOLFBOOT_DISK_C_

#include <stdint.h>
#include <string.h>

#include "wolfboot/wolfboot.h"
#include "disk.h"
#include "printf.h"

/**
 * @brief This array holds instances of the `struct disk_drive` representing
 * multiple disk drives (up to `MAX_DISKS`).
 */
static struct disk_drive Drives[MAX_DISKS] = {0};

/**
 * @brief Parse MBR partition table entries.
 *
 * Reads up to 4 primary MBR partition entries and populates the drive's
 * partition array. Start/end are stored as byte offsets (LBA * sector size).
 *
 * @param[in,out] drive Pointer to the disk_drive structure to populate.
 * @param[in] mbr_sector The 512-byte MBR sector data.
 * @return The number of partitions found, or -1 on error.
 */
static int disk_open_mbr(struct disk_drive *drive, const uint8_t *mbr_sector)
{
    uint32_t i;
    const struct gpt_mbr_part_entry *pte;

    for (i = 0; i < 4; i++) {
        pte = (const struct gpt_mbr_part_entry *)(mbr_sector +
                GPT_MBR_ENTRY_START + (i * sizeof(struct gpt_mbr_part_entry)));

        /* Skip empty entries (type 0) and extended partition types */
        if (pte->ptype == 0x00 || pte->ptype == 0x05 || pte->ptype == 0x0F ||
            pte->ptype == 0x85) {
            continue;
        }
        if (pte->lba_first == 0 || pte->lba_size == 0) {
            continue;
        }

        {
            uint32_t n = drive->n_parts;
            uint64_t start_bytes = (uint64_t)pte->lba_first * GPT_SECTOR_SIZE;
            uint64_t end_bytes = start_bytes +
                ((uint64_t)pte->lba_size * GPT_SECTOR_SIZE) - 1;

            if (n >= MAX_PARTITIONS)
                break;

            drive->part[n].drv = drive->drv;
            drive->part[n].start = start_bytes;
            drive->part[n].end = end_bytes;
            memset(drive->part[n].name, 0, sizeof(drive->part[n].name));
            drive->n_parts++;

            wolfBoot_printf("  MBR part %u: type=0x%02x, start=0x%x, "
                "size=%uMB\r\n", i + 1, pte->ptype,
                (uint32_t)start_bytes,
                (uint32_t)(pte->lba_size / 2048));
        }
    }

    return drive->n_parts;
}

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
    uint8_t sector[GPT_SECTOR_SIZE] XALIGNED(4);

    if ((drv < 0) || (drv >= MAX_DISKS)) {
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

    Drives[drv].is_open = 1;
    Drives[drv].drv = drv;
    Drives[drv].n_parts = 0;

    /* Try GPT first: check for protective MBR with type 0xEE */
    if (gpt_check_mbr_protective((uint8_t*)sector, &gpt_lba) == 0) {
        struct guid_ptable ptable;

        wolfBoot_printf("Found GPT PTE at sector %u\r\n", gpt_lba);

        /* Read GPT header */
        r = disk_read(drv, GPT_SECTOR_SIZE * gpt_lba, GPT_SECTOR_SIZE, sector);
        if (r < 0) {
            wolfBoot_printf("Disk read failed\r\n");
            Drives[drv].is_open = 0;
            return -1;
        }

        /* Parse and validate GPT header */
        if (gpt_parse_header((uint8_t*)sector, &ptable) != 0) {
            wolfBoot_printf("Invalid GPT header\r\n");
            Drives[drv].is_open = 0;
            return -1;
        }

        wolfBoot_printf("Valid GPT partition table\r\n");
        wolfBoot_printf("Max number of partitions: %d\r\n", ptable.n_part);

        n_parts = ptable.n_part;
        if (n_parts > MAX_PARTITIONS)
            n_parts = MAX_PARTITIONS;

        /* Read and parse GPT partition entries */
        for (i = 0; i < n_parts; i++) {
            struct gpt_part_info part_info;
            uint64_t address = (ptable.start_array * GPT_SECTOR_SIZE) +
                (i * ptable.array_sz);
            uint8_t entry_buf[GPT_PART_ENTRY_SIZE] XALIGNED(4);

            if (ptable.array_sz > sizeof(entry_buf))
                break;

            r = disk_read(drv, address, ptable.array_sz, entry_buf);
            if (r < 0) {
                Drives[drv].is_open = 0;
                return -1;
            }

            if (gpt_parse_partition((uint8_t*)entry_buf, ptable.array_sz,
                    &part_info) == 0) {
                uint64_t size = part_info.end - part_info.start + 1;
                uint32_t pc = Drives[drv].n_parts;
                Drives[drv].n_parts++;
                Drives[drv].part[pc].drv = drv;
                Drives[drv].part[pc].start = part_info.start;
                Drives[drv].part[pc].end = part_info.end;
                memcpy(&Drives[drv].part[pc].name, part_info.name,
                       sizeof(part_info.name));

                wolfBoot_printf("  GPT part %u: %x_%xh @ %x_%x\r\n", pc,
                    (uint32_t)(size >> 32), (uint32_t)size,
                    (uint32_t)(part_info.start >> 32),
                    (uint32_t)(part_info.start));
            } else {
                break; /* End of used entries */
            }
        }
    } else {
        const uint16_t *boot_sig = (const uint16_t *)(sector +
            GPT_MBR_BOOTSIG_OFFSET);

        /* Check MBR boot signature (0xAA55) */
        if (*boot_sig != GPT_MBR_BOOTSIG_VALUE) {
            wolfBoot_printf("No valid partition table found\r\n");
            Drives[drv].is_open = 0;
            return -1;
        }

        wolfBoot_printf("Found MBR partition table\r\n");
        if (disk_open_mbr(&Drives[drv], sector) < 0) {
            wolfBoot_printf("Failed to parse MBR\r\n");
            Drives[drv].is_open = 0;
            return -1;
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
    if ((drv < 0) || (drv >= MAX_DISKS)) {
        wolfBoot_printf("Attempting to access invalid drive %d\r\n", drv);
        return NULL;
    }
    if ((part < 0) || (part >= MAX_PARTITIONS)) {
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
int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    uint64_t start;
    int ret;
    if (p == NULL) {
        return -1;
    }
    start = p->start + off;
    /* overflow */
    if (start < p->start) {
        return -1;
    }
    /* p->end is the last valid byte we can read */
    if (start > p->end) {
        return -1;
    }
    if ((p->end - start + 1) < sz) {
        len = (uint32_t)(p->end - start + 1);
    }
    if (len < 0) {
        return -1;
    }
    ret = disk_read(drv, start, len, buf);
#ifdef DEBUG_DISK
    wolfBoot_printf("disk_part_read: drv: %d, part: %d, off: %llu, sz: %llu, "
        "buf: %p, ret %d\r\n", drv, part, p->start + off, len, buf, ret);
#endif
    if (ret == 0) {
        ret = len; /* success expects to return the number of bytes read */
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
int disk_part_write(int drv, int part, uint64_t off, uint64_t sz, const uint8_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    uint64_t start;
    int ret;
    if (p == NULL) {
        return -1;
    }
    start = p->start + off;
    /* overflow */
    if (start < p->start) {
        return -1;
    }
    if (start > p->end) {
        return -1;
    }
    if ((p->end - start + 1) < sz) {
        len = (uint32_t)(p->end - start + 1);
    }
    if (len < 0) {
        return -1;
    }
    ret = disk_write(drv, start, len, buf);
#ifdef DEBUG_DISK
    wolfBoot_printf("disk_part_write: drv: %d, part: %d, off: %llu, sz: %llu, "
        "buf: %p, ret %d\r\n", drv, part, p->start + off, sz, buf, ret);
#endif
    if (ret == 0) {
        ret = len; /* success expects to return the number of bytes written */
    }
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

    if ((drv < 0) || (drv >= MAX_DISKS)) {
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
