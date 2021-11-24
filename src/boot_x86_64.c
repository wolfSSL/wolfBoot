/* boot_x86_64.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"

#ifdef PLATFORM_X86_64_EFI

#include <efi/efi.h>
#include <efi/efilib.h>

extern unsigned int __bss_start__;
extern unsigned int __bss_end__;
static volatile unsigned int cpu_id;
extern unsigned int *END_STACK;

extern void RAMFUNCTION x86_64_efi_do_boot(uint8_t *kernel);

#ifdef MMU
void RAMFUNCTION do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void RAMFUNCTION do_boot(const uint32_t *app_offset)
#endif
{
    x86_64_efi_do_boot((uint8_t *)app_offset);
}

#endif /* PLATFORM_X86_64_EFI */
