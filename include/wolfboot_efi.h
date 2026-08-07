/* wolfboot_efi.h
 *
 * Shared helpers for the wolfBoot EFI-application targets (aarch64_efi,
 * x86_64_efi).
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

#ifndef WOLFBOOT_EFI_H
#define WOLFBOOT_EFI_H

/* IMPORTANT: include this AFTER the gnu-efi headers (<efi/efi.h>,
 * <efi/efilib.h>) so the EFI types (CHAR16/UINTN/BS/uefi_call_wrapper) are
 * visible. wolfBoot declarations (HDR_CMDLINE, IMAGE_HEADER_OFFSET,
 * wolfBoot_find_header) come from wolfboot/wolfboot.h, included below. */
#include "wolfboot/wolfboot.h"

/* Return the authenticated OS command line (the HDR_CMDLINE TLV) from a
 * VERIFIED wolfBoot manifest header as a NUL-terminated CHAR16 buffer suitable
 * for an EFI LoadedImage's LoadOptions. The TLV is covered by the image
 * signature, so this must be called only AFTER the image has been verified.
 *
 * `manifest` points at the wolfBoot header (i.e. boot_addr - IMAGE_HEADER_SIZE).
 * Returns NULL and sets *out_bytes = 0 when the image carries no command-line
 * TLV (the caller then boots with no command line, so the kernel uses its
 * built-in CONFIG_CMDLINE). The returned buffer is allocated with
 * BS->AllocatePool (EfiLoaderData) and includes the terminating NUL in
 * *out_bytes (a byte count). */
static inline CHAR16 *wolfBoot_efi_get_cmdline(const uint8_t *manifest,
                                               UINTN *out_bytes)
{
    uint8_t *val = NULL;
    uint16_t len;
    UINTN i;
    CHAR16 *wide = NULL;
    EFI_STATUS status;

    *out_bytes = 0;
    if (manifest == NULL)
        return NULL;

    len = wolfBoot_find_header((uint8_t *)(manifest + IMAGE_HEADER_OFFSET),
                               HDR_CMDLINE, &val);
    if (len == 0 || val == NULL)
        return NULL; /* no signed command line present */

    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               ((UINTN)len + 1) * sizeof(CHAR16), (void **)&wide);
    if (status != EFI_SUCCESS || wide == NULL)
        return NULL;

    /* The TLV value is ASCII and not NUL-terminated; widen exactly len bytes. */
    for (i = 0; i < (UINTN)len; i++)
        wide[i] = (CHAR16)val[i];
    wide[len] = 0;

    *out_bytes = ((UINTN)len + 1) * sizeof(CHAR16);
    return wide;
}

#endif /* WOLFBOOT_EFI_H */
