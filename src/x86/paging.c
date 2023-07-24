/* paging.c
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
#include <stdint.h>
#include <printf.h>
#include <x86/common.h>
#include <string.h>

#define PAGE_TABLE_PAGE_SIZE (0x1000)
#ifndef PAGE_TABLE_PAGE_NUM
#define PAGE_TABLE_PAGE_NUM 100
#endif /* PAGE_TABLE_PAGE_NUM */

#define PAGE_TABLE_SIZE (PAGE_TABLE_PAGE_SIZE * PAGE_TABLE_PAGE_NUM)
#define PAGE_MASK ~((1 << 12) - 1)
#define PAGE_SHIFT 9

#define PAGE_ENTRY_PRESENT (1 << 0)
#define PAGE_ENTRY_RW (1 << 1)
#define PAGE_ENTRY_US (1 << 2)
#define PAGE_ENTRY_PS (1 << 7)
#define PAGE_ENTRIES_PER_PAGE (512)

#define PAGE_2MB_SHIFT 21
#define MEM_SIZE 256 * 1024 * 1024

#if !defined(WOLFBOOT_LOADER)
#define WOLFBOOT_PTP_NUM 128
static uint8_t page_table_pages[WOLFBOOT_PTP_NUM * PAGE_TABLE_PAGE_SIZE]
__attribute__((aligned(PAGE_TABLE_PAGE_SIZE)));
static int page_table_page_used;
/* TODO: reserve space in the linker? check amount of memory space? */
uint8_t _mem[MEM_SIZE] __attribute__((aligned(PAGE_TABLE_PAGE_SIZE)));
uint8_t *mem = &_mem[0];
#endif /* !BUILD_LOADER_STAGE1 */

static inline void x86_paging_clear_pte(uint64_t *pte)
{
    *pte = 0;
}

static inline void x86_paging_pte_set_pfn(uint64_t *pte, uint64_t pfn)
{
    *pte |= (pfn & PAGE_MASK);
}

static inline void x86_paging_pte_set_present(uint64_t *pte)
{
    *pte |= PAGE_ENTRY_PRESENT;
}

static inline void x86_paging_pte_set_ps(uint64_t *pte)
{
    *pte |= PAGE_ENTRY_PS;
}

static inline void x86_paging_pte_set_rw(uint64_t *pte)
{
    *pte |= PAGE_ENTRY_RW;
}

static inline uint64_t x86_paging_pte_get_pfn(uint64_t *pte)
{
    return *pte & PAGE_MASK;
}

static uint32_t x86_paging_get_needed_entries(uint64_t size, int level)
{
    return ((size >> (PAGE_SHIFT * level + 3)) & ~PAGE_MASK) + 1;
}

static void x86_paging_setup_entry(uint64_t *e, uintptr_t addr)
{
    x86_paging_pte_set_pfn(e, (uint64_t)(addr));
    x86_paging_pte_set_present(e);
    x86_paging_pte_set_rw(e);
}

int x86_paging_get_page_table_size()
{
    return PAGE_TABLE_SIZE;
}

int x86_paging_build_identity_mapping(uint64_t top_address, uint8_t *page_table)
{
    uint32_t entries_l4, entries_l3, entries_l2;
    uint64_t *ptpl4, *ptpl3, *ptpl2, *e, *p;
    uint32_t pages_l3, pages_l2;
    unsigned int i, y;

    entries_l4 = x86_paging_get_needed_entries(top_address, 4);
    entries_l3 = x86_paging_get_needed_entries(top_address, 3);
    entries_l2 = x86_paging_get_needed_entries(top_address, 2);

    pages_l3 = entries_l4;
    pages_l2 = entries_l3;

    /* one page is used for l4 */
    if (pages_l2 + pages_l3 + 1 > PAGE_TABLE_PAGE_NUM)
        return -1;

    ptpl4 = (uint64_t*)page_table;
    ptpl3 = ptpl4 + (1 * PAGE_ENTRIES_PER_PAGE);
    ptpl2 = ptpl3 + (pages_l3 * PAGE_ENTRIES_PER_PAGE);

    for (i = 0; i < entries_l4; ++i) {
        e = ptpl4 + i;
        p = ptpl3 + i * PAGE_ENTRIES_PER_PAGE;
        x86_paging_setup_entry(e, (uintptr_t)p);
    }
    for (i = 0; i < entries_l3; ++i) {
        e = ptpl3 + i;
        p = ptpl2 + i * PAGE_ENTRIES_PER_PAGE;
        x86_paging_setup_entry(e, (uintptr_t)p);
    }
    for (i = 0; i < entries_l2; ++i) {
        e = ptpl2 + i;
        x86_paging_setup_entry(e, (uintptr_t)(i << PAGE_2MB_SHIFT));
        x86_paging_pte_set_ps(e);
    }

    return 0;
}

