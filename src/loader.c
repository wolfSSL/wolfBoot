/* loader.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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
/**
 * @file loader.c
 *
 * @brief Loader implementation for wolfBoot.
 *
 * This file contains the implementation of the loader for wolfBoot. It includes
 * functions to initialize the hardware, probe SPI flash,
 * initialize UART (if applicable), initialize TPM2 (if applicable), and
 * start the wolfBoot process.
 */

#include "loader.h"
#include "image.h"
#include "hal.h"
#include "spi_flash.h"
#include "uart_flash.h"
#include "wolfboot/wolfboot.h"

#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif

#ifdef RAM_CODE
/**
 * @brief Start address of the text section in RAM code.
 */
extern unsigned int _start_text;
/**
 * @brief wolfBoot version number (used in RAM code).
 */
static volatile const uint32_t __attribute__((used)) wolfboot_version = WOLFBOOT_VERSION;
/**
 * @brief RAM Interrupt Vector table.
 */
extern void (** const IV_RAM)(void);
#endif

#ifdef TARGET_sim
/**
 * @brief Command line arguments for the test-app in sim mode.
 */
extern char **main_argv;
/**
 * @brief Number of command line arguments for the test-app in sim mode.
 */
extern int main_argc;

/**
 * @brief Main function in sim platform.
 *
 * This function is the entry point for the simulator platform. It forwards the
 * command line arguments to the test-app for testing purposes.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line arguments.
 * @return The return code of the test-app.
 */
int main(int argc, char *argv[])
#elif defined(WOLFBOOT_LOADER_MAIN)
/**
 * @brief Main function for the wolfBoot loader.
 *
 * This function is the entry point for the wolfBoot loader. It initializes
 * the hardware, probes SPI flash, initializes UART (if applicable), initializes
 * TPM2 (if applicable), and starts the wolfBoot process.
 *
 * @return The return code of the wolfBoot process (should not return).
 */
int loader_main(void)
#else
int main(void)
#endif
{

#ifdef TARGET_sim
    /* to forward arguments to the test-app for testing. See
     * test-app/app_sim.c */
    main_argv = argv;
    main_argc = argc;
#endif

    hal_init();
#ifdef TEST_FLASH
    hal_flash_test();
#endif
    spi_flash_probe();
#ifdef UART_FLASH
    uart_init(UART_FLASH_BITRATE, 8, 'N', 1);
    uart_send_current_version();
#endif
#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_init();
#endif
#ifdef WOLFCRYPT_SECURE_MODE
    wcs_Init();
#endif
    wolfBoot_start();


    /* wolfBoot_start should never return. */
    wolfBoot_panic();

    return 0;
}
