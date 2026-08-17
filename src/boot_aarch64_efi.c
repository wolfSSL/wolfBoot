/* boot_aarch64_efi.c
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

/* AArch64 UEFI-application boot glue (generic AArch64 UEFI target).
 *
 * When wolfBoot is built as a UEFI application the gnu-efi CRT0 is the entry
 * point and efi_main() (hal/aarch64_efi.c) does all the work, so none of the
 * bare-metal reset / EL2->EL1 / cache-teardown path in boot_aarch64.c applies.
 * This provides the do_boot() the updater (src/update_ram.c) calls; it simply
 * forwards the verified image to the UEFI LoadImage/StartImage handoff in
 * hal/aarch64_efi.c. Mirrors src/boot_x86_64.c. */

#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"

#ifdef TARGET_aarch64_efi

/* aarch64_efi never defines MMU (UEFI owns MMU/FDT), so do_boot has only the
 * app-only form; it forwards the verified image to the LoadImage/StartImage
 * handoff in hal/aarch64_efi.c. */
extern void RAMFUNCTION aarch64_efi_do_boot(const uint32_t *boot_addr);

void RAMFUNCTION do_boot(const uint32_t *app_offset)
{
    aarch64_efi_do_boot(app_offset);
}

#endif /* TARGET_aarch64_efi */
