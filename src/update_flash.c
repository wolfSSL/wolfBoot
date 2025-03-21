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

#ifdef WOLFBOOT_ELF
#include "elf.h"
#ifdef WOLFBOOT_HASH_SHA256
#include "wolfssl/wolfcrypt/sha256.h"
#endif
#ifdef WOLFBOOT_HASH_SHA384
#include "wolfssl/wolfcrypt/sha384.h"
#endif
#ifdef WOLFBOOT_HASH_SHA3_384
#include "wolfssl/wolfcrypt/sha3_384.h"
#endif
#endif /* WOLFBOOT_ELF */

#ifdef WOLFBOOT_ELF
/**
 * --------------------------------------------------------------------------------
 * ELF XIP Update Scheme Overview
 * --------------------------------------------------------------------------------
 * This module implements a secure update mechanism for ELF files that can be
 * executed in place (XIP) from flash. The implementation uses standard wolfBoot
 * signature verification plus an additional scattered hash verification:
 *
 * 1. Standard wolfBoot Signature: Used to verify the authenticity and integrity
 *    of the entire ELF image as stored in the partition
 *    - Leverages existing wolfBoot signature verification mechanism
 *    - Verifies the entire image during update and at boot time
 *
 * 2. Scattered Hash: A hash of all PT_LOAD segments in their XIP memory locations
 *    - Computed by hashing loadable segments in ascending physical address order
 *    - Stored in a custom TLV in the wolfBoot image header, which is covered by
 *      the image signature, thus guaranteeing its authenticity
 *    - Verifies that segments loaded to their XIP addresses match the original
 *      contents of the ELF file
 *
 * Update Process:
 * 1. Standard wolfBoot verification of the stored elf file in the update partition
 * 2. Perform the standard three-way interruptible partition swap (update -> boot)
 * 3. Set boot partition state to IMG_STATE_ELF_LOADING
 * 4. Parse ELF headers from the boot partition and load each PT_LOAD segment to its XIP address
 * 5. Compute scattered hash of loaded segments and verify against the authenticated
 *    scattered hash TLV from the image header
 * 6. If process is interrupted during scatter loading/verification, the scatter load from the
 *    boot partition is restarted
 * 7. If verification succeeds, set boot partition to IMG_STATE_TESTING, extract entry point from
 *    ELF header and boot
 * 8. If verification fails, the boot partition is rolled back to the previous state (update)
 *    and the update process is restarted
 *
 * Boot Process:
 * 1. Standard wolfBoot verification of the boot image signature
 * 2. Additionally verify the scattered hash by hashing PT_LOAD
 *    segments in their XIP locations and comparing with the authenticated hash
 *    from the image header
 * 3. If verification succeeds, extract entry point from ELF header and boot
 * 4. If verification fails, the boot partition is rolled back to the previous state (update)
 *    and the new boot partition is scatter loaded and verified
 *
 * The update process is failsafe and interruptible. If power is lost during
 * ELF loading, the system can resume from where it left off (or close to it) on next boot.
 */

/**
 * @brief Load ELF segments to their runtime memory addresses in flash
 *
 * @param img Pointer to the wolfBoot image
 * @return 0 on success, negative value on error
 */
static int wolfBoot_elf_load_segments(struct wolfBoot_image* img)
{
    uint8_t elf_header_buf[sizeof(elf64_header)];
    uint8_t program_header_buf[sizeof(elf64_program_header)];
    elf32_header* h32;
    elf64_header* h64;
    uint16_t entry_count, entry_size;
    uint32_t ph_offset;
    int is_elf32, is_le, i;
    int ret = 0;

#ifdef DEBUG_ELF
    wolfBoot_printf("Loading ELF segments to XIP flash from %p\r\n",
                    (void*)(img->fw_base));
#endif

#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        /* Read ELF header from external flash */
        ext_flash_check_read((uintptr_t)(img->fw_base), elf_header_buf, sizeof(elf64_header));
    } else {
        memcpy(elf_header_buf, (void*)(img->fw_base), sizeof(elf64_header));
    }
#else
    memcpy(elf_header_buf, (void*)(img->fw_base), sizeof(elf64_header));
