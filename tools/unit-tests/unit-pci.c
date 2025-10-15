/* unit-pci.c
 *
 * Unit test for pci functions
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <check.h>

#define MOCKED_BASE (2*1024*1024*1024ULL)
#define MOCKED_LEN (1024*1024*1024ULL)
#define PCI_USE_ECAM
#define PCI_ECAM_BASE MOCKED_BASE

#include <pci.h>
#include <pci.c>

struct  type1_pci_header {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    uint32_t base_address_0;
    uint32_t base_address_1;
    uint8_t primary_bus_number;
    uint8_t secondary_bus_number;
    uint8_t subordinate_bus_number;
    uint8_t secondary_latency_timer;
    uint16_t io_base;
    uint16_t io_limit;
    uint16_t secondary_status;
    uint16_t memory_base;
    uint16_t memory_limit;
    uint16_t prefetchable_memory_base;
    uint16_t prefetchable_memory_limit;
    uint32_t prefetchable_base_upper;
    uint32_t prefetchable_limit_upper;
    uint16_t io_base_upper;
    uint16_t io_limit_upper;
    uint8_t capability_pointer;
    uint8_t reserved_0[3];
    uint32_t expansion_rom_base_address;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint16_t bridge_control;
};

struct type1_pci_header type1_pci_header_mock = {
    .vendor_id = 0xdead,
    .device_id = 0xddee,
    .status = 0x8000,
    .command = 0x0010,
    .class_code = 0x06,
    .subclass = 0x04,
    .prog_if = 0x80,
    .revision_id = 0x03,
    .bist = 0x00,
    .header_type = 0x01,
    .latency_timer = 0x40,
    .cache_line_size = 0x08,
    .base_address_0 = 0x12345678,
    .base_address_1 = 0x9abcdef0,
    .secondary_latency_timer = 0xee,
    .subordinate_bus_number = 0xdd,
    .secondary_bus_number = 0xbb,
    .primary_bus_number = 0xaa,
    .secondary_status = 0x0000,
    .io_limit = 0xffff,
    .io_base = 0x0000,
    .memory_limit = 0xffff,
    .memory_base = 0x0000,
    .prefetchable_memory_limit = 0xffff,
    .prefetchable_memory_base = 0x0000,
    .prefetchable_base_upper = 0x00000000,
    .prefetchable_limit_upper = 0x00000000,
    .io_limit_upper = 0x0000,
    .io_base_upper = 0x0000,
    .reserved_0 = {0x00, 0x00},
    .capability_pointer = 0x00,
    .expansion_rom_base_address = 0x13579bdf,
    .bridge_control = 0x0000,
    .interrupt_pin = 0x01,
    .interrupt_line = 0x0a
};

void mmio_write32(uintptr_t address, uint32_t value)
{
    uint32_t *p = (uint32_t*)address;
    *p = value;
}

uint32_t mmio_read32(uintptr_t address)
{
    return *((uint32_t*)(address));
}

void panic()
{
    ck_abort_msg("panic!");
}

START_TEST (test_pci_config_write)
{
    struct type1_pci_header *hdr_mem;
    uint16_t reg16;
    uint8_t reg8;

    hdr_mem =
        (struct type1_pci_header *)mmap((uint8_t *)MOCKED_BASE, MOCKED_LEN,
                                        PROT_WRITE | PROT_READ,
                MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

    ck_assert_ptr_eq(hdr_mem, (uint8_t*)MOCKED_BASE);

    memcpy(hdr_mem, &type1_pci_header_mock, sizeof(type1_pci_header_mock));
    reg16 = pci_config_read16(0, 0, 0, PCI_VENDOR_ID_OFFSET);
    ck_assert_uint_eq(reg16, hdr_mem->vendor_id);
    reg16 = pci_config_read16(0, 0, 0, PCI_DEVICE_ID_OFFSET);
    ck_assert_uint_eq(reg16, hdr_mem->device_id);
    reg16 = 0xdead;
    pci_config_write16(0, 0, 0, PCI_VENDOR_ID_OFFSET, reg16);
    ck_assert_uint_eq(reg16, hdr_mem->vendor_id);
    reg16 = 0xbeef;
    pci_config_write16(0, 0, 0, PCI_DEVICE_ID_OFFSET, reg16);
    ck_assert_uint_eq(reg16, hdr_mem->device_id);
    reg8 = pci_config_read8(0, 0, 0, PCI_PRIMARY_BUS);
    ck_assert_uint_eq(reg8, hdr_mem->primary_bus_number);
    reg8 = pci_config_read8(0, 0, 0, PCI_SECONDARY_BUS);
    ck_assert_uint_eq(reg8, hdr_mem->secondary_bus_number);
    reg8 = 0xbe;
    pci_config_write8(0, 0, 0, PCI_PRIMARY_BUS, reg8);
    ck_assert_uint_eq(reg8, hdr_mem->primary_bus_number);
    reg8 = 0xca;
    pci_config_write8(0, 0, 0, PCI_SECONDARY_BUS, reg8);
    ck_assert_uint_eq(reg8, hdr_mem->secondary_bus_number);
    pci_config_write32(0, 0, 0, PCI_PRIMARY_BUS, 0xaabbccdd);
    ck_assert_uint_eq(0xaabbccdd, *((uint32_t*)&(hdr_mem->primary_bus_number)));
    reg8 = pci_config_read8(0, 0, 0, PCI_SECONDARY_BUS);
    ck_assert_uint_eq(reg8, hdr_mem->secondary_bus_number);
    reg8 = 0xbe;
    pci_config_write8(0, 0, 0, PCI_PRIMARY_BUS, reg8);
    ck_assert_uint_eq(reg8, hdr_mem->primary_bus_number);

    munmap(hdr_mem, MOCKED_LEN);
}
END_TEST

Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfboot-pci");

    /* Test cases */
    TCase *pci_config  = tcase_create("pci-config-write");

    tcase_add_test(pci_config, test_pci_config_write);
    tcase_set_timeout(pci_config, 60*5);
    suite_add_tcase(s, pci_config);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
