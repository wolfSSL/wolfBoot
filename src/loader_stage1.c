
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

#ifndef EXT_FLASH
    #error The stage1 loader only supports use with external flash
#endif

#ifndef LOADER_STAGE1_SIZE
    #define LOADER_STAGE1_SIZE (4*1024)
#endif
#ifndef LOADER_STAGE1_START_ADDR
    /* default is end of 4KB region (0x0FFC) */
    #define LOADER_STAGE1_START_ADDR \
        (WOLFBOOT_STAGE1_LOAD_ADDR + LOADER_STAGE1_SIZE - 0x4)
#endif


int main(void)
{
    int ret = -1;
    __attribute__((noreturn)) void (*wolfboot_start)(void);

    hal_init();
    spi_flash_probe(); /* make sure external flash is initialized */

#ifdef PRINTF_ENABLED
    wolfBoot_printf("Loader Stage 1: Flash 0x%x to RAM 0x%x\r\n",
        WOLFBOOT_ORIGIN, WOLFBOOT_STAGE1_LOAD_ADDR);
#elif defined(DEBUG_UART)
    uart_write("Loader Stage 1\r\n", 16);
#endif

    ret = ext_flash_read(
        (uintptr_t)WOLFBOOT_ORIGIN,       /* flash offset */
        (uint8_t*)WOLFBOOT_STAGE1_LOAD_ADDR, /* ram destination */
        BOOTLOADER_PARTITION_SIZE         /* boot-loader partition (entire) */
    );
    if (ret >= 0) {
        wolfboot_start = (void*)LOADER_STAGE1_START_ADDR;
        wolfboot_start(); /* never returns */
    }

    return 0;
}
