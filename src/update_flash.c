/* update_flash.c
 *
 * Implementation for Flash based updater
 *
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

#include <string.h>
#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "target.h"
#include "wolfboot/wolfboot.h"

#include "delta.h"
#include "printf.h"
#ifdef EXT_ENCRYPTED
int wolfBoot_force_fallback_iv(int enable);
#endif
#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif
#ifdef SECURE_PKCS11
int WP11_Library_Init(void);
#endif

#ifdef EXT_ENCRYPTED
#include "encrypt.h"
#endif /* EXT_ENCRYPTED */

#ifdef MMU
#error "MMU is not yet supported for update_flash.c, please consider update_ram.c instead"
#endif

#ifdef RAM_CODE
#ifndef TARGET_rp2350
extern unsigned int _start_text;
#else
extern unsigned int __logical_binary_start;
unsigned int _start_text = (unsigned int)&__logical_binary_start;
#endif
static volatile const uint32_t __attribute__((used)) wolfboot_version = WOLFBOOT_VERSION;

#ifdef EXT_FLASH
#  ifndef BUFFER_DECLARED
#  define BUFFER_DECLARED
static uint8_t buffer[FLASHBUFFER_SIZE] XALIGNED(4);
#  endif
#endif


static void RAMFUNCTION wolfBoot_erase_bootloader(void)
{
    uint32_t len = WOLFBOOT_PARTITION_BOOT_ADDRESS - ARCH_FLASH_OFFSET;
    hal_flash_erase(ARCH_FLASH_OFFSET, len);

}

#include <string.h>

#ifdef WOLFBOOT_SELF_HEADER
static void RAMFUNCTION wolfBoot_update_self_header(struct wolfBoot_image* src)
{
    uint32_t  offset = 0;
    uint8_t   buffer[FLASHBUFFER_SIZE];
    int       dst_ext = 0;
    uintptr_t dst_int_addr;
    uintptr_t dst_ext_addr = WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS;
    dst_int_addr           = WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS;

    /* Determine destination flash type */
#if defined(EXT_FLASH) && defined(WOLFBOOT_SELF_HEADER_EXT)
    dst_ext = 1;
#endif

#ifdef EXT_FLASH
    /* Erase the self-header sector - sets all bytes to 0xFF */
    if (dst_ext) {
        ext_flash_unlock();
        ext_flash_erase(dst_ext_addr, WOLFBOOT_SELF_HEADER_SIZE);
    }
    else
#endif
    {
        hal_flash_erase(dst_int_addr, WOLFBOOT_SELF_HEADER_SIZE);
    }

    /* Write only the actual header data (IMAGE_HEADER_SIZE bytes).
     * Any reserved space beyond IMAGE_HEADER_SIZE remains 0xFF from erase. */
    while (offset < IMAGE_HEADER_SIZE) {
        uint32_t chunk = IMAGE_HEADER_SIZE - offset;
        if (chunk > FLASHBUFFER_SIZE) {
            chunk = FLASHBUFFER_SIZE;
        }

#ifdef EXT_FLASH
        if (PART_IS_EXT(src)) {
            ext_flash_check_read((uintptr_t)(src->hdr) + offset, (void*)buffer,
                                 chunk);
        }
        else
#endif
        {
            memcpy(buffer, (uint8_t*)(src->hdr + offset), chunk);
        }

#ifdef EXT_FLASH
        if (dst_ext) {
            ext_flash_write(dst_ext_addr + offset, buffer, chunk);
        }
        else
#endif
        {
            hal_flash_write(dst_int_addr + offset, buffer, chunk);
        }

        offset += chunk;
    }

#ifdef EXT_FLASH
    if (dst_ext) {
        ext_flash_lock();
    }
#endif
}
#endif

