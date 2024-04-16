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
#include "wolfboot/wolfboot.h"

#include "delta.h"
#include "printf.h"
#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif
#ifdef SECURE_PKCS11
int WP11_Library_Init(void);
#endif

#ifdef RAM_CODE
extern unsigned int _start_text;
static volatile const uint32_t __attribute__((used)) wolfboot_version = WOLFBOOT_VERSION;

#ifdef EXT_FLASH
#  ifndef BUFFER_DECLARED
#  define BUFFER_DECLARED
static uint8_t buffer[FLASHBUFFER_SIZE];
#  endif
#endif

#ifdef EXT_ENCRYPTED
#include "encrypt.h"
#endif

static void RAMFUNCTION wolfBoot_erase_bootloader(void)
{
    uint32_t len = WOLFBOOT_PARTITION_BOOT_ADDRESS - ARCH_FLASH_OFFSET;
    hal_flash_erase(ARCH_FLASH_OFFSET, len);

}

#include <string.h>

static void RAMFUNCTION wolfBoot_self_update(struct wolfBoot_image *src)
{
    uintptr_t pos = 0;
    uintptr_t src_offset = IMAGE_HEADER_SIZE;

    hal_flash_unlock();
    wolfBoot_erase_bootloader();
#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
        while (pos < src->fw_size) {
            uint8_t buffer[FLASHBUFFER_SIZE];
            if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
                uintptr_t opos = pos + ((uintptr_t)&_start_text);
                ext_flash_check_read((uintptr_t)(src->hdr) + src_offset + pos, (void*)buffer, FLASHBUFFER_SIZE);
                hal_flash_write(opos, buffer, FLASHBUFFER_SIZE);
            }
            pos += FLASHBUFFER_SIZE;
        }
        goto lock_and_reset;
    }
