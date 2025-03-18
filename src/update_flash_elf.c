/* update_flash_elf.c
 *
 * Implementation for ELF based updater with XIP support
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 *
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

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#include "string.h"
#ifdef WOLFBOOT_ELF
#include "elf.h"
#endif

/* Define the additional state for ELF loading */
#ifndef IMG_STATE_ELF_LOADING
#define IMG_STATE_ELF_LOADING 0x70
#endif

/* Custom TLV type for scattered hash */
#define HDR_SCATTERED_HASH  0x0040

/* Forward declarations */
static int wolfBoot_elf_load_segments(struct wolfBoot_image *img);
static int wolfBoot_update_elf(int fallback_allowed);
static int wolfBoot_compute_scattered_hash(struct wolfBoot_image *img, uint8_t *hash);
static int wolfBoot_verify_scattered_hash(struct wolfBoot_image *img);

#ifdef WOLFBOOT_ELF

/**
 * @brief Load ELF segments to their runtime memory addresses in flash
 *
 * @param img Pointer to the wolfBoot image
 * @return 0 on success, negative value on error
 */
static int wolfBoot_elf_load_segments(struct wolfBoot_image *img)
{
    elf32_header* h32 = (elf32_header*)(img->fw_base);
    elf64_header* h64 = (elf64_header*)(img->fw_base);
    uint16_t entry_count, entry_size;
    uint8_t *entry_off;
    int is_elf32, is_le, i;
    int ret = 0;

#ifdef DEBUG_ELF
    wolfBoot_printf("Loading ELF segments to XIP flash from %p\r\n", (void*)(img->fw_base));
#endif

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

    /* programs */
    entry_off = (uint8_t*)(img->fw_base) + 
        (is_elf32 ? GET32(h32->ph_offset) : GET32(h64->ph_offset));
    entry_size = (is_elf32 ? GET16(h32->ph_entry_size) : GET16(h64->ph_entry_size));
    entry_count = (is_elf32 ? GET16(h32->ph_entry_count) : GET16(h64->ph_entry_count));

#ifdef DEBUG_ELF
    wolfBoot_printf("Program Headers %d (size %d)\r\n", entry_count, entry_size);
#endif

    for (i = 0; i < entry_count; i++) {
        uint8_t *ptr = ((uint8_t*)entry_off) + (i * entry_size);
        elf32_program_header* e32 = (elf32_program_header*)ptr;
        elf64_program_header* e64 = (elf64_program_header*)ptr;
        uint32_t type = (is_elf32 ? GET32(e32->type) : GET32(e64->type));
        uintptr_t paddr = (is_elf32 ? GET32(e32->paddr) : GET64(e64->paddr));
        uintptr_t vaddr = (is_elf32 ? GET32(e32->vaddr) : GET64(e64->vaddr));
        uintptr_t mem_size = (is_elf32 ? GET32(e32->mem_size) : GET64(e64->mem_size));
        uintptr_t offset = (is_elf32 ? GET32(e32->offset) : GET64(e64->offset));
        uintptr_t file_size = (is_elf32 ? GET32(e32->file_size) : GET64(e64->file_size));

        if (type != ELF_PT_LOAD || mem_size == 0) {
            continue;
        }

#ifdef DEBUG_ELF
        if (file_size > 0) {
            wolfBoot_printf(
                "Load %u bytes (offset %p) to %p (p %p)\r\n",
                (uint32_t)mem_size, (void*)offset, (void*)vaddr, (void*)paddr);
        }
        if (mem_size > file_size) {
            wolfBoot_printf(
                "Clear %u bytes at %p (p %p)\r\n",
                (uint32_t)(mem_size - file_size), (void*)vaddr, (void*)paddr);
        }
#endif

        /* Use physical address for XIP */
        if (file_size > 0) {
            /* We need to unlock flash before writing */
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif
            /* Erase the target flash area before writing */
            if (paddr >= WOLFBOOT_PARTITION_BOOT_ADDRESS && 
                paddr + mem_size <= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) {
                /* Target is in internal flash */
                hal_flash_erase(paddr, mem_size);
                /* Copy the segment data to flash */
                if (hal_flash_write(paddr, (void*)(img->fw_base + offset), file_size) < 0) {
                    ret = -3;
                }
            }
#ifdef EXT_FLASH
            else if (paddr >= WOLFBOOT_PARTITION_UPDATE_ADDRESS && 
                     paddr + mem_size <= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE) {
                /* Target is in external flash */
                ext_flash_erase(paddr, mem_size);
                /* Copy the segment data to external flash */
                if (ext_flash_write(paddr, (void*)(img->fw_base + offset), file_size) < 0) {
                    ret = -3;
                }
            }
#endif
            else {
                /* Target is outside of defined flash partitions */
                ret = -4;
            }

            /* If mem_size > file_size, we need to zero out the rest */
            if (mem_size > file_size && ret == 0) {
                uint8_t zero_buf[64];
                uint32_t to_clear = mem_size - file_size;
                uint32_t chunk, pos = 0;
                
                /* Initialize zero buffer */
                memset(zero_buf, 0, sizeof(zero_buf));
                
                /* Zero out remainder in chunks */
                while (to_clear > 0) {
                    chunk = (to_clear > sizeof(zero_buf)) ? sizeof(zero_buf) : to_clear;
                    if (paddr >= WOLFBOOT_PARTITION_BOOT_ADDRESS && 
                        paddr + mem_size <= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE) {
                        if (hal_flash_write(paddr + file_size + pos, zero_buf, chunk) < 0) {
                            ret = -5;
                            break;
                        }
                    }
#ifdef EXT_FLASH
                    else {
                        if (ext_flash_write(paddr + file_size + pos, zero_buf, chunk) < 0) {
                            ret = -5;
                            break;
                        }
                    }
#endif
                    pos += chunk;
                    to_clear -= chunk;
                }
            }

            /* Lock flash after writing */
#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
            
            if (ret != 0) {
                return ret;
            }
        }

#ifdef ARCH_PPC
        flush_cache(paddr, mem_size);
#endif
    }

    return ret;
}