#endif

    h32 = (elf32_header*)elf_header_buf;
    h64 = (elf64_header*)elf_header_buf;

    /* Verify ELF header */
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    /* Load class and endianess */
    is_elf32 = (h32->ident[4] == ELF_CLASS_32);
    is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);
    (void)is_le;

    /* Verify this is an executable */
    if ((is_elf32 ? GET16(h32->type) : GET16(h64->type)) != ELF_HET_EXEC) {
        return -2; /* not executable */
    }

#ifdef DEBUG_ELF
    wolfBoot_printf("Found valid elf%d (%s endian) for XIP loading\r\n",
                    is_elf32 ? 32 : 64, is_le ? "little" : "big");
#endif

    /* Get program headers info */
    ph_offset = is_elf32 ? GET32(h32->ph_offset) : GET32(h64->ph_offset);
    entry_size = is_elf32 ? GET16(h32->ph_entry_size) : GET16(h64->ph_entry_size);
    entry_count = is_elf32 ? GET16(h32->ph_entry_count) : GET16(h64->ph_entry_count);

#ifdef DEBUG_ELF
    wolfBoot_printf("Program Headers %d (size %d)\r\n", entry_count,
                    entry_size);
#endif

    /* We need to unlock flash before writing */
    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif

    for (i = 0; i < entry_count; i++) {
        uint32_t type;
        uintptr_t paddr, vaddr, mem_size, offset, file_size;
        
        /* Read program header */
#ifdef EXT_FLASH
        if (PART_IS_EXT(img)) {
            ext_flash_check_read((uintptr_t)(img->fw_base + ph_offset + (i * entry_size)), 
                                program_header_buf, entry_size);
        } else
#endif
        {
            memcpy(program_header_buf, (void*)(img->fw_base + ph_offset + (i * entry_size)), entry_size);
        }

        if (is_elf32) {
            elf32_program_header* e32 = (elf32_program_header*)program_header_buf;
            type = GET32(e32->type);
            paddr = GET32(e32->paddr);
            vaddr = GET32(e32->vaddr);
            mem_size = GET32(e32->mem_size);
            offset = GET32(e32->offset);
            file_size = GET32(e32->file_size);
        } else {
            elf64_program_header* e64 = (elf64_program_header*)program_header_buf;
            type = GET32(e64->type);
            paddr = GET64(e64->paddr);
            vaddr = GET64(e64->vaddr);
            mem_size = GET64(e64->mem_size);
            offset = GET64(e64->offset);
            file_size = GET64(e64->file_size);
        }

        if (type != ELF_PT_LOAD || mem_size == 0) {
            continue;
        }

#ifdef DEBUG_ELF
        if (file_size > 0) {
            wolfBoot_printf("Load %u bytes (offset %p) to %p (p %p)\r\n",
                            (uint32_t)mem_size, (void*)offset, (void*)vaddr,
                            (void*)paddr);
        }
        if (mem_size > file_size) {
            wolfBoot_printf("Clear %u bytes at %p (p %p)\r\n",
                            (uint32_t)(mem_size - file_size), (void*)vaddr,
                            (void*)paddr);
        }
#endif

        /* Use physical address for XIP */
        if (file_size > 0) {
            uint8_t buffer[WOLFBOOT_SECTOR_SIZE];
            uint32_t pos = 0;
            uint32_t chunk_size;
            
            /* Erase the target flash area before writing */
            /* TODO: THis could erase data outside the region we want to program
             * - AURIX HAL handles read/modify/erase/write but not sure all HALs
             * do...need to do this manually here and do erases before writes */
            /* wb_flash_erase(img, paddr, mem_size); */
            
            /* Copy the segment data to flash in chunks */
            while (pos < file_size) {
                chunk_size = (file_size - pos > sizeof(buffer)) ? 
                             sizeof(buffer) : (file_size - pos);
                
#ifdef EXT_FLASH
                if (PART_IS_EXT(img)) {
                    ext_flash_check_read((uintptr_t)(img->fw_base + offset + pos), 
                                        buffer, chunk_size);
                } else
#endif
                {
                    memcpy(buffer, (void*)(img->fw_base + offset + pos), chunk_size);
                }
                
                if (wb_flash_write(img, paddr + pos, buffer, chunk_size) < 0) {
                    ret = -3;
                    break;
                }
                
                pos += chunk_size;
            }
            
            /* If mem_size > file_size, we need to zero out the rest */
            if (mem_size > file_size && ret == 0) {
                uint8_t zero_buf[64];
                uint32_t to_clear = mem_size - file_size;
                uint32_t chunk, zero_pos = 0;

                /* Initialize zero buffer */
                memset(zero_buf, 0, sizeof(zero_buf));

                /* Zero out remainder in chunks */
                while (to_clear > 0) {
                    chunk = (to_clear > sizeof(zero_buf)) ? sizeof(zero_buf) : to_clear;
                    
                    if (wb_flash_write(img, paddr + file_size + zero_pos, zero_buf, chunk) < 0) {
                        ret = -5;
                        break;
                    }
                    
                    zero_pos += chunk;
                    to_clear -= chunk;
                }
            }

            if (ret != 0) {
                break;
            }
        }

#ifdef ARCH_PPC
        flush_cache(paddr, mem_size);
#endif
    }

    /* Lock flash after writing */
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

    return ret;
}

