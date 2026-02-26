/* disk.h
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

#ifndef _WOLFBOOT_DISK_H
#define _WOLFBOOT_DISK_H

#include <stdint.h>
#include "gpt.h"

/* cap DISK I/O write operation to a reasonable size */
#define DISK_IO_MAX_SIZE 0x7FFFFFFFUL

#ifndef MAX_PARTITIONS
#define MAX_PARTITIONS 16
#endif
#ifndef MAX_DISKS
#define MAX_DISKS      4
#endif

/**
 * @brief This structure holds information about a disk partition, including
 * the drive it belongs to, partition number, start, and end offsets.
 */
struct disk_partition {
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

/* user supplied functions */
int disk_init(int drv);
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf);
int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf);
void disk_close(int drv);

/* standard functions */
int disk_open(int drv);
int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf);
int disk_part_write(int drv, int part, uint64_t off, uint64_t sz, const uint8_t *buf);
int disk_find_partition_by_label(int drv, const char *label);

#endif /* _WOLFBOOT_DISK_H */

