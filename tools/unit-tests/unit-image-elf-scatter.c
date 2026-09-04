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

/* --- Multi-segment fixtures for the PT_LOAD bounds-rejection tests ---
 *
 * The manifest holds image header + ELF header + N program headers
 * (tightly packed). Each segment's flash-resident payload lives in its
 * own static array referenced by ph.paddr. */
#define SEG1_SIZE 0x2000U
#define SEG2_SIZE 64U

static uint8_t seg1_flash[SEG1_SIZE];
static uint8_t seg2_flash[SEG2_SIZE];

struct seg_spec {
    uint64_t offset;
    uint64_t filesz;
    uint64_t paddr;
    uint8_t *payload; /* pattern-filled for fillsz bytes */
    uint32_t fillsz;
};

static void build_scattered_image_n(const struct seg_spec *segs, unsigned n,
                                    uint32_t fw_size)
{
    uint8_t *manifest = (uint8_t *)(uintptr_t)MOCK_ADDRESS_BOOT;
    uint32_t magic = WOLFBOOT_MAGIC;
    size_t pht_sz = sizeof(elf64_header) + n * sizeof(elf64_program_header);
    elf64_header *eh;
    elf64_program_header *ph;
    unsigned i, j;

    memset(manifest, 0, IMAGE_HEADER_SIZE + pht_sz);

    /* manifest header with a zeroed HDR_HASH TLV (patched by caller) */
    memcpy(manifest + 0, &magic, sizeof(magic));
    memcpy(manifest + 4, &fw_size, sizeof(fw_size));
    write_le16(manifest + 8, HDR_HASH);
    write_le16(manifest + 10, WOLFBOOT_SHA_DIGEST_SIZE);

    eh = (elf64_header *)(manifest + IMAGE_HEADER_SIZE);
    memcpy(eh->ident, ELF_IDENT_STR, 4);
    eh->ident[ELF_CLASS_OFF] = ELF_CLASS_64;
    eh->ident[5]             = ELF_ENDIAN_LITTLE;
    eh->type                 = ELF_HET_EXEC;
    eh->machine              = 0;
    eh->version              = 1;
    eh->entry                = 0x2000;
    eh->ph_offset            = sizeof(elf64_header);
    eh->flags                = 0;
    eh->header_size          = sizeof(elf64_header);
    eh->ph_entry_size        = sizeof(elf64_program_header);
    eh->ph_entry_count       = n;

    ph = (elf64_program_header *)((uint8_t *)eh + sizeof(elf64_header));
    for (i = 0; i < n; i++) {
        memset(&ph[i], 0, sizeof(ph[i]));
        ph[i].type      = ELF_PT_LOAD;
        ph[i].offset    = segs[i].offset;
        ph[i].paddr     = segs[i].paddr;
        ph[i].file_size = segs[i].filesz;
        ph[i].mem_size  = segs[i].filesz;
        ph[i].align     = 1;
        for (j = 0; j < segs[i].fillsz; j++) {
            segs[i].payload[j] = (uint8_t)(0x60U + i + j);
        }
    }
}

#define ELF_HDR_SZ_2 (sizeof(elf64_header) + 2 * sizeof(elf64_program_header))

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

/* A segment whose 64-bit file_size does not fit the uint32_t length the
 * flash hash reader consumes must be rejected outright. Pre-fix, the
 * size was silently truncated (2^32 -> 0 bytes hashed) and an image
 * whose stored digest matched the truncated walk verified OK. */
