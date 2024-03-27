/* pci.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#include <pci.h>
#include <printf.h>
#include <x86/common.h>

#ifdef DEBUG_PCI
#define PCI_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define PCI_DEBUG_PRINTF(...) do {} while(0)
#endif /* DEBUG_PCI */

/* TODO: recover from BAR and set after FSP-T */
#ifdef PCH_HAS_PCR
#ifndef PCH_PCR_BASE
#error "define PCH_PCR_BASE"
#endif
#endif /* PCH_HAS_PCR */

#ifdef PCI_USE_ECAM
#ifndef PCI_ECAM_BASE
#error "define PCI_ECAM_BASE"
#endif
#endif /* PCI_USE_ECAM */

#define PCI_ADDR_16BIT_ALIGNED_MASK (0x1)
#define PCI_ADDR_32BIT_ALIGNED_MASK (0x3)
#define PCI_DATA_LO16_MASK (0xffff)
#define PCI_DATA_HI16_MASK (0xffff0000)
#define PCI_DATA_HI16_SHIFT (16)

/* PCI address space starts at 0x80000000 (2GB), on TGL ECAM starts at
 * 0xC0000000 (3GB). So there is 1 GB under the 4GB boundary available. By
 * default allocate 128MB for normal memory and 896MB for prefetchable
 * memory. */
#ifndef PCI_MMIO32_BASE
#define PCI_MMIO32_BASE 0x80000000ULL
#define PCI_MMIO32_LENGTH (128U * 1024U * 1024U)
#endif /* PCI_MMIO32_BASE */

#ifndef PCI_MMIO32_PREFETCH_BASE
#define PCI_MMIO32_PREFETCH_BASE (PCI_MMIO32_BASE + PCI_MMIO32_LENGTH)
#define PCI_MMIO32_PREFETCH_LENGTH (896U * 1024U * 1024U)
#endif /* PCI_MMIO32_PREFETCH_BASE */

#ifndef PCI_IO32_BASE
#define PCI_IO32_BASE 0x2000
#endif /* PCI_IO32_BASE */

#define PCI_ENUM_MAX_DEV  32
#define PCI_ENUM_MAX_FUN  8
#define PCI_ENUM_MAX_BARS 6

#define PCI_ENUM_MMIND_MASK   (0x1)
#define PCI_ENUM_TYPE_MASK    (0x1 << 1 | 0x1 << 2)
#define PCI_ENUM_TYPE_SHIFT   1
#define PCI_ENUM_TYPE_64bit   (0x1 << 1)
#define PCI_ENUM_TYPE_32bit   (0x1)
#define PCI_ENUM_IS_PREFETCH  (0x1 << 3)
#define PCI_ENUM_MM_BAR_MASK ~(0xf)
#define PCI_ENUM_IO_BAR_MASK ~(0x3)

#define CAPID0_A_0_0_0_PCI (0xE4)
#define DEVICE_ENABLE (0x54)
#define DTT_DEVICE_DISABLE (1 << 15)
#define ONE_MB (1024 * 1024)
#define FOUR_KB (4 * 1024)

static int pci_enum_is_64bit(uint32_t value);
static int pci_enum_is_mmio(uint32_t value);

static inline uint32_t align_up(uint32_t address, uint32_t alignment) {
    return (address + alignment - 1) & ~(alignment - 1);
}

static inline uint32_t align_down(uint32_t address, uint32_t alignment) {
    return address & ~(alignment - 1);
}

static int pci_align_check_up(uint32_t address, uint32_t alignment,
                              uint32_t limit, uint32_t *aligned)
{
    uint32_t a;
    a = align_up(address, alignment);
    if (a < address || a >= limit)
        return -1;
    *aligned = a;
    return 0;
}

#ifdef PCH_HAS_PCR

static uint32_t pch_make_address(uint8_t port_id, uint16_t offset)
{
    return (PCH_PCR_BASE + (port_id << 16 | offset));
}

uint32_t pch_read32(uint8_t port_id, uint16_t offset)
{
    return mmio_read32(pch_make_address(port_id, offset));
}

void pch_write32(uint8_t port_id, uint16_t offset, uint32_t val)
{
    mmio_write32(pch_make_address(port_id, offset), val);
}
#endif /* PCH_HAS_PCR */

static uint32_t pci_align32_address(uint32_t addr, int *aligned)
{
    *aligned = !(addr & PCI_ADDR_32BIT_ALIGNED_MASK);
    return (addr & ~(PCI_ADDR_32BIT_ALIGNED_MASK));
}

#ifdef PCI_USE_ECAM
static uint32_t pci_config_ecam_make_address(uint8_t bus, uint8_t dev,
                                              uint8_t func, uint8_t off)
{
    return (PCI_ECAM_BASE +
            ((bus&0xff) << 20) |
            ((dev&0x1f) << 15) |
            ((func&0x07) << 12) |
            (off & 0xfff));
}

