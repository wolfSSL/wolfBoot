/* loader.c
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
#include <loader.h>
#include <image.h>
#include <hal.h>

extern void do_boot(const uint32_t *app_offset);

static int wolfBoot_copy(uint32_t src, uint32_t dst, uint32_t size)
{
    uint32_t *orig, *copy;
    uint32_t pos = 0;
    if (src == dst)
        return 0;
    if ((src & 0x03) || (dst & 0x03))
        return -1;
    while (pos < size) {
        orig = (uint32_t *)(src + pos);
        copy = (uint32_t *)(dst + pos);
        while (*orig != *copy)
            hal_flash_write(dst + pos, (void *)orig, sizeof(uint32_t));
        pos += sizeof(uint32_t);
    }
    return pos;
}

static int wolfBoot_update(void)
{
    uint32_t total_size = 0;
    uint32_t sector_size = WOLFBOOT_SECTOR_SIZE;
    uint32_t sector = 0;
    uint8_t flag, st;
    struct wolfBoot_image boot, update;

    /* Use biggest size for the swap */
    if ((wolfBoot_open_image(&update, PART_UPDATE) == 0) && (update.fw_size + IMAGE_HEADER_SIZE) > total_size)
            total_size = update.fw_size + IMAGE_HEADER_SIZE;
    if ((wolfBoot_open_image(&boot, PART_BOOT) == 0) && (boot.fw_size + IMAGE_HEADER_SIZE) > total_size)
            total_size = boot.fw_size + IMAGE_HEADER_SIZE;

    if (total_size < IMAGE_HEADER_SIZE)
        return -1;

    /* Check the first sector to detect interrupted update */
    if ((wolfBoot_get_sector_flag(PART_UPDATE, 0, &flag) < 0) || (flag == SECT_FLAG_NEW))
    {
        /* In case this is a new update, check 
         * integrity/authenticity of the firmware update 
         * before starting the swap
         */
        if (!update.hdr_ok || (wolfBoot_verify_integrity(&update) < 0)  
                || (wolfBoot_verify_authenticity(&update) < 0)) {
            return -1;
        }
    }

    hal_flash_unlock();

    /* Interruptible swap
     * The status is saved in the sector flags of the update partition.
     * If something goes wrong, the operation will be resumed upon reboot.
     */
    while ((sector * sector_size) < total_size) {
        if ((wolfBoot_get_sector_flag(PART_UPDATE, sector, &flag) != 0) || (flag == SECT_FLAG_NEW)) {
           flag = SECT_FLAG_SWAPPING;
           hal_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
           wolfBoot_copy(WOLFBOOT_PARTITION_UPDATE_ADDRESS + sector * sector_size,
                   WOLFBOOT_PARTITION_SWAP_ADDRESS,
                   WOLFBOOT_SECTOR_SIZE);
           wolfBoot_set_sector_flag(PART_UPDATE, sector, flag);
        }
        if (flag == SECT_FLAG_SWAPPING) {
            uint32_t size = total_size - (sector * sector_size);
            if (size > sector_size)
                size = sector_size;
            flag = SECT_FLAG_BACKUP;
            hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS + sector * sector_size,
                    WOLFBOOT_SECTOR_SIZE);
            wolfBoot_copy(WOLFBOOT_PARTITION_BOOT_ADDRESS + sector * sector_size,
                   WOLFBOOT_PARTITION_UPDATE_ADDRESS + sector * sector_size,
                   WOLFBOOT_SECTOR_SIZE);
            wolfBoot_set_sector_flag(PART_UPDATE, sector, flag);
        }
        if (flag == SECT_FLAG_BACKUP) {
            uint32_t size = total_size - (sector * sector_size);
            if (size > sector_size)
                size = sector_size;
            flag = SECT_FLAG_UPDATED;
            hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS + sector * sector_size,
                    sector_size);
            wolfBoot_copy(WOLFBOOT_PARTITION_SWAP_ADDRESS,
                   WOLFBOOT_PARTITION_BOOT_ADDRESS + sector * sector_size,
                   size);
            wolfBoot_set_sector_flag(PART_UPDATE, sector, flag);
        }
        sector++;
    }
    while((sector * sector_size) < WOLFBOOT_PARTITION_SIZE) {
        hal_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS + sector * sector_size,
                sector_size);
        hal_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS + sector * sector_size,
                sector_size);
        sector++;
    }
    hal_flash_erase(WOLFBOOT_PARTITION_SWAP_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_BOOT, st);
    hal_flash_lock();
    return 0;
}

static void wolfBoot_start(void)
{
    uint8_t st;
    struct wolfBoot_image boot, update;
    if ((wolfBoot_get_partition_state(PART_UPDATE, &st) == 0) && (st == IMG_STATE_UPDATING)) {
        wolfBoot_update();
    } else if ((wolfBoot_get_partition_state(PART_BOOT, &st) == 0) && (st == IMG_STATE_TESTING)) {
        wolfBoot_update_trigger();
        wolfBoot_update();
    }
    if ((wolfBoot_open_image(&boot, PART_BOOT) < 0) ||
            (wolfBoot_verify_integrity(&boot) < 0)  ||
            (wolfBoot_verify_authenticity(&boot) < 0)) {
        if (wolfBoot_update() < 0) {
            while(1)
                /* panic */;
        }
    }
    do_boot((void *)boot.fw_base);
}

int main(void)
{
    hal_init();
    wolfBoot_start();
    while(1)
        ;
    return 0;
}
