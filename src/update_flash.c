/* update_flash.c
 *
 * Implementation for Flash based updater
 *
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

#include <string.h>
#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "wolfboot/wolfboot.h"
#include "delta.h"


#ifdef RAM_CODE
extern unsigned int _start_text;
static volatile const uint32_t __attribute__((used)) wolfboot_version = WOLFBOOT_VERSION;

#ifdef EXT_FLASH
#  ifndef BUFFER_DECLARED
#  define BUFFER_DECLARED
static uint8_t buffer[FLASHBUFFER_SIZE];
#  endif
#endif

static void RAMFUNCTION wolfBoot_erase_bootloader(void)
{
    uint32_t *start = (uint32_t *)&_start_text;
    uint32_t len = WOLFBOOT_PARTITION_BOOT_ADDRESS - (uint32_t)start;
    hal_flash_erase((uint32_t)start, len);

}

#include <string.h>

static void RAMFUNCTION wolfBoot_self_update(struct wolfBoot_image *src)
{
    uint32_t pos = 0;
    uint32_t src_offset = IMAGE_HEADER_SIZE;

    hal_flash_unlock();
    wolfBoot_erase_bootloader();
#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
        while (pos < src->fw_size) {
            uint8_t buffer[FLASHBUFFER_SIZE];
            if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
                ext_flash_check_read((uintptr_t)(src->hdr) + src_offset + pos, (void *)buffer, FLASHBUFFER_SIZE);
                hal_flash_write(pos + (uint32_t)&_start_text, buffer, FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
        goto lock_and_reset;
    }
#endif
    while (pos < src->fw_size) {
        if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
            uint8_t *orig = (uint8_t*)(src->hdr + src_offset + pos);
            hal_flash_write(pos + (uint32_t)&_start_text, orig, FLASHBUFFER_SIZE);
        }
        pos += FLASHBUFFER_SIZE;
    }
#ifdef EXT_FLASH
lock_and_reset:
#endif
    hal_flash_lock();
    arch_reboot();
}

void wolfBoot_check_self_update(void)
{
    uint8_t st;
    struct wolfBoot_image update;

    /* Check for self update in the UPDATE partition */
    if ((wolfBoot_get_partition_state(PART_UPDATE, &st) == 0) && (st == IMG_STATE_UPDATING) &&
            (wolfBoot_open_image(&update, PART_UPDATE) == 0) &&
            wolfBoot_get_image_type(PART_UPDATE) == (HDR_IMG_TYPE_WOLFBOOT | HDR_IMG_TYPE_AUTH)) {
        uint32_t update_version = wolfBoot_update_firmware_version();
        if (update_version <= wolfboot_version) {
            hal_flash_unlock();
            wolfBoot_erase_partition(PART_UPDATE);
            hal_flash_lock();
            return;
        }
        if (wolfBoot_verify_integrity(&update) < 0)
            return;
        if (wolfBoot_verify_authenticity(&update) < 0)
            return;
        wolfBoot_self_update(&update);
    }
}
#endif /* RAM_CODE for self_update */

