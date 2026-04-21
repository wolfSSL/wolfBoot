/* app_nxp_t2080.c
 *
 * Test bare-metal application for NXP T2080.
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

#include <stdint.h>
#include <string.h>

#include "wolfboot/wolfboot.h"

/* Assembly entry point: set stack pointer, enable FPU, and call main.
 * The linker script defines _stack_top at the end of the stack region.
 * PPC ABI: r1 = stack pointer, 16-byte aligned, back-chain to 0. */
void __attribute__((naked, section(".text._app_entry"))) _app_entry(void)
{
    __asm__ volatile (
        "lis    1, _stack_top@ha\n"
        "addi   1, 1, _stack_top@l\n"
        "li     0, 0\n"
        "stwu   0, -16(1)\n"   /* Create initial stack frame, back-chain = 0 */
        /* Enable FPU: set MSR[FP] (bit 18) = 0x2000.
         * Required for benchmark float formatting. */
        "mfmsr  0\n"
        "ori    0, 0, 0x2000\n"
        "mtmsr  0\n"
        "isync\n"
        "b      main\n"
    );
}
#include "target.h"
#include "printf.h"
#include "keystore.h"

#include "../hal/nxp_ppc.h"

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
    int i, j;
    uint32_t n_keys;

    wolfBoot_get_partition_state(PART_BOOT, &boot_part_state);
    wolfBoot_get_partition_state(PART_UPDATE, &update_part_state);

    wolfBoot_printf("\r\n");
    wolfBoot_printf("System information\r\n");
    wolfBoot_printf("====================================\r\n");
    wolfBoot_printf("Current firmware state: %s\r\n",
        part_state_name(boot_part_state));
    wolfBoot_printf("Update state: %s\r\n",
            part_state_name(update_part_state));


    wolfBoot_printf("\r\n");
    wolfBoot_printf("Bootloader keystore information\r\n");
    wolfBoot_printf("====================================\r\n");
    n_keys = keystore_num_pubkeys();
    wolfBoot_printf("Number of public keys: %lu\r\n", n_keys);
    for (i = 0; i < (int)n_keys; i++) {
        uint32_t size = keystore_get_size(i);
        uint32_t type = keystore_get_key_type(i);
        uint32_t mask = keystore_get_mask(i);
        uint8_t *keybuf = keystore_get_buffer(i);

        wolfBoot_printf("\r\n");
        wolfBoot_printf("  Public Key #%d: size %lu, type %lx, mask %08lx\r\n",
            i, size, type, mask);
        wolfBoot_printf("  ====================================\r\n  ");
        for (j = 0; j < (int)size; j++) {
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
    /* Zero BSS - required for bare-metal since there's no crt0 startup.
     * Without this, static variables (gTestMemory, HEAP_HINT, etc.)
     * contain DDR garbage, causing crashes in wc_LoadStaticMemory. */
    extern char _start_bss[], _end_bss[];
    {
        char *p = _start_bss;
        while (p < _end_bss)
            *p++ = 0;
    }

    uart_init();

    wolfBoot_printf("========================\r\n");
    wolfBoot_printf("NXP T2080 wolfBoot demo Application\r\n");
    wolfBoot_printf("Copyright 2026 wolfSSL Inc\r\n");
    wolfBoot_printf("GPL v3\r\n");
    wolfBoot_printf("========================\r\n");

    print_info();

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    wolfCrypt_Init();

#ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\r\nRunning wolfCrypt tests...\r\n");
    wolfcrypt_test(NULL);
    wolfBoot_printf("Tests complete.\r\n\r\n");
#endif

#ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("Running wolfCrypt benchmarks...\r\n");
    benchmark_test(NULL);
    wolfBoot_printf("Benchmarks complete.\r\n\r\n");
#endif

    wolfCrypt_Cleanup();
#endif

    wolfBoot_printf("Test App: idle loop\r\n");
    while(1) {
        /* Idle */
    }
}
