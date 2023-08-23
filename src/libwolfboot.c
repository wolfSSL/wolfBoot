/* libwolfboot.c
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
/**
 * @file libwolfboot.c
 *
 * @brief wolfBoot library implementation.
 *
 * This file contains the implementation of the wolfBoot library.
 */
#include <stdint.h>


#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "printf.h"

#ifdef UNIT_TEST
/**
 * @def unit_dbg
 * @brief Conditional debug macro for unit tests.
 *
 * Conditional debug macro for unit tests, redirects to wolfBoot_printf.
 */
#   define unit_dbg wolfBoot_printf
#else
/**
 * @def unit_dbg
 * @brief Empty macro for unit_dbg in non-test builds.
 *
 * Empty macro for unit_dbg in non-test builds.
 */
#   define unit_dbg(...) do{}while(0)
#endif

#ifndef TRAILER_SKIP
/**
 * @def TRAILER_SKIP
 * @brief Trailer skip value for partition encryption.
 *
 * Trailer skip value for partition encryption, defaults to 0 if not defined.
 */
#   define TRAILER_SKIP 0
#endif

#include <stddef.h> /* for size_t */

#if defined(EXT_ENCRYPTED)
static int encrypt_initialized = 0;
static uint8_t encrypt_iv_nonce[ENCRYPT_NONCE_SIZE];
    #if defined(__WOLFBOOT)
        #include "encrypt.h"
    #elif !defined(XMEMSET)
        #include <string.h>
        #define XMEMSET memset
        #define XMEMCPY memcpy
        #define XMEMCMP memcmp
    #endif
#endif

#if defined(EXT_FLASH) && defined(EXT_ENCRYPTED)
    #define ENCRYPT_TMP_SECRET_OFFSET (WOLFBOOT_PARTITION_SIZE - \
                         (TRAILER_SKIP + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE))
    #define TRAILER_OVERHEAD (4 + 1 + (WOLFBOOT_PARTITION_SIZE  / \
                             (2 * WOLFBOOT_SECTOR_SIZE)))
                             /* MAGIC (4B) + PART_FLAG (1B) + (N_SECTORS / 2) */
    #define START_FLAGS_OFFSET (ENCRYPT_TMP_SECRET_OFFSET - TRAILER_OVERHEAD)
    #define SECTOR_FLAGS_SIZE WOLFBOOT_SECTOR_SIZE - (4 + 1 + \
        ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE)
    /* MAGIC (4B) + PART_FLAG (1B) + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE */
#else
    #define ENCRYPT_TMP_SECRET_OFFSET (WOLFBOOT_PARTITION_SIZE - (TRAILER_SKIP))
    #define SECTOR_FLAGS_SIZE WOLFBOOT_SECTOR_SIZE - (4 + 1)
    /* MAGIC (4B) + PART_FLAG (1B) */
#endif /* EXT_FLASH && EXT_ENCRYPTED */

#if !defined(__WOLFBOOT) && !defined(UNIT_TEST)
    #define XMEMSET memset
    #define XMEMCPY memcpy
    #define XMEMCMP memcmp
#endif

#ifndef NULL
#   define NULL (void *)0
#endif

#ifndef NVM_CACHE_SIZE
#define NVM_CACHE_SIZE WOLFBOOT_SECTOR_SIZE
#endif

#ifdef BUILD_TOOL
/* Building for a local utility tool */
#undef EXT_FLASH
#undef EXT_ENCRYPTED
#undef WOLFBOOT_FIXED_PARTITIONS
#endif

#ifdef EXT_FLASH
static uint32_t ext_cache;
#endif


#if defined(__WOLFBOOT) || defined (UNIT_TEST)
#define WOLFSSL_MISC_INCLUDED /* allow misc.c code to be inlined */
#include <wolfcrypt/src/misc.c> /* for ByteReverseWord32 */
static uint32_t wb_reverse_word32(uint32_t x)
{
    return ByteReverseWord32(x);
}
#endif


static const uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
/* Top addresses for FLAGS field
 *  - PART_BOOT_ENDFLAGS = top of flags for BOOT partition
 *  - PART_UPDATE_ENDFLAGS = top of flags for UPDATE_PARTITION
 */

#ifndef PART_BOOT_ENDFLAGS
#define PART_BOOT_ENDFLAGS   (WOLFBOOT_PARTITION_BOOT_ADDRESS + ENCRYPT_TMP_SECRET_OFFSET)
#endif
#define FLAGS_BOOT_EXT() PARTN_IS_EXT(PART_BOOT)

#ifdef FLAGS_HOME
/*
 * In FLAGS_HOME mode, all FLAGS live at the end of the boot partition:
 *                        / -12    /-8       /-4     / END
 *  |Sn| ... |S2|S1|S0|PU|  MAGIC  |X|X|X|PB| MAGIC |
 *   ^--sectors   --^  ^--update           ^---boot partition
 *      flags             partition            flag
 *                        flag
 *
 * */
#define PART_UPDATE_ENDFLAGS (PART_BOOT_ENDFLAGS - 8)
#define FLAGS_UPDATE_EXT() PARTN_IS_EXT(PART_BOOT)
#else
/* FLAGS are at the end of each partition */
#define PART_UPDATE_ENDFLAGS (WOLFBOOT_PARTITION_UPDATE_ADDRESS + ENCRYPT_TMP_SECRET_OFFSET)
#define FLAGS_UPDATE_EXT() PARTN_IS_EXT(PART_UPDATE)
#endif

#ifdef NVM_FLASH_WRITEONCE
/* Some internal FLASH memory models don't allow
 * multiple writes after erase in the same page/area.
 *
 * NVM_FLASH_WRITEONCE uses a redundant two-sector model
 * to mitigate the effect of power failures.
 *
 */

#ifndef WOLFBOOT_FLAGS_INVERT
#define FLAG_CMP(a,b) ((a < b)? 0 : 1)
#else
#define FLAG_CMP(a,b) ((a > b)? 0 : 1)
#endif

#include <stddef.h>
#include <string.h>
static uint8_t NVM_CACHE[NVM_CACHE_SIZE] __attribute__((aligned(16)));
static int nvm_cached_sector = 0;

#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
#endif
static uint8_t get_base_offset(uint8_t *base, uintptr_t off)
{
    return *(base - off); /* ignore array bounds error */
}
#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif

