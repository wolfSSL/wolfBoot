/* loader.c
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

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "uart_flash.h"
#include "wolfboot/wolfboot.h"

#ifdef RAM_CODE
extern unsigned int _start_text;
static volatile const uint32_t __attribute__((used)) wolfboot_version = WOLFBOOT_VERSION;
extern void (** const IV_RAM)(void);
#endif

#ifdef PLATFORM_sim
extern char **main_argv;
extern int main_argc;
int main(int argc, char *argv[])
#elif defined(WOLFBOOT_LOADER_MAIN)
int loader_main(void)
#else
int main(void)
#endif
{

#ifdef PLATFORM_sim
    /* to forward arguments to the test-app for testing. See
     * test-app/app_sim.c */
    main_argv = argv;
    main_argc = argc;
#endif

    hal_init();
    spi_flash_probe();
#ifdef UART_FLASH
    uart_init(UART_FLASH_BITRATE, 8, 'N', 1);
    uart_send_current_version();
#endif
#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_init();
#endif

    wolfBoot_start();

    /* wolfBoot_start should never return. */
    wolfBoot_panic();

    return 0;
}