static void RAMFUNCTION wolfBoot_self_update(struct wolfBoot_image *src)
{
    uintptr_t pos = 0;
    uintptr_t src_offset = IMAGE_HEADER_SIZE;
#ifdef ARCH_SIM
    uintptr_t start_text = ARCH_FLASH_OFFSET;
#else
    uintptr_t start_text = (uintptr_t)&_start_text; /* save off before erase */
#endif

    hal_flash_unlock();
    wolfBoot_erase_bootloader();
#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
        while (pos < src->fw_size) {
            uint8_t buffer[FLASHBUFFER_SIZE];
            if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
                ext_flash_check_read((uintptr_t)(src->hdr) + src_offset + pos, (void*)buffer, FLASHBUFFER_SIZE);
                hal_flash_write(start_text + pos, buffer, FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
    }
    else
#endif
    {
        while (pos < src->fw_size) {
            if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
                uint8_t *orig = (uint8_t*)(src->hdr + src_offset + pos);
                hal_flash_write(pos + start_text, orig, FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
    }

#ifdef WOLFBOOT_SELF_HEADER
    wolfBoot_update_self_header(src);
#endif

    hal_flash_lock();
    arch_reboot();
}

void RAMFUNCTION wolfBoot_check_self_update(void)
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
        PART_SANITY_CHECK(&update);
        wolfBoot_self_update(&update);
    }
}
#endif /* RAM_CODE for self_update */

static int RAMFUNCTION wolfBoot_copy_sector(struct wolfBoot_image *src,
    struct wolfBoot_image *dst, uint32_t sector)
{
    uint32_t pos = 0;
    uint32_t src_sector_offset = (sector * WOLFBOOT_SECTOR_SIZE);
    uint32_t dst_sector_offset = src_sector_offset;
#ifdef EXT_ENCRYPTED
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    uint32_t iv_counter;
#endif

    if (src == dst)
        return 0;

    wolfBoot_printf("Copy sector %d (part %d->%d)\n",
        sector, src->part, dst->part);

    if (src->part == PART_SWAP)
        src_sector_offset = 0;
    if (dst->part == PART_SWAP)
        dst_sector_offset = 0;

#ifdef EXT_ENCRYPTED
    if (wolfBoot_initialize_encryption() < 0)
        return -1;

    wolfBoot_get_encrypt_key(key, nonce);
    if (src->part == PART_SWAP)
        iv_counter = dst_sector_offset;
    else
        /*
         * Always re-derive the IV starting from the source address.
         * This guarantees we do not reuse the same IV in the SWAP partition.
         */
        iv_counter = src_sector_offset;
    iv_counter /= ENCRYPT_BLOCK_SIZE;
    wolfBoot_crypto_set_iv(nonce, iv_counter);
#endif /* EXT_ENCRYPTED */

#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
#ifndef BUFFER_DECLARED
#define BUFFER_DECLARED
        static uint8_t buffer[FLASHBUFFER_SIZE] XALIGNED(4);
#endif
        wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
        while (pos < WOLFBOOT_SECTOR_SIZE)  {
          if (src_sector_offset + pos <
              (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE)) {
              /* bypass decryption, copy encrypted data into swap if its external */
              if (dst->part == PART_SWAP && SWAP_EXT) {
                  ext_flash_read((uintptr_t)(src->hdr) + src_sector_offset + pos,
                                 (void *)buffer, FLASHBUFFER_SIZE);
              } else {
                  ext_flash_check_read((uintptr_t)(src->hdr) + src_sector_offset +
                                         pos,
                                     (void *)buffer, FLASHBUFFER_SIZE);
              }

              wb_flash_write(dst, dst_sector_offset + pos, buffer,
                  FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
        return pos;
    }
#endif
    wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
    while (pos < WOLFBOOT_SECTOR_SIZE) {
        if (src_sector_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE +
            FLASHBUFFER_SIZE))  {
            uint8_t *orig = (uint8_t*)(src->hdr + src_sector_offset + pos);
            wb_flash_write(dst, dst_sector_offset + pos, orig, FLASHBUFFER_SIZE);
        }
        pos += FLASHBUFFER_SIZE;
    }
    return pos;
}

#ifdef EXT_ENCRYPTED
static int RAMFUNCTION wolfBoot_backup_last_boot_sector(uint32_t sector)
{
    uint32_t pos = 0;
    uint32_t src_sector_offset = (sector * WOLFBOOT_SECTOR_SIZE);
    uint32_t dst_sector_offset = 0;
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    uint32_t iv_counter;
    uint8_t block[ENCRYPT_BLOCK_SIZE], encrypted_block[ENCRYPT_BLOCK_SIZE];
    struct wolfBoot_image src[1], dst[1];

    wolfBoot_open_image(src, PART_BOOT);
    wolfBoot_open_image(dst, PART_SWAP);

    wolfBoot_printf("Copy sector %d (part %d->%d)\n",
        sector, src->part, dst->part);

    wolfBoot_get_encrypt_key(key, nonce);
    wolfBoot_printf("In function wolfBoot_backup_last_boot_sector (sector # %u)\n",
            sector);

    iv_counter = src_sector_offset;
    iv_counter /= ENCRYPT_BLOCK_SIZE;
    if (wolfBoot_initialize_encryption() < 0)
        return -1;
    /*
     * Preserve the IV sequence used by the source sector so that the staging
     * copy in SWAP can be decrypted with exactly the same keystream when it is
     * restored to BOOT.
     */
    wolfBoot_crypto_set_iv(nonce, iv_counter);

    /* Erase swap space */
    wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
    if (PART_IS_EXT(dst)) {
        uint8_t *orig = (uint8_t *)(WOLFBOOT_PARTITION_BOOT_ADDRESS) +
            src_sector_offset;
        while (pos < WOLFBOOT_SECTOR_SIZE) {
            XMEMCPY(block, orig + pos, ENCRYPT_BLOCK_SIZE);
            crypto_encrypt(encrypted_block, block, ENCRYPT_BLOCK_SIZE);
            wb_flash_write(dst, dst_sector_offset + pos, encrypted_block, ENCRYPT_BLOCK_SIZE);
            pos += ENCRYPT_BLOCK_SIZE;
        }
        return 0;
    } else
        return wolfBoot_copy_sector(src, dst, sector);
}
#else
#define wolfBoot_backup_last_boot_sector(sec) wolfBoot_copy_sector(boot, swap, sec)
#endif

#if !defined(DISABLE_BACKUP) && !defined(CUSTOM_PARTITION_TRAILER)

#ifdef EXT_ENCRYPTED
#   define TRAILER_OFFSET_WORDS \
        ((ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) / sizeof(uint32_t))
#else
#   define TRAILER_OFFSET_WORDS 0
#endif

/**
 * @brief Performs the final swap and erase operations during a secure update,
 * ensuring that if power is lost during the update, the process can be resumed
 * on next boot. Not supported with CUSTOM_PARTITION_TRAILER
 *
 * This function handles the final phase of the three-way swap update process.
 * It ensures that the update is atomic and power-fail safe by:
 * 1. Saving the sector at tmpBootPos (staging sector) to the swap area
 * 2. Setting a magic trailer value to mark the swap as in progress
 * 3. Erasing the last sector(s) of the boot partition (where partition state is stored)
 * 4. Restoring the saved staging sector from swap back to boot
 * 5. Setting the boot partition state to TESTING
 * 6. Erasing the last sector(s) of the update partition
 *
 * The staging sector (tmpBootPos) is positioned right before the final sectors
 * that will be erased. This sector is preserved and used to store a magic trailer
 * that indicates a swap operation is in progress.
 *
 * The function can be called in two modes:
 * - Normal mode (resume=0): Initiates the swap and erase process
 * - Resume mode (resume=1): Checks if a swap was interrupted and completes it
 *
 * @param resume If 1, checks for interrupted swap and resumes it; if 0, starts
 * new swap
 * @return 0 on success, negative value if no swap needed or on error
 */
static int RAMFUNCTION wolfBoot_swap_and_final_erase(int resume)
{
    struct wolfBoot_image boot[1];
    struct wolfBoot_image update[1];
    struct wolfBoot_image swap[1];
    uint8_t updateState;
    int eraseLen = (WOLFBOOT_SECTOR_SIZE
#ifdef NVM_FLASH_WRITEONCE /* need to erase the redundant sector too */
        * 2
#endif
    );
    int swapDone = 0;
    /* Calculate position of staging sector - just before the final sectors
     * that store partition state */
    uintptr_t tmpBootPos = WOLFBOOT_PARTITION_SIZE - eraseLen -
        WOLFBOOT_SECTOR_SIZE;
    uint32_t tmpBuffer[TRAILER_OFFSET_WORDS + 1];

    /* open partitions (ignore failure) */
    wolfBoot_open_image(boot, PART_BOOT);
    wolfBoot_open_image(update, PART_UPDATE);
    wolfBoot_open_image(swap, PART_SWAP);
    wolfBoot_get_partition_state(PART_UPDATE, &updateState);

    /* Read the trailer from the staging sector to check if we're resuming an
     * interrupted operation */
#if defined(EXT_FLASH) && PARTN_IS_EXT(PART_BOOT)
    ext_flash_read((uintptr_t)(boot->hdr + tmpBootPos), (void*)tmpBuffer,
        sizeof(tmpBuffer));
#else
    memcpy(tmpBuffer, boot->hdr + tmpBootPos, sizeof(tmpBuffer));
#endif

    /* Check if the magic trailer exists - indicates an interrupted swap
     * operation */
    /* final swap and erase flag is WOLFBOOT_MAGIC_TRAIL */
    if (tmpBuffer[TRAILER_OFFSET_WORDS] == WOLFBOOT_MAGIC_TRAIL) {
        swapDone = 1;
    }
    /* If we're in resume mode but no swap was in progress, return */
    if ((resume == 1) && (swapDone == 0) &&
        (updateState != IMG_STATE_FINAL_FLAGS)
    ) {
        return -1;
    }

    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif

    /* If update state isn't set to FINAL_FLAGS, this is the first run of the function */
    /* IMG_STATE_FINAL_FLAGS allows re-entry without blowing away swap */
    if (updateState != IMG_STATE_FINAL_FLAGS) {
        /* First, backup the staging sector (sector at tmpBootPos) into swap partition */
        /* This sector will be modified with the magic trailer, so we need to preserve it */
        wolfBoot_backup_last_boot_sector(tmpBootPos / WOLFBOOT_SECTOR_SIZE);
        wolfBoot_printf("Copied boot sector to swap\n");
        /* Mark update as being in final swap phase to allow resumption if power fails */
        wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_FINAL_FLAGS);
    }
#ifdef EXT_ENCRYPTED
    wolfBoot_printf("In function wolfBoot_final_swap: swapDone = %d\n", swapDone);
    if (swapDone == 0) {
        /* For encrypted images: Get the encryption key and IV */
        wolfBoot_get_encrypt_key((uint8_t*)tmpBuffer,
            (uint8_t*)&tmpBuffer[ENCRYPT_KEY_SIZE/sizeof(uint32_t)]);
        /* Set the magic trailer in the buffer and write it to the staging sector */
        tmpBuffer[TRAILER_OFFSET_WORDS] = WOLFBOOT_MAGIC_TRAIL;

        wb_flash_erase(boot, tmpBootPos, WOLFBOOT_SECTOR_SIZE);
        wb_flash_write(boot, tmpBootPos, (void*)tmpBuffer, sizeof(tmpBuffer));
    }
#endif
    /* Erase the last sector(s) of boot partition (where partition state is stored) */
    wb_flash_erase(boot, WOLFBOOT_PARTITION_SIZE - eraseLen, eraseLen);

#ifdef EXT_ENCRYPTED
    /* Initialize encryption with the saved key */
    wolfBoot_set_encrypt_key((uint8_t*)tmpBuffer,
            (uint8_t*)&tmpBuffer[ENCRYPT_KEY_SIZE/sizeof(uint32_t)]);
    /* wolfBoot_set_encrypt_key calls hal_flash_unlock, need to unlock again */
    hal_flash_unlock();
#endif
    /* Restore the original contents of the staging sector (with the magic trailer if encrypted) */
    if (tmpBootPos < boot->fw_size + IMAGE_HEADER_SIZE) {
        wolfBoot_printf("Restoring last boot sector from swap\n");
        wolfBoot_copy_sector(swap, boot, tmpBootPos / WOLFBOOT_SECTOR_SIZE);
    }
    else {
        wb_flash_erase(boot, tmpBootPos, WOLFBOOT_SECTOR_SIZE);
    }

    /* Mark boot partition as TESTING - this tells bootloader to fallback if update fails */
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);

    /* Erase the last sector(s) of update partition */
    /* This resets the update partition state to IMG_STATE_NEW */
    wb_flash_erase(update, WOLFBOOT_PARTITION_SIZE - eraseLen, eraseLen);

#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

    return 0;
}
#endif /* !DISABLE_BACKUP && !CUSTOM_PARTITION_TRAILER */