static int RAMFUNCTION nvm_select_fresh_sector(int part)
{
    int sel;
    uintptr_t off;
    uint8_t *base;
    uint8_t* addrErase;
    uint32_t word_0;
    uint32_t word_1;

    /* if FLAGS_HOME check both boot and update for changes */
#ifdef FLAGS_HOME
    base = (uint8_t *)PART_BOOT_ENDFLAGS;
    addrErase = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS +
        WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
#else
    if (part == PART_BOOT) {
        base = (uint8_t *)PART_BOOT_ENDFLAGS;
        addrErase = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS +
            WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
    }
    else {
        base = (uint8_t *)PART_UPDATE_ENDFLAGS;
        addrErase = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS +
            WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
    }
#endif

#ifdef EXT_ENCRYPTED
#ifndef FLAGS_HOME
    if (part == PART_BOOT)
#endif
    {
        word_0 = *((uint32_t *)(ENCRYPT_TMP_SECRET_OFFSET +
            WOLFBOOT_PARTITION_BOOT_ADDRESS));
        word_1 = *((uint32_t *)(ENCRYPT_TMP_SECRET_OFFSET +
            WOLFBOOT_PARTITION_BOOT_ADDRESS - WOLFBOOT_SECTOR_SIZE));

        if (word_0 == FLASH_WORD_ERASED && word_1 !=
            FLASH_WORD_ERASED) {
            sel = 1;
            goto finish;
        }
        else if (word_0 != FLASH_WORD_ERASED && word_1 ==
            FLASH_WORD_ERASED) {
            sel = 0;
            goto finish;
        }
    }
#endif

    /* check magic in case the sector is corrupt */
    word_0 = *((uint32_t*)(base - sizeof(uint32_t)));
    word_1 = *((uint32_t*)(base - WOLFBOOT_SECTOR_SIZE - sizeof(uint32_t)));

    if (word_0 == WOLFBOOT_MAGIC_TRAIL && word_1 != WOLFBOOT_MAGIC_TRAIL) {
        sel = 0;
        goto finish;
    }
    else if (word_0 != WOLFBOOT_MAGIC_TRAIL && word_1 == WOLFBOOT_MAGIC_TRAIL) {
        sel = 1;
        goto finish;
    }

/* try the update magic as well */
#ifdef FLAGS_HOME
    /* check magic in case the sector is corrupt */
    word_0 = *((uint32_t*)(PART_UPDATE_ENDFLAGS - sizeof(uint32_t)));
    word_1 = *((uint32_t*)(PART_UPDATE_ENDFLAGS - WOLFBOOT_SECTOR_SIZE -
        sizeof(uint32_t)));

    if (word_0 == WOLFBOOT_MAGIC_TRAIL && word_1 != WOLFBOOT_MAGIC_TRAIL) {
        sel = 0;
        goto finish;
    }
    else if (word_0 != WOLFBOOT_MAGIC_TRAIL && word_1 == WOLFBOOT_MAGIC_TRAIL) {
        sel = 1;
        goto finish;
    }
#endif

    /* Default to last sector if no match is found */
    sel = 0;

    /* Select the sector with more flags set */
    for (off = 1; off < WOLFBOOT_SECTOR_SIZE; off++) {
        uint8_t byte_0 = get_base_offset(base, off);
        uint8_t byte_1 = get_base_offset(base, (WOLFBOOT_SECTOR_SIZE + off));

        if (byte_0 == FLASH_BYTE_ERASED && byte_1 != FLASH_BYTE_ERASED) {
            sel = 1;
            break;
        }
        else if (byte_0 != FLASH_BYTE_ERASED && byte_1 == FLASH_BYTE_ERASED) {
            sel = 0;
            break;
        }
        else if ((byte_0 == FLASH_BYTE_ERASED) &&
                (byte_1 == FLASH_BYTE_ERASED)) {
#ifdef FLAGS_HOME
            /* if we're still checking boot flags, check update flags */
            if (base - off > (uint8_t*)PART_UPDATE_ENDFLAGS) {
                base = (uint8_t *)PART_UPDATE_ENDFLAGS;
                off = 0;
                continue;
            }
#endif
            /* First time boot?  Assume no pending update */
            if (off == 1) {
                sel=0;
                break;
            }
            /* Examine previous position one byte ahead */
            byte_0 = get_base_offset(base, (1 - off));
            byte_1 = get_base_offset(base, (1 - (WOLFBOOT_SECTOR_SIZE + off)));

            sel = FLAG_CMP(byte_0, byte_1);
            break;
        }
    }
finish:
    /* Erase the non-selected partition */
    addrErase -= WOLFBOOT_SECTOR_SIZE * (!sel);
    if (*((uint32_t*)(addrErase + WOLFBOOT_SECTOR_SIZE - sizeof(uint32_t)))
            != FLASH_WORD_ERASED) {
        hal_flash_erase((uintptr_t)addrErase, WOLFBOOT_SECTOR_SIZE);
    }
    return sel;
}

/**
 * @brief Write the trailer in a non-volatile memory.
 *
 * This function writes the trailer in a non-volatile memory.
 *
 * @param[in] part Partition number.
 * @param[in] addr Address of the trailer.
 * @param[in] val New value to write in the trailer.
 * @return 0 on success, -1 on failure.
 */
static int RAMFUNCTION trailer_write(uint8_t part, uintptr_t addr, uint8_t val)
{
    uintptr_t addr_align = (size_t)(addr & (~(NVM_CACHE_SIZE - 1)));
    uintptr_t addr_read, addr_write;
    uintptr_t addr_off = addr & (NVM_CACHE_SIZE - 1);
    int ret = 0;

    nvm_cached_sector = nvm_select_fresh_sector(part);
    addr_read = addr_align - (nvm_cached_sector * NVM_CACHE_SIZE);
    XMEMCPY(NVM_CACHE, (void*)addr_read, NVM_CACHE_SIZE);
    NVM_CACHE[addr_off] = val;

    /* Calculate write address */
    addr_write = addr_align - ((!nvm_cached_sector) * NVM_CACHE_SIZE);

    /* Ensure that the destination was erased, or force erase */
    if (*((uint32_t *)(addr_write + NVM_CACHE_SIZE - sizeof(uint32_t)))
            != FLASH_WORD_ERASED)
    {
        hal_flash_erase(addr_write, NVM_CACHE_SIZE);
    }
#if FLASHBUFFER_SIZE != WOLFBOOT_SECTOR_SIZE
    addr_off = 0;
    while ((addr_off < WOLFBOOT_SECTOR_SIZE) && (ret == 0)) {
        ret = hal_flash_write(addr_write + addr_off, NVM_CACHE + addr_off,
            FLASHBUFFER_SIZE);
        addr_off += FLASHBUFFER_SIZE;
    }
#else
    ret = hal_flash_write(addr_write, NVM_CACHE, NVM_CACHE_SIZE);
#endif

    /* Once a copy has been written, erase the older sector */
    ret = hal_flash_erase(addr_read, NVM_CACHE_SIZE);
    nvm_cached_sector = !nvm_cached_sector;
    return ret;
}

/**
 * @brief Write the partition magic in a non-volatile memory.
 *
 * This function writes the partition magic in a non-volatile memory.
 *
 * @param[in] part Partition number.
 * @param[in] addr Address of the magic trailer.
 * @return 0 on success, -1 on failure.
 */
static int RAMFUNCTION partition_magic_write(uint8_t part, uintptr_t addr)
{
    uintptr_t off = addr % NVM_CACHE_SIZE;
    uintptr_t base = (uintptr_t)addr - off;
    uintptr_t addr_read, addr_write;
    int ret;
    nvm_cached_sector = nvm_select_fresh_sector(part);
    addr_read = base - (nvm_cached_sector * NVM_CACHE_SIZE);
    addr_write = base - (!nvm_cached_sector * NVM_CACHE_SIZE);
    XMEMCPY(NVM_CACHE, (void*)addr_read, NVM_CACHE_SIZE);
    XMEMCPY(NVM_CACHE + off, &wolfboot_magic_trail, sizeof(uint32_t));
    ret = hal_flash_write(addr_write, NVM_CACHE, WOLFBOOT_SECTOR_SIZE);
    nvm_cached_sector = !nvm_cached_sector;
    ret = hal_flash_erase(addr_read, WOLFBOOT_SECTOR_SIZE);
    return ret;
}

#else
#   define trailer_write(part,addr, val) hal_flash_write(addr, (void *)&val, 1)
#   define partition_magic_write(part,addr) hal_flash_write(addr, \
                                (void*)&wolfboot_magic_trail, sizeof(uint32_t));
#endif

#ifdef EXT_FLASH

/**
 * @brief Get the trailer at a specific address in a fixed partition.
 *
 * This function retrieves the trailer at a specific address in a fixed partition.
 *
 * @param[in] part Partition number.
 * @param[in] at Address offset.
 * @return Pointer to the trailer at the specified address.
 */
