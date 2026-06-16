/* unit-elf-bss-guard.c
 *
 * Regression test for F-4418: elf_load_image_mmu collision guard uses
 * file_size rather than mem_size, so a BSS-bearing PT_LOAD segment can
 * overwrite program headers that have not been parsed yet.
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

/* 4 KiB is plenty for the ELF64 header + 9 program headers (568 bytes). */
#define TEST_IMG_SIZE 4096

static uint8_t g_image[TEST_IMG_SIZE];

/*
 * Build a minimal ELF64 executable image in g_image with entry_count = 9
 * program headers (one more than ELF_MAX_PH = 8).  That forces
 * elf_load_image_mmu to leave the headers in the original image buffer
 * rather than caching them on the stack.
 *
 * ph[0]: PT_LOAD, file_size=0, mem_size=56 (= sizeof elf64_program_header).
 *        vaddr is set to the runtime address of ph[1] so that the BSS
 *        zero-fill (memset) writes exactly over ph[1] when the guard is
 *        evaluated with file_size instead of mem_size.
 *
 * ph[1]: sentinel — type field set to ELF_PT_LOAD (1).  With the bug the
 *        BSS memset zeroes ph[1].type to 0; with the fix the write is
 *        blocked and the sentinel is preserved.
 *
 * ph[2..8]: type=0, mem_size=0 — skipped by the loader.
 */
START_TEST(test_elf_bss_guard_no_header_corruption)
{
    elf64_header          *hdr;
    elf64_program_header  *ph;
    uintptr_t              entry = 0;
    int                    i, ret;

    memset(g_image, 0, sizeof(g_image));

    hdr = (elf64_header *)g_image;
    memcpy(hdr->ident, ELF_IDENT_STR, 4);
    hdr->ident[ELF_CLASS_OFF]   = ELF_CLASS_64;
    hdr->ident[5]               = ELF_ENDIAN_LITTLE;
    hdr->type                   = ELF_HET_EXEC;
    hdr->version                = 1;
    hdr->entry                  = 0x1000;
    hdr->ph_offset              = sizeof(elf64_header);          /* 64 */
    hdr->ph_entry_size          = sizeof(elf64_program_header);  /* 56 */
    hdr->ph_entry_count         = ELF_MAX_PH + 1;               /* 9 */

    ph = (elf64_program_header *)(g_image + sizeof(elf64_header));

    /*
     * ph[0]: BSS-only PT_LOAD segment.
     * vaddr points at ph[1] so that a memset of mem_size bytes zeroes it.
     * With the buggy guard (file_size check):
     *   vaddr + file_size  = ph[1]_start + 0  = ph[1]_start  <= ph[1]_start  → TRUE
     *   → write proceeds; memset zeroes ph[1].
     * With the fixed guard (mem_size check):
     *   vaddr + mem_size   = ph[1]_start + 56 > ph[1]_start                  → FALSE
     *   → load is aborted (-5) before any write; ph[1] intact.
     */
    ph[0].type      = ELF_PT_LOAD;
    ph[0].flags     = 0;
    ph[0].offset    = 0;
    ph[0].vaddr     = (uint64_t)(uintptr_t)(ph + 1); /* = &ph[1] */
    ph[0].paddr     = ph[0].vaddr;
    ph[0].file_size = 0;
    ph[0].mem_size  = sizeof(elf64_program_header); /* 56 bytes of BSS */
    ph[0].align     = 1;

    /* ph[1]: sentinel — preserve this type value to detect corruption. */
    ph[1].type     = ELF_PT_LOAD;
    ph[1].mem_size = 0; /* skipped by the "mem_size == 0" guard if reached */

    /* ph[2..8]: empty, will be skipped. */
    for (i = 2; i <= ELF_MAX_PH; i++) {
        ph[i].type     = 0;
        ph[i].mem_size = 0;
    }

    ret = elf_load_image_mmu(g_image, sizeof(g_image), &entry, NULL);

    /* The loader should reject this image instead of loading it, because the
     * segment would overwrite part of the image it has not read yet. */
    ck_assert_int_eq(ret, -5);

    /*
     * ph[1].type must not have been zeroed by the BSS memset from ph[0].
     * Failure here means the guard compared vaddr+file_size instead of
     * vaddr+mem_size, allowing the memset to corrupt the unread header.
     */
    ck_assert_msg(ph[1].type != 0,
        "BSS memset from ph[0] corrupted ph[1].type: "
        "collision guard must use mem_size, not file_size");
}
END_TEST

Suite *elf_bss_guard_suite(void)
{
    Suite *s  = suite_create("ELF BSS guard");
    TCase *tc = tcase_create("bss-header-collision");
    tcase_add_test(tc, test_elf_bss_guard_no_header_corruption);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int     fails;
    Suite  *s  = elf_bss_guard_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
