
/* loader_stage1.c
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

/* A stage 1 loader to copy wolfBoot from flash to RAM location */

#ifdef BUILD_LOADER_STAGE1

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

#include <string.h>
#include <stdint.h>


#if !defined(EXT_FLASH) && defined(NO_XIP)
    #error Using first stage loader requires XIP or External Flash (EXT_FLASH)
#endif

#ifndef WOLFBOOT_STAGE1_SIZE
    #define WOLFBOOT_STAGE1_SIZE (4*1024)
#endif

#ifdef WOLFBOOT_ARCH_PPC
#include "hal/nxp_ppc.h"
#endif

#ifndef DO_BOOT
#ifdef MMU
#define DO_BOOT(addr) do_boot((addr), NULL)
#else
#define DO_BOOT(addr) do_boot((addr))
#endif
#endif

int main(void)
{
    int ret = -1;
    uint32_t* wolfboot_start;

    hal_init();
    spi_flash_probe(); /* make sure external flash is initialized */

#if defined(NO_XIP) && defined(BOOT_ROM_ADDR)
    /* if this is executing from boot 4KB region (FCM buffer) it must
     * first be relocated to RAM before the eLBC NAND can be read */
    if ((get_pc() & BOOT_ROM_ADDR) == BOOT_ROM_ADDR) {
        wolfboot_start = (uint32_t*)WOLFBOOT_STAGE1_BASE_ADDR;
    #ifdef DEBUG_UART
        uart_write("\nRelocating BOOT ROM to DDR\n", 28);
    #endif

        /* relocate 4KB code to DST and jump */
        memcpy((void*)wolfboot_start, (void*)BOOT_ROM_ADDR, BOOT_ROM_SIZE);

    #ifdef WOLFBOOT_ARCH_PPC
        /* TODO: Fix hack and consider moving to hal_prepare_boot */
        /* HACK: Fix up stack values modified with trap */
        *((uint32_t*)(WOLFBOOT_STAGE1_BASE_ADDR + 0xB70)) = 0x9421FFF0; /* main() */
        *((uint32_t*)(WOLFBOOT_STAGE1_BASE_ADDR + 0xBCC)) = 0x39200000; /* instruction above */

        /* call to relocate code does not return */
        relocate_code(wolfboot_start, (void*)BOOT_ROM_ADDR, BOOT_ROM_SIZE);
    #else
        hal_prepare_boot();
        DO_BOOT(wolfboot_start); /* never returns */
    #endif
    }
#endif

#ifdef DEBUG_UART
    uart_write("Loading wolfBoot to DDR\n", 24);
#endif

#ifdef EXT_FLASH
    ret = ext_flash_read(
        (uintptr_t)WOLFBOOT_ORIGIN,         /* flash offset */
        (uint8_t*)WOLFBOOT_STAGE1_LOAD_ADDR,/* ram destination */
        BOOTLOADER_PARTITION_SIZE           /* boot-loader partition (entire) */
    );
#else
    /* copy from flash to ram */
    memcpy(
        (uint8_t*)WOLFBOOT_STAGE1_LOAD_ADDR,/* ram destination */
        (uint8_t*)WOLFBOOT_ORIGIN,          /* flash offset */
        BOOTLOADER_PARTITION_SIZE           /* boot-loader partition (entire) */
    );
    ret = 0;
#endif
    if (ret >= 0) {
        wolfboot_start = (uint32_t*)WOLFBOOT_STAGE1_LOAD_ADDR;
    #ifdef PRINTF_ENABLED
        wolfBoot_printf("Jumping to full wolfBoot at %p\n", wolfboot_start);
    #elif defined(DEBUG_UART)
        uart_write("Jumping to full wolfBoot\n", 25);
    #endif

        hal_prepare_boot();
        DO_BOOT(wolfboot_start); /* never returns */
    }

    return 0;
}

#endif /* BUILD_LOADER_STAGE1 */
