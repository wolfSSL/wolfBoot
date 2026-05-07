/* app_zynq7000.c
 *
 * Bare-metal Cortex-A9 test app for the Zynq-7000 ZC702. Prints a banner
 * on UART1 and a heartbeat character so the user can see do_boot() landed.
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

#ifdef TARGET_zynq7000

#define UART1_FIFO  (*(volatile uint32_t*)0xE0001030U)
#define UART1_SR    (*(volatile uint32_t*)0xE000102CU)
#define UART_SR_TXFULL  0x10U
#define UART_SR_TXEMPTY 0x08U

static void uart_putc(char c)
{
    while (UART1_SR & UART_SR_TXFULL)
        ;
    UART1_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

static void delay(volatile uint32_t n)
{
    while (n--) {
        __asm__ volatile("nop");
    }
}

/* Freestanding entry point - wolfBoot's do_boot bx's directly to
 * WOLFBOOT_LOAD_ADDRESS, which is the first byte of the raw .bin payload.
 * Pin main to a known input section (.boot_entry) and KEEP that section
 * first in the linker script (test-app/ARM-zynq7000.ld) so this function
 * is at offset 0 of the .bin regardless of compiler/LTO function
 * ordering. noreturn lets the compiler skip emitting a return path. */
__attribute__((section(".boot_entry"), noreturn))
int main(void)
{
    uart_puts("\n=== ZC702 test-app: BOOT OK ===\n");
    uart_puts("wolfBoot verified + chain-loaded this image\n");
    while (1) {
        uart_putc('.');
        delay(2000000);
    }
}

#endif /* TARGET_zynq7000 */
