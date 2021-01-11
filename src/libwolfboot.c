/* libwolfboot.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
#include <inttypes.h>

#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "image.h"

#ifdef UNIT_TEST
#   define unit_dbg printf
#else
#   define unit_dbg(...) do{}while(0)
#endif

#ifndef TRAILER_SKIP
#   define TRAILER_SKIP 0
#endif

#if defined(EXT_ENCRYPTED)
    #if defined(__WOLFBOOT)
        #include "encrypt.h"
    #else
        #include <stddef.h>
        #include <string.h>
        #define XMEMSET memset
        #define XMEMCPY memcpy
        #define XMEMCMP memcmp
    #endif
    #define ENCRYPT_TMP_SECRET_OFFSET (WOLFBOOT_PARTITION_SIZE - (TRAILER_SKIP + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE))
    #define TRAILER_OVERHEAD (4 + 1 + (WOLFBOOT_PARTITION_SIZE  / (2 * WOLFBOOT_SECTOR_SIZE))) /* MAGIC + PART_FLAG (1B) + (N_SECTORS / 2) */
    #define START_FLAGS_OFFSET (ENCRYPT_TMP_SECRET_OFFSET - TRAILER_OVERHEAD)
#else
    #define XMEMCPY memcpy
    #define ENCRYPT_TMP_SECRET_OFFSET (WOLFBOOT_PARTITION_SIZE - (TRAILER_SKIP))
#endif

#ifndef NULL
#   define NULL (void *)0
#endif

#define NVM_CACHE_SIZE WOLFBOOT_SECTOR_SIZE

#ifdef EXT_FLASH
static uint32_t ext_cache;
#endif

static const uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
/* Top addresses for FLAGS field
 *  - PART_BOOT_ENDFLAGS = top of flags for BOOT partition
 *  - PART_UPDATE_ENDFLAGS = top of flags for UPDATE_PARTITION
 */

#define PART_BOOT_ENDFLAGS   (WOLFBOOT_PARTITION_BOOT_ADDRESS + ENCRYPT_TMP_SECRET_OFFSET)
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
#include <stddef.h>
#include <string.h>
static uint8_t NVM_CACHE[NVM_CACHE_SIZE] __attribute__((aligned(16)));

int RAMFUNCTION hal_trailer_write(uint32_t addr, uint8_t val) {
    uint32_t addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    uint32_t addr_off = addr & (WOLFBOOT_SECTOR_SIZE - 1);
    int ret = 0;
    XMEMCPY(NVM_CACHE, (void *)addr_align, WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
    NVM_CACHE[addr_off] = val;
    ret = hal_flash_write(addr_align, NVM_CACHE, WOLFBOOT_SECTOR_SIZE);
    return ret;
}

int RAMFUNCTION hal_set_partition_magic(uint32_t addr)
{
    uint32_t off = addr % NVM_CACHE_SIZE;
    uint32_t base = addr - off;
    int ret;
    XMEMCPY(NVM_CACHE, (void *)base, NVM_CACHE_SIZE);
    ret = hal_flash_erase(base, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
    XMEMCPY(NVM_CACHE + off, &wolfboot_magic_trail, sizeof(uint32_t));
    ret = hal_flash_write(base, NVM_CACHE, WOLFBOOT_SECTOR_SIZE);
    return ret;
}

#else
#   define hal_trailer_write(addr, val) hal_flash_write(addr, (void *)&val, 1)
#   define hal_set_partition_magic(addr) hal_flash_write(addr, (void*)&wolfboot_magic_trail, sizeof(uint32_t));
#endif

#if defined EXT_FLASH


static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()){
            ext_flash_check_read(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&ext_cache, sizeof(uint32_t));
            return (uint8_t *)&ext_cache;
        } else {
            return (void *)(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at));
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&ext_cache, sizeof(uint32_t));
            return (uint8_t *)&ext_cache;
        } else {
            return (void *)(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at));
        }
    } else
        return NULL;
}

static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()) {
            ext_flash_check_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
        } else {
            hal_trailer_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
        } else {
            hal_trailer_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
}

static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    if (part == PART_BOOT) {
        if (FLAGS_BOOT_EXT()) {
            ext_flash_check_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            hal_set_partition_magic(PART_BOOT_ENDFLAGS - sizeof(uint32_t));
        }
    }
    else if (part == PART_UPDATE) {
        if (FLAGS_UPDATE_EXT()) {
            ext_flash_check_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            hal_set_partition_magic(PART_UPDATE_ENDFLAGS - sizeof(uint32_t));
        }
    }
}