#ifdef DELTA_UPDATES

    #ifndef DELTA_BLOCK_SIZE
    #   define DELTA_BLOCK_SIZE 1024
    #endif

static int wolfBoot_delta_update(struct wolfBoot_image *boot,
    struct wolfBoot_image *update, struct wolfBoot_image *swap, int inverse,
    int resume)
{
    int sector = 0;
    int ret;
    uint8_t flag;
    uint8_t delta_blk[DELTA_BLOCK_SIZE];
    uint32_t *img_offset;
    uint32_t *img_size;
    uint32_t total_size;
    WB_PATCH_CTX ctx;
    uint32_t cur_v, upd_v, delta_base_v;
#ifdef EXT_ENCRYPTED
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    uint8_t enc_blk[DELTA_BLOCK_SIZE];
#endif
    uint16_t delta_base_hash_sz;
    uint8_t *delta_base_hash;
    uint16_t base_hash_sz;
    uint8_t *base_hash;

    if (boot->fw_size == 0) {
        /* Resume after powerfail can leave boot header erased; bound by partition size. */
        boot->fw_size = WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE;
    }

    /* Use biggest size for the swap */
    total_size = boot->fw_size + IMAGE_HEADER_SIZE;
    if ((update->fw_size + IMAGE_HEADER_SIZE) > total_size)
            total_size = update->fw_size + IMAGE_HEADER_SIZE;

    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif
    /* Read encryption key/IV before starting the update */
#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
#endif
    if (wolfBoot_get_delta_info(PART_UPDATE, inverse, &img_offset, &img_size,
                &delta_base_hash, &delta_base_hash_sz) < 0) {
        return -1;
    }
    cur_v = wolfBoot_current_firmware_version();
    upd_v = wolfBoot_update_firmware_version();
    delta_base_v = wolfBoot_get_diffbase_version(PART_UPDATE);

    if (delta_base_hash_sz != WOLFBOOT_SHA_DIGEST_SIZE) {
        if (delta_base_hash_sz == 0) {
            wolfBoot_printf("Warning: delta update: Base hash not found in image\n");
            delta_base_hash = NULL;
        } else {
            wolfBoot_printf("Error: delta update: Base hash size mismatch"
                    " (size: %x expected %x)\n", delta_base_hash_sz,
                    WOLFBOOT_SHA_DIGEST_SIZE);
            return -1;
        }
    }

#if defined(WOLFBOOT_HASH_SHA256)
    base_hash_sz = wolfBoot_find_header(boot->hdr + IMAGE_HEADER_OFFSET,
            HDR_SHA256, &base_hash);
#elif defined(WOLFBOOT_HASH_SHA384)
    base_hash_sz = wolfBoot_find_header(boot->hdr + IMAGE_HEADER_OFFSET,
            HDR_SHA384, &base_hash);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    base_hash_sz = wolfBoot_find_header(boot->hdr + IMAGE_HEADER_OFFSET,
            HDR_SHA3_384, &base_hash);
#else
    #error "Delta update: Fatal error, no hash algorithm defined!"
#endif

    if (inverse) {
        /* Fallback path: accept the delta when resuming or when the base image
         * matches the recorded diff origin. */
        if (resume ||
            ((cur_v == upd_v) && (delta_base_v <= cur_v)) ||
            ((cur_v == delta_base_v) && (upd_v >= cur_v))) {
            ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size +
                    IMAGE_HEADER_SIZE, update->hdr + *img_offset, *img_size);
        } else {
            wolfBoot_printf("Delta version check failed! "
                "Cur 0x%x, Upd 0x%x, Delta 0x%x\n",
                cur_v, upd_v, delta_base_v);
            ret = -1;
        }
    } else {
        if (!resume && (cur_v != delta_base_v)) {
            /* Wrong base image version, cannot apply delta patch */
            wolfBoot_printf("Delta Base 0x%x != Cur 0x%x\n",
                cur_v, delta_base_v);
            ret = -1;
        } else if (!resume && delta_base_hash &&
                memcmp(base_hash, delta_base_hash, base_hash_sz) != 0) {
            /* Wrong base image digest, cannot apply delta patch */
            wolfBoot_printf("Delta Base hash mismatch\n");
            ret = -1;
        } else {
            ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size + IMAGE_HEADER_SIZE,
                    update->hdr + IMAGE_HEADER_SIZE, *img_size);
        }
    }
    if (ret < 0)
        goto out;

    while((sector * WOLFBOOT_SECTOR_SIZE) < (int)total_size) {
        if ((wolfBoot_get_update_sector_flag(sector, &flag) != 0) ||
                (flag == SECT_FLAG_NEW)) {
            uint32_t len = 0;
            wb_flash_erase(swap, 0, WOLFBOOT_SECTOR_SIZE);
            while (len < WOLFBOOT_SECTOR_SIZE) {
                ret = wb_patch(&ctx, delta_blk, DELTA_BLOCK_SIZE);
                if (ret > 0) {
#ifdef EXT_ENCRYPTED
                    uint32_t iv_counter = sector * WOLFBOOT_SECTOR_SIZE + len;
                    int wr_ret;
                    if (wolfBoot_initialize_encryption() < 0) {
                        ret = -1;
                        goto out;
                    }
                    iv_counter /= ENCRYPT_BLOCK_SIZE;
                    /* Encrypt + send */
                    wolfBoot_crypto_set_iv(nonce, iv_counter);
                    crypto_encrypt(enc_blk, delta_blk, ret);
                    wr_ret = ext_flash_write(
                            (uint32_t)(WOLFBOOT_PARTITION_SWAP_ADDRESS + len),
                            enc_blk, ret);
                    if (wr_ret < 0) {
                        ret = wr_ret;
                        goto out;
                    }
#else
                    wb_flash_write(swap, len, delta_blk, ret);
#endif
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
        if (sector == 0) {
            /* New total image size after first sector is patched */
            volatile uint32_t update_size;
            hal_flash_lock();
            update_size =
                wolfBoot_image_size((uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS)
                + IMAGE_HEADER_SIZE;
            hal_flash_unlock();
            if (update_size > total_size)
                total_size = update_size;
            if (total_size <= IMAGE_HEADER_SIZE) {
                ret = -1;
                goto out;
            }
            if (total_size > WOLFBOOT_PARTITION_SIZE) {
                ret = -1;
                goto out;
            }

        }
        sector++;
    }
    ret = 0;
    /* erase to the last sector, writeonce has 2 sectors */
    while((sector * WOLFBOOT_SECTOR_SIZE) < WOLFBOOT_PARTITION_SIZE -
        WOLFBOOT_SECTOR_SIZE
#ifdef NVM_FLASH_WRITEONCE
        * 2
#endif
    ) {
        wb_flash_erase(boot, sector * WOLFBOOT_SECTOR_SIZE, WOLFBOOT_SECTOR_SIZE);
        sector++;
    }
out:
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

#if !defined(DISABLE_BACKUP) && !defined(CUSTOM_PARTITION_TRAILER)
    /* start re-entrant final erase, return code is only for resumption in
     * wolfBoot_start */
    if (ret == 0) {
        wolfBoot_swap_and_final_erase(0);
    }
#endif
    /* encryption key was not erased, will be erased by success */
    return ret;
}

#endif


#ifdef WOLFBOOT_ARMORED
#    ifdef __GNUC__
#        pragma GCC push_options
#        pragma GCC optimize("O0")
#    elif defined(__IAR_SYSTEMS_ICC__)
#        pragma optimize=none
#    endif
#endif

/* Max firmware size: partition must hold header + fw + trailer sector(s) */
#ifndef NVM_FLASH_WRITEONCE
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - \
        IMAGE_HEADER_SIZE - WOLFBOOT_SECTOR_SIZE))
#else
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - \
        IMAGE_HEADER_SIZE - (2 * WOLFBOOT_SECTOR_SIZE)))