static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    uint8_t *ret = NULL;
    uint32_t sel_sec = 0;
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()){
            ext_flash_check_read(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            ret = (uint8_t *)&ext_cache;
        } else {
            /* only internal flash should be writeonce */
#ifdef NVM_FLASH_WRITEONCE
            sel_sec = nvm_select_fresh_sector(part);
#endif
            ret = (void *)(PART_BOOT_ENDFLAGS -
                    (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            ret = (uint8_t *)&ext_cache;
        } else {
            /* only internal flash should be writeonce */
#ifdef NVM_FLASH_WRITEONCE
            sel_sec = nvm_select_fresh_sector(part);
#endif
            ret = (void *)(PART_UPDATE_ENDFLAGS -
                    (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
        }
    }
    return ret;
}

/**
 * @brief Set the trailer at a specific address in an external flash.
 *
 * This function sets the trailer at a specific address in an external flash.
 *
 * @param[in] part Partition number.
 * @param[in] at Address offset.
 * @param[in] val New value to set in the trailer.
 */
static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()) {
            ext_flash_check_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&val, 1);
        } else {
            trailer_write(part, PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&val, 1);
        } else {
            trailer_write(part, PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
}

/**
 * @brief Set the partition magic trailer in an external flash.
 *
 * This function sets the partition magic trailer in an external flash.
 *
 * @param[in] part Partition number.
 */
static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()) {
            ext_flash_check_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t),
                (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            partition_magic_write(part, PART_BOOT_ENDFLAGS - sizeof(uint32_t));
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t),
                (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            partition_magic_write(part, PART_UPDATE_ENDFLAGS - sizeof(uint32_t));
        }
    }
}
#elif !defined(WOLFBOOT_FIXED_PARTITIONS)
static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    (void)part;
    (void)at;
    return 0;
}
static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    (void)part;
    (void)at;
    (void)val;
    return;
}
static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    (void)part;
    return;
}

#else
static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    uint8_t *ret = NULL;
    uint32_t sel_sec = 0;
#ifdef NVM_FLASH_WRITEONCE
    sel_sec = nvm_select_fresh_sector(part);