static uint32_t pci_ecam_config_read32(uint8_t bus, uint8_t dev, uint8_t fun,
                                       uint8_t off)
{
    return mmio_read32(pci_config_ecam_make_address(bus, dev, fun, off));
}

static void pci_ecam_config_write32(uint8_t bus, uint8_t dev, uint8_t fun,
                                    uint8_t off, uint32_t value)
{
    mmio_write32(pci_config_ecam_make_address(bus, dev, fun, off), value);
}
static uint16_t pci_ecam_config_read16(uint8_t bus, uint8_t dev, uint8_t fun,
                                       uint8_t off)
{
    uint32_t addr;
    uint32_t data;
    int aligned32;

    addr = pci_config_ecam_make_address(bus, dev, fun, off);
    addr = pci_align32_address(addr, &aligned32);
    data = mmio_read32(addr);
    if (aligned32)
        data = data & PCI_DATA_LO16_MASK;
    else
        data = data >> PCI_DATA_HI16_SHIFT;
    return (uint16_t)data;
}

static void pci_ecam_config_write16(uint8_t bus, uint8_t dev, uint8_t fun,
                                    uint8_t off, uint16_t value)
{
    uint32_t addr;
    int aligned32;
    uint32_t reg;

    addr = pci_config_ecam_make_address(bus, dev, fun, off);
    addr = pci_align32_address(addr, &aligned32);
    reg = mmio_read32(addr);
    if (aligned32)
        reg = (reg & PCI_DATA_HI16_MASK) | value;
    else
        reg = (reg & PCI_DATA_LO16_MASK) | (value << PCI_DATA_HI16_SHIFT);
    mmio_write32(addr, reg);
}
#else

#define PCI_CONFIG_ADDR_PORT 0xcf8
#define PCI_CONFIG_DATA_PORT 0xcfc

#define PCI_IO_CONFIG_ADDR(bus, dev, fn, off) \
    (uint32_t)( \
           (1UL << PCI_CONFIG_ADDRESS_ENABLE_BIT_SHIFT) | \
           (bus << PCI_CONFIG_ADDRESS_BUS_SHIFT) | \
           (dev << PCI_CONFIG_ADDRESS_DEVICE_SHIFT) | \
           (fn  << PCI_CONFIG_ADDRESS_FUNCTION_SHIFT) | \
           ((off & 0xF00) << 16) | \
            (off & PCI_CONFIG_ADDRESS_OFFSET_MASK))

static uint32_t pci_io_config_read32(uint32_t bus, uint32_t dev, uint32_t func,
                                    uint32_t off)
{
    uint32_t address = PCI_IO_CONFIG_ADDR(bus, dev, func, off);
    uint32_t data = 0xffffffff;

    io_write32((uint16_t)PCI_CONFIG_ADDR_PORT, address);
    data = io_read32(PCI_CONFIG_DATA_PORT);
    return data;
}

static void pci_io_config_write32(uint32_t bus, uint32_t dev, uint32_t func,
                           uint32_t off, uint32_t val)
{
    uint32_t dst_addr = PCI_IO_CONFIG_ADDR(bus, dev, func, off);

    io_write32(PCI_CONFIG_ADDR_PORT, dst_addr);
    io_write32(PCI_CONFIG_DATA_PORT, val);
}

static uint16_t pci_io_config_read16(uint32_t bus, uint32_t dev, uint32_t func,
                                     uint32_t off)
{
    uint32_t address = PCI_IO_CONFIG_ADDR(bus, dev, func, off);
    uint32_t data = 0xffff;
    int aligned32;

    /* off must be 16 bit aligned */
    if ((address & PCI_ADDR_16BIT_ALIGNED_MASK) != 0)
        return data;

    address = pci_align32_address(address, &aligned32);
    data = pci_io_config_read32(bus, dev, func, address);
    if (!aligned32)
        data >>= PCI_DATA_HI16_SHIFT;
    else
        data &= PCI_DATA_LO16_MASK;
    return (uint16_t)data;
}

static void pci_io_config_write16(uint32_t bus, uint32_t dev, uint32_t func,
                                  uint32_t off, uint16_t val)
{
    uint32_t dst_addr = PCI_IO_CONFIG_ADDR(bus, dev, func, off);
    uint32_t reg;
    int aligned32;

    /* off must be 16 bit aligned */
    if ((dst_addr & PCI_ADDR_16BIT_ALIGNED_MASK) != 0)
        return;

    dst_addr = pci_align32_address(dst_addr, &aligned32);
    reg = pci_io_config_read32(bus, dev, func, dst_addr);
    if (aligned32) {
        reg &= PCI_DATA_HI16_MASK;
        reg |= val;
    } else {
        reg &= PCI_DATA_LO16_MASK;
        reg |= (val << PCI_DATA_HI16_SHIFT);
    }
    pci_io_config_write32(bus, dev, func, dst_addr, reg);
}
#endif /* PCI_USE_ECAM */

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
#ifdef PCI_USE_ECAM
    return pci_ecam_config_read32(bus, dev, fun, off);
