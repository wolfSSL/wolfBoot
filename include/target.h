/* target.h
 *
 * User configurable build-time options for bootloader and application offsets
 *
 *
 * Copyright (C) 2019 wolfSSL Inc.
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

#ifndef ARCH_FLASH_OFFSET
    #define ARCH_FLASH_OFFSET 0x0
#endif

/* Example flash partitioning.
 * Ensure that your firmware entry point is
 * at FLASH_AREA_IMAGE_0_OFFSET + 0x100
 */

#ifndef WOLFBOOT_SECTOR_SIZE
    #define WOLFBOOT_SECTOR_SIZE                 0x20000
#endif
#ifndef WOLFBOOT_PARTITION_BOOT_ADDRESS
    #define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x20000
#endif

#ifdef EXT_FLASH

/* Test configuration with 1MB external memory */
/* (Addresses are relative to the beginning of the ext)*/

#ifndef WOLFBOOT_PARTITION_SIZE
    #define WOLFBOOT_PARTITION_SIZE              0x80000
#endif
#ifndef WOLFBOOT_PARTITION_UPDATE_ADDRESS
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x00000
#endif
#ifndef WOLFBOOT_PARTITION_SWAP_ADDRESS
    #define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x80000
#endif

#else

/* Test configuration with internal memory */
#ifndef WOLFBOOT_PARTITION_SIZE
    #define WOLFBOOT_PARTITION_SIZE              0x20000
#endif
#ifndef WOLFBOOT_PARTITION_UPDATE_ADDRESS
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x40000
#endif
#ifndef WOLFBOOT_PARTITION_SWAP_ADDRESS
    #define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x60000
#endif

#endif

#endif /* !H_TARGETS_TARGET_ */