#if !defined(BUILD_LOADER_STAGE1)
static uint8_t* x86_paging_get_paget_table_root()
{
    uintptr_t cr3;
    __asm__ ("mov %%cr3, %0\r\n" : "=a"(cr3));
    return (uint8_t*)cr3;
}

static uint64_t *x86_paging_get_entry_ptr(uint64_t address,
                                         uint8_t *ptp, int level)
{
    uint64_t* entry_ptr = (uint64_t*)ptp;
    uint64_t index = (address >> (12 + (level - 1) * 9)) & 0x1FF;
    return &entry_ptr[index];
}

static void x86_paging_setup_ptp(uint64_t* e)
{
    uint8_t *ptp;

    ptp = &page_table_pages[page_table_page_used * PAGE_TABLE_PAGE_SIZE];
    page_table_page_used++;
    if (page_table_page_used == WOLFBOOT_PTP_NUM) {
        wolfBoot_printf("No more page table page structure\r\n");
        panic();
    }
    x86_paging_setup_entry(e, (uintptr_t)ptp);
    memset(ptp, 0, PAGE_TABLE_PAGE_SIZE);
}

static void x86_paging_map_page(uint64_t vaddress, uint64_t paddress)
{
    uint64_t *pl4e, *pl3e, *pl2e, *pl1e;
    uint8_t *pl4, *pl3, *pl2, *pl1;

    pl4 = x86_paging_get_paget_table_root();
    pl4e = x86_paging_get_entry_ptr(vaddress, pl4, 4);
    if (*pl4e == 0)
        x86_paging_setup_ptp(pl4e);
    pl3 = (uint8_t*)x86_paging_pte_get_pfn(pl4e);
    pl3e = x86_paging_get_entry_ptr(vaddress, pl3, 3);
    if (*pl3e == 0)
        x86_paging_setup_ptp(pl3e);
    pl2 = (uint8_t*)x86_paging_pte_get_pfn(pl3e);
    pl2e = x86_paging_get_entry_ptr(vaddress, pl2, 2);
    if (*pl2e == 0)
        x86_paging_setup_ptp(pl2e);
    /* already mapped by a 2mb page */
    if (*pl2e & PAGE_ENTRY_PS)
        return;
    pl1 = (uint8_t*)x86_paging_pte_get_pfn(pl2e);
    pl1e = x86_paging_get_entry_ptr(vaddress, pl1, 1);
    /* already mapped */
    if (*pl1e != 0)
        return;
    if (paddress == 0) {
        paddress = (uint64_t)mem;
        mem += PAGE_TABLE_PAGE_SIZE;
    }
    x86_paging_setup_entry(pl1e, paddress);
}

int x86_paging_map_memory(uint64_t va, uint64_t pa, uint32_t size)
{
    uint64_t start, end, page;

    end = va + size;
    start = va & PAGE_MASK;
    pa = pa & PAGE_MASK;

    for (page = start; page < end; page += PAGE_TABLE_PAGE_SIZE,
        pa += PAGE_TABLE_PAGE_SIZE)
         x86_paging_map_page(page, pa);

    return 0;
}
#endif /* !BUILD_LOADER_STAGE1 */
