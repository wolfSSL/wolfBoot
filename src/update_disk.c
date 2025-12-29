/* update_disk.c
 *
 * Implementation for RAM based updater, for systems that provide
 * drives and partition mapping.
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
/**
 * @file update_disk.c
 *
 * @brief Implementation for RAM based updater, for systems that provide
 * drives and partition mapping.
 *
 * This file contains the implementation for the RAM-based updater for systems
 * that offer drives and partition mapping. It includes functions to read
 * and load OS images from disk partitions, validate their integrity and
 * authenticity, and perform the boot process.
 */

#ifdef WOLFBOOT_UPDATE_DISK

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include "disk.h"
#ifdef WOLFBOOT_ELF
#include "elf.h"
#endif

/* Disk encryption support for AES-256, AES-128, or ChaCha20 */
#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128) || \
    defined(ENCRYPT_WITH_CHACHA)
#define DISK_ENCRYPT
#include "encrypt.h"
#endif

#include <stdint.h>
#include <string.h>

#ifdef WOLFBOOT_FSP
#include "stage2_params.h"
#include "x86/common.h"
#include "x86/ahci.h"
#include "x86/ata.h"
#include "pci.h"
#include "x86/tgl_fsp.h"

#ifdef TARGET_kontron_vx3060_s2
    #define BOOT_PART_A 5
    #define BOOT_PART_B 6
#endif
#endif /* WOLFBOOT_FSP */

/* Default values for BOOT_DISK, BOOT_PART_A and BOOT_PART_B */
#ifndef BOOT_DISK
#define BOOT_DISK 0
#endif
#ifndef BOOT_PART_A
#define BOOT_PART_A 0
#endif
#ifndef BOOT_PART_B
#define BOOT_PART_B 1
#endif

#ifndef MAX_FAILURES
#define MAX_FAILURES 4
#endif

#ifndef DISK_BLOCK_SIZE
#define DISK_BLOCK_SIZE 512
#endif

#ifdef DISK_ENCRYPT
/**
 * @brief Decrypt an image header in RAM.
 *
 * This function decrypts the image header using the configured encryption
 * algorithm (AES-256/AES-128 CTR mode or ChaCha20).
 *
 * @param src Pointer to the encrypted header.
 * @param dst Pointer to the destination buffer for decrypted header.
 *
 * @return 0 if successful, -1 on failure.
 */
static int decrypt_header(const uint8_t *src, uint8_t *dst)
{
    uint32_t i;
    uint32_t magic;

    for (i = 0; i < IMAGE_HEADER_SIZE; i += ENCRYPT_BLOCK_SIZE) {
        wolfBoot_crypto_set_iv(NULL, i / ENCRYPT_BLOCK_SIZE);
        crypto_decrypt(dst + i, src + i, ENCRYPT_BLOCK_SIZE);
    }
    magic = *((uint32_t*)dst);
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    return 0;
}

/**
 * @brief Decrypt an image in RAM.
 *
 * This function decrypts the full image (header + firmware) using the
 * configured encryption algorithm. The decryption is done in-place.
 *
 * @param data Pointer to the encrypted image data.
 * @param size Size of the image (header + firmware).
 *
 * @return 0 if successful, -1 on failure.
 */
static int decrypt_image(uint8_t *data, uint32_t size)
{
    uint32_t iv_counter = 0;
    uint32_t offset = 0;
    uint8_t dec_block[ENCRYPT_BLOCK_SIZE];

    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > ENCRYPT_BLOCK_SIZE)
            chunk = ENCRYPT_BLOCK_SIZE;

        wolfBoot_crypto_set_iv(NULL, iv_counter);
        crypto_decrypt(dec_block, data + offset, chunk);
        memcpy(data + offset, dec_block, chunk);

        offset += ENCRYPT_BLOCK_SIZE;
        iv_counter++;
    }
    return 0;
}
#endif /* DISK_ENCRYPT */

extern int wolfBoot_get_dts_size(void *dts_addr);

#if defined(WOLFBOOT_NO_LOAD_ADDRESS) || !defined(WOLFBOOT_LOAD_ADDRESS)
/* from the linker, where wolfBoot ends */
extern uint8_t _end_wb[];
#endif

/**
 * @brief function for starting the boot process.
 *
 * This function starts the boot process by attempting to read and load
 * the OS image from disk partitions. It then verifies the integrity and
 * authenticity of the loaded image before initiating the boot.
 */
