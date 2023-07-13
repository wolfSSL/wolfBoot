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

#include <loader.h>
#include <stdint.h>
#include <string.h>
#include <printf.h>

#include <x86/linux_loader.h>

#define ENDLINE "\r\n"

#ifdef WOLFBOOT_64BIT
#error "Linux loader 64bit is not supported"
#endif
static void jump_to_linux(uint32_t kernel_addr, struct boot_params *p)
{
    (void)kernel_addr;
    (void)p;

#if !defined(WOLFBOOT_64BIT)
    __asm__ __volatile__("movl %0, %%esi\n\t"
                         "xorl %%ebp, %%ebp\n\t"
                         "xorl %%edi, %%edi\n\t"
                         "xorl %%ebx, %%ebx\n\t"
                         "jmp *%1"
                         :
                         : "r"(p), "r"(kernel_addr)
                         : "esi", "edi", "ebx");
#endif /* WOLFBOOT_64BIT */
}


static int linux_boot_params_fill_memory_map(struct boot_params *bp,
                                             void *stage2_params)
{
    (void)bp;
    (void)stage2_params;
    return -1;
}

#define KERNEL_LOAD_ADDRESS 0x100000
#define KERNEL_CMDLINE_ADDRESS 0x10000

void load_linux(uint8_t *linux_image, void *params, const char *cmd_line)
{
    struct boot_params param = { 0 };
    uint32_t kernel_size, param_size;
    uint8_t *image_boot_param;
    uint16_t end_of_header_off;
    uint8_t *_cmd_line;
    (void)cmd_line;
    int ret;

    wolfBoot_printf("linux payload" ENDLINE);

    image_boot_param = linux_image + 0x1f1;
    end_of_header_off = *(linux_image + 0x201) + 0x202;
    memcpy((uint8_t*)&param.hdr,
            image_boot_param, sizeof(struct setup_header));

    ret = linux_boot_params_fill_memory_map(&param,
                                            (struct efi_hob*)
                                            params);
    if (ret != 0) {
        wolfBoot_printf("fail to compute the memory map" ENDLINE);
        wolfBoot_panic();
    }

    if (param.hdr.setup_sects != 0) {
        param_size = (param.hdr.setup_sects + 1) * 512;
    } else {
        param_size = 5 * 512;
    }

    _cmd_line = (uint8_t*)KERNEL_CMDLINE_ADDRESS;
    memcpy(_cmd_line, (uint8_t*)cmd_line, strlen(cmd_line)+1);
    param.hdr.type_of_loader = 0xff;
    param.hdr.cmd_line_ptr = (uint32_t)(uintptr_t)_cmd_line;
    kernel_size = param.hdr.syssize * 16;
    memcpy((uint8_t *)KERNEL_LOAD_ADDRESS, linux_image + param_size,
           kernel_size);

    wolfBoot_printf("booting..." ENDLINE);
    jump_to_linux(param.hdr.code32_start, &param);
}
