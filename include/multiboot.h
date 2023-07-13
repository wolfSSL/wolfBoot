/* 
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

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

uint8_t *mb2_find_header(uint8_t *image, int size);
int mb2_build_boot_info_header(uint8_t *mb2_boot_info,
                               uint8_t *mb2_header,
                               void *stage2_params,
                               unsigned max_size);
void mb2_jump(uintptr_t entry, uint32_t mb2_boot_info);
#endif /* MULTIBOOT_H */
