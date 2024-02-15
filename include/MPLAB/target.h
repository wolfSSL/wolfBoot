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

#define WOLFBOOT_SECTOR_SIZE                 0x8000

#ifdef WOLFBOOT_FIXED_PARTITIONS

#ifdef ARCH_SIM
    #include <stdint.h>
    /* use runtime ram base for simulator */
    extern uint8_t *sim_ram_base;
    #undef  ARCH_FLASH_OFFSET
    #define ARCH_FLASH_OFFSET ((size_t)sim_ram_base)
    #define WOLFBOOT_PART_USE_ARCH_OFFSET
#endif

#ifdef PULL_LINKER_DEFINES
    #include <stdint.h>

    /* linker script variables */
    extern const uint32_t _wolfboot_partition_boot_address[];
    extern const uint32_t _wolfboot_partition_size[];
    extern const uint32_t _wolfboot_partition_update_address[];
    extern const uint32_t _wolfboot_partition_swap_address[];

    /* create plain integers from linker script variables */
    static const uint32_t WOLFBOOT_PARTITION_BOOT_ADDRESS = (uint32_t)_wolfboot_partition_boot_address;
    static const uint32_t WOLFBOOT_PARTITION_SIZE = (uint32_t)_wolfboot_partition_size;
    static const uint32_t WOLFBOOT_PARTITION_UPDATE_ADDRESS = (uint32_t)_wolfboot_partition_update_address;
    static const uint32_t WOLFBOOT_PARTITION_SWAP_ADDRESS = (uint32_t)_wolfboot_partition_swap_address;
#else
    #if defined(WOLFBOOT_PART_USE_ARCH_OFFSET)
        #if !defined(EXT_FLASH) || (defined(EXT_FLASH) && !defined(PART_BOOT_EXT))
            #define WOLFBOOT_PARTITION_BOOT_ADDRESS   (ARCH_FLASH_OFFSET + 0x08000)
        #endif
        #if !defined(EXT_FLASH) || (defined(EXT_FLASH) && !defined(PART_UPDATE_EXT))
            #define WOLFBOOT_PARTITION_UPDATE_ADDRESS (ARCH_FLASH_OFFSET + 0x088000)
        #endif
        #if !defined(EXT_FLASH) || (defined(EXT_FLASH) && !defined(PART_SWAP_EXT))
            #define WOLFBOOT_PARTITION_SWAP_ADDRESS   (ARCH_FLASH_OFFSET + 0x200000)
        #endif
    #endif

    /* use values provided on input template parsing */
    #ifndef WOLFBOOT_PARTITION_BOOT_ADDRESS
    #define WOLFBOOT_PARTITION_BOOT_ADDRESS   0x08000
    #endif
    #ifndef WOLFBOOT_PARTITION_UPDATE_ADDRESS
    #define WOLFBOOT_PARTITION_UPDATE_ADDRESS 0x088000
    #endif
    #ifndef WOLFBOOT_PARTITION_SWAP_ADDRESS
    #define WOLFBOOT_PARTITION_SWAP_ADDRESS   0x200000
    #endif
    #ifndef WOLFBOOT_PARTITION_SIZE
    #define WOLFBOOT_PARTITION_SIZE           0x20000
    #endif
#endif

#define WOLFBOOT_DTS_BOOT_ADDRESS             
#define WOLFBOOT_DTS_UPDATE_ADDRESS           

#endif /* WOLFBOOT_FIXED_PARTITIONS */

/* Load address in RAM for staged OS (update_ram only) */
#define WOLFBOOT_LOAD_ADDRESS                 
#define WOLFBOOT_LOAD_DTS_ADDRESS             


#endif /* !H_TARGETS_TARGET_ */
