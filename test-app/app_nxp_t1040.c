/* app_nxp_t1040.c
 *
 * Test bare-metal application for NXP T1040.
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

#include "target.h"
#include "wolfboot/wolfboot.h"

/* wait_ticks: spin for r3 ticks using PPC timebase.
 * Required by udelay() in nxp_ppc.c (linked via HAL). */
__asm__ (
    ".section .text.wait_ticks\n"
    ".global wait_ticks\n"
    "wait_ticks:\n"
    "    mflr    8\n"
    "    mftbu   5\n"
    "    mftbl   4\n"
    "    addc    7, 4, 3\n"
    "    addze   6, 5\n"
    "1:  mftbu   5\n"
    "    mftbl   4\n"
    "    subfc   4, 4, 7\n"
    "    subfe.  5, 5, 6\n"
    "    bge     1b\n"
    "    mtlr    8\n"
    "    blr\n"
);

/* Assembly entry point: set stack pointer, enable FPU, and call main.
 * The linker script defines _stack_top at the end of the stack region.
 * PPC ABI: r1 = stack pointer, 16-byte aligned, back-chain to 0.
 * FPU must be enabled because GCC's variadic function ABI (used by
 * uart_printf/wolfBoot_printf) saves FP registers via stfd instructions.
 * Without MSR[FP], this causes a Floating-Point Unavailable exception. */
__asm__ (
    ".section .text._app_entry\n"
    ".global _app_entry\n"
    "_app_entry:\n"
    "    lis    1, _stack_top@ha\n"
    "    addi   1, 1, _stack_top@l\n"
    "    li     0, 0\n"
    "    stwu   0, -16(1)\n"
    /* Enable FPU: set MSR[FP] (bit 18) = 0x2000 */
    "    mfmsr  0\n"
    "    ori    0, 0, 0x2000\n"
    "    mtmsr  0\n"
    "    isync\n"
    "    b      main\n"
);
#include "printf.h"
#include "keystore.h"

#include "../hal/nxp_ppc.h"

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
    wolfBoot_printf("Boot partition version: %lu\r\n",
        wolfBoot_current_firmware_version());
    wolfBoot_printf("Update partition version: %lu\r\n",
        wolfBoot_update_firmware_version());
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
     * Without this, static variables contain DDR garbage. */
    extern char _start_bss[], _end_bss[];
    {
        char *p = _start_bss;
        while (p < _end_bss)
            *p++ = 0;
    }

    uart_init();

    wolfBoot_printf("========================\r\n");
    wolfBoot_printf("NXP T1040 wolfBoot demo Application\r\n");
    wolfBoot_printf("Copyright 2025 wolfSSL Inc\r\n");
    wolfBoot_printf("GPL v3\r\n");
    wolfBoot_printf("========================\r\n");

    print_info();

    /* Mark boot partition as successful */
    wolfBoot_success();
    wolfBoot_printf("\r\nBoot partition marked successful\r\n");

    wolfBoot_printf("Test App: idle loop\r\n");
    while(1) {
        /* Idle */
    }
}