#else
    return pci_io_config_read32(bus, dev, fun, off);
#endif /* PCI_HAS_ECAM */
}

void pci_config_write32(uint8_t bus,
                        uint8_t dev, uint8_t fun, uint8_t off, uint32_t value)
{
#ifdef PCI_USE_ECAM
    pci_ecam_config_write32(bus, dev, fun, off, value);
#else
    pci_io_config_write32(bus, dev, fun, off, value);
#endif /* PCI_HAS_ECAM */
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
#ifdef PCI_USE_ECAM
    return pci_ecam_config_read16(bus, dev, fun, off);
#else
    return pci_io_config_read16(bus, dev, fun, off);
#endif /* PCI_HAS_ECAM */
}

void pci_config_write16(uint8_t bus,
                        uint8_t dev, uint8_t fun, uint8_t off, uint16_t value)
{
#ifdef PCI_USE_ECAM
    pci_ecam_config_write16(bus, dev, fun, off, value);
#else
    pci_io_config_write16(bus, dev, fun, off, value);
#endif /* PCI_HAS_ECAM */
}

void pci_config_write8(uint8_t bus,
                        uint8_t dev, uint8_t fun, uint8_t off, uint8_t value)
{
    uint32_t off_aligned;
    uint32_t shift, mask;
    uint32_t reg;

    off_aligned = align_down(off, 4);
    reg = pci_config_read32(bus, dev, fun, off_aligned);
    shift = (off & PCI_ADDR_32BIT_ALIGNED_MASK) * 8;
    mask = 0xff << shift;
    reg &= ~(mask);
    reg |= (value << shift);
    pci_config_write32(bus, dev, fun, off_aligned, reg);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    uint32_t off_aligned;
    uint32_t shift, mask;
    uint32_t reg;

    off_aligned = align_down(off, 4);
    reg = pci_config_read32(bus, dev, fun, off_aligned);
    shift = (off & PCI_ADDR_32BIT_ALIGNED_MASK) * 8;
    mask = 0xff << shift;
    reg &= mask;
    reg = (reg >> shift);
    return reg;
}

uint64_t pci_get_mmio_addr(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t bar)
{
    uint32_t reg;
    uint64_t addr = 0;

    if (bar >= PCI_ENUM_MAX_BARS)
    {
        return addr;
    }

    reg = pci_config_read32(bus, dev, fun, PCI_BAR0_OFFSET + (bar * 8));
    wolfBoot_printf("BAR%d[0x%x] reg value = 0x%x\r\n", bar, PCI_BAR0_OFFSET + (bar * 8), reg);

    if (!pci_enum_is_mmio(reg))
    {
        return addr;
    }

    addr = reg & 0xFFFFFFF0;

    if (pci_enum_is_64bit(reg))
    {
        reg = pci_config_read32(bus, dev, fun, (PCI_BAR0_OFFSET + 4) + (bar * 8));
        wolfBoot_printf("BAR%d_HIGH[0x%x] reg value = 0x%x\r\n", bar, (PCI_BAR0_OFFSET + 4) + (bar * 8), reg);
        addr |= ((uint64_t)reg << 32);
    }

    return addr;
}

static int pci_enum_is_64bit(uint32_t value)
{
    uint8_t type = (value & PCI_ENUM_TYPE_MASK) >> PCI_ENUM_TYPE_SHIFT;
    return type == PCI_ENUM_TYPE_64bit;
}

static int pci_enum_is_prefetch(uint32_t value)
{
    return value & PCI_ENUM_IS_PREFETCH;
}

static int pci_enum_is_mmio(uint32_t value)
{
    return (value & PCI_ENUM_MMIND_MASK) == 0;
}

static int pci_enum_next_aligned32(uint32_t address, uint32_t *next,
                                   uint32_t align, uint32_t limit)
{
    uintptr_t addr;

    addr = (uintptr_t)address;
    align = align-1;
    addr = (addr + align) & (~align);
    if (addr > 0xffffffff)
        return -1;
    if (addr < (uintptr_t)address)
        return -1;
    if (addr >= limit)
        return -1;

    *next = (uint32_t)addr;
    return 0;
}