#endif

static int wolfBoot_get_total_size(struct wolfBoot_image* boot,
    struct wolfBoot_image* update)
{
    uint32_t total_size = 0;

    /* Use biggest size for the swap */
    total_size = boot->fw_size + IMAGE_HEADER_SIZE;
    if ((update->fw_size + IMAGE_HEADER_SIZE) > total_size)
        total_size = update->fw_size + IMAGE_HEADER_SIZE;

    return total_size;
}

static int RAMFUNCTION wolfBoot_update(int fallback_allowed)
{
    uint32_t total_size = 0;
    const uint32_t sector_size = WOLFBOOT_SECTOR_SIZE;
    uint32_t sector = 0;
    /* we need to pre-set flag to SECT_FLAG_NEW in case magic hasn't been set
     * on the update partition as part of the delta update direction check. if
     * magic has not been set flag will have an un-determined value when we go
     * to check it */
    uint8_t flag = SECT_FLAG_NEW;
    struct wolfBoot_image boot, update, swap;
    uint16_t update_type;
    uint32_t fw_size;
    uint32_t size;
#if defined(DELTA_UPDATES)
    int inverse = 0;
#endif
    int fallback_image = 0;
#ifndef DISABLE_BACKUP
    int rollback_needed = 0;
    int bootStateRet = -1;
    uint8_t bootState = 0;
#endif
#if defined(DISABLE_BACKUP) && defined(EXT_ENCRYPTED)
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
#endif
#ifdef DELTA_UPDATES
    uint8_t st;
    int resume = 0;
    int stateRet = -1;
#endif
    uint32_t cur_ver, upd_ver;

    wolfBoot_printf("Starting Update (fallback allowed %d)\n",
        fallback_allowed);

    /* No Safety check on open: we might be in the middle of a broken update */
    {
        int update_open;
#ifdef EXT_ENCRYPTED
        /* Start with the standard IV mapping for every fresh update attempt. */
        wolfBoot_enable_fallback_iv(0);
#endif
        update_open = wolfBoot_open_image(&update, PART_UPDATE);
#ifdef EXT_ENCRYPTED
        if (update_open < 0) {
            int prev = wolfBoot_enable_fallback_iv(1);
            (void)prev;
            update_open = wolfBoot_open_image(&update, PART_UPDATE);
            if (update_open < 0) {
                wolfBoot_enable_fallback_iv(0);
                return -1;
            }
            fallback_image = 1;
        }
        wolfBoot_enable_fallback_iv(fallback_image);
#else
        if (update_open < 0)
            return -1;
#endif
        wolfBoot_open_image(&boot, PART_BOOT);
        wolfBoot_open_image(&swap, PART_SWAP);

#if defined(EXT_ENCRYPTED) && defined(DELTA_UPDATES)
        wolfBoot_printf("Update partition fallback image: %d\n", fallback_image);
        if (fallback_image)
            inverse = 1;
#endif
    }

    /* get total size */
    total_size = wolfBoot_get_total_size(&boot, &update);
    if (total_size <= IMAGE_HEADER_SIZE) {
        wolfBoot_printf("Image total size %u too large!\n", total_size);
        return -1;
    }
    /* In case this is a new update, do the required
     * checks on the firmware update
     * before starting the swap
     */
    update_type = wolfBoot_get_image_type(PART_UPDATE);

    cur_ver = wolfBoot_current_firmware_version();
    upd_ver = wolfBoot_update_firmware_version();
#ifndef DISABLE_BACKUP
    bootStateRet = wolfBoot_get_partition_state(PART_BOOT, &bootState);
    if ((bootStateRet == 0) && (bootState == IMG_STATE_TESTING) &&
        (fallback_allowed != 0) && (cur_ver >= upd_ver)) {
        rollback_needed = 1;
    }
#endif

    wolfBoot_get_update_sector_flag(0, &flag);
    /* Check the first sector to detect interrupted update */
    if (flag == SECT_FLAG_NEW) {
        if (((update_type & HDR_IMG_TYPE_PART_MASK) != HDR_IMG_TYPE_APP) ||
            ((update_type & HDR_IMG_TYPE_AUTH_MASK) != HDR_IMG_TYPE_AUTH)) {
            wolfBoot_printf("Update type invalid 0x%x!=0x%x\n",
                update_type, HDR_IMG_TYPE_AUTH);
            return -1;
        }
        if (update.fw_size > MAX_UPDATE_SIZE - 1) {
            wolfBoot_printf("Invalid update size %u\n", update.fw_size);
            return -1;
        }
        if (!fallback_image) {
            if (!update.hdr_ok
                    || (wolfBoot_verify_integrity(&update) < 0)
                    || (wolfBoot_verify_authenticity(&update) < 0)) {
                wolfBoot_printf("Update verify failed: Hdr %d, Hash %d, Sig %d\n",
                    update.hdr_ok, update.sha_ok, update.signature_ok);
                return -1;
            }
        } else {
#ifdef EXT_ENCRYPTED
            int prev = wolfBoot_force_fallback_iv(1);
#endif
            if (!update.hdr_ok
                    || (wolfBoot_verify_integrity(&update) < 0)
                    || (wolfBoot_verify_authenticity(&update) < 0)) {
#ifdef EXT_ENCRYPTED
                wolfBoot_force_fallback_iv(prev);
#endif
                wolfBoot_printf("Update verify failed: Hdr %d, Hash %d, Sig %d\n",
                    update.hdr_ok, update.sha_ok, update.signature_ok);
                return -1;
            }
#ifdef EXT_ENCRYPTED
            wolfBoot_force_fallback_iv(prev);
#endif
        }
        PART_SANITY_CHECK(&update);


        wolfBoot_printf("Versions: Current 0x%x, Update 0x%x\n",
            cur_ver, upd_ver);

#ifndef ALLOW_DOWNGRADE
        {
            uint32_t fb_ok = (fallback_allowed == 1);
            VERIFY_VERSION_ALLOWED(fb_ok);
            (void)fb_ok;
        }
        if ((fallback_allowed == 0) && (cur_ver >= upd_ver)) {
            wolfBoot_printf("Update version not allowed\n");
            return -1;
        }
#endif
    }

#ifdef DELTA_UPDATES
    if (cur_ver > upd_ver)
        inverse = 1;

    if ((update_type & 0x00F0) == HDR_IMG_TYPE_DIFF) {
        /* if magic isn't set stateRet will be -1 but that means we're on a
         * fresh partition and aren't resuming */
        stateRet = wolfBoot_get_partition_state(PART_UPDATE, &st);

        /* if we've already written a sector or we've mangled the boot partition
         * header we can't determine the direction by version numbers. instead
         * use the update partition state, updating means regular, new means
         * reverting */
        /* Any touched sector (or lack of recorded version) means we are
         * recovering from an interrupted delta application. */
        if ((flag != SECT_FLAG_NEW) || (cur_ver == 0)) {
            resume = 1;
            if (stateRet == 0) {
                /* Partition trailer tells us whether we were mid-upgrade
                 * (UPDATING) or reverting an older image. */
                if (st == IMG_STATE_UPDATING) {
                    inverse = 0;
                } else {
                    if (cur_ver < upd_ver)
                        inverse = 1;
                    else
                        inverse = 0;
                }
            }
        }
        /* If we're dealing with a "ping-pong" fallback that wasn't interrupted
         * we need to set to UPDATING, otherwise there's no way to tell the
         * original direction of the update once interrupted */
        else if ((inverse == 0) && (fallback_allowed == 1) &&
                 (cur_ver >= upd_ver)) {
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif
            wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_NEW);
#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
            inverse = 1;
        }

        return wolfBoot_delta_update(&boot, &update, &swap, inverse, resume);
    }
