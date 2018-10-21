/* run.c
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
#include <string.h>
#include <stdint.h>
#include <bootutil/bootutil.h>
#include <bootutil/image.h>

#include "target.h"
#include "sysflash/sysflash.h"
#include "hal/hal_flash.h"

#include <flash_map_backend/flash_map_backend.h>

#define BOOT_LOG_DBG(...) do{}while(0)

#define print_log(...) do{}while(0)

void abort(void)
{
    while(1)
        ;
}

struct area {
    struct flash_area whole;
    struct flash_area *areas;
    uint32_t num_areas;
    uint8_t id;
};


struct area_desc {
    struct area slots[3];
    uint32_t num_slots;
};

static struct area_desc flash_areas[1] = {
    {
        .slots = {
            {
                .whole = {
                    .fa_id = FLASH_AREA_IMAGE_0,
                    .fa_device_id = 0,
                    .fa_off = FLASH_AREA_IMAGE_0_OFFSET,
                    .fa_size = FLASH_AREA_IMAGE_0_SIZE,
                },
                .id = FLASH_AREA_IMAGE_0,
                .num_areas = 1
            }, 
            {
                .whole = {
                    .fa_id = FLASH_AREA_IMAGE_1,
                    .fa_device_id = 0,
                    .fa_off = FLASH_AREA_IMAGE_1_OFFSET,
                    .fa_size = FLASH_AREA_IMAGE_1_SIZE,
                },
                .id = FLASH_AREA_IMAGE_1,
                .num_areas = 1
            },
#ifndef WOLFBOOT_OVERWRITE_ONLY 
            {
                .whole = {
                    .fa_id = FLASH_AREA_IMAGE_SCRATCH,
                    .fa_device_id = 0,
                    .fa_off = FLASH_AREA_IMAGE_SCRATCH_OFFSET,
                    .fa_size = FLASH_AREA_IMAGE_SCRATCH_SIZE,
                },
                .id = FLASH_AREA_IMAGE_SCRATCH,
                .num_areas = 1
            }
        },
        .num_slots = 3
#else
        },
        .num_slots = 2
#endif
    }
} ;

uint8_t flash_area_align(const struct flash_area *area)
{
    (void)area;
    return 1;
}

int flash_area_open(uint8_t id, const struct flash_area **area)
{
    uint32_t i;

    for (i = 0; i < flash_areas->num_slots; i++) {
        if (flash_areas->slots[i].id == id)
            break;
    }
    if (i == flash_areas->num_slots) {
        print_log("Unsupported area\n");
        abort();
    }

    /* Unsure if this is right, just returning the first area. */
    *area = &flash_areas->slots[i].whole;
    return 0;
}

void flash_area_close(const struct flash_area *area)
{
    (void)area;
}

/*
 * Read/write/erase. Offset is relative from beginning of flash area.
 */
int flash_area_read(const struct flash_area *area, uint32_t off, void *dst,
                    uint32_t len)
{
    int i;
    uint8_t *src8, *dst8;
    BOOT_LOG_DBG("%s: area=%d, off=%x, len=%x",
                 __func__, area->fa_id, off, len);
    if (!area)
        return -1;
    if ((off + len) > (area->fa_size))
            return -1;
    src8 = (uint8_t *)(area->fa_off + off);
    dst8 = (uint8_t *)dst;
    for (i = 0; i < len; i++) { 
        dst8[i] = src8[i];
    }
    return 0;
}

int flash_area_write(const struct flash_area *area, uint32_t off, const void *src,
                     uint32_t len)
{
    BOOT_LOG_DBG("%s: area=%d, off=%x, len=%x", __func__,
                 area->fa_id, off, len);
    hal_flash_unlock();
    hal_flash_write(area->fa_off + off, src, len);
    hal_flash_lock();
    return 0;
}

int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len)
{
    BOOT_LOG_DBG("%s: area=%d, off=%x, len=%x", __func__,
                 area->fa_id, off, len);
    hal_flash_unlock();
    hal_flash_erase(area->fa_off + off, len);
    hal_flash_lock();
    return 0;
}

int flash_area_to_sectors(int idx, int *cnt, struct flash_area *ret)
{
    uint32_t i;
    struct area *slot;

    for (i = 0; i < flash_areas->num_slots; i++) {
        if (flash_areas->slots[i].id == idx)
            break;
    }
    if (i == flash_areas->num_slots) {
        print_log("Unsupported area\n");
        abort();
    }

    slot = &flash_areas->slots[i];

    if (slot->num_areas > (uint32_t)*cnt) {
        print_log("Too many areas in slot\n");
        abort();
    }

    *cnt = slot->num_areas;
    memcpy(ret, slot->areas, slot->num_areas * sizeof(struct flash_area));

    return 0;
}

int flash_area_get_sectors(int fa_id, uint32_t *count,
                           struct flash_sector *sectors)
{
    uint32_t i;
    struct area *slot;

    for (i = 0; i < flash_areas->num_slots; i++) {
        if (flash_areas->slots[i].id == fa_id)
            break;
    }
    if (i == flash_areas->num_slots) {
        print_log("Unsupported area\n");
        abort();
    }

    slot = &flash_areas->slots[i];

    if (slot->num_areas > *count) {
        print_log("Too many areas in slot\n");
        abort();
    }

    for (i = 0; i < slot->num_areas; i++) {
        sectors[i].fs_off = slot->areas[i].fa_off -
            slot->whole.fa_off;
        sectors[i].fs_size = slot->areas[i].fa_size;
    }
    *count = slot->num_areas;

    return 0;
}

int flash_area_id_from_image_slot(int slot)
{
    return slot + FLASH_AREA_IMAGE_0;
}

uint8_t flash_area_erased_val(const struct flash_area *fap)
{
    (void)fap;
    return 0xff;
}
/*

int _close(int fd)
{
    return -1;
}

int _fstat(int fd)
{
    return -1;
}

int _lseek(int fd, int whence, int off)
{
    return -1;
}

int _read(uint8_t *buf, int len)
{
    return -1;
}

int _isatty(int fd)
{
    return 1;
}

int _write(void *r, uint8_t *text, int len)
{
    return -1;
}

*/
