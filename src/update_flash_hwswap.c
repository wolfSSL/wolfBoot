/* update_flash_hwswap.c
 *
 * Implementation for hardware assisted updater
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
#include "wolfboot/wolfboot.h"

extern void hal_flash_dualbank_swap(void);

static inline void boot_panic(void)
{
    while(1)
        ;
}

void RAMFUNCTION wolfBoot_start(void)
{
    int active;
    struct wolfBoot_image fw_image;
    uint8_t p_state;
    active = wolfBoot_dualboot_candidate();

    if (active < 0) /* panic if no images available */
        boot_panic();

    for (;;) {
        if ((wolfBoot_open_image(&fw_image, active) < 0) ||
            (wolfBoot_verify_integrity(&fw_image) < 0) ||
            (wolfBoot_verify_authenticity(&fw_image) < 0)) {

            /* panic if authentication fails and no backup */
            if (!wolfBoot_fallback_is_possible())
                boot_panic();
            else {
                /* Invalidate failing image and switch to the
                 * other partition
                 */
                wolfBoot_erase_partition(active);
                active ^= 1;
            }
        } else
            break; /* candidate successfully authenticated */
    }

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

    /* Booting from update is possible via HW-assisted swap */
    if (active == PART_UPDATE)
        hal_flash_dualbank_swap();

    hal_prepare_boot();
    do_boot((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE);
}
