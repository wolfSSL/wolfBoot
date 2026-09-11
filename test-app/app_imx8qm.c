/* app_imx8qm.c
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

#ifdef TARGET_imx8qm

#include "hal/imx8qm.h"

/* Same LPUART0 console the bootloader chain and wolfBoot use (see
 * hal/imx8qm.h). The MMU is off here, so the registers are driven directly,
 * and the baud rate set up by SCFW/ATF is left alone. */
#define LPUART_REG(off) \
    (*(volatile uint32_t*)(uintptr_t)(IMX8QM_LPUART0_BASE + (off)))

/* Bounded spin: no timer is set up in this payload, so a plain counter is
 * enough to keep a stalled console from hanging the app mid-message. */
#define LPUART_TX_SPIN_MAX  1000000

static void uart_putc(char c)
{
    unsigned int spin = 0;

    while ((LPUART_REG(LPUART_STAT) & LPUART_STAT_TDRE) == 0) {
        if (++spin > LPUART_TX_SPIN_MAX)
            return; /* drop the character rather than stall forever */
    }
    LPUART_REG(LPUART_DATA) = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char* s)
{
    while (*s != '\0') {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_puthex(uint64_t v)
{
    int i;
    uart_puts("0x");
    for (i = 60; i >= 0; i -= 4)
        uart_putc("0123456789abcdef"[(v >> i) & 0xF]);
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

    uart_puts("\n*** wolfBoot i.MX 8QuadMax payload: verified + booted ***\n");

    /* Prove which EL the payload was entered at, and that a valid FDT arrived
     * in x0: the full arm64 boot contract for a signed payload. */
    uart_puts("  CurrentEL = EL");
    uart_putc('0' + (char)(el & 3));
    uart_puts("\n");

    uart_puts("  DTB (x0)  = ");
    uart_puthex(dtb);
    uart_puts("\n  DTB magic = ");
    /* Range- and alignment-check before dereferencing: with the MMU off this
     * is Device memory, where an unaligned access faults. A stray pointer
     * should be reported, not taken. */
    if (dtb < IMX8QM_DRAM_BASE || dtb > (IMX8QM_DRAM_END - 4) ||
            (dtb & 0x3) != 0) {
        uart_puts("(pointer not in DRAM or misaligned)\n");
    }
    else if (*((volatile uint32_t*)(uintptr_t)dtb) == FDT_MAGIC_LE) {
        uart_puts("OK (0xd00dfeed)\n");
    }
    else {
        uart_puthex(*((volatile uint32_t*)(uintptr_t)dtb));
        uart_puts(" (BAD)\n");
    }

    uart_puts("*** payload complete: parked ***\n");

    /* Wait for reboot */
    while(1)
        ;
}
#endif /** TARGET_imx8qm **/
