/* pci.c
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

#ifndef PCI_MMIO32_BASE
#define PCI_MMIO32_BASE 0x80000000
#endif /* PCI_MMIO32_BASE */

#ifndef PCI_IO32_BASE
#define PCI_IO32_BASE 0x2000
#endif /* PCI_IO32_BASE */

#define PCI_ENUM_MAX_DEV 32
#define PCI_ENUM_MAX_FUN 8
#define PCI_ENUM_MAX_BARS 6

#define PCI_ENUM_MMIND_MASK (0x1)
#define PCI_ENUM_TYPE_MASK (0x1 << 1 | 0x1 << 2)
#define PCI_ENUM_TYPE_SHIFT 1
#define PCI_ENUM_TYPE_64bit (0x1 << 1)
#define PCI_ENUM_TYPE_32bit (0x1)
#define PCI_ENUM_MM_BAR_MASK ~(0xf)
#define PCI_ENUM_IO_BAR_MASK ~(0x3)

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

/* Shifts & masks for CONFIG_ADDRESS register */

#define PCI_CONFIG_ADDRESS_ENABLE_BIT_SHIFT 31
#define PCI_CONFIG_ADDRESS_BUS_SHIFT    16
#define PCI_CONFIG_ADDRESS_DEVICE_SHIFT 11
#define PCI_CONFIG_ADDRESS_FUNCTION_SHIFT 8
#define PCI_CONFIG_ADDRESS_OFFSET_MASK 0xFF

#define PCI_IO_CONFIG_ADDR(bus, dev, fn, off) \
    (uint32_t)( \
           (1   << PCI_CONFIG_ADDRESS_ENABLE_BIT_SHIFT) | \
           (bus << PCI_CONFIG_ADDRESS_BUS_SHIFT) | \
           (dev << PCI_CONFIG_ADDRESS_DEVICE_SHIFT) | \
           (fn  << PCI_CONFIG_ADDRESS_FUNCTION_SHIFT) | \
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

static int pci_enum_is_64bit(uint32_t value)
{
    uint8_t type = (value & PCI_ENUM_TYPE_MASK) >> PCI_ENUM_TYPE_SHIFT;
    return type == PCI_ENUM_TYPE_64bit;
}

static int pci_enum_is_mmio(uint32_t value)
{
    return (value & PCI_ENUM_MMIND_MASK) == 0;
}

static int pci_enum_next_aligned32(uint32_t address, uint32_t *next,
                                   uint32_t align)
{
    uintptr_t addr;

    addr = (uintptr_t)address;
    align = align-1;
    addr = (addr + align) & (~align);
    if (addr > 0xffffffff)
        return -1;
    if (addr < (uintptr_t)address)
        return -1;

    *next = (uint32_t)addr;
    return 0;
}

static int pci_pre_enum_cb(uint8_t bus, uint8_t dev, uint8_t fun)
{
#ifdef WOLFBOOT_TGL
    if (bus != 0)
        return 0;
    if (dev == 0x1e) {
        return 1;
    }
    /* PMC BARs shouldn't be programmed as per FSP integration guide */
    if (dev == 31 && fun == 2)
        return 1;
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
                           uint32_t *mm_base_ptr,
                           uint32_t *io_base_ptr,
                           uint8_t *is_64bit)
{

    uint32_t bar_align, bar_value;
    uint32_t orig_bar, orig_bar2;
    uint32_t length, align;
    uint8_t bar_off;
    uint32_t *base;
    uint32_t reg;
    int is_mmio;
    int ret = 0;

    *is_64bit = 0;
    if (bar_idx >= PCI_ENUM_MAX_BARS)
        return -1;

    bar_off = PCI_BAR0_OFFSET + bar_idx * 4;
    orig_bar = pci_config_read32(bus, dev, fun, bar_off);
    pci_config_write32(bus, dev, fun, bar_off, 0xffffffff);
    bar_value = pci_config_read32(bus, dev,fun, bar_off);
    PCI_DEBUG_PRINTF("bar value: 0x%x\r\n", bar_value);

    if (bar_value == 0) {
        PCI_DEBUG_PRINTF("PCI enum: %x:%x.%x bar: %d val: %x - skipping\r\n",
                        bus, dev, fun, bar_idx, bar_value);
        goto restore_bar;
    }

    is_mmio = pci_enum_is_mmio(bar_value);
    if (is_mmio) {
        bar_align = bar_value & PCI_ENUM_MM_BAR_MASK;
        base = mm_base_ptr;
        if (pci_enum_is_64bit((uint32_t)bar_value)) {
            PCI_DEBUG_PRINTF("bar is 64bit\r\n");
            orig_bar2 = pci_config_read32(bus, dev, fun, bar_off + 4);
            pci_config_write32(bus, dev, fun, bar_off + 4, 0xffffffff);
            reg = pci_config_read32(bus, dev, fun, bar_off + 4);
            PCI_DEBUG_PRINTF("bar high 32bit: %d\r\n", reg);
            if (reg != 0xffffffff) {
                PCI_DEBUG_PRINTF("Too big BAR, skipping\r\n");
                pci_config_write32(bus, dev, fun, bar_off + 4, orig_bar2);
                goto restore_bar;
            }
            *is_64bit = 1;
        }
    } else {
        bar_align = bar_value & PCI_ENUM_IO_BAR_MASK;
        /* when io has high 16 bits to zero, cosider them all ones (ref
         *  spec.)  */
        if ((bar_align & PCI_DATA_HI16_MASK) == 0)
            bar_align |= PCI_DATA_HI16_MASK;
        base = io_base_ptr;
    }

    PCI_DEBUG_PRINTF("PCI enum: %s %x:%x.%x bar: %d val: %x (%s)\r\n",
                    (is_mmio ? "mm" : "io"), bus, dev, fun, bar_idx,
                    (uint32_t)bar_value, *is_64bit ? "64bit" : "");
    align = length = (~bar_align) + 1;
    /* force pci address to be on page boundary */
    if (align < 0x1000)
        align = 0x1000;

    /* check max length */
    ret = pci_enum_next_aligned32(*base, &bar_value, align);
    if (ret != 0)
        goto restore_bar;

    pci_config_write32(bus, dev, fun, bar_off, bar_value);
    if (*is_64bit)
        pci_config_write32(bus, dev, fun, bar_off + 4, 0x0);
    *base = bar_value + length;
    PCI_DEBUG_PRINTF("PCI enum: %s bus: %x:%x.%x bar: %d [%x,%x] (%x %s)\r\n",
                    (is_mmio ? "mm" : "io"), bus, dev, fun, bar_idx, bar_value,
                    bar_value + length, length, (*is_64bit) ? "64bit" : "");

    return 0;

restore_bar:
    pci_config_write32(bus, dev, fun, bar_idx, orig_bar);

    return ret;
}

static void pci_dump_id(uint8_t bus, uint8_t dev, uint8_t fun)
{
    uint16_t vid, did;

    vid = pci_config_read16(bus, dev, fun, PCI_VENDOR_ID_OFFSET);
    did = pci_config_read16(bus, dev, fun, PCI_DEVICE_ID_OFFSET);

    PCI_DEBUG_PRINTF("PCI enum: dev: %x:%x.%x vid: %x did: %x\r\n",
                    bus, dev, fun, (int)vid, (int)did);
}

static int pci_program_bars(uint8_t bus, uint8_t dev, uint8_t fun,
                            uint32_t *mm_base_ptr, uint32_t *io_base_ptr)
{
    uint32_t  orig_cmd;
    uint8_t is64bit;
    int _bar_idx;
    int ret;

    orig_cmd = pci_config_read16(bus, dev, fun, PCI_COMMAND_OFFSET);
    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, 0);

    for (_bar_idx = 0; _bar_idx < PCI_ENUM_MAX_BARS; _bar_idx++) {
        ret = pci_program_bar(bus, dev, fun, _bar_idx, mm_base_ptr,
                              io_base_ptr, &is64bit);
        if (ret != 0)
            break;

        /* 64bit BAR uses two consecutive BAR registers  */
        if (is64bit)
            _bar_idx++;
    }

    pci_config_write16(bus, dev, fun, PCI_COMMAND_OFFSET, orig_cmd);

    return 0;
}