#endif
    while (pos < src->fw_size) {
        if (src_offset + pos < (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE))  {
            uint8_t *orig = (uint8_t*)(src->hdr + src_offset + pos);
            hal_flash_write(pos + (uintptr_t)&_start_text, orig, FLASHBUFFER_SIZE);
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

    if (src->part == PART_SWAP)
        src_sector_offset = 0;
    if (dst->part == PART_SWAP)
        dst_sector_offset = 0;

#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
    if(src->part == PART_SWAP)
        iv_counter = dst_sector_offset;
    else
        iv_counter = src_sector_offset;

    iv_counter /= ENCRYPT_BLOCK_SIZE;
    crypto_set_iv(nonce, iv_counter);
#endif

#ifdef EXT_FLASH
    if (PART_IS_EXT(src)) {
#ifndef BUFFER_DECLARED
#define BUFFER_DECLARED
        static uint8_t buffer[FLASHBUFFER_SIZE];
#endif
        wb_flash_erase(dst, dst_sector_offset, WOLFBOOT_SECTOR_SIZE);
        while (pos < WOLFBOOT_SECTOR_SIZE)  {
          if (src_sector_offset + pos <
              (src->fw_size + IMAGE_HEADER_SIZE + FLASHBUFFER_SIZE)) {
              /* bypass decryption, copy encrypted data into swap */
              if (dst->part == PART_SWAP) {
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

#ifdef DELTA_UPDATES

    #ifndef DELTA_BLOCK_SIZE
    #   define DELTA_BLOCK_SIZE 1024
    #endif

static int wolfBoot_delta_update(struct wolfBoot_image *boot,
    struct wolfBoot_image *update, struct wolfBoot_image *swap, int inverse,
    int resume_inverse)
{
    int sector = 0;
    int ret;
    uint8_t flag, st;
    int hdr_size;
    uint8_t delta_blk[DELTA_BLOCK_SIZE];
    uint32_t offset = 0;
    uint16_t ptr_len;
    uint32_t *img_offset;
    uint32_t *img_size;
    uint32_t total_size;
    WB_PATCH_CTX ctx;
#ifdef EXT_ENCRYPTED
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
    uint8_t enc_blk[DELTA_BLOCK_SIZE];
#endif

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
    if (wolfBoot_get_delta_info(PART_UPDATE, inverse, &img_offset, &img_size) < 0) {
        return -1;
    }
    if (inverse) {
        uint32_t cur_v, upd_v, delta_base_v;
        cur_v = wolfBoot_current_firmware_version();
        upd_v = wolfBoot_update_firmware_version();
        delta_base_v = wolfBoot_get_diffbase_version(PART_UPDATE);
        if (((cur_v == upd_v) && (delta_base_v < cur_v)) || resume_inverse) {
            ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size +
                    IMAGE_HEADER_SIZE, update->hdr + *img_offset, *img_size);
        } else {
            ret = -1;
        }
    } else {
        ret = wb_patch_init(&ctx, boot->hdr, boot->fw_size + IMAGE_HEADER_SIZE,
                update->hdr + IMAGE_HEADER_SIZE, *img_size);
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
                    iv_counter /= ENCRYPT_BLOCK_SIZE;
                    /* Encrypt + send */
                    crypto_set_iv(nonce, iv_counter);
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
    /* mark that our sector flags aren't reliable in testing mode */
    st = IMG_STATE_FINAL_FLAGS;
    wolfBoot_set_partition_state(PART_UPDATE, st);
    /* mark boot partition as testing */
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_BOOT, st);
out:
    wb_flash_erase(swap, 0, WOLFBOOT_SECTOR_SIZE);
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();
    /* encryption key was not erased, will be erased by success */
    return ret;
}

#endif


#ifdef WOLFBOOT_ARMORED
#    pragma GCC push_options
#    pragma GCC optimize("O0")
#endif

/* Reserve space for two sectors in case of NVM_FLASH_WRITEONCE, for redundancy */
#ifndef NVM_FLASH_WRITEONCE
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE))
#else
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - (2 *WOLFBOOT_SECTOR_SIZE)))
#endif

static inline int wolfBoot_get_total_size(struct wolfBoot_image* boot,
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
    uint8_t flag, st;
    struct wolfBoot_image boot, update, swap;
    uint16_t update_type;
    uint32_t fw_size;
#if defined(DISABLE_BACKUP) && defined(EXT_ENCRYPTED)
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
#endif
#ifdef DELTA_UPDATES
    int inverse = 0;
    int inverse_resume = 0;
    uint32_t cur_v;
    uint32_t up_v;
#endif

    /* No Safety check on open: we might be in the middle of a broken update */
    wolfBoot_open_image(&update, PART_UPDATE);
    wolfBoot_open_image(&boot, PART_BOOT);
    wolfBoot_open_image(&swap, PART_SWAP);

    /* get total size */
    total_size = wolfBoot_get_total_size(&boot, &update);

    if (total_size <= IMAGE_HEADER_SIZE)
        return -1;
    /* In case this is a new update, do the required
     * checks on the firmware update
     * before starting the swap
     */

    update_type = wolfBoot_get_image_type(PART_UPDATE);

    /* Check the first sector to detect interrupted update */
    if ((wolfBoot_get_update_sector_flag(0, &flag) < 0) ||
            (flag == SECT_FLAG_NEW))
    {
        if (((update_type & 0x000F) != HDR_IMG_TYPE_APP) ||
                ((update_type & 0xFF00) != HDR_IMG_TYPE_AUTH))
            return -1;
        if (update.fw_size > MAX_UPDATE_SIZE - 1)
            return -1;
        if (!update.hdr_ok || (wolfBoot_verify_integrity(&update) < 0)
                || (wolfBoot_verify_authenticity(&update) < 0)) {
            return -1;
        }
        PART_SANITY_CHECK(&update);
#ifndef ALLOW_DOWNGRADE
        if ( ((fallback_allowed==1) &&
                    (~(uint32_t)fallback_allowed == 0xFFFFFFFE)) ||
                (wolfBoot_current_firmware_version() <
                 wolfBoot_update_firmware_version()) ) {
            VERIFY_VERSION_ALLOWED(fallback_allowed);
        } else
            return -1;
#endif
    }


#ifdef DELTA_UPDATES
    if ((update_type & 0x00F0) == HDR_IMG_TYPE_DIFF) {
        cur_v = wolfBoot_current_firmware_version();
        up_v = wolfBoot_update_firmware_version();
        inverse = cur_v >= up_v;

        /* if the first sector flag is not new but we are updating then */
        /* we were interrupted */
        if (flag != SECT_FLAG_NEW &&
            wolfBoot_get_partition_state(PART_UPDATE, &st) == 0 &&
            st == IMG_STATE_UPDATING) {
            if (cur_v == up_v) {
                inverse = 0;
            }
            else if (cur_v < up_v) {
                inverse = 1;
                inverse_resume = 1;
            }
        }

        return wolfBoot_delta_update(&boot, &update, &swap, inverse,
            inverse_resume);
    }
#endif

    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
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

            if (total_size <= IMAGE_HEADER_SIZE)
                return -1;
        }
    }
    /* erase to the last sector, writeonce has 2 sectors */
    while((sector * sector_size) < WOLFBOOT_PARTITION_SIZE -
        sector_size
#ifdef NVM_FLASH_WRITEONCE
        * 2
#endif
    ) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        wb_flash_erase(&update, sector * sector_size, sector_size);
        sector++;
    }
    /* mark that our sector flags aren't reliable in testing mode */
    st = IMG_STATE_FINAL_FLAGS;
    wolfBoot_set_partition_state(PART_UPDATE, st);
    /* mark boot partition as testing */
    st = IMG_STATE_TESTING;
    wolfBoot_set_partition_state(PART_BOOT, st);
    wb_flash_erase(&swap, 0, WOLFBOOT_SECTOR_SIZE);
    /* encryption key was not erased, will be erased by success */
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();
#else /* DISABLE_BACKUP */
#ifdef EXT_ENCRYPTED
    wolfBoot_get_encrypt_key(key, nonce);
