/* unit-elf-entry-inplace.c
 *
 * Regression test: elf_load_image_mmu read e_entry from the ELF header
 * AFTER copying the PT_LOAD segments. On an in-place load the first
 * segment's destination is the image buffer itself, so the copy overwrites
 * that header and the reported entry point is whatever segment bytes
 * happened to land on it.
 *
 * Observed on a ZynqMP SD boot: a correctly signed and verified ELF was
 * handed off at 0xEB02003F instead of its real entry, because the single
 * PT_LOAD segment had p_vaddr equal to the staging address.
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

#define TEST_IMG_SIZE   4096
#define SEG_OFFSET      0x400
#define SEG_SIZE        0x100
#define SEG_FILL        0xAA
#define TEST_ENTRY      0xDEADBEEFUL

static uint8_t g_image[TEST_IMG_SIZE];

/*
 * Build an ELF64 executable whose single PT_LOAD segment loads to the image
 * buffer itself -- the in-place / RAM-boot arrangement wolfBoot uses when
 * WOLFBOOT_LOAD_ADDRESS equals the segment's link address.
 *
 * The segment source bytes are all SEG_FILL, so once the copy has run the
 * header's e_entry field reads back as 0xAAAA... rather than TEST_ENTRY.
 * Reading e_entry before the copy is the only way to report it correctly.
 */
START_TEST(test_elf_entry_survives_inplace_load)
{
    elf64_header         *hdr;
    elf64_program_header *ph;
    uintptr_t             entry = 0;
    int                   ret;

    memset(g_image, 0, sizeof(g_image));
    memset(g_image + SEG_OFFSET, SEG_FILL, SEG_SIZE);

    hdr = (elf64_header *)g_image;
    memcpy(hdr->ident, ELF_IDENT_STR, 4);
    hdr->ident[ELF_CLASS_OFF] = ELF_CLASS_64;
    hdr->ident[5]             = ELF_ENDIAN_LITTLE;
    hdr->type                 = ELF_HET_EXEC;
    hdr->version              = 1;
    hdr->entry                = TEST_ENTRY;
    hdr->ph_offset            = sizeof(elf64_header);
    hdr->ph_entry_size        = sizeof(elf64_program_header);
    hdr->ph_entry_count       = 1;

    ph = (elf64_program_header *)(g_image + sizeof(elf64_header));
    ph[0].type      = ELF_PT_LOAD;
    ph[0].flags     = 0;
    ph[0].offset    = SEG_OFFSET;
    /* in-place: destination is the image buffer itself */
    ph[0].vaddr     = (uint64_t)(uintptr_t)g_image;
    ph[0].paddr     = ph[0].vaddr;
    ph[0].file_size = SEG_SIZE;
    ph[0].mem_size  = SEG_SIZE;
    ph[0].align     = 1;

    ret = elf_load_image_mmu(g_image, sizeof(g_image), &entry, NULL);
    ck_assert_int_eq(ret, 0);

    /* The segment really did land on the header -- otherwise this test is
     * not exercising the overlap it claims to. */
    ck_assert_uint_eq(g_image[0], SEG_FILL);

    /* ...and the entry point must still be the one the ELF declared. */
    ck_assert_msg(entry == (uintptr_t)TEST_ENTRY,
        "entry point read after in-place segment copy: got 0x%lx, want 0x%lx",
        (unsigned long)entry, (unsigned long)TEST_ENTRY);
}
END_TEST

Suite *elf_entry_inplace_suite(void)
{
    Suite *s  = suite_create("ELF in-place entry point");
    TCase *tc = tcase_create("entry-before-load");
    tcase_add_test(tc, test_elf_entry_survives_inplace_load);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int      fails;
    Suite   *s  = elf_entry_inplace_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
