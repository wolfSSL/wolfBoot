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

#include <stdint.h>


#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "image.h"

#ifdef UNIT_TEST
#   include "printf.h"
#   define unit_dbg wolfBoot_printf
#else
#   define unit_dbg(...) do{}while(0)
#endif

#ifndef TRAILER_SKIP
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
                             /* MAGIC + PART_FLAG (1B) + (N_SECTORS / 2) */
    #define START_FLAGS_OFFSET (ENCRYPT_TMP_SECRET_OFFSET - TRAILER_OVERHEAD)
#else
    #define ENCRYPT_TMP_SECRET_OFFSET (WOLFBOOT_PARTITION_SIZE - (TRAILER_SKIP))
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
/* Inline use of ByteReverseWord32 */
#define WOLFSSL_MISC_INCLUDED
#include <wolfcrypt/src/misc.c>
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
 * NVM_FLASH_WRITEONCE uses a redundand two-sector model
 * to mitigate the effect of power failures.
 *
 */

#ifndef FLAGS_INVERT
#define FLAG_CMP(a,b) ((a < b)? 0 : 1)
#else
#define FLAG_CMP(a,b) ((a > b)? 0 : 1)
#endif

#include <stddef.h>
#include <string.h>
static uint8_t NVM_CACHE[NVM_CACHE_SIZE] __attribute__((aligned(16)));
static int nvm_cached_sector = 0;

static int nvm_select_fresh_sector(int part)
{
    int sel;
    uint32_t off;
    uint8_t *base;
    uint8_t *addr_align;

    if (part == PART_BOOT)
        base = (uint8_t *)PART_BOOT_ENDFLAGS;
    else
        base = (uint8_t *)PART_UPDATE_ENDFLAGS;

    /* Default to last sector if no match is found */
    sel = 0;

    /* Select the sector with more flags set */
    for (off = 1; off < WOLFBOOT_SECTOR_SIZE; off++) {
        uint8_t byte_0 = *(base - off);
        uint8_t byte_1 = *(base - (WOLFBOOT_SECTOR_SIZE + off));

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
            /* First time boot?  Assume no pending update */
            if(off == 1) {
                sel=0;
                break;
            }
            /* Examine previous position one byte ahead */
            byte_0 = *(base + 1 - off);
            byte_1 = *(base + 1 - (WOLFBOOT_SECTOR_SIZE + off));
            sel = FLAG_CMP(byte_0, byte_1);
            break;
        }
    }
    /* Erase the non-selected partition */
    addr_align = (uint8_t *)((((uintptr_t)base - ((1 + (!sel)) * WOLFBOOT_SECTOR_SIZE)))
        & ((~(NVM_CACHE_SIZE - 1))));
    if (*((uint32_t*)(addr_align + WOLFBOOT_SECTOR_SIZE - sizeof(uint32_t)))
            != FLASH_WORD_ERASED) {
        hal_flash_erase((uintptr_t)addr_align, WOLFBOOT_SECTOR_SIZE);
    }
    return sel;
}

