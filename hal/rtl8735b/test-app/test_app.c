/* test_app.c
 *
 * Minimal RTL8735B (AmebaPro2) bare-metal application for validating the
 * wolfBoot RTL8735B port (Model A: verify -> copy-to-DDR -> jump).
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *
 * wolfBoot (in SRAM) verifies this image in a partition, copies it to
 * WOLFBOOT_LOAD_ADDRESS (0x70100000 in DDR), then do_boot() sets VTOR to that
 * base, loads MSP from word[0] and branches to word[1]. This app prints a
 * banner on UART1 (left initialized by wolfBoot -- 115200 8N1) and spins.
 *
 * Build (ASDK 10.3.0 arm-none-eabi-gcc), default version 1:
 *   arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 \
 *       -mfloat-abi=softfp -Os -ffreestanding -nostdlib -nostartfiles \
 *       -DAPP_VERSION=1 -T test_app.ld test_app.c -o test_app.elf
 *   arm-none-eabi-objcopy -O binary test_app.elf test_app.bin
 * Then sign and stage it -- see hal/rtl8735b/README and docs/Targets.md.
 */
#include <stdint.h>

#ifndef APP_VERSION
#define APP_VERSION 1
#endif

/* UART1 (RealTek LOGUART) registers, same base wolfBoot uses. */
#define UART_THR     (*(volatile uint32_t *)0x40040424u)  /* TX holding    */
#define UART_TFLVR   (*(volatile uint32_t *)0x40040454u)  /* tx_fifo_lv     */
#define UART_TX_FIFO 16u

/* Vendor watchdog: the RealTek boot ROM arms the secure vendor WDT (VNDR_S base
 * 0x50002C00, VNDR_S_REG_SECURE_WATCH_DOG_TIMER at offset 0x000) before handing
 * off to wolfBoot, and it resets the SoC if not serviced. wolfBoot leaves it
 * running (the application owns the watchdog), so an app that does not service
 * it reboots after the timeout. Bit 24 (WDT_CLEAR, write-1-pulse) reloads the
 * counter; pet it with a read-modify-write so the ROM-set enable/mode/divfactor
 * bits are preserved. A real application would service or reconfigure it. */
#define VNDR_S_WDT       (*(volatile uint32_t *)0x50002C00u)
#define VNDR_S_WDT_CLEAR (1u << 24)

extern uint32_t _stack_top;     /* provided by the linker script */
void reset_handler(void);
static void default_handler(void);

/* Cortex-M33 vector table at the image base. wolfBoot's do_boot sets VTOR here,
 * loads MSP from word[0], and branches to word[1] (Reset). The remaining core
 * exception vectors point at a spin so a stray fault traps here visibly instead
 * of fetching a garbage handler address from .text. This banner-and-spin demo
 * never enables interrupts, so the external IRQ vectors are omitted on purpose;
 * extend the table if you add interrupt-driven code. */
__attribute__((section(".vectors"), used))
const uint32_t vectors[16] = {
    (uint32_t)&_stack_top,      /*  0: initial MSP  */
    (uint32_t)reset_handler,    /*  1: Reset        */
    (uint32_t)default_handler,  /*  2: NMI          */
    (uint32_t)default_handler,  /*  3: HardFault    */
    (uint32_t)default_handler,  /*  4: MemManage    */
    (uint32_t)default_handler,  /*  5: BusFault     */
    (uint32_t)default_handler,  /*  6: UsageFault   */
    (uint32_t)default_handler,  /*  7: SecureFault  */
    0u, 0u, 0u,                 /*  8-10: reserved  */
    (uint32_t)default_handler,  /* 11: SVCall       */
    (uint32_t)default_handler,  /* 12: DebugMonitor */
    0u,                         /* 13: reserved     */
    (uint32_t)default_handler,  /* 14: PendSV       */
    (uint32_t)default_handler   /* 15: SysTick      */
};

static void default_handler(void)
{
    for (;;) {
        /* trap unexpected exceptions */
    }
}

static void uart_putc(char c)
{
    uint32_t timeout = 0;
    while ((UART_TFLVR & 0x1Fu) >= UART_TX_FIFO) {
        if (++timeout > 1000000u)
            break;
    }
    UART_THR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void reset_handler(void)
{
#if APP_VERSION >= 2
    uart_puts("\nwolfBoot test app v2: running from DDR at 0x70100000\n");
#else
    uart_puts("\nwolfBoot test app v1: running from DDR at 0x70100000\n");
#endif
    for (;;) {
        /* Pet the vendor watchdog so the boot-armed WDT does not reset us. */
        VNDR_S_WDT |= VNDR_S_WDT_CLEAR;
    }
}
