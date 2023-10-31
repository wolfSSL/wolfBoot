/* gpt.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 * @file gpt.c
 * @brief GPT (GUID Partition Table) driver implementation.
 *
 * This file contains the implementation of the GPT driver used for interacting
 * with GPT partitioned disks. It provides functions for disk initialization,
 * partition reading, and writing.
 */
#ifndef GPT_C
#define GPT_C
#include <stdint.h>
#include <x86/common.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <printf.h>
#include <string.h>
#include <inttypes.h>

#define MAX_PARTITIONS 16
#define MAX_DISKS      4
#define SECTOR_SIZE    0x200
#define GPT_OFFSET     0x200
#define GPT_SIGNATURE  0x5452415020494645ULL /* "EFI PART" */

#define PTYPE_GPT         0xEE
#define P_ENTRY_START     0x01BE
#define P_BOOTSIG_OFFSET  0x01FE
#define GPT_PART_NAME_SIZE (36)

/**
 * @brief This packed structure defines the layout of an MBR partition table entry
 * used to identify GPT partitions.
 */
struct __attribute__((packed)) mbr_ptable_entry {
    uint8_t stat;
    uint8_t chs_first[3];
    uint8_t ptype;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t lba_size;
};

/**
 * @brief Structure representing a GPT (GUID Partition Table) header.
 */
struct __attribute__((packed)) guid_ptable
{
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
    uint8_t  res1[SECTOR_SIZE - 0x5C];
};

/**
 * @brief This packed structure defines the layout of a GPT partition entry
 * used to describe individual partitions on the disk.
 */
struct __attribute__((packed)) guid_part_array
{
    uint64_t type[2];
    uint64_t uuid[2];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    uint16_t name[GPT_PART_NAME_SIZE];
};

/**
 * @brief This structure holds information about a disk partition, including
 * the drive it belongs to, partition number, start, and end offsets.
 */
struct disk_partition
{
    int drv;
    int part_no;
    uint64_t start;
    uint64_t end;
    uint16_t name[GPT_PART_NAME_SIZE];
};

/**
 * @brief This structure holds information about a disk drive, including its
 * drive number, open status, the number of partitions, and an array of disk
 * partitions.
 */
struct disk_drive {
    int drv;
    int is_open;
    int n_parts;
    struct disk_partition part[MAX_PARTITIONS];
};

/**
 * @brief This array holds instances of the `struct disk_drive` representing
 * multiple disk drives (up to `MAX_DISKS`).
 */
static struct disk_drive Drives[MAX_DISKS] = {0};

