/* gpt.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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


struct __attribute__((packed)) mbr_ptable_entry {
    uint8_t stat;
    uint8_t chs_first[3];
    uint8_t ptype;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t lba_size;
};

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

struct __attribute__((packed)) guid_part_array
{
    uint64_t type[2];
    uint64_t uuid[2];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    int16_t name[72];
};

struct disk_partition
{
    int drv;
    int part_no;
    uint64_t start;
    uint64_t end;
};

struct disk_drive {
    int drv;
    int is_open;
    int n_parts;
    struct disk_partition part[MAX_PARTITIONS];
};

static struct disk_drive Drives[MAX_DISKS] = { };

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
