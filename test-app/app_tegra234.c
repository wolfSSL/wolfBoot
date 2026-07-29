/* app_tegra234.c
 *
 * Test bare-metal boot application
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

#include "wolfboot/wolfboot.h"

#ifdef TARGET_tegra234

#include "hal/tegra234.h"

/* Same TCU console the bootloader chain and wolfBoot use (see hal/tegra234.h).
 * The MMU is off here, so the mailbox register is driven directly. One byte
 * per message keeps this payload trivial. */
#define TCU_TX_MBOX  ((volatile uint32_t*)(uintptr_t)TEGRA_TCU_TX_MBOX)

/* Bounded spin: no timer is set up in this payload, so a plain counter is
 * enough to keep a stalled SPE from hanging the PoC mid-message. */
#define TCU_TX_SPIN_MAX  1000000

static void tcu_putc(char c)
{
    unsigned int spin = 0;

    while ((*TCU_TX_MBOX & TEGRA_TCU_MBOX_FULL) != 0) {
        if (++spin > TCU_TX_SPIN_MAX)
            return; /* drop the character rather than stall forever */
    }
    *TCU_TX_MBOX = TEGRA_TCU_MBOX_FULL |
        (1u << TEGRA_TCU_MBOX_NBYTES_SHIFT) | (uint8_t)c;
}

static void tcu_puts(const char* s)
{
    while (*s != '\0') {
        if (*s == '\n')
            tcu_putc('\r');
        tcu_putc(*s++);
    }
}

static void tcu_puthex(uint64_t v)
{
    int i;
    tcu_puts("0x");
    for (i = 60; i >= 0; i -= 4)
        tcu_putc("0123456789abcdef"[(v >> i) & 0xF]);
}

/* FDT magic 0xd00dfeed, stored big-endian -> reads as 0xedfe0dd0 here */
#define FDT_MAGIC_LE 0xedfe0dd0u

/* The DTB pointer arrives as the first argument: wolfBoot's el2_to_el1_boot()
 * leaves it in x0 per the arm64 boot protocol, and boot_arm64_start.S forwards
 * it to main per AAPCS. Taking it as a parameter rather than reading x0 with
 * inline asm keeps this correct at any optimization level. */
void __attribute__((section(".boot"))) main(uint64_t dtb) {
    uint64_t el;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    el >>= 2;

    tcu_puts("\n*** wolfBoot Tegra234 PoC payload: verified + booted ***\n");

    /* Prove the EL2->EL1 drop happened, and that a valid FDT arrived in x0:
     * the full arm64 boot contract for a signed payload. */
    tcu_puts("  CurrentEL = EL");
    tcu_putc('0' + (char)(el & 3));
    tcu_puts("\n");

    tcu_puts("  DTB (x0)  = ");
    tcu_puthex(dtb);
    tcu_puts("\n  DTB magic = ");
    /* Range- and alignment-check before dereferencing: with the MMU off this
     * is Device memory, where an unaligned access faults. A stray pointer
     * should be reported, not taken. */
    if (dtb < TEGRA234_DRAM_BASE || dtb > (TEGRA234_DRAM_END - 4) ||
            (dtb & 0x3) != 0) {
        tcu_puts("(pointer not in DRAM or misaligned)\n");
    }
    else if (*((volatile uint32_t*)(uintptr_t)dtb) == FDT_MAGIC_LE) {
        tcu_puts("OK (0xd00dfeed)\n");
    }
    else {
        tcu_puthex(*((volatile uint32_t*)(uintptr_t)dtb));
        tcu_puts(" (BAD)\n");
    }

    tcu_puts("*** PoC complete: parked ***\n");

    /* Wait for reboot */
    while(1)
        ;
}
#endif /** TARGET_tegra234 **/