#endif
    if (part == PART_BOOT) {
    	ret = (void *)(PART_BOOT_ENDFLAGS -
                (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
    }
    else if (part == PART_UPDATE) {
    	ret = (void *)(PART_UPDATE_ENDFLAGS -
                (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
    }
    return ret;
}

static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        trailer_write(part, PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
    }
    else if (part == PART_UPDATE) {
        trailer_write(part, PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
    }
}

static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    if (part == PART_BOOT) {
        partition_magic_write(part, PART_BOOT_ENDFLAGS - sizeof(uint32_t));
    }
    else if (part == PART_UPDATE) {
        partition_magic_write(part, PART_UPDATE_ENDFLAGS - sizeof(uint32_t));
    }
}
#endif /* EXT_FLASH */



#ifdef WOLFBOOT_FIXED_PARTITIONS
/**
 * @brief Get the magic trailer of a partition.
 *
 * This function retrieves the magic trailer of a fixed partition.
 *
 * @param[in] part Partition number.
 * @return Pointer to the magic trailer of the partition.
 */
static uint32_t* RAMFUNCTION get_partition_magic(uint8_t part)
{
    return (uint32_t *)get_trailer_at(part, 0);
}

static uint8_t* RAMFUNCTION get_partition_state(uint8_t part)
{
    return (uint8_t *)get_trailer_at(part, 1);
}


static void RAMFUNCTION set_partition_state(uint8_t part, uint8_t val)
{
    set_trailer_at(part, 1, val);
}

/**
 * @brief Set the flags of an update sector.
 *
 * This function sets the flags of an update sector in a fixed partition.
 *
 * @param[in] pos Update sector position.
 * @param[in] val New flags value to set.
 * @return 0 on success, -1 on failure.
 */
static void RAMFUNCTION set_update_sector_flags(uint32_t pos, uint8_t val)
{
    set_trailer_at(PART_UPDATE, 2 + pos, val);
}

/**
 * @brief Get the flags of an update sector.
 *
 * This function retrieves the flags of an update sector in a fixed partition.
 *
 * @param[in] pos Update sector position.
 * @return Pointer to the flags of the update sector.
 */
static uint8_t* RAMFUNCTION get_update_sector_flags(uint32_t pos)
{
    return (uint8_t *)get_trailer_at(PART_UPDATE, 2 + pos);
}

/**
 * @brief Set the state of a partition.
 *
 * This function sets the state of a fixed partition.
 *
 * @param[in] part Partition number.
 * @param[in] newst New state value to set.
 * @return 0 on success, -1 on failure.
 */
int RAMFUNCTION wolfBoot_set_partition_state(uint8_t part, uint8_t newst)
{
    uint32_t *magic;
    uint8_t *state;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        set_partition_magic(part);
    state = get_partition_state(part);
    if (*state != newst)
        set_partition_state(part, newst);
    return 0;
}

int RAMFUNCTION wolfBoot_set_update_sector_flag(uint16_t sector, uint8_t newflag)
{
    uint32_t *magic;
    uint8_t *flags;
    uint8_t fl_value;
    uint8_t pos = sector >> 1;

    magic = get_partition_magic(PART_UPDATE);
    if (*magic != wolfboot_magic_trail)
        set_partition_magic(PART_UPDATE);

    flags = get_update_sector_flags(pos);
    if (sector == (pos << 1))
        fl_value = (*flags & 0xF0) | (newflag & 0x0F);
    else
        fl_value = ((newflag & 0x0F) << 4) | (*flags & 0x0F);
    if (fl_value != *flags)
        set_update_sector_flags(pos, fl_value);
    return 0;
}

/**
 * @brief Get the state of a partition.
 *
 * This function retrieves the state of a fixed partition.
 *
 * @param[in] part Partition number.
 * @param[out] st Pointer to store the partition state.
 * @return 0 on success, -1 on failure.
 */
int RAMFUNCTION wolfBoot_get_partition_state(uint8_t part, uint8_t *st)
{
    uint32_t *magic;
    uint8_t *state;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    state = get_partition_state(part);
    *st = *state;
    return 0;
}

int wolfBoot_get_update_sector_flag(uint16_t sector, uint8_t *flag)
{
    uint32_t *magic;
    uint8_t *flags;
    uint8_t pos = sector >> 1;
    magic = get_partition_magic(PART_UPDATE);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    flags = get_update_sector_flags(pos);
    if (sector == (pos << 1))
        *flag = *flags & 0x0F;
    else
        *flag = (*flags & 0xF0) >> 4;
    return 0;
}

/**
 * @brief Erase a partition.
 *
 * This function erases a partition.
 *
 * @param[in] part Partition number.
 */
void RAMFUNCTION wolfBoot_erase_partition(uint8_t part)
{
    uint32_t address = 0;
    int size = 0;

    if (part == PART_BOOT) {
        address = (uint32_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        size = WOLFBOOT_PARTITION_SIZE;
    }
    if (part == PART_UPDATE) {
        address = (uint32_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        size = WOLFBOOT_PARTITION_SIZE;
    }
    if (part == PART_SWAP) {
        address = (uint32_t)WOLFBOOT_PARTITION_SWAP_ADDRESS;
        size = WOLFBOOT_SECTOR_SIZE;
    }

    if (size > 0) {
        if (PARTN_IS_EXT(part)) {
            ext_flash_unlock();
            ext_flash_erase(address, size);
            ext_flash_lock();
        } else {
            hal_flash_erase(address, size);
        }
    }
}

/**
 * @brief Update trigger function.
 *
 * This function updates the boot partition state to "IMG_STATE_UPDATING".
 * If the FLAGS_HOME macro is defined, it erases the last sector of the boot
 * partition before updating the partition state. It also checks FLAGS_UPDATE_EXT
 * and calls the appropriate flash unlock and lock functions before
 * updating the partition state.
 */
void RAMFUNCTION wolfBoot_update_trigger(void)
{
    uint8_t st = IMG_STATE_UPDATING;
#if defined(NVM_FLASH_WRITEONCE) || defined(WOLFBOOT_FLAGS_INVERT)
    uintptr_t lastSector = PART_UPDATE_ENDFLAGS -
        (PART_UPDATE_ENDFLAGS % WOLFBOOT_SECTOR_SIZE);

#ifndef FLAGS_HOME
    /* if PART_UPDATE_ENDFLAGS stradles a sector, (all non FLAGS_HOME builds)
     * align it to the correct sector */
    if (PART_UPDATE_ENDFLAGS % WOLFBOOT_SECTOR_SIZE == 0)
        lastSector -= WOLFBOOT_SECTOR_SIZE;
#endif
#endif
#ifdef NVM_FLASH_WRITEONCE
    uint8_t selSec = 0;
#endif

    /* erase the sector flags */
    if (FLAGS_UPDATE_EXT()) {
        ext_flash_unlock();
    } else {
        hal_flash_unlock();
    }

    /* NVM_FLASH_WRITEONCE needs erased flags since it selects the fresh
     * partition based on how many flags are non-erased
     * FLAGS_INVERT needs erased flags because the bin-assemble's fill byte may
     * not match what's in wolfBoot */
#if defined(NVM_FLASH_WRITEONCE) || defined(WOLFBOOT_FLAGS_INVERT)
    if (FLAGS_UPDATE_EXT()) {
        ext_flash_erase(lastSector, SECTOR_FLAGS_SIZE);
    } else {
#ifdef NVM_FLASH_WRITEONCE
        selSec = nvm_select_fresh_sector(PART_UPDATE);
        XMEMCPY(NVM_CACHE,
            (uint8_t*)(lastSector - WOLFBOOT_SECTOR_SIZE * selSec),
            WOLFBOOT_SECTOR_SIZE);
        XMEMSET(NVM_CACHE, FLASH_BYTE_ERASED, SECTOR_FLAGS_SIZE);
        /* write to the non selected sector */
        hal_flash_write(lastSector - WOLFBOOT_SECTOR_SIZE * !selSec, NVM_CACHE,
            WOLFBOOT_SECTOR_SIZE);
        /* erase the previously selected sector */
        hal_flash_erase(lastSector - WOLFBOOT_SECTOR_SIZE * selSec,
            WOLFBOOT_SECTOR_SIZE);
#elif defined(WOLFBOOT_FLAGS_INVERT)
        hal_flash_erase(lastSector, SECTOR_FLAGS_SIZE);
#endif
    }
#endif

    wolfBoot_set_partition_state(PART_UPDATE, st);

    if (FLAGS_UPDATE_EXT()) {
        ext_flash_lock();
    } else {
        hal_flash_lock();
    }
}

/**
 * @brief Success function.
 *
 * This function updates the boot partition state to "IMG_STATE_SUCCESS".
 * If the FLAGS_BOOT_EXT macro is defined, it calls the appropriate flash unlock
 * and lock functions before updating the partition state. If the EXT_ENCRYPTED
 * macro is defined, it calls wolfBoot_erase_encrypt_key function.
 */
void RAMFUNCTION wolfBoot_success(void)
{
    uint8_t st = IMG_STATE_SUCCESS;
    if (FLAGS_BOOT_EXT()) {
        ext_flash_unlock();
        wolfBoot_set_partition_state(PART_BOOT, st);
        ext_flash_lock();
    } else {
        hal_flash_unlock();
        wolfBoot_set_partition_state(PART_BOOT, st);
        hal_flash_lock();
    }
#ifdef EXT_ENCRYPTED
    wolfBoot_erase_encrypt_key();
#endif
}
#endif /* WOLFBOOT_FIXED_PARTITIONS */

/**
 * @brief Find header function.
 *
 * This function searches for a specific header type in the given buffer.
 * It returns the length of the header and sets the 'ptr' parameter to the
 * position of the header if found.
 * @param haystack Pointer to the buffer to search for the header.
 * @param type The type of header to search for.
 * @param ptr Pointer to store the position of the header.
 *
 * @return uint16_t The length of the header found, or 0 if not found.
 *
 */
uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    uint8_t *p = haystack;
    uint16_t len;
    const volatile uint8_t *max_p = (haystack - IMAGE_HEADER_OFFSET) +
                                                    IMAGE_HEADER_SIZE;
    *ptr = NULL;
    if (p > max_p) {
        unit_dbg("Illegal address (too high)\n");
        return 0;
    }
    while ((p + 4) < max_p) {
        if ((p[0] == 0) && (p[1] == 0)) {
            unit_dbg("Explicit end of options reached\n");
            break;
        }
        if (*p == HDR_PADDING) {
            /* Padding byte (skip one position) */
            p++;
            continue;
        }
        /* Sanity check to prevent dereferencing unaligned half-words */
        if ((((size_t)p) & 0x01) != 0) {
            p++;
            continue;
        }
        len = p[2] | (p[3] << 8);
        if ((4 + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            unit_dbg("This field is too large (bigger than the space available "
                     "in the current header)\n");
            unit_dbg("%d %d %d\n", len, IMAGE_HEADER_SIZE, IMAGE_HEADER_OFFSET);
            break;
        }
        if (p + 4 + len > max_p) {
            unit_dbg("This field is too large and would overflow the image "
                     "header\n");
            break;
        }
        if ((p[0] | (p[1] << 8)) == type) {
            *ptr = (p + 4);
            return len;
        }
        p += 4 + len;
    }
    return 0;
}

#ifdef EXT_FLASH
static uint8_t hdr_cpy[IMAGE_HEADER_SIZE];
static uint32_t hdr_cpy_done = 0;
#endif

/**
 * @brief Convert little-endian to native-endian (uint32_t).
 *
 * This function converts a little-endian 32-bit value to the native-endian format.
 * It is used to handle endianness differences when reading data from memory.
 *
 * @param val The value to convert.
 *
 * @return The converted value.
 */
static inline uint32_t im2n(uint32_t val)
{
#ifdef BIG_ENDIAN_ORDER
    val = (((val & 0x000000FF) << 24) |
           ((val & 0x0000FF00) <<  8) |
           ((val & 0x00FF0000) >>  8) |
           ((val & 0xFF000000) >> 24));
#endif
  return val;
}

/**
 * @brief Convert little-endian to native-endian (uint16_t).
 *
 * This function converts a little-endian 16-bit value to the native-endian format.
 * It is used to handle endianness differences when reading data from memory.
 *
 * @param val The value to convert.
 * @return uint16_t The converted value.

 */
static inline uint16_t im2ns(uint16_t val)
{
#ifdef BIG_ENDIAN_ORDER
    val = (((val & 0x000000FF) << 8) |
           ((val & 0x0000FF00) >>  8));
#endif
  return val;
}

#ifdef DELTA_UPDATES
/**
 * @brief Get delta update information.
 *
 * This function retrieves the delta update information for a given partition.
 * It checks if the partition is extended, reads the image header, and returns
 * the delta image offset and size. The 'inverse' flag indicates whether to get
 * the inverse delta information or regular delta information.
 *
 * @param part The partition to check for delta update information.
 * @param inverse Flag to indicate if the delta update is inverse.
 * @param img_offset Pointer to store the delta image offset.
 * @param img_size Pointer to store the delta image size.
 *
 * @return int 0 if successful, -1 if not found or an error occurred.
 *
 */
int wolfBoot_get_delta_info(uint8_t part, int inverse, uint32_t **img_offset,
    uint16_t **img_size)
{
    uint32_t *version_field = NULL;
    uint32_t *magic = NULL;
    uint8_t *image = (uint8_t *)0x00000000;
    if (part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE)) {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
                hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        }
    } else if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS,
                hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        }
    }
    /* Don't check image against NULL to allow using address 0x00000000 */
    magic = (uint32_t *)image;
    if (*magic != WOLFBOOT_MAGIC)
        return -1;
    if (inverse) {
        if (wolfBoot_find_header((uint8_t *)(image + IMAGE_HEADER_OFFSET),
                    HDR_IMG_DELTA_INVERSE, (uint8_t **)img_offset)
                        != sizeof(uint32_t)) {
            return -1;
        }
        if (wolfBoot_find_header((uint8_t *)(image + IMAGE_HEADER_OFFSET),
                    HDR_IMG_DELTA_INVERSE_SIZE, (uint8_t **)img_size)
                        != sizeof(uint32_t)) {
            return -1;
        }
    } else {
        *img_offset = 0x0000000;
        if (wolfBoot_find_header((uint8_t *)(image + IMAGE_HEADER_OFFSET),
                    HDR_IMG_DELTA_SIZE, (uint8_t **)img_size)
                        != sizeof(uint32_t)) {
            return -1;
        }
    }
    return 0;
}
#endif


