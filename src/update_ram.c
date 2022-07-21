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

extern void hal_flash_dualbank_swap(void);
extern int wolfBoot_get_dts_size(void *dts_addr);

static inline void boot_panic(void)
{
    while(1)
        ;
}


void RAMFUNCTION wolfBoot_start(void)
{
    int active, ret = 0;
    struct wolfBoot_image os_image;
    uint8_t *image_ptr;
    uint8_t p_state;
    uint32_t *load_address;
    uint8_t *dts_buf = NULL;
    uint32_t dts_size = 0;

#ifdef WOLFBOOT_FIXED_PARTITIONS
    active = wolfBoot_dualboot_candidate();
    if (active == PART_UPDATE)
        load_address = (uint32_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    else
        load_address = (uint32_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
#else
    active = wolfBoot_dualboot_candidate_addr((void**)&load_address);
#endif
    if (active < 0) /* panic if no images available */
        wolfBoot_panic();


    #ifdef WOLFBOOT_FIXED_PARTITIONS
    /* Check current status for failure (image still in TESTING), and fall-back
     * if an alternative is available
     */
    if (wolfBoot_fallback_is_possible() &&
            (wolfBoot_get_partition_state(active, &p_state) == 0) &&
            (p_state == IMG_STATE_TESTING))
    {
        active ^= 1; /* switch to other partition if available */
    }
    #endif

    wolfBoot_printf("Active Partition: %c\n", active?'B':'A');
    wolfBoot_printf("Active Partition start address: %x\n", load_address);
    for (;;) {
        if (((ret = wolfBoot_open_image_address(&os_image, (uint8_t*)load_address)) < 0) ||
            ((ret = wolfBoot_verify_integrity(&os_image) < 0)) ||
            ((ret = wolfBoot_verify_authenticity(&os_image)) < 0)) {

        wolfBoot_printf("Failure %d: Part %d, Hdr %d, Hash %d, Sig %d\n", ret,
            active, os_image.hdr_ok, os_image.sha_ok, os_image.signature_ok);

            /* panic if authentication fails and no backup */
            if (!wolfBoot_fallback_is_possible())
                wolfBoot_panic();
            else {
                /* Invalidate failing image and switch to the
                 * other partition
                 */
                active ^= 1;
                continue;
            }
        } else {
        	break;
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

    /* Check for U-Boot Legacy format image header */
    image_ptr = wolfBoot_peek_image(&os_image, 0, NULL);
    if (image_ptr) {
        if (*((uint32_t*)image_ptr) == UBOOT_IMG_HDR_MAGIC) {
            /* Note: Could parse header and get load_address at 0x10 */

            /* Skip 64 bytes (size of Legacy format image header) */
            os_image.fw_base += UBOOT_IMG_HDR_SZ;
            os_image.fw_size -= UBOOT_IMG_HDR_SZ;
        }
    }

#ifdef EXT_FLASH
    /* Load image to RAM */
    if (PART_IS_EXT(&os_image)) {
        wolfBoot_printf("Loading %d bytes to RAM at %08lx\n",
                os_image.fw_size, WOLFBOOT_LOAD_ADDRESS);

        ext_flash_read((uintptr_t)os_image.fw_base,
                       (uint8_t*)WOLFBOOT_LOAD_ADDRESS,
                       os_image.fw_size);
    }
  #ifdef MMU
    /* Load DTS to RAM */
    if (PART_IS_EXT(&os_image)) {
        if (wolfBoot_open_image(&os_image, PART_DTS_BOOT) >= 0) {
            dts_address = (uint32_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
        wolfBoot_printf("Loading DTS (size %lu) to RAM at %08lx\n",
                os_image.fw_size, WOLFBOOT_LOAD_DTS_ADDRESS);
        ext_flash_read((uintptr_t)os_image.fw_base,
                (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS,
                os_image.fw_size);
    }
  #endif /* MMU */
#else
    wolfBoot_printf("Loading %d bytes to RAM at %08lx\n",
            os_image.fw_size, WOLFBOOT_LOAD_ADDRESS);
    memcpy((void*)WOLFBOOT_LOAD_ADDRESS, os_image.fw_base, os_image.fw_size);
  #ifdef MMU
    dts_buf = hal_get_dts_address();
    if (dts_buf) {
        ret = wolfBoot_get_dts_size(dts_buf);
        if (ret < 0) {
            wolfBoot_printf("Failed parsing DTB to load.\n");
        } else {
            dts_size = (uint32_t)ret;
            wolfBoot_printf("Loading DTB (size %d) to RAM at %08lx\n",
                    dts_size, dts_address);
            memcpy((void*)WOLFBOOT_LOAD_DTS_ADDRESS, dts_buf, dts_size);
        }
    }
  #endif /* MMU */

#endif /* WOLFBOOT_FIXED_PARTITIONS */
    wolfBoot_printf("Booting at %08lx\n", WOLFBOOT_LOAD_ADDRESS);
    hal_prepare_boot();

#if defined MMU
    do_boot((uint32_t*)WOLFBOOT_LOAD_ADDRESS,
            (uint32_t*)WOLFBOOT_LOAD_DTS_ADDRESS);
#else
    do_boot((uint32_t*)os_image.fw_base);
#endif
}