/**
 * @brief Compute the scattered hash by hashing PT_LOAD segments at their XIP addresses
 *
 * @param img Pointer to the wolfBoot image
 * @param hash Buffer to store the computed hash (must be at least SHA256_DIGEST_SIZE bytes)
 * @return 0 on success, negative value on error
 */
static int wolfBoot_compute_scattered_hash(struct wolfBoot_image *img, uint8_t *hash)
{
    elf32_header* h32 = (elf32_header*)(img->fw_base);
    elf64_header* h64 = (elf64_header*)(img->fw_base);
    uint16_t entry_count, entry_size;
    uint8_t *entry_off;
    int is_elf32, is_le, i;
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256 sha256_ctx;
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384 sha384_ctx;
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3 sha3_384_ctx;
#endif

    /* Verify ELF header */
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) != 0) {
        return -1; /* not valid header identifier */
    }

    /* Load class and endianess */
    is_elf32 = (h32->ident[4] == ELF_CLASS_32);
    is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);
    (void)is_le;

    /* Initialize hash context */
#if defined(WOLFBOOT_HASH_SHA256)
    wc_InitSha256(&sha256_ctx);
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_InitSha384(&sha384_ctx);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_InitSha3_384(&sha3_384_ctx, NULL, INVALID_DEVID);
#endif

    /* Get program headers */
    entry_off = (uint8_t*)(img->fw_base) + 
        (is_elf32 ? GET32(h32->ph_offset) : GET32(h64->ph_offset));
    entry_size = (is_elf32 ? GET16(h32->ph_entry_size) : GET16(h64->ph_entry_size));
    entry_count = (is_elf32 ? GET16(h32->ph_entry_count) : GET16(h64->ph_entry_count));

    /* Sort loadable segments by physical address */
    /* Simple bubble sort - ELF files typically have few program headers */
    typedef struct {
        uint32_t type;
        uintptr_t paddr;
        uintptr_t file_size;
    } segment_info;
    
    segment_info segments[32]; /* Assuming max 32 segments, increase if needed */
    int num_loadable = 0;
    
    /* Collect loadable segments */
    for (i = 0; i < entry_count && num_loadable < 32; i++) {
        uint8_t *ptr = ((uint8_t*)entry_off) + (i * entry_size);
        elf32_program_header* e32 = (elf32_program_header*)ptr;
        elf64_program_header* e64 = (elf64_program_header*)ptr;
        uint32_t type = (is_elf32 ? GET32(e32->type) : GET32(e64->type));
        uintptr_t paddr = (is_elf32 ? GET32(e32->paddr) : GET64(e64->paddr));
        uintptr_t file_size = (is_elf32 ? GET32(e32->file_size) : GET64(e64->file_size));
        
        if (type == ELF_PT_LOAD && file_size > 0) {
            segments[num_loadable].type = type;
            segments[num_loadable].paddr = paddr;
            segments[num_loadable].file_size = file_size;
            num_loadable++;
        }
    }
    
    /* Sort segments by physical address (ascending) */
    for (i = 0; i < num_loadable - 1; i++) {
        int j;
        for (j = 0; j < num_loadable - i - 1; j++) {
            if (segments[j].paddr > segments[j + 1].paddr) {
                segment_info temp = segments[j];
                segments[j] = segments[j + 1];
                segments[j + 1] = temp;
            }
        }
    }
    
    /* Hash segments in order of physical address */
    for (i = 0; i < num_loadable; i++) {
        /* Hash the segment data from physical address */
#if defined(WOLFBOOT_HASH_SHA256)
        wc_Sha256Update(&sha256_ctx, (uint8_t*)segments[i].paddr, segments[i].file_size);
#elif defined(WOLFBOOT_HASH_SHA384)
        wc_Sha384Update(&sha384_ctx, (uint8_t*)segments[i].paddr, segments[i].file_size);
#elif defined(WOLFBOOT_HASH_SHA3_384)
        wc_Sha3_384_Update(&sha3_384_ctx, (uint8_t*)segments[i].paddr, segments[i].file_size);
#endif
    }

    /* Finalize hash */