#if defined(EXT_ENCRYPTED) && defined(MMU)
static uint8_t dec_hdr[IMAGE_HEADER_SIZE];

static int decrypt_header(uint8_t *src)
{
    int i;
    uint32_t magic;
    uint32_t len;
    for (i = 0; i < IMAGE_HEADER_SIZE; i+=ENCRYPT_BLOCK_SIZE) {
        crypto_set_iv(encrypt_iv_nonce, i / ENCRYPT_BLOCK_SIZE);
        crypto_decrypt(dec_hdr + i, src + i, ENCRYPT_BLOCK_SIZE);
    }
    magic = *((uint32_t*)(dec_hdr));
    len = *((uint32_t*)(dec_hdr + sizeof(uint32_t)));
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    return 0;
}

#endif
/**
 * @brief Get blob version.
 *
 * This function retrieves the version number from the blob.
 * It checks the magic number in the blob to ensure it is valid before reading
 * the version field.
 *
 * @param blob Pointer to the buffer containing the blob.
 *
 * @return The version number of the blob, or 0 if the blob is invalid.
 *
 */
uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    uint32_t *volatile version_field = NULL;
    uint32_t *magic = NULL;
    uint8_t *img_bin = blob;
#if defined(EXT_ENCRYPTED) && defined(MMU)
    if (!encrypt_initialized)
        if (crypto_init() < 0)
            return 0;
    decrypt_header(blob);
    img_bin = dec_hdr;
#endif
    magic = (uint32_t *)img_bin;
    if (*magic != WOLFBOOT_MAGIC)
        return 0;
    if (wolfBoot_find_header(img_bin + IMAGE_HEADER_OFFSET, HDR_VERSION,
            (void *)&version_field) == 0)
        return 0;
    if (version_field)
        return im2n(*version_field);
    return 0;
}

/**
 * @brief Get blob type.
 *
 * This function retrieves the type of the blob.
 * It checks the magic number in the blob to ensure it is valid before reading
 * the type field.
 *
 * @param blob Pointer to the buffer containing the blob.
 *
 * @return The type of the blob, or 0 if the blob is invalid.
 */
uint32_t wolfBoot_get_blob_type(uint8_t *blob)
{
    uint32_t *volatile type_field = NULL;
    uint32_t *magic = NULL;
    uint8_t *img_bin = blob;
#if defined(EXT_ENCRYPTED) && defined(MMU)
    if (!encrypt_initialized)
        if (crypto_init() < 0)
            return 0;
    decrypt_header(blob);
    img_bin = dec_hdr;
#endif
    magic = (uint32_t *)img_bin;
    if (*magic != WOLFBOOT_MAGIC)
        return 0;
    if (wolfBoot_find_header(img_bin + IMAGE_HEADER_OFFSET, HDR_IMG_TYPE,
            (void *)&type_field) == 0)
        return 0;
    if (type_field)
        return im2ns(*type_field);

    return 0;
}

/**
 * @brief Get blob difference base version.
 *
 * This function retrieves the difference base version from the blob.
 * It checks the magic number in the blob to ensure it is valid before reading
 * the difference base field.
 *
 * @param blob Pointer to the buffer containing the blob.
 *
 * @return The difference base version of the blob, or 0 if not found
 * or the blob is invalid.
 *
 */

uint32_t wolfBoot_get_blob_diffbase_version(uint8_t *blob)
{
    uint32_t *volatile delta_base = NULL;
    uint32_t *magic = NULL;
    uint8_t *img_bin = blob;
#if defined(EXT_ENCRYPTED) && defined(MMU)
    if (!encrypt_initialized)
        if (crypto_init() < 0)
            return 0;
    decrypt_header(blob);
    img_bin = dec_hdr;
#endif
    magic = (uint32_t *)img_bin;
    if (*magic != WOLFBOOT_MAGIC)
        return 0;
    if (wolfBoot_find_header(img_bin + IMAGE_HEADER_OFFSET, HDR_IMG_DELTA_BASE,
            (void *)&delta_base) == 0)
        return 0;
    if (delta_base)
        return *delta_base;
    return 0;
}


#ifdef WOLFBOOT_FIXED_PARTITIONS
/**
 * @brief Get image pointer from a partition.
 *
 * This function retrieves the pointer to the image in the specified partition.
 * It handles both regular and extended partitions by reading from memory or
 * external flash if needed.
 *
 * @param part The partition to get the image pointer for.
 *
 * @return uint8_t* Pointer to the image in the specified partition, or
 * NULL if the partition is invalid or empty.
 *
 */
static uint8_t* wolfBoot_get_image_from_part(uint8_t part)
{
    uint8_t *image = (uint8_t *)0x00000000;

    if (part == PART_UPDATE) {
        image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;

    } else if (part == PART_BOOT) {
        image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    }
#ifdef EXT_FLASH
    if (PARTN_IS_EXT(part)) {
        ext_flash_check_read((uintptr_t)image, hdr_cpy, IMAGE_HEADER_SIZE);
        hdr_cpy_done = 1;
        image = hdr_cpy;
    }
#endif

    return image;
}

/**
 * @brief Get image version for a partition.
 *
 * This function retrieves the version number of the image in the specified
 * partition. It uses the 'wolfBoot_get_blob_version' function to extract the
 * version from the image blob.
 *
 * @param part The partition to get the image version for.
 *
 * @return The version number of the image in the partition,
 * or 0 if the partition is invalid or empty.
 *
 */

uint32_t wolfBoot_get_image_version(uint8_t part)
{
    /* Don't check image against NULL to allow using address 0x00000000 */
    return wolfBoot_get_blob_version(wolfBoot_get_image_from_part(part));
}

/**
 * @brief Get difference base version for a partition.
 *
 * This function retrieves the difference base version from the image in the
 * specified partition. It uses the 'wolfBoot_get_blob_diffbase_version'
 * function to extract the difference base version from the image blob.
 *
 * @param part The partition to get the difference base version for.
 *
 * @return The difference base version of the image in the partition, or
 * 0 if not found or the partition is invalid or empty.
 *
 */

uint32_t wolfBoot_get_diffbase_version(uint8_t part)
{
    /* Don't check image against NULL to allow using address 0x00000000 */
    return wolfBoot_get_blob_diffbase_version(
                wolfBoot_get_image_from_part(part));
}
/**
 * @brief Get image type for a partition.
 *
 * This function retrieves the image type from the image in the specified
 * partition. It uses the 'wolfBoot_get_blob_type' function to extract the image
 * type from the image blob.
 *
 * @param part The partition to get the image type for.
 *
 * @return uint16_t The image type of the image in the partition, or
 * 0 if the partition is invalid or empty.
 *
 */
uint16_t wolfBoot_get_image_type(uint8_t part)
{
    uint8_t *image = wolfBoot_get_image_from_part(part);

    if (image) {
      return wolfBoot_get_blob_type(image);
    }

    return 0;
}
#endif /* WOLFBOOT_FIXED_PARTITIONS */

#if defined(WOLFBOOT_DUALBOOT)

#if defined(WOLFBOOT_FIXED_PARTITIONS)
/**
 * @brief Find the dual-boot candidate partition.
 *
 * This function determines the dual-boot candidate partition based on the
 * current firmware versions and states. If both primary and update images
 * are present, it chooses the one with a higher version.
 * If no primary image is present, it selects the update partition.
 * It also handles the case where the current partition is in "IMG_STATE_TESTING"
 * and switches to the other partition if available.
 *
 * @return The partition number (PART_BOOT or PART_UPDATE) to be used as
 * the dual-boot candidate.
 * Returns -1 if no valid candidate is found.
 *
 */
