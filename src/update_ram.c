/* update_ram.c
 *
 * Implementation for RAM based updater
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

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include <string.h>
#ifdef WOLFBOOT_ELF
#include "elf.h"
#endif

extern void hal_flash_dualbank_swap(void);
extern int wolfBoot_get_dts_size(void *dts_addr);

extern uint32_t kernel_load_addr;
extern uint32_t dts_load_addr;

#if (defined(EXT_FLASH) && defined(NO_XIP)) || \
    (defined(EXT_ENCRYPTED) && defined(MMU)) && \
    !defined(WOLFBOOT_NO_RAMBOOT)
    /* Load firmware to RAM on boot (single flash read) */
    #undef  WOLFBOOT_USE_RAMBOOT
    #define WOLFBOOT_USE_RAMBOOT
#endif

#ifdef WOLFBOOT_USE_RAMBOOT
/* requires/assumes inputs and size to be 4-byte aligned */
static void memcpy32(void *dst, const void *src, size_t n)
{
    size_t i;
    const uint32_t *s = (const uint32_t*)src;
    uint32_t *d = (uint32_t*)dst;
    for (i = 0; i < n/4; i++) {
        d[i] = s[i];
    }
}

/* Function to load image from flash to ram */
int wolfBoot_ramboot(struct wolfBoot_image *img, uint8_t *src, uint8_t *dst)
{
    int ret;
    uint32_t img_size;

    /* read header into RAM */
    wolfBoot_printf("Loading header %d bytes from %p to %p\n",
        IMAGE_HEADER_SIZE, src, dst);
#if defined(EXT_FLASH) && defined(NO_XIP)
    ret = ext_flash_read((uintptr_t)src, dst, IMAGE_HEADER_SIZE);
    if (ret != IMAGE_HEADER_SIZE){
        wolfBoot_printf("Error reading header at %p\n", img);
        return -1;
    }
#else
    memcpy32(dst, src, IMAGE_HEADER_SIZE);
#endif

    /* check for valid header and version */
    ret = wolfBoot_get_blob_version((uint8_t*)dst);
    if (ret <= 0) {
        wolfBoot_printf("No valid image found at %p\n", src);
        return -1;
    }

    /* determine size of partition */
    img_size = wolfBoot_image_size((uint8_t*)dst);

    /* Read the entire image into RAM */
    wolfBoot_printf("Loading image %d bytes from %p to %p\n",
        img_size, src + IMAGE_HEADER_SIZE, dst + IMAGE_HEADER_SIZE);
#if defined(EXT_FLASH) && defined(NO_XIP)
    ret = ext_flash_read((uintptr_t)src + IMAGE_HEADER_SIZE,
                                    dst + IMAGE_HEADER_SIZE, img_size);
    if (ret < 0) {
        wolfBoot_printf("Error reading image at %p\n", src);
        return -1;
    }
#else
    memcpy32(dst + IMAGE_HEADER_SIZE, src + IMAGE_HEADER_SIZE, img_size);
#endif

    /* mark image as no longer external */
    img->not_ext = 1;

    return 0; /* success */
}
#endif

