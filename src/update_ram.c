/* update_ram.c
 *
 * Implementation for RAM based updater
 *
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

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "hooks.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include <string.h>
#include "encrypt.h"

#ifdef WOLFBOOT_UBOOT_LEGACY
#include "gpt.h" /* gpt_crc32_* helpers (reflected CRC-32, poly 0xEDB88320) */
#endif

#ifdef WOLFBOOT_TPM
#include "tpm.h"
#endif
#ifdef WOLFBOOT_ELF
#include "elf.h"
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
#include "../hal/zynqmp_atf.h"
#endif

extern void hal_flash_dualbank_swap(void);
/* DTS helpers declared in include/image.h under (MMU || WOLFBOOT_FDT). */

extern uint32_t kernel_load_addr;
extern uint32_t dts_load_addr;

#if defined(__WOLFBOOT) && defined(WOLFBOOT_LOAD_ADDRESS)
extern uint8_t _end[];  /* linker symbol: end of wolfBoot BSS */
#endif

/* Return non-zero if the RAM load region [img_lo, img_hi) overlaps wolfBoot's
 * own region [wb_lo, wb_hi). wb_lo == 0 means the origin is unknown, so guard
 * only that the image loads above wolfBoot's end (wb_hi). Pure arithmetic,
 * exposed at file scope for unit testing (tools/unit-tests/unit-update-ram.c). */
static inline int ramboot_region_overlap(uintptr_t img_lo, uintptr_t img_hi,
    uintptr_t wb_lo, uintptr_t wb_hi)
{
    if (img_hi < img_lo)
        return 1; /* header+size wrapped the end address: reject conservatively */
    if (wb_lo == 0)
        return (img_lo < wb_hi);
    return (img_lo < wb_hi && img_hi > wb_lo);
}

#if ((defined(EXT_FLASH) && defined(NO_XIP)) || \
    (defined(EXT_ENCRYPTED) && defined(MMU))) && \
    !defined(WOLFBOOT_NO_RAMBOOT)
    /* Load firmware to RAM on boot (single flash read) */
    #undef  WOLFBOOT_USE_RAMBOOT
    #define WOLFBOOT_USE_RAMBOOT
#endif

#ifdef WOLFBOOT_USE_RAMBOOT

