/* paging.h
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifdef WOLFBOOT_64BIT
#include <stdint.h>
/* build 1:1 virtual mapping in the range from 0 to top_address, return the root
 * of the memory mapping in the **cr3 register */
int x86_paging_build_identity_mapping(uint64_t top_address,
                                      uint8_t *page_table);
/* if not already mapped, map virtual address range (address, address + size)
 * using new physical memory */
int x86_paging_map_range(uint64_t address, uint32_t size);
int x86_paging_get_page_table_size();
int x86_paging_set_page_table();

#if !defined(BUILD_LOADER_STAGE1)
int x86_paging_map_memory(uint64_t va, uint64_t pa, uint32_t size);
int x86_paging_dump_info();
#endif /* !BUILD_LOADER_STAGE1 */

#endif /* WOLFBOOT_64BIT */
