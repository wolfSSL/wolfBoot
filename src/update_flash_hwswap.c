/* update_flash_hwswap.c
 *
 * Implementation for hardware assisted updater
 *
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "hooks.h"
#include "spi_flash.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"
#ifdef SECURE_PKCS11
int WP11_Library_Init(void);
#endif

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
#ifndef ALLOW_DOWNGRADE
    int boot_v_raw = (int)wolfBoot_current_firmware_version();
    int update_v_raw = (int)wolfBoot_update_firmware_version();
    uint32_t boot_v = 0U;
    uint32_t update_v = 0U;
    uint32_t max_v;

    if (boot_v_raw >= 0)
        boot_v = (uint32_t)boot_v_raw;
    if (update_v_raw >= 0)
        update_v = (uint32_t)update_v_raw;
    max_v = (boot_v > update_v) ? boot_v : update_v;
#endif
    active = wolfBoot_dualboot_candidate();

    if (active < 0) /* panic if no images available */
        boot_panic();

    for (;;) {
#ifndef ALLOW_DOWNGRADE
        uint32_t active_v = (active == PART_UPDATE) ? update_v : boot_v;
        if ((max_v > 0U) && (active_v < max_v)) {
            wolfBoot_printf("Rollback to lower version not allowed\n");
            boot_panic();
            return;
        }
#endif
        if ((wolfBoot_open_image(&fw_image, active) < 0)
#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
            || (wolfBoot_verify_integrity(&fw_image) < 0)
            || (wolfBoot_verify_authenticity(&fw_image) < 0)
#endif
        ) {
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
    if ((active == PART_BOOT) && (wolfBoot_get_partition_state(active, &p_state) == 0) &&
        (p_state == IMG_STATE_UPDATING))
    {
        hal_flash_unlock();
        wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
        hal_flash_lock();
    }

    /* Booting from update is possible via HW-assisted swap */
    if (active == PART_UPDATE) {
        hal_flash_dualbank_swap();
        /* On some platform, e.g. STM32L5, hal_flash_dualbank_swap
         * never returns. A reboot is triggered instead, so the code
         * below is only executed if we are staging the firmware.
         */
        active = PART_BOOT;
        if ((wolfBoot_get_partition_state(active, &p_state) == 0) &&
                (p_state == IMG_STATE_UPDATING))
        {
            hal_flash_unlock();
            wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
            hal_flash_lock();
        }
    }
#ifdef SECURE_PKCS11
    WP11_Library_Init();
#endif
#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)hal_hsm_disconnect();
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    (void)hal_hsm_server_cleanup();
#endif
#ifndef TZEN
    if (hal_flash_protect(WOLFBOOT_ORIGIN, BOOTLOADER_PARTITION_SIZE) < 0)
        boot_panic();
#endif
    hal_prepare_boot();
#ifdef WOLFBOOT_HOOK_BOOT
    wolfBoot_hook_boot(&fw_image);
#endif
#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
    PART_SANITY_CHECK(&fw_image);
#endif
    do_boot((void *)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE));
}