#if defined(WOLFBOOT_HASH_SHA256)
    wc_Sha256Final(&sha256_ctx, hash);
#elif defined(WOLFBOOT_HASH_SHA384)
    wc_Sha384Final(&sha384_ctx, hash);
#elif defined(WOLFBOOT_HASH_SHA3_384)
    wc_Sha3_384_Final(&sha3_384_ctx, hash);
#endif

    return 0;
}

/**
 * @brief Verify that the scattered hash matches the one stored in the image header
 *
 * @param img Pointer to the wolfBoot image
 * @return 0 on success, negative value on error
 */
static int wolfBoot_verify_scattered_hash(struct wolfBoot_image *img)
{
    int ret;
    uint8_t computed_hash[SHA256_DIGEST_SIZE];
    uint8_t *stored_hash;
    uint16_t stored_hash_len;

    /* Compute scattered hash */
    ret = wolfBoot_compute_scattered_hash(img, computed_hash);
    if (ret != 0) {
        return ret;
    }

    /* Get stored scattered hash from header */
    stored_hash_len = wolfBoot_get_header(img, HDR_SCATTERED_HASH, &stored_hash);
    if (stored_hash_len != SHA256_DIGEST_SIZE) {
        return -1; /* Scattered hash not found or invalid size */
    }

    /* Compare hashes */
    if (memcmp(computed_hash, stored_hash, SHA256_DIGEST_SIZE) != 0) {
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
static int is_elf_image(struct wolfBoot_image *img)
{
    elf32_header* h32 = (elf32_header*)(img->fw_base);
    
    if (memcmp(h32->ident, ELF_IDENT_STR, 4) == 0) {
        return 1;
    }
    return 0;
}

/**
 * @brief Update function for ELF images with XIP support
 *
 * @param fallback_allowed Whether fallback is allowed if update fails
 * @return 0 on success, negative value on error
 */
static int wolfBoot_update_elf(int fallback_allowed)
{
    int ret = -1;
    uint8_t st;
    struct wolfBoot_image boot, update, swap;

    memset(&boot, 0, sizeof(struct wolfBoot_image));
    memset(&update, 0, sizeof(struct wolfBoot_image));
    memset(&swap, 0, sizeof(struct wolfBoot_image));

    /* Determine the update state */
    if (wolfBoot_get_partition_state(PART_UPDATE, &st) != 0) {
        return -1;
    }

    /* We only proceed if the update partition is marked for update */
    if (st != IMG_STATE_UPDATING && st != IMG_STATE_ELF_LOADING) {
        return -2;
    }

    /* Open update partition */
    if (wolfBoot_open_image(&update, PART_UPDATE) != 0) {
        return -3;
    }

    /* Check if this is an ELF image */
    uint16_t update_type = wolfBoot_get_image_type(PART_UPDATE);
    if (!(update_type & HDR_IMG_TYPE_ELF)) {
        /* Not an ELF image, use standard update */
        return -4;
    }

    if (st == IMG_STATE_ELF_LOADING) {
        /* Resume ELF loading */
#ifdef DEBUG_ELF
        wolfBoot_printf("Resuming ELF loading\r\n");
#endif
        /* Open boot partition */
        if (wolfBoot_open_image(&boot, PART_BOOT) != 0) {
            return -5;
        }

        /* Load ELF segments to their XIP addresses */
        ret = wolfBoot_elf_load_segments(&boot);
        if (ret != 0) {
            return ret;
        }

        /* Verify scattered hash */
        ret = wolfBoot_verify_scattered_hash(&boot);
        if (ret != 0) {
            /* If verification fails, rollback if possible */
            if (fallback_allowed) {
                hal_flash_unlock();
#ifdef EXT_FLASH
                ext_flash_unlock();
#endif
                wolfBoot_erase_partition(PART_BOOT);
                wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_TESTING);
#ifdef EXT_FLASH
                ext_flash_lock();
#endif
                hal_flash_lock();
                return -6;
            }
            return ret;
        }

        /* ELF loading complete, set state to TESTING */
        hal_flash_unlock();
#ifdef EXT_FLASH
        ext_flash_unlock();
#endif
        wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
#ifdef EXT_FLASH
        ext_flash_lock();
#endif
        hal_flash_lock();
        return 0;
    }

    /* 
     * Normal update flow:
     * 1. Verify linear hash of update image (done by wolfBoot_open_image)
     * 2. Perform image swap (done by wolfBoot_swap_and_final_erase)
     * 3. Scatter load segments
     * 4. Verify scattered hash
     */

    /* Perform the standard image swap first (linear hash already verified by wolfBoot_open_image) */
    ret = wolfBoot_swap_and_final_erase(0);
    if (ret != 0) {
        return ret;
    }

    /* At this point, the standard image swap is complete. Now we need to load the ELF segments */
    /* Open boot partition */
    if (wolfBoot_open_image(&boot, PART_BOOT) != 0) {
        return -8;
    }

    /* Set state to ELF_LOADING to indicate we're in the ELF loading phase */
    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_ELF_LOADING);
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

    /* Load ELF segments to their XIP addresses */
    ret = wolfBoot_elf_load_segments(&boot);
    if (ret != 0) {
        /* If loading fails and we can fallback, rollback to previous image */
        if (fallback_allowed) {
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif
            wolfBoot_erase_partition(PART_BOOT);
            wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_TESTING);
#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
        }
        return ret;
    }

    /* Verify scattered hash */
    ret = wolfBoot_verify_scattered_hash(&boot);
    if (ret != 0) {
        /* If verification fails, rollback if possible */
        if (fallback_allowed) {
            hal_flash_unlock();
#ifdef EXT_FLASH
            ext_flash_unlock();
#endif
            wolfBoot_erase_partition(PART_BOOT);
            wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_TESTING);
#ifdef EXT_FLASH
            ext_flash_lock();
#endif
            hal_flash_lock();
        }
        return ret;
    }

    /* ELF loading complete, set state to TESTING */
    hal_flash_unlock();