void RAMFUNCTION wolfBoot_start(void)
{
    uint8_t p_hdr[IMAGE_HEADER_SIZE] XALIGNED_STACK(16);
#ifdef DISK_ENCRYPT
    uint8_t dec_hdr[IMAGE_HEADER_SIZE] XALIGNED_STACK(16);
#endif
#ifdef WOLFBOOT_FSP
    struct stage2_parameter *stage2_params;
#endif
    struct wolfBoot_image os_image;
    int pA_ver = 0, pB_ver = 0;
    uint32_t cur_part = 0;
    int ret = -1;
    int selected;
    uint32_t *load_address;
    int failures = 0;
    uint32_t load_off;
#ifdef MMU
    uint8_t *dts_addr = NULL;
    uint32_t dts_size = 0;
#endif
    char part_name[4] = {'P', ':', 'X', '\0'};
    uint64_t start_us, elapsed_ms;

#ifdef DISK_ENCRYPT
    /* Initialize encryption */
    if (wolfBoot_initialize_encryption() != 0) {
        wolfBoot_printf("Error initializing encryption\r\n");
        wolfBoot_panic();
    }
#endif

    ret = disk_init(BOOT_DISK);
    if (ret != 0) {
        wolfBoot_panic();
    }

    if (disk_open(BOOT_DISK) < 0) {
        wolfBoot_printf("Error opening disk %d\r\n", BOOT_DISK);
        wolfBoot_panic();
    }

    wolfBoot_printf("Checking primary OS image in %d,%d...\r\n", BOOT_DISK,
            BOOT_PART_A);
    if (disk_part_read(BOOT_DISK, BOOT_PART_A, 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
#ifdef DISK_ENCRYPT
        if (decrypt_header(p_hdr, dec_hdr) == 0) {
            pA_ver = wolfBoot_get_blob_version(dec_hdr);
        }
#else
        pA_ver = wolfBoot_get_blob_version((uint8_t*)p_hdr);
#endif
    }

    wolfBoot_printf("Checking secondary OS image in %d,%d...\r\n", BOOT_DISK,
            BOOT_PART_B);
    if (disk_part_read(BOOT_DISK, BOOT_PART_B, 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
#ifdef DISK_ENCRYPT
        if (decrypt_header(p_hdr, dec_hdr) == 0) {
            pB_ver = wolfBoot_get_blob_version(dec_hdr);
        }
#else
        pB_ver = wolfBoot_get_blob_version((uint8_t*)p_hdr);
#endif
    }

    if ((pB_ver == 0) && (pA_ver == 0)) {
        wolfBoot_printf("No valid OS image found in either partition %d or %d\r\n",
            BOOT_PART_A, BOOT_PART_B);
        wolfBoot_panic();
    }

    wolfBoot_printf("Versions, A:%u B:%u\r\n", pA_ver, pB_ver);

    /* Choose partition with higher version */
    selected = (pB_ver > pA_ver) ? 1: 0;

#ifdef WOLFBOOT_FSP
    stage2_params = stage2_get_parameters();
#endif

#if !defined(WOLFBOOT_NO_LOAD_ADDRESS) && defined(WOLFBOOT_LOAD_ADDRESS)
    load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
#else
    /* load the image just after wolfboot, 16 bytes aligned */
    load_address = (uint32_t *)((((uintptr_t)_end_wb) + 0xf) & ~0xf);
#endif

    wolfBoot_printf("Load address 0x%x\r\n", load_address);
    do {
        failures++;
        if (selected)
            cur_part = BOOT_PART_B;
        else
            cur_part = BOOT_PART_A;

        part_name[2] = 'A' + selected;

        wolfBoot_printf("Attempting boot from %s\r\n", part_name);

        /* Fetch header only */
        if (disk_part_read(BOOT_DISK, cur_part, 0, IMAGE_HEADER_SIZE, p_hdr)
            != IMAGE_HEADER_SIZE) {
            wolfBoot_printf("Error reading image header from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }

        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)p_hdr);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }

#ifdef WOLFBOOT_FSP
        /* Verify image size fits in low memory */
        if (os_image.fw_size > ((uint32_t)(stage2_params->tolum) -
                                           (uint32_t)(uintptr_t)load_address)) {
            wolfBoot_printf("Image size %d doesn't fit in low memory\r\n",
                os_image.fw_size);
            break;
        }
        /* Log memory load */
        x86_log_memory_load((uint32_t)(uintptr_t)load_address,
                            (uint32_t)(uintptr_t)load_address + os_image.fw_size,
                            part_name);
#endif

        /* Read the image into RAM */
        wolfBoot_printf("Loading image from disk...");
        start_us = hal_get_timer_us();
        load_off = 0;
        do {
            ret = disk_part_read(BOOT_DISK, cur_part, load_off,
                DISK_BLOCK_SIZE, ((uint8_t *)load_address) + load_off);
            if (ret < 0)
                break;
            load_off += ret;
        } while (load_off < os_image.fw_size + IMAGE_HEADER_SIZE);

        if (ret < 0) {
            wolfBoot_printf("Error reading image from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }
        elapsed_ms = (hal_get_timer_us() - start_us) / 1000;
        wolfBoot_printf("done. (%lu ms)\r\n", (unsigned long)elapsed_ms);

#ifdef DISK_ENCRYPT
        /* Decrypt the image in RAM */
        wolfBoot_printf("Decrypting image...");
        start_us = hal_get_timer_us();
        ret = decrypt_image((uint8_t*)load_address,
                os_image.fw_size + IMAGE_HEADER_SIZE);
        if (ret != 0) {
            wolfBoot_printf("Error decrypting image\r\n");
            selected ^= 1;
            continue;
        }
        elapsed_ms = (hal_get_timer_us() - start_us) / 1000;
        wolfBoot_printf("done. (%lu ms)\r\n", (unsigned long)elapsed_ms);
#endif

        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)load_address);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }

        wolfBoot_printf("Checking image integrity...");
        start_us = hal_get_timer_us();
        if (wolfBoot_verify_integrity(&os_image) != 0) {
            wolfBoot_printf("Error validating integrity for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
        elapsed_ms = (hal_get_timer_us() - start_us) / 1000;
        wolfBoot_printf("done. (%lu ms)\r\n", (unsigned long)elapsed_ms);

        wolfBoot_printf("Verifying image signature...");
        start_us = hal_get_timer_us();
        if (wolfBoot_verify_authenticity(&os_image) != 0) {
            wolfBoot_printf("Error validating authenticity for %s\r\n",
                part_name);
            selected ^= 1;
            continue;
        } else {
            elapsed_ms = (hal_get_timer_us() - start_us) / 1000;
            wolfBoot_printf("done. (%lu ms)\r\n", (unsigned long)elapsed_ms);
            failures = 0;
            break; /* Success case */
        }
    } while (failures < MAX_FAILURES);

    if (failures) {
        wolfBoot_printf("Unable to find a valid partition!\r\n");
        wolfBoot_panic();
    }

    disk_close(BOOT_DISK);

    wolfBoot_printf("Firmware Valid.\r\n");

    load_address = (uint32_t*)os_image.fw_base;

#ifdef WOLFBOOT_FDT
    /* Is this a Flattened uImage Tree (FIT) image (FDT format) */
    if (wolfBoot_get_dts_size(load_address) > 0) {
        void* fit = (void*)load_address;
        const char *kernel = NULL, *flat_dt = NULL;

        wolfBoot_printf("Flattened uImage Tree: Version %d, Size %d\n",
            fdt_version(fit), fdt_totalsize(fit));

        (void)fit_find_images(fit, &kernel, &flat_dt);
        if (kernel != NULL) {
            load_address = fit_load_image(fit, kernel, NULL);
        }
        if (flat_dt != NULL) {
            uint8_t *dts_ptr = fit_load_image(fit, flat_dt, (int*)&dts_size);
            if (dts_ptr != NULL && wolfBoot_get_dts_size(dts_ptr) >= 0) {
                /* relocate to load DTS address */
                dts_addr = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
                wolfBoot_printf("Loading DTS: %p -> %p (%d bytes)\n",
                    dts_ptr, dts_addr, dts_size);
                memcpy(dts_addr, dts_ptr, dts_size);
            }
        }
    }
#endif

#if defined(WOLFBOOT_ELF) && !defined(WOLFBOOT_FSP)
    /* Load elf sections and return the new entry point */
    /* Skip for FSP, since it expects ELF image directly */
    if (elf_load_image_mmu((uint8_t*)load_address, (uintptr_t*)&load_address, NULL) != 0){
        wolfBoot_printf("Invalid elf, falling back to raw binary\n");
    }
#endif

    wolfBoot_printf("Booting at %08lx\r\n", load_address);

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)hal_hsm_disconnect();
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    (void)hal_hsm_server_cleanup();
#endif
    hal_prepare_boot();

    do_boot((uint32_t*)load_address
    #ifdef MMU
        ,(uint32_t*)dts_addr
    #endif
    );
}
#endif /* WOLFBOOT_UPDATE_DISK */
