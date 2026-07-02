/* unit-image-elf-scatter.c
 *
 * Regression/mutation-pinning test for wolfBoot_check_flash_image_elf()
 * (src/image.c), the WOLFBOOT_ELF_FLASH_SCATTER integrity check that
 * update_flash.c relies on (and wolfBoot_panic()s on failure of) before
 * booting/staging a "scattered" ELF image whose PT_LOAD segments already
 * live at their final flash addresses (paddr + BASE_OFF), separate from the
 * manifest partition that holds the ELF header/program header table.
 *
 * The function re-hashes the scattered image and compares the digest
 * against the HDR_HASH TLV stored in the manifest header via
 * image_CT_compare(). A mutation of that "!= 0" to "== 0", or a dropped
 * "return -2", would silently accept a corrupted scattered image. These
 * tests pin: (1) a correctly-hashed scattered image verifies OK, and (2) a
 * single corrupted byte in a scattered segment's flash-resident payload
 * (not the manifest/ELF headers) is rejected via the digest-mismatch branch.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>

#include "user_settings.h"
#include "wolfboot/wolfboot.h"
#include "elf.h"

/* Pull in elf.c and image.c directly (in-tree compilation, mirroring
 * unit-image.c / unit-elf-bss-guard.c). This gives the test direct access
 * to the *static* hashing helpers used internally by
 * wolfBoot_check_flash_image_elf() (header_hash, update_hash_flash_fwimg,
 * update_hash_flash_addr, final_hash), so the expected digest can be
 * computed independently by replaying the same primitives on the same
 * bytes, rather than tautologically trusting the function's own output.
 *
 * libwolfboot.c is intentionally *not* pulled in here (it would clash with
 * static helpers of the same name already defined in image.c, e.g. im2n());
 * the handful of libwolfboot.c symbols image.c still needs at link time
 * (wolfBoot_find_header/wolfBoot_get_blob_version/wolfBoot_get_blob_type)
 * are provided below, with wolfBoot_find_header reusing the real TLV-parsing
 * logic from libwolfboot.c verbatim, since HDR_HASH lookup correctness is
 * central to this test. */
#include "elf.c"
#include "image.c"

#include "unit-mock-flash.c"

#define MOCK_ADDRESS_BOOT 0xCD000000

uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    (void)blob;
    return 1;
}

uint16_t wolfBoot_get_blob_type(uint8_t *blob)
{
    (void)blob;
    return HDR_IMG_TYPE_APP;
}

/* Verbatim copy of wolfBoot_find_header() from src/libwolfboot.c (not
 * pulled in as a whole to avoid the im2n() redefinition clash noted above).
 */
uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    uint8_t *p;
    uint16_t len, htype;
    uintptr_t p_addr, max_addr;

    *ptr = NULL;

    if (haystack == NULL) {
        return 0;
    }

    p_addr = (uintptr_t)haystack;
    if (p_addr < IMAGE_HEADER_OFFSET) {
        return 0;
    }

    max_addr = p_addr - IMAGE_HEADER_OFFSET;
    if (max_addr > (UINTPTR_MAX - IMAGE_HEADER_SIZE)) {
        return 0;
    }
    max_addr += IMAGE_HEADER_SIZE;

    if (p_addr > max_addr) {
        return 0;
    }

    while (p_addr < max_addr) {
        if ((max_addr - p_addr) < 4U) {
            break;
        }
        p = (uint8_t *)p_addr;
        htype = (uint16_t)(p[0] | (p[1] << 8));
        if (htype == 0) {
            break;
        }
        if ((p[0] == HDR_PADDING) || ((p_addr & 0x01U) != 0U)) {
            p_addr++;
            continue;
        }

        len = (uint16_t)(p[2] | (p[3] << 8));
        if ((4U + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            break;
        }
        if ((max_addr - p_addr) < (uintptr_t)(4U + len)) {
            break;
        }

        if (htype == type) {
            *ptr = (uint8_t *)(p_addr + 4U);
            return len;
        }
        p_addr += (uintptr_t)(4U + len);
    }
    return 0;
}

