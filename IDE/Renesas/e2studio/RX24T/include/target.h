/* target.h
 *
 * Flash layout for the RX24T CC-RX projects, the same as
 * config/examples/renesas-rx24t.config.
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

#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_

#ifndef WOLFBOOT_NO_PARTITIONS
#    define WOLFBOOT_FIXED_PARTITIONS
#endif

/* wolfBoot: 0xFFFF8000-0xFFFFFFFF (last 32 KB, with the vector tables) */
#define WOLFBOOT_ORIGIN                     0xFFFF8000
#define BOOTLOADER_PARTITION_SIZE           0x8000
#define WOLFBOOT_SECTOR_SIZE                0x800
#define WOLFBOOT_PARTITION_SIZE             0x18000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS     0xFFFC0000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS   0xFFFD8000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS     0xFFFF0000

#endif /* !H_TARGETS_TARGET_ */