#endif

#ifndef DISABLE_BACKUP
    /* Interruptible swap */

    hal_flash_unlock();
    #ifdef EXT_FLASH
    ext_flash_unlock();
    #endif
    /* Interruptible swap
     * The status is saved in the sector flags of the update partition.
     * If something goes wrong, the operation will be resumed upon reboot.
     */
    while ((sector * sector_size) < total_size) {
        flag = SECT_FLAG_NEW;
        wolfBoot_get_update_sector_flag(sector, &flag);
        switch (flag) {
            case SECT_FLAG_NEW:
               flag = SECT_FLAG_SWAPPING;
               wolfBoot_copy_sector(&update, &swap, sector);
               if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
                   wolfBoot_set_update_sector_flag(sector, flag);
                /* FALL THROUGH */
            case SECT_FLAG_SWAPPING:
                size = total_size - (sector * sector_size);
                if (size > sector_size)
                    size = sector_size;
                flag = SECT_FLAG_BACKUP;
                {
#ifdef EXT_ENCRYPTED
                    /*
                     * When we are performing a fallback, force the alternate
                     * IV offset only for the segment copied from BOOT into
                     * UPDATE.  All other copies see the offset that was
                     * active beforehand (0 for the normal path, fallback
                     * offset for the recovery path).
                     */
                    int prev_iv = wolfBoot_enable_fallback_iv(1);
#endif
                    wolfBoot_copy_sector(&boot, &update, sector);
#ifdef EXT_ENCRYPTED
                    wolfBoot_enable_fallback_iv(prev_iv);
#endif
                }
                if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
                    wolfBoot_set_update_sector_flag(sector, flag);
                /* FALL THROUGH */
            case SECT_FLAG_BACKUP:
                size = total_size - (sector * sector_size);
                if (size > sector_size)
                    size = sector_size;
                flag = SECT_FLAG_UPDATED;
                wolfBoot_copy_sector(&swap, &boot, sector);
                if (((sector + 1) * sector_size) < WOLFBOOT_PARTITION_SIZE)
                    wolfBoot_set_update_sector_flag(sector, flag);
                break;
            case SECT_FLAG_UPDATED:
                /* FALL THROUGH */
            default:
                break;
        }
        sector++;

        /* headers that can be in different positions depending on when the
         * power fails are now in a known state, re-read and swap fw_size
         * because the locations are correct but the metadata is now swapped
         * also recalculate total_size since it could be invalid */
        if (sector == 1) {
            wolfBoot_open_image(&boot, PART_BOOT);
            wolfBoot_open_image(&update, PART_UPDATE);

            /* swap the fw_size since they're now swapped */
            fw_size = boot.fw_size;
            boot.fw_size = update.fw_size;
            update.fw_size = fw_size;

            /* get total size */
            total_size = wolfBoot_get_total_size(&boot, &update);
        }
    }

    /* Erase remainder of partition */
