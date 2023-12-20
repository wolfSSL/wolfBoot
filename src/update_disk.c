/* update_disk.c
 *
 * Implementation for RAM based updater, for systems that provide
 * drives and partition mapping.
 *
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

#ifndef UPDATE_DISK_H_
#define UPDATE_DISK_H_
#include "image.h"
#include "loader.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "stage1.h"
#include "wolfboot/wolfboot.h"
#include <stdint.h>
#include <string.h>
#include <x86/common.h>
#include <x86/ahci.h>
#include <x86/ata.h>
#include <x86/gpt.h>
#include <pci.h>

#if defined(WOLFBOOT_FSP)
#include <x86/tgl_fsp.h>
#endif

#ifdef TARGET_x86_fsp_qemu
#define BOOT_DISK 0
#define BOOT_PART_A 0
#define BOOT_PART_B 1
#else
#define BOOT_DISK 0
#define BOOT_PART_A 5
#define BOOT_PART_B 6
#endif

#define MAX_FAILURES 4

/* from the linker, where wolfBoot ends */
extern uint8_t _end_wb[];

/**
 * @brief function for starting the boot process.
 *
 * This function starts the boot process by attempting to read and load
 * the OS image from disk partitions. It then verifies the integrity and
 * authenticity of the loaded image before initiating the boot.
 */
void RAMFUNCTION wolfBoot_start(void)
{
    struct stage2_parameter *stage2_params;
    struct wolfBoot_image os_image;
    uint8_t p_hdr[IMAGE_HEADER_SIZE];
    int pA_ver = 0, pB_ver = 0;
    uint32_t cur_part = 0;
    int ret = -1;
    int selected;
    uint32_t img_size = 0;
    uint32_t *load_address;
    int failures = 0;
    uint32_t load_off;
    uint32_t sata_bar;

#if defined(WOLFBOOT_FSP)
    ret = x86_fsp_tgl_init_sata(&sata_bar);
    if (ret != 0)
        panic();
#if defined(WOLFBOOT_ATA_DISK_LOCK)
    ret = sata_unlock_disk(BOOT_DISK, 1);
    if (ret != 0)
        panic();
#endif /* WOLFBOOT_ATA_DISK_LOCK */
#endif /* WOLFBOOT_FSP */

    if (disk_open(BOOT_DISK) < 0)
        panic();

    memset(&os_image, 0, sizeof(struct wolfBoot_image));

    wolfBoot_printf("Checking primary OS image in %d,%d...\r\n", BOOT_DISK,
            BOOT_PART_A);
    if (disk_read(BOOT_DISK, BOOT_PART_A, 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
        pA_ver = wolfBoot_get_blob_version(p_hdr);
    }

    wolfBoot_printf("Checking secondary OS image in %d,%d...\r\n", BOOT_DISK,
            BOOT_PART_B);
    if (disk_read(BOOT_DISK, BOOT_PART_B, 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
        pB_ver = wolfBoot_get_blob_version(p_hdr);
    }

    if ((pB_ver == 0) && (pA_ver == 0)) {
        wolfBoot_printf("No valid OS image found in either partitions.\r\n");
        panic();
    }

    wolfBoot_printf("Versions, A:%u B:%u\r\n", pA_ver, pB_ver);

    if (pB_ver > pA_ver)
        selected = 1;
    else
        selected = 0;

    stage2_params = stage2_get_parameters();
    /* load the image just after wolfboot */
    load_address = (uint32_t *)(_end_wb);
    wolfBoot_printf("Load address 0x%x\r\n", load_address);
    do {
        failures++;
        if (selected)
            cur_part = BOOT_PART_B;
        else
            cur_part = BOOT_PART_A;

        wolfBoot_printf("Attempting boot from partition %c\r\n", 'A' + selected);
    
        /* Fetch header again */
        if (disk_read(BOOT_DISK, cur_part, 0, IMAGE_HEADER_SIZE, p_hdr)
            != IMAGE_HEADER_SIZE) {
            wolfBoot_printf("Error reading image header from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }

        /* Dereference img_size from header */
        img_size = *( ((uint32_t *)p_hdr) + 1);

        if (img_size >
            ((uint32_t)(stage2_params->tolum) - (uint32_t)(uintptr_t)load_address)) {
                wolfBoot_printf("Image size %d doesn't fit in low memory\r\n", img_size);
                break;
        }

        /* Read the image into RAM */
        x86_log_memory_load((uint32_t)(uintptr_t)load_address,
                            (uint32_t)(uintptr_t)load_address + img_size,
                            "ELF");
        wolfBoot_printf("Loading image from disk...");
        load_off = 0;
        do {
            ret = disk_read(BOOT_DISK, cur_part, load_off, 512,
                    (uint8_t *)load_address + load_off);
            if (ret < 0)
                break;
            load_off += ret;
        } while (load_off < img_size + IMAGE_HEADER_SIZE);

        if (ret < 0) {
            wolfBoot_printf("Error reading image from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }
        wolfBoot_printf("done.\r\n");
        ret = wolfBoot_open_image_address(&os_image, (void *)load_address);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }

        wolfBoot_printf("Checking image integrity...");
        if (wolfBoot_verify_integrity(&os_image) != 0) {
            wolfBoot_printf("Error validating integrity for partition %c\r\n",
                    'A' + selected);
            selected ^= 1;
            continue;
        }
        wolfBoot_printf("done.\r\n");
        wolfBoot_printf("Verifying image signature...");
        if (wolfBoot_verify_authenticity(&os_image) != 0) {
            wolfBoot_printf("Error validating authenticity for partition %c\r\n",
                    'A' + selected);
            selected ^= 1;
            continue;
        } else {
            wolfBoot_printf("done.\r\n");
            failures = 0;
            break; /* Success case */
        }
    } while (failures < MAX_FAILURES);

    if (failures) {
        panic();
    }

    sata_disable(sata_bar);
    wolfBoot_printf("Firmware Valid.\r\n");
    wolfBoot_printf("Booting at %08lx\r\n", os_image.fw_base);
    hal_prepare_boot();
    do_boot((uint32_t*)os_image.fw_base);
}
#endif /* UPDATE_DISK_H_ */