/* Function to load image from flash to ram */
int wolfBoot_ramboot(struct wolfBoot_image *img, uint8_t *src, uint8_t *dst)
{
    int ret;
    uint32_t img_size;
    BENCHMARK_DECLARE();

    /* read header into RAM */
    wolfBoot_printf("Loading header %d bytes from %p to %p\n",
        IMAGE_HEADER_SIZE, src, dst);
#if defined(EXT_FLASH) && defined(NO_XIP)
    ret = ext_flash_read((uintptr_t)src, dst, IMAGE_HEADER_SIZE);
    if (ret != IMAGE_HEADER_SIZE){
        wolfBoot_printf("Error reading header at %p\n", src);
        return -1;
    }
#else
    memcpy(dst, src, IMAGE_HEADER_SIZE);
#endif

    /* check for valid header and version */
    ret = wolfBoot_get_blob_version((uint8_t*)dst);
    if (ret <= 0) {
        wolfBoot_printf("No valid image found at %p\n", src);
        return -1;
    }

    /* determine size of partition */
    img_size = wolfBoot_image_size((uint8_t*)dst);
#if !defined(WOLFBOOT_FIXED_PARTITIONS) && !defined(WOLFBOOT_RAMBOOT_MAX_SIZE)
#  error "WOLFBOOT_FIXED_PARTITIONS or WOLFBOOT_RAMBOOT_MAX_SIZE required to bound the RAM load"
#endif
    /* Bound the UNAUTHENTICATED image length before it drives the copy into the
     * RAM load region: the image is loaded to RAM before its signature is
     * verified, so this length (read from the not-yet-authenticated header) is
     * attacker-influenceable and must be range checked first. When both are
     * configured, WOLFBOOT_RAMBOOT_MAX_SIZE takes precedence: it is the explicit
     * cap on the RAM load region and may be tighter than the partition size. */
#if defined(WOLFBOOT_RAMBOOT_MAX_SIZE)
    if (img_size > WOLFBOOT_RAMBOOT_MAX_SIZE) {
        wolfBoot_printf("Invalid image size %u at %p\n", img_size, src);
        return -1;
    }
#elif defined(WOLFBOOT_FIXED_PARTITIONS)
    if (WOLFBOOT_PARTITION_SIZE <= IMAGE_HEADER_SIZE ||
            img_size > (uint32_t)(WOLFBOOT_PARTITION_SIZE - IMAGE_HEADER_SIZE)) {
        wolfBoot_printf("Invalid image size %u at %p\n", img_size, src);
        return -1;
    }
#endif

#if defined(__WOLFBOOT) && defined(WOLFBOOT_LOAD_ADDRESS)
    /* Overlap check: the image destination must not overwrite wolfBoot's own
     * code/data/bss (ends at _end). The image occupies [dst, dst+header+size]. */
    {
        uintptr_t wb_hi  = (uintptr_t)_end;
        uintptr_t img_lo = (uintptr_t)dst;
        uintptr_t img_hi = img_lo + (uintptr_t)IMAGE_HEADER_SIZE +
                           (uintptr_t)img_size;
#if defined(WOLFBOOT_ORIGIN)
        /* wolfBoot spans [WOLFBOOT_ORIGIN, _end]; range-intersect so it holds
         * whether wolfBoot is below or above the image -- e.g. ZynqMP FSBL runs
         * from high OCM while the image loads to low DDR, where the plain
         * "dst < _end" test gave a false positive. */
        uintptr_t wb_lo = (uintptr_t)(WOLFBOOT_ORIGIN);
#else
        /* Without WOLFBOOT_ORIGIN, wb_lo=0 keeps the original low-addr guard. */
        uintptr_t wb_lo = 0;
#endif
        if (ramboot_region_overlap(img_lo, img_hi, wb_lo, wb_hi)) {
            wolfBoot_printf("Error: image %p-%p overlaps wolfBoot %p-%p\n",
                (void*)img_lo, (void*)img_hi, (void*)wb_lo, (void*)wb_hi);
            return -1;
        }
    }
#endif

    /* Read the entire image into RAM */
    wolfBoot_printf("Loading image %d bytes from %p to %p...",
        img_size, src + IMAGE_HEADER_SIZE, dst + IMAGE_HEADER_SIZE);
    BENCHMARK_START();
#if defined(EXT_FLASH) && defined(NO_XIP)
    ret = ext_flash_read((uintptr_t)src + IMAGE_HEADER_SIZE,
                                    dst + IMAGE_HEADER_SIZE, img_size);
    if (ret < 0) {
        wolfBoot_printf("Error reading image at %p\n", src);
        return -1;
    }
#else
    memcpy(dst + IMAGE_HEADER_SIZE, src + IMAGE_HEADER_SIZE, img_size);
#endif
    BENCHMARK_END("done");

    /* mark image as no longer external */
    img->not_ext = 1;

    return 0; /* success */
}
#endif /* WOLFBOOT_USE_RAMBOOT */

#ifdef WOLFBOOT_UBOOT_LEGACY
/* Validate a 64-byte U-Boot legacy image header (image_header_t).
 *
 * Layout (all multi-byte fields stored big-endian on flash):
 *   0x00  4   ih_magic   0x27051956
 *   0x04  4   ih_hcrc    CRC32 of header with hcrc treated as 0
 *   0x08  4   ih_time    timestamp
 *   0x0C  4   ih_size    payload size (excl. header)
 *   0x10  4   ih_load    load address
 *   0x14  4   ih_ep      entry point
 *   0x18  4   ih_dcrc    data CRC32 (validated by wolfBoot signature)
 *   0x1C  1   ih_os
 *   0x1D  1   ih_arch
 *   0x1E  1   ih_type
 *   0x1F  1   ih_comp
 *   0x20 32   ih_name
 *
 * Magic alone is a ~1-in-2^32 collision for random data, so we also
 * validate the header CRC32 (~2^-32) and the payload size, dropping the
 * joint false-positive probability to roughly 2^-64. This matches
 * U-Boot's own mkimage/bootm validation. */

/* Read a 32-bit big-endian uImage header field from a (possibly
 * unaligned) location and return it in host byte order. Done locally
 * rather than via fdt32_to_cpu() so WOLFBOOT_UBOOT_LEGACY does not
 * require src/fdt.c to be linked -- some targets enable uImage support
 * without the FDT/MMU code (e.g. zynq7000 when MMU != 1). The memcpy
 * also avoids an unaligned 32-bit load on architectures that fault. */
static uint32_t uboot_read_be32(const uint8_t *p)
{
    uint32_t x;
    memcpy(&x, p, sizeof(x));
#ifdef BIG_ENDIAN_ORDER
    return x;
#else
    return ((x & 0xFF000000U) >> 24) |
           ((x & 0x00FF0000U) >>  8) |
           ((x & 0x0000FF00U) <<  8) |
           ((x & 0x000000FFU) << 24);
#endif
}

