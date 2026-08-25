/* unit-elf-mmu-fail.c
 *
 * Regression test for F-11027: elf_load_image_mmu() skipped a segment
 * when its mmu_cb mapping failed (continue) instead of aborting, so it
 * could publish the ELF entry point and return success for a partially
 * loaded image. The x86 FSP payload path (boot_x86_fsp_payload.c)
 * passes a real mmu_cb and panics only on a non-zero return, so the
 * buggy continue booted a payload with a missing segment.
 *
 * The loader must fail the whole load when a mapping fails, matching
 * the program-header clobber guard that aborts for the same reason:
 * never silently drop a PT_LOAD segment.
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

#include <stdint.h>
#include <string.h>
#include <check.h>

#include "elf.h"

/* Pull in elf.c directly (avoids a separate link step). */
#include "../../src/elf.c"

/* 4 KiB is plenty for the ELF64 header + 2 program headers + segment data. */
#define TEST_IMG_SIZE 4096

static uint8_t g_image[TEST_IMG_SIZE];
static uint8_t g_target[64];

/* Fail the mapping of the first segment only, on the first call. */
static int g_fail_first;

static int mmu_cb_fail_first(uint64_t vaddr, uint64_t paddr, uint32_t size)
{
    (void)vaddr;
    (void)paddr;
    (void)size;

    if (g_fail_first) {
        g_fail_first = 0;
        return -1;
    }
    return 0;
}

static int mmu_cb_ok(uint64_t vaddr, uint64_t paddr, uint32_t size)
{
    (void)vaddr;
    (void)paddr;
    (void)size;
    return 0;
}

/* Build a minimal ELF64 with two 8-byte PT_LOAD segments, with their
 * data at image offsets 256 and 264, mapped onto g_target. */
static void build_elf(void)
{
    elf64_header        *hdr;
    elf64_program_header *ph;

    memset(g_image, 0, sizeof(g_image));
    memset(g_target, 0, sizeof(g_target));

    hdr = (elf64_header *)g_image;
    memcpy(hdr->ident, ELF_IDENT_STR, 4);
    hdr->ident[ELF_CLASS_OFF]   = ELF_CLASS_64;
    hdr->ident[5]               = ELF_ENDIAN_LITTLE;
    hdr->type                   = ELF_HET_EXEC;
    hdr->version                = 1;
    hdr->entry                  = 0x1000;
    hdr->ph_offset              = sizeof(elf64_header);          /* 64 */
    hdr->ph_entry_size          = sizeof(elf64_program_header);  /* 56 */
    hdr->ph_entry_count         = 2;

    ph = (elf64_program_header *)(g_image + sizeof(elf64_header));

    ph[0].type      = ELF_PT_LOAD;
    ph[0].flags     = 0;
    ph[0].offset    = 256;
    ph[0].vaddr     = (uint64_t)(uintptr_t)g_target;
    ph[0].paddr     = ph[0].vaddr;
    ph[0].file_size = 8;
    ph[0].mem_size  = 8;
    ph[0].align     = 1;

    ph[1].type      = ELF_PT_LOAD;
    ph[1].flags     = 0;
    ph[1].offset    = 264;
    ph[1].vaddr     = (uint64_t)(uintptr_t)(g_target + 16);
    ph[1].paddr     = ph[1].vaddr;
    ph[1].file_size = 8;
    ph[1].mem_size  = 8;
    ph[1].align     = 1;

    /* Segment payloads: recognizable bytes. */
    memset(g_image + 256, 0xA5, 8);
    memset(g_image + 264, 0x5A, 8);
}

/* A failed mapping must fail the whole load: no entry point published,
 * no success returned. */
START_TEST(test_mmu_failure_aborts_load)
{
    uintptr_t entry = 0;
    int ret;

    build_elf();
    g_fail_first = 1;

    ret = elf_load_image_mmu(g_image, sizeof(g_image), &entry,
                             mmu_cb_fail_first);

    ck_assert_int_ne(ret, 0);
    ck_assert_msg(entry == 0,
        "entry point published despite a failed mmu mapping");
}
END_TEST

/* With successful mappings the load completes and the entry point is
 * published: the failure path must not over-reject. */
START_TEST(test_successful_mappings_still_load)
{
    uintptr_t entry = 0;
    int ret;

    build_elf();

    ret = elf_load_image_mmu(g_image, sizeof(g_image), &entry, mmu_cb_ok);

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(entry, 0x1000);
    ck_assert_mem_eq(g_target, g_image + 256, 8);
    ck_assert_mem_eq(g_target + 16, g_image + 264, 8);
}
END_TEST

Suite *elf_mmu_fail_suite(void)
{
    Suite *s  = suite_create("ELF mmu failure");
    TCase *tc = tcase_create("mmu-cb-failure");
    tcase_add_test(tc, test_mmu_failure_aborts_load);
    tcase_add_test(tc, test_successful_mappings_still_load);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int    fails;
    Suite *s  = elf_mmu_fail_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