#ifdef EXT_FLASH
    ext_flash_unlock();
#endif
    wolfBoot_set_partition_state(PART_BOOT, IMG_STATE_TESTING);
#ifdef EXT_FLASH
    ext_flash_lock();
#endif
    hal_flash_lock();

    return 0;
}

/**
 * @brief Entry point for ELF-aware update process
 *
 * @param fallback_allowed Whether fallback is allowed if update fails
 * @return 0 on success, negative value on error
 */
int wolfBoot_update_elf_handler(int fallback_allowed)
{
    uint8_t st;
    uint16_t update_type;
    struct wolfBoot_image update;
    
    /* Check if update is needed */
    if (wolfBoot_get_partition_state(PART_UPDATE, &st) != 0) {
        return 0; /* No update needed */
    }
    
    if (st != IMG_STATE_UPDATING && st != IMG_STATE_ELF_LOADING) {
        return 0; /* No update needed */
    }
    
    /* Open update image */
    if (wolfBoot_open_image(&update, PART_UPDATE) != 0) {
        return -1;
    }
    
    /* Check if this is an ELF image */
    update_type = wolfBoot_get_image_type(PART_UPDATE);
    if (update_type & HDR_IMG_TYPE_ELF) {
        return wolfBoot_update_elf(fallback_allowed);
    } else {
        /* Not an ELF image, use standard update */
        return wolfBoot_update(fallback_allowed);
    }
}

