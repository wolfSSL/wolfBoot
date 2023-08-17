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
#ifndef HOB_C
#define HOB_C
#include <stdint.h>
#include <x86/hob.h>
#include <string.h>
#ifndef NULL
#define NULL 0
#endif
#include <printf.h>

__attribute__((section(".text")))
static struct efi_guid hob_fsp_reserved_guid = {
    0x69a79759,
    0x1373,
    0x4367,
    { 0xa6, 0xc4, 0xc7, 0xf5, 0x9e, 0xfd, 0x98, 0x6e }
};

/**
 * @brief Retrieves the HOB type from the provided HOB structure.
 *
 * This function takes a pointer to an EFI_HOB structure and returns the HOB type
 * as a uint16_t.
 *
 * @param[in] hob Pointer to the EFI_HOB structure.
 * @return The HOB type as a uint16_t.
 */
uint16_t hob_get_type(struct efi_hob *hob)
{
    return hob->u.header.hob_type;
}

/**
 * @brief Retrieves the length of the HOB structure from the provided HOB structure.
 *
 * This function takes a pointer to an EFI_HOB structure and returns the length
 * of the HOB structure as a uint16_t.
 *
 * @param[in] hob Pointer to the EFI_HOB structure.
 * @return The length of the HOB structure as a uint16_t.
 */
uint16_t hob_get_length(struct efi_hob *hob)
{
    return hob->u.header.hob_length;
}

/**
 * @brief Retrieves the next HOB structure in the HOB list.
 *
 * This function takes a pointer to an EFI_HOB structure and returns a pointer to
 * the next HOB structure in the HOB list.
 *
 * @param[in] hob Pointer to the EFI_HOB structure.
 * @return A pointer to the next HOB structure or NULL if the end of the list is
 * reached.
 */
struct efi_hob *hob_get_next(struct efi_hob *hob)
{
    struct efi_hob *ret;
    ret = (struct efi_hob *)(((uint8_t *)hob) + hob_get_length(hob));
    return ret;
}

/**
 * @brief Compares two EFI_GUID structures for equality.
 *
 * This function takes two pointers to EFI_GUID structures and compares them
 * for equality. It returns 1 if the GUIDs are equal and 0 otherwise.
 *
 * @param[in] a Pointer to the first EFI_GUID structure.
 * @param[in] b Pointer to the second EFI_GUID structure.
 * @return 1 if the GUIDs are equal, 0 otherwise.
 */
int hob_guid_equals(struct efi_guid *a, struct efi_guid *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/**
 * @brief Finds an EFI_HOB_RESOURCE_DESCRIPTOR with a specific owner GUID.
 *
 * This function searches the HOB list for an EFI_HOB_RESOURCE_DESCRIPTOR with
 * the specified owner GUID. If found, it returns a pointer to the descriptor,
 * otherwise, it returns NULL.
 *
 * @param[in] hoblist Pointer to the beginning of the HOB list.
 * @param[in] guid Pointer to the owner GUID to search for.
 * @return A pointer to the EFI_HOB_RESOURCE_DESCRIPTOR or NULL if not found.
 */
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

/**
 * @brief Finds the EFI_HOB_RESOURCE_DESCRIPTOR with the FSP reserved GUID.
 *
 * This function searches the HOB list for the EFI_HOB_RESOURCE_DESCRIPTOR
 * with the FSP reserved GUID and returns a pointer to the descriptor if found.
 * If not found, it returns NULL.
 *
 * @param[in] hoblist Pointer to the beginning of the HOB list.
 * @return A pointer to the EFI_HOB_RESOURCE_DESCRIPTOR or NULL if not found.
 */
struct efi_hob_resource_descriptor *
hob_find_fsp_reserved(struct efi_hob *hoblist)
{
    return hob_find_resource_by_guid(hoblist, &hob_fsp_reserved_guid);
}

/**
 * @brief Iterates through the memory map in the HOB list and calls a callback
 * function for each resource descriptor.
 *
 * This function iterates through the memory map in the HOB list and calls the
 * specified callback function for each EFI_HOB_RESOURCE_DESCRIPTOR found
 * in the list.
 *
 * @param[in] hobList Pointer to the beginning of the HOB list.
 * @param[in] cb Callback function to be called for each resource descriptor.
 * @param[in] ctx User-defined context to be passed to the callback function.
 * @return 0 on success, or an error code if an error occurs during iteration.
 */
int hob_iterate_memory_map(struct efi_hob *hobList, hob_mem_map_cb cb,
                           void *ctx)
{
    int r;

    while (hob_get_type(hobList) != EFI_HOB_TYPE_END_OF_HOB_LIST) {
        if (hob_get_type(hobList) == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
            r = cb(hobList->u.resource_descriptor.physical_start,
                   hobList->u.resource_descriptor.resource_length,
                   hobList->u.resource_descriptor.resource_type,
                   ctx);
            if (r != 0)
                return r;
        }
        hobList = hob_get_next(hobList);
    }

    return 0;
}
#endif /* HOB_C */
