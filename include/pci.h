/* pci.h
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
#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_VENDOR_ID_OFFSET 0x00
#define PCI_DEVICE_ID_OFFSET 0x02
#define PCI_COMMAND_OFFSET   0x04
/* Programming interface, Rev. ID and class code */
#define PCI_RID_CC_OFFSET 0x08
#define PCI_HEADER_TYPE_OFFSET 0x0E
#define PCI_BAR0_OFFSET (0x10)
#define PCI_BAR5_OFFSET 0x24
#define PCI_BAR_MASK (~0x3)
#define PCI_INTR_OFFSET   0x3C
#define PCI_HEADER_TYPE_MULTIFUNC_MASK 0x80
#define PCI_HEADER_TYPE_TYPE_MASK 0x7F
#define PCI_HEADER_TYPE_DEVICE 0x0
#define PCI_HEADER_TYPE_BRIDGE 0x1
#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define PCI_INTERFACE_AHCI 0x01
#define PCI_PRIMARY_BUS 0x18
#define PCI_SECONDARY_BUS 0x19
#define PCI_SUB_SEC_BUS 0x1a
#define PCI_SUB_LAT_TIME 0x1b
#define PCI_PREFETCH_BASE_OFF 0x24
#define PCI_PREFETCH_LIMIT_OFF 0x26
#define PCI_MMIO_BASE_OFF 0x20
#define PCI_MMIO_LIMIT_OFF 0x22
#define PCI_IO_BASE_OFF 0x30
#define PCI_IO_LIMIT_OFF 0x32
#define PCI_PWR_MGMT_CTRL_STATUS 0x84
#define PCI_POWER_STATE_MASK 0x3
/* Shifts & masks for CONFIG_ADDRESS register */
#define PCI_CONFIG_ADDRESS_ENABLE_BIT_SHIFT 31
#define PCI_CONFIG_ADDRESS_BUS_SHIFT    16
#define PCI_CONFIG_ADDRESS_DEVICE_SHIFT 11
#define PCI_CONFIG_ADDRESS_FUNCTION_SHIFT 8
#define PCI_CONFIG_ADDRESS_OFFSET_MASK 0xFF

/* COMMAND bits */
#define PCI_COMMAND_INT_DIS         (1 << 10)
#define PCI_COMMAND_FAST_B2B_EN     (1 << 9)
#define PCI_COMMAND_SERR_EN         (1 << 8)
#define PCI_COMMAND_PE_RESP         (1 << 6)
#define PCI_COMMAND_VGASNOOP        (1 << 5)
#define PCI_COMMAND_MW_INV_EN       (1 << 4)
#define PCI_COMMAND_SPECIAL_CYCLE   (1 << 3)
#define PCI_COMMAND_BUS_MASTER      (1 << 2)
#define PCI_COMMAND_MEM_SPACE       (1 << 1)
#define PCI_COMMAND_IO_SPACE        (1 << 0)

typedef struct {
    int bus;
    int device;
    int function;
    uint32_t device_id;
} pci_ctrlr_info_t;

struct pci_enum_info {
    uint32_t mem;
    uint32_t mem_limit;
    uint32_t io;
    uint32_t mem_pf;
    uint32_t mem_pf_limit;
    uint8_t curr_bus_number;
};


#ifdef __cplusplus
extern "C"
{
#endif

#ifdef PCH_HAS_PCR
uint32_t pch_read32(uint8_t port_id, uint16_t offset);
void pch_write32(uint8_t port_id, uint16_t offset, uint32_t val);
#endif

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off);
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off,
                        uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off);
void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off,
                        uint16_t value);
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off);
void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off,
                       uint8_t value);
uint64_t pci_get_mmio_addr(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t bar);

uint32_t pci_enum_bus(uint8_t bus, struct pci_enum_info *info);

int pci_enum_do(void);
int pci_pre_enum(void);

#ifdef __cplusplus
}
#endif

#endif /* PCI_H */
