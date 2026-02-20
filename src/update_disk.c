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

/* Module-level storage for encryption nonce */
static uint8_t disk_encrypt_nonce[ENCRYPT_NONCE_SIZE];
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

/* Module-level storage for encryption key */
static uint8_t disk_encrypt_key[ENCRYPT_KEY_SIZE];

/**
 * @brief Get the version from an already-decrypted header.
 *
 * This function extracts the version from a decrypted header blob
 * without calling wolfBoot_get_blob_version, which might try to
 * decrypt again if EXT_ENCRYPTED && MMU is defined.
 *
 * @param hdr Pointer to the decrypted header.
 *
 * @return The version number, or 0 if not found.
 */
static uint32_t get_decrypted_blob_version(uint8_t *hdr)
{
    uint32_t *magic = (uint32_t *)hdr;
    uint16_t tlv_type, tlv_len;
    uint8_t *p = hdr + IMAGE_HEADER_OFFSET;
    uint8_t *max_p = hdr + IMAGE_HEADER_SIZE;

    if (*magic != WOLFBOOT_MAGIC)
        return 0;

    /* Search for version TLV */
    while (p + 4 < max_p) {
        tlv_type = *((uint16_t*)p);
        tlv_len = *((uint16_t*)(p + 2));

        if (tlv_type == 0 || tlv_type == 0xFFFF)
            break;

        /* Skip padding bytes */
        if ((p[0] == 0xFF) || ((((uintptr_t)p) & 0x01) != 0)) {
            p++;
            continue;
        }

        if (tlv_type == HDR_VERSION && tlv_len == 4) {
            uint32_t ver = *((uint32_t*)(p + 4));
            return ver;
        }

        p += 4 + tlv_len;
    }
    return 0;
}

/**
 * @brief Set up decryption context with IV at specified block offset.
 *
 * This function sets up the AES/ChaCha context with the IV positioned
 * at the specified block offset. It matches how sign.c sets up encryption.
 *
 * @param block_offset Block offset for IV counter (0 = start of image).
 */
static void disk_crypto_set_iv(uint32_t block_offset)
{
#if defined(ENCRYPT_WITH_CHACHA)
    wc_Chacha_SetIV(&chacha, disk_encrypt_nonce, block_offset);
#elif defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256)
    /* For AES CTR, we need to construct the IV with the counter.
     * The sign tool uses the IV directly without byte-reversal,
     * so we must match that behavior here. */
    uint8_t iv[ENCRYPT_BLOCK_SIZE];
    uint32_t ctr;

    /* Copy nonce/IV (first 12 bytes for CTR nonce, last 4 for counter) */
    memcpy(iv, disk_encrypt_nonce, ENCRYPT_NONCE_SIZE);

    /* Add block offset to the counter portion (last 4 bytes, big-endian) */
    /* The IV from sign.c is already in the correct format, we just need
     * to add the block offset to the counter portion */
    ctr = ((uint32_t)iv[12] << 24) | ((uint32_t)iv[13] << 16) |
          ((uint32_t)iv[14] << 8) | (uint32_t)iv[15];
    ctr += block_offset;
    iv[12] = (uint8_t)(ctr >> 24);
    iv[13] = (uint8_t)(ctr >> 16);
    iv[14] = (uint8_t)(ctr >> 8);
    iv[15] = (uint8_t)(ctr);

    wc_AesSetIV(&aes_dec, iv);
#endif
}

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
    uint32_t magic;

    /* Reset IV to start of image (block 0) */
    disk_crypto_set_iv(0);

    /* Decrypt header - CTR mode handles counter increment internally */
    crypto_decrypt(dst, src, IMAGE_HEADER_SIZE);

    magic = *((uint32_t*)dst);
    if (magic != WOLFBOOT_MAGIC)
        return -1;
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
    const uint8_t *hdr_ptr = NULL;
#ifdef MMU
    uint8_t *dts_addr = NULL;
    #ifdef WOLFBOOT_FDT
    uint32_t dts_size = 0;
    #endif
#endif
    char part_name[4] = {'P', ':', 'X', '\0'};
    BENCHMARK_DECLARE();

#ifdef DISK_ENCRYPT
    /* Initialize encryption - this sets up the cipher with key from storage */
    if (wolfBoot_initialize_encryption() != 0) {
        wolfBoot_printf("Error initializing encryption\r\n");
        wolfBoot_panic();
    }
    /* Retrieve encryption key and nonce for disk decryption */
    if (wolfBoot_get_encrypt_key(disk_encrypt_key, disk_encrypt_nonce) != 0) {
        wolfBoot_printf("Error getting encryption key\r\n");
        wolfBoot_panic();
    }
    wolfBoot_printf("Disk encryption enabled\r\n");
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
            /* Use local version parser to avoid double-decryption issue
             * when EXT_ENCRYPTED && MMU is also defined */
            pA_ver = get_decrypted_blob_version(dec_hdr);
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
            /* Use local version parser to avoid double-decryption issue
             * when EXT_ENCRYPTED && MMU is also defined */
            pB_ver = get_decrypted_blob_version(dec_hdr);
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

        hdr_ptr = p_hdr;
#ifdef DISK_ENCRYPT
        /* Decrypt header to parse image size */
        if (decrypt_header(p_hdr, dec_hdr) != 0) {
            wolfBoot_printf("Error decrypting header for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
        hdr_ptr = dec_hdr;
#endif
        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)hdr_ptr);
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

        /* Read the payload into RAM (skip header) */
        wolfBoot_printf("Loading image from disk...");
        BENCHMARK_START();
        load_off = 0;
        do {
            uint32_t chunk = os_image.fw_size - load_off;
            if (chunk > DISK_BLOCK_SIZE)
                chunk = DISK_BLOCK_SIZE;
            ret = disk_part_read(BOOT_DISK, cur_part,
                IMAGE_HEADER_SIZE + load_off, chunk,
                ((uint8_t *)load_address) + load_off);
            if (ret <= 0)
                break;
            load_off += ret;
        } while (load_off < os_image.fw_size);

        if (ret < 0) {
            wolfBoot_printf("Error reading image from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }
        BENCHMARK_END("done");

#ifdef DISK_ENCRYPT
        /* Decrypt the payload in RAM */
        wolfBoot_printf("Decrypting image...");
        BENCHMARK_START();
        if ((IMAGE_HEADER_SIZE % ENCRYPT_BLOCK_SIZE) != 0) {
            wolfBoot_printf("Encrypted disk images require aligned header size\r\n");
            wolfBoot_panic();
        }
        disk_crypto_set_iv(IMAGE_HEADER_SIZE / ENCRYPT_BLOCK_SIZE);
        crypto_decrypt((uint8_t*)load_address, (uint8_t*)load_address,
            os_image.fw_size);
        BENCHMARK_END("done");
#endif

        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)hdr_ptr);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }
        os_image.fw_base = (uint8_t*)load_address;

        wolfBoot_printf("Checking image integrity...");
        BENCHMARK_START();
        if (wolfBoot_verify_integrity(&os_image) != 0) {
            wolfBoot_printf("Error validating integrity for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
        BENCHMARK_END("done");

        wolfBoot_printf("Verifying image signature...");
        BENCHMARK_START();
        if (wolfBoot_verify_authenticity(&os_image) != 0) {
            wolfBoot_printf("Error validating authenticity for %s\r\n",
                part_name);
            selected ^= 1;
            continue;
        } else {
            BENCHMARK_END("done");
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
    if (elf_load_image_mmu((uint8_t*)load_address, os_image.fw_size,
            (uintptr_t*)&load_address, NULL) != 0){
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