#endif

    /* Directly copy the content of the UPDATE partition into the BOOT partition.
     */
    while ((sector * sector_size) < total_size) {
        wolfBoot_copy_sector(&update, &boot, sector);
        sector++;
    }
    while((sector * sector_size) < WOLFBOOT_PARTITION_SIZE) {
        wb_flash_erase(&boot, sector * sector_size, sector_size);
        sector++;
    }
    st = IMG_STATE_SUCCESS;
    wolfBoot_set_partition_state(PART_BOOT, st);
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

/* Save the encryption key after swapping */
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key(key, nonce);
#endif

#endif /* DISABLE_BACKUP */
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

    TPM2_ForceZero(secret, sizeof(secretSz));
    return ret;
}
#endif


void RAMFUNCTION wolfBoot_start(void)
{
    int bootRet;
    int updateRet;
    uint8_t bootState;
    uint8_t updateState;
    struct wolfBoot_image boot;

#if defined(ARCH_SIM) && defined(WOLFBOOT_TPM) && defined(WOLFBOOT_TPM_SEAL)
    wolfBoot_unlock_disk();
#endif

#ifdef RAM_CODE
    wolfBoot_check_self_update();
#endif

    bootRet = wolfBoot_get_partition_state(PART_BOOT, &bootState);
    updateRet = wolfBoot_get_partition_state(PART_UPDATE, &updateState);

    /* Check if the BOOT partition is still in TESTING,
     * to trigger fallback.
     */
    if (bootRet == 0 && bootState == IMG_STATE_TESTING) {
        /* wolfBoot_update_trigger now erases all the sector flags, only trigger
         * if we're not already updating */
        if (updateRet || updateState != IMG_STATE_UPDATING) {
            wolfBoot_update_trigger();
        }
        wolfBoot_update(1);
    /* Check for new updates in the UPDATE partition or if we were
     * interrupted during the flags setting */
    } else if (updateRet == 0 && (updateState == IMG_STATE_UPDATING || updateState == IMG_STATE_FINAL_FLAGS)) {
        /* Check for new updates in the UPDATE partition */
        wolfBoot_update(0);
    }
    if ((wolfBoot_open_image(&boot, PART_BOOT) < 0)
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
                    (wolfBoot_verify_authenticity(&boot) < 0)))) {
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
#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_deinit();
#endif

#ifdef SECURE_PKCS11
    WP11_Library_Init();
#endif

    hal_prepare_boot();
    do_boot((void *)boot.fw_base);
}
#ifdef WOLFBOOT_ARMORED
#    pragma GCC pop_options
#endif