static int RAMFUNCTION wolfBoot_copy_sector(struct wolfBoot_image *src, struct wolfBoot_image *dst, uint32_t sector)
{
    uint32_t pos = 0;
    uint32_t src_sector_offset = (sector * WOLFBOOT_SECTOR_SIZE);
    uint32_t dst_sector_offset = (sector * WOLFBOOT_SECTOR_SIZE);
    if (src == dst)
        return 0;

    if (src->part == PART_SWAP)
        src_sector_offset = 0;
    if (dst->part == PART_SWAP)
        dst_sector_offset = 0;
#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
#ifndef BUFFER_DECLARED
#define BUFFER_DECLARED
        static uint8_t buffer[FLASHBUFFER_SIZE];
#endif
        wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
        while (pos < WOLFBOOT_SECTOR_SIZE)  {
            if (src_sector_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
                ext_flash_check_read((uintptr_t)(src->hdr) + src_sector_offset + pos, (void *)buffer, FLASHBUFFER_SIZE);
                wb_flash_write(dst, dst_sector_offset + pos, buffer, FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
        return pos;
    }
#endif
    wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
    while (pos < WOLFBOOT_SECTOR_SIZE) {
        if (src_sector_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
            uint8_t *orig = (uint8_t*)(src->hdr + src_sector_offset + pos);
            wb_flash_write(dst, dst_sector_offset + pos, orig, FLASHBUFFER_SIZE);
        }
        pos += FLASHBUFFER_SIZE;
    }
    return pos;
}

#ifdef DELTA_UPDATES

#ifndef DELTA_BLOCK_SIZE
#define DELTA_BLOCK_SIZE 256
#endif

static int wolfBoot_delta_update(struct wolfBoot_image *boot,
        struct wolfBoot_image *update, struct wolfBoot_image *swap, int inverse)
{
    int sector = 0;
    int ret;
    uint8_t flag, st;
    int hdr_size;
    uint8_t delta_blk[DELTA_BLOCK_SIZE];
    uint32_t offset = 0;
    uint16_t ptr_len;


    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif
    /* Read encryption key/IV before starting the update */
#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
#endif
    WB_PATCH_CTX ctx;

    if (inverse) {
        uint32_t cur_v, upd_v, delta_base_v;
        uint32_t *inv_patch_off;
        uint16_t *inv_patch_len;
        cur_v = wolfBoot_current_firmware_version();
        upd_v = wolfBoot_update_firmware_version();
        delta_base_v = wolfBoot_get_diffbase_version(PART_UPDATE);
        if ((cur_v == upd_v) && (delta_base_v < cur_v)) {
            ptr_len = wolfBoot_find_header(update->hdr + IMAGE_HEADER_OFFSET,
                    HDR_IMG_DELTA_INVERSE, (void *)&inv_patch_off);
            if (ptr_len != sizeof(uint32_t))
                return -1;
            ptr_len = wolfBoot_find_header(update->hdr + IMAGE_HEADER_OFFSET,
                    HDR_IMG_DELTA_INVERSE_SIZE, (void *)&inv_patch_len);
            if (ptr_len != sizeof(uint16_t))
                return -1;
            ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size + IMAGE_HEADER_SIZE,
                    update->hdr + *inv_patch_off, *inv_patch_len);
        } else {
            ret = -1;
        }
    } else {
        uint16_t *patch_len;
        ptr_len = wolfBoot_find_header(update->hdr + IMAGE_HEADER_OFFSET,
                HDR_IMG_DELTA_SIZE, (void *)&patch_len);
        if (ptr_len != sizeof(uint16_t))
            return -1;
        ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size + IMAGE_HEADER_SIZE,
                update->hdr + IMAGE_HEADER_SIZE, *patch_len);
    }
    if (ret < 0)
        return ret;

     while((sector * WOLFBOOT_SECTOR_SIZE) < WOLFBOOT_PARTITION_SIZE) {
        if ((wolfBoot_get_update_sector_flag(sector, &flag) != 0) || (flag == SECT_FLAG_NEW)) {
            uint32_t len = 0;
            wb_flash_erase(swap, 0, WOLFBOOT_SECTOR_SIZE);
            while (len < WOLFBOOT_SECTOR_SIZE) {
                ret = wb_patch(&ctx, delta_blk, DELTA_BLOCK_SIZE);
                if (ret > 0) {
                    wb_flash_write(swap, len, delta_blk, ret);
                    len += ret;
                } else if (ret == 0) {
                    break;
                } else
                    goto out;
            }
            flag = SECT_FLAG_SWAPPING;
            wolfBoot_set_update_sector_flag(sector, flag);
        } else {
            /* Consume one sector off the patched image
             * when resuming an interrupted patch
             */
            uint32_t len = 0;
            while (len < WOLFBOOT_SECTOR_SIZE) {
                ret = wb_patch(&ctx, delta_blk, DELTA_BLOCK_SIZE);
                if (ret == 0)
                    break;
                if (ret < 0)
                    goto out;
                len += ret;
            }
        }
        if (flag == SECT_FLAG_SWAPPING) {
           wolfBoot_copy_sector(swap, boot, sector);
           flag = SECT_FLAG_UPDATED;
           if (((sector + 1) * WOLFBOOT_SECTOR_SIZE) < WOLFBOOT_PARTITION_SIZE)
               wolfBoot_set_update_sector_flag(sector, flag);
        }
        sector++;
    }
    ret = 0;
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_BOOT, st);
out:
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

/* Save the encryption key after swapping */
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key(key, nonce);
#endif
    return ret;
}

#endif

