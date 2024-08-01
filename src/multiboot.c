/* multiboot.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifdef WOLFBOOT_MULTIBOOT2

#include <stdint.h>
#include <stdio.h>
#include <printf.h>
#include <sys/types.h>

#include <x86/hob.h>
#include <multiboot.h>

#ifdef WOLFBOOT_FSP
#include <stage2_params.h>
#endif

#define MB2_HEADER_MAX_OFF (32768 - 4)
#define MB2_HEADER_MAGIC 0xe85250d6
#define MB2_TAG_TYPE_INFO_REQ 1
#define MB2_REQ_TAG_BASIC_MEM_INFO 4
#define MB2_REQ_TAG_MEM_MAP 6
#define MB2_MEM_INFO_MEM_RAM 1
#define MB2_MEM_INFO_MEM_RESERVED 2

#ifdef DEBUG_MB2
#define MB2_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
#define MB2_DEBUG_PRINTF(...) do {} while(0)
#endif /* DEBUG_MB2 */


struct mb2_tag_info_req {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t mbi_tag_types[];
};

struct mb2_tag {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

struct mb2_header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
};

struct mb2_mem_map_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct mb2_mem_map_header {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct mb2_basic_memory_info {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct mb2_boot_info_header {
    uint32_t total_size;
    uint32_t reserved;
};

static uint8_t* mb2_align_address_up(uint8_t *addr, int align)
{
    uintptr_t v = (uintptr_t)addr;
    uintptr_t mask = align - 1;

    return (uint8_t*)((v + mask) & ~(mask));
}

static uint8_t *mb2_find_tag_by_type(uint8_t *tags, uint32_t type)
{
    struct mb2_tag* tag = (struct mb2_tag*)tags;

    while (tag->type != 0) {
        if (tag->type == type)
            return (uint8_t*)tag;
        tag = (struct mb2_tag*)mb2_align_address_up((uint8_t*)tag + tag->size,
                                                    8);
    }
    return NULL;
}

static int mb2_start_mem_map(uint8_t *tag)
{
    struct mb2_mem_map_header *map;

    map = (struct mb2_mem_map_header *)tag;
    map->type = 6;
    map->entry_size = sizeof(struct mb2_mem_map_entry);
    map->entry_version = 0;
    map->size = sizeof(struct mb2_mem_map_header);
    return 0;
}

#ifdef WOLFBOOT_FSP

struct mb2_mem_map_cb_info {
    struct mb2_mem_map_header *map_hdr;
    unsigned *max_size;
};

static int mb2_add_mem_entry_cb(uint64_t baseAddr, uint64_t length,
                                uint32_t type, void *ctx) {
    struct mb2_mem_map_header *mem_map;
    struct mb2_mem_map_cb_info *info;
    struct mb2_mem_map_entry *e;

    info = (struct mb2_mem_map_cb_info *)ctx;
    if (*info->max_size < sizeof(struct mb2_mem_map_entry)) {
        MB2_DEBUG_PRINTF("Not enough size to build mb2 mep map\r\n");
        return -1;
    }
    *info->max_size = *info->max_size - sizeof(struct mb2_mem_map_entry);
    mem_map = info->map_hdr;
    e = (struct mb2_mem_map_entry*)((uint8_t*)mem_map + mem_map->size);
    e->base_addr = baseAddr;
    e->length = length;
    e->type =
        (type == EFI_RESOURCE_SYSTEM_MEMORY) ? MB2_MEM_INFO_MEM_RAM : MB2_MEM_INFO_MEM_RESERVED;
    e->reserved = 0;
    mem_map->size += sizeof(struct mb2_mem_map_entry);

    return 0;
}

static int mb2_add_mem_basic_info_cb(uint64_t baseAddr, uint64_t length,
                              uint32_t type, void *ctx)
{
    struct mb2_basic_memory_info *mem_info =
        (struct mb2_basic_memory_info *)ctx;

    (void)type;
    if (baseAddr == 0)
        mem_info->mem_lower = length / 1024;
    if (baseAddr == 1 * 1024 * 1024)
        mem_info->mem_upper = length / 1024;
    return 0;
}
#endif /* WOLFBOOT_FSP */

static int mb2_add_basic_mem_info(uint8_t **idx, void *stage2_param,
                                  unsigned *max_size)
{
#ifdef WOLFBOOT_FSP
    struct mb2_basic_memory_info *meminfo = (struct mb2_basic_memory_info*)(*idx);
    struct stage2_parameter *p = (struct stage2_parameter*)stage2_param;
    struct efi_hob *hobs = (struct efi_hob*)(uintptr_t)p->hobList;
    int r;

    if (*max_size < sizeof(struct mb2_basic_memory_info)) {
        MB2_DEBUG_PRINTF("Not enough size to build mb2 basic info\r\n");
        return -1;
    }
    *max_size -= sizeof(struct mb2_basic_memory_info);

    meminfo->type = MB2_REQ_TAG_BASIC_MEM_INFO;
    meminfo->size = sizeof(*meminfo);
    meminfo->mem_lower = 0;
    meminfo->mem_upper = 0;
    r = hob_iterate_memory_map(hobs,
                               mb2_add_mem_basic_info_cb, (void*)meminfo);
    if (r != 0)
        return r;
    *idx += sizeof(*meminfo);
    return 0;
#else
    (void)idx;
    (void)stage2_param;
    (void)max_size;
    return -1;
#endif /* WOLFBOOT_FSP */
}

static int mb2_add_mem_map(uint8_t **idx, void *stage2_param, unsigned *max_size)
{
#ifdef WOLFBOOT_FSP
    struct mb2_mem_map_header *map_hdr = (struct mb2_mem_map_header*)(*idx);
    struct stage2_parameter *p = (struct stage2_parameter*)stage2_param;
    struct efi_hob *hobs = (struct efi_hob*)(uintptr_t)p->hobList;
    struct mb2_mem_map_cb_info info;
    int r;

    if (*max_size < sizeof(struct mb2_mem_map_header)) {
        MB2_DEBUG_PRINTF("Not enough size to build mb2 map header\r\n");
        return -1;
    }
    info.map_hdr = map_hdr;
    info.max_size = max_size;
    *info.max_size = *info.max_size - sizeof(struct mb2_mem_map_header);
    r = mb2_start_mem_map(*idx);
    if (r != 0)
        return r;
    r = hob_iterate_memory_map(hobs, mb2_add_mem_entry_cb, (void *)&info);
    if (r != 0)
        return r;
    *idx += map_hdr->size;
    return 0;
#else
    (void)idx;
    (void)stage2_param;
    (void)max_size;
    return -1;
#endif /* WOLFBOOT_FSP */
}

int mb2_build_boot_info_header(uint8_t *mb2_boot_info,
                               uint8_t *mb2_header,
                               void* stage2_params,
                               unsigned max_size)
{
    struct mb2_boot_info_header *hdr =
        (struct mb2_boot_info_header *)mb2_boot_info;
    struct mb2_tag_info_req *info_req_tag;
    int requested_tags, i, r;
    uint8_t *idx;

    if (max_size < sizeof(*hdr)) {
        MB2_DEBUG_PRINTF("Not enough size to build mb2 info header\r\n");
        return -1;
    }
    max_size -= sizeof(*hdr);
    idx = (uint8_t*)hdr + sizeof(*hdr);
    hdr->reserved = 0;
    info_req_tag =
        (struct mb2_tag_info_req *)mb2_find_tag_by_type(mb2_header + sizeof(struct mb2_header), MB2_TAG_TYPE_INFO_REQ);
    if (info_req_tag == NULL)
        return -1;
    requested_tags = (info_req_tag->size - sizeof(struct mb2_tag_info_req)) / sizeof(uint32_t);
    for (i = 0; i < requested_tags; i++) {
        switch (info_req_tag->mbi_tag_types[i]) {
        case MB2_REQ_TAG_BASIC_MEM_INFO:
            r = mb2_add_basic_mem_info(&idx, stage2_params, &max_size);
            if (r != 0)
                return r;
            break;
        case MB2_REQ_TAG_MEM_MAP:
            r = mb2_add_mem_map(&idx, stage2_params, &max_size);
            if (r != 0)
                return r;
            break;
        default:
            wolfBoot_printf("mb2: unsupported info request tag: %d\r\n", i);
            return -1;
        }
    }

    hdr->total_size = idx - (uint8_t*)hdr;

    return 0;
}

#ifdef DEBUG_MB2
static void mb2_parse_info_request_tag(void* tag) {
    struct mb2_tag_info_req *infoTag = (struct mb2_tag_info_req*)tag;

    uint32_t numTagTypes =
        (infoTag->size - sizeof(struct mb2_tag_info_req)) / sizeof(uint32_t);

    MB2_DEBUG_PRINTF("Information Request Tag:\r\n");
    MB2_DEBUG_PRINTF("Tag Type: %u\r\n", infoTag->type);
    MB2_DEBUG_PRINTF("Tag Flags: 0x%x\r\n", infoTag->flags);
    MB2_DEBUG_PRINTF("Tag Size: %u\r\n", infoTag->size);
    MB2_DEBUG_PRINTF("Number of Tag Types: %u\r\n", numTagTypes);

    for (uint32_t i = 0; i < numTagTypes; i++) {
        uint32_t tagType = infoTag->mbi_tag_types[i];
        MB2_DEBUG_PRINTF("Tag Type %u: %u\r\n", i, tagType);
    }
}

static void mb2_dump_tags(void* mbTags) {
    struct mb2_tag* tag = (struct  mb2_tag*)mbTags;

    while (tag->type != 0) {
        MB2_DEBUG_PRINTF("Tag Type: %u\r\n", tag->type);
        MB2_DEBUG_PRINTF("Tag Flags: 0x%x\r\n", tag->flags);
        MB2_DEBUG_PRINTF("Tag Size: %u\r\n", tag->size);

        if (tag->type == MB2_TAG_TYPE_INFO_REQ)
            mb2_parse_info_request_tag(tag);

        tag = (struct mb2_tag*)mb2_align_address_up((uint8_t*)tag + tag->size,
                                                    8);
    }
}

static void mb2_dump_header(void* mbHeader) {
    struct mb2_header* header = (struct mb2_header*)mbHeader;
    uint8_t *tags;

    MB2_DEBUG_PRINTF("Magic: 0x%x\r\n", header->magic);
    MB2_DEBUG_PRINTF("Architecture: 0x%x\r\n", header->architecture);
    MB2_DEBUG_PRINTF("Header Length: %u\r\n", header->header_length);
    MB2_DEBUG_PRINTF("Checksum: 0x%x\r\n", header->checksum);

    tags = (uint8_t*)header + sizeof(*header);
    mb2_dump_tags(tags);
}
#endif /* DEBUG_MB2 */

uint8_t *mb2_find_header(uint8_t *image, int size)
{
    uint32_t *ptr;
    int i;

    if (size > MB2_HEADER_MAX_OFF/4)
        size = MB2_HEADER_MAX_OFF;
    size = size / 4;
    for (ptr = (uint32_t*)image,i = 0; i < size; ++i) {
        if (ptr[i] == MB2_HEADER_MAGIC) {
#ifdef DEBUG_MB2
            mb2_dump_header(&ptr[i]);
#endif /* DEBUG_MB2 */
            return (uint8_t*)&ptr[i];
        }
    }
    return NULL;
}

void mb2_jump(uintptr_t entry, uint32_t mb2_boot_info)
{
    __asm__(
            "mov $0x36d76289, %%eax\r\n"
            "mov %0, %%ebx\r\n"
            "jmp *%1\r\n"
            :
            : "g"(mb2_boot_info), "g"(entry)
            : "eax", "ebx");
}

#endif /* WOLFBOOT_MULTIBOOT2 */