/**
 * @brief Verify that the scattered hash matches the one stored in the image
 * header
 *
 * @param img Pointer to the wolfBoot image
 * @return 0 on success, negative value on error
 */
static int wolfBoot_verify_scattered_hash(struct wolfBoot_image* img)
{
    int      ret;
    uint8_t  computed_hash[WOLFBOOT_SHA_DIGEST_SIZE];
    uint8_t* stored_hash;
    uint16_t stored_hash_len;

    /* Compute scattered hash */
    ret = wolfBoot_compute_scattered_hash(img, computed_hash);
    if (ret != 0) {
        return ret;
    }

    /* Get stored scattered hash from header */
    stored_hash_len =
        wolfBoot_get_header(img, HDR_ELF_SCATTERED_HASH, &stored_hash);
    if (stored_hash_len != WOLFBOOT_SHA_DIGEST_SIZE) {
        return -1; /* Scattered hash not found or invalid size */
    }

    /* Compare hashes */
    if (memcmp(computed_hash, stored_hash, WOLFBOOT_SHA_DIGEST_SIZE) != 0) {
        return -2; /* Hash mismatch */
    }

    return 0; /* Success */
}

/**
 * @brief Check if an image is an ELF file
 *
 * @param img Pointer to the wolfBoot image
 * @return 1 if ELF, 0 if not
 */
static int is_elf_image(struct wolfBoot_image* img)
{
    elf32_header h32;
    
#ifdef EXT_FLASH
    if (PART_IS_EXT(img)) {
        /* Read ELF header from external flash */
        ext_flash_check_read((uintptr_t)(img->fw_base), (uint8_t*)&h32, sizeof(elf32_header));
    } else {
        memcpy(&h32, (void*)(img->fw_base), sizeof(elf32_header));
    }
#else
    memcpy(&h32, (void*)(img->fw_base), sizeof(elf32_header));
#endif

    if (memcmp(h32.ident, ELF_IDENT_STR, 4) == 0) {
        return 1;
    }
    return 0;
}


/* Scatter loads an ELF image from boot partition if it is an ELF image */
static void check_and_load_boot_elf(struct wolfBoot_image* boot)
{
    /* Check if this is an ELF image */
    if (is_elf_image(&boot)) {
        /* Set state to ELF_LOADING before starting scatter load */
        wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_ELF_LOADING);

        /* Load ELF segments to their XIP addresses */
        if (wolfBoot_elf_load_segments(&boot) != 0) {
            wolfBoot_printf("Failed to load ELF segments\n");
            wolfBoot_panic();
        }

        /* If we get here, ELF loading and verification succeeded */
        wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
    }
}

#endif /* WOLFBOOT_ELF */

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

    wolfBoot_printf("Copy sector %d (part %d->%d)\n",
        sector, src->part, dst->part);

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

#ifndef DISABLE_BACKUP

#ifdef EXT_ENCRYPTED
#   define TRAILER_OFFSET_WORDS \
        ((ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) / sizeof(uint32_t))
#else
#   define TRAILER_OFFSET_WORDS 0
#endif