void RAMFUNCTION wolfBoot_start(void)
{
    int active, ret = 0;
    struct wolfBoot_image os_image;
    uint8_t *image_ptr;
    uint32_t *load_address = NULL;
    uint32_t *source_address = NULL;
#ifdef WOLFBOOT_FIXED_PARTITIONS
    uint8_t p_state;
#endif
#ifdef MMU
    uint8_t *dts_addr = NULL;
    uint32_t dts_size = 0;
#endif

    memset(&os_image, 0, sizeof(struct wolfBoot_image));

    for (;;) {
    #ifdef WOLFBOOT_FIXED_PARTITIONS
        active = wolfBoot_dualboot_candidate();
        if (active == PART_BOOT)
            source_address = (uint32_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        else
            source_address = (uint32_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    #else
        active = wolfBoot_dualboot_candidate_addr((void**)&source_address);
    #endif
        if (active < 0) { /* panic if no images available */
            wolfBoot_printf("No valid image found!\n");
            wolfBoot_panic();
        }

    #ifdef WOLFBOOT_FIXED_PARTITIONS
        wolfBoot_printf("Trying %s partition at %p\n",
                active == PART_BOOT ? "Boot" : "Update", source_address);
    #else
        wolfBoot_printf("Trying partition %d at %p\n",
                active, source_address);
    #endif

    #ifdef WOLFBOOT_USE_RAMBOOT
        load_address = (uint32_t*)(WOLFBOOT_LOAD_ADDRESS -
            IMAGE_HEADER_SIZE);
      #if defined(EXT_ENCRYPTED) && defined(MMU)
        ret = wolfBoot_ram_decrypt((uint8_t*)source_address,
            (uint8_t*)load_address);
      #else
        ret = wolfBoot_ramboot(&os_image, (uint8_t*)source_address,
            (uint8_t*)load_address);
      #endif
        if (ret != 0) {
            goto backup_on_failure;
        }
    #else
        load_address = source_address;
    #endif
    #if !defined(WOLFBOOT_FIXED_PARTITIONS) || \
            defined(WOLFBOOT_USE_RAMBOOT)
        ret = wolfBoot_open_image_address(&os_image, (uint8_t*)load_address);
    #else
        ret = wolfBoot_open_image(&os_image, active);
    #endif
        if ( (ret < 0) ||
            ((ret = wolfBoot_verify_integrity(&os_image) < 0)) ||
            ((ret = wolfBoot_verify_authenticity(&os_image)) < 0)) {
            goto backup_on_failure;

        } else {
            /* Success - integrity and signature valid */
        #ifdef WOLFBOOT_LOAD_ADDRESS
            load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
        #elif !defined(NO_XIP)
            load_address = (uint32_t*)os_image.fw_base;
        #else
            #error missing WOLFBOOT_LOAD_ADDRESS or XIP
        #endif
            break;
        }

backup_on_failure:
        wolfBoot_printf("Failure %d: Part %d, Hdr %d, Hash %d, Sig %d\n", ret,
            active, os_image.hdr_ok, os_image.sha_ok, os_image.signature_ok);
        /* panic if authentication fails and no backup */
        if (!wolfBoot_fallback_is_possible())
            wolfBoot_panic();
        else {
            /* Invalidate failing image and switch to the other partition */
            active ^= 1;
            continue;
        }
    }

    wolfBoot_printf("Firmware Valid\n");

    /* First time we boot this update, set to TESTING to await
     * confirmation from the system
     */
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if ((wolfBoot_get_partition_state(active, &p_state) == 0) &&
        (p_state == IMG_STATE_UPDATING))
    {
        hal_flash_unlock();
        wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
        hal_flash_lock();
    }
#endif

#ifdef WOLFBOOT_UBOOT_LEGACY
    /* Check for U-Boot Legacy format image header */
    image_ptr = wolfBoot_peek_image(&os_image, 0, NULL);
    if (image_ptr) {
        if (*((uint32_t*)image_ptr) == UBOOT_IMG_HDR_MAGIC) {
            /* Note: Could parse header and get load address at 0x10 */

            /* Skip 64 bytes (size of Legacy format image header) */
            load_address += UBOOT_IMG_HDR_SZ;
            os_image.fw_base += UBOOT_IMG_HDR_SZ;
            os_image.fw_size -= UBOOT_IMG_HDR_SZ;
        }
    }
#endif

#ifdef __GNUC__
    /* WOLFBOOT_LOAD_ADDRESS can be 0 address.
     * Do not warn on use of NULL for memcpy */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wnonnull"
#endif

#ifndef WOLFBOOT_USE_RAMBOOT
    /* if needed copy image to RAM */
    wolfBoot_printf("Loading %d bytes to RAM at %p\n", os_image.fw_size,
            load_address);
    memcpy((void*)load_address, os_image.fw_base, os_image.fw_size);
#endif

#ifdef WOLFBOOT_ELF
    /* Load elf */
    if (elf_load_image((uint8_t*)load_address, (uintptr_t*)&load_address) != 0){
        wolfBoot_printf("Invalid elf, falling back to raw binary\n");
    }
#endif

#ifdef MMU
    /* Load DTS to RAM */
    #ifdef EXT_FLASH
    if (PART_IS_EXT(&os_image) &&
        wolfBoot_open_image(&os_image, PART_DTS_BOOT) >= 0) {
        dts_addr = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
        dts_size = (uint32_t)os_image.fw_size;

        wolfBoot_printf("Loading DTS (size %lu) to RAM at %08lx\n",
                dts_size, dts_addr);
        ext_flash_check_read((uintptr_t)os_image.fw_base,
                (uint8_t*)dts_addr, dts_size);
    }
    else
    #endif /* EXT_FLASH */
    {
        dts_addr = hal_get_dts_address();
        if (dts_addr) {
            ret = wolfBoot_get_dts_size(dts_addr);
            if (ret < 0) {
                wolfBoot_printf("Failed parsing DTB to load\n");
                /* Allow failure, continue booting */
            }
            else {
                /* relocate DTS to RAM */
                uint8_t* dts_dst = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
                dts_size = (uint32_t)ret;
                wolfBoot_printf("Loading DTB (size %d) from %p to RAM at %p\n",
                        dts_size, dts_addr, WOLFBOOT_LOAD_DTS_ADDRESS);
                memcpy(dts_dst, dts_addr, dts_size);
                dts_addr = dts_dst;
            }
        }
    }
#endif /* MMU */

    wolfBoot_printf("Booting at %p\n", load_address);
    hal_prepare_boot();

#ifdef MMU
    do_boot((uint32_t*)load_address,
            (uint32_t*)dts_addr);
#else
    /* Use load_address instead of os_image.fw_base, which may have
     * wrong base address */
    do_boot((uint32_t*)load_address);
#endif

#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
}
