/* target.h
 *
 * User configurable build-time options for bootloader and application offsets
 *
 * This is for Renesas RA6M4 board.
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
 */

#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#ifndef WOLFBOOT_NO_PARTITIONS
#    define WOLFBOOT_FIXED_PARTITIONS
#endif

#define WOLFBOOT_FLASH_ADDR                 0x0
#define WOLFBOOT_FLASH_SIZE                 0x100000

#define WOLFBOOT_SECTOR_SIZE                0x10000
#define WOLFBOOT_PARTITION_SIZE             0x60000

#define WOLFBOOT_PARTITION_BOOT_ADDRESS     0x10000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS   0x80000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS     0xf0000

#endif /* !H_TARGETS_TARGET_ */