/**
 * @brief Performs the final swap and erase operations during a secure update,
 * ensuring that if power is lost during the update, the process can be resumed
 * on next boot.
 *
 * This function handles the final phase of the three-way swap update process.
 * It ensures that the update is atomic and power-fail safe by:
 * 1. Saving the last sector of the boot partition to the swap area
 * 2. Setting a magic trailer value to mark the swap as in progress
 * 3. Erasing the last sector(s) of the boot partition
 * 4. Restoring the saved sector from swap back to boot
 * 5. Setting the boot partition state to TESTING
 * 6. Erasing the last sector(s) of the update partition
 *
 * The function can be called in two modes:
 * - Normal mode (resume=0): Initiates the swap and erase process
 * - Resume mode (resume=1): Checks if a swap was interrupted and completes it
 *
 * @param resume If 1, checks for interrupted swap and resumes it; if 0, starts
 * new swap
 * @return 0 on success, negative value if no swap needed or on error
 */
static int wolfBoot_swap_and_final_erase(int resume)
{
    struct wolfBoot_image boot[1];
    struct wolfBoot_image update[1];
    struct wolfBoot_image swap[1];
    uint8_t updateState;
#ifdef WOLFBOOT_ELF
    uint8_t bootState;
#endif
    int eraseLen = (WOLFBOOT_SECTOR_SIZE
#ifdef NVM_FLASH_WRITEONCE /* need to erase the redundant sector too */
        * 2
#endif
    );
    int swapDone = 0;
    uintptr_t tmpBootPos = WOLFBOOT_PARTITION_SIZE - eraseLen -
        WOLFBOOT_SECTOR_SIZE;
    uint32_t tmpBuffer[TRAILER_OFFSET_WORDS + 1];

    /* open partitions (ignore failure) */
    wolfBoot_open_image(boot, PART_BOOT);
    wolfBoot_open_image(update, PART_UPDATE);
    wolfBoot_open_image(swap, PART_SWAP);
    wolfBoot_get_partition_state(PART_UPDATE, &updateState);
#ifdef WOLFBOOT_ELF
    wolfBoot_get_partition_state(PART_BOOT, &bootState);
#endif


#ifdef WOLFBOOT_ELF
    if ((resume == 1) && (is_elf_image(boot))) {
        /* If we're resuming an interrupted elf load, we can skip the image swap
         * since we know it was already completed */
        if (bootState == IMG_STATE_ELF_LOADING) {
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif

            /* Load ELF segments to their XIP addresses */
            if (wolfBoot_elf_load_segments(&boot) != 0) {
                wolfBoot_printf("Failed to load ELF segments\n");
                wolfBoot_panic();
            }

            wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);

            /* Only after successful ELF loading, erase update partition state
             */
            if (updateState == IMG_STATE_FINAL_FLAGS) {
                wb_flash_erase(update, WOLFBOOT_PARTITION_SIZE - eraseLen,
                               eraseLen);
            }

#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
            return 0;
        }
    }
#endif


    /* read trailer */
#if defined(EXT_FLASH) && PARTN_IS_EXT(PART_BOOT)
    ext_flash_read((uintptr_t)(boot->hdr + tmpBootPos), (void*)tmpBuffer,
        sizeof(tmpBuffer));
#else
    memcpy(tmpBuffer, boot->hdr + tmpBootPos, sizeof(tmpBuffer));
#endif

    /* check for trailing magic (BOOT) */
    /* final swap and erase flag is WOLFBOOT_MAGIC_TRAIL */
    if (tmpBuffer[TRAILER_OFFSET_WORDS] == WOLFBOOT_MAGIC_TRAIL) {
        swapDone = 1;
    }
    /* if resuming, quit if swap isn't done */
    if ((resume == 1) && (swapDone == 0) &&
        (updateState != IMG_STATE_FINAL_FLAGS)
#ifdef WOLFBOOT_ELF
        && (bootState != IMG_STATE_ELF_LOADING)
#endif
    ) {
        return -1;
    }

    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif

    /* IMG_STATE_FINAL_FLAGS allows re-entry without blowing away swap */
    if (updateState != IMG_STATE_FINAL_FLAGS) {
        /* store the sector at tmpBootPos into swap */
        wolfBoot_copy_sector(boot, swap, tmpBootPos / WOLFBOOT_SECTOR_SIZE);
        /* set FINAL_SWAP for re-entry */
        wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_FINAL_FLAGS);
    }