#if defined(WOLFBOOT_FLASH_MULTI_SECTOR_ERASE) || defined(PRINTF_ENABLED)
    /* calculate number of remaining bytes */
    /* reserve 1 sector for status (2 sectors for NV write once) */
#ifdef NVM_FLASH_WRITEONCE
    size = WOLFBOOT_PARTITION_SIZE - (sector * sector_size) - (2 * sector_size);
#else
    size = WOLFBOOT_PARTITION_SIZE - (sector * sector_size) - sector_size;
#endif

    wolfBoot_printf("Erasing remainder of partition (%d sectors)...\n",
        size/sector_size);
#endif

#ifdef WOLFBOOT_FLASH_MULTI_SECTOR_ERASE
    /* Erase remainder of flash sectors in one HAL command. */
    /* This can improve performance if the HAL supports erase of
     * multiple sectors */
    wb_flash_erase(&boot, sector * sector_size, size);
    wb_flash_erase(&update, sector * sector_size, size);
#else
    /* Iterate over every remaining sector and erase individually. */
    /* This loop is smallest code size */
    while ((sector * sector_size) < WOLFBOOT_PARTITION_SIZE -
        sector_size
    #ifdef NVM_FLASH_WRITEONCE
        * 2
    #endif
    ) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        wb_flash_erase(&update, sector * sector_size, sector_size);
        sector++;
    }
#endif /* WOLFBOOT_FLASH_MULTI_SECTOR_ERASE */

    /* encryption key was not erased, will be erased by success */
    #ifdef EXT_FLASH
    ext_flash_lock();
    #endif
    hal_flash_lock();

#if !defined(CUSTOM_PARTITION_TRAILER)
    /* start re-entrant final erase, return code is only for resumption in
     * wolfBoot_start */
    wolfBoot_swap_and_final_erase(0);
#ifndef DISABLE_BACKUP
    if (rollback_needed) {
        hal_flash_unlock();
#ifdef EXT_FLASH
        ext_flash_unlock();
#endif
        wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_SUCCESS);
