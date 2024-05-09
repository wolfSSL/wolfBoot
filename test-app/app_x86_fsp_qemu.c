/* app_x86_fsp_qemu.c
 *
 * Test bare-metal boot application
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


#ifdef PLATFORM_x86_fsp_qemu

#include <printf.h>
#include <stdint.h>

struct mb2_header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
} __attribute__((__packed__));

struct mb2_tag_info_req {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t mbi_tag_types[2];
} __attribute__((__packed__));

struct multiboot_header {
    struct mb2_header hdr;
    struct mb2_tag_info_req req;
} __attribute__((__packed__));

__attribute__((aligned(8))) struct multiboot_header mbh = {
    .hdr.magic = 0xe85250d6,
    .hdr.architecture = 0,
    .hdr.checksum = 0,
    .hdr.header_length = sizeof(struct mb2_header),
    .req.type = 1,
    .req.flags = 0,
    .req.size = sizeof(struct mb2_tag_info_req),
    /* basic mem info */
    .req.mbi_tag_types[0] = 4,
    /* mem map */
    .req.mbi_tag_types[1] = 6,
};

void start()
{
    wolfBoot_printf("wolfBoot QEMU x86 FSP test app\r\n");
    __asm__ ("hlt\r\n");
}
#endif