#ifdef EXT_ENCRYPTED
    if (swapDone == 0) {
        /* get encryption key and iv if encryption is enabled */
        wolfBoot_get_encrypt_key((uint8_t*)tmpBuffer,
            (uint8_t*)&tmpBuffer[ENCRYPT_KEY_SIZE/sizeof(uint32_t)]);
        /* write TRAIL, encryption key and iv if enabled to tmpBootPos*/
        tmpBuffer[TRAILER_OFFSET_WORDS] = WOLFBOOT_MAGIC_TRAIL;

        wb_flash_erase(boot, tmpBootPos, WOLFBOOT_SECTOR_SIZE);
        wb_flash_write(boot, tmpBootPos, (void*)tmpBuffer, sizeof(tmpBuffer));
    }
#endif
    /* erase the last boot sector(s) */
    wb_flash_erase(boot, WOLFBOOT_PARTITION_SIZE - eraseLen, eraseLen);
    /* set the encryption key */
#ifdef EXT_ENCRYPTED
    wolfBoot_set_encrypt_key((uint8_t*)tmpBuffer,
            (uint8_t*)&tmpBuffer[ENCRYPT_KEY_SIZE/sizeof(uint32_t)]);
    /* wolfBoot_set_encrypt_key calls hal_flash_unlock, need to unlock again */
    hal_flash_unlock();
#endif
    /* write the original contents of tmpBootPos back */
    if (tmpBootPos < boot->fw_size + IMAGE_HEADER_SIZE) {
        wolfBoot_copy_sector(swap, boot, tmpBootPos / WOLFBOOT_SECTOR_SIZE);
    }
    else {
        wb_flash_erase(boot, tmpBootPos, WOLFBOOT_SECTOR_SIZE);
    }

#ifdef WOLFBOOT_ELF
    /* load elf file from boot partition if applicable. This sets the boot
     * partition state to loading during the load and then to testing on success
     */
    check_and_load_boot_elf(boot);
#else
    /* mark boot as TESTING */
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
#endif
    /* erase the last sector(s) of update. This resets the update partition state
     * to IMG_STATE_NEW */
    wb_flash_erase(update, WOLFBOOT_PARTITION_SIZE - eraseLen, eraseLen);

#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

    return 0;
}
#endif

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
    uint8_t flag, st;
    int hdr_size;
    uint8_t delta_blk[DELTA_BLOCK_SIZE];
    uint32_t offset = 0;
    uint16_t ptr_len;
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
        if (((cur_v == upd_v) && (delta_base_v < cur_v)) || resume) {
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
out:
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();
    /* start re-entrant final erase, return code is only for resumption in
     * wolfBoot_start */
#ifndef DISABLE_BACKUP
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

/* Reserve space for two sectors in case of NVM_FLASH_WRITEONCE, for redundancy */
#ifndef NVM_FLASH_WRITEONCE
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE))
#else
    #define MAX_UPDATE_SIZE (size_t)((WOLFBOOT_PARTITION_SIZE - (2 *WOLFBOOT_SECTOR_SIZE)))
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
#if defined(DISABLE_BACKUP) && defined(EXT_ENCRYPTED)
    uint8_t key[ENCRYPT_KEY_SIZE];
    uint8_t nonce[ENCRYPT_NONCE_SIZE];
#endif
#ifdef DELTA_UPDATES
    uint8_t st;
    int inverse = 0;
    int resume = 0;
    int stateRet = -1;
    uint32_t cur_v;
    uint32_t up_v;
#endif
    uint32_t cur_ver, upd_ver;

    wolfBoot_printf("Staring Update (fallback allowed %d)\n", fallback_allowed);

    /* No Safety check on open: we might be in the middle of a broken update */
    wolfBoot_open_image(&update, PART_UPDATE);
    wolfBoot_open_image(&boot, PART_BOOT);
    wolfBoot_open_image(&swap, PART_SWAP);

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
        if (!update.hdr_ok
                || (wolfBoot_verify_integrity(&update) < 0)
                || (wolfBoot_verify_authenticity(&update) < 0)) {
            wolfBoot_printf("Update verify failed: Hdr %d, Hash %d, Sig %d\n",
                update.hdr_ok, update.sha_ok, update.signature_ok);
            return -1;
        }
        PART_SANITY_CHECK(&update);

        cur_ver = wolfBoot_current_firmware_version();
        upd_ver = wolfBoot_update_firmware_version();

        wolfBoot_printf("Versions: Current 0x%x, Update 0x%x\n",
            cur_ver, upd_ver);

