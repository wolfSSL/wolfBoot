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
 * along with this program; if not, write to the Free Softwar
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

void trigger_setup(void);

int main(void)
{
    hal_init();
    // volatile uint32_t k = 0;
    // for (volatile uint32_t i = 0; i < 50; i++) {
    //     for (volatile uint32_t j = 0; j < 50; j++) {
    //         k++;
    //     }
    // }
    // for (volatile uint32_t i = 0; i < 50; i++) {
    //     for (volatile uint32_t j = 0; j < 50; j++) {
    //         k++;
    //     }
    // }
    // for (volatile uint32_t i = 0; i < 50; i++) {
    //     for (volatile uint32_t j = 0; j < 50; j++) {
    //         k++;
    //     }
    // }
    wolfBoot_update_trigger();
    trigger_setup();
    // while (1) {
    //     trigger_high();
    //     volatile uint32_t k = 0;
    //     for (volatile uint32_t i = 0; i < 50; i++) {
    //         for (volatile uint32_t j = 0; j < 50; j++) {
    //             k++;
    //         }
    //     }
    //     if (k == 2500) {
    //         trigger_low();
    //     } else {
    //         while(1);
    //     }
    //     for (volatile uint32_t i = 0; i < 50; i++) {
    //         for (volatile uint32_t j = 0; j < 50; j++) {
    //             k++;
    //         }
    //     }
    // }
#ifdef UART_FLASH
    uart_init(UART_FLASH_BITRATE, 8, 'N', 1);
    uart_send_current_version();
#endif
#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_init();
#endif
    wolfBoot_start();
    while(1)
        ;
    return 0;
}
