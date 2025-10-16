/* linux_loader.h
 *
 * Headers for linux boot parameters
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

#ifndef LINUX_LOADER_H
#define LINUX_LOADER_H

#include <stdint.h>

#define E820_MAX_ENTRIES_ZEROPAGE 128

struct boot_e820_entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed));

enum e820_type {
    E820_TYPE_RAM = 1,
    E820_TYPE_RESERVED = 2,
};

struct setup_header {
    uint8_t setup_sects;
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;
    uint16_t jump;
    uint32_t header;
    uint16_t version;
    uint32_t realmode_swtch;
    uint16_t start_sys_seg;
    uint16_t kernel_version;
    uint8_t type_of_loader;
    uint8_t loadflags;
    uint16_t setup_move_size;
    uint32_t code32_start;
    uint32_t ramdisk_image;
    uint32_t ramdisk_size;
    uint32_t bootsect_kludge;
    uint16_t heap_end_ptr;
    uint8_t ext_loader_ver;
    uint8_t ext_loader_type;
    uint32_t cmd_line_ptr;
    uint32_t initrd_addr_max;
    uint32_t kernel_alignment;
    uint8_t relocatable_kernel;
    uint8_t min_alignment;
    uint16_t xloadflags;
    uint32_t cmdline_size;
    uint32_t hardware_subarch;
    uint64_t hardware_subarch_data;
    uint32_t payload_offset;
    uint32_t payload_length;
    uint64_t setup_data;
    uint64_t pref_address;
    uint32_t init_size;
    uint32_t handover_offset;
    uint32_t kernel_info_offset;
} __attribute__((packed));

/* size and layout taken from https://docs.kernel.org/x86/zero-page.html */
struct boot_params {
    uint8_t screen_info[0x40];
    uint8_t apm_bios_info[0x14];
    uint8_t _pad2[4];
    uint64_t tboot_addr;
    uint8_t ist_info[0x10];
    uint64_t acpi_rsdp_addr;
    uint8_t _pad3[8];
    uint8_t hd0_info[16];
    uint8_t hd1_info[16];
    uint8_t sys_desc_table[0x10];
    uint8_t olpc_ofw_header[0x10];
    uint32_t ext_ramdisk_image;
    uint32_t ext_ramdisk_size;
    uint32_t ext_cmd_line_ptr;
    uint8_t _pad4[112];
    uint32_t cc_blob_address;
    uint8_t edid_info[0x80];
    uint8_t efi_info[0x20];
    uint32_t alt_mem_k;
    uint32_t scratch;
    uint8_t e820_entries;
    uint8_t eddbuf_entries;
    uint8_t edd_mbr_sig_buf_entries;
    uint8_t kbd_status;
    uint8_t secure_boot;
    uint8_t _pad5[2];
    uint8_t sentinel;
    uint8_t _pad6[1];
    struct setup_header hdr;
    uint8_t _pad7[0x290 - 0x1f1 - sizeof(struct setup_header)];
    uint8_t edd_mbr_sig_buffer[0x40];
    struct boot_e820_entry e820_table[E820_MAX_ENTRIES_ZEROPAGE];
    uint8_t _pad8[48];
    uint8_t eddbuf[0x1ec];
    uint8_t _pad9[276];
} __attribute__((packed));

void load_linux(uint8_t *linux_image, void *params, const char *cmd_line);

#endif /* LINUX_LOADER_H */