static int pci_pre_enum_cb(uint8_t bus, uint8_t dev, uint8_t fun)
{
#ifdef WOLFBOOT_TGL
    if (bus != 0)
        return 0;
    /* don't change UART mapping */
    if (dev == 0x1e && (fun == 0 || fun == 1)) {
        return 1;
    }
    /* PMC BARs shouldn't be programmed as per FSP integration guide */
    if (dev == 31 && fun == 2)
        return 1;
#else
    (void)bus;
    (void)dev;
    (void)fun;
#endif /* WOLFBOOT_TGL */
    return 0;
}

static int pci_post_enum_cb(uint8_t bus, uint8_t dev, uint8_t fun)
{
    (void)bus;
    (void)dev;
    (void)fun;
    return 0;
}

static int pci_program_bar(uint8_t bus, uint8_t dev, uint8_t fun,
                           uint8_t bar_idx,
                           struct pci_enum_info *info,
                           uint8_t *is_64bit)
{

    uint32_t bar_align, bar_value;
    uint32_t orig_bar, orig_bar2;
    uint32_t length, align;
    uint8_t bar_off;
    int is_prefetch;
    uint32_t *base;
    uint32_t limit;
    uint32_t reg;
    int is_mmio;
    int ret = 0;

    *is_64bit = 0;
    if (bar_idx >= PCI_ENUM_MAX_BARS)
        return -1;

    is_prefetch = 0;
    (void)is_prefetch;
    bar_off = PCI_BAR0_OFFSET + bar_idx * 4;
    orig_bar = pci_config_read32(bus, dev, fun, bar_off);
    pci_config_write32(bus, dev, fun, bar_off, 0xffffffff);
    bar_value = pci_config_read32(bus, dev,fun, bar_off);
    PCI_DEBUG_PRINTF("bar value after writing 0xff..ff: 0x%x\r\n", bar_value);

    if (bar_value == 0) {
        PCI_DEBUG_PRINTF("PCI enum: %x:%x.%x bar: %d val: %x - skipping\r\n",
                        bus, dev, fun, bar_idx, bar_value);
        goto restore_bar;
    }

    is_mmio = pci_enum_is_mmio(bar_value);
    if (is_mmio) {
        bar_align = bar_value & PCI_ENUM_MM_BAR_MASK;
        is_prefetch = pci_enum_is_prefetch(bar_value);
        if (pci_enum_is_64bit(bar_value)) {
            orig_bar2 = pci_config_read32(bus, dev, fun, bar_off + 4);
            pci_config_write32(bus, dev, fun, bar_off + 4, 0xffffffff);
            reg = pci_config_read32(bus, dev, fun, bar_off + 4);
            PCI_DEBUG_PRINTF("bar high 32bit: %d\r\n", reg);
            if (reg != 0xffffffff) {
                PCI_DEBUG_PRINTF("Device wants too much memory, skipping\r\n");
                pci_config_write32(bus, dev, fun, bar_off + 4, orig_bar2);
                goto restore_bar;
            }
            *is_64bit = 1;
        }
        if (is_prefetch) {
            base = &info->mem_pf;
            limit = info->mem_pf_limit;
        } else {
            base = &info->mem;
            limit = info->mem_limit;
        }
    } else {
        bar_align = bar_value & PCI_ENUM_IO_BAR_MASK;
        /* when io has high 16 bits to zero, cosider them all ones (ref
         *  spec.)  */
        if ((bar_align & PCI_DATA_HI16_MASK) == 0)
            bar_align |= PCI_DATA_HI16_MASK;
        base = &info->io;
        limit = 0xffffffff;
    }

    PCI_DEBUG_PRINTF("PCI enum: %s %x:%x.%x bar: %d val: %x (%s %s)\r\n",
                    (is_mmio ? "mm" : "io"), bus, dev, fun, bar_idx,
                     (uint32_t)bar_value, *is_64bit ? "64bit" : "",
                     is_prefetch ? "prefetch" : "");
    align = length = (~bar_align) + 1;
    /* force pci address to be on page boundary */
    if (align < 0x1000)
        align = 0x1000;

    /* check max length */
    ret = pci_enum_next_aligned32(*base, &bar_value, align, limit);
    if (ret != 0) {
        wolfBoot_printf("Not memory space for mapping the PCI device... skipping\r\n");
        goto restore_bar;
    }

    pci_config_write32(bus, dev, fun, bar_off, bar_value);
    if (*is_64bit)
        pci_config_write32(bus, dev, fun, bar_off + 4, 0x0);
    *base = bar_value + length;
    PCI_DEBUG_PRINTF("PCI enum: %s bus: %x:%x.%x bar: %d [%x,%x] (0x%x %s %s)\r\n",
                    (is_mmio ? "mm" : "io"), bus, dev, fun, bar_idx, bar_value,
                     bar_value + length, length, (*is_64bit) ? "64bit" : "",
                     is_prefetch ? "prefetch" : "");

    return 0;

restore_bar:
    pci_config_write32(bus, dev, fun, bar_idx, orig_bar);

    return ret;
}

