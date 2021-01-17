/* update_ram.c
 *
 * Implementation for RAM based updater
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include <string.h>

extern void hal_flash_dualbank_swap(void);

static inline void boot_panic(void)
{
    wolfBoot_printf("entering %s() - to die here\r\n", __func__);
    while(1)
        ;
}

void RAMFUNCTION wolfBoot_start(void)
{
    int active, ret = 0;
    struct wolfBoot_image os_image;
    uint32_t* load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
    uint8_t* image_ptr;
    uint8_t p_state;
#ifdef MMU
    uint32_t* dts_address = NULL;
#endif
    wolfBoot_printf("entering %s()\r\n", __func__);

    active = wolfBoot_dualboot_candidate();

    wolfBoot_printf("Active Part %d\r\n", active);

    if (active < 0) {
        /* panic if no images available */
        wolfBoot_printf("no partitions found\r\n");
        boot_panic();
    }
    /* Check current status for failure (image still in TESTING), and fall-back
     * if an alternative is available
     */
    if (wolfBoot_fallback_is_possible() &&
            (wolfBoot_get_partition_state(active, &p_state) == 0) &&
            (p_state == IMG_STATE_TESTING))
    {
        active ^= 1; /* switch to other partition if available */
    }

    for (;;) {
        if (((ret = wolfBoot_open_image(&os_image, active)) < 0) ||
            ((ret = wolfBoot_verify_integrity(&os_image) < 0)) ||
            ((ret = wolfBoot_verify_authenticity(&os_image)) < 0)) {

            wolfBoot_printf("Failure %d: Part %d, Hdr %d, Hash %d, Sig %d\r\n", ret, 
                active, os_image.hdr_ok, os_image.sha_ok, os_image.signature_ok);

            /* panic if authentication fails and no backup */
            if (!wolfBoot_fallback_is_possible()) {
                wolfBoot_printf("no fallback image available\r\n");
                boot_panic();
            }
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

    wolfBoot_printf("Firmware Valid\r\n");

    /* First time we boot this update, set to TESTING to await
     * confirmation from the system
     */
    if ((wolfBoot_get_partition_state(active, &p_state) == 0) &&
        (p_state == IMG_STATE_UPDATING))
    {
        hal_flash_unlock();
        wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
        hal_flash_lock();
    }

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
        wolfBoot_printf("Loading %d to RAM at %p\r\n", os_image.fw_size, load_address);

        ext_flash_read((uintptr_t)os_image.fw_base, 
                       (uint8_t*)load_address,
                       os_image.fw_size);
    }
#endif

#ifdef MMU
    /* Device Tree Blob (DTB) Handling */
    if (wolfBoot_open_image(&os_image, PART_DTS_BOOT) >= 0) {
        dts_address = (uint32_t*)WOLFBOOT_LOAD_DTS_ADDRESS;

    #ifdef EXT_FLASH
        /* Load DTS to RAM */
        if (PART_IS_EXT(&os_image)) {
            wolfBoot_printf("Loading DTB %d to RAM at %p\r\n", os_image.fw_size, dts_address);

            ext_flash_read((uintptr_t)os_image.fw_base, 
                        (uint8_t*)dts_address,
                        os_image.fw_size);
        }
    #endif
    }
#endif

    hal_prepare_boot();
	
#ifdef MMU
    wolfBoot_printf("Booting at %p, with DTB at %p\n", load_address, dts_address);
    do_boot((uint32_t*)load_address, (uint32_t*)dts_address);
#else
    do_boot((uint32_t*)load_address);
#endif
    wolfBoot_printf("Booting at %p\r\n", load_address);
}