START_TEST(test_elf_scatter_filesz_over_32bit_rejected)
{
    uint8_t expected_digest[WOLFBOOT_SHA_DIGEST_SIZE];
    unsigned long entry = 0;
    uint32_t fw_size = (uint32_t)ELF_HDR_SZ_2 + SEG2_SIZE;
    struct seg_spec segs[2];
    struct wolfBoot_image boot;
    wolfBoot_hash_t ctx;
    int ret;

    map_boot_partition();

    memset(seg1_flash, 0, sizeof(seg1_flash));
    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ_2 + 0x1000U; /* layout gap, never hashed */
    segs[0].filesz = 0x100000000ULL;         /* 2^32: truncates to 0 */
    segs[0].paddr  = (uint64_t)(uintptr_t)seg2_flash; /* 0 bytes read */
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = 0;
    segs[1].offset = ELF_HDR_SZ_2;
    segs[1].filesz = SEG2_SIZE;
    segs[1].paddr  = (uint64_t)(uintptr_t)seg2_flash;
    segs[1].payload = seg2_flash;
    segs[1].fillsz  = SEG2_SIZE;

    build_scattered_image_n(segs, 2, fw_size);

    /* Replay the pre-fix walk: the oversized segment contributes zero
     * hashed bytes, only seg2 does. */
    ck_assert_int_eq(wolfBoot_open_image(&boot, PART_BOOT), 0);
    ck_assert_int_eq(header_hash(&ctx, &boot), 0);
    ck_assert_int_eq(update_hash_flash_fwimg(&ctx, &boot, 0, (uint32_t)ELF_HDR_SZ_2), 0);
    ck_assert_int_eq(
        update_hash_flash_addr(&ctx, (uintptr_t)seg2_flash, SEG2_SIZE,
                               PART_IS_EXT(&boot)),
        0);
    ck_assert_int_eq(final_hash(&ctx, expected_digest), 0);
    patch_expected_digest(expected_digest);

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    /* Pre-fix this verified OK (ret 0) with the truncated size. */
    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

/* A segment whose file layout (offset + file_size) extends past the
 * manifest image must be rejected. Pre-fix, intermediate segments were
 * never bounds-checked (only the last one, after the loop) and an image
 * whose stored digest matched the out-of-layout walk verified OK. */
START_TEST(test_elf_scatter_segment_beyond_fw_size_rejected)
{
    uint8_t expected_digest[WOLFBOOT_SHA_DIGEST_SIZE];
    unsigned long entry = 0;
    uint32_t fw_size = (uint32_t)ELF_HDR_SZ_2 + SEG2_SIZE;
    struct seg_spec segs[2];
    struct wolfBoot_image boot;
    wolfBoot_hash_t ctx;
    int ret;

    map_boot_partition();

    memset(seg1_flash, 0, sizeof(seg1_flash));
    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ_2;
    segs[0].filesz = SEG1_SIZE; /* 0x2000 > the 64-byte layout slack */
    segs[0].paddr  = (uint64_t)(uintptr_t)seg1_flash;
    segs[0].payload = seg1_flash;
    segs[0].fillsz  = SEG1_SIZE;
    segs[1].offset = ELF_HDR_SZ_2;
    segs[1].filesz = SEG2_SIZE;
    segs[1].paddr  = (uint64_t)(uintptr_t)seg2_flash;
    segs[1].payload = seg2_flash;
    segs[1].fillsz  = SEG2_SIZE;

    build_scattered_image_n(segs, 2, fw_size);

    /* Replay the pre-fix walk: both segments are hashed in full at their
     * paddr locations despite seg0's layout extending past fw_size. */
    ck_assert_int_eq(wolfBoot_open_image(&boot, PART_BOOT), 0);
    ck_assert_int_eq(header_hash(&ctx, &boot), 0);
    ck_assert_int_eq(update_hash_flash_fwimg(&ctx, &boot, 0, (uint32_t)ELF_HDR_SZ_2), 0);
    ck_assert_int_eq(
        update_hash_flash_addr(&ctx, (uintptr_t)seg1_flash, SEG1_SIZE,
                               PART_IS_EXT(&boot)),
        0);
    ck_assert_int_eq(
        update_hash_flash_addr(&ctx, (uintptr_t)seg2_flash, SEG2_SIZE,
                               PART_IS_EXT(&boot)),
        0);
    ck_assert_int_eq(final_hash(&ctx, expected_digest), 0);
    patch_expected_digest(expected_digest);

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    /* Pre-fix this verified OK (ret 0). */
    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

/* A paddr whose segment range overflows the address space must be
 * rejected before any flash read. Pre-fix this walked off into
 * unmapped memory (segfault here; bus fault/hang on target). */
START_TEST(test_elf_scatter_paddr_range_overflow_rejected)
{
    unsigned long entry = 0;
    uint32_t fw_size = IMG_FW_SIZE;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG_SIZE;
    segs[0].paddr  = UINT64_MAX - 4; /* +SEG_SIZE wraps past UINT64_MAX */
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = SEG_SIZE;

    build_scattered_image_n(segs, 1, fw_size);

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

#if UINTPTR_MAX < UINT64_MAX
/* A paddr that fits in 64 bits but not in the destination (uintptr_t)
 * width must be rejected: the load_addr cast after the check would
 * silently wrap and the hash walk would read the wrapped address.
 * 32-bit builds only: on 64-bit builds UINTPTR_MAX == UINT64_MAX and
 * the case above already covers it. */
START_TEST(test_elf_scatter_paddr_beyond_pointer_width_rejected)
{
    unsigned long entry = 0;
    uint32_t fw_size = IMG_FW_SIZE;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG_SIZE;
    segs[0].paddr  = 1ULL << 32; /* fits uint64_t, exceeds 32-bit width */
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = SEG_SIZE;

    build_scattered_image_n(segs, 1, fw_size);

    ret = wolfBoot_check_flash_image_elf(PART_BOOT, &entry);

    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST
#endif

/* --- wolfBoot_load_flash_image_elf() (the store/restore path) ---
 *
 * The load function walks the same program header table and copies each
 * PT_LOAD segment from the manifest (fw_base + offset) to its scattered
 * destination (paddr + BASE_OFF). In this harness the boot partition is
 * mmap'd at MOCK_ADDRESS_BOOT (== WOLFBOOT_PARTITION_BOOT_ADDRESS) and
 * BASE_OFF is 0, so a valid destination is any address inside
 * [MOCK_ADDRESS_BOOT, MOCK_ADDRESS_BOOT + WOLFBOOT_PARTITION_SIZE).
 * The mock flash layer fails the test on any erase/write outside the
 * known partition ranges, so an unvalidated paddr cannot walk off into
 * unmapped memory here. */

#define LOAD_DEST (MOCK_ADDRESS_BOOT + 0x4000)

static void set_load_paddr(uint64_t paddr)
{
    uint8_t *manifest = (uint8_t *)(uintptr_t)MOCK_ADDRESS_BOOT;
    elf64_program_header *ph = (elf64_program_header *)
        (manifest + IMAGE_HEADER_SIZE + sizeof(elf64_header));

    ph->paddr = paddr;
}

START_TEST(test_elf_scatter_load_valid_image_restores)
{
    unsigned long entry = 0;
    uint8_t *manifest = (uint8_t *)(uintptr_t)MOCK_ADDRESS_BOOT;
    uint8_t *source   = manifest + IMAGE_HEADER_SIZE + ELF_HDR_SZ;
    uint8_t *dest     = (uint8_t *)(uintptr_t)LOAD_DEST;
    unsigned int i;
    int ret;

    map_boot_partition();

    build_scattered_image();
    set_load_paddr(LOAD_DEST);
    for (i = 0; i < SEG_SIZE; i++) {
        source[i] = (uint8_t)(0x30U + i);
    }

    ret = wolfBoot_load_flash_image_elf(PART_BOOT, &entry, 0);

    ck_assert_int_eq(ret, 0);
    for (i = 0; i < SEG_SIZE; i++) {
        ck_assert_uint_eq(dest[i], source[i]);
    }

    unmap_boot_partition();
}
END_TEST

/* A segment whose file layout (offset + file_size) extends past the
 * manifest image must be rejected before any flash write. Pre-fix the
 * source read walked past fw_size and the copy still "succeeded"
 * (ret 0). */
START_TEST(test_elf_scatter_load_segment_beyond_fw_size_rejected)
{
    unsigned long entry = 0;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG1_SIZE; /* 0x2000 > the manifest layout slack */
    segs[0].paddr  = LOAD_DEST;
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = 0;

    build_scattered_image_n(segs, 1, IMG_FW_SIZE);

    ret = wolfBoot_load_flash_image_elf(PART_BOOT, &entry, 0);

    /* Pre-fix this returned 0: the mock happily erased/wrote the valid
     * destination with bytes read past fw_size. */
    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

/* A paddr whose segment range overflows the address space must be
 * rejected before any flash access. Pre-fix the wrapped load_addr drove
 * the mock into its out-of-range erase check (fail("Invalid address")). */
START_TEST(test_elf_scatter_load_paddr_range_overflow_rejected)
{
    unsigned long entry = 0;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG_SIZE;
    segs[0].paddr  = UINT64_MAX - 4; /* +SEG_SIZE wraps past UINT64_MAX */
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = SEG_SIZE;

    build_scattered_image_n(segs, 1, IMG_FW_SIZE);

    ret = wolfBoot_load_flash_image_elf(PART_BOOT, &entry, 0);

    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

/* A destination outside the boot partition must be rejected before any
 * flash access. Pre-fix the mock's out-of-range erase check caught it
 * with fail("Invalid address"); on real hardware this is an
 * erase/write at an unintended location. */
START_TEST(test_elf_scatter_load_dest_outside_partition_rejected)
{
    unsigned long entry = 0;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG_SIZE;
    segs[0].paddr  = (uint64_t)(MOCK_ADDRESS_BOOT + WOLFBOOT_PARTITION_SIZE +
                                0x1000);
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = SEG_SIZE;

    build_scattered_image_n(segs, 1, IMG_FW_SIZE);

    ret = wolfBoot_load_flash_image_elf(PART_BOOT, &entry, 0);

    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

/* A program header that lies past the end of the manifest image cannot
 * be read; the load must abort instead of consuming the uninitialized
 * header locals. The PHT sits at fw offset 64 (right after the 64-byte
 * ELF header, the only offset check_scatter_format accepts), so a
 * fw_size of 100 makes the 56-byte phdr read (64 + 56) run past
 * fw_size. Pre-fix the read failure was ignored and p64 held
 * indeterminate stack data (valgrind: conditional jump on uninitialised
 * value at the is_loadable check). */
START_TEST(test_elf_scatter_load_phdr_read_failure_rejected)
{
    unsigned long entry = 0;
    struct seg_spec segs[1];
    int ret;

    map_boot_partition();

    memset(seg2_flash, 0, sizeof(seg2_flash));
    segs[0].offset = ELF_HDR_SZ;
    segs[0].filesz = SEG_SIZE;
    segs[0].paddr  = LOAD_DEST;
    segs[0].payload = seg2_flash;
    segs[0].fillsz  = SEG_SIZE;

    build_scattered_image_n(segs, 1, 100); /* PHT extends past fw_size */

    ret = wolfBoot_load_flash_image_elf(PART_BOOT, &entry, 0);

    ck_assert_int_eq(ret, -1);

    unmap_boot_partition();
}
END_TEST

Suite *elf_scatter_suite(void)
{
    Suite *s       = suite_create("ELF flash-scatter image check");
    TCase *tc      = tcase_create("wolfBoot_check_flash_image_elf");
    TCase *tc_load = tcase_create("wolfBoot_load_flash_image_elf");
    tcase_add_test(tc, test_elf_scatter_valid_image_verifies_ok);
    tcase_add_test(tc, test_elf_scatter_corrupted_segment_rejected);
    tcase_add_test(tc, test_elf_scatter_filesz_over_32bit_rejected);
    tcase_add_test(tc, test_elf_scatter_segment_beyond_fw_size_rejected);
    tcase_add_test(tc, test_elf_scatter_paddr_range_overflow_rejected);
#if UINTPTR_MAX < UINT64_MAX
    tcase_add_test(tc, test_elf_scatter_paddr_beyond_pointer_width_rejected);
#endif
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    tcase_add_test(tc_load, test_elf_scatter_load_valid_image_restores);
    tcase_add_test(tc_load,
                   test_elf_scatter_load_segment_beyond_fw_size_rejected);
    tcase_add_test(tc_load,
                   test_elf_scatter_load_paddr_range_overflow_rejected);
    tcase_add_test(tc_load,
                   test_elf_scatter_load_dest_outside_partition_rejected);
    tcase_add_test(tc_load, test_elf_scatter_load_phdr_read_failure_rejected);
    tcase_set_timeout(tc_load, 10);
    suite_add_tcase(s, tc_load);
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