#if defined(DEBUG_PCI)
static void pci_dump_id(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint16_t vid, did;

    vid = pci_config_read16(bus, dev, fun, PCI_VENDOR_ID_OFFSET);
    did = pci_config_read16(bus, dev, fun, PCI_DEVICE_ID_OFFSET);

    PCI_DEBUG_PRINTF("\r\n\r\nPCI: %x:%x.%x %x:%x\r\n",
                    bus, dev, fun, (int)vid, (int)did);
}
#else
#define pci_dump_id(bus, dev, fun) do{}while(0)
#endif

static int pci_program_bars(uint8_t bus, uint8_t dev, uint8_t fun,
                            struct pci_enum_info *info)
{
    uint32_t orig_cmd;
    uint8_t is64bit;
    int _bar_idx;
    int ret;

    orig_cmd = pci_config_read16(bus, dev, fun, PCI_COMMAND_OFFSET);
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, 0);

    for (_bar_idx = 0; _bar_idx < PCI_ENUM_MAX_BARS; _bar_idx++) {
        ret = pci_program_bar(bus, dev, fun, _bar_idx, info, &is64bit);
        if (ret != 0)
            break;

        /* 64bit BAR uses two consecutive BAR registers  */
        if (is64bit)
            _bar_idx++;
    }

    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, orig_cmd);

    return 0;
}

#ifdef DEBUG_PCI
static void pci_dump_bridge(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint16_t mbase, mlimit;
    uint16_t pfbase, pflimit;
    uint8_t iobase, iolimit;
    uint8_t prim, sec, ssb;

    mbase = pci_config_read16(bus, dev, fun, PCI_MMIO_BASE_OFF);
    mlimit = pci_config_read16(bus, dev, fun, PCI_MMIO_LIMIT_OFF);
    pfbase = pci_config_read16(bus, dev, fun, PCI_PREFETCH_BASE_OFF);
    pflimit = pci_config_read16(bus, dev, fun, PCI_PREFETCH_LIMIT_OFF);
    iobase = pci_config_read8(bus, dev, fun, PCI_IO_BASE_OFF);
    iolimit = pci_config_read8(bus, dev, fun, PCI_IO_LIMIT_OFF);
    prim = pci_config_read8(bus, dev, fun, PCI_PRIMARY_BUS);
    sec = pci_config_read8(bus, dev, fun, PCI_SECONDARY_BUS);
    ssb = pci_config_read8(bus, dev, fun, PCI_SUB_SEC_BUS);

    PCI_DEBUG_PRINTF("mbase: 0x%x, mlimit: 0x%x\n\r", mbase, mlimit);
    PCI_DEBUG_PRINTF("pfbase: 0x%x, pflimit: 0x%x\n\r", pfbase, pflimit);
    PCI_DEBUG_PRINTF("iobase: 0x%x, iolimit: 0x%x\n\r", iobase, iolimit);
    PCI_DEBUG_PRINTF("prim: 0x%x, sec: 0x%x, ssb: 0x%x\n\r", prim, sec, ssb);

}
#else
static inline void pci_dump_bridge(uint8_t bus, uint8_t dev, uint8_t fun)
{
    (void)bus;
    (void)dev;
    (void)fun;
}
#endif /* DEBUG_PCI */

static int pci_program_bridge(uint8_t bus, uint8_t dev, uint8_t fun,
                                   struct pci_enum_info *info)
{
    uint32_t prefetch_start;
    uint32_t mem_start;
    uint32_t io_start;
    uint32_t orig_cmd;
    int ret;