#else
static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    if (part == PART_BOOT)
        return (void *)(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at));
    else if (part == PART_UPDATE) {
        return (void *)(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at));
    } else
        return NULL;
}

static void RAMFUNCTION set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        hal_trailer_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
    }
    else if (part == PART_UPDATE) {
        hal_trailer_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
    }
}

static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    if (part == PART_BOOT) {
        hal_set_partition_magic(PART_BOOT_ENDFLAGS - sizeof(uint32_t));
    }
    else if (part == PART_UPDATE) {
        hal_set_partition_magic(PART_UPDATE_ENDFLAGS - sizeof(uint32_t));
    }
}
#endif /* EXT_FLASH */



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
    if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
            ext_flash_unlock();
            ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
            ext_flash_lock();
        } else {
            hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
        }
    }
    if (part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE)) {
            ext_flash_unlock();
            ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
            ext_flash_lock();
        } else {
            hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
        }
    }
    if (part == PART_SWAP) {
        if (PARTN_IS_EXT(PART_SWAP)) {
            ext_flash_unlock();
            ext_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
            ext_flash_lock();
        } else {
            hal_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
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
    uint32_t last_sector = PART_UPDATE_ENDFLAGS - (PART_UPDATE_ENDFLAGS % WOLFBOOT_SECTOR_SIZE);
    hal_flash_unlock();
    hal_flash_erase(last_sector, WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
#endif

    if (FLAGS_UPDATE_EXT())
    {
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
    if (FLAGS_BOOT_EXT())
    {
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

uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    uint8_t *p = haystack;
    uint16_t len;
    const volatile uint8_t *max_p = (haystack - IMAGE_HEADER_OFFSET) + IMAGE_HEADER_SIZE;
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
        if ((((unsigned long)p) & 0x01) != 0) {
            p++;
            continue;
        }
        len = p[2] | (p[3] << 8);
        if ((4 + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            unit_dbg("This field is too large (bigger than the space available in the current header)\n");
            break;
        }
        if (p + 4 + len > max_p) {
            unit_dbg("This field is too large and would overflow the image header\n");
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

uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    uint32_t *version_field = NULL;
    uint32_t *magic = NULL;
    magic = (uint32_t *)blob;
    if (*magic != WOLFBOOT_MAGIC)
        return 0;
    if (wolfBoot_find_header(blob + IMAGE_HEADER_OFFSET, HDR_VERSION, (void *)&version_field) == 0)
        return 0;
    if (version_field)
        return *version_field;
    return 0;
}

uint32_t wolfBoot_get_image_version(uint8_t part)
{
    uint8_t *image = (uint8_t *)0x00000000;
    if(part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE))
        {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        }
    } else if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        }
    }
    /* Don't check image against NULL to allow using address 0x00000000 */
    return wolfBoot_get_blob_version(image);
}

uint16_t wolfBoot_get_image_type(uint8_t part)
{
    uint16_t *type_field = NULL;
    uint8_t *image = NULL;
    uint32_t *magic = NULL;
    if(part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE))
        {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        }
    } else if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
    #ifdef EXT_FLASH
            ext_flash_check_read((uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        }
    }
    if (image) {
        magic = (uint32_t *)image;
        if (*magic != WOLFBOOT_MAGIC)
            return 0;
        if (wolfBoot_find_header(image + IMAGE_HEADER_OFFSET, HDR_IMG_TYPE, (void *)&type_field) == 0)
            return 0;
        if (type_field)
            return *type_field;
    }
    return 0;
}

#if defined(ARCH_AARCH64) || defined(DUALBANK_SWAP)
int wolfBoot_fallback_is_possible(void)
{
    uint32_t boot_v, update_v;
    boot_v = wolfBoot_current_firmware_version();
    update_v = wolfBoot_update_firmware_version();
    if ((boot_v == 0) || (update_v == 0))
        return 0;
    return 1;
}

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
int wolfBoot_dualboot_candidate(void) { return 0; }
int wolfBoot_fallback_is_possible(void)
{
    if (wolfBoot_update_firmware_version() > 0)
        return 1;
    return 0;
}
#endif /* ARCH_AARCH64 || DUALBANK_SWAP */

#ifdef EXT_ENCRYPTED
#include "encrypt.h"
#ifndef EXT_FLASH
#error option EXT_ENCRYPTED requires EXT_FLASH
#endif



#ifdef NVM_FLASH_WRITEONCE
#define ENCRYPT_CACHE NVM_CACHE
#else
static uint8_t ENCRYPT_CACHE[NVM_CACHE_SIZE] __attribute__((aligned(32)));
#endif