int wolfBoot_dualboot_candidate(void)
{
    int candidate = PART_BOOT;
    int fallback_possible = 0;
    uint32_t boot_v, update_v;
    uint8_t p_state;
    /* Find the candidate */
    boot_v = wolfBoot_current_firmware_version();
    update_v = wolfBoot_update_firmware_version();

    wolfBoot_printf("Versions: Boot %d, Update %d\n", boot_v, update_v);

    /* -1 means  no images available */
    if ((boot_v == 0) && (update_v == 0))
        return -1;

    if (boot_v == 0) /* No primary image */
        candidate = PART_UPDATE;
    else if ((boot_v > 0) && (update_v > 0)) {
        fallback_possible = 1;
        if (update_v > boot_v)
            candidate = PART_UPDATE;
    }
    /* Check current status for failure (still in TESTING), and fall-back
     * if an alternative is available
     */
    if (fallback_possible &&
            (wolfBoot_get_partition_state(candidate, &p_state) == 0) &&
            (p_state == IMG_STATE_TESTING))
    {
        wolfBoot_erase_partition(candidate);
        candidate ^= 1; /* switch to other partition if available */
    }
    return candidate;
}
#else

static int wolfBoot_current_firmware_version()
{
    return wolfBoot_get_blob_version(hal_get_primary_address());
}
static int wolfBoot_update_firmware_version() {
    return wolfBoot_get_blob_version(hal_get_update_address());
}

int wolfBoot_dualboot_candidate_addr(void** addr)
{
    int fallback_possible = 0;
    uint32_t boot_v, update_v;
    uint8_t p_state;
    int retval = 0;

    /* Find the candidate */
    boot_v = wolfBoot_current_firmware_version();
    update_v = wolfBoot_update_firmware_version();
    /* -1 means  no images available */
    if ((boot_v == 0) && (update_v == 0))
        return -1;

    *addr = hal_get_primary_address();

    if (boot_v == 0) { /* No primary image */
        retval = 1;
        *addr = hal_get_update_address();
    }
    else if ((boot_v > 0) && (update_v > 0)) {
        fallback_possible = 1;
        if (update_v > boot_v) {
            retval = 1;
            *addr = hal_get_update_address();
        }
    }

    return retval;
}
#endif /* WOLFBOOT_FIXED_PARTITIONS */
/**
 * @brief Check if fallback is possible.
 *
 * This function checks if fallback is possible, i.e., both primary and
 * update images are present.
 *
 * @return 1 if fallback is possible, 0 otherwise.
 *
 */
int wolfBoot_fallback_is_possible(void)
{
    uint32_t boot_v, update_v;
    boot_v = wolfBoot_current_firmware_version();
    update_v = wolfBoot_update_firmware_version();
    if ((boot_v == 0) || (update_v == 0))
        return 0;
    return 1;
}
#endif /* WOLFBOOT_DUALBOOT */

#ifdef EXT_ENCRYPTED
#include "encrypt.h"
#if !defined(EXT_FLASH) && !defined(MMU)
#error option EXT_ENCRYPTED requires EXT_FLASH or MMU mode
#endif



#ifdef NVM_FLASH_WRITEONCE
#define ENCRYPT_CACHE NVM_CACHE
#else
static uint8_t ENCRYPT_CACHE[NVM_CACHE_SIZE] __attribute__((aligned(32)));
#endif

#if defined(EXT_ENCRYPTED) && defined(MMU)
static uint8_t ENCRYPT_KEY[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
#endif

static int RAMFUNCTION hal_set_key(const uint8_t *k, const uint8_t *nonce)
{
    uintptr_t addr, addr_align, addr_off;
    int ret = 0;
    int sel_sec = 0;
#ifdef MMU
    XMEMCPY(ENCRYPT_KEY, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_KEY + ENCRYPT_KEY_SIZE, nonce, ENCRYPT_NONCE_SIZE);
    return 0;
#else
    addr = ENCRYPT_TMP_SECRET_OFFSET + WOLFBOOT_PARTITION_BOOT_ADDRESS;
    addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    addr_off = addr & (WOLFBOOT_SECTOR_SIZE - 1);
#ifdef NVM_FLASH_WRITEONCE
    /* we read from the populated sector, now write to the erased sector */
    sel_sec = nvm_select_fresh_sector(PART_BOOT);
    addr_align -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
#endif
    hal_flash_unlock();
    /* casting to unsigned long to abide compilers on 64bit architectures */
    XMEMCPY(ENCRYPT_CACHE,
            (void*)(unsigned long)(addr_align),
                WOLFBOOT_SECTOR_SIZE);
#ifdef NVM_FLASH_WRITEONCE
    /* we read from the populated sector, now write to the erased sector */
    addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    addr_align -= (!sel_sec * WOLFBOOT_SECTOR_SIZE);
#else
    /* erase the old key */
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
#endif
    XMEMCPY(ENCRYPT_CACHE + addr_off, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_CACHE + addr_off + ENCRYPT_KEY_SIZE, nonce,
        ENCRYPT_NONCE_SIZE);
    ret = hal_flash_write(addr_align, ENCRYPT_CACHE, WOLFBOOT_SECTOR_SIZE);
#ifdef NVM_FLASH_WRITEONCE
    /* now erase the old populated sector */
    if (ret != 0)
        return ret;
    addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    addr_align -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
#endif
    hal_flash_lock();
    return ret;
#endif
}
/**
 * @brief Set the encryption key.
 *
 * This function sets the encryption key and nonce used for encrypting the
 * firmware image. It stores the key and nonce in the designated memory location.
 *
 * @param key Pointer to the encryption key.
 * @param nonce Pointer to the encryption nonce.
 *
 * @return 0 if successful.
 *
 */
int RAMFUNCTION wolfBoot_set_encrypt_key(const uint8_t *key,
    const uint8_t *nonce)
{
    hal_set_key(key, nonce);
    return 0;
}

int RAMFUNCTION wolfBoot_backup_encrypt_key(const uint8_t* key,
    const uint8_t* nonce)
{
#ifndef MMU
    uint32_t magic[2] = {WOLFBOOT_MAGIC, WOLFBOOT_MAGIC_TRAIL};

    hal_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS, key,
        ENCRYPT_KEY_SIZE);
    hal_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_KEY_SIZE, nonce, ENCRYPT_NONCE_SIZE);
    /* write magic so we know we finished in case of a powerfail */
    hal_flash_write(WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE, (uint8_t*)magic, sizeof(magic));
#endif
    return 0;
}

#ifndef UNIT_TEST
/**
 * @brief Get the encryption key.
 *
 * This function gets the encryption key and nonce used for encrypting the
 * firmware image.
 *
 * @param k Pointer to the encryption key.
 * @param nonce Pointer to the encryption nonce.
 *
 * @return 0 if successful.
 *
 */