static int uboot_legacy_header_valid(const uint8_t *hdr, uint32_t total)
{
    struct gpt_crc32_ctx ctx;
    uint8_t scratch[UBOOT_IMG_HDR_SZ];
    uint32_t magic;
    uint32_t hcrc;
    uint32_t size;
    uint32_t crc;

    if (hdr == NULL)
        return 0;
    if (total < UBOOT_IMG_HDR_SZ)
        return 0;

    /* ih_magic is stored big-endian on flash; UBOOT_IMG_HDR_MAGIC is
     * defined as the host-order word matching that BE encoding (see
     * include/image.h -- different on LE vs BE hosts). */
    memcpy(&magic, hdr + 0x00, sizeof(magic));
    if (magic != UBOOT_IMG_HDR_MAGIC)
        return 0;

    /* ih_size: big-endian payload length. uboot_read_be32 converts to
     * host order (no-op on a BE host, byte-swap on LE). Reject zero
     * and anything that would overrun the signed image. */
    size = uboot_read_be32(hdr + 0x0C);
    if (size == 0)
        return 0;
    if (size > (total - UBOOT_IMG_HDR_SZ))
        return 0;

    /* ih_hcrc: CRC32 of the header with the hcrc field treated as zero.
     * Read (and convert from big-endian) before zeroing the field. */
    memcpy(scratch, hdr, UBOOT_IMG_HDR_SZ);
    hcrc = uboot_read_be32(scratch + 0x04);
    memset(scratch + 0x04, 0, sizeof(hcrc));
    gpt_crc32_init(&ctx);
    gpt_crc32_update(&ctx, scratch, UBOOT_IMG_HDR_SZ);
    crc = gpt_crc32_final(&ctx);
    if (hcrc != crc)
        return 0;

    return 1;
}
#endif /* WOLFBOOT_UBOOT_LEGACY */

