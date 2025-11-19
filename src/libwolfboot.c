/* libwolfboot.c
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

#if defined(EXT_ENCRYPTED) && (defined(__WOLFBOOT) || defined(UNIT_TEST) || defined(MMU))
#include "encrypt.h"
static int encrypt_initialized = 0;

static uint8_t encrypt_iv_nonce[ENCRYPT_NONCE_SIZE] XALIGNED(4);
static uint32_t encrypt_iv_offset = 0;
static int fallback_iv_forced = 0;

#define FALLBACK_IV_OFFSET 0x00100000U
    #if !defined(XMEMSET)
        #include <string.h>
        #define XMEMSET memset
        #define XMEMCPY memcpy
        #define XMEMCMP memcmp
    #endif
#if defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256)
extern void aes_set_iv(uint8_t *nonce, uint32_t address);
#endif

#if defined (__WOLFBOOT) || defined (UNIT_TEST)
int wolfBoot_initialize_encryption(void)
{
    if (!encrypt_initialized) {
        if (crypto_init() != 0) {
            return -1;
        }
        encrypt_initialized = 1;
    }
    return 0;
}
#endif

#else
    #define wolfBoot_initialize_encryption() (0)
#endif /* EXT_ENCRYPTED */



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
    /* Avoid redefining if already defined by wolfSSL headers */
    #if !defined(XMEMSET)
        #define XMEMSET memset
        #define XMEMCPY memcpy
        #define XMEMCMP memcmp
    #endif
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

#if defined(EXT_FLASH) && !defined(WOLFBOOT_NO_PARTITIONS)
static uint32_t ext_cache;
#endif


#if defined(__WOLFBOOT) || defined(UNIT_TEST)
#define WOLFSSL_MISC_INCLUDED /* allow misc.c code to be inlined */
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfcrypt/src/misc.c> /* for ByteReverseWord32 */
#if defined(EXT_ENCRYPTED) || defined(UNIT_TEST)
static uint32_t wb_reverse_word32(uint32_t x)
{
    return ByteReverseWord32(x);
}
#endif
#endif

#if (defined(WOLFBOOT_FIXED_PARTITIONS) || defined(EXT_FLASH) || \
    defined(NVM_FLASH_WRITEONCE)) && !defined(WOLFBOOT_NO_PARTITIONS)
static const uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
#endif

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
static uint8_t NVM_CACHE[NVM_CACHE_SIZE] XALIGNED(16);
static int nvm_cached_sector = 0;
static uint8_t get_base_offset(uint8_t *base, uintptr_t off)
{
    return *(uint8_t*)((uintptr_t)base - off); /* ignore array bounds error */
}

void WEAKFUNCTION hal_cache_invalidate(void)
{
    /* if cache flushing is required implement in hal */
}

static int RAMFUNCTION nvm_select_fresh_sector(int part)
{
    int sel;
    uintptr_t off;
    uint8_t *base;
    uint8_t* addrErase = 0;
    uint32_t word_0;
    uint32_t word_1;

#if defined(EXT_FLASH) && !defined(FLAGS_HOME)
    if ((part == PART_UPDATE) && FLAGS_UPDATE_EXT()) {
        return 0;
    }
#endif

    hal_cache_invalidate();

    if (part == PART_BOOT) {
        base = (uint8_t *)PART_BOOT_ENDFLAGS;
        addrErase = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS +
            WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
    }
    else {
        base = (uint8_t *)PART_UPDATE_ENDFLAGS;
#ifdef FLAGS_HOME
        addrErase = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS +
            WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
#else
        addrErase = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS +
            WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE;
#endif
    }

    /* check magic in case the sector is corrupt */
    word_0 = *((uint32_t*)((uintptr_t)base -  sizeof(uint32_t)));
    word_1 = *((uint32_t*)((uintptr_t)base - (WOLFBOOT_SECTOR_SIZE + sizeof(uint32_t))));

    if (word_0 == WOLFBOOT_MAGIC_TRAIL && word_1 != WOLFBOOT_MAGIC_TRAIL) {
        sel = 0;
        goto finish;
    }
    else if (word_0 != WOLFBOOT_MAGIC_TRAIL && word_1 == WOLFBOOT_MAGIC_TRAIL) {
        sel = 1;
        goto finish;
    } else if (word_0 != WOLFBOOT_MAGIC_TRAIL && word_1 != WOLFBOOT_MAGIC_TRAIL) {
        /* none of the partition has a valid trailer, default to '0' */
        sel = 0;
        goto finish;
    }

    /* Default to last sector if no match is found */
    sel = 0;

    /* Select the sector with more flags set. Partition flag is at offset '4'.
     * Sector flags begin from offset '5'.
     */
    for (off = 4; off < WOLFBOOT_SECTOR_SIZE; off++) {
        volatile uint8_t byte_0 = get_base_offset(base, off);
        volatile uint8_t byte_1 = get_base_offset(base, (WOLFBOOT_SECTOR_SIZE + off));

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
            /* Examine previous position one byte ahead */
            byte_0 = get_base_offset(base, (off - 1));
            byte_1 = get_base_offset(base, ((WOLFBOOT_SECTOR_SIZE + off) - 1));

            sel = FLAG_CMP(byte_0, byte_1);
            break;
        }
    }
finish:
    /* Erase the non-selected partition, requires unlocked flash */
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

    /* Ensure that the destination was erased */
    hal_flash_erase(addr_write, NVM_CACHE_SIZE);
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
#endif /* NVM_FLASH_WRITEONCE */

