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

#if defined(WOLFBOOT_PARTION_INFO) && defined(PRINTF_ENABLED)

#include <printf.h>

static void printPart(uint8_t *part)
{
#ifdef WOLFBOOT_PARTION_VERBOS
    uint32_t *v;
    int i;
#endif
    uint8_t  *magic;
    uint8_t  state;
    uint32_t ver;

    magic = part;
    wolfBoot_printf("Magic:    %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    ver = wolfBoot_get_blob_version(part);
    wolfBoot_printf("Version:  %02x\n", ver);
    state = *(part + WOLFBOOT_PARTITION_SIZE - sizeof(uint32_t) - 1);
    wolfBoot_printf("Status:   %02x\n", state);
    magic = part + WOLFBOOT_PARTITION_SIZE - sizeof(uint32_t);
    wolfBoot_printf("Tail Mgc: %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);

#ifdef WOLFBOOT_PARTIION_VERBOS
    v = (uint32_t *)part;
    for(i = 0; i < 0x100/4; i++) {
        if(i % 4 == 0)
            wolfBoot_printf("\n%08x: ", (uint32_t)v+i*4);
        wolfBoot_printf("%08x ", v[i]);
    }

    wolfBoot_printf("\n\nImage:");

    for( ; i < 0x100/4 + 16; i++) {
        if(i % 4 == 0)
            wolfBoot_printf("\n%08x: ", (uint32_t)v+i*4);
        wolfBoot_printf("%08x ", v[i]);
    }

    wolfBoot_printf("\n\n");
#endif

}


static void printPartitions(void)
{
    wolfBoot_printf("\n=== Boot Partition[%08x] ===\n", WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printPart((uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS);
    wolfBoot_printf("\n=== Update Partition[%08x] ===\n", WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printPart((uint8_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
}
#endif

#ifdef PLATFORM_sim
extern char **main_argv;
extern int main_argc;
int main(int argc, char *argv[])
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

#if defined(WOLFBOOT_PARTION_INFO) && defined(PRINTF_ENABLED)
    printPartitions();
#endif
    wolfBoot_start();

    /* wolfBoot_start should never return. */
    wolfBoot_panic();

    return 0;
}
