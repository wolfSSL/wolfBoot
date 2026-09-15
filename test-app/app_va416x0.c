/* app_va416x0.c
 *
 * Test bare-metal application.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "system.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"
#include "printf.h"
#include "keystore.h"

#include "../hal/va416x0.h"

/* wolfCrypt test/benchmark support */
#ifdef WOLFCRYPT_TEST
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif

#ifdef WOLFCRYPT_BENCHMARK
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

/* Vorago HAL includes */
#include "va416xx_hal.h"
#include "va416xx_hal_clkgen.h"
#include "va416xx_hal_irqrouter.h"
#include "va416xx_hal_timer.h"
#include "va416xx_hal_ioconfig.h"

/* Confirm VTOR points at the application's table and that exceptions reach it.
 * SysTick advances HAL_time_ms, so a frozen counter means they do not. */
static void print_vector_table_info(void)
{
    uint32_t vtor;
    uint32_t expect;
    uint32_t t0, t1;   /* short delta; keeps the print 32-bit */
    volatile uint32_t spin;

    vtor = SCB->VTOR;
    expect = (uint32_t)(WOLFBOOT_PARTITION_BOOT_ADDRESS + IMAGE_HEADER_SIZE);

    wolfBoot_printf("\r\n");
    wolfBoot_printf("Vector table\r\n");
    wolfBoot_printf("====================================\r\n");
    wolfBoot_printf("VTOR            : 0x%08lx (expected 0x%08lx) %s\r\n",
        (unsigned long)vtor, (unsigned long)expect,
        (vtor == expect) ? "OK" : "MISMATCH");

    /* The core ORs the vector offset into VTOR rather than adding it, so a
     * 212-entry table that is not 1024-aligned misdispatches every IRQ from
     * 112 up while SysTick and the EDAC IRQs still look healthy. */
    wolfBoot_printf("VT alignment    : %lu %s\r\n",
        (unsigned long)(vtor & 0x3FFU),
        ((vtor & 0x3FFU) == 0) ? "(1024-aligned) OK"
                               : "*** NOT 1024-ALIGNED: IRQ >= 112 broken ***");

    t0 = (uint32_t)HAL_time_ms;
    /* Bounded: HAL_Timer_DelayMs() spins on HAL_time_ms and would hang */
    for (spin = 0; spin < 2000000UL; spin++) {
    }
    t1 = (uint32_t)HAL_time_ms;

    wolfBoot_printf("SysTick         : %s (%lu -> %lu ms)\r\n",
        (t1 != t0) ? "ticking" : "STOPPED",
        (unsigned long)t0, (unsigned long)t1);
}

static uint8_t boot_part_state = IMG_STATE_NEW;
static uint8_t update_part_state = IMG_STATE_NEW;

const char part_state_names[6][16] = {
    "NEW",
    "UPDATING",
    "FFLAGS",
    "TESTING",
    "CONFIRMED",
    "[Invalid state]"
};

static const char *part_state_name(uint8_t state)
{
    switch(state) {
        case IMG_STATE_NEW:
            return part_state_names[0];
        case IMG_STATE_UPDATING:
            return part_state_names[1];
        case IMG_STATE_FINAL_FLAGS:
            return part_state_names[2];
        case IMG_STATE_TESTING:
            return part_state_names[3];
        case IMG_STATE_SUCCESS:
            return part_state_names[4];
        default:
            return part_state_names[5];
    }
}

static int print_info(void)
{
    uint32_t i, j;
    uint32_t cur_fw_version, update_fw_version;
    uint32_t n_keys;

    cur_fw_version = wolfBoot_current_firmware_version();
    update_fw_version = wolfBoot_update_firmware_version();

    wolfBoot_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_get_partition_state(PART_UPDATE, &update_part_state);

    wolfBoot_printf("\r\n");
    wolfBoot_printf("System information\r\n");
    wolfBoot_printf("====================================\r\n");
    wolfBoot_printf("Firmware version : 0x%lx\r\n", wolfBoot_current_firmware_version());
    wolfBoot_printf("Current firmware state: %s\r\n", part_state_name(boot_part_state));
    if (update_fw_version != 0) {
        if (update_part_state == IMG_STATE_UPDATING)
            wolfBoot_printf("Candidate firmware version : 0x%lx\r\n", update_fw_version);
        else
            wolfBoot_printf("Backup firmware version : 0x%lx\r\n", update_fw_version);
        wolfBoot_printf("Update state: %s\r\n", part_state_name(update_part_state));
        if (update_fw_version > cur_fw_version) {
            wolfBoot_printf("'reboot' to initiate update.\r\n");
        } else {
            wolfBoot_printf("Update image older than current.\r\n");
        }
    } else {
        wolfBoot_printf("No image in update partition.\r\n");
    }

    wolfBoot_printf("\r\n");
    wolfBoot_printf("Bootloader OTP keystore information\r\n");
    wolfBoot_printf("====================================\r\n");
    n_keys = keystore_num_pubkeys();
    wolfBoot_printf("Number of public keys: %lu\r\n", n_keys);
    for (i = 0; i < n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint32_t mask = keystore_get_mask(i);
        uint8_t *keybuf = keystore_get_buffer(i);

        wolfBoot_printf("\r\n");
        wolfBoot_printf("  Public Key #%lu: size %lu, type %lx, mask %08lx\r\n",
                (unsigned long)i, size, type, mask);
        wolfBoot_printf("  ====================================\r\n  ");
        for (j = 0; j < size; j++) {
            wolfBoot_printf("%02X ", keybuf[j]);
            if (j % 16 == 15) {
                wolfBoot_printf("\r\n  ");
            }
        }
        wolfBoot_printf("\r\n");
    }
    return 0;
}

void main(void)
{
    uint32_t app_version;

    hal_init();

    /* Turn on boot LED */
    EVK_LED2_BANK.SETOUT |= 1<<EVK_LED2_PIN;

    app_version = wolfBoot_current_firmware_version();

    wolfBoot_printf("========================\r\n");
    wolfBoot_printf("VA416x0 wolfBoot demo Application\r\n");
    wolfBoot_printf("Copyright 2025 wolfSSL Inc\r\n");
    wolfBoot_printf("GPL v3\r\n");
    wolfBoot_printf("Version : 0x%lx\r\n", app_version);
    wolfBoot_printf("========================\r\n");

    print_info();

    print_vector_table_info();

#ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\r\nRunning wolfCrypt tests...\r\n");
    wolfCrypt_Init();
    wolfcrypt_test(NULL);
    wolfCrypt_Cleanup();
    wolfBoot_printf("Tests complete.\r\n\r\n");
#endif

#ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("Running wolfCrypt benchmarks...\r\n");
    wolfCrypt_Init();
    benchmark_test(NULL);
    wolfCrypt_Cleanup();
    wolfBoot_printf("Benchmarks complete.\r\n\r\n");
#endif

    if (app_version > 1) {
        /* Turn on update LED */
        EVK_LED4_BANK.SETOUT |= 1<<EVK_LED4_PIN;

        if (boot_part_state == IMG_STATE_TESTING) {
            wolfBoot_printf("Booting new firmware, marking successful boot\n");

            /* Mark successful boot, so update won't be rolled back */
            wolfBoot_success();
        }
    }

    while(1) {
        /* tickle watchdog */
        WDFEED();
    }
}
