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

static uint8_t *get_trailer(uint8_t part)
{
    if (part == PART_BOOT)
        return (void *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE);
    else if (part == PART_UPDATE)
        return (void *)(WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE);
    else
        return (void *)0;
}

int wolfBoot_set_partition_state(uint8_t part, uint8_t newst)
{
    uint8_t *trailer_end = get_trailer(part);
    uint32_t *magic;
    uint8_t *state;
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    if (!trailer_end)
        return -1;
    magic = (uint32_t *)(trailer_end - sizeof(uint32_t));
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        hal_flash_write((uint32_t)magic, (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    state = (trailer_end - sizeof(uint32_t)) - 1;
    if (*state != newst)
       hal_flash_write((uint32_t)state, (void *)&newst, 1); 
    return 0;
}

int wolfBoot_set_sector_flag(uint8_t part, uint8_t sector, uint8_t newflag)
{
    uint8_t *trailer_end = get_trailer(part);
    uint32_t *magic;
    uint8_t *flags;
    uint8_t fl_value;
    uint32_t wolfboot_magic_trail = WOLFBOOT_MAGIC_TRAIL;
    uint8_t pos = sector >> 1;
    if (!trailer_end)
        return -1;
    magic = (uint32_t *)(trailer_end - sizeof(uint32_t));
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        hal_flash_write((uint32_t)magic, (void *)&wolfboot_magic_trail, sizeof(uint32_t));
    flags = (trailer_end - sizeof(uint32_t)) - pos;
    if (sector == (pos << 1))
        fl_value = (*flags & 0xF0) | (newflag & 0x0F);
    else
        fl_value = ((newflag & 0x0F) << 4) | (*flags & 0x0F);
    if (fl_value != *flags)
        hal_flash_write((uint32_t)flags, &fl_value, 1);
    return 0;
}

int wolfBoot_get_partition_state(uint8_t part, uint8_t *st)
{
    uint8_t *trailer_end = get_trailer(part);
    uint32_t *magic;
    uint8_t *state;
    if (trailer_end)
        return -1;
    magic = (uint32_t *)(trailer_end - sizeof(uint32_t));
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    state = (trailer_end - sizeof(uint32_t)) - 1;
    *st = *state;
    return 0;
}

int wolfBoot_get_sector_flag(uint8_t part, uint8_t sector, uint8_t *flag)
{
    uint8_t *trailer_end = get_trailer(part);
    uint32_t *magic;
    uint8_t *flags;
    uint8_t pos = sector >> 1;
    if (!trailer_end)
        return -1;
    magic = (uint32_t *)(trailer_end - sizeof(uint32_t));
    if (*magic != WOLFBOOT_MAGIC_TRAIL)
        return -1;
    flags = (trailer_end - sizeof(uint32_t)) - pos;
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
    if (part == PART_UPDATE)
        hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS, WOLFBOOT_PARTITION_SIZE);
    if (part == PART_SWAP)
        hal_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
}

void wolfBoot_update_trigger(void)
{
    uint8_t st = IMG_STATE_UPDATING;
    hal_flash_unlock();
    wolfBoot_set_partition_state(PART_UPDATE, st);
    hal_flash_lock();
}

void wolfBoot_success(void)
{
    uint8_t st = IMG_STATE_SUCCESS;
    hal_flash_unlock();
    wolfBoot_set_partition_state(PART_BOOT, st);
    hal_flash_lock();
}