    orig_cmd = pci_config_read16(bus, dev, fun, PCI_COMMAND_OFFSET);
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, 0);

    info->curr_bus_number++;
    PCI_DEBUG_PRINTF("Bridge: %x.%x.%x (using bus number: %d)\r\n",
                     (int)bus, (int)dev, (int)fun, info->curr_bus_number);
    pci_config_write8(bus, dev, fun, PCI_PRIMARY_BUS, bus);
    pci_config_write8(bus, dev, fun, PCI_SECONDARY_BUS, info->curr_bus_number);

    /* temporarly allows all conf transaction on the bus range
     * (curr_bus_number,0xff) to scan the bus behind the bridge */
    pci_config_write8(bus, dev, fun, PCI_SUB_SEC_BUS, 0xff);

    ret = pci_align_check_up(info->mem_pf, ONE_MB,
                             info->mem_pf_limit,
                             &prefetch_start);
    if (ret == -1)
        goto err;
    info->mem_pf = prefetch_start;

    ret = pci_align_check_up(info->mem, ONE_MB,
                             info->mem_limit,
                             &mem_start);
    if (ret == -1)
        goto err;
    info->mem = mem_start;

    ret = pci_align_check_up(info->io, FOUR_KB,
                             0xffffffff,
                             &io_start);
    if (ret == -1)
        goto err;
    info->io = io_start;

    ret = pci_enum_bus(info->curr_bus_number, info);
    if (ret != 0)
        goto err;
    /* update subordinate secondary bus with the max bus number found behind the
     * bridge */
    pci_config_write8(bus, dev, fun, PCI_SUB_SEC_BUS, info->curr_bus_number);

    /* upate prefetch range */
    if (prefetch_start != info->mem_pf) {
        ret = pci_align_check_up(info->mem_pf, ONE_MB,
                                 info->mem_pf_limit,
                                 &info->mem_pf);
        if (ret != 0)
            goto err;
        pci_config_write16(bus, dev, fun, PCI_PREFETCH_BASE_OFF,
                           prefetch_start >> 16);
        pci_config_write16(bus, dev, fun, PCI_PREFETCH_LIMIT_OFF,
                           (info->mem_pf - 1) >> 16);
        orig_cmd |= PCI_COMMAND_MEM_SPACE;
    } else {
        /* disable prefetch */
        pci_config_write16(bus, dev, fun, PCI_PREFETCH_BASE_OFF,
                           0xffff);
        pci_config_write16(bus, dev, fun, PCI_PREFETCH_LIMIT_OFF,
                           0x0);
    }

    /* upate mem range */
    if (mem_start != info->mem) {
        ret = pci_align_check_up(info->mem, ONE_MB,
                                 info->mem_limit,
                                 &info->mem);
        if (ret != 0)
            goto err;
        pci_config_write16(bus, dev, fun, PCI_MMIO_BASE_OFF,
                           mem_start >> 16);
        pci_config_write16(bus, dev, fun, PCI_MMIO_LIMIT_OFF,
                           (info->mem - 1) >> 16);
        orig_cmd |= PCI_COMMAND_MEM_SPACE;
    } else {
        /* disable mem */
        pci_config_write16(bus, dev, fun, PCI_MMIO_BASE_OFF,
                           0xffff);
        pci_config_write16(bus, dev, fun, PCI_MMIO_LIMIT_OFF,
                           0x0);
    }

    /* upate io range */
    if (io_start != info->io) {
        ret = pci_align_check_up(info->io, FOUR_KB,
                                 0xffffffff,
                                 &info->io);
        if (ret != 0)
            goto err;
        pci_config_write8(bus, dev, fun, PCI_IO_BASE_OFF,
                           io_start >> 8);
        pci_config_write8(bus, dev, fun, PCI_IO_LIMIT_OFF,
                          (info->io - 1) >> 8);
        orig_cmd |= PCI_COMMAND_IO_SPACE;
    }
    else {
        pci_config_write8(bus, dev, fun, PCI_IO_BASE_OFF,
                           0xff);
        pci_config_write8(bus, dev, fun, PCI_IO_LIMIT_OFF,
                          0x0);
    }

    orig_cmd |= PCI_COMMAND_BUS_MASTER;
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, orig_cmd);

    pci_dump_bridge(bus,dev,fun);
    return 0;

 err:
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, orig_cmd);
    return -1;
}

uint32_t pci_enum_bus(uint8_t bus, struct pci_enum_info *info)
{
    uint16_t vendor_id, device_id, header_type;
    uint32_t vd_code, reg;
    uint32_t dev, fun;
    int ret;

    PCI_DEBUG_PRINTF("enumerating bus %d\r\n", bus);

    for (dev = 0; dev < PCI_ENUM_MAX_DEV; dev++) {

        vd_code = pci_config_read32(bus, dev, 0, PCI_VENDOR_ID_OFFSET);
        if (vd_code == 0xFFFFFFFF) {
            PCI_DEBUG_PRINTF("Skipping %x:%x\r\n", bus, dev);
            /* No device here. */
            continue;
        }

        for (fun = 0; fun < PCI_ENUM_MAX_FUN; fun++) {
            if (pci_pre_enum_cb(bus, dev, fun)) {
                PCI_DEBUG_PRINTF("skipping fun %x:%x.%x\r\n", bus, dev, fun);
                continue;
            }

            vd_code = pci_config_read32(bus, dev, fun, PCI_VENDOR_ID_OFFSET);
            if (vd_code == 0xFFFFFFFF) {
                PCI_DEBUG_PRINTF("Skipping %x:%x.%x\r\n", bus, dev, fun);
                /* No device here, try next function*/
                continue;
            }
            header_type = pci_config_read16(bus, dev, fun,
                                            PCI_HEADER_TYPE_OFFSET);
            pci_dump_id(bus, dev, fun);
            if ((header_type & PCI_HEADER_TYPE_TYPE_MASK) == PCI_HEADER_TYPE_DEVICE) {
                pci_program_bars(bus, dev, fun, info);
                pci_post_enum_cb(bus, dev, fun);
            } else {
                pci_program_bridge(bus, dev, fun, info);
            }
            /* just one function */
            if ((fun == 0) && !(header_type & PCI_HEADER_TYPE_MULTIFUNC_MASK)) {
                PCI_DEBUG_PRINTF("one function only device\r\n");
                break;
            }
        }
    }


    return 0;
}

