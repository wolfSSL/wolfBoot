/* hob.c
 *
 * Functions to encrypt/decrypt external flash content
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

#include <x86/hob.h>

#ifndef NULL
#define NULL 0
#endif

__attribute__((section(".text")))
static struct efi_guid hob_fsp_reserved_guid = {
    0x69a79759,
    0x1373,
    0x4367,
    { 0xa6, 0xc4, 0xc7, 0xf5, 0x9e, 0xfd, 0x98, 0x6e }
};

static int _memcmp(const void *_s1, const void *_s2, unsigned int n)
{
    int diff = 0;
    const unsigned char *s1 = (const unsigned char *)_s1;
    const unsigned char *s2 = (const unsigned char *)_s2;

    while (!diff && n) {
        diff = (int)*s1 - (int)*s2;
        s1++;
        s2++;
        n--;
    }

    return diff;
}

uint16_t hob_get_type(struct efi_hob *hob)
{
    return hob->u.header.hob_type;
}

uint16_t hob_get_length(struct efi_hob *hob)
{
    return hob->u.header.hob_length;
}

struct efi_hob *hob_get_next(struct efi_hob *hob)
{
    struct efi_hob *ret;
    ret = (struct efi_hob *)(((uint8_t *)hob) + hob_get_length(hob));
    return ret;
}

int hob_guid_equals(struct efi_guid *a, struct efi_guid *b)
{
    return _memcmp(a, b, sizeof(*a)) == 0;
}

struct efi_hob_resource_descriptor *
hob_find_resource_by_guid(struct efi_hob *hoblist, struct efi_guid *guid)
{
    struct efi_hob *it;

    it = hoblist;
    while (hob_get_type(it) != EFI_HOB_TYPE_END_OF_HOB_LIST) {
        if (hob_get_type(it) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
            if (it->u.resource_descriptor.resource_type ==
                    EFI_RESOURCE_MEMORY_RESERVED &&
                hob_guid_equals(&it->u.resource_descriptor.owner, guid)) {
                return &it->u.resource_descriptor;
            }
        }
        it = hob_get_next(it);
    }
    return NULL;
}

struct efi_hob_resource_descriptor *
hob_find_fsp_reserved(struct efi_hob *hoblist)
{
    return hob_find_resource_by_guid(hoblist, &hob_fsp_reserved_guid);
}
