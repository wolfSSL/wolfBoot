/* bios.c
 *
 * BIOS services for the x86_64_lilo target: disk, keyboard, video, memory.
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

#include "x86/bios.h"

/* BIOS Data Area */
#define BDA_KBD_FLAGS   0x417
#define BDA_TICKS       0x46C

/* Keep GCC from treating fixed low addresses as null-page accesses. */
static inline volatile void *phys(uintptr_t addr)
{
    volatile void *p = (volatile void *)addr;
    __asm__ ("" : "+r"(p));
    return p;
}

int bios_disk_read(uint8_t drive, uint64_t lba, uint16_t count, void *buf)
{
    struct bios_dap {
        uint8_t  size;
        uint8_t  zero;
        uint16_t count;
        uint16_t off;
        uint16_t seg;
        uint64_t lba;
    } __attribute__((packed));
    static struct bios_dap dap;
    struct bios_regs r;
    int tries;

    if (((uintptr_t)buf + (uint32_t)count * 512U) > BIOS_LOWMEM_LIMIT)
        return -1;
    /* Floppy-era BIOSes reset the drive and retry; so does LILO. */
    for (tries = 0; tries < 3; tries++) {
        dap.size = sizeof(dap);
        dap.zero = 0;
        dap.count = count;
        dap.off = BIOS_OFF(buf);
        dap.seg = BIOS_SEG(buf);
        dap.lba = lba;
        memset(&r, 0, sizeof(r));
        r.eax = 0x4200;
        r.edx = drive;
        r.esi = BIOS_OFF(&dap);
        r.ds = BIOS_SEG(&dap);
        bios_int(0x13, &r);
        if ((r.flags & BIOS_FLAG_CF) == 0 && dap.count == count)
            return 0;
        memset(&r, 0, sizeof(r));
        r.edx = drive;
        bios_int(0x13, &r);
    }
    return -1;
}

void bios_putc(char c)
{
    struct bios_regs r;

    memset(&r, 0, sizeof(r));
    r.eax = 0x0E00 | (uint8_t)c;
    r.ebx = 0x0007;
    bios_int(0x10, &r);
}

int bios_getkey(void)
{
    struct bios_regs r;

    memset(&r, 0, sizeof(r));
    r.eax = 0x0100;
    bios_int(0x16, &r);
    if (r.flags & BIOS_FLAG_ZF)
        return -1;
    memset(&r, 0, sizeof(r));
    bios_int(0x16, &r);
    return (int)(r.eax & 0xFF);
}

/* Shift, Ctrl, Alt, Scroll Lock or Caps Lock: what LILO checks for. */
int bios_shift_pressed(void)
{
    return (*(volatile uint8_t *)phys(BDA_KBD_FLAGS) & 0x5F) != 0;
}

uint32_t bios_ticks(void)
{
    return *(volatile uint32_t *)phys(BDA_TICKS);
}

/* Low memory in bytes (INT 12h), the ceiling for real-mode payloads. */
uint32_t bios_lowmem_top(void)
{
    struct bios_regs r;

    memset(&r, 0, sizeof(r));
    bios_int(0x12, &r);
    return (r.eax & 0xFFFF) * 1024U;
}

/* End of the usable RAM range that starts at 1 MB, capped below 4 GB. */
uint32_t bios_mem_top(void)
{
    struct e820_entry {
        uint64_t base;
        uint64_t len;
        uint32_t type;
        uint32_t ext;
    } __attribute__((packed));
    static struct e820_entry e;
    struct bios_regs r;
    uint32_t cont = 0;
    uint64_t top = 0;
    int i;

    for (i = 0; i < 128; i++) {
        memset(&r, 0, sizeof(r));
        memset(&e, 0, sizeof(e));
        e.ext = 1;
        r.eax = 0xE820;
        r.edx = 0x534D4150; /* "SMAP" */
        r.ecx = sizeof(e);
        r.ebx = cont;
        r.edi = BIOS_OFF(&e);
        r.es = BIOS_SEG(&e);
        bios_int(0x15, &r);
        if ((r.flags & BIOS_FLAG_CF) || r.eax != 0x534D4150)
            break;
        if (e.type == 1 && (r.ecx < 24 || (e.ext & 1)) &&
                e.base <= 0x100000ULL && e.base + e.len > 0x100000ULL)
            top = e.base + e.len;
        cont = r.ebx;
        if (cont == 0)
            break;
    }
    if (top == 0) {
        /* No E820: E801 reports KB up to 16 MB and 64 KB blocks above */
        memset(&r, 0, sizeof(r));
        r.eax = 0xE801;
        bios_int(0x15, &r);
        if ((r.flags & BIOS_FLAG_CF) == 0) {
            uint32_t kb_low = r.eax & 0xFFFF;
            uint32_t blk_high = r.ebx & 0xFFFF;
            if (kb_low == 0 && blk_high == 0) {
                kb_low = r.ecx & 0xFFFF;
                blk_high = r.edx & 0xFFFF;
            }
            if (kb_low >= 15 * 1024 && blk_high != 0)
                top = 0x1000000ULL + (uint64_t)blk_high * 0x10000ULL;
            else
                top = 0x100000ULL + (uint64_t)kb_low * 1024ULL;
        }
    }
    if (top > 0xFFFFF000ULL)
        top = 0xFFFFF000ULL;
    return (uint32_t)top;
}
