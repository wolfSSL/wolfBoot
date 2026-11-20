/* x86_64_lilo.c
 *
 * HAL for the x86_64_lilo target: legacy BIOS/MBR boot, no EFI, no FSP.
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

#include "hal.h"
#include "disk.h"
#include "printf.h"
#include "x86/bios.h"

/* Console: BIOS teletype plus an 8250 UART driven directly. Output always
 * goes to both; serial input is accepted only once the map enables it, as
 * a floating UART reads back as a stream of 0xFF. */

#define UART_THR 0
#define UART_RBR 0
#define UART_DLL 0
#define UART_DLH 1
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_SCR 7
#define UART_LSR_DR   0x01
#define UART_LSR_THRE 0x20

static inline void io_write8(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t io_read8(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static const uint16_t uart_ports[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
static uint16_t uart_base;
static int uart_input;
static char last_out;

static int serial_setup(uint16_t base, uint32_t baud)
{
    uint16_t div;

    io_write8(base + UART_SCR, 0x5A);
    if (io_read8(base + UART_SCR) != 0x5A)
        return -1;
    if (baud == 0 || baud > 115200)
        baud = 115200;
    div = (uint16_t)(115200 / baud);
    io_write8(base + UART_IER, 0x00);
    io_write8(base + UART_LCR, 0x80);
    io_write8(base + UART_DLL, (uint8_t)div);
    io_write8(base + UART_DLH, (uint8_t)(div >> 8));
    io_write8(base + UART_LCR, 0x03); /* 8N1 */
    io_write8(base + UART_FCR, 0x07);
    io_write8(base + UART_MCR, 0x03);
    return 0;
}

static void serial_putc(char c)
{
    int spin = 100000;

    if (uart_base == 0)
        return;
    while ((io_read8(uart_base + UART_LSR) & UART_LSR_THRE) == 0 && --spin)
        ;
    io_write8(uart_base + UART_THR, (uint8_t)c);
}

static void console_putc(char c)
{
    if (c == '\n' && last_out != '\r') {
        serial_putc('\r');
        bios_putc('\r');
    }
    serial_putc(c);
    bios_putc(c);
    last_out = c;
}

void uart_init(void)
{
    if (serial_setup(uart_ports[0], 115200) == 0)
        uart_base = uart_ports[0];
}

void uart_write(const char *buf, unsigned int len)
{
    while (len--)
        console_putc(*buf++);
}

void hal_console_serial(int port, uint32_t baud)
{
    if (port < 0 || port > 3)
        return;
    if (serial_setup(uart_ports[port], baud) == 0) {
        uart_base = uart_ports[port];
        uart_input = 1;
    }
}

int hal_console_getc(void)
{
    uint8_t lsr;
    int c = bios_getkey();

    if (c >= 0)
        return c;
    if (uart_input && uart_base != 0) {
        lsr = io_read8(uart_base + UART_LSR);
        if (lsr != 0xFF && (lsr & UART_LSR_DR))
            return io_read8(uart_base + UART_RBR);
    }
    return -1;
}

void hal_init(void)
{
    uart_init();
    /* Third LILO letter: the second stage is running. */
    console_putc('L');
}

void hal_prepare_boot(void)
{
}

/* Disk: drive 0 is the BIOS boot drive. Every transfer goes through a low
 * memory bounce buffer, since the BIOS only reaches the first megabyte;
 * aligning it to its size keeps any transfer off a 64 KB boundary. */
#define BOUNCE_SECTORS 64
static uint8_t bounce[BOUNCE_SECTORS * 512]
    __attribute__((aligned(BOUNCE_SECTORS * 512)));

int disk_init(int drv)
{
    return (drv == 0) ? 0 : -1;
}

int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    uint32_t done = 0;

    if (drv != 0)
        return -1;
    while (done < count) {
        uint64_t off = start + done;
        uint32_t skip = (uint32_t)(off & 511U);
        uint32_t left = count - done;
        uint32_t n = (skip + left + 511U) >> 9;
        uint32_t chunk;

        if (n > BOUNCE_SECTORS)
            n = BOUNCE_SECTORS;
        if (bios_disk_read(bios_boot_drive, off >> 9, (uint16_t)n,
                    bounce) != 0) {
            wolfBoot_printf("BIOS disk read error at LBA %u\n",
                (unsigned int)(off >> 9));
            return -1;
        }
        chunk = n * 512U - skip;
        if (chunk > left)
            chunk = left;
        memcpy(buf + done, bounce + skip, chunk);
        done += chunk;
    }
    return 0;
}

/* Read-only: nothing on the boot path writes to the disk. */
int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv;
    (void)start;
    (void)count;
    (void)buf;
    return -1;
}

void disk_close(int drv)
{
    (void)drv;
}

/* No flash: images are read from the disk into RAM. */
int hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

void *hal_get_primary_address(void)
{
    return NULL;
}

void *hal_get_update_address(void)
{
    return NULL;
}

void *hal_get_dts_address(void)
{
    return NULL;
}

void *hal_get_dts_update_address(void)
{
    return NULL;
}