static uint32_t pci_enum_bus(uint8_t bus, uint32_t *pci_base, uint32_t *io_base)
{
    uint16_t vendor_id, device_id, header_type;
    uint32_t vd_code, reg;
    uint32_t dev, fun;
    int ret;

    for (dev = 0; dev < PCI_ENUM_MAX_DEV; dev++) {

        vd_code = pci_config_read32(bus, dev, 0, 0x0);
        if (vd_code == 0xFFFFFFFF) {
            /* No device here. */
            continue;
        }

        for (fun = 0; fun < PCI_ENUM_MAX_FUN; fun++) {
            if (pci_pre_enum_cb(bus, dev, fun))
                continue;

            vd_code = pci_config_read32(bus, dev, fun, PCI_VENDOR_ID_OFFSET);
            if (vd_code == 0xFFFFFFFF) {
                /* No device here, try next function*/
                continue;
            }
            header_type = pci_config_read16(bus, dev, fun,
                                            PCI_HEADER_TYPE_OFFSET);
            pci_dump_id(bus, dev, fun);
            if ((header_type & PCI_HEADER_TYPE_TYPE_MASK) != 0) {
                PCI_DEBUG_PRINTF("not a general device: %, skipping\r\n",
                                header_type);
                continue;
            }
            pci_program_bars(bus, dev, fun, pci_base, io_base);
            pci_post_enum_cb(bus, dev, fun);
            /* just one function */
            if (!(header_type & PCI_HEADER_TYPE_MULTIFUNC_MASK))
                break;
        }
    }

    return 0;
}

int pci_enum_do()
{
    uint32_t pci_base = PCI_MMIO32_BASE;
    uint32_t io_base = PCI_IO32_BASE;
    /* bus 0 only supported */
    return pci_enum_bus(0, &pci_base, &io_base);
}