/**
 * @brief Verify integrity of an ELF image at boot time
 * 
 * This function first verifies the linear hash using standard wolfBoot
 * verification, then verifies the scattered hash for ELF files
 *
 * @param img Pointer to the wolfBoot image
 * @return 0 on success, negative value on error
 */
int wolfBoot_verify_elf_integrity(struct wolfBoot_image *img)
{
    int ret;
    
    /* First verify linear hash (standard wolfBoot verification) */
    ret = wolfBoot_verify_integrity(img);
    if (ret != 0) {
        return ret;
    }
    
    /* For ELF images, also verify the scattered hash */
    if (is_elf_image(img)) {
        ret = wolfBoot_verify_scattered_hash(img);
        if (ret != 0) {
            return -10 - ret; /* Use a different error range to distinguish */
        }
    }
    
    return 0;
}

/**
 * @brief ELF-aware function to check and handle updates
 */
void wolfBoot_check_elf_updates(void)
{
    uint8_t st;
    
    /* Check if boot partition is in ELF_LOADING state */
    if (wolfBoot_get_partition_state(PART_BOOT, &st) == 0 && 
        st == IMG_STATE_ELF_LOADING) {
        
        /* Resume ELF loading */
        wolfBoot_update_elf_handler(1);
        return;
    }
    
    /* Check if update is needed */
    if (wolfBoot_get_partition_state(PART_UPDATE, &st) == 0 && 
        (st == IMG_STATE_UPDATING || st == IMG_STATE_ELF_LOADING)) {
        
        /* Handle update with ELF awareness */
        wolfBoot_update_elf_handler(1);
        return;
    }
}

/**
 * @brief Hook function for bootloader to verify ELF image integrity
 * 
 * This function is to be called during boot to verify both linear and
 * scattered hash of the ELF image in the boot partition
 *
 * @return 0 on success, negative value on error
 */
int wolfBoot_verify_elf_boot_image(void)
{
    int ret;
    struct wolfBoot_image boot;
    uint16_t boot_type;
    
    /* Open boot partition */
    if (wolfBoot_open_image(&boot, PART_BOOT) != 0) {
        return -1;
    }
    
    /* Check if this is an ELF image */
    boot_type = wolfBoot_get_image_type(PART_BOOT);
    if (!(boot_type & HDR_IMG_TYPE_ELF)) {
        /* Not an ELF image, rely on standard verification already done */
        return 0;
    }
    
    /* Verify scattered hash */
    ret = wolfBoot_verify_scattered_hash(&boot);
    if (ret != 0) {
        /* Verification failed, report error */
        return ret;
    }
    
    return 0;
}

/**
 * @brief Entry point for ELF-aware bootloader
 * 
 * This function is called during boot to handle ELF image verification and loading
 * It follows a similar flow to the standard wolfBoot_start but adds ELF-specific handling
 */
