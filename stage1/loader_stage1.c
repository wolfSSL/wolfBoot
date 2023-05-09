
/* loader_stage1.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

/* A stage 1 loader to copy wolfBoot from flash to RAM location */

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include "string.h"

#include <stdint.h>

#ifndef EXT_FLASH
    #error The stage1 loader only supports use with external flash
#endif

#ifndef WOLFBOOT_STAGE1_SIZE
    #define WOLFBOOT_STAGE1_SIZE (4*1024)
#endif

#ifdef BUILD_LOADER_STAGE1

#if defined(WOLFBOOT_ARCH) && WOLFBOOT_ARCH == PPC
#include "hal/nxp_ppc.h"
#endif

int main(void)
{
    int ret = -1;
    uint32_t* wolfboot_start;

    hal_init();
    spi_flash_probe(); /* make sure external flash is initialized */

#ifdef PRINTF_ENABLED
    wolfBoot_printf("Loader Stage 1: Flash 0x%x to RAM 0x%x\r\n",
        WOLFBOOT_ORIGIN, WOLFBOOT_STAGE1_LOAD_ADDR);
#elif defined(DEBUG_UART)
    uart_write("Loader Stage 1\r\n", 16);
#endif

#ifdef BOOT_ROM_ADDR
    /* if this is executing from boot 4KB region (FCM buffer) it must
     * first be relocated to RAM before the eLBC NAND can be read */
    if (((uintptr_t)&hal_init & BOOT_ROM_ADDR) == BOOT_ROM_ADDR) {
        wolfboot_start = (uint32_t*)WOLFBOOT_STAGE1_BASE_ADDR;

        /* relocate 4KB code to DST and jump */
        memmove((void*)WOLFBOOT_STAGE1_BASE_ADDR, (void*)BOOT_ROM_ADDR,
            BOOT_ROM_SIZE);

        do_boot(wolfboot_start); /* never returns */
    }
#endif

    ret = ext_flash_read(
        (uintptr_t)WOLFBOOT_ORIGIN,         /* flash offset */
        (uint8_t*)WOLFBOOT_STAGE1_LOAD_ADDR,/* ram destination */
        BOOTLOADER_PARTITION_SIZE           /* boot-loader partition (entire) */
    );
    if (ret >= 0) {
        wolfboot_start = (uint32_t*)WOLFBOOT_STAGE1_LOAD_ADDR;
    #ifdef PRINTF_ENABLED
        wolfBoot_printf("Jumping to %p\r\n", wolfboot_start);
    #elif defined(DEBUG_UART)
        uart_write("Jump to relocated wolfboot_start\r\n", 34);
    #endif

        do_boot(wolfboot_start); /* never returns */
    }

    return 0;
}

#endif /* BUILD_LOADER_STAGE1 */