#ifndef MOCK_PARTITION_TRAILER /* used for unit-mock-state.c */
#ifdef CUSTOM_PARTITION_TRAILER

/* Custom partition trailer
 * Function implementation externally defined
 */
uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at);
void     RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val);
void     RAMFUNCTION set_partition_magic(uint8_t part);

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

/**
 * @brief Get the trailer at a specific address
 *
 * This function retrieves the trailer at a specific address in external or
 * internal flash
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
    #ifdef EXT_FLASH
        if (FLAGS_BOOT_EXT()){
            ext_flash_check_read(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            ret = (uint8_t *)&ext_cache;
        }
        else
    #endif
        {
            /* only internal flash should be writeonce */
        #ifdef NVM_FLASH_WRITEONCE
            sel_sec = nvm_select_fresh_sector(part);
        #endif
            ret = (void *)(PART_BOOT_ENDFLAGS -
                    (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
        }
    }
    else if (part == PART_UPDATE) {
    #ifdef EXT_FLASH
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            ret = (uint8_t *)&ext_cache;
        }
        else
    #endif
        {
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
 * @brief Set the trailer at a specific address
 *
 * This function sets the trailer at a specific address in external or
 * internal flash.
 *
 * @param[in] part Partition number.
 * @param[in] at Address offset.
 * @param[in] val New value to set in the trailer.
 */
static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
    #ifdef EXT_FLASH
        if (FLAGS_BOOT_EXT()) {
            /* use ext_cache and 32-bit writes to avoid any underlying hardware
             * issues with 1-byte write */
            ext_cache &= ~0xFF;
            ext_cache |= val;
            ext_flash_check_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
        }
        else
    #endif
        {
            trailer_write(part, PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
    else if (part == PART_UPDATE) {
    #ifdef EXT_FLASH
        if (FLAGS_UPDATE_EXT()) {
            /* use ext_cache and 32-bit writes to avoid any underlying hardware
             * issues with 1-byte write */
            ext_cache &= ~0xFF;
            ext_cache |= val;
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
        }
        else
    #endif
        {
            trailer_write(part, PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
}

/**
 * @brief Set the partition magic trailer
 *
 * This function sets the partition magic trailer in external or internal flash.
 *
 * @param[in] part Partition number.
 */
static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    if (part == PART_BOOT) {
    #ifdef EXT_FLASH
        if (FLAGS_BOOT_EXT()) {
            ext_flash_check_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t),
                (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        }
        else
    #endif
        {
            partition_magic_write(part, PART_BOOT_ENDFLAGS - sizeof(uint32_t));
        }
    }
    else if (part == PART_UPDATE) {
    #ifdef EXT_FLASH
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t),
                (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        }
        else
    #endif
        {
            partition_magic_write(part, PART_UPDATE_ENDFLAGS - sizeof(uint32_t));
        }
    }
}
#endif
#endif /* !MOCK_PARTITION_TRAILER */



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
    if (part == PART_NONE)
        return -1;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        set_partition_magic(part);
    state = get_partition_state(part);
    if (*state != newst)
        set_partition_state(part, newst);
    return 0;
}

/**
 * @brief Set the flag for sector
 *
 * This function sets the sector flag for update partition.
 *
 * @param[in] sector Sector number.
 * @param[in] newflag Nibble (4-bits) for sector flag
 * @return 0 on success, -1 on failure.
 */
int RAMFUNCTION wolfBoot_set_update_sector_flag(uint16_t sector,
    uint8_t newflag)
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
    if (part == PART_NONE)
        return -1;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    state = get_partition_state(part);
    *st = *state;
    return 0;
}

/**
 * @brief Get the flag for sector
 *
 * This function retrieves the sector flag for update partition.
 *
 * User may override this is function for cases where the update partition
 * flags are not at the end of partition
 *
 * @param[in] sector Sector number.
 * @param[out] flag Nibble (4-bits) for sector flags
 * @return 0 on success, -1 on failure.
 */
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

    switch (part) {
        case PART_BOOT:
            address = (uint32_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
            size = WOLFBOOT_PARTITION_SIZE;
            break;
        case PART_UPDATE:
            address = (uint32_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
            size = WOLFBOOT_PARTITION_SIZE;
            break;
        case PART_SWAP:
            address = (uint32_t)WOLFBOOT_PARTITION_SWAP_ADDRESS;
            size = WOLFBOOT_SECTOR_SIZE;
            break;
        default:
            break;
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
    uintptr_t lastSector = ((PART_UPDATE_ENDFLAGS - 1) / WOLFBOOT_SECTOR_SIZE) * WOLFBOOT_SECTOR_SIZE;
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
    if (FLAGS_UPDATE_EXT()) {
        ext_flash_erase(lastSector, WOLFBOOT_SECTOR_SIZE);
        wolfBoot_set_partition_state(PART_UPDATE, st);
    } else {
#ifndef NVM_FLASH_WRITEONCE
        hal_flash_erase(lastSector, WOLFBOOT_SECTOR_SIZE);
        wolfBoot_set_partition_state(PART_UPDATE, st);
#else
        uint32_t magic = WOLFBOOT_MAGIC_TRAIL;
        uint32_t offset = SECTOR_FLAGS_SIZE;
#ifdef FLAGS_HOME
        offset -= (PART_BOOT_ENDFLAGS - PART_UPDATE_ENDFLAGS);
#endif
        selSec = nvm_select_fresh_sector(PART_UPDATE);
        XMEMCPY(NVM_CACHE, (uint8_t*)lastSector - WOLFBOOT_SECTOR_SIZE * selSec,
            WOLFBOOT_SECTOR_SIZE);
        /* write to the non selected sector */
        hal_flash_erase(lastSector - WOLFBOOT_SECTOR_SIZE * !selSec,
            WOLFBOOT_SECTOR_SIZE);

        NVM_CACHE[offset] = IMG_STATE_UPDATING;
        memcpy(NVM_CACHE + offset + 1, &magic, sizeof(uint32_t));
        hal_flash_write(lastSector - WOLFBOOT_SECTOR_SIZE * !selSec, NVM_CACHE,
            WOLFBOOT_SECTOR_SIZE);
        /* erase the previously selected sector */
        hal_flash_erase(lastSector - WOLFBOOT_SECTOR_SIZE * selSec,
            WOLFBOOT_SECTOR_SIZE);
#endif
    }


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
    uint16_t len, htype;
    const volatile uint8_t *max_p = (haystack - IMAGE_HEADER_OFFSET) +
                                                    IMAGE_HEADER_SIZE;
    *ptr = NULL;
    if (p > max_p) {
        unit_dbg("Illegal address (too high)\n");
        return 0;
    }
    while ((p + 4) < max_p) {
        htype = p[0] | (p[1] << 8);
        if (htype == 0) {
            unit_dbg("Explicit end of options reached\n");
            break;
        }
        /* skip unaligned half-words and padding bytes */
        if ((p[0] == HDR_PADDING) || ((((size_t)p) & 0x01) != 0)) {
            p++;
            continue;
        }

        len = p[2] | (p[3] << 8);
        /* check len */
        if ((4 + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            unit_dbg("This field is too large (bigger than the space available "
                     "in the current header)\n");
            unit_dbg("%d %d %d\n", len, IMAGE_HEADER_SIZE, IMAGE_HEADER_OFFSET);
            break;
        }
        /* check max pointer */
        if (p + 4 + len > max_p) {
            unit_dbg("This field is too large and would overflow the image "
                     "header\n");
            break;
        }

        /* skip header [type|len] */
        p += 4;

        if (htype == type) {
            /* found, return pointer to data portion */
            *ptr = p;
            return len;
        }
        p += len;
    }
    return 0;
}

#ifdef EXT_FLASH
uint8_t hdr_cpy[IMAGE_HEADER_SIZE] XALIGNED(4);
uint32_t hdr_cpy_done = 0;
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

/* forward declaration */
static uint8_t* wolfBoot_get_image_from_part(uint8_t part);

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
    uint32_t **img_size, uint8_t **base_hash, uint16_t *base_hash_size)
{
    uint32_t *magic = NULL;
    uint8_t *image = wolfBoot_get_image_from_part(part);

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
    *base_hash_size = wolfBoot_find_header((uint8_t *)(image + IMAGE_HEADER_OFFSET),
            HDR_IMG_DELTA_BASE_HASH, base_hash);
    return 0;
}
#endif


#if defined(EXT_ENCRYPTED) && defined(MMU)
static uint8_t dec_hdr[IMAGE_HEADER_SIZE];

static int decrypt_header(uint8_t *src)
{
    int i;
    uint32_t magic;
    for (i = 0; i < IMAGE_HEADER_SIZE; i+=ENCRYPT_BLOCK_SIZE) {
        wolfBoot_crypto_set_iv(encrypt_iv_nonce, i / ENCRYPT_BLOCK_SIZE);
        crypto_decrypt(dec_hdr + i, src + i, ENCRYPT_BLOCK_SIZE);
    }
    magic = *((uint32_t*)(dec_hdr));
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    /* Example for extracting the length - not used for now */
    /* len = *((uint32_t*)(dec_hdr + sizeof(uint32_t))); */
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
    if (blob == NULL)
        return 0;
#if defined(EXT_ENCRYPTED) && defined(MMU)
    if (wolfBoot_initialize_encryption() < 0)
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
uint16_t wolfBoot_get_blob_type(uint8_t *blob)
{
    uint16_t *volatile type_field = NULL;
    uint32_t *magic = NULL;
    uint8_t *img_bin = blob;
#if defined(EXT_ENCRYPTED) && defined(MMU)
    if (wolfBoot_initialize_encryption() < 0)
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
    if (wolfBoot_initialize_encryption() < 0)
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
    uint8_t *image = (uint8_t *)0x00000000; /* default to 0x0 base */

    if (part == PART_BOOT) {
        image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    }
    else if (part == PART_UPDATE) {
        image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
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

#ifdef WOLFBOOT_SELF_HEADER
uint8_t* wolfBoot_get_self_header(void)
{
#if defined(EXT_FLASH) && defined(WOLFBOOT_SELF_HEADER_EXT)
    static uint8_t hdr_buf[IMAGE_HEADER_SIZE];
    uint32_t       magic;

    ext_flash_read((uintptr_t)WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS, hdr_buf,
                   IMAGE_HEADER_SIZE);
    magic = *((uint32_t*)hdr_buf);
    if (magic != WOLFBOOT_MAGIC) {
        return NULL;
    }

    return hdr_buf;
#else
    uint8_t* hdr   = (uint8_t*)WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS;
    uint32_t magic = *((uint32_t*)hdr);

    if (magic != WOLFBOOT_MAGIC) {
        return NULL;
    }

    return hdr;
#endif
}

uint32_t wolfBoot_get_self_version(void)
{
    uint8_t* hdr = wolfBoot_get_self_header();
    if (hdr == NULL) {
        return 0;
    }

    return wolfBoot_get_blob_version(hdr);
}

#endif

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

static int wolfBoot_current_firmware_version(void)
{
    return wolfBoot_get_blob_version(hal_get_primary_address());
}
static int wolfBoot_update_firmware_version(void)
{
    return wolfBoot_get_blob_version(hal_get_update_address());
}

int wolfBoot_dualboot_candidate_addr(void** addr)
{
    uint32_t boot_v, update_v;
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
#include "string.h"

#if defined(WOLFBOOT_RENESAS_TSIP)
    #include "wolfssl/wolfcrypt/port/Renesas/renesas-tsip-crypt.h"

    /* Provides wrap_enc_key_t structure generated using
     * Renesas Security Key Management Tool. See docs/Renesas.md */
    #include "enckey_data.h"
#endif

#if !defined(EXT_FLASH) && !defined(MMU)
    #error option EXT_ENCRYPTED requires EXT_FLASH or MMU mode
#endif

#ifndef WOLFBOOT_ENCRYPT_CACHE
    #ifdef NVM_FLASH_WRITEONCE
        #define ENCRYPT_CACHE NVM_CACHE
    #else
        #ifdef WOLFBOOT_SMALL_STACK
        static uint8_t ENCRYPT_CACHE[NVM_CACHE_SIZE] XALIGNED(32);
        #endif
    #endif
#else
    #define ENCRYPT_CACHE (WOLFBOOT_ENCRYPT_CACHE)
#endif

#if defined(EXT_ENCRYPTED) && defined(MMU)
static uint8_t ENCRYPT_KEY[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
#endif

#if defined(EXT_ENCRYPTED) && (defined(__WOLFBOOT) || defined(UNIT_TEST) || defined(MMU))
int RAMFUNCTION wolfBoot_enable_fallback_iv(int enable)
{
    int prev = 0;
    if (encrypt_iv_offset != 0)
        prev = 1;

    if (enable)
        encrypt_iv_offset = FALLBACK_IV_OFFSET;
    else
        encrypt_iv_offset = 0;

    return prev;
}

int RAMFUNCTION wolfBoot_force_fallback_iv(int enable)
{
    int prev = fallback_iv_forced;
    fallback_iv_forced = enable ? 1 : 0;
    return prev;
}

void RAMFUNCTION wolfBoot_crypto_set_iv(const uint8_t *nonce, uint32_t iv_counter)
{
#if defined(ENCRYPT_WITH_CHACHA)
    crypto_set_iv((uint8_t *)nonce, iv_counter + encrypt_iv_offset);
#elif defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256) || \
        defined(ENCRYPT_PKCS11)
    uint8_t local_nonce[ENCRYPT_NONCE_SIZE];
    XMEMCPY(local_nonce, nonce, ENCRYPT_NONCE_SIZE);
    crypto_set_iv(local_nonce, iv_counter + encrypt_iv_offset);
#else
    (void)nonce;
    (void)iv_counter;
#endif

    /* Fallback IV offset is single-use; clear it once applied. */
    encrypt_iv_offset = 0;
}
#endif /* EXT_ENCRYPTED && (__WOLFBOOT || UNIT_TEST || MMU) */

static int RAMFUNCTION hal_set_key(const uint8_t *k, const uint8_t *nonce)
{
#ifdef WOLFBOOT_RENESAS_TSIP
    /* must be flashed to RENESAS_TSIP_INSTALLEDENCKEY_ADDR */
    (void)k;
    (void)nonce;
    return 0;
#elif defined(MMU)
    XMEMCPY(ENCRYPT_KEY, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_KEY + ENCRYPT_KEY_SIZE, nonce, ENCRYPT_NONCE_SIZE);
    return 0;
#else
    uintptr_t addr, addr_align, addr_off;
    int ret = 0;
    uint32_t trailer_relative_off = 4;
#ifdef NVM_FLASH_WRITEONCE
    int sel_sec = 0;
#endif
#if !defined(WOLFBOOT_SMALL_STACK) && !defined(NVM_FLASH_WRITEONCE) && \
    !defined(WOLFBOOT_ENCRYPT_CACHE)
    uint8_t ENCRYPT_CACHE[NVM_CACHE_SIZE] XALIGNED_STACK(32);
#endif

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

    /* Populate key + nonce in the cache */
    XMEMCPY(ENCRYPT_CACHE + addr_off, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_CACHE + addr_off + ENCRYPT_KEY_SIZE, nonce,
        ENCRYPT_NONCE_SIZE);

    /* Add a valid trailer */
    XMEMCPY(ENCRYPT_CACHE + addr_off - trailer_relative_off,
            &wolfboot_magic_trail, 4);
#ifdef FLAGS_HOME
    /* If flags are stored in BOOT partition, take into account the offset
     * of the flags used for the update partition too, to avoid erasing the
     * sector.
     */
    trailer_relative_off += (PART_BOOT_ENDFLAGS - PART_UPDATE_ENDFLAGS);
    XMEMCPY(ENCRYPT_CACHE + addr_off - trailer_relative_off,
            &wolfboot_magic_trail, 4);
#endif

    /* Writing cache back to sector "!sel_sec" */
    ret = hal_flash_write(addr_align, ENCRYPT_CACHE, WOLFBOOT_SECTOR_SIZE);
#ifdef NVM_FLASH_WRITEONCE
    if (ret != 0)
        return ret;
    /* Erasing original sector "sel_sec",
     * same one returned from by nvm_select.
     */
    addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    addr_align -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
#endif
    hal_flash_lock();
    return ret;
#endif
}
#ifndef CUSTOM_ENCRYPT_KEY
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
#ifdef WOLFBOOT_RENESAS_TSIP
    wrap_enc_key_t* enc_key =(wrap_enc_key_t*)RENESAS_TSIP_INSTALLEDENCKEY_ADDR;
    XMEMCPY(k, enc_key->encrypted_user_key, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, enc_key->initial_vector, ENCRYPT_NONCE_SIZE);
#elif defined(MMU)
    XMEMCPY(k, ENCRYPT_KEY, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, ENCRYPT_KEY + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
#else
    uint8_t *mem = (uint8_t *)(ENCRYPT_TMP_SECRET_OFFSET +
        WOLFBOOT_PARTITION_BOOT_ADDRESS);
    #ifdef NVM_FLASH_WRITEONCE
    int sel_sec = 0;
    sel_sec = nvm_select_fresh_sector(PART_BOOT);
    mem -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
    #endif
    XMEMCPY(k, mem, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, mem + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
#endif
    return 0;
}
#endif /* UNIT_TEST */

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
#ifdef WOLFBOOT_RENESAS_TSIP
    /* nothing to erase */
#elif defined(MMU)
    ForceZero(ENCRYPT_KEY, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE);
#else
    uint8_t ff[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    uint8_t *mem = (uint8_t *)ENCRYPT_TMP_SECRET_OFFSET +
        WOLFBOOT_PARTITION_BOOT_ADDRESS;
#ifdef NVM_FLASH_WRITEONCE
    int sel_sec = nvm_select_fresh_sector(PART_BOOT);
    mem -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
#endif
    XMEMSET(ff, FLASH_BYTE_ERASED, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE);
    if (XMEMCMP(mem, ff, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) != 0)
        hal_set_key(ff, ff + ENCRYPT_KEY_SIZE);
#endif
    return 0;
}
#endif /* !CUSTOM_ENCRYPT_KEY */

#if defined(__WOLFBOOT) || defined(UNIT_TEST)


#ifdef ENCRYPT_WITH_CHACHA

ChaCha chacha;

int RAMFUNCTION chacha_init(void)
{
#ifdef CUSTOM_ENCRYPT_KEY
    uint8_t stored_nonce[ENCRYPT_NONCE_SIZE];
    uint8_t key[ENCRYPT_KEY_SIZE];
#else
    const uint8_t* stored_nonce;
    uint8_t *key;
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];

#ifdef CUSTOM_ENCRYPT_KEY
    int ret = wolfBoot_get_encrypt_key(key, stored_nonce);
    if (ret != 0)
        return ret;
#else
    #if defined(MMU) || defined(UNIT_TEST)
        key = ENCRYPT_KEY;
    #else
        key = (uint8_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS +
            ENCRYPT_TMP_SECRET_OFFSET);
    #endif
    #ifdef NVM_FLASH_WRITEONCE
        key -= WOLFBOOT_SECTOR_SIZE * nvm_select_fresh_sector(PART_BOOT);
    #endif
    stored_nonce = key + ENCRYPT_KEY_SIZE;
#endif

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
    int devId = INVALID_DEVID;
#if defined(CUSTOM_ENCRYPT_KEY) && !defined(WOLFBOOT_RENESAS_TSIP)
    uint8_t stored_nonce[ENCRYPT_NONCE_SIZE];
    uint8_t key[ENCRYPT_KEY_SIZE];
#else
    uint8_t *stored_nonce;
    uint8_t *key;
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];

#ifdef WOLFBOOT_RENESAS_TSIP
    int ret;
    wrap_enc_key_t* enc_key;
    devId = RENESAS_DEVID + 1;
    enc_key =(wrap_enc_key_t*)RENESAS_TSIP_INSTALLEDENCKEY_ADDR;
    key = enc_key->encrypted_user_key;
    stored_nonce = enc_key->initial_vector;
    wolfCrypt_Init(); /* required to setup the crypto callback defaults */
#elif defined(CUSTOM_ENCRYPT_KEY)
    wolfBoot_get_encrypt_key(key, stored_nonce);
#else
    #if defined(MMU) || defined(UNIT_TEST)
        key = ENCRYPT_KEY;
    #else
        key = (uint8_t*)(WOLFBOOT_PARTITION_BOOT_ADDRESS +
            ENCRYPT_TMP_SECRET_OFFSET);
    #endif
    #ifdef NVM_FLASH_WRITEONCE
        key -= WOLFBOOT_SECTOR_SIZE * nvm_select_fresh_sector(PART_BOOT);
    #endif
        stored_nonce = key + ENCRYPT_KEY_SIZE;
#endif /* non TSIP */

    XMEMSET(&aes_enc, 0, sizeof(aes_enc));
    XMEMSET(&aes_dec, 0, sizeof(aes_dec));
    wc_AesInit(&aes_enc, NULL, devId);
    wc_AesInit(&aes_dec, NULL, devId);

    /* Check against 'all 0xff' or 'all zero' cases */
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;
    XMEMSET(ff, 0x00, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;

#ifdef WOLFBOOT_RENESAS_TSIP
    /* Unwrap key and get key index */
#if ENCRYPT_KEY_SIZE == 32
    ret = R_TSIP_GenerateAes256KeyIndex(enc_key->wufpk, enc_key->initial_vector,
        enc_key->encrypted_user_key, &aes_enc.ctx.tsip_keyIdx);
#else
    ret = R_TSIP_GenerateAes128KeyIndex(enc_key->wufpk, enc_key->initial_vector,
        enc_key->encrypted_user_key, &aes_enc.ctx.tsip_keyIdx);
#endif
    if (ret != TSIP_SUCCESS) {
        return -1;
    }
    /* set encryption key size */
    aes_enc.ctx.keySize = ENCRYPT_KEY_SIZE;

    /* copy TSIP ctx to decryption key */
    XMEMCPY(&aes_dec.ctx, &aes_enc.ctx, sizeof(aes_enc.ctx));

    /* register AES crypto callback */
    wc_CryptoCb_RegisterDevice(devId, wc_tsip_AesCipher, NULL);
#endif /* WOLFBOOT_RENESAS_TSIP */

    /* AES_ENCRYPTION is used for both directions in CTR
     * IV is set later with "wc_AesSetIV" */
    wc_AesSetKeyDirect(&aes_enc, key, ENCRYPT_KEY_SIZE, NULL, AES_ENCRYPTION);
    wc_AesSetKeyDirect(&aes_dec, key, ENCRYPT_KEY_SIZE, NULL, AES_ENCRYPTION);

    /* Set global IV nonce used in aes_set_iv */
    XMEMCPY(encrypt_iv_nonce, stored_nonce, ENCRYPT_NONCE_SIZE);
    encrypt_initialized = 1;

    return 0;
}

/**
 * @brief Set the AES initialization vector (IV) for CTR mode.
 *
 * This function sets the AES initialization vector (IV) for the Counter (CTR)
 * mode encryption. It takes a 16-byte nonce and a 32-bit IV counter value to
 * construct the 16-byte IV used for encryption.
 *
 * @param nonce Pointer to the 16-byte nonce (IV) buffer.
 * @param iv_ctr The IV counter value.
 *
 */
void aes_set_iv(uint8_t *nonce, uint32_t iv_ctr)
{
    uint32_t iv_buf[ENCRYPT_BLOCK_SIZE / sizeof(uint32_t)];
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

#elif defined(ENCRYPT_PKCS11)

static CK_FUNCTION_LIST *pkcs11_function_list;
static CK_SESSION_HANDLE pkcs11_session;
static uint8_t pkcs11_pin[] = ENCRYPT_PKCS11_PIN;
static CK_OBJECT_HANDLE pkcs11_key_handle;
static int pkcs11_enc_initialized = 0, pkcs11_dec_initialized = 0;
#if ENCRYPT_PKCS11_MECHANISM == CKM_AES_CTR
static CK_AES_CTR_PARAMS pkcs11_params;
#endif

int pkcs11_crypto_init(void)
{
    CK_RV ret = 0;
    uint8_t *key_id;
    uint8_t *stored_nonce;
    CK_OBJECT_CLASS class = CKO_SECRET_KEY;
    CK_ATTRIBUTE search_attr[] = {
        { CKA_CLASS, &class, sizeof(class) },
        { CKA_ID, NULL, 0 },
    };
    CK_ULONG search_attr_count = sizeof(search_attr) / sizeof(*search_attr);
    CK_ULONG obj_count = 0;
    int pkcs11_intiialized = 0, session_opened = 0, logged_in = 0;

    if (encrypt_initialized)
        return 0;

    ret = C_GetFunctionList(&pkcs11_function_list);

    if (ret == CKR_OK) {

#if defined(MMU) || defined(UNIT_TEST)
        key_id = ENCRYPT_KEY;
#else
        key_id = (uint8_t*)(WOLFBOOT_PARTITION_BOOT_ADDRESS +
            ENCRYPT_TMP_SECRET_OFFSET);
#endif

#ifdef NVM_FLASH_WRITEONCE
        key_id -= WOLFBOOT_SECTOR_SIZE * nvm_select_fresh_sector(PART_BOOT);
#endif
        stored_nonce = key_id + ENCRYPT_KEY_SIZE;

        search_attr[1].pValue = key_id;
        search_attr[1].ulValueLen = ENCRYPT_PKCS11_KEY_ID_SIZE;

        /* Ensure TRNG is initialized, in case we're being called early */
        hal_trng_init();

        ret = pkcs11_function_list->C_Initialize(NULL);
    }

    if (ret == CKR_OK) {
        pkcs11_initialized = 1;

        ret = pkcs11_function_list->C_OpenSession(1,
                CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL, NULL,
                &pkcs11_session);
    }

    if (ret == CKR_OK) {
        session_opened = 1;

        ret = pkcs11_function_list->C_Login(pkcs11_session, CKU_USER,
                pkcs11_pin, sizeof(pkcs11_pin) - 1);
    }

    if (ret == CKR_OK) {
        logged_in = 1;

        /* Retrieve AES key by CKA_ID */
        ret = pkcs11_function_list->C_FindObjectsInit(pkcs11_session,
                search_attr, search_attr_count);
    }

    if (ret == CKR_OK) {
        ret = pkcs11_function_list->C_FindObjects(pkcs11_session,
                &pkcs11_key_handle, 1, &obj_count);
    }

    if (ret == CKR_OK && obj_count != 1) {
        ret = CKR_KEY_HANDLE_INVALID;
    }

    if (ret == CKR_OK) {
        ret = pkcs11_function_list->C_FindObjectsFinal(pkcs11_session);
    }

    if (ret == CKR_OK) {
        XMEMCPY(encrypt_iv_nonce, stored_nonce, ENCRYPT_PKCS11_NONCE_SIZE);
        encrypt_initialized = 1;
    }

    if (ret != CKR_OK) {
        if (logged_in) {
            pkcs11_function_list->C_Logout(pkcs11_session);
        }
        if (session_opened) {
            pkcs11_function_list->C_CloseSession(pkcs11_session);
        }
        if (pkcs11_initialized) {
            pkcs11_function_list->C_Finalize(NULL);
        }
    }

    return ret;
}

void pkcs11_crypto_set_iv(uint8_t *nonce, uint32_t iv_ctr)
{
    CK_RV ret;
    uint8_t buf[ENCRYPT_BLOCK_SIZE];
    CK_ULONG buf_len = sizeof(buf);

    if (pkcs11_enc_initialized) {
        ret = pkcs11_function_list->C_EncryptFinal(pkcs11_session, buf,
                &buf_len);
        pkcs11_enc_initialized = 0;
        if (ret != CKR_OK) {
            return;
        }
    }
    else if (pkcs11_dec_initialized) {
        ret = pkcs11_function_list->C_DecryptFinal(pkcs11_session, buf,
                &buf_len);
        pkcs11_dec_initialized = 0;
        if (ret != CKR_OK) {
            return;
        }
    }

#if ENCRYPT_PKCS11_MECHANISM == CKM_AES_CTR
    {
        uint32_t *cb_words = (uint32_t *)pkcs11_params.cb;
        int i;
        XMEMCPY(cb_words, nonce, ENCRYPT_NONCE_SIZE);
#ifndef BIG_ENDIAN_ORDER
        for (i = 0; i < 4; i++) {
            cb_words[i] = wb_reverse_word32(cb_words[i]);
        }
#endif
        cb_words[3] += iv_ctr;
        if (cb_words[3] < iv_ctr) { /* overflow */
            for (i = 2; i >= 0; i--) {
                cb_words[i]++;
                if (cb_words[i] != 0)
                    break;
            }
        }
#ifndef BIG_ENDIAN_ORDER
        for (i = 0; i < 4; i++) {
            cb_words[i] = wb_reverse_word32(cb_words[i]);
        }
#endif

        pkcs11_params.ulCounterBits = 32;
    }
#endif /* ENCRYPT_PKCS11_MECHANISM */
}

int pkcs11_crypto_encrypt(uint8_t *out, uint8_t *in, size_t size)
{
    CK_RV ret;
    CK_ULONG encrypted_len;

    if (pkcs11_dec_initialized)
        return -1;

    if (!pkcs11_enc_initialized) {
        CK_MECHANISM mech;

        mech.mechanism = ENCRYPT_PKCS11_MECHANISM;
        mech.pParameter = &pkcs11_params;
        mech.ulParameterLen = sizeof(pkcs11_params);

        ret = pkcs11_function_list->C_EncryptInit(pkcs11_session, &mech,
                pkcs11_key_handle);
        if (ret != CKR_OK) {
            return -1;
        }

        pkcs11_enc_initialized = 1;
    }

    encrypted_len = size;
    ret = pkcs11_function_list->C_EncryptUpdate(pkcs11_session, in, size, out,
            &encrypted_len);
    if (ret != CKR_OK) {
        return -1;
    }

    return 0;
}

int pkcs11_crypto_decrypt(uint8_t *out, uint8_t *in, size_t size)
{
    CK_RV ret;
    CK_ULONG decrypted_len;

    if (pkcs11_enc_initialized)
        return -1;

    if (!pkcs11_dec_initialized) {
        CK_MECHANISM mech;

        mech.mechanism = ENCRYPT_PKCS11_MECHANISM;
        mech.pParameter = &pkcs11_params;
        mech.ulParameterLen = sizeof(pkcs11_params);

        ret = pkcs11_function_list->C_DecryptInit(pkcs11_session, &mech,
                pkcs11_key_handle);
        if (ret != CKR_OK) {
            return -1;
        }

        pkcs11_dec_initialized = 1;
    }

    decrypted_len = size;
    ret = pkcs11_function_list->C_DecryptUpdate(pkcs11_session, in, size, out,
            &decrypted_len);
    if (ret != CKR_OK) {
        return -1;
    }

    return 0;
}

void pkcs11_crypto_deinit(void)
{
    if (encrypt_initialized) {
        pkcs11_function_list->C_CloseSession(pkcs11_session);
        encrypt_initialized = 0;
    }
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
int RAMFUNCTION ext_flash_encrypt_write(uintptr_t address, const uint8_t *data,
    int len)
{
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t enc_block[ENCRYPT_BLOCK_SIZE];
    uint32_t row_address = address, row_offset;
    int sz = len, i, step;
    uint8_t part;
    uint32_t iv_counter = 0;
#if defined(EXT_ENCRYPTED) && !defined(WOLFBOOT_SMALL_STACK) && \
    !defined(NVM_FLASH_WRITEONCE)
    uint8_t ENCRYPT_CACHE[NVM_CACHE_SIZE] XALIGNED_STACK(32);
#endif

    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
        sz += ENCRYPT_BLOCK_SIZE - row_offset;
    }
    if (sz < ENCRYPT_BLOCK_SIZE) {
        sz = ENCRYPT_BLOCK_SIZE;
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
            if (wolfBoot_initialize_encryption() < 0)
                return -1;
            wolfBoot_crypto_set_iv(encrypt_iv_nonce, iv_counter);
            break;
        case PART_SWAP:
            /* data is coming from update and is already encrypted */
            return ext_flash_write(address, data, len);
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
    uint8_t  block[ENCRYPT_BLOCK_SIZE] XALIGNED_STACK(4);
    uint8_t  dec_block[ENCRYPT_BLOCK_SIZE] XALIGNED_STACK(4);
    uint32_t row_address = address, row_offset, iv_counter = 0;
    int i;
    int flash_read_size;
    int read_remaining = len;
    int unaligned_head_size, unaligned_trailer_size;
    uint8_t part;
    uintptr_t base_address;

    (void)base_address;
    (void)part;

    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
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
            if (!encrypt_initialized) {
                if (crypto_init() < 0) {
                    return -1;
                }
            }
            if (fallback_iv_forced)
                encrypt_iv_offset = FALLBACK_IV_OFFSET;
            wolfBoot_crypto_set_iv(encrypt_iv_nonce, iv_counter);
            break;
        case PART_SWAP:
            break;

        default:
            return -1;
    }
    /* Decrypt block. If the address does not align with the encryption block,
     * decrypt then copy only the bytes from the requested address.
     */
    if (row_offset != 0) {
        unaligned_head_size = ENCRYPT_BLOCK_SIZE - row_offset;
        if (ext_flash_read(row_address, block, ENCRYPT_BLOCK_SIZE)
                != ENCRYPT_BLOCK_SIZE) {
            return -1;
        }
        crypto_decrypt(dec_block, block, ENCRYPT_BLOCK_SIZE);
        XMEMCPY(data, dec_block + row_offset, unaligned_head_size);
        address += unaligned_head_size;
        data += unaligned_head_size;
        read_remaining -= unaligned_head_size;
        iv_counter++;
    }
    /* Trim the read size to align with the Encryption Blocks. Read the
     * remaining unaligned trailer bytes after, since the `data` buffer won't
     * have enough space to handle the extra bytes.
     */
    flash_read_size = read_remaining & ~(ENCRYPT_BLOCK_SIZE - 1);
    if (ext_flash_read(address, data, flash_read_size) != flash_read_size)
        return -1;
    for (i = 0; i < flash_read_size / ENCRYPT_BLOCK_SIZE; i++)
    {
        XMEMCPY(block, data + (ENCRYPT_BLOCK_SIZE * i), ENCRYPT_BLOCK_SIZE);
        crypto_decrypt(data + (ENCRYPT_BLOCK_SIZE * i), block,
                ENCRYPT_BLOCK_SIZE);
        iv_counter++;
    }

    address += flash_read_size;
    data += flash_read_size;
    read_remaining -= flash_read_size;

    /* Read the unaligned trailer bytes. */
    unaligned_trailer_size = read_remaining;
    if (unaligned_trailer_size > 0)
    {
        uint8_t dec_block[ENCRYPT_BLOCK_SIZE] XALIGNED_STACK(4);
        if (ext_flash_read(address, block, ENCRYPT_BLOCK_SIZE)
                != ENCRYPT_BLOCK_SIZE)
            return -1;
        crypto_decrypt(dec_block, block, ENCRYPT_BLOCK_SIZE);
        XMEMCPY(data, dec_block, unaligned_trailer_size);
        read_remaining -= unaligned_trailer_size;
    }
    return (len - read_remaining);
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
    uint8_t dec_block[ENCRYPT_BLOCK_SIZE];
    uint8_t *row_address = src;
    uint32_t dst_offset = 0, iv_counter = 0;
    uint32_t len;

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
        wolfBoot_crypto_set_iv(encrypt_iv_nonce, iv_counter);
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

#if defined(__WOLFBOOT) && defined(WOLFCRYPT_SECURE_MODE)
CSME_NSE_API
void wolfBoot_nsc_success(void)
{
    wolfBoot_success();
}

CSME_NSE_API
void wolfBoot_nsc_update_trigger(void)
{
    wolfBoot_update_trigger();
}

CSME_NSE_API
uint32_t wolfBoot_nsc_get_image_version(uint8_t part)
{
    return wolfBoot_get_image_version(part);
}

CSME_NSE_API
int wolfBoot_nsc_get_partition_state(uint8_t part, uint8_t *st)
{
    return wolfBoot_get_partition_state(part, st);
}

CSME_NSE_API
int wolfBoot_nsc_erase_update(uint32_t address, uint32_t len)
{
    int ret;

    if (address > WOLFBOOT_PARTITION_SIZE)
        return -1;
    if (address + len > WOLFBOOT_PARTITION_SIZE)
        return -1;

    hal_flash_unlock();
    ret = hal_flash_erase(address + WOLFBOOT_PARTITION_UPDATE_ADDRESS, len);
    hal_flash_lock();
    return ret;
}

CSME_NSE_API
int wolfBoot_nsc_write_update(uint32_t address, const uint8_t *buf, uint32_t len)
{
    int ret;

    if (address > WOLFBOOT_PARTITION_SIZE)
        return -1;
    if (address + len > WOLFBOOT_PARTITION_SIZE)
        return -1;
    hal_flash_unlock();
    ret = hal_flash_write(address + WOLFBOOT_PARTITION_UPDATE_ADDRESS, buf, len);
    hal_flash_lock();
    return ret;
}

#endif