#ifdef EXT_FLASH
        ext_flash_lock();
#endif
        hal_flash_lock();
    }
#endif
#else
    /* Mark boot partition as TESTING - this tells bootloader to fallback if update fails */
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
#endif

#else /* DISABLE_BACKUP */
#ifdef WOLFBOOT_ELF_FLASH_SCATTER
    unsigned long entry;
    void*         base = (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    wolfBoot_printf("ELF Scattered image digest check\n");
    if (wolfBoot_check_flash_image_elf(PART_BOOT, &entry) < 0) {
        wolfBoot_printf("ELF Scattered image digest check: failed. Restoring "
                        "scattered image...\n");
        wolfBoot_load_flash_image_elf(PART_BOOT, &entry, PART_IS_EXT(boot));
        if (wolfBoot_check_flash_image_elf(PART_BOOT, &entry) < 0) {
            wolfBoot_printf(
                "Fatal: Could not verify digest after scattering. Panic().\n");
            wolfBoot_panic();
        }
    }
    wolfBoot_printf(
        "Scattered image correctly verified. Setting entry point to %lx\n",
        entry);
    boot.fw_base = (void*)entry;
#endif
    /* Direct Swap without power fail safety */

    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif

#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
#endif

    /* Directly copy the content of the UPDATE partition into the BOOT
     * partition. */
    while ((sector * sector_size) < total_size) {
        wolfBoot_copy_sector(&update, &boot, sector);
        sector++;
    }
    /* erase remainder of partition */
#ifdef PRINTF_ENABLED
    size = WOLFBOOT_PARTITION_SIZE - (sector * sector_size);
    wolfBoot_printf("Erasing remainder of partition (%d sectors)...\n",
        size/sector_size);
#endif
    while ((sector * sector_size) < WOLFBOOT_PARTITION_SIZE) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        sector++;
    }


    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_SUCCESS);

    #ifdef EXT_FLASH
    ext_flash_lock();
    #endif
    hal_flash_lock();

    /* Save the encryption key after swapping */
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key(key, nonce);
#endif
#endif /* DISABLE_BACKUP */
#ifdef EXT_ENCRYPTED
    /* Make sure we leave the global IV offset in its normal state. */
    wolfBoot_enable_fallback_iv(0);
#endif
    return 0;
}



#if defined(ARCH_SIM) && defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_SEAL)
int wolfBoot_unlock_disk(void)
{
    int ret;
    struct wolfBoot_image img;
    uint8_t secret[WOLFBOOT_MAX_SEAL_SZ];
    int     secretSz;
    uint8_t* policy = NULL, *pubkey_hint = NULL;
    uint16_t policySz = 0;
    int      nvIndex = 0; /* where the sealed blob is stored in NV */

    memset(secret, 0, sizeof(secret));

    wolfBoot_printf("Unlocking disk...\n");

    /* check policy */
    ret = wolfBoot_open_image(&img, PART_BOOT);
    if (ret == 0) {
        ret = wolfBoot_get_header(&img, HDR_PUBKEY, &pubkey_hint);
        ret = (ret  == WOLFBOOT_SHA_DIGEST_SIZE) ? 0 : -1;
    }
    if (ret == 0) {
        ret = wolfBoot_get_policy(&img, &policy, &policySz);
        if (ret == -TPM_RC_POLICY_FAIL) {
            /* the image is not signed with a policy */
            wolfBoot_printf("Image policy signature missing!\n");
        }
    }
    if (ret == 0) {
        /* try to unseal the secret */
        ret = wolfBoot_unseal(pubkey_hint, policy, policySz, nvIndex,
            secret, &secretSz);
        if (ret != 0) { /* if secret does not exist, expect TPM_RC_HANDLE here */
            if ((ret & RC_MAX_FMT1) == TPM_RC_HANDLE) {
                wolfBoot_printf("Sealed secret does not exist!\n");
            }
            /* create secret to seal */
            secretSz = 32;
            ret = wolfBoot_get_random(secret, secretSz);
            if (ret == 0) {
                wolfBoot_printf("Creating new secret (%d bytes)\n", secretSz);
                wolfBoot_print_hexstr(secret, secretSz, 0);

                /* seal new secret */
                ret = wolfBoot_seal(pubkey_hint, policy, policySz, nvIndex,
                    secret, secretSz);
            }
            if (ret == 0) {
                uint8_t secretCheck[WOLFBOOT_MAX_SEAL_SZ];
                int     secretCheckSz = 0;

                /* unseal again to make sure it works */
                memset(secretCheck, 0, sizeof(secretCheck));
                ret = wolfBoot_unseal(pubkey_hint, policy, policySz, nvIndex,
                    secretCheck, &secretCheckSz);
                if (ret == 0) {
                    if (secretSz != secretCheckSz ||
                        memcmp(secret, secretCheck, secretSz) != 0)
                    {
                        wolfBoot_printf("secret check mismatch!\n");
                        ret = -1;
                    }
                }

                wolfBoot_printf("Secret Check %d bytes\n", secretCheckSz);
                wolfBoot_print_hexstr(secretCheck, secretCheckSz, 0);
                TPM2_ForceZero(secretCheck, sizeof(secretCheck));
            }
        }
    }

    if (ret == 0) {
        wolfBoot_printf("Secret %d bytes\n", secretSz);
        wolfBoot_print_hexstr(secret, secretSz, 0);

        /* TODO: Unlock disk */


        /* Extend a PCR from the mask to prevent future unsealing */
    #if !defined(ARCH_SIM) && !defined(WOLFBOOT_NO_UNSEAL_PCR_EXTEND)
        {
        uint32_t pcrMask;
        uint32_t pcrArraySz;
        uint8_t  pcrArray[1]; /* get one PCR from mask */
        /* random value to extend the first PCR mask */
        const uint8_t digest[WOLFBOOT_TPM_PCR_DIG_SZ] = {
            0xEA, 0xA7, 0x5C, 0xF6, 0x91, 0x7C, 0x77, 0x91,
            0xC5, 0x33, 0x16, 0x6D, 0x74, 0xFF, 0xCE, 0xCD,
            0x27, 0xE3, 0x47, 0xF6, 0x82, 0x1D, 0x4B, 0xB1,
            0x32, 0x70, 0x88, 0xFC, 0x69, 0xFF, 0x6C, 0x02,
        };
        memcpy(&pcrMask, policy, sizeof(pcrMask));
        pcrArraySz = wolfBoot_tpm_pcrmask_sel(pcrMask,
            pcrArray, sizeof(pcrArray)); /* get first PCR from mask */
        wolfBoot_tpm2_extend(pcrArray[0], (uint8_t*)digest, __LINE__);
        }
    #endif
    }
    else {
        wolfBoot_printf("unlock disk failed! %d (%s)\n",
            ret, wolfTPM2_GetRCString(ret));
    }

    TPM2_ForceZero(secret, sizeof(secret));
    return ret;
}
#endif

