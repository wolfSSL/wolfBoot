/* linux_loader.c
 * Copyright (C) 2026 wolfSSL Inc.
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
 *
 *
 * Linux x86 boot protocol implementation (32-bit and 64-bit entry)
 */

#include <loader.h>
#include <stdint.h>
#include <string.h>
#include <printf.h>

#include <x86/linux_loader.h>
#ifdef WOLFBOOT_64BIT
#include <x86/paging.h>
#endif

#ifdef WOLFBOOT_FSP
#include <x86/hob.h>
#include <stage2_params.h>
#endif /* WOLFBOOT_FSP */

#define ENDLINE "\r\n"

/* XLF_KERNEL_64: the kernel has a 64-bit entry point (boot protocol >= 2.12,
 * xloadflags bit 0). Without it a 64-bit loader has nothing to jump to. */
#define XLF_KERNEL_64 (1u << 0)

static void jump_to_linux(uint64_t kernel_addr, struct boot_params *p)
{
#if defined(WOLFBOOT_64BIT)
    /* 64-bit boot protocol: in long mode already, so pass the zero page in RSI,
     * clear RDI as the protocol requires, and jump to the 64-bit entry (0x200
     * past the loaded kernel), interrupts off. The kernel sets up its own GDT
     * and stack. */
    __asm__ __volatile__("cli\n\t"
                         "movq %0, %%rsi\n\t"
                         "xorl %%edi, %%edi\n\t"
                         "jmp *%1"
                         :
                         : "r"(p), "r"(kernel_addr)
                         : "rsi", "rdi", "memory");
#else
    __asm__ __volatile__("movl %0, %%esi\n\t"
                         "xorl %%ebp, %%ebp\n\t"
                         "xorl %%edi, %%edi\n\t"
                         "xorl %%ebx, %%ebx\n\t"
                         "jmp *%1"
                         :
                         : "r"((uint32_t)p), "r"((uint32_t)kernel_addr)
                         : "esi", "edi", "ebx");
#endif /* WOLFBOOT_64BIT */
}

#ifdef WOLFBOOT_FSP

#ifdef DEBUG
static void print_e820_entry(struct boot_e820_entry *e)
{
    wolfBoot_printf("start: %x" ENDLINE, (uint32_t)e->addr);
    wolfBoot_printf("size: %x" ENDLINE, (uint32_t)e->size);
    wolfBoot_printf("type: %s" ENDLINE,
                    ((uint32_t)e->type == E820_TYPE_RAM ? "ram" : "reserved"));
}
#else
static inline void print_e820_entry(struct boot_e820_entry *e) {}
#endif /* DEBUG */

static int e820_add_entry_cb(uint64_t start, uint64_t length, uint32_t type,
                             void *ctx)
{
    struct boot_params *bp = (struct boot_params*)ctx;
    struct boot_e820_entry *map;

    if (bp->e820_entries >= E820_MAX_ENTRIES_ZEROPAGE)
        return -1;

    map = bp->e820_table + bp->e820_entries;
    map->addr = start;
    map->size = length;
    map->type = (type == EFI_RESOURCE_SYSTEM_MEMORY) ? E820_TYPE_RAM :
        E820_TYPE_RESERVED;
    bp->e820_entries++;
    return 0;
}

static int memory_map_from_hoblist(struct boot_params *bp,
                                   struct efi_hob *hobList)
{
    return hob_iterate_memory_map(hobList, e820_add_entry_cb, (void *)bp);
}
#endif /* WOLFBOOT_FSP */

static int linux_boot_params_fill_memory_map(struct boot_params *bp,
                                             void *stage2_params)
{
#ifdef WOLFBOOT_FSP
    struct stage2_parameter *p = (struct stage2_parameter *)stage2_params;
    return memory_map_from_hoblist(bp, (struct efi_hob*)(uintptr_t)p->hobList);
#else
    (void)bp;
    (void)stage2_params;
    return -1;
#endif /* WOLFBOOT_FSP */
}

#define KERNEL_LOAD_ADDRESS 0x100000
#define KERNEL_CMDLINE_ADDRESS 0x10000

/* Compute the protected-mode kernel size (syssize * 16) in 64-bit to avoid the
 * uint32_t multiplication wrap, and reject any image whose kernel would not fit
 * in the destination window [KERNEL_LOAD_ADDRESS, load_limit).
 *
 * load_limit == 0 means "no destination window bound is known" (non-FSP builds,
 * which do not expose a tolum): in that case only the wrap and zero-size cases
 * are rejected, bounding the result to what fits in the uint32_t kernel_size.
 *
 * Returns 0 on success, -1 if the size is zero or out of range. */
static int linux_kernel_size(uint32_t syssize, uint32_t load_limit,
                             uint32_t *kernel_size)
{
    uint64_t ksz = (uint64_t)syssize * 16u;
    uint64_t max_size;

    if (load_limit == 0)
        max_size = (uint64_t)0xFFFFFFFFu;
    else if (load_limit <= KERNEL_LOAD_ADDRESS)
        return -1;
    else
        max_size = (uint64_t)(load_limit - KERNEL_LOAD_ADDRESS);

    if (ksz == 0 || ksz > max_size)
        return -1;
    *kernel_size = (uint32_t)ksz;
    return 0;
}

void load_linux(uint8_t *linux_image, void *params, const char *cmd_line)
{
    struct boot_params param = { 0 };
    uint32_t kernel_size, param_size, load_limit;
    uint8_t *image_boot_param;
    uint16_t end_of_header_off;
    uint8_t *_cmd_line;
    int ret;
#if defined(WOLFBOOT_64BIT)
    uint32_t map_size;
#endif
    (void)cmd_line;

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
#ifdef WOLFBOOT_FSP
    load_limit = ((struct stage2_parameter *)params)->tolum;
#else
    load_limit = 0;
#endif /* WOLFBOOT_FSP */
    if (linux_kernel_size(param.hdr.syssize, load_limit, &kernel_size) != 0) {
        wolfBoot_printf("invalid kernel size" ENDLINE);
        wolfBoot_panic();
    }
    memcpy((uint8_t *)KERNEL_LOAD_ADDRESS, linux_image + param_size,
           kernel_size);

#if defined(WOLFBOOT_64BIT)
    /* A 64-bit build needs the kernel's 64-bit entry point. */
    if ((param.hdr.xloadflags & XLF_KERNEL_64) == 0) {
        wolfBoot_printf("kernel has no 64-bit entry (xloadflags 0x%x)" ENDLINE,
                        param.hdr.xloadflags);
        wolfBoot_panic();
    }

    /* Identity-map the load region (init_size bytes of scratch), the command
     * line and the zero page; the kernel cannot fault these in itself. */
    map_size = param.hdr.init_size;
    if (kernel_size > map_size) {
        map_size = kernel_size;
    }
    x86_paging_map_memory(KERNEL_LOAD_ADDRESS, KERNEL_LOAD_ADDRESS, map_size);
    x86_paging_map_memory(KERNEL_CMDLINE_ADDRESS, KERNEL_CMDLINE_ADDRESS, 0x1000);
    x86_paging_map_memory((uint64_t)(uintptr_t)&param,
                          (uint64_t)(uintptr_t)&param, sizeof(param));

    /* 64-bit entry point: 0x200 past the loaded protected-mode kernel. */
    wolfBoot_printf("booting (64-bit entry)..." ENDLINE);
    jump_to_linux((uint64_t)KERNEL_LOAD_ADDRESS + 0x200, &param);
#else
    wolfBoot_printf("booting..." ENDLINE);
    jump_to_linux(param.hdr.code32_start, &param);
#endif
}
