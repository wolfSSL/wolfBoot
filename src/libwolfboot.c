/* libwolfboot.c
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include <hal.h>
#include <stdint.h>
#include <inttypes.h>
#include <wolfboot/wolfboot.h>

#ifndef NULL
#   define NULL (void *)0
#endif

uint32_t ext_cache;

#ifndef TRAILER_SKIP
#   define TRAILER_SKIP 0
#endif
#define PART_BOOT_ENDFLAGS   ((WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) - TRAILER_SKIP)
#define PART_UPDATE_ENDFLAGS ((WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE) - TRAILER_SKIP)

#if defined PART_UPDATE_EXT
static uint8_t *get_trailer_at(uint8_t part, uint32_t at)
{
    if (part == PART_BOOT)
        return (void *)(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at));
    else if (part == PART_UPDATE) {
        ext_flash_read(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&ext_cache, sizeof(uint32_t));
        return (uint8_t *)&ext_cache;
    } else
        return NULL;
}

static void set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        hal_flash_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
    }
    else if (part == PART_UPDATE) {
        ext_flash_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
    }
}

static void set_partition_magic(uint8_t part)
{
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    if (part == PART_BOOT) {
        hal_flash_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    }
    else if (part == PART_UPDATE) {
        ext_flash_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    }
}

#else
static uint8_t *get_trailer_at(uint8_t part, uint32_t at)
{
    if (part == PART_BOOT)
        return (void *)(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at));
    else if (part == PART_UPDATE) {
        return (void *)(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at));
    } else
        return NULL;
}

static void set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    if (part == PART_BOOT) {
        hal_flash_write(PART_BOOT_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
    }
    else if (part == PART_UPDATE) {
        hal_flash_write(PART_UPDATE_ENDFLAGS - (sizeof(uint32_t) + at), (void *)&val, 1);
    }
}

static void set_partition_magic(uint8_t part)
{
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    if (part == PART_BOOT) {
        hal_flash_write(PART_BOOT_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    }
    else if (part == PART_UPDATE) {
        hal_flash_write(PART_UPDATE_ENDFLAGS - sizeof(uint32_t), (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    }
}
#endif /* PART_UPDATE_EXT */



static uint32_t *get_partition_magic(uint8_t part)
{
    return (uint32_t *)get_trailer_at(part, 0);
}

static uint8_t *get_partition_state(uint8_t part)
{
    return (uint8_t *)get_trailer_at(part, 1);
}

static uint8_t *get_sector_flags(uint8_t part, uint32_t pos)
{
    return (uint8_t *)get_trailer_at(part, 2 + pos);
}

static void set_partition_state(uint8_t part, uint8_t val)
{
    set_trailer_at(part, 1, val);
}

static void set_sector_flags(uint8_t part, uint32_t pos, uint8_t val)
{
    set_trailer_at(part, 2 + pos, val);
}

int wolfBoot_set_partition_state(uint8_t part, uint8_t newst)
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

int wolfBoot_set_sector_flag(uint8_t part, uint8_t sector, uint8_t newflag)
{
    uint32_t *magic;
    uint8_t *flags;
    uint8_t fl_value;
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    uint8_t pos = sector >> 1;
    magic = get_partition_magic(part);
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
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

int wolfBoot_get_partition_state(uint8_t part, uint8_t *st)
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

void wolfBoot_erase_partition(uint8_t part)
{
    if (part == PART_BOOT)
        hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    if (part == PART_UPDATE) {
#ifdef PART_UPDATE_EXT
        ext_flash_unlock();
        ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
        ext_flash_lock();
#else
        hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
#endif
    }
    if (part == PART_SWAP)
        hal_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
}

void wolfBoot_update_trigger(void)
{
    uint8_t st = IMG_STATE_UPDATING;
#ifdef PART_UPDATE_EXT
    ext_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, st);
    ext_flash_lock();
#else
    hal_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, st);
    hal_flash_lock();
#endif
}

void wolfBoot_success(void)
{
    uint8_t st = IMG_STATE_SUCCESS;
    hal_flash_unlock();
    wolfBoot_set_partition_state(PART_BOOT, st);
    hal_flash_lock();
}

uint8_t wolfBoot_find_header(uint8_t *haystack, uint8_t type, uint8_t **ptr)
{
    uint8_t *p = haystack;
    while (*p != 0) {
        if (*p == HDR_PADDING) {
            p++;
            continue;
        }
        if (*p == type) {
            p++;
            *ptr = (p + 1);
            return *p;
        } 
        p++;
        p += (*p + 1);
    }
    *ptr = NULL;
    return 0;
}

#ifdef EXT_FLASH
static uint8_t hdr_cpy[IMAGE_HEADER_SIZE];
static uint32_t hdr_cpy_done = 0;
#endif

uint32_t wolfBoot_get_image_version(uint8_t part)
{
    uint32_t *version_field = NULL;
    uint32_t version = 0;
    uint8_t *image = NULL;
    uint32_t *magic = NULL;
    if(part == PART_UPDATE) {
#ifdef PART_UPDATE_EXT
        ext_flash_read((uint32_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS, hdr_cpy, IMAGE_HEADER_SIZE);
        hdr_cpy_done = 1;
        image = hdr_cpy;
#else
        image = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
#endif
    }
    if (part == PART_BOOT)
        image = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;

    if (image) {
        magic = (uint32_t *)image;
        if (*magic != WOLFBOOT_MAGIC)
            return 0;
        wolfBoot_find_header(image + IMAGE_HEADER_OFFSET, HDR_VERSION, (void *)&version_field);
        if (version_field)
            return *version_field;
    }
    return 0;
}