static int disk_u16_ascii_eq(const uint16_t *utf16, const char *ascii)
{
    unsigned int utf16_idx;
    unsigned int i;

    if (strlen(ascii) > GPT_PART_NAME_SIZE)
        return 0;

    utf16_idx = 0;
    /* skip BOM if present */
    if (utf16[utf16_idx] == 0xfeff)
        utf16_idx = 1;
    for (i = 0; i < strlen(ascii); i++, utf16_idx++) {
        /* non-ascii character*/
        if (utf16[utf16_idx] != (uint16_t)ascii[i])
            return 0;
    }

    if (utf16_idx < GPT_PART_NAME_SIZE && utf16[utf16_idx] != 0x0)
        return 0;

    return 1;
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
    uint64_t signature = GPT_SIGNATURE;
    struct guid_ptable ptable;
    struct mbr_ptable_entry pte;
    uint16_t boot_signature;
    int gpt_found = 0;

    if ((drv < 0) || (drv > MAX_DISKS)) {
        wolfBoot_printf("Attempting to access invalid drive %d\r\n", drv);
        return -1;
    }

    wolfBoot_printf("Reading MBR...\r\n");
    for (i = 0; i < 4; i++) {
        r = ata_drive_read(drv, P_ENTRY_START + 0x10 * i, sizeof(struct mbr_ptable_entry),
                (void *)&pte);
        if ((r > 0) && (pte.ptype == PTYPE_GPT)) {
            wolfBoot_printf("Found GPT PTE at sector %u\r\n", pte.lba_first);
            gpt_found = 1;
            break;
        }
    }
    if (!gpt_found) {
        wolfBoot_printf("Cannot find valid partition table entry for GPT\r\n");
        return -1;
    }


    r = ata_drive_read(drv, P_BOOTSIG_OFFSET, sizeof(uint16_t), (void *)&boot_signature);
    if ((r > 0) && (boot_signature == 0xAA55)) {
        wolfBoot_printf("Found valid boot signature in MBR\r\n");
    } else {
        wolfBoot_printf("FATAL: Invalid boot signature in MBR!\r\n");
        return -1;
    }

    Drives[drv].is_open = 1;
    Drives[drv].drv = drv;
    Drives[drv].n_parts = 0;
    r = ata_drive_read(drv, SECTOR_SIZE * pte.lba_first, SECTOR_SIZE,
            (void *)&ptable);
    if (r > 0) {
        if (ptable.signature == signature) {
            wolfBoot_printf("Valid GPT partition table\r\n");
            wolfBoot_printf("Current LBA: 0x%llx \r\n", ptable.main_lba);
            wolfBoot_printf("Backup LBA: 0x%llx \r\n", ptable.backup_lba);
            wolfBoot_printf("Max number of partitions: %d\r\n", ptable.n_part);
            n_parts = ptable.n_part;
            if (ptable.n_part > MAX_PARTITIONS) {
                n_parts = MAX_PARTITIONS;
                wolfBoot_printf("Software limited: only allowing up to %d partitions per disk.\r\n", n_parts);
            }
            wolfBoot_printf("Disk size: %d\r\n", (1 + ptable.last_usable - ptable.first_usable) * SECTOR_SIZE);
        } else {
            wolfBoot_printf("Invalid partition table\r\n");
            return -1;
        }
    } else {
        wolfBoot_printf("ATA: Read failed\r\n");
        return -1;
    }
    for (i = 0; i < n_parts; i++) {
        struct guid_part_array pa;
        uint64_t address = ptable.start_array * SECTOR_SIZE +
            i * ptable.array_sz;
        r = ata_drive_read(drv, address, ptable.array_sz, (void *)&pa);
        if (pa.type[0] != 0 || pa.type[1] != 0) {
            uint64_t size;
            uint32_t part_count;
            if (pa.first > pa.last) {
                wolfBoot_printf("Bad geometry for partition %d\r\n", part_count);
                break;
            }
            size = (1 + pa.last - pa.first) * SECTOR_SIZE;
            part_count = Drives[drv].n_parts;
            Drives[drv].n_parts++;
            Drives[drv].part[part_count].drv = drv;
            Drives[drv].part[part_count].start = pa.first * SECTOR_SIZE;
            Drives[drv].part[part_count].end = (pa.last * SECTOR_SIZE - 1);
            memcpy(&Drives[drv].part[part_count].name, (uint8_t*)&pa.name, sizeof(pa.name));
            wolfBoot_printf("disk%d.p%u ", drv, part_count);
            wolfBoot_printf("(%x_%xh", (uint32_t)(size>>32), (uint32_t)size);
            wolfBoot_printf("@ %x_%x)\r\n", (uint32_t)((pa.first * SECTOR_SIZE) >> 32),
                            (uint32_t)((pa.first * SECTOR_SIZE)));
        } else
            break;
    }
    wolfBoot_printf("Total partitions on disk%u: %u\r\n", drv, Drives[drv].n_parts);
    return Drives[drv].n_parts;
}
/**
 * @brief Opens a disk partition and returns a pointer to its structure.
 *
 * This function opens a disk partition with the specified drive number and
 * partition number and returns a pointer to its disk_partition structure.
 * It is a static helper function used internally by the disk_read and disk_write
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
int disk_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    int ret;
    if (p == NULL)
        return -1;

    if ((p->end - (p->start + off)) < sz) {
        len = p->end - (p->start + off);
    }
    if (len < 0) {
        return -1;
    }
    ret = ata_drive_read(drv, p->start + off, len, buf);
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
int disk_write(int drv, int part, uint64_t off, uint64_t sz, const uint8_t *buf)
{
    struct disk_partition *p = open_part(drv, part);
    int len = sz;
    int ret;
    if (p == NULL)
        return -1;

    if ((p->end - (p->start + off)) < sz) {
        len = p->end - (p->start + off);
    }
    if (len < 0) {
        return -1;
    }
    ret = ata_drive_write(drv, p->start + off, len, buf);
    return ret;
}

int disk_find_partion_by_label(int drv, const char *label)
{
    struct disk_partition *p;
    int i;

    if ((drv < 0) || (drv > MAX_DISKS))
        return -1;

    if (Drives[drv].is_open == 0)
        return -1;

    for (i = 0; i < Drives[drv].n_parts; i++) {
        p = open_part(drv, i);
        if (disk_u16_ascii_eq(p->name, label) == 1)
            return i;
    }
    return -1;
}
#endif /* GPT_C */