static int pci_get_capability(uint8_t bus, uint8_t dev, uint8_t fun,
                              uint8_t cap_id, uint8_t *cap_off)
{
    uint8_t r8, id;
    uint32_t r32;

    r32 = pci_config_read16(bus, dev, fun, PCI_STATUS_OFFSET);
    if (!(r32 & PCI_STATUS_CAP_LIST))
        return -1;
    r8 = pci_config_read8(bus, dev, fun, PCI_CAP_OFFSET);
    while (r8 != 0) {
        id = pci_config_read8(bus, dev, fun, r8);
        if (id == cap_id) {
            *cap_off = r8;
            return 0;
        }
        r8 = pci_config_read8(bus, dev, fun, r8 + 1);
    }
    return -1;
}

int pcie_retraining_link(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint16_t link_status, link_control, vid;
    uint8_t pcie_cap_off;
    int ret, tries;

    PCI_DEBUG_PRINTF("retraining link: %x:%x.%x\r\n", bus, dev, fun);
    vid = pci_config_read16(bus, dev, 0, PCI_VENDOR_ID_OFFSET);
    if (vid == 0xffff) {
        PCI_DEBUG_PRINTF("can't find dev: %x:%x.%d\r\n", bus, dev, fun);
        return -1;
    }
    
    ret = pci_get_capability(bus, dev, fun, PCI_PCIE_CAP_ID, &pcie_cap_off);
    if (ret != 0) {
        PCI_DEBUG_PRINTF("can't find PCIE cap pointer\r\n");
        return -1;
    }

    PCI_DEBUG_PRINTF("pcie cap off: 0x%x\r\n", pcie_cap_off);
    link_status = pci_config_read16(bus, dev, fun,
                                    pcie_cap_off + PCIE_LINK_STATUS_OFF);
    if (link_status & PCIE_LINK_STATUS_TRAINING) {
        PCI_DEBUG_PRINTF("link already training, waiting...\r\n");
        delay(PCIE_TRAINING_TIMEOUT_MS);
        link_status = pci_config_read16(bus, dev, fun,
                                        pcie_cap_off + PCIE_LINK_STATUS_OFF);
        if (link_status & PCIE_LINK_STATUS_TRAINING) {
            PCI_DEBUG_PRINTF("link training error: timeout\r\n");
            return -1;
        }
    }

    link_control = pci_config_read16(bus, dev, fun,
                                         pcie_cap_off + PCIE_LINK_CONTROL_OFF);
    link_control |= PCIE_LINK_CONTROL_RETRAINING;
    pci_config_write16(bus, dev, fun, pcie_cap_off + PCIE_LINK_CONTROL_OFF,
                       link_control);
    tries = PCIE_TRAINING_TIMEOUT_MS / 10;
    do {
        link_status = pci_config_read16(bus, dev, fun,
                                        pcie_cap_off + PCIE_LINK_STATUS_OFF);
        if (!(link_status & PCIE_LINK_STATUS_TRAINING))
            break;
        delay(10);
    } while(tries--);

    if ((link_status & PCIE_LINK_STATUS_TRAINING)) {
        PCI_DEBUG_PRINTF("Timeout reached during retraining\r\n");
        return -1;
    }

    PCI_DEBUG_PRINTF("retraining complete\r\n");
    return 0;
}

int pci_pre_enum(void)
{
    uint32_t reg;

    reg = pci_config_read32(0, 0, 0, CAPID0_A_0_0_0_PCI);
    wolfBoot_printf("cap a %d\r\n", reg);
    wolfBoot_printf("ddt disabled %d\r\n", reg & DTT_DEVICE_DISABLE);
    reg &= ~(DTT_DEVICE_DISABLE);
    pci_config_write32(0, 0, 0, CAPID0_A_0_0_0_PCI, reg);
    reg = pci_config_read32(0, 0, 0, DEVICE_ENABLE);
    wolfBoot_printf("device enable: %d\r\n", reg);
    reg |= (1 << 7);
    pci_config_write32(0, 0, 0, DEVICE_ENABLE, reg);
     reg = pci_config_read32(0, 0, 0, DEVICE_ENABLE);
    wolfBoot_printf("device enable: %d\r\n", reg);

    return 0;
}