/* --- Scattered ELF layout (ELF64, 1 PT_LOAD segment) ---
 *
 * The manifest partition (PART_BOOT) holds: image header (IMAGE_HEADER_SIZE
 * bytes, with the HDR_HASH TLV) immediately followed by the ELF64 header and
 * its single program header (tightly packed, no gaps). The PT_LOAD segment
 * itself is *not* stored in the manifest: its bytes live at a separate flash
 * address (ph.paddr), exactly as wolfBoot_check_flash_image_elf expects for
 * a scattered image (BASE_OFF is 0 here since ARCH_SIM is not defined).
 *
 * The program header's offset/file_size are chosen so that:
 *   - ph.offset == ELF_HDR_SZ            (no padding before the segment)
 *   - ph.offset + ph.file_size == fw_size (no trailing bytes to hash)
 * so the only bytes fed to the hash are: the manifest header (up to
 * HDR_HASH), the ELF header + program header table, and the segment
 * payload at its scattered flash address.
 */
#define SEG_SIZE    64U
#define PH_COUNT    1U
#define ELF_HDR_SZ  (sizeof(elf64_header) + PH_COUNT * sizeof(elf64_program_header))
#define IMG_FW_SIZE (ELF_HDR_SZ + SEG_SIZE)

/* The scattered PT_LOAD segment's flash-resident payload. Its address is
 * used directly as the program header's paddr (BASE_OFF == 0), standing in
 * for "flash at address paddr" the way a real target would. */
static uint8_t segment_flash[SEG_SIZE];

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

/* Builds the manifest header + ELF header/PHT at MOCK_ADDRESS_BOOT, and the
 * scattered segment payload in segment_flash[], with the HDR_HASH TLV left
 * zeroed (caller must patch it in via patch_expected_digest()). */
static void build_scattered_image(void)
{
    uint8_t *manifest = (uint8_t *)(uintptr_t)MOCK_ADDRESS_BOOT;
    uint32_t magic = WOLFBOOT_MAGIC;
    uint32_t fw_size = IMG_FW_SIZE;
    elf64_header *eh;
    elf64_program_header *ph;
    unsigned int i;

    memset(manifest, 0, IMAGE_HEADER_SIZE + ELF_HDR_SZ);

    /* --- manifest header --- */
    memcpy(manifest + 0, &magic, sizeof(magic));
    memcpy(manifest + 4, &fw_size, sizeof(fw_size));
    /* HDR_HASH TLV at offset 8: type(2) + len(2) + digest(WOLFBOOT_SHA_DIGEST_SIZE) */
    write_le16(manifest + 8, HDR_HASH);
    write_le16(manifest + 10, WOLFBOOT_SHA_DIGEST_SIZE);
    /* digest bytes at manifest+12 are left zeroed; patched in later */

    /* --- ELF64 header, immediately following the manifest header --- */
    eh = (elf64_header *)(manifest + IMAGE_HEADER_SIZE);
    memcpy(eh->ident, ELF_IDENT_STR, 4);
    eh->ident[ELF_CLASS_OFF] = ELF_CLASS_64;
    eh->ident[5]             = ELF_ENDIAN_LITTLE;
    eh->type                 = ELF_HET_EXEC;
    eh->machine              = 0;
    eh->version              = 1;
    eh->entry                = 0x2000;
    eh->ph_offset            = sizeof(elf64_header);
    eh->sh_offset            = 0;
    eh->flags                = 0;
    eh->header_size          = sizeof(elf64_header);
    eh->ph_entry_size        = sizeof(elf64_program_header);
    eh->ph_entry_count       = PH_COUNT;
    eh->sh_entry_size        = 0;
    eh->sh_entry_count       = 0;
    eh->sh_str_index         = 0;

    /* --- single PT_LOAD program header, immediately after the ELF header --- */
    ph = (elf64_program_header *)((uint8_t *)eh + sizeof(elf64_header));
    ph->type      = ELF_PT_LOAD;
    ph->flags     = 5; /* R+X, not inspected by the function under test */
    ph->offset    = ELF_HDR_SZ;
    ph->vaddr     = 0; /* not read by wolfBoot_check_flash_image_elf */
    ph->paddr     = (uint64_t)(uintptr_t)segment_flash;
    ph->file_size = SEG_SIZE;
    ph->mem_size  = SEG_SIZE;
    ph->align     = 1;

    /* --- scattered segment payload, deterministic non-trivial pattern --- */
    for (i = 0; i < SEG_SIZE; i++) {
        segment_flash[i] = (uint8_t)(0xA0U + i);
    }
}