void RAMFUNCTION wolfBoot_start(void)
{
    int active = -1, ret = 0;
    /* Candidates already tried this boot; a failed RAM image cannot be
     * erased to invalidate it like the flash path does. */
    int tried_boot = 0, tried_update = 0;
    struct wolfBoot_image os_image;
    BENCHMARK_DECLARE();
#ifdef WOLFBOOT_UBOOT_LEGACY
    uint8_t *image_ptr;
    /* uImage ih_ep, kept only when the entry point differs from the load
     * address (see the do_boot() entry override below). */
    uint32_t *uboot_entry = NULL;
    /* Set when a later stage (ELF/FIT) re-derives the load address and so
     * supplies its own entry point, which then wins over ih_ep. */
    int stage_entry_override = 0;
#endif
    uint32_t *load_address = NULL;
    uint32_t *source_address = NULL;
#ifdef WOLFBOOT_FIXED_PARTITIONS
    uint8_t p_state;
#endif
#if defined(MMU) || defined(WOLFBOOT_FDT)
    /* Passed to the 2-arg do_boot() below; NULL when there is no DTS (e.g.
     * WOLFBOOT_FDT without MMU, booting a non-FIT image -> no fixup). */
    uint8_t *dts_addr = NULL;
#endif
#ifdef MMU
    uint32_t dts_size = 0;
    /* Validated view of the FIT staged at load_address. */
    fdt_ctx  fit_ctx;
    /* HDR_DEVICE_TREE_DIGEST snapshot, taken before the raw DTB is loaded. */
    uint8_t  dts_digest[WOLFBOOT_SHA_DIGEST_SIZE];
    uint8_t *dts_tlv = NULL;
    uint16_t dts_tlv_len = 0;
    int      dts_digest_present = 0; /* 0 absent, 1 valid, -1 malformed */
#if defined(EXT_FLASH) && defined(WOLFBOOT_DTS_BOOT_ADDRESS)
    /* FDT header peek (fdt_peek_size needs >= 40 bytes, 4-byte aligned) */
    uint8_t  dts_hdr[64] __attribute__((aligned(4)));
#endif
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
    /* When wolfBoot is the FSBL, the boot FIT carries an "atf" (BL31)
     * sub-image. If present, hand off to BL31 instead of jumping to the
     * kernel directly. */
    uintptr_t bl31_entry = 0;
#endif
#if !defined(ALLOW_DOWNGRADE) && defined(WOLFBOOT_FIXED_PARTITIONS)
    uint32_t boot_v = wolfBoot_current_firmware_version();
    uint32_t update_v = wolfBoot_update_firmware_version();
    uint32_t max_v = (boot_v > update_v) ? boot_v : update_v;
#endif /* !ALLOW_DOWNGRADE && WOLFBOOT_FIXED_PARTITIONS */

    memset(&os_image, 0, sizeof(struct wolfBoot_image));

    for (;;) {
    #if defined(WOLFBOOT_DUALBOOT) && defined(WOLFBOOT_FIXED_PARTITIONS)
        if (active < 0)
            active = wolfBoot_dualboot_candidate();
        if (active == PART_BOOT)
            source_address = (uint32_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
        else
            source_address = (uint32_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    #else
        if (active < 0)
            active = wolfBoot_dualboot_candidate_addr((void**)&source_address);
        else if (active == PART_BOOT)
            source_address = (uint32_t*)hal_get_primary_address();
        else
            source_address = (uint32_t*)hal_get_update_address();
    #endif
        if (active < 0) { /* panic if no images available */
            wolfBoot_printf("No valid image found!\n");
            wolfBoot_panic();
            break;
        }
#if !defined(ALLOW_DOWNGRADE) && defined(WOLFBOOT_FIXED_PARTITIONS)
        {
            uint32_t active_v = (active == PART_UPDATE) ? update_v : boot_v;
            if ((max_v > 0U) && (active_v < max_v)) {
                wolfBoot_printf("Rollback to lower version not allowed\n");
                wolfBoot_panic();
                break;
            }
        }
#endif /* !ALLOW_DOWNGRADE && WOLFBOOT_FIXED_PARTITIONS */

    #if defined(WOLFBOOT_DUALBOOT) && defined(WOLFBOOT_FIXED_PARTITIONS)
        wolfBoot_printf("Trying %s partition at %p\n",
                active == PART_BOOT ? "Boot" : "Update", source_address);
    #else
        wolfBoot_printf("Trying partition %d at %p\n",
                active, source_address);
    #endif

    #ifdef WOLFBOOT_USE_RAMBOOT
        load_address = (uint32_t *)(uintptr_t)(WOLFBOOT_LOAD_ADDRESS -
            IMAGE_HEADER_SIZE);
      #if defined(EXT_ENCRYPTED) && defined(MMU)
        ret = wolfBoot_ram_decrypt((uint8_t*)source_address,
            (uint8_t*)load_address);
      #else
        ret = wolfBoot_ramboot(&os_image, (uint8_t*)source_address,
            (uint8_t*)load_address);
      #endif
        if (ret != 0) {
            goto backup_on_failure;
        }
    #else
        load_address = source_address;
    #endif
    #if !defined(WOLFBOOT_FIXED_PARTITIONS) || \
            defined(WOLFBOOT_USE_RAMBOOT)
        ret = wolfBoot_open_image_address(&os_image, (uint8_t*)load_address);
    #else
        ret = wolfBoot_open_image(&os_image, active);
    #endif
        if (ret < 0) {
            goto backup_on_failure;
        }

#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
        /* Verify image integrity (hash check) */
        wolfBoot_printf("Checking integrity...");
        BENCHMARK_START();
        ret = wolfBoot_verify_integrity(&os_image);
        if (ret < 0) {
            wolfBoot_printf("FAILED\n");
            goto backup_on_failure;
        }
        BENCHMARK_END("done");

        /* Verify image authenticity (signature check) */
        wolfBoot_printf("Verifying signature...");
        BENCHMARK_START();
        ret = wolfBoot_verify_authenticity(&os_image);
        if (ret < 0) {
            wolfBoot_printf("FAILED\n");
            goto backup_on_failure;
        }
        BENCHMARK_END("done");

#endif

        {
            /* Success - integrity and signature valid */
        #if !defined(WOLFBOOT_NO_LOAD_ADDRESS) && defined(WOLFBOOT_LOAD_ADDRESS)
            load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
        #elif !defined(NO_XIP)
            load_address = (uint32_t*)os_image.fw_base;
        #else
            #error missing WOLFBOOT_LOAD_ADDRESS or XIP
        #endif
            wolfBoot_printf("Successfully selected image in part: %d\n", active);
            break;
        }

backup_on_failure:
        wolfBoot_printf("Failure %d: Part %d, Hdr %d, Hash %d, Sig %d\n", ret,
            active, os_image.hdr_ok, os_image.sha_ok, os_image.signature_ok);
        /* panic if authentication fails and no backup */
        if (!wolfBoot_fallback_is_possible()) {
            wolfBoot_printf("Impossible recovery with fallback.\n");
            wolfBoot_panic();
            break;
        }
        if (active == PART_BOOT)
            tried_boot = 1;
        else
            tried_update = 1;
        if (tried_boot && tried_update) {
            /* Both partitions were tried and both failed: the images that
             * made fallback look possible are invalid, and nothing left to
             * boot. */
            wolfBoot_printf(
                "Both images failed verification; no valid image to boot.\n");
            wolfBoot_panic();
            break;
        }
        /* Switch to the other partition */
        active ^= 1;
        wolfBoot_printf("Active is now: %d\n", active);
        continue;
    }
#ifdef UNIT_TEST
    if (wolfBoot_panicked != 0) {
        wolfBoot_printf("panic!\n");
        return;
    }
#endif

    wolfBoot_printf("Firmware Valid\n");

    /* First time we boot this update, set to TESTING to await
     * confirmation from the system
     */
#ifdef WOLFBOOT_FIXED_PARTITIONS
    if ((wolfBoot_get_partition_state(active, &p_state) == 0) &&
        (p_state == IMG_STATE_UPDATING))
    {
        #ifdef EXT_FLASH
        ext_flash_unlock();
        #else
        hal_flash_unlock();
        #endif
        wolfBoot_set_partition_state(active, IMG_STATE_TESTING);
        #ifdef EXT_FLASH
        ext_flash_lock();
        #else
        hal_flash_lock();
        #endif
    }
#endif

#ifdef WOLFBOOT_UBOOT_LEGACY
    /* Check for U-Boot legacy format image header. Validate magic +
     * header CRC32 + payload size (uboot_legacy_header_valid) before
     * stripping the 64-byte header -- a non-uImage payload whose first
     * 4 bytes happen to collide with UBOOT_IMG_HDR_MAGIC (~1 in 2^32)
     * cannot be misinterpreted because the CRC + size checks fail.
     *
     * uImage header (64 bytes, big-endian fields):
     *   off 0  : magic         0x27051956
     *   off 4  : header CRC
     *   off 8  : creation time
     *   off 12 : data size
     *   off 16 : ih_load       data load address
     *   off 20 : ih_ep         entry point address
     *   off 24 : data CRC
     *   off 28 : os/arch/type/comp
     *   off 32 : name (32 bytes)
     *
     * After validation, honor the uImage's ih_load: U-Boot bootm copies
     * the payload to ih_load and jumps to ih_ep, because PowerPC /
     * VxWorks kernels are typically built non-relocatable with absolute
     * references baked in. Falling back to the wolfBoot default
     * WOLFBOOT_LOAD_ADDRESS would place the kernel at the wrong address
     * and the first internal jump would fault.
     *
     * (The ih_load override applies only when ih_load is non-zero --
     * typical for VxWorks: 0x00100000; for Linux PPC: 0x00000000 ->
     * leave load_address alone.) */
    image_ptr = wolfBoot_peek_image(&os_image, 0, NULL);
    if (image_ptr != NULL &&
        uboot_legacy_header_valid(image_ptr, os_image.fw_size)) {
        uint32_t ih_load;
        uint32_t ih_ep;

        ih_load = uboot_read_be32((const uint8_t*)image_ptr + 16);
        ih_ep   = uboot_read_be32((const uint8_t*)image_ptr + 20);

        wolfBoot_printf("U-Boot Legacy header detected: load=0x%x ep=0x%x "
            "(skipping %d bytes)\n",
            ih_load, ih_ep, UBOOT_IMG_HDR_SZ);

        /* Skip 64 bytes (size of legacy format image header). */
        os_image.fw_base += UBOOT_IMG_HDR_SZ;
        os_image.fw_size -= UBOOT_IMG_HDR_SZ;

        if (ih_load != 0) {
            load_address = (uint32_t*)(uintptr_t)ih_load;
#ifdef WOLFBOOT_USE_RAMBOOT
            /* RAMBOOT already staged the payload at os_image.fw_base, and the
             * generic copy-to-RAM memcpy below is compiled out under RAMBOOT.
             * Relocate the payload to ih_load ourselves when the two differ,
             * mirroring the non-RAMBOOT memcpy. memmove: both ranges are in
             * RAM and may overlap. */
            if ((uintptr_t)ih_load != (uintptr_t)os_image.fw_base) {
                memmove((void*)(uintptr_t)ih_load, os_image.fw_base,
                    os_image.fw_size);
            }
#endif
            /* bootm relocates to ih_load but enters at ih_ep: kernels built
             * with a preamble ahead of the entry point set the two to
             * different addresses. Remember the entry point; ih_load remains
             * the relocation destination. */
            if ((ih_ep != 0) && (ih_ep != ih_load)) {
                uboot_entry = (uint32_t*)(uintptr_t)ih_ep;
            }
        } else {
            /* Linux PPC path: leave load_address alone, just advance it
             * past the header to match upstream behaviour. load_address is
             * a uint32_t*, so advance by BYTES, not words.
             * ih_ep is deliberately ignored here: with ih_load == 0 there is
             * no relocation destination to enter past, and upstream enters at
             * the payload start. A uImage built with "mkimage -a 0 -e <ep>"
             * is therefore entered at the header offset, not at ih_ep. */
            load_address = (uint32_t*)((uint8_t*)load_address +
                UBOOT_IMG_HDR_SZ);
        }
    }
#endif

#ifdef __GNUC__
    /* WOLFBOOT_LOAD_ADDRESS can be 0 address.
     * Do not warn on use of NULL for memcpy */
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wnonnull"
#endif

#ifndef WOLFBOOT_USE_RAMBOOT
    /* copy image to RAM */
    #if defined(EXT_FLASH) && defined(NO_XIP)
    wolfBoot_printf("Loading flash image from %p to RAM at %p (%d bytes)\n",
        os_image.fw_base, load_address, os_image.fw_size);
    ret = ext_flash_read((uintptr_t)os_image.fw_base, (uint8_t*)load_address,
        os_image.fw_size);
    /* Backends return the number of bytes read: a positive short read
     * leaves a truncated image in RAM, so require the full size.
     * ret is int, fw_size uint32_t: check the error range first and
     * cast for the size comparison to keep -Wsign-compare quiet. */
    if (ret < 0 || (uint32_t)ret != os_image.fw_size) {
        wolfBoot_printf("Error loading image at %p (ret %d)\n",
            os_image.fw_base, ret);
        return;
    }
    #else
    wolfBoot_printf("Copying image from %p to RAM at %p (%d bytes)\n",
        os_image.fw_base, load_address, os_image.fw_size);
    memcpy((void*)load_address, os_image.fw_base, os_image.fw_size);
    #endif
#endif /* !WOLFBOOT_USE_RAMBOOT */

#ifdef WOLFBOOT_ELF
    /* Load elf */
    if (elf_load_image_mmu((uint8_t*)load_address, os_image.fw_size,
            (uintptr_t*)&load_address, NULL) != 0){
        wolfBoot_printf("Invalid elf, falling back to raw binary\n");
    }
#ifdef WOLFBOOT_UBOOT_LEGACY
    else {
        stage_entry_override = 1;
    }
#endif
#endif

#ifdef MMU
    /* Snapshot the digest from the verified header before os_image can be
     * reused for the DTS partition below. (FIT DTBs are covered by the FIT.) */
    dts_tlv_len = wolfBoot_get_header(&os_image, HDR_DEVICE_TREE_DIGEST,
        &dts_tlv);
    if (dts_tlv_len != 0 && dts_tlv != NULL) {
        if (dts_tlv_len == WOLFBOOT_SHA_DIGEST_SIZE) {
            memcpy(dts_digest, dts_tlv, WOLFBOOT_SHA_DIGEST_SIZE);
            dts_digest_present = 1; /* present and well-formed */
        }
        else {
            /* A present-but-malformed digest TLV must not silently downgrade
             * to an unauthenticated DTB boot; treat it as a hard failure. */
            dts_digest_present = -1;
        }
    }

    /* Is this a Flattened uImage Tree (FIT) image (FDT format)? The
     * capacity handed to the parser is the number of verified bytes
     * staged at load_address, so a FIT that overstates its own size is
     * rejected here rather than read past. */
    if (fdt_open(&fit_ctx, (void*)load_address, os_image.fw_size) == 0) {
        fdt_ctx* fit = &fit_ctx;
        const char *kernel = NULL, *flat_dt = NULL, *ramdisk = NULL;
        const char *fpga = NULL;
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
        void *atf_load;
#endif

        wolfBoot_printf("Flattened uImage Tree: Size %d\n",
            (int)fdt_size(fit));

        (void)fit_find_images(fit, &kernel, &flat_dt, &ramdisk, &fpga);
#ifdef WOLFBOOT_FPGA_BITSTREAM
        /* Program the PL before booting so PL-dependent clocks and
         * peripherals are up first. */
        if (fpga != NULL) {
            if (fit_load_fpga(fit, fpga) != 0) {
                wolfBoot_printf("FIT: FPGA load failed\n");
                wolfBoot_panic();
            }
        }
#else
        (void)fpga;
#endif
        if (kernel != NULL) {
            void *new_load = fit_load_image(fit, kernel, NULL);
            if (new_load == NULL) {
                wolfBoot_printf("FIT: failed to load kernel '%s'\n", kernel);
                wolfBoot_panic();
            }
            load_address = new_load;
#ifdef WOLFBOOT_UBOOT_LEGACY
            stage_entry_override = 1;
#endif
        }
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
        /* Load BL31 (ARM Trusted Firmware) to its DDR exec address. Its entry
         * point is the FIT `load`/`entry` address returned here. Optional: if
         * absent, fall through to the normal direct boot. */
        atf_load = fit_load_image(fit, "atf", NULL);
        if (atf_load != NULL) {
            bl31_entry = (uintptr_t)atf_load;
            wolfBoot_printf("FIT: BL31 (atf) loaded at %p\n", atf_load);
        }
#endif
        if (flat_dt != NULL) {
            int dt_len = 0;
            uint8_t *dts_ptr = fit_load_image(fit, flat_dt, &dt_len);
            /* Bound the parse by the sub-image's own declared length,
             * not by the generic staging maximum: that is the tightest
             * bound available here. */
            int parsed = (dts_ptr != NULL && dt_len > 0)
                ? wolfBoot_get_dts_size(dts_ptr, (uint32_t)dt_len) : -1;
            if (dts_ptr != NULL &&
                    parsed >= (int)WOLFBOOT_DTS_MIN_SIZE &&
                    (uint32_t)parsed <= WOLFBOOT_DTS_MAX_SIZE) {
                /* Relocate to the load DTS address. The copy length is
                 * the parsed DTB size, clamped to WOLFBOOT_DTS_MAX_SIZE,
                 * not the FIT-declared property length. The staging window
                 * at WOLFBOOT_LOAD_DTS_ADDRESS must be at least that large
                 * (or the bound must be overridden for the target). */
                dts_addr = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
                dts_size = (uint32_t)parsed;
                wolfBoot_printf("Loading DTS: %p -> %p (%d bytes)\n",
                    dts_ptr, dts_addr, dts_size);
                memcpy(dts_addr, dts_ptr, dts_size);
            }
        }
#ifdef WOLFBOOT_FIT_RAMDISK
        if (ramdisk != NULL) {
            fdt_ctx dts_ctx;
            fdt_ctx* dts_for_initrd = NULL;

            /* The relocated DTB sits in the staging window, so that is
             * the capacity the initrd fixup may grow into. */
            if (dts_addr != NULL &&
                    fdt_open(&dts_ctx, dts_addr, WOLFBOOT_DTS_MAX_SIZE) == 0) {
                dts_for_initrd = &dts_ctx;
            }
            (void)fit_load_ramdisk(fit, ramdisk, dts_for_initrd);
        }
#else
        (void)ramdisk;
#endif
    }
    else {
        /* Prefer the HAL's memory-mapped DTB (unchanged for XIP targets); fall
         * back to external flash at WOLFBOOT_DTS_BOOT_ADDRESS when the HAL has
         * no usable address (NULL, or a flash offset on NO_XIP targets). */
        dts_addr = hal_get_dts_address();
        if (dts_addr != NULL) {
            ret = wolfBoot_get_dts_size(dts_addr, WOLFBOOT_DTS_MAX_SIZE);
            if (ret < (int)WOLFBOOT_DTS_MIN_SIZE ||
                    (uint32_t)ret > WOLFBOOT_DTS_MAX_SIZE) {
                wolfBoot_printf("DTB parse/size check failed - ignoring\n");
                dts_addr = NULL; /* never forward an unvalidated address */
            }
            else {
                dts_size = (uint32_t)ret;
                memcpy((void*)WOLFBOOT_LOAD_DTS_ADDRESS, dts_addr, dts_size);
                dts_addr = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
            }
        }
    #if defined(EXT_FLASH) && defined(WOLFBOOT_DTS_BOOT_ADDRESS)
        if (dts_addr == NULL) {
            /* Peek the FDT header for the size, clamp it, then read the body.
             * Each ext_flash_read length is checked so a short/failed read
             * never yields a partial or oversized tree. */
            ret = ext_flash_read((uintptr_t)WOLFBOOT_DTS_BOOT_ADDRESS,
                    dts_hdr, (int)sizeof(dts_hdr));
            if (ret == (int)sizeof(dts_hdr)) {
                uint32_t peeked = 0;
                /* Only the header has been read so far; fdt_peek_size
                 * validates just that much and reports the size to
                 * fetch. The complete blob is validated below. */
                ret = fdt_peek_size(dts_hdr, (uint32_t)sizeof(dts_hdr),
                    &peeked);
                if (ret == 0) {
                    dts_size = peeked;
                    if (ext_flash_read((uintptr_t)WOLFBOOT_DTS_BOOT_ADDRESS,
                            (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS, (int)dts_size)
                            == (int)dts_size &&
                            wolfBoot_get_dts_size(
                                (void*)WOLFBOOT_LOAD_DTS_ADDRESS, dts_size)
                                == (int)dts_size)
                        dts_addr = (uint8_t*)WOLFBOOT_LOAD_DTS_ADDRESS;
                    else
                        dts_size = 0;
                }
            }
        }
    #endif /* EXT_FLASH && WOLFBOOT_DTS_BOOT_ADDRESS */

        /* Authenticate the raw DTB before boot. dts_size == 0 with a non-NULL
         * address (e.g. a zeroed fdt totalsize) is rejected. A bound digest is
         * always enforced; a missing one only panics under
         * WOLFBOOT_REQUIRE_SIGNED_DTB, so unsigned raw-DTB targets keep booting
         * until they adopt 'sign --dts'. */
        if (dts_addr != NULL) {
            if (dts_size == 0) {
                wolfBoot_printf("DTB has zero size - rejecting\n");
                wolfBoot_panic();
            }
            if (dts_digest_present == 1) {
                if (wolfBoot_verify_dts_digest(dts_digest, dts_addr, dts_size)
                        != 0) {
                    wolfBoot_printf("DTB digest mismatch - rejecting\n");
                    wolfBoot_panic();
                }
                wolfBoot_printf("DTB digest verified\n");
            }
            else if (dts_digest_present < 0) {
                wolfBoot_printf("Malformed DTB digest TLV - rejecting\n");
                wolfBoot_panic();
            }
            else {
            #ifdef WOLFBOOT_REQUIRE_SIGNED_DTB
                wolfBoot_printf("No DTB digest - rejecting\n");
                wolfBoot_panic();
            #else
                wolfBoot_printf("Warning: DTB not authenticated (sign --dts)\n");
            #endif
            }
        }
    }
#endif /* MMU */

#ifdef WOLFBOOT_UBOOT_LEGACY
    /* Enter the uImage at ih_ep. Skipped if a later stage (ELF/FIT) re-derived
     * the load address, since that stage provides its own entry point. The
     * flag is tracked explicitly rather than by comparing load_address:
     * elf_load_image_mmu() publishes its entry point before it finishes
     * validating, so a rejected ELF also leaves load_address rewritten. */
    if ((uboot_entry != NULL) && !stage_entry_override) {
        load_address = uboot_entry;
    }
#endif

    wolfBoot_printf("Booting at %p\n", load_address);

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
    (void)hal_hsm_disconnect();
#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
    (void)hal_hsm_server_cleanup();
#endif

#ifdef ENCRYPT_PKCS11
    pkcs11_crypto_deinit();
#endif

#ifndef TZEN
    if (hal_flash_protect(WOLFBOOT_ORIGIN, BOOTLOADER_PARTITION_SIZE) < 0) {
        wolfBoot_printf("Error protecting bootloader flash region\n");
        wolfBoot_panic();
    }
#endif
    hal_prepare_boot();

#ifdef WOLFBOOT_HOOK_BOOT
    wolfBoot_hook_boot(&os_image);
#endif
#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
    PART_SANITY_CHECK(&os_image);
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
    if (bl31_entry != 0) {
        /* Hand off to BL31 (resident EL3 monitor). BL31 starts the kernel
         * (BL33) at EL2; the DTB is forwarded via PMU_GLOBAL scratch (see
         * hal/zynqmp_atf.c). Does not return. */
        wolfBoot_printf("Handing off to BL31 at %p (kernel %p)\n",
            (void*)bl31_entry, (void*)load_address);
        zynqmp_atf_handoff(bl31_entry, (uintptr_t)load_address,
            (uintptr_t)dts_addr, ZYNQMP_ATF_EL2);
    }
#endif
#if defined(MMU) || defined(WOLFBOOT_FDT)
    /* Match the do_boot() signature condition in src/boot_riscv.c. */
    do_boot((uint32_t*)load_address,
            (uint32_t*)dts_addr);
#else
    /* Use load_address instead of os_image.fw_base, which may have
     * wrong base address */
    do_boot((uint32_t*)load_address);
#endif

#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
}
