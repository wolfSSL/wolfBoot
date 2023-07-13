/* boot_x86_fsp_payload.c
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
#include <string.h>
#include <x86/common.h>

#include <printf.h>
#include <stage1.h>
#include <x86/mptable.h>

#if defined(WOLFBOOT_LINUX_PAYLOAD)
#include <x86/linux_loader.h>
#endif /* defined(WOLFBOOT_LINUX_PAYLOAD) */

#if defined(WOLFBOOT_ELF)
#include <elf.h>
#include <multiboot.h>

#define MAX_MB2_BOOT_INFO_SIZE 0x2000
uint8_t mb2_boot_info[MAX_MB2_BOOT_INFO_SIZE];
#endif
#ifdef WOLFBOOT_64BIT
#include <x86/paging.h>
#endif /* WOLFBOOT_64BIT */

extern uint8_t *_stage2_params[];

#ifdef TARGET_kontron_vx3060_s2
static char *cmdline = "earlyprintk=pciserial,force,00:1e.0,115200 apic=verbose console=ttyS0,115200 pci=earlydump debug";
#elif TARGET_x86_fsp_qemu
static char *cmdline = "console=ttyS0,115200 pci=earlydump debug";
#else
static char *cmdline = "auto";
#endif /* TARGET_kontron_vx3060_s2 */

void jump(uintptr_t entry)
{
    __asm__(
            "jmp *%0\r\n"
            :
            : "g"(entry));
}


void do_boot(const uint32_t *app)
{

#if defined(WOLFBOOT_LINUX_PAYLOAD)
    mptable_setup();
    load_linux((uint8_t *)app, (struct stage2_parameter*)_stage2_params,
               cmdline);
#elif defined(WOLFBOOT_ELF)
    int r;
    uint64_t e;
    uint8_t *mb2_header;
    elf_mmu_map_cb mmu_cb = NULL;

#ifdef WOLFBOOT_64BIT
    mmu_cb = x86_paging_map_memory;
#endif /* WOLFBOOT_64BIT */

    /* TODO: to remove */
    mptable_setup();
    r = elf_load_image_mmu((uint8_t *)app, &e, mmu_cb);
    wolfBoot_printf("Elf loaded (ret %d), entry 0x%x_%x\r\n", 0,
                    (uint32_t)(e >> 32),
                    (uint32_t)(e));
    if (r != 0)
        panic();

#ifdef WOLFBOOT_MULTIBOOT2
    mb2_header = mb2_find_header((uint8_t*)app, 1024*1024*15);
    if (mb2_header == NULL) {
        wolfBoot_printf("No mb2 header found\r\n");
        panic();
    }
    wolfBoot_printf("mb2 header found at %x\r\n",
                    (uint32_t)(uintptr_t)mb2_header);
    r = mb2_build_boot_info_header(mb2_boot_info, mb2_header,
                                   (struct stage2_parameter*)_stage2_params,
                                   MAX_MB2_BOOT_INFO_SIZE);
    if (r != 0) {
        wolfBoot_printf("can't build multiboot2 header, panicking\r\n");
        panic();
    }
    wolfBoot_printf("booting...\r\n");
    mb2_jump(e, (uint32_t)(uintptr_t)mb2_boot_info);
#else
    jump(e);
#endif /* WOLFBOOT_MULTIBOOT2 */

#else
#error "No payload compiled in"
#endif /* WOLFBOOT_LINUX_PAYLOAD */
}
