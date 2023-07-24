/* common.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define CPUID_EXTFEAT_LEAF01 0x80000001
#define CPUID_EDX_1GB_PAGE_SUPPORTED (0x1 << 26)

struct fit_table_entry
{
    uint64_t address;
    uint16_t size_split_16_lo;
    uint8_t size_split_16_hi;
    uint8_t reserved;
    uint16_t version;
    uint8_t type:7;
    uint8_t checksum_valid:1;
    uint8_t checksum;
} __attribute__((packed));

enum FIT_ENTRY_TYPES {
    FIT_ENTRY_FIT_HEADER = 0,
    FIT_ENTRY_UCODE_UPDATE = 1,
    FIT_ENTRY_TXT_POL_DATA_REC = 0x0a,
};

void mmio_write8(uintptr_t address, uint8_t value);
uint8_t mmio_read8(uintptr_t address);
uint16_t mmio_or16(uintptr_t address, uint16_t value);
void mmio_write16(uintptr_t address, uint16_t value);
uint16_t mmio_read16(uintptr_t address);
uint32_t mmio_or32(uintptr_t address, uint32_t value);
void mmio_write32(uintptr_t address, uint32_t value);
uint32_t mmio_read32(uintptr_t address);
void io_write8(uint16_t port, uint8_t value);
uint8_t io_read8(uint16_t port);
void io_write16(uint16_t port, uint16_t value);
uint16_t io_read16(uint16_t port);
void io_write32(uint16_t port, uint32_t value);
uint32_t io_read32(uint16_t port);
void reset(uint8_t warm);
void delay(int msec);
void panic();
void cpuid(uint32_t eax_param,
           uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
int cpuid_is_1gb_page_supported();
void switch_to_long_mode(uint64_t *entry, uint32_t page_table);
#endif /* COMMON_H */
