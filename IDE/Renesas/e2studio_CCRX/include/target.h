/* target.h
 *
 * User configurable build-time options for bootloader and application offsets
 *
 * target.h is automatically generated using the template in target.h.in by running
 * "make config".
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#define WOLFBOOT_FLASH_ADDR                 0xffc00000
#define WOLFBOOT_FLASH_SIZE                 (0x100000000 - WOLFBOOT_FLASH_ADDR)

#if defined(WOLFBOOT_RENESAS_TSIP)

    #define WOLFBOOT_BOOT_SIZE                  0x50000
    #define WOLFBOOT_RX_EXCVECT                 0x10000
    #define WOLFBOOT_SECTOR_SIZE                0x20000
    
    #define WOLFBOOT_PARTITION_SIZE\
            ((WOLFBOOT_FLASH_SIZE - WOLFBOOT_BOOT_SIZE -\
              WOLFBOOT_RX_EXCVECT - WOLFBOOT_SECTOR_SIZE) / 2)

    #define WOLFBOOT_PARTITION_BOOT_ADDRESS\
            (WOLFBOOT_FLASH_ADDR + WOLFBOOT_BOOT_SIZE)
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS\
            (WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)
    #define WOLFBOOT_PARTITION_SWAP_ADDRESS\
            (WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)

#elif defined(WOLFBOOT_DUALBOOT)

    #define WOLFBOOT_BOOT_SIZE                  0x10000
    #define WOLFBOOT_RX_EXCVECT                 0x10000
    #define WOLFBOOT_SECTOR_SIZE                0x0
    
    #define WOLFBOOT_PARTITION_SIZE     WOLFBOOT_FLASH_SIZE/2 - WOLFBOOT_BOOT_SIZE

    #define WOLFBOOT_PARTITION_BOOT_ADDRESS\
            (WOLFBOOT_FLASH_ADDR + WOLFBOOT_BOOT_SIZE)
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS\
            (WOLFBOOT_FLASH_ADDR + WOLFBOOT_FLASH_SIZE/2 + WOLFBOOT_BOOT_SIZE)

    #define WOLFBOOT_PARTITION_SWAP_ADDRESS     0x0

#else
    #define WOLFBOOT_BOOT_SIZE                  0x10000
    #define WOLFBOOT_RX_EXCVECT                 0x10000
    #define WOLFBOOT_SECTOR_SIZE                0x20000
    
    #define WOLFBOOT_PARTITION_SIZE\
            ((WOLFBOOT_FLASH_SIZE - WOLFBOOT_BOOT_SIZE -\
              WOLFBOOT_RX_EXCVECT - WOLFBOOT_SECTOR_SIZE) / 2)

    #define WOLFBOOT_PARTITION_BOOT_ADDRESS\
            (WOLFBOOT_FLASH_ADDR + WOLFBOOT_BOOT_SIZE)
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS\
            (WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)
    #define WOLFBOOT_PARTITION_SWAP_ADDRESS\
            (WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)
#endif

#endif /* !H_TARGETS_TARGET_ */