static int RAMFUNCTION trailer_write(uint8_t part, uint32_t addr, uint8_t val) {
    size_t addr_align = (size_t)(addr & (~(NVM_CACHE_SIZE - 1)));
    size_t addr_read, addr_write;
    uint32_t addr_off = addr & (NVM_CACHE_SIZE - 1);
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

static int RAMFUNCTION partition_magic_write(uint8_t part, uint32_t addr)
{
    uint32_t off = addr % NVM_CACHE_SIZE;
    size_t base = (size_t)addr - off;
    size_t addr_read, addr_write;
    int ret;
    nvm_cached_sector = nvm_select_fresh_sector(part);
    addr_read = base - (nvm_cached_sector * NVM_CACHE_SIZE);
    addr_write = base - (!nvm_cached_sector * NVM_CACHE_SIZE);
    XMEMCPY(NVM_CACHE, (void*)base, NVM_CACHE_SIZE);
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

static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    uint32_t sel_sec = 0;
#ifdef NVM_FLASH_WRITEONCE
    sel_sec = nvm_select_fresh_sector(part);
#endif
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()){
            ext_flash_check_read(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            return (uint8_t *)&ext_cache;
        } else {
            return (void *)(PART_BOOT_ENDFLAGS -
                    (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at),
                (void *)&ext_cache, sizeof(uint32_t));
            return (uint8_t *)&ext_cache;
        } else {
            return (void *)(PART_UPDATE_ENDFLAGS -
                    (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
        }
    } else
        return NULL;
}

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
    uint32_t sel_sec = 0;
#ifdef NVM_FLASH_WRITEONCE
    sel_sec = nvm_select_fresh_sector(part);
#endif
    if (part == PART_BOOT) {
    	return (void *)(PART_BOOT_ENDFLAGS -
                (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
    }
    else if (part == PART_UPDATE) {
    	return (void *)(PART_UPDATE_ENDFLAGS -
                (WOLFBOOT_SECTOR_SIZE * sel_sec + (sizeof(uint32_t) + at)));
    }

    return NULL;
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

static void RAMFUNCTION set_update_sector_flags(uint32_t pos, uint8_t val)
{
    set_trailer_at(PART_UPDATE, 2 + pos, val);
}

static uint8_t* RAMFUNCTION get_update_sector_flags(uint32_t pos)
{
    return (uint8_t *)get_trailer_at(PART_UPDATE, 2 + pos);
}

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


void RAMFUNCTION wolfBoot_erase_partition(uint8_t part)
{
    uint32_t address = 0;
    int size = 0;

    if (part == PART_BOOT) {
        address = WOLFBOOT_PARTITION_BOOT_ADDRESS;
        size = WOLFBOOT_PARTITION_SIZE;
    }
    if (part == PART_UPDATE) {
        address = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        size = WOLFBOOT_PARTITION_SIZE;
    }
    if (part == PART_SWAP) {
        address = WOLFBOOT_PARTITION_SWAP_ADDRESS;
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

void RAMFUNCTION wolfBoot_update_trigger(void)
{
    uint8_t st = IMG_STATE_UPDATING;

#ifdef FLAGS_HOME
    /* Erase last sector of boot partition prior to
     * setting the partition state.
     */
    uint32_t last_sector = PART_UPDATE_ENDFLAGS -
        (PART_UPDATE_ENDFLAGS % WOLFBOOT_SECTOR_SIZE);
    hal_flash_unlock();
    hal_flash_erase(last_sector, WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
#endif

    if (FLAGS_UPDATE_EXT()) {
        ext_flash_unlock();
        wolfBoot_set_partition_state(PART_UPDATE, st);
        ext_flash_lock();
    } else {
        hal_flash_unlock();
        wolfBoot_set_partition_state(PART_UPDATE, st);
        hal_flash_lock();
    }
}

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


static inline uint16_t im2ns(uint16_t val)
{
#ifdef BIG_ENDIAN_ORDER
    val = (((val & 0x000000FF) << 8) |
           ((val & 0x0000FF00) >>  8));
#endif
  return val;
}

#ifdef DELTA_UPDATES
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
                        != sizeof(uint16_t)) {
            return -1;
        }
    } else {
        *img_offset = 0x0000000;
        if (wolfBoot_find_header((uint8_t *)(image + IMAGE_HEADER_OFFSET),
                    HDR_IMG_DELTA_SIZE, (uint8_t **)img_size)
                        != sizeof(uint16_t)) {
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
static uint8_t* wolfBoot_get_image_from_part(uint8_t part) {
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


uint32_t wolfBoot_get_image_version(uint8_t part)
{
    /* Don't check image against NULL to allow using address 0x00000000 */
    return wolfBoot_get_blob_version(wolfBoot_get_image_from_part(part));
}

uint32_t wolfBoot_get_diffbase_version(uint8_t part)
{
    /* Don't check image against NULL to allow using address 0x00000000 */
    return wolfBoot_get_blob_diffbase_version(
                wolfBoot_get_image_from_part(part));
}

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

int wolfBoot_dualboot_candidate(void)
{
    int candidate = PART_BOOT;
    int fallback_possible = 0;
    uint32_t boot_v, update_v;
    uint8_t p_state;
    /* Find the candidate */
    boot_v = wolfBoot_current_firmware_version();
    update_v = wolfBoot_update_firmware_version();
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
    uint32_t addr, addr_align, addr_off;
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
        sel_sec = nvm_select_fresh_sector(PART_BOOT);
        addr_align -= (sel_sec * WOLFBOOT_SECTOR_SIZE);
    #endif
    hal_flash_unlock();
    /* casting to unsigned long to abide compilers on 64bit architectures */
    XMEMCPY(ENCRYPT_CACHE,
            (void*)(unsigned long)(addr_align) ,
                WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
    XMEMCPY(ENCRYPT_CACHE + addr_off, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_CACHE + addr_off + ENCRYPT_KEY_SIZE, nonce,
        ENCRYPT_NONCE_SIZE);
    ret = hal_flash_write(addr_align, ENCRYPT_CACHE, WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
    return ret;
#endif
}

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

    hal_flash_write((uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS, key,
        ENCRYPT_KEY_SIZE);
    hal_flash_write((uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_KEY_SIZE, nonce, ENCRYPT_NONCE_SIZE);
    /* write magic so we know we finished in case of a powerfail */
    hal_flash_write((uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE, magic, sizeof(magic));
#endif
    return 0;
}

#ifndef UNIT_TEST
int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *k, uint8_t *nonce)
{
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
    return 0;
}
#endif

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
#elif defined(WOLFTPM_ENCRYPT_KEYSTORE)
    uint8_t key[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    uint32_t keySz = sizeof(key);
    struct wolfBoot_image boot;
#else
    uint8_t *key = (uint8_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_TMP_SECRET_OFFSET);
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];
    uint8_t* stored_nonce;

#ifdef WOLFTPM_ENCRYPT_KEYSTORE
    wolfBoot_open_image(&boot, PART_BOOT);

    if (wolfBoot_unseal_encryptkey(key, &keySz) != 0)
        return -1;
#endif

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

int aes_init(void)
{
#if defined(MMU) || defined(UNIT_TEST)
    uint8_t *key = ENCRYPT_KEY;
#elif defined(WOLFTPM_ENCRYPT_KEYSTORE)
    uint8_t key[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    uint32_t keySz = sizeof(key);
    struct wolfBoot_image boot;
#else
    uint8_t *key = (uint8_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS +
        ENCRYPT_TMP_SECRET_OFFSET);
#endif
    uint8_t ff[ENCRYPT_KEY_SIZE];
    uint8_t iv_buf[ENCRYPT_NONCE_SIZE];
    uint8_t* stored_nonce;

#ifdef WOLFTPM_ENCRYPT_KEYSTORE
    wolfBoot_open_image(&boot, PART_BOOT);

    if (wolfBoot_unseal_encryptkey(key, &keySz) != 0)
        return -1;
#endif

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


static uint8_t RAMFUNCTION part_address(uintptr_t a)
{
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if ( 1 &&
#if WOLFBOOT_PARTITION_UPDATE_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
#endif
        (a < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE))
        return PART_UPDATE;
    if ( 1 &&
#if WOLFBOOT_PARTITION_SWAP_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
#endif
        (a < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE))
        return PART_SWAP;
#endif
    return PART_NONE;
}

#ifdef EXT_FLASH
int RAMFUNCTION ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len)
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
int wolfBoot_ram_decrypt(uint8_t *src, uint8_t *dst)
{
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t dec_block[ENCRYPT_BLOCK_SIZE];
    uint8_t *row_address = src;
    uint32_t dst_offset = 0, iv_counter = 0;
    uint32_t magic, len;


    if (!encrypt_initialized) {
        if (crypto_init() < 0)
            return -1;
    }
    /* Attempt to decrypt firmware header */

    if (decrypt_header(src) != 0)
        return -1;
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
#endif
#endif /* EXT_ENCRYPTED */