int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *k, uint8_t *nonce)
{
    int ret = 0;
#if defined(MMU)
    XMEMCPY(k, ENCRYPT_KEY, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, ENCRYPT_KEY + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
#else
    uint8_t* mem;
    uint32_t magic[2];

    /* see if we've backed up the key, this will only matter for final swap */
    XMEMCPY(magic, (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE, sizeof(magic));

    if (magic[0] == WOLFBOOT_MAGIC && magic[1] == WOLFBOOT_MAGIC_TRAIL) {
        mem = (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        /* not a failure but finalize needs to know that it's safe to erase and
         * write the key to the normal spot */
        ret = 1;
    }
    else {
        mem = (uint8_t *)(ENCRYPT_TMP_SECRET_OFFSET +
            WOLFBOOT_PARTITION_BOOT_ADDRESS);

#ifdef NVM_FLASH_WRITEONCE
        int sel_sec = 0;
        sel_sec = nvm_select_fresh_sector(PART_BOOT);
        mem -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
#endif
    }

    XMEMCPY(k, mem, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, mem + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
#endif
    return ret;
}
#endif
/**
 * @brief Erase the encryption key.
 *
 * This function erases the encryption key and nonce, resetting them to all 0xFF
 * bytes.It ensures that the key and nonce cannot be accessed after erasure.
 *
 * @return 0 if successful.
 *
 */
int RAMFUNCTION wolfBoot_erase_encrypt_key(void)
{
#if defined(MMU)
    ForceZero(ENCRYPT_KEY, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE);
#else
    uint8_t ff[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    uint8_t *mem = (uint8_t *)ENCRYPT_TMP_SECRET_OFFSET +
        WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int sel_sec = 0;
#ifdef NVM_FLASH_WRITEONCE
    sel_sec = nvm_select_fresh_sector(PART_BOOT);
    mem -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
#endif
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE);
    if (XMEMCMP(mem, ff, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) != 0)
        hal_set_key(ff, ff + ENCRYPT_KEY_SIZE);
#endif
    return 0;
}

#if defined(__WOLFBOOT) || defined(UNIT_TEST)


#ifdef ENCRYPT_WITH_CHACHA

ChaCha chacha;

int RAMFUNCTION chacha_init(void)
{
#if defined(MMU) || defined(UNIT_TEST)
    uint8_t *key = ENCRYPT_KEY;
#else
    uint8_t key[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    wolfBoot_get_encrypt_key(key, key + ENCRYPT_KEY_SIZE);
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];
    uint8_t* stored_nonce;

    stored_nonce = key + ENCRYPT_KEY_SIZE;

    XMEMSET(&chacha, 0, sizeof(chacha));

    /* Check against 'all 0xff' or 'all zero' cases */
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;
    XMEMSET(ff, 0x00, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;

    XMEMCPY(encrypt_iv_nonce, stored_nonce, ENCRYPT_NONCE_SIZE);

    wc_Chacha_SetKey(&chacha, key, ENCRYPT_KEY_SIZE);
    encrypt_initialized = 1;
    return 0;
}

#elif defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256)

Aes aes_dec, aes_enc;
/**
 * @brief Initialize AES encryption.
 *
 * This function initializes the AES encryption by setting the encryption key
 * and encryption nonce, checking for valid keys, and copying the encryption
 * nonce from the key buffer.
 *
 * @return 0 if successful, -1 on failure.
 */
int aes_init(void)
{
#if defined(MMU) || defined(UNIT_TEST)
    uint8_t *key = ENCRYPT_KEY;
#else
    uint8_t key[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    wolfBoot_get_encrypt_key(key, key + ENCRYPT_KEY_SIZE);
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];
    uint8_t iv_buf[ENCRYPT_NONCE_SIZE];
    uint8_t* stored_nonce;

    stored_nonce = key + ENCRYPT_KEY_SIZE;

    XMEMSET(&aes_enc, 0, sizeof(aes_enc));
    XMEMSET(&aes_dec, 0, sizeof(aes_dec));
    wc_AesInit(&aes_enc, NULL, 0);
    wc_AesInit(&aes_dec, NULL, 0);

    /* Check against 'all 0xff' or 'all zero' cases */
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;
    XMEMSET(ff, 0x00, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;

    XMEMCPY(encrypt_iv_nonce, stored_nonce, ENCRYPT_NONCE_SIZE);
    XMEMCPY(iv_buf, stored_nonce, ENCRYPT_NONCE_SIZE);
    /* AES_ENCRYPTION is used for both directions in CTR */
    wc_AesSetKeyDirect(&aes_enc, key, ENCRYPT_KEY_SIZE, iv_buf, AES_ENCRYPTION);
    wc_AesSetKeyDirect(&aes_dec, key, ENCRYPT_KEY_SIZE, iv_buf, AES_ENCRYPTION);
    encrypt_initialized = 1;
    return 0;
}

/**
 * @brief Set the AES initialization vector (IV) for CTR mode.
 *
 * This function sets the AES initialization vector (IV) for the Counter (CTR)
 * mode encryption. It takes a 12-byte nonce and a 32-bit IV counter value to
 * construct the 16-byte IV used for encryption.
 *
 * @param nonce Pointer to the 12-byte nonce (IV) buffer.
 * @param iv_ctr The IV counter value.
 *
 */
void aes_set_iv(uint8_t *nonce, uint32_t iv_ctr)
{
    uint32_t iv_buf[ENCRYPT_BLOCK_SIZE / sizeof(uint32_t)];
    uint32_t iv_local_ctr;
    int i;
    XMEMCPY(iv_buf, nonce, ENCRYPT_NONCE_SIZE);
#ifndef BIG_ENDIAN_ORDER
    for (i = 0; i < 4; i++) {
        iv_buf[i] = wb_reverse_word32(iv_buf[i]);
    }
#endif
    iv_buf[3] += iv_ctr;
    if (iv_buf[3] < iv_ctr) { /* overflow */
        for (i = 2; i >= 0; i--) {
            iv_buf[i]++;
            if (iv_buf[i] != 0)
                break;
        }
    }
#ifndef BIG_ENDIAN_ORDER
    for (i = 0; i < 4; i++) {
        iv_buf[i] = wb_reverse_word32(iv_buf[i]);
    }
#endif
    wc_AesSetIV(&aes_enc, (byte *)iv_buf);
    wc_AesSetIV(&aes_dec, (byte *)iv_buf);
}

#endif

/**
 * @brief Determine the partition address type.
 *
 * This function determines the partition type based on the provided address.
 * If the address belongs to the update partition or swap partition (if defined),
 * the corresponding partition type is returned.
 * Otherwise, PART_NONE is returned.
 *
 * @param a The address to check for its partition type.
 *
 * @return The partition type (PART_UPDATE, PART_SWAP, or PART_NONE).

 */
static uint8_t RAMFUNCTION part_address(uintptr_t a)
{
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if ( 1 &&
#if !defined(WOLFBOOT_PART_USE_ARCH_OFFSET) && !defined(PULL_LINKER_DEFINES)
    #if WOLFBOOT_PARTITION_UPDATE_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
    #endif
#endif
        (a < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE))
        return PART_UPDATE;
    if ( 1 &&
#if !defined(WOLFBOOT_PART_USE_ARCH_OFFSET) && !defined(PULL_LINKER_DEFINES)
    #if WOLFBOOT_PARTITION_SWAP_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
    #endif
#endif
        (a < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE))
        return PART_SWAP;
#endif
    return PART_NONE;
}

#ifdef EXT_FLASH
/**
 * @brief Write encrypted data to an external flash.
 *
 * This function encrypts the provided data using the AES encryption algorithm
 * and writes it to the external flash.
 *
 * @param address The address in the external flash to write the data to.
 * @param data Pointer to the data buffer to be written.
 * @param len The length of the data to be written.
 * @param forcedEnc force writing encryption, used during final swap
 *
 *  @return int 0 if successful, -1 on failure.
 */
int RAMFUNCTION ext_flash_encrypt_write_ex(uintptr_t address,
    const uint8_t *data, int len, int forcedEnc)
{
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t enc_block[ENCRYPT_BLOCK_SIZE];
    uint32_t row_address = address, row_offset;
    int sz = len, i, step;
    uint8_t part;
    uint32_t iv_counter;

    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
        sz += ENCRYPT_BLOCK_SIZE - row_offset;
    }
    if (sz < ENCRYPT_BLOCK_SIZE) {
        sz = ENCRYPT_BLOCK_SIZE;
    }
    if (!encrypt_initialized) {
        if (crypto_init() < 0)
            return -1;
    }
    part = part_address(address);
    switch (part) {
        case PART_UPDATE:
            iv_counter = (address - WOLFBOOT_PARTITION_UPDATE_ADDRESS) /
                ENCRYPT_BLOCK_SIZE;
            /* Do not encrypt last sector */
            if (iv_counter >= (START_FLAGS_OFFSET - ENCRYPT_BLOCK_SIZE) /
                    ENCRYPT_BLOCK_SIZE) {
                return ext_flash_write(address, data, len);
            }
            crypto_set_iv(encrypt_iv_nonce, iv_counter);
            break;
        case PART_SWAP:
            /* data is coming from update and is already encrypted */
            if (forcedEnc == 0)
                return ext_flash_write(address, data, len);
            break;
        default:
            return -1;
    }

    /* encrypt blocks */
    if (sz > len) {
        step = ENCRYPT_BLOCK_SIZE - row_offset;
        if (ext_flash_read(row_address, block, ENCRYPT_BLOCK_SIZE)
                != ENCRYPT_BLOCK_SIZE) {
            return -1;
        }
        XMEMCPY(block + row_offset, data, step);
        crypto_encrypt(enc_block, block, ENCRYPT_BLOCK_SIZE);
        ext_flash_write(row_address, enc_block, ENCRYPT_BLOCK_SIZE);
        address += step;
        data += step;
        sz = len - step;
    }

    /* encrypt remainder */
    step = sz & ~(ENCRYPT_BLOCK_SIZE - 1);
    for (i = 0; i < step / ENCRYPT_BLOCK_SIZE; i++) {
        XMEMCPY(block, data + (ENCRYPT_BLOCK_SIZE * i), ENCRYPT_BLOCK_SIZE);
        crypto_encrypt(ENCRYPT_CACHE + (ENCRYPT_BLOCK_SIZE * i), block,
            ENCRYPT_BLOCK_SIZE);
    }

    return ext_flash_write(address, ENCRYPT_CACHE, step);
}

/**
 * @brief Write encrypted data to an external flash.
 *
 * This function calls ext_flash_encrypt_write_ex with forced encryption off
 *
 * @param address The address in the external flash to write the data to.
 * @param data Pointer to the data buffer to be written.
 * @param len The length of the data to be written.
 *
 *  @return int 0 if successful, -1 on failure.
 */
int RAMFUNCTION ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len)
{
    return ext_flash_encrypt_write_ex(address, data, len, 0);
}

/**
 * @brief Read and decrypt data from an external flash.
 *
 * This function reads the encrypted data from the external flash,
 * decrypts it using the AES decryption algorithm, and stores the decrypted data
 * in the provided buffer.

 * @param address The address in the external flash to read the encrypted data from.
 * @param data Pointer to the buffer to store the decrypted data.
 * @param len The length of the data to be read and decrypted.
 *
 * @return The number of decrypted bytes read, or -1 on failure.
 *
 */
int RAMFUNCTION ext_flash_decrypt_read(uintptr_t address, uint8_t *data, int len)
{
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t dec_block[ENCRYPT_BLOCK_SIZE];
    uint32_t row_address = address, row_offset, iv_counter = 0;
    int sz = len, i, step;
    uint8_t part;
    uintptr_t base_address;

    (void)base_address;
    (void)part;

    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
        sz += ENCRYPT_BLOCK_SIZE - row_offset;
    }
    if (sz < ENCRYPT_BLOCK_SIZE) {
        sz = ENCRYPT_BLOCK_SIZE;
    }
    if (!encrypt_initialized) {
        if (crypto_init() < 0)
            return -1;
    }
    part = part_address(row_address);
    switch (part) {
        case PART_UPDATE:
            iv_counter = (address - WOLFBOOT_PARTITION_UPDATE_ADDRESS) /
                ENCRYPT_BLOCK_SIZE;
            /* Do not decrypt last sector */
            if (iv_counter >= (START_FLAGS_OFFSET - ENCRYPT_BLOCK_SIZE) /
                    ENCRYPT_BLOCK_SIZE) {
                return ext_flash_read(address, data, len);
            }
            crypto_set_iv(encrypt_iv_nonce, iv_counter);
            break;
        case PART_SWAP:
            {
                break;
            }
        default:
            return -1;
    }
    /* decrypt blocks */
    if (sz > len) {
        step = ENCRYPT_BLOCK_SIZE - row_offset;
        if (ext_flash_read(row_address, block, ENCRYPT_BLOCK_SIZE)
                != ENCRYPT_BLOCK_SIZE) {
            return -1;
        }
        crypto_decrypt(dec_block, block, ENCRYPT_BLOCK_SIZE);
        XMEMCPY(data, dec_block + row_offset, step);
        address += step;
        data += step;
        sz = len - step;
        iv_counter++;
    }

    /* decrypt remainder */
    step = sz & ~(ENCRYPT_BLOCK_SIZE - 1);
    if (ext_flash_read(address, data, step) != step)
        return -1;
    for (i = 0; i < step / ENCRYPT_BLOCK_SIZE; i++) {
        XMEMCPY(block, data + (ENCRYPT_BLOCK_SIZE * i), ENCRYPT_BLOCK_SIZE);
        crypto_decrypt(data + (ENCRYPT_BLOCK_SIZE * i), block,
            ENCRYPT_BLOCK_SIZE);
        iv_counter++;
    }
    sz -= step;
    if (sz > 0) {
        if (ext_flash_read(address + step, block, ENCRYPT_BLOCK_SIZE)
                != ENCRYPT_BLOCK_SIZE) {
            return -1;
        }
        crypto_decrypt(dec_block, block, ENCRYPT_BLOCK_SIZE);
            XMEMCPY(data + step, dec_block, sz);
        iv_counter++;
    }
    return len;
}
#endif /* EXT_FLASH */
#endif /* __WOLFBOOT */

#if defined(MMU)
/**
 * @brief Decrypt data from RAM.
 *
 * This function decrypts data from the RAM using the AES decryption algorithm.
 *
 * @param src Pointer to the source buffer containing the encrypted data.
 * @param dst Pointer to the destination buffer to store the decrypted data.
 *
 *  @return int 0 if successful, -1 on failure.
 *
 */
int wolfBoot_ram_decrypt(uint8_t *src, uint8_t *dst)
{
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t dec_block[ENCRYPT_BLOCK_SIZE];
    uint8_t *row_address = src;
    uint32_t dst_offset = 0, iv_counter = 0;
    uint32_t magic, len;

    wolfBoot_printf("Decrypting %p to %p\n", src, dst);

    if (!encrypt_initialized) {
        if (crypto_init() < 0) {
            wolfBoot_printf("Error initializing crypto!\n");
            return -1;
        }
    }

    /* Attempt to decrypt firmware header */
    if (decrypt_header(src) != 0) {
        wolfBoot_printf("Error decrypting header at %p!\n", src);
        return -1;
    }
    len = *((uint32_t*)(dec_hdr + sizeof(uint32_t)));

    /* decrypt content */
    while (dst_offset < (len + IMAGE_HEADER_SIZE)) {
        crypto_set_iv(encrypt_iv_nonce, iv_counter);
        crypto_decrypt(dec_block, row_address, ENCRYPT_BLOCK_SIZE);
        XMEMCPY(dst + dst_offset, dec_block, ENCRYPT_BLOCK_SIZE);
        row_address += ENCRYPT_BLOCK_SIZE;
        dst_offset += ENCRYPT_BLOCK_SIZE;
        iv_counter++;
    }
    return 0;
}
#endif /* MMU */
#endif /* EXT_ENCRYPTED */

#ifdef FLAGS_HOME
/* we need to write a marker to update since the boot and update flags are all
 * in the same sector so write magic to the first sector of boot */
int wolfBoot_flags_home_set_final_swap()
{
/* EXT_ENCRYPTED uses the first sector to store the key and magic, don't
 * overwrite it */
#ifndef EXT_ENCRYPTED
    uint32_t magic[2] = {WOLFBOOT_MAGIC, WOLFBOOT_MAGIC_TRAIL};
    uintptr_t addr = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;

    hal_flash_write(addr, (uint8_t*)magic, sizeof(magic));
#endif /* !EXT_ENCRYPTED */

    return 0;
}

int wolfBoot_flags_home_get_final_swap()
{
    uint32_t magic[2];
    uintptr_t addr = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;

/* if encryption is on magic will be after the key and nonce */
#ifdef EXT_ENCRYPTED
    addr += ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE;
#endif

    XMEMCPY((uint8_t*)magic, (uint8_t*)addr, sizeof(magic));

    if (magic[0] == WOLFBOOT_MAGIC && magic[1] == WOLFBOOT_MAGIC_TRAIL)
        return 1;

    return 0;
}
#endif /* FLAGS_HOME */