/* Independently recomputes the expected HDR_HASH digest by replaying the
 * exact same hashing primitives wolfBoot_check_flash_image_elf() uses
 * internally (header_hash / update_hash_flash_fwimg / update_hash_flash_addr
 * / final_hash), rather than deriving the "expected" value from the
 * function under test itself. */
static void compute_expected_digest(uint8_t *out)
{
    struct wolfBoot_image boot;
    wolfBoot_hash_t ctx;

    ck_assert_int_eq(wolfBoot_open_image(&boot, PART_BOOT), 0);
    ck_assert_int_eq(header_hash(&ctx, &boot), 0);
    ck_assert_int_eq(update_hash_flash_fwimg(&ctx, &boot, 0, (uint32_t)ELF_HDR_SZ), 0);
    ck_assert_int_eq(
        update_hash_flash_addr(&ctx, (uintptr_t)segment_flash, SEG_SIZE,
                               PART_IS_EXT(&boot)),
        0);
    ck_assert_int_eq(final_hash(&ctx, out), 0);
}

static void patch_expected_digest(const uint8_t *digest)
{
    uint8_t *manifest = (uint8_t *)(uintptr_t)MOCK_ADDRESS_BOOT;
    memcpy(manifest + 12, digest, WOLFBOOT_SHA_DIGEST_SIZE);
}

static void map_boot_partition(void)
{
    int ret = mmap_file("/tmp/wolfboot-unit-elf-scatter-boot.bin",
                        (void *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE,
                        NULL);
    ck_assert_int_ge(ret, 0);
}

static void unmap_boot_partition(void)
{
    munmap((void *)MOCK_ADDRESS_BOOT, WOLFBOOT_PARTITION_SIZE);
}

START_TEST(test_elf_scatter_valid_image_verifies_ok)
{
    uint8_t expected_digest[WOLFBOOT_SHA_DIGEST_SIZE];
    unsigned long entry = 0;
    int ret;

    map_boot_partition();

    build_scattered_image();
    compute_expected_digest(expected_digest);
    patch_expected_digest(expected_digest);

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq((uint32_t)entry, 0x2000U);

    unmap_boot_partition();
}
END_TEST

START_TEST(test_elf_scatter_corrupted_segment_rejected)
{
    uint8_t expected_digest[WOLFBOOT_SHA_DIGEST_SIZE];
    unsigned long entry = 0;
    int ret;

    map_boot_partition();

    build_scattered_image();
    compute_expected_digest(expected_digest);
    patch_expected_digest(expected_digest);

    /* Sanity check: with the digest matching, verification must succeed
     * first, so the corruption below is the only thing that changes. */
    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);
    ck_assert_int_eq(ret, 0);

    /* Corrupt a single byte of the scattered segment's *flash-resident*
     * payload (at its paddr location), strictly after the expected digest
     * was computed and stored. The ELF/program headers are untouched, so
     * every structural check (magic, elf_open, scatter-format, ph parsing,
     * bounds) still passes -- only the final digest comparison should
     * fail. */
    segment_flash[SEG_SIZE / 2] ^= 0xFFU;

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    /* -2 is the dedicated "digest mismatch" return value from
     * image_CT_compare() failing in wolfBoot_check_flash_image_elf(); any
     * other value (0, or -1 from an earlier structural check) means either
     * the corruption was not detected, or it was detected for the wrong
     * reason. */
    ck_assert_int_eq(ret, -2);

    unmap_boot_partition();
}
END_TEST

Suite *elf_scatter_suite(void)
{
    Suite *s  = suite_create("ELF flash-scatter image check");
    TCase *tc = tcase_create("wolfBoot_check_flash_image_elf");
    tcase_add_test(tc, test_elf_scatter_valid_image_verifies_ok);
    tcase_add_test(tc, test_elf_scatter_corrupted_segment_rejected);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int     fails;
    Suite  *s  = elf_scatter_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
