/* linux_loader.c
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
 *
 *
 * Linux x86 32bit protocol implementation
 */

#include "loader.h"
#include <string.h>
#include <x86/linux.h>
#include <x86/fsp.h>
#include <printf.h>

#ifdef PLATFORM_x86_fsp
#include <x86/hob.h>
#endif /* PLATFORM_x86_fsp */

static void jump_to_linux(uint32_t kernel_addr, struct boot_params *p)
{
    (void)kernel_addr;
    (void)p;

    __asm__ __volatile__("movl %0, %%esi\n\t"
                         "xorl %%ebp, %%ebp\n\t"
                         "xorl %%edi, %%edi\n\t"
                         "xorl %%ebx, %%ebx\n\t"
                         "jmp *%1"
                         :
                         : "r"(p), "r"(kernel_addr)
                         : "esi", "edi", "ebx");
}

#ifdef PLATFORM_x86_fsp
static int memory_map_from_hoblist(struct boot_params *bp,
                                   struct efi_hob *hobList)
{
    struct boot_e820_entry *map = bp->e820_table;

    while (hob_get_type(hobList) != EFI_HOB_TYPE_END_OF_HOB_LIST) {
        if (hob_get_type(hobList) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
            map->addr = hobList->u.resource_descriptor.physical_start;
            map->size = hobList->u.resource_descriptor.resource_length;
            if (hobList->u.resource_descriptor.resource_type ==
                EFI_RESOURCE_SYSTEM_MEMORY) {
                map->type = 1;
            } else {
                map->type = 2;
            }
            bp->e820_entries++;
            if (bp->e820_entries++ >= E820_MAX_ENTRIES_ZEROPAGE)
                return -1;
            map++;
        }
        hobList = hob_get_next(hobList);
    }

    return 0;
}
#endif /* PLATFORM_x86_fsp */

static int linux_boot_params_fill_memory_map(struct boot_params *bp,
                                             struct efi_hob *hobList)
{
#ifdef PLATFORM_x86_fsp
    return memory_map_from_hoblist(bp, hobList);
#endif

    return -1;
}

#define KERNEL_LOAD_ADDRESS 0x100000

void load_linux(uint8_t *linux_image,
                struct stage2_parameter *params,
                const char *cmd_line)
{
    struct boot_params param = { 0 };
    uint32_t kernel_size, param_size;
    uint8_t *image_boot_param;
    uint16_t end_of_header;
    int ret;

    image_boot_param = linux_image + 0x1f1;
    end_of_header = *(linux_image + 0x201) + 0x202;
    memcpy((uint8_t*)&param.hdr,
            image_boot_param, sizeof(struct setup_header));

    ret = linux_boot_params_fill_memory_map(&param, params->hobList);
    if (ret != 0) {
        wolfBoot_printf("fail to compute the memory map\n");
        wolfBoot_panic();
    }

    if (param.hdr.setup_sects != 0) {
        param_size = (param.hdr.setup_sects + 1) * 512;
    } else {
        param_size = 5 * 512;
    }

    param.hdr.type_of_loader = 0xff;
    param.hdr.cmd_line_ptr = (uint32_t)cmd_line;
    kernel_size = param.hdr.syssize * 16;
    memcpy((uint8_t *)KERNEL_LOAD_ADDRESS, linux_image + param_size,
           kernel_size);
    jump_to_linux(param.hdr.code32_start, &param);
}
