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

#ifndef NULL
#   define NULL (void *)0
#endif

uint32_t ext_cache;

#ifndef TRAILER_SKIP
#   define TRAILER_SKIP 0
#endif
#define PART_BOOT_ENDFLAGS   ((WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) - TRAILER_SKIP)
#define PART_UPDATE_ENDFLAGS ((WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE) - TRAILER_SKIP)

#ifdef NVM_FLASH_WRITEONCE
#include <stddef.h>
extern void *memcpy(void *dst, const void *src, size_t n);
static uint8_t NVM_CACHE[WOLFBOOT_SECTOR_SIZE];
int RAMFUNCTION hal_trailer_write(uint32_t addr, uint8_t val) {
    uint32_t addr_align = addr & (~(WOLFBOOT_SECTOR_SIZE - 1));
    uint32_t addr_off = addr & (WOLFBOOT_SECTOR_SIZE - 1);
    int ret = 0;
    memcpy(NVM_CACHE, (void *)addr_align, WOLFBOOT_SECTOR_SIZE);
    ret = hal_flash_erase(addr_align, WOLFBOOT_SECTOR_SIZE);
    if (ret != 0)
        return ret;
    NVM_CACHE[addr_off] = val;
    ret = hal_flash_write(addr_align, NVM_CACHE, WOLFBOOT_SECTOR_SIZE);
    return ret;
}
#else
#   define hal_trailer_write(addr, val) hal_flash_write(addr, (void *)&val, 1)
#endif

#if defined EXT_FLASH

static uint8_t* RAMFUNCTION get_trailer_at(uint8_t part, uint32_t at)
{
    if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
            ext_flash_read(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&ext_cache, sizeof(uint32_t));
            return (uint8_t *)&ext_cache;
        } else {
            return (void *)(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at));
        }
    }
    else if (part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE)) {
            ext_flash_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&ext_cache, sizeof(uint32_t));
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
        if (PARTN_IS_EXT(PART_BOOT)) {
            ext_flash_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
        } else {
            hal_trailer_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
    else if (part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE)) {
            ext_flash_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
        } else {
            hal_trailer_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), val);
        }
    }
}

static void RAMFUNCTION set_partition_magic(uint8_t part)
{
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
            ext_flash_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            hal_flash_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        }
    }
    else if (part == PART_UPDATE) {
        if (PARTN_IS_EXT(PART_UPDATE)) {
            ext_flash_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
        } else {
            hal_flash_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
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
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    if (part == PART_BOOT) {
        hal_flash_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    }
    else if (part == PART_UPDATE) {
        hal_flash_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
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

static uint8_t* RAMFUNCTION get_sector_flags(uint8_t part, uint32_t pos)
{
    return (uint8_t *)get_trailer_at(part, 2 + pos);
}

static void RAMFUNCTION set_partition_state(uint8_t part, uint8_t val)
{
    set_trailer_at(part, 1, val);
}

static void RAMFUNCTION set_sector_flags(uint8_t part, uint32_t pos, uint8_t val)
{
    set_trailer_at(part, 2 + pos, val);
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

int RAMFUNCTION wolfBoot_set_sector_flag(uint8_t part, uint8_t sector, uint8_t newflag)
{
    uint32_t *magic;
    uint8_t *flags;
    uint8_t fl_value;
    const uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    uint8_t pos = sector >> 1;
    magic = get_partition_magic(part);
    if (*magic != wolfboot_magic_trail)
        set_partition_magic(part);
    flags = get_sector_flags(part, pos);
    if (sector == (pos << 1))
        fl_value = (*flags & 0xF0) | (newflag & 0x0F);
    else
        fl_value = ((newflag & 0x0F) << 4) | (*flags & 0x0F);
    if (fl_value != *flags)
        set_sector_flags(part, pos, fl_value);
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

int wolfBoot_get_sector_flag(uint8_t part, uint8_t sector, uint8_t *flag)
{
    uint32_t *magic;
    uint8_t *flags;
    uint8_t pos = sector >> 1;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    flags = get_sector_flags(part, pos);
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
    if (PARTN_IS_EXT(PART_UPDATE))
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
    if (PARTN_IS_EXT(PART_BOOT))
    {
        ext_flash_unlock();
        wolfBoot_set_partition_state(PART_BOOT, st);
        ext_flash_lock();
    } else {
        hal_flash_unlock();
        wolfBoot_set_partition_state(PART_BOOT, st);
        hal_flash_lock();
    }
}

uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    uint8_t *p = haystack;
    uint16_t len;
    while (((p[0] != 0) || (p[1] != 0)) && ((p - haystack) < IMAGE_HEADER_SIZE)) {
        if (*p == HDR_PADDING) {
            p++;
            continue;
        }
        len = p[2] | (p[3] << 8);
        if ((p[0] | (p[1] << 8)) == type) {
            *ptr = (p + 4);
            return len;
        }
        p += 4 + len;
    }
    *ptr = NULL;
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
    wolfBoot_find_header(blob + IMAGE_HEADER_OFFSET, HDR_VERSION, (void *)&version_field);
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
            ext_flash_read((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        }
    } else if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
    #ifdef EXT_FLASH
            ext_flash_read((uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
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
            ext_flash_read((uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
            hdr_cpy_done = 1;
            image = hdr_cpy;
    #endif
        } else {
            image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
        }
    } else if (part == PART_BOOT) {
        if (PARTN_IS_EXT(PART_BOOT)) {
    #ifdef EXT_FLASH
            ext_flash_read((uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
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
        wolfBoot_find_header(image + IMAGE_HEADER_OFFSET, HDR_IMG_TYPE, (void *)&type_field);
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
#endif