static int RAMFUNCTION wolfBoot_update(int fallback_allowed)
{
    uint32_t total_size = 0;
    const uint32_t sector_size = WOLFBOOT_SECTOR_SIZE;
    uint32_t sector = 0;
    uint8_t flag, st;
    struct wolfBoot_image boot, update, swap;
#ifdef EXT_ENCRYPTED
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
#endif

    /* No Safety check on open: we might be in the middle of a broken update */
    wolfBoot_open_image(&update, PART_UPDATE);
    wolfBoot_open_image(&boot, PART_BOOT);
    wolfBoot_open_image(&swap, PART_SWAP);


    /* Use biggest size for the swap */
    total_size = boot.fw_size + IMAGE_HEADER_SIZE;
    if ((update.fw_size + IMAGE_HEADER_SIZE) > total_size)
            total_size = update.fw_size + IMAGE_HEADER_SIZE;

    if (total_size <= IMAGE_HEADER_SIZE)
        return -1;

    /* Check the first sector to detect interrupted update */
    if ((wolfBoot_get_update_sector_flag(0, &flag) < 0) || (flag == SECT_FLAG_NEW))
    {
        uint16_t update_type;
        /* In case this is a new update, do the required
         * checks on the firmware update
         * before starting the swap
         */

        update_type = wolfBoot_get_image_type(PART_UPDATE);
        if (((update_type & 0x000F) != HDR_IMG_TYPE_APP) || ((update_type & 0xFF00) != HDR_IMG_TYPE_AUTH))
            return -1;
        if (!update.hdr_ok || (wolfBoot_verify_integrity(&update) < 0)
                || (wolfBoot_verify_authenticity(&update) < 0)) {
            return -1;
        }
#ifndef ALLOW_DOWNGRADE
        if ( !fallback_allowed &&
                (wolfBoot_update_firmware_version() <= wolfBoot_current_firmware_version()) )
            return -1;
#endif
#ifdef DELTA_UPDATES
        if ((update_type & 0x00F0) == HDR_IMG_TYPE_DIFF) {
            return wolfBoot_delta_update(&boot, &update, &swap, fallback_allowed);
        }
#endif
    }


    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif

/* Read encryption key/IV before starting the update */
#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
#endif

#ifndef DISABLE_BACKUP
    /* Interruptible swap
     * The status is saved in the sector flags of the update partition.
     * If something goes wrong, the operation will be resumed upon reboot.
     */
    while ((sector * sector_size) < total_size) {
        if ((wolfBoot_get_update_sector_flag(sector, &flag) != 0) || (flag == SECT_FLAG_NEW)) {
           flag = SECT_FLAG_SWAPPING;
           wolfBoot_copy_sector(&update, &swap, sector);
           if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
               wolfBoot_set_update_sector_flag(sector, flag);
        }
        if (flag == SECT_FLAG_SWAPPING) {
            uint32_t size = total_size - (sector * sector_size);
            if (size > sector_size)
                size = sector_size;
            flag = SECT_FLAG_BACKUP;
            wolfBoot_copy_sector(&boot, &update, sector);
           if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
                wolfBoot_set_update_sector_flag(sector, flag);
        }
        if (flag == SECT_FLAG_BACKUP) {
            uint32_t size = total_size - (sector * sector_size);
            if (size > sector_size)
                size = sector_size;
            flag = SECT_FLAG_UPDATED;
            wolfBoot_copy_sector(&swap, &boot, sector);
            if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
                wolfBoot_set_update_sector_flag(sector, flag);
        }
        sector++;
    }
    while((sector * sector_size) < WOLFBOOT_PARTITION_SIZE) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        wb_flash_erase(&update, sector * sector_size, sector_size);
        sector++;
    }
    wb_flash_erase(&swap, 0, WOLFBOOT_SECTOR_SIZE);
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_BOOT, st);
#else /* DISABLE_BACKUP */
#warning "Backup mechanism disabled! Update installation will not be interruptible"
    /* Directly copy the content of the UPDATE partition into the BOOT partition.
     * This mechanism is not fail-safe, and will brick your device if interrupted
     * before the copy is finished.
     */
    while ((sector * sector_size) < total_size) {
        if ((wolfBoot_get_update_sector_flag(sector, &flag) != 0) || (flag == SECT_FLAG_NEW)) {
           flag = SECT_FLAG_SWAPPING;
           wolfBoot_copy_sector(&update, &boot, sector);
           if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
               wolfBoot_set_update_sector_flag(sector, flag);
        }
        sector++;
    }
    while((sector * sector_size) < WOLFBOOT_PARTITION_SIZE) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        sector++;
    }
    st = IMG_STATE_SUCCESS;
    wolfBoot_set_partition_state(PART_BOOT, st);
#endif

#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

/* Save the encryption key after swapping */
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key(key, nonce);
#endif
    return 0;
}

void RAMFUNCTION wolfBoot_start(void)
{
    uint8_t st;
    struct wolfBoot_image boot;

#ifdef RAM_CODE
    wolfBoot_check_self_update();
#endif

    /* Check if the BOOT partition is still in TESTING,
     * to trigger fallback.
     */
    if ((wolfBoot_get_partition_state(PART_BOOT, &st) == 0) && (st == IMG_STATE_TESTING)) {
        wolfBoot_update_trigger();
        wolfBoot_update(1);
    } else if ((wolfBoot_get_partition_state(PART_UPDATE, &st) == 0) && (st == IMG_STATE_UPDATING)) {
    /* Check for new updates in the UPDATE partition */
        wolfBoot_update(0);
    }
    if ((wolfBoot_open_image(&boot, PART_BOOT) < 0)
            || (wolfBoot_verify_integrity(&boot) < 0)
            || (wolfBoot_verify_authenticity(&boot) < 0)
            ) {
        if (wolfBoot_update(1) < 0) {
            /* panic: no boot option available. */
            while(1)
                ;
        } else {
            /* Emergency update successful, try to re-open boot image */
            if ((wolfBoot_open_image(&boot, PART_BOOT) < 0) ||
                    (wolfBoot_verify_integrity(&boot) < 0)  ||
                    (wolfBoot_verify_authenticity(&boot) < 0)) {
                /* panic: something went wrong after the emergency update */
                while(1)
                    ;
            }
        }
    }
    hal_prepare_boot();
    do_boot((void *)boot.fw_base);
}
