/* unit-boot-x86-fsp-payload.c
 *
 * Unit tests for the x86 FSP payload hand-off (src/boot_x86_fsp_payload.c):
 * the ELF segment check against wolfBoot's own memory, and the per-image
 * choice between the Multiboot2 ELF and Linux paths, with the signed or
 * built-in kernel command line.
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

#include <check.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "../../src/boot_x86_fsp_payload.c"

static struct stage2_parameter g_params;
static jmp_buf g_panic_jmp;
static int g_panics;
static int g_elf_loads;
static int g_mb2_jumps;
static int g_linux_loads;
static const char *g_linux_cmdline;
static uint8_t g_mb2_header[16];

void panic(void)
{
    g_panics++;
    longjmp(g_panic_jmp, 1);
}

struct stage2_parameter *stage2_get_parameters(void)
{
    return &g_params;
}

void mptable_setup(void)
{
}

int x86_paging_dump_info(void)
{
    return 0;
}

int x86_paging_map_memory(uint64_t va, uint64_t pa, uint32_t size)
{
    (void)va;
    (void)pa;
    (void)size;
    return 0;
}

int elf_load_image_mmu(uint8_t *image, uint32_t image_sz, uintptr_t *pentry,
    elf_mmu_map_cb mmu_cb)
{
    (void)image;
    (void)image_sz;
    (void)mmu_cb;
    g_elf_loads++;
    *pentry = 0x1000;
    return 0;
}

uint8_t *mb2_find_header(uint8_t *image, int size)
{
    (void)image;
    (void)size;
    return g_mb2_header;
}

int mb2_build_boot_info_header(uint8_t *boot_info, uint8_t *mb2_header,
    void *params, unsigned max_size)
{
    (void)boot_info;
    (void)mb2_header;
    (void)params;
    (void)max_size;
    return 0;
}

void mb2_jump(uintptr_t entry, uint32_t boot_info)
{
    (void)entry;
    (void)boot_info;
    g_mb2_jumps++;
}

void load_linux(uint8_t *linux_image, uint32_t image_size, void *params,
    const char *cmd_line)
{
    (void)linux_image;
    (void)image_size;
    (void)params;
    g_linux_loads++;
    g_linux_cmdline = cmd_line;
}

static void reset_mocks(void)
{
    memset(&g_params, 0, sizeof(g_params));
    g_params.payload_size = 4096;
    g_panics = 0;
    g_elf_loads = 0;
    g_mb2_jumps = 0;
    g_linux_loads = 0;
    g_linux_cmdline = NULL;
}

START_TEST(test_elf_segments_outside_wolfboot_memory)
{
    /* below the reserved window, ending exactly at its start, and above
     * 4 GB are all accepted; an empty segment is never checked */
    ck_assert_int_eq(boot_elf_map_cb(0, 0x200000, 0x1000), 0);
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_START - 0x1000, 0x1000),
                     0);
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_END, 0x1000), 0);
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_START, 0), 0);
}
END_TEST

START_TEST(test_elf_segments_over_wolfboot_memory)
{
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_START - 0x1000, 0x1001),
                     -1);
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_START, 1), -1);
    ck_assert_int_eq(boot_elf_map_cb(0, ELF_RESERVED_END - 1, 0x1000), -1);
    ck_assert_int_eq(boot_elf_map_cb(0, 0x100000, 0xfff00000U), -1);
    /* a safe physical address with a virtual one over stage 2 */
    ck_assert_int_eq(boot_elf_map_cb(WOLFBOOT_LOAD_BASE, 0x200000, 0x1000),
                     -1);
}
END_TEST

START_TEST(test_elf_image_takes_the_multiboot2_path)
{
    uint32_t image[4] = { 0 };

    reset_mocks();
    memcpy(image, ELF_IDENT_STR, 4);
    if (setjmp(g_panic_jmp) == 0)
        do_boot(image);
    ck_assert_int_eq(g_panics, 0);
    ck_assert_int_eq(g_elf_loads, 1);
    ck_assert_int_eq(g_mb2_jumps, 1);
    ck_assert_int_eq(g_linux_loads, 0);
}
END_TEST

START_TEST(test_other_image_boots_linux)
{
    uint32_t image[4] = { 0 };

    reset_mocks();
    memcpy(image, "MZ\0\0", 4);
    do_boot(image);
    ck_assert_int_eq(g_elf_loads, 0);
    ck_assert_int_eq(g_linux_loads, 1);
    ck_assert_ptr_eq(g_linux_cmdline, cmdline);

    reset_mocks();
    /* stage2_params carries a 32-bit address; only compared, not read */
    g_params.cmdline = 0x5a000000U;
    do_boot(image);
    ck_assert_int_eq(g_linux_loads, 1);
    ck_assert_ptr_eq(g_linux_cmdline, (const char *)(uintptr_t)0x5a000000U);
}
END_TEST

static Suite *payload_suite(void)
{
    Suite *s = suite_create("boot-x86-fsp-payload");
    TCase *tc = tcase_create("boot-x86-fsp-payload");

    tcase_add_test(tc, test_elf_segments_outside_wolfboot_memory);
    tcase_add_test(tc, test_elf_segments_over_wolfboot_memory);
    tcase_add_test(tc, test_elf_image_takes_the_multiboot2_path);
    tcase_add_test(tc, test_other_image_boots_linux);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = payload_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
