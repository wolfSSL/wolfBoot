/* bios.h
 *
 * Real-mode BIOS services for the x86_64_lilo target (legacy MBR boot).
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

#ifndef WOLFBOOT_X86_BIOS_H
#define WOLFBOOT_X86_BIOS_H

#include <stdint.h>

/* Register image exchanged with a real-mode interrupt. The layout is shared
 * with src/boot_x86_bios_start.S. */
struct bios_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint16_t ds;
    uint16_t es;
    uint16_t flags;
    uint16_t pad;
} __attribute__((packed));

#define BIOS_FLAG_CF 0x0001
#define BIOS_FLAG_ZF 0x0040

/* Final real-mode handoff (Linux real-mode setup, chain-loaded boot sector).
 * Layout shared with src/boot_x86_bios_start.S. */
struct bios_jump {
    uint16_t cs;
    uint16_t ip;
    uint16_t ds;   /* also loaded into es, fs, gs */
    uint16_t ss;
    uint16_t sp;
    uint16_t dx;
    uint16_t si;
    uint16_t pad;
} __attribute__((packed));

/* Every buffer handed to the BIOS must be addressable from real mode. */
#define BIOS_LOWMEM_LIMIT 0x100000UL
#define BIOS_SEG(addr) ((uint16_t)(((uintptr_t)(addr)) >> 4))
#define BIOS_OFF(addr) ((uint16_t)(((uintptr_t)(addr)) & 0xF))

/* Drive number the BIOS booted from (DL at MBR entry). */
extern uint8_t bios_boot_drive;

/* Copy of the MBR boot descriptor, taken before 0x7C00 can be reused. */
extern uint8_t bios_mbr_desc[];

void bios_int(uint8_t intno, struct bios_regs *regs);
void __attribute__((noreturn)) bios_realmode_jump(const struct bios_jump *j);

/* src/x86/bios.c */
int bios_disk_read(uint8_t drive, uint64_t lba, uint16_t count, void *buf);
void bios_putc(char c);
int bios_getkey(void);
int bios_shift_pressed(void);
uint32_t bios_ticks(void);
uint32_t bios_lowmem_top(void);
uint32_t bios_mem_top(void);

/* hal/x86_64_lilo.c */
void hal_console_serial(int port, uint32_t baud);
int hal_console_getc(void);

#endif /* WOLFBOOT_X86_BIOS_H */
