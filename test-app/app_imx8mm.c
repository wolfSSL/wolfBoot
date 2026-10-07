/* app_imx8mm.c
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

#ifdef TARGET_imx8mm

#include "hal/imx8mm.h"

/* UART2 console, already set up by the earlier boot stages */
#define UART_REG(off) \
    (*(volatile uint32_t*)(uintptr_t)(IMX8MM_UART_BASE + (off)))

/* Bounded wait for TX FIFO space */
#define UART_TX_SPIN_MAX  1000000

static void uart_putc(char c)
{
    unsigned int spin = 0;

    while ((UART_REG(IMX8MM_UART_UTS) & UTS_TXFULL) != 0) {
        if (++spin > UART_TX_SPIN_MAX)
            return;
    }
    UART_REG(IMX8MM_UART_UTXD) = (uint32_t)(uint8_t)c;
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

/* FDT magic 0xd00dfeed read little-endian */
#define FDT_MAGIC_LE 0xedfe0dd0u

/* dtb: x0 from wolfBoot (arm64 boot protocol) */
void __attribute__((section(".boot"))) main(uint64_t dtb) {
    uint64_t el;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    el >>= 2;

    uart_puts("\n*** wolfBoot i.MX 8M Mini payload: verified + booted ***\n");

    uart_puts("  CurrentEL = EL");
    uart_putc('0' + (char)(el & 3));
    uart_puts("\n");

    uart_puts("  DTB (x0)  = ");
    uart_puthex(dtb);
    uart_puts("\n  DTB magic = ");
    /* MMU off: check range and alignment before reading */
    if (dtb < IMX8MM_DRAM_BASE || dtb > (IMX8MM_DRAM_END - 4) ||
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
#endif /** TARGET_imx8mm **/
