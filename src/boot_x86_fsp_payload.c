/* boot_x86_fsp_payload.c
 *
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
 */
/**
 * @file boot_x86_fsp_payload.c
 *
 * @brief Boot functions for x86 FSP payload
 *
 * This file contains the boot functions for the x86 FSP payload of wolfBoot.
 * These functions include booting into Linux payload or ELF binaries.
 */
#ifndef BOOT_X86_FSP_PAYLOAD_H_
#define BOOT_X86_FSP_PAYLOAD_H_

#include <stdint.h>
#include <string.h>
#include <x86/common.h>
#include <wolfboot/wolfboot.h>

#include <printf.h>
#include <stage2_params.h>
#include <x86/mptable.h>

#if defined(WOLFBOOT_LINUX_PAYLOAD)
#include <x86/linux_loader.h>
#endif /* defined(WOLFBOOT_LINUX_PAYLOAD) */

#if defined(WOLFBOOT_TPM)
#include <tpm.h>
#endif /* WOLFBOOT_TPM */

#if defined(WOLFBOOT_ELF)
#include <elf.h>
#include <multiboot.h>

/**
 * @brief Maximum size of Multiboot2 boot info data structure.
 */
#define MAX_MB2_BOOT_INFO_SIZE 0x2000
/**
 * @brief Multiboot2 boot info data structure.
 */
uint8_t mb2_boot_info[MAX_MB2_BOOT_INFO_SIZE];
#endif
#ifdef WOLFBOOT_64BIT
#include <x86/paging.h>
#endif /* WOLFBOOT_64BIT */

#ifdef TARGET_kontron_vx3060_s2
static char *cmdline = "apic=verbose acpi=no pci=debug console=ttyS0,115200 debug";
#elif defined(TARGET_nai_68int6)
#define CMDLINE_STR_(x) #x
#define CMDLINE_STR(x) CMDLINE_STR_(x)
/* earlycon covers LPSS UART2 at its FSP-assigned base until the 8250 driver
 * binds it as ttyS4 */
static char *cmdline =
    "earlycon=uart8250,mmio32," CMDLINE_STR(X86_UART_BASE) ",115200n8 "
    "console=ttyS4,115200 console=tty0 "
    "root=/dev/sda2 rw rootwait";
#elif TARGET_x86_fsp_qemu
static char *cmdline = "console=ttyS0,115200 pci=earlydump debug";
#else
static char *cmdline = "auto";
#endif /* TARGET_kontron_vx3060_s2 */

/**
 * @brief Jump to the specified entry point.
 *
 * This function performs an unconditional jump to the provided entry point.
 *
 * @param entry The entry point address to jump to.
 */
void jump(uintptr_t entry)
{
    __asm__(
            "jmp *%0\r\n"
            :
            : "g"(entry));
}

#if defined(WOLFBOOT_ELF)
/* wolfBoot's own memory: stage 2, the staged payload after it, and the stack,
 * page tables and FSP memory above them, all from the stage 2 load address
 * up to 4 GB. */
#define ELF_RESERVED_START ((uint64_t)WOLFBOOT_LOAD_BASE - IMAGE_HEADER_SIZE)
#define ELF_RESERVED_END (1ULL << 32)

static int elf_reserved_overlap(uint64_t addr, uint32_t size)
{
    return size != 0 && addr < ELF_RESERVED_END &&
           addr + size > ELF_RESERVED_START;
}

/* The loader copies through vaddr, and an address that is already mapped
 * keeps its identity mapping, so both ranges are checked. */
static int boot_elf_map_cb(uint64_t vaddr, uint64_t paddr, uint32_t size)
{
    if (elf_reserved_overlap(paddr, size) ||
        elf_reserved_overlap(vaddr, size)) {
        wolfBoot_printf("ELF: segment at 0x%x_%x overlaps wolfBoot memory\r\n",
                        (uint32_t)(paddr >> 32), (uint32_t)paddr);
        return -1;
    }
#ifdef WOLFBOOT_64BIT
    return x86_paging_map_memory(vaddr, paddr, size);
#else
    (void)vaddr;
    return 0;
#endif
}

/**
 * @brief Load an ELF image and boot it, through Multiboot2 when enabled.
 *
 * @param app Pointer to the start of the ELF image.
 * @param stage2_params Stage 2 parameters (memory layout and FSP HOBs).
 */
static void boot_elf(const uint32_t *app,
                     struct stage2_parameter *stage2_params)
{
    int r;
    uint64_t e = 0;
    uint8_t *mb2_header;

    /* TODO: to remove */
    mptable_setup();
    x86_paging_dump_info();
    /* The disk loader records the verified payload size */
    if (stage2_params->payload_size == 0) {
        wolfBoot_printf("ELF: no verified payload size\r\n");
        panic();
    }
    r = elf_load_image_mmu((uint8_t *)app, stage2_params->payload_size, &e,
            boot_elf_map_cb);
    wolfBoot_printf("Elf loaded (ret %d), entry 0x%x_%x\r\n", r,
                    (uint32_t)(e >> 32),
                    (uint32_t)(e));
    x86_paging_dump_info();
    if (r != 0)
        panic();

#ifdef WOLFBOOT_MULTIBOOT2
    mb2_header = mb2_find_header((uint8_t*)app,
                                 (int)stage2_params->payload_size);
    if (mb2_header == NULL) {
        wolfBoot_printf("No mb2 header found\r\n");
        panic();
    }
    wolfBoot_printf("mb2 header found at %x\r\n",
                    (uint32_t)(uintptr_t)mb2_header);
    r = mb2_build_boot_info_header(mb2_boot_info, mb2_header,
                                   stage2_params,
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
}
#endif /* WOLFBOOT_ELF */

/**
 * @brief Perform the boot process for the given application.
 *
 * This function performs the boot process for the specified application.
 * It either loads and boots a Linux payload or an ELF binary, depending on the
 * configuration. With both built in, an image starting with the ELF magic
 * takes the ELF path and anything else boots as Linux.
 *
 * @param app Pointer to the start of the application.
 */
void do_boot(const uint32_t *app)
{
    struct stage2_parameter *stage2_params;

#ifdef WOLFBOOT_TPM
    wolfBoot_tpm2_deinit();
#endif /* WOLFBOOT_TPM */

    stage2_params = stage2_get_parameters();
#if defined(WOLFBOOT_LINUX_PAYLOAD) && defined(WOLFBOOT_ELF)
    if (memcmp(app, ELF_IDENT_STR, 4) == 0) {
        boot_elf(app, stage2_params);
        return;
    }
#endif
#if defined(WOLFBOOT_LINUX_PAYLOAD)
    mptable_setup();
    load_linux((uint8_t *)app, stage2_params->payload_size, stage2_params,
               stage2_params->cmdline != 0 ?
               (const char *)(uintptr_t)stage2_params->cmdline : cmdline);
#elif defined(WOLFBOOT_ELF)
    boot_elf(app, stage2_params);
#else
#error "No payload compiled in"
#endif /* WOLFBOOT_LINUX_PAYLOAD */
}
#endif /* BOOT_X86_FSP_PAYLOAD_H_ */