#if defined(DEBUG_PCI)
/**
 * @brief Dump PCI configuration space
 *
 * @param bus PCI bus number
 * @param dev PCI device number
 * @param fun PCI function number
*/
void pci_dump(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint32_t reg[256/4];
    uint8_t *ptr;
    int i;

    for (i = 0; i < 256 / 4; i++) {
        reg[i] = pci_config_read32(bus, dev, fun, i * 4);
    }

    ptr = (uint8_t*)reg;
    for (i = 0; i < 256; i++) {
        if (i % 0x10 == 0x0) {
            if (i < 0x10) {
                wolfBoot_printf("0");
            }
            wolfBoot_printf("%x: ", (int)i);
        }
        wolfBoot_printf("%s%x%s", (ptr[i] < 0x10 ? "0" :""), (int)ptr[i],
                        (i % 0x10 == 0xf ? "\r\n":" "));
    }
}

/**
 * @brief Dump PCI configuration space for all devices in the bus
 *
 * This function will dump the PCI configuration space for all devices in the
 * bus, it will recursively dump buses behind bridges.
 *
 * @param bus PCI bus number
*/
static void pci_dump_bus(uint8_t bus)
{
    uint16_t vendor_id, device_id, header_type;
    uint8_t dev, fun, sec_bus;
    uint32_t vd_code, reg;
    int ret;

    for (dev = 0; dev < PCI_ENUM_MAX_DEV; dev++) {
        vd_code = pci_config_read32(bus, dev, 0, PCI_VENDOR_ID_OFFSET);
        if (vd_code == 0xFFFFFFFF) {
            /* No device here. */
            continue;
        }

        for (fun = 0; fun < PCI_ENUM_MAX_FUN; fun++) {
            vd_code = pci_config_read32(bus, dev, fun, PCI_VENDOR_ID_OFFSET);
            if (vd_code == 0xFFFFFFFF) {
                /* No device here. */
                continue;
            }

            if (bus < 0x10)
                wolfBoot_printf("0");
            wolfBoot_printf("%x:", bus);
            if (dev < 0x10)
                wolfBoot_printf("0");
            wolfBoot_printf("%x.", dev);
            wolfBoot_printf("%d \r\n", fun);
            pci_dump(bus, dev, fun);
            header_type = pci_config_read16(bus, dev, fun,
                                            PCI_HEADER_TYPE_OFFSET);

            if ((header_type & PCI_HEADER_TYPE_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
                sec_bus = pci_config_read8(bus, dev, fun, PCI_SECONDARY_BUS);
                if (sec_bus != 0)
                    pci_dump_bus(sec_bus);
            }

            /* just one function */
            if ((fun == 0) && !(header_type & PCI_HEADER_TYPE_MULTIFUNC_MASK)) {
                break;
            }
        }
    }
}

/**
 * @brief Dump full PCI configuration space
*/
void pci_dump_config_space(void)
{
    return pci_dump_bus(0);
}
#else
void pci_dump_config_space(void) {};
#endif

int pci_enum_do(void)
{
    struct pci_enum_info enum_info;
    int ret;

    enum_info.mem = PCI_MMIO32_BASE;
    enum_info.mem_limit = enum_info.mem + (PCI_MMIO32_LENGTH - 1);
    enum_info.mem_pf = PCI_MMIO32_PREFETCH_BASE;
    enum_info.mem_pf_limit = enum_info.mem_pf +
        (PCI_MMIO32_PREFETCH_LENGTH - 1);
    enum_info.io = PCI_IO32_BASE;
    enum_info.curr_bus_number = 0;

    ret = pci_pre_enum();
    if (ret != 0) {
        wolfBoot_printf("pci_pre_enum error: %d\r\n", ret);
        return ret;
    }

    ret = pci_enum_bus(0, &enum_info);

    PCI_DEBUG_PRINTF("PCI Memory Mapped I/O range [0x%x,0x%x] (0x%x)\r\n",
                     (uint32_t)PCI_MMIO32_BASE, enum_info.mem,
                     enum_info.mem - PCI_MMIO32_BASE);

    PCI_DEBUG_PRINTF("PCI Memory Mapped I/O range (prefetch) [0x%x,0x%x] (0x%x)\r\n",
                     (uint32_t)PCI_MMIO32_PREFETCH_BASE, enum_info.mem_pf,
                     enum_info.mem_pf - PCI_MMIO32_PREFETCH_BASE);

    PCI_DEBUG_PRINTF("PCI I/O range [0x%x,0x%x] (0x%x)\r\n",
                     (uint32_t)PCI_IO32_BASE, enum_info.io,
                     enum_info.io - PCI_IO32_BASE);

    return ret;
}
