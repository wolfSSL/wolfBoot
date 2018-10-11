/* main.c
 *
 * Copyright (C) 2018 wolfSSL Inc.
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

#include "target.h"
#include "hal/hal_flash.h"
#include "printf.h"

#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "flash_map_backend/flash_map_backend.h"

extern void do_boot(void *);

void main(void)
{
    struct boot_rsp rsp;
    int rc;

    hal_init();
#ifdef TEST_PENDING
    boot_set_pending(1);
#endif

    wolfBoot_printf("Starting bootloader");

    rc = boot_go(&rsp);
    if (rc != 0) {
        wolfBoot_printf("Unable to find bootable image");
        while (1)
            ;
    }

    wolfBoot_printf("Bootloader chainload address offset: 0x%x",
                 rsp.br_image_off);


    hal_prepare_boot();
    wolfBoot_printf("Jumping to the first image slot");
    do_boot((uint8_t *)0 + rsp.br_hdr->ih_load_addr);

    wolfBoot_printf("something went wrong.");
    while (1)
        ;
}