void wolfBoot_start(void)
{
    int ret;
    struct wolfBoot_image boot_image;
    uint32_t *load_address = NULL;
    uint32_t *source_address = NULL;
    uint16_t image_type;
    uint8_t p_state;

    memset(&boot_image, 0, sizeof(struct wolfBoot_image));
    
    /* First, check and handle any pending updates */
    wolfBoot_check_elf_updates();

    /* Open the boot image */
    ret = wolfBoot_open_image(&boot_image, PART_BOOT);

    /* Verify integrity and authenticity */
    if (ret < 0 || 
        (ret = wolfBoot_verify_integrity(&boot_image)) < 0 ||
        (ret = wolfBoot_verify_authenticity(&boot_image)) < 0) {
        
        wolfBoot_printf("Verification failed: Part %d, Hdr %d, Hash %d, Sig %d\n", 
            active, boot_image.hdr_ok, boot_image.sha_ok, boot_image.signature_ok);
            
        /* If fallback is possible, try the other partition */
        if (wolfBoot_fallback_is_possible()) {
            active ^= 1;
            wolfBoot_update(1); /* Allow fallback */
            wolfBoot_start_elf(); /* Recursively try again */
            return;
        }
        
        /* No fallback possible, panic */
        wolfBoot_printf("No fallback possible, panic!\n");
        wolfBoot_panic();
        return;
    }

    /* Get image type to check if this is an ELF image */
    image_type = wolfBoot_get_image_type(active);
    
    if (image_type & HDR_IMG_TYPE_ELF) {
        /* This is an ELF image - verify scattered hash */
        ret = wolfBoot_verify_scattered_hash(&boot_image);
        if (ret != 0) {
            wolfBoot_printf("ELF scattered hash verification failed: %d\n", ret);
            
            /* If fallback is possible, try the other partition */
            if (wolfBoot_fallback_is_possible()) {
                active ^= 1;
                wolfBoot_update(1); /* Allow fallback */
                wolfBoot_start_elf(); /* Recursively try again */
                return;
            }
            
            /* No fallback possible, panic */
            wolfBoot_printf("No fallback possible, panic!\n");
            wolfBoot_panic();
            return;
        }
        
        /* For ELF images, use the entry point from the ELF header */
        elf32_header* h32 = (elf32_header*)(boot_image.fw_base);
        elf64_header* h64 = (elf64_header*)(boot_image.fw_base);
        int is_elf32 = (h32->ident[4] == ELF_CLASS_32);
        int is_le = (h32->ident[5] == ELF_ENDIAN_LITTLE);
        uintptr_t entry_point = GET_H64(entry);
        
        wolfBoot_printf("ELF Entry point: %p\n", (void*)entry_point);
        load_address = (uint32_t*)entry_point;
    } else {
        /* Not an ELF image, use standard load address */
#ifdef WOLFBOOT_LOAD_ADDRESS
        load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
#elif !defined(NO_XIP)
        load_address = (uint32_t*)boot_image.fw_base;
#else
        #error missing WOLFBOOT_LOAD_ADDRESS or XIP
#endif
    }

    /* First time we boot this update, set to TESTING to await
     * confirmation from the system */
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if ((wolfBoot_get_partition_state(active, &p_state) == 0) &&
        (p_state == IMG_STATE_UPDATING))
    {
#ifdef EXT_FLASH
        ext_flash_unlock();
#else
        hal_flash_unlock();
#endif
        wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
#ifdef EXT_FLASH
        ext_flash_lock();
#else
        hal_flash_lock();
#endif
    }
#endif

    wolfBoot_printf("Firmware verification complete\n");
    wolfBoot_printf("Booting at %p\n", load_address);

    /* Prepare for boot */
    hal_prepare_boot();

    /* Boot the firmware */
#ifdef MMU
    uint8_t *dts_addr = hal_get_dts_address();
    uint32_t dts_size = 0;
    
    if (dts_addr) {
        dts_size = wolfBoot_get_dts_size(dts_addr);
        if (dts_size > 0) {
            /* Relocate DTS to RAM if needed */
            uint8_t* dts_dst = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
            wolfBoot_printf("Loading DTB (size %d) from %p to RAM at %p\n",
                dts_size, dts_addr, WOLFBOOT_LOAD_DTS_ADDRESS);
            memcpy(dts_dst, dts_addr, dts_size);
            dts_addr = dts_dst;
        }
    }
    
    do_boot(load_address, (uint32_t*)dts_addr);
#else
    do_boot(load_address);
#endif
}

#endif /* WOLFBOOT_ELF */ 