#ifndef ALLOW_DOWNGRADE
        if ( ((fallback_allowed==1) &&
                    (~(uint32_t)fallback_allowed == 0xFFFFFFFE)) ||
                (cur_ver < upd_ver) ) {
            VERIFY_VERSION_ALLOWED(fallback_allowed);
        } else {
            wolfBoot_printf("Update version not allowed\n");
            return -1;
        }
#endif
    }

#ifdef DELTA_UPDATES
    if ((update_type & 0x00F0) == HDR_IMG_TYPE_DIFF) {
        cur_v = wolfBoot_current_firmware_version();
        up_v = wolfBoot_update_firmware_version();
        inverse = cur_v >= up_v;
        /* if magic isn't set stateRet will be -1 but that means we're on a
         * fresh partition and aren't resuming */
        stateRet = wolfBoot_get_partition_state(PART_UPDATE, &st);

        /* if we've already written a sector or we've mangled the boot partition
         * header we can't determine the direction by version numbers. instead
         * use the update partition state, updating means regular, new means
         * reverting */
        if ((stateRet == 0) && ((flag != SECT_FLAG_NEW) || (cur_v == 0))) {
            resume = 1;
            if (st == IMG_STATE_UPDATING) {
                inverse = 0;
            }
            else {
                inverse = 1;
            }
        }
        /* If we're dealing with a "ping-pong" fallback that wasn't interrupted
         * we need to set to UPDATING, otherwise there's no way to tell the
         * original direction of the update once interrupted */
        else if ((inverse == 0) && (fallback_allowed == 1)) {
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif
            wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
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
                wolfBoot_copy_sector(&boot, &update, sector);
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

    /* Erase remainder of partitions */
#if defined(WOLFBOOT_FLASH_MULTI_SECTOR_ERASE) || defined(PRINTF_ENABLED)
    /* calculate number of remaining bytes */
    /* reserve 1 sector for status (2 sectors for NV write once) */
#ifdef NVM_FLASH_WRITEONCE
    size = WOLFBOOT_PARTITION_SIZE - (sector * sector_size) - (2 * sector_size);
#else
    size = WOLFBOOT_PARTITION_SIZE - (sector * sector_size) - sector_size;
#endif

    wolfBoot_printf("Erasing remainder of partitions (%d sectors)...\n",
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
    /* start re-entrant final erase, return code is only for resumption in
     * wolfBoot_start*/
    wolfBoot_swap_and_final_erase(0);

#else /* DISABLE_BACKUP */        /* Compute and verify scattered hash */
        if (wolfBoot_verify_scattered_hash(&boot) != 0) {
            wolfBoot_printf("Scattered hash verification failed\n");
            return -1;
        }
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

#ifdef WOLFBOOT_ELF
    check_and_load_boot_elf(&boot);
#endif

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

    bootRet =   wolfBoot_get_partition_state(PART_BOOT, &bootState);
    updateRet = wolfBoot_get_partition_state(PART_UPDATE, &updateState);

#ifdef NVM_FLASH_WRITEONCE
    hal_flash_lock();
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
#endif

#if !defined(DISABLE_BACKUP)
    /* resume the final erase in case the power failed before it finished */
    resumedFinalErase = wolfBoot_swap_and_final_erase(1);
    if (resumedFinalErase != 0)
#endif
    {
        /* Check if the BOOT partition is still in TESTING,
         * to trigger fallback.
         */
        if ((bootRet == 0) && (bootState == IMG_STATE_TESTING)) {
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
#ifdef WOLFBOOT_ELF
            || (is_elf_image(&boot) && wolfBoot_verify_scattered_elf(&boot) < 0)
#endif
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
#ifdef WOLFBOOT_ELF
                    || (is_elf_image(&boot) && wolfBoot_verify_scattered_elf(&boot) < 0)
#endif
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
#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_deinit();
#endif

#ifdef SECURE_PKCS11
    WP11_Library_Init();
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)hal_hsm_disconnect();
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