void RAMFUNCTION wolfBoot_start(void)
{
    int bootRet;
    int updateRet;
#ifndef DISABLE_BACKUP
    int resumedFinalErase;
#endif
    uint8_t bootState;
    uint8_t updateState;
    struct wolfBoot_image boot;

#if defined(ARCH_SIM) && defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_SEAL)
    wolfBoot_unlock_disk();
#endif

#ifdef RAM_CODE
    wolfBoot_check_self_update();
#endif

#ifdef NVM_FLASH_WRITEONCE
    /* nvm_select_fresh_sector needs unlocked flash in cases where the unused
     * sector needs to be erased */
    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif
#endif

#ifdef SECURE_PKCS11
    WP11_Library_Init();
#endif

    bootRet =   wolfBoot_get_partition_state(PART_BOOT, &bootState);
    updateRet = wolfBoot_get_partition_state(PART_UPDATE, &updateState);

#ifdef NVM_FLASH_WRITEONCE
    hal_flash_lock();
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
#endif

#if !defined(DISABLE_BACKUP) && !defined(CUSTOM_PARTITION_TRAILER)
    /* resume the final erase in case the power failed before it finished */
    resumedFinalErase = wolfBoot_swap_and_final_erase(1);
    if ((resumedFinalErase != 0) ||
        ((bootRet == 0) && (bootState == IMG_STATE_TESTING)))
#endif
    {
        /* Check if the BOOT partition is still in TESTING,
         * to trigger fallback.
         */
        if ((bootRet == 0) && (bootState == IMG_STATE_TESTING)) {
            if (updateRet != 0) {
                hal_flash_unlock();
#ifdef EXT_FLASH
                ext_flash_unlock();
#endif
                wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
#ifdef EXT_FLASH
                ext_flash_lock();
#endif
                hal_flash_lock();
                updateRet = 0;
                updateState = IMG_STATE_UPDATING;
            }
            wolfBoot_update(1);
        }

        /* Check for new updates in the UPDATE partition or if we were
         * interrupted during the flags setting */
        else if ((updateRet == 0) && (updateState == IMG_STATE_UPDATING)) {
            /* Check for new updates in the UPDATE partition */
            wolfBoot_update(0);
        }
    }

    bootRet = wolfBoot_open_image(&boot, PART_BOOT);
    wolfBoot_printf("Booting version: 0x%x\n",
        wolfBoot_get_blob_version(boot.hdr));

    if (bootRet < 0
            || (wolfBoot_verify_integrity(&boot) < 0)
            || (wolfBoot_verify_authenticity(&boot) < 0)
    ) {
        wolfBoot_printf("Boot failed: Hdr %d, Hash %d, Sig %d\n",
            boot.hdr_ok, boot.sha_ok, boot.signature_ok);
        wolfBoot_printf("Trying emergency update\n");
        if (likely(wolfBoot_update(1) < 0)) {
            /* panic: no boot option available. */
            wolfBoot_printf("Boot failed! No boot option available!\n");
        #ifdef WOLFBOOT_TPM
            wolfBoot_tpm2_deinit();
        #endif
            wolfBoot_panic();
        } else {
            /* Emergency update successful, try to re-open boot image */
            if (likely(((wolfBoot_open_image(&boot, PART_BOOT) < 0) ||
                    (wolfBoot_verify_integrity(&boot) < 0)  ||
                    (wolfBoot_verify_authenticity(&boot) < 0)
                    ))) {
                wolfBoot_printf("Boot (try 2) failed: Hdr %d, Hash %d, Sig %d\n",
                    boot.hdr_ok, boot.sha_ok, boot.signature_ok);
                /* panic: something went wrong after the emergency update */
            #ifdef WOLFBOOT_TPM
                wolfBoot_tpm2_deinit();
            #endif
                wolfBoot_panic();
            }
        }
    }
    PART_SANITY_CHECK(&boot);

#ifdef WOLFBOOT_ELF_FLASH_SCATTER
    unsigned long entry;
    wolfBoot_printf("ELF Scattered image digest check\n");
    if (wolfBoot_check_flash_image_elf(PART_BOOT, &entry) < 0) {
        wolfBoot_printf("ELF Scattered image digest check: failed. Restoring "
                        "scattered image...\n");
        if (wolfBoot_load_flash_image_elf(PART_BOOT, &entry,
                                          PART_IS_EXT(&boot)) < 0) {
            wolfBoot_printf(
                "ELF: [BOOT] ERROR: could not store scattered image\n");
            wolfBoot_panic();
        }
        if (wolfBoot_check_flash_image_elf(PART_BOOT, &entry) < 0) {
            wolfBoot_printf(
                "Fatal: Could not verify digest after scattering. Panic().\n");
            wolfBoot_panic();
        }
    }
    wolfBoot_printf(
        "Scattered image correctly verified. Setting entry point to %lx\n",
        entry);
    boot.fw_base = (void*)entry;
#endif


#if defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE)
    /* leave TPM2 available to be called from non-secure callable */
    wolfBoot_tpm2_deinit();
#endif

#ifdef ENCRYPT_PKCS11
    pkcs11_crypto_deinit();
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)hal_hsm_disconnect();
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    (void)hal_hsm_server_cleanup();
#endif

    hal_prepare_boot();

    do_boot((void *)boot.fw_base);
}

#ifdef WOLFBOOT_ARMORED
#    ifdef __GNUC__
#        pragma GCC pop_options
#    elif defined(__IAR_SYSTEMS_ICC__)
#        pragma optimize=default
#    endif
#endif