static int RAMFUNCTION hal_set_key(const uint8_t *k, const uint8_t *nonce)
{
    uint32_t addr = ENCRYPT_TMP_SECRET_OFFSET + WOLFBOOT_PARTITION_BOOT_ADDRESS;
    uint32_t addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    uint32_t addr_off = addr & (WOLFBOOT_SECTOR_SIZE - 1);
    int ret = 0;
    hal_flash_unlock();
    XMEMCPY(ENCRYPT_CACHE, (void *)addr_align, WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
    XMEMCPY(ENCRYPT_CACHE + addr_off, k, ENCRYPT_KEY_SIZE);
    XMEMCPY(ENCRYPT_CACHE + addr_off + ENCRYPT_KEY_SIZE, nonce, ENCRYPT_NONCE_SIZE);
    ret = hal_flash_write(addr_align, ENCRYPT_CACHE, WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
    return ret;
}

int RAMFUNCTION wolfBoot_set_encrypt_key(const uint8_t *key, const uint8_t *nonce)
{
    hal_set_key(key, nonce);
    return 0;
}

int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *k, uint8_t *nonce)
{
    uint8_t *mem = (uint8_t *)(ENCRYPT_TMP_SECRET_OFFSET + WOLFBOOT_PARTITION_BOOT_ADDRESS);
    XMEMCPY(k, mem, ENCRYPT_KEY_SIZE);
    XMEMCPY(nonce, mem + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
    return 0;
}

int RAMFUNCTION wolfBoot_erase_encrypt_key(void)
{
    uint8_t ff[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE];
    int i;
    uint8_t *mem = (uint8_t *)ENCRYPT_TMP_SECRET_OFFSET + WOLFBOOT_PARTITION_BOOT_ADDRESS;
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE);
    if (XMEMCMP(mem, ff, ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) != 0)
        hal_set_key(ff, ff + ENCRYPT_KEY_SIZE);
    return 0;
}

#ifdef __WOLFBOOT

static ChaCha chacha;
static int chacha_initialized = 0;
static uint8_t chacha_iv_nonce[ENCRYPT_NONCE_SIZE];

static int chacha_init(void)
{
    uint8_t *key = (uint8_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + ENCRYPT_TMP_SECRET_OFFSET);
    uint8_t ff[ENCRYPT_KEY_SIZE];
    uint8_t *stored_nonce = key + ENCRYPT_KEY_SIZE;

    /* Check against 'all 0xff' or 'all zero' cases */
    XMEMSET(ff, 0xFF, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;
    XMEMSET(ff, 0x00, ENCRYPT_KEY_SIZE);
    if (XMEMCMP(key, ff, ENCRYPT_KEY_SIZE) == 0)
        return -1;

    XMEMCPY(chacha_iv_nonce, stored_nonce, ENCRYPT_NONCE_SIZE);
    wc_Chacha_SetKey(&chacha, key, ENCRYPT_KEY_SIZE);
    chacha_initialized = 1;
    return 0;
}


static inline uint8_t part_address(uintptr_t a)
{
    if ( 1 &&
#if WOLFBOOT_PARTITION_UPDATE_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
#endif
        (a <= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE))
        return PART_UPDATE;
    if ( 1 &&
#if WOLFBOOT_PARTITION_SWAP_ADDRESS != 0
        (a >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
#endif
        (a <= WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE))
        return PART_SWAP;
    return PART_NONE;
}

int ext_flash_encrypt_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t iv_counter;
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t part;
    int sz = len;
    uint32_t row_address = address, row_offset;
    int i;
    uint8_t enc_block[ENCRYPT_BLOCK_SIZE];
    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
        sz += ENCRYPT_BLOCK_SIZE - row_offset;
    }
    if (sz < ENCRYPT_BLOCK_SIZE) {
        sz = ENCRYPT_BLOCK_SIZE;
    }
    if (!chacha_initialized)
        if (chacha_init() < 0)
            return -1;
    part = part_address(address);
    switch(part) {
        case PART_UPDATE:
            iv_counter = (address - WOLFBOOT_PARTITION_UPDATE_ADDRESS) / ENCRYPT_BLOCK_SIZE;
            /* Do not encrypt last sectors */
            if (iv_counter >= (START_FLAGS_OFFSET - ENCRYPT_BLOCK_SIZE) / ENCRYPT_BLOCK_SIZE) {
                return ext_flash_write(address, data, len);
            }
            break;
        case PART_SWAP:
            {
                uint32_t row_number;
                row_number = (address - WOLFBOOT_PARTITION_SWAP_ADDRESS) / ENCRYPT_BLOCK_SIZE;
                iv_counter = row_number;
                break;
            }
        default:
            return -1;
    }
    if (sz > len) {
        int step = ENCRYPT_BLOCK_SIZE - row_offset;
        if (ext_flash_read(row_address, block, ENCRYPT_BLOCK_SIZE) != ENCRYPT_BLOCK_SIZE)
            return -1;
        XMEMCPY(block + row_offset, data, step);
        wc_Chacha_SetIV(&chacha, chacha_iv_nonce, iv_counter);
        wc_Chacha_Process(&chacha, enc_block, block, ENCRYPT_BLOCK_SIZE);
        ext_flash_write(row_address, enc_block, ENCRYPT_BLOCK_SIZE);
        address += step;
        data += step;
        sz -= step;
        iv_counter++;
    }
    for (i = 0; i < sz / ENCRYPT_BLOCK_SIZE; i++) {
        wc_Chacha_SetIV(&chacha, chacha_iv_nonce, iv_counter);
        XMEMCPY(block, data + (ENCRYPT_BLOCK_SIZE * i), ENCRYPT_BLOCK_SIZE);
        wc_Chacha_Process(&chacha, ENCRYPT_CACHE + (ENCRYPT_BLOCK_SIZE * i), block, ENCRYPT_BLOCK_SIZE);
        iv_counter++;
    }
    return ext_flash_write(address, ENCRYPT_CACHE, len);
}

int ext_flash_decrypt_read(uintptr_t address, uint8_t *data, int len)
{
    uint32_t iv_counter = 0;
    uint8_t block[ENCRYPT_BLOCK_SIZE];
    uint8_t part;
    int sz = len;
    uint32_t row_address = address, row_offset;
    int i;

    row_offset = address & (ENCRYPT_BLOCK_SIZE - 1);
    if (row_offset != 0) {
        row_address = address & ~(ENCRYPT_BLOCK_SIZE - 1);
        sz += ENCRYPT_BLOCK_SIZE - row_offset;
    }
    if (sz < ENCRYPT_BLOCK_SIZE) {
        sz = ENCRYPT_BLOCK_SIZE;
    }
    if (!chacha_initialized)
        if (chacha_init() < 0)
            return -1;
    part = part_address(row_address);
    switch(part) {
        case PART_UPDATE:
            iv_counter = (address - WOLFBOOT_PARTITION_UPDATE_ADDRESS) / ENCRYPT_BLOCK_SIZE;
            /* Do not decrypt last sector */
            if (iv_counter >= (START_FLAGS_OFFSET - ENCRYPT_BLOCK_SIZE) / ENCRYPT_BLOCK_SIZE) {
                return ext_flash_read(address, data, len);
            }
            break;
        case PART_SWAP:
            {
                uint32_t row_number;
                row_number = (address - WOLFBOOT_PARTITION_SWAP_ADDRESS) / ENCRYPT_BLOCK_SIZE;
                iv_counter = row_number;
                break;
            }
        default:
            return -1;
    }
    if (sz > len) {
        uint8_t dec_block[ENCRYPT_BLOCK_SIZE];
        int step = ENCRYPT_BLOCK_SIZE - row_offset;
        if (ext_flash_read(row_address, block, ENCRYPT_BLOCK_SIZE) != ENCRYPT_BLOCK_SIZE)
            return -1;
        wc_Chacha_SetIV(&chacha, chacha_iv_nonce, iv_counter);
        wc_Chacha_Process(&chacha, dec_block, block, ENCRYPT_BLOCK_SIZE);
        XMEMCPY(data, dec_block + row_offset, step);
        address += step;
        data += step;
        sz -= step;
        iv_counter++;
    }
    if (ext_flash_read(address, data, sz) != sz)
        return -1;
    for (i = 0; i < sz / ENCRYPT_BLOCK_SIZE; i++) {
        wc_Chacha_SetIV(&chacha, chacha_iv_nonce, iv_counter);
        XMEMCPY(block, data + (ENCRYPT_BLOCK_SIZE * i), ENCRYPT_BLOCK_SIZE);
        wc_Chacha_Process(&chacha, data + (ENCRYPT_BLOCK_SIZE * i), block, ENCRYPT_BLOCK_SIZE);
        iv_counter++;
    }
    return len;
}
#endif

#endif /* EXT_ENCRYPTED */

