/* hob.h
 *
 * Headers for Hand-off Block (HOB) objects
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

#ifndef HOB_H_INCLUDED
#define HOB_H_INCLUDED

#include <stdint.h>

typedef int (*hob_mem_map_cb)(uint64_t start, uint64_t length, uint32_t type,
                              void *ctx);

enum hob_type {
    EFI_HOB_TYPE_HANDOFF = 0x0001,
    EFI_HOB_TYPE_MEMORY_ALLOCATION = 0x0002,
    EFI_HOB_TYPE_RESOURCE_DESCRIPTOR = 0x0003,
    EFI_HOB_TYPE_GUID_EXTENSION = 0x0004,
    EFI_HOB_TYPE_FV = 0x0005,
    EFI_HOB_TYPE_CPU = 0x0006,
    EFI_HOB_TYPE_MEMORY_POOL = 0x0007,
    EFI_HOB_TYPE_FV2 = 0x0009,
    EFI_HOB_TYPE_LOAD_PEIM_UNUSED = 0x000A,
    EFI_HOB_TYPE_UEFI_CAPSULE = 0x000B,
    EFI_HOB_TYPE_FV3 = 0x000C,
    EFI_HOB_TYPE_UNUSED = 0xFFFE,
    EFI_HOB_TYPE_END_OF_HOB_LIST = 0xFFFF,
};

enum boot_type {
    BOOT_WITH_FULL_CONFIGURATION = 0x00,
    BOOT_WITH_MINIMAL_CONFIGURATION = 0x01,
    BOOT_ASSUMING_NO_CONFIGURATION_CHANGES = 0x02,
    BOOT_WITH_FULL_CONFIGURATION_PLUS_DIAGNOSTICS = 0x03,
    BOOT_WITH_DEFAULT_SETTINGS = 0x04,
    BOOT_ON_S4_RESUME = 0x05,
    BOOT_ON_S5_RESUME = 0x06,
    BOOT_WITH_MFG_MODE_SETTINGS = 0x07,
    BOOT_ON_S2_RESUME = 0x10,
    BOOT_ON_S3_RESUME = 0x11,
    BOOT_ON_FLASH_UPDATE = 0x12,
    BOOT_IN_RECOVERY_MODE = 0x20,
};

typedef uint64_t efi_physical_address;

struct efi_hob_generic_header {
    uint16_t hob_type;
    uint16_t hob_length;
    uint32_t reserved;
};

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct efi_hob_handoff_info_table {
    struct efi_hob_generic_header header;
    uint32_t version;
    uint32_t boot_mode;
    efi_physical_address efi_memory_top;
    efi_physical_address efi_memory_bottom;
    efi_physical_address efi_free_memory_top;
    efi_physical_address efi_free_memory_bottom;
    efi_physical_address efi_end_of_hob_list;
};

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} efi_memory_type;

struct efi_hob_memory_allocation_header {
    struct efi_guid name;
    efi_physical_address memory_base_address;
    uint64_t memory_length;
    efi_memory_type memory_type;
    uint8_t reserved[4];
};

struct efi_hob_memory_allocation {
    struct efi_hob_generic_header header;
    struct efi_hob_memory_allocation_header alloc_descriptor;
};

enum resource_type {
    EFI_RESOURCE_SYSTEM_MEMORY = 0x00000000,
    EFI_RESOURCE_MEMORY_MAPPED_IO = 0x00000001,
    EFI_RESOURCE_IO = 0x00000002,
    EFI_RESOURCE_FIRMWARE_DEVICE = 0x00000003,
    EFI_RESOURCE_MEMORY_MAPPED_IO_PORT = 0x00000004,
    EFI_RESOURCE_MEMORY_RESERVED = 0x00000005,
    EFI_RESOURCE_IO_RESERVED = 0x00000006,
    EFI_RESOURCE_MAX_MEMORY_TYPE = 0x00000007,
};

enum resource_attribute {
    EFI_RESOURCE_ATTRIBUTE_PRESENT = 0x00000001,
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED = 0x00000002,
    EFI_RESOURCE_ATTRIBUTE_TESTED = 0x00000004,
    EFI_RESOURCE_ATTRIBUTE_READ_PROTECTED = 0x00000080,
    EFI_RESOURCE_ATTRIBUTE_WRITE_PROTECTED = 0x00000100,
    EFI_RESOURCE_ATTRIBUTE_EXECUTION_PROTECTED = 0x00000200,
    EFI_RESOURCE_ATTRIBUTE_PERSISTENT = 0x00800000,
    EFI_RESOURCE_ATTRIBUTE_SINGLE_BIT_ECC = 0x00000008,
    EFI_RESOURCE_ATTRIBUTE_MULTIPLE_BIT_ECC = 0x00000010,
    EFI_RESOURCE_ATTRIBUTE_ECC_RESERVED_1 = 0x00000020,
    EFI_RESOURCE_ATTRIBUTE_ECC_RESERVED_2 = 0x00000040,
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE = 0x00000400,
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE = 0x00000800,
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE = 0x00001000,
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE = 0x00002000,
    EFI_RESOURCE_ATTRIBUTE_16_BIT_IO = 0x00004000,
    EFI_RESOURCE_ATTRIBUTE_32_BIT_IO = 0x00008000,
    EFI_RESOURCE_ATTRIBUTE_64_BIT_IO = 0x00010000,
    EFI_RESOURCE_ATTRIBUTE_UNCACHED_EXPORTED = 0x00020000,
    EFI_RESOURCE_ATTRIBUTE_READ_PROTECTABLE = 0x00100000,
    EFI_RESOURCE_ATTRIBUTE_WRITE_PROTECTABLE = 0x00200000,
    EFI_RESOURCE_ATTRIBUTE_EXECUTION_PROTECTABLE = 0x00400000,
    EFI_RESOURCE_ATTRIBUTE_PERSISTABLE = 0x01000000,
    EFI_RESOURCE_ATTRIBUTE_READ_ONLY_PROTECTED = 0x00040000,
    EFI_RESOURCE_ATTRIBUTE_READ_ONLY_PROTECTABLE = 0x00080000,
    EFI_RESOURCE_ATTRIBUTE_MORE_RELIABLE = 0x02000000,
};

struct efi_hob_resource_descriptor {
    struct efi_hob_generic_header header;
    struct efi_guid owner;
    uint32_t resource_type;
    uint32_t resource_attribute;
    efi_physical_address physical_start;
    uint64_t resource_length;
};

struct efi_hob_guid_type {
    struct efi_hob_generic_header header;
    struct efi_guid name;
};

struct efi_hob_cpu {
    struct efi_hob_generic_header header;
    uint8_t size_of_memory_space;
    uint8_t size_of_io_space;
    uint8_t reserved[6];
};

/* Union of all the possible HOB Types. */
struct efi_hob {
    union {
        struct efi_hob_generic_header header;
        struct efi_hob_handoff_info_table handoff_information_table;
        struct efi_hob_memory_allocation memory_allocation;
        struct efi_hob_resource_descriptor resource_descriptor;
        struct efi_hob_guid_type guid;
        struct efi_hob_cpu cpu;
        uint8_t raw;
    } u;
};

uint16_t hob_get_type(struct efi_hob *hob);
uint16_t hob_get_length(struct efi_hob *hob);
struct efi_hob *hob_get_next(struct efi_hob *hob);
int hob_guid_equals(struct efi_guid *a, struct efi_guid *b);

struct efi_hob_resource_descriptor *
hob_find_resource_by_guid(struct efi_hob *hoblist, struct efi_guid *guid);

struct efi_hob_resource_descriptor *
hob_find_fsp_reserved(struct efi_hob *hoblist);
int hob_iterate_memory_map(struct efi_hob *hobList, hob_mem_map_cb cb,
                           void* ctx);
#ifdef DEBUG
void hob_dump_memory_map(struct efi_hob *hobList);
#endif /* DEBUG */
#endif
