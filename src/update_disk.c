/* update_disk.c
 *
 * Implementation for RAM based updater, for systems that provide
 * drives and partition mapping.
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
/**
 * @file update_disk.c
 *
 * @brief Implementation for RAM based updater, for systems that provide
 * drives and partition mapping.
 *
 * This file contains the implementation for the RAM-based updater for systems
 * that offer drives and partition mapping. It includes functions to read
 * and load OS images from disk partitions, validate their integrity and
 * authenticity, and perform the boot process.
 */

#ifdef WOLFBOOT_UPDATE_DISK

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "hooks.h"
#include "spi_flash.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"
#include "disk.h"
#ifdef WOLFBOOT_DISK_FS
#include "disk_fs.h"
#endif
#ifdef WOLFBOOT_ELF
#include "elf.h"
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
#include "../hal/zynqmp_atf.h"
#endif

/* Disk encryption support for AES-256, AES-128, or ChaCha20 */
#if defined(ENCRYPT_WITH_AES256) || defined(ENCRYPT_WITH_AES128) || \
    defined(ENCRYPT_WITH_CHACHA)
#define DISK_ENCRYPT
#include "encrypt.h"
#include <wolfssl/wolfcrypt/memory.h> /* wc_ForceZero */

/* Module-level storage for encryption nonce */
static uint8_t disk_encrypt_nonce[ENCRYPT_NONCE_SIZE];
#endif

#include <stdint.h>
#include <string.h>

#ifdef WOLFBOOT_FSP
#include "stage2_params.h"
#include "x86/common.h"
#include "x86/ahci.h"
#include "x86/ata.h"
#include "pci.h"
#include "x86/tgl_fsp.h"

#ifdef TARGET_kontron_vx3060_s2
    #define BOOT_PART_A 5
    #define BOOT_PART_B 6
#endif
#endif /* WOLFBOOT_FSP */

/* Default values for BOOT_DISK, BOOT_PART_A and BOOT_PART_B */
#ifndef BOOT_DISK
#define BOOT_DISK 0
#endif
#ifndef BOOT_PART_A
#define BOOT_PART_A 0
#endif
#ifndef BOOT_PART_B
#define BOOT_PART_B 1
#endif

/* Optional read-only filesystem support. When a slot's partition holds a
 * supported filesystem, the signed image is read from BOOT_FILE_x instead
 * of from the start of the partition. A partition that holds no
 * filesystem is read exactly as before. */
#ifndef BOOT_FILE_A
#define BOOT_FILE_A NULL
#endif
#ifndef BOOT_FILE_B
#define BOOT_FILE_B NULL
#endif
/* Optional partition selection by name, overriding BOOT_PART_x. Matches
 * the GPT partition label first, then the filesystem volume label. */
#ifndef BOOT_LABEL_A
#define BOOT_LABEL_A NULL
#endif
#ifndef BOOT_LABEL_B
#define BOOT_LABEL_B NULL
#endif

#ifndef MAX_FAILURES
#define MAX_FAILURES 4
#endif

#ifndef DISK_BLOCK_SIZE
#define DISK_BLOCK_SIZE 512
#endif

#if !defined(WOLFBOOT_FSP) && !defined(WOLFBOOT_RAMBOOT_MAX_SIZE)
#  error "WOLFBOOT_RAMBOOT_MAX_SIZE required to bound the disk image RAM load"
#endif

#ifdef DISK_ENCRYPT

/* Module-level storage for encryption key */
static uint8_t disk_encrypt_key[ENCRYPT_KEY_SIZE];

static uint16_t get_hdr_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_hdr_u32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * @brief Get the version from an already-decrypted header.
 *
 * This function extracts the version from a decrypted header blob
 * without calling wolfBoot_get_blob_version, which might try to
 * decrypt again if EXT_ENCRYPTED && MMU is defined.
 *
 * @param hdr Pointer to the decrypted header.
 *
 * @return The version number, or 0 if not found.
 */
static uint32_t get_decrypted_blob_version(uint8_t *hdr)
{
    uint32_t *magic = (uint32_t *)hdr;
    uint8_t *p = hdr + IMAGE_HEADER_OFFSET;
    uint8_t *max_p = hdr + IMAGE_HEADER_SIZE;

    if (*magic != WOLFBOOT_MAGIC)
        return 0;

    /* Search for version TLV */
    while ((size_t)(max_p - p) >= 4U) {
        size_t remaining = (size_t)(max_p - p);
        size_t tlv_total;
        uint16_t tlv_type, tlv_len;

        /* Skip padding bytes and unaligned half-words before reading TLVs. */
        if ((p[0] == HDR_PADDING) || ((((uintptr_t)p) & 0x01U) != 0U)) {
            p++;
            continue;
        }

        tlv_type = get_hdr_u16(p);
        if (tlv_type == 0 || tlv_type == 0xFFFF)
            break;

        tlv_len = get_hdr_u16(p + 2);
        tlv_total = 4U + (size_t)tlv_len;
        if (remaining < tlv_total)
            break;

        if (tlv_type == HDR_VERSION && tlv_len == 4) {
            return get_hdr_u32(p + 4);
        }

        p += tlv_total;
    }
    return 0;
}

/**
 * @brief Set up decryption context with IV at specified block offset.
 *
 * This function sets up the AES/ChaCha context with the IV positioned
 * at the specified block offset. It matches how sign.c sets up encryption.
 *
 * @param block_offset Block offset for IV counter (0 = start of image).
 */
static void disk_crypto_set_iv(uint32_t block_offset)
{
#if defined(ENCRYPT_WITH_CHACHA)
    wc_Chacha_SetIV(&chacha, disk_encrypt_nonce, block_offset);
#elif defined(ENCRYPT_WITH_AES128) || defined(ENCRYPT_WITH_AES256)
    /* For AES CTR, we need to construct the IV with the counter.
     * The sign tool uses the IV directly without byte-reversal,
     * so we must match that behavior here. */
    uint8_t iv[ENCRYPT_BLOCK_SIZE];
    uint32_t ctr;

    /* Copy nonce/IV (first 12 bytes for CTR nonce, last 4 for counter) */
    memcpy(iv, disk_encrypt_nonce, ENCRYPT_NONCE_SIZE);

    /* Add block offset to the counter portion (last 4 bytes, big-endian) */
    /* The IV from sign.c is already in the correct format, we just need
     * to add the block offset to the counter portion */
    ctr = ((uint32_t)iv[12] << 24) | ((uint32_t)iv[13] << 16) |
          ((uint32_t)iv[14] << 8) | (uint32_t)iv[15];
    ctr += block_offset;
    iv[12] = (uint8_t)(ctr >> 24);
    iv[13] = (uint8_t)(ctr >> 16);
    iv[14] = (uint8_t)(ctr >> 8);
    iv[15] = (uint8_t)(ctr);

    wc_AesSetIV(&aes_dec, iv);
#endif
}

/**
 * @brief Decrypt an image header in RAM.
 *
 * This function decrypts the image header using the configured encryption
 * algorithm (AES-256/AES-128 CTR mode or ChaCha20).
 *
 * @param src Pointer to the encrypted header.
 * @param dst Pointer to the destination buffer for decrypted header.
 *
 * @return 0 if successful, -1 on failure.
 */
static int decrypt_header(const uint8_t *src, uint8_t *dst)
{
    uint32_t magic;

    /* Reset IV to start of image (block 0) */
    disk_crypto_set_iv(0);

    /* Decrypt header - CTR mode handles counter increment internally */
    crypto_decrypt(dst, src, IMAGE_HEADER_SIZE);

    magic = *((uint32_t*)dst);
    if (magic != WOLFBOOT_MAGIC)
        return -1;
    return 0;
}

static void disk_crypto_clear(void)
{
    wc_ForceZero(disk_encrypt_key, sizeof(disk_encrypt_key));
    wc_ForceZero(disk_encrypt_nonce, sizeof(disk_encrypt_nonce));
}

static void disk_decrypted_header_clear(uint8_t *hdr)
{
    wc_ForceZero(hdr, IMAGE_HEADER_SIZE);
}

#endif /* DISK_ENCRYPT */

extern int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity);
#ifdef MMU
/* Platform hook: return a DTB the boot firmware handed us (e.g. the RPi
 * firmware's fully-patched dtb), used below when the loaded image carries no
 * FDT of its own. Weak default returns none; platform HALs (e.g. hal/cm4.c)
 * override it. Defined here (not in an arch boot_*.c) so every MMU disk target -
 * AArch64, ARM32, RISC-V - resolves the symbol. */
void* WEAKFUNCTION hal_get_boot_dts(void)
{
    return NULL;
}

/* Weak default: no A/B boot-slot bookkeeping. Platform HALs (e.g. hal/cm4.c's
 * RAUC path) override it. Defined here so every MMU disk target resolves the
 * symbol. */
int WEAKFUNCTION hal_boot_slot_select(void)
{
    return 0;
}
#endif

#if defined(WOLFBOOT_NO_LOAD_ADDRESS) || !defined(WOLFBOOT_LOAD_ADDRESS)
/* from the linker, where wolfBoot ends */
extern uint8_t _end_wb[];
#endif

/**
 * @brief One A/B boot slot: a partition, and optionally a file on it.
 *
 * The whole point of this indirection is that the retry loop below reads
 * through slot_read() and so is written once, whether the image lives at
 * the start of a partition or in a file on a filesystem.
 */
struct boot_slot {
    int part;
    int ready;
#ifdef WOLFBOOT_DISK_FS
    const char *file;
    struct fs_volume vol;
    struct fs_file f;
#endif
};

/* File scope, not stack: several disk targets build with
 * WOLFBOOT_SMALL_STACK=1. */
static struct boot_slot boot_slots[2];

/**
 * @brief Read from a boot slot.
 *
 * With WOLFBOOT_DISK_FS undefined, or on a partition that holds no
 * filesystem, this is exactly the disk_part_read() call the loader has
 * always made.
 */
static int slot_read(struct boot_slot *s, uint64_t off, uint64_t sz,
                     uint8_t *buf)
{
    if (s->ready == 0) {
        return -1;
    }
#ifdef WOLFBOOT_DISK_FS
    if (s->vol.type != FS_TYPE_RAW) {
        return fs_read(&s->f, off, sz, buf);
    }
#endif
    return disk_part_read(BOOT_DISK, s->part, off, sz, buf);
}

#ifdef WOLFBOOT_DISK_FS
/**
 * @brief Find a partition by name.
 *
 * The GPT partition label is tried first. MBR disks have no partition
 * names at all -- src/disk.c zeroes that field on the MBR path -- so the
 * filesystem's own volume label is tried next, which is what makes name
 * based selection usable on the MBR layouts the SD card targets use.
 */
/* Not on the stack: this runs on the same WOLFBOOT_SMALL_STACK disk
 * targets that boot_slots and the filesystem metadata cache are kept off
 * the stack for. */
static struct fs_volume slot_label_probe;

static int slot_find_by_label(const char *label)
{
    int n;
    int i;

    i = disk_find_partition_by_label(BOOT_DISK, label);
    if (i >= 0) {
        return i;
    }
    n = disk_part_count(BOOT_DISK);
    for (i = 0; i < n; i++) {
        if (fs_mount(&slot_label_probe, BOOT_DISK, i) != WOLFBOOT_FS_OK) {
            continue;
        }
        if (fs_label_eq(&slot_label_probe, label) == 1) {
            return i;
        }
    }
    return -1;
}
#endif /* WOLFBOOT_DISK_FS */

/**
 * @brief Resolve, mount and open one boot slot.
 *
 * @param max_size Upper bound on the image size, applied to the size the
 *                 filesystem reports before it can drive any I/O. The
 *                 payload lands in RAM before its signature is checked,
 *                 so this bound has to come from the build, not the media.
 *
 * @return 0 when the slot can be read from, -1 otherwise.
 */
static int slot_prepare(struct boot_slot *s, int part, const char *label,
                        const char *file, uint64_t max_size)
{
#ifdef WOLFBOOT_DISK_FS
    int ret;
#endif

    memset(s, 0, sizeof(*s));
    s->part = part;

#ifdef WOLFBOOT_DISK_FS
    s->file = file;
    if (label != NULL) {
        s->part = slot_find_by_label(label);
        if (s->part < 0) {
            wolfBoot_printf("No partition named %s\r\n", label);
            return -1;
        }
    }
    ret = fs_mount(&s->vol, BOOT_DISK, s->part);
    if (ret != WOLFBOOT_FS_OK) {
        /* Neutral wording: fs_mount() also reports E_IO for a partition that
         * does not exist or cannot be read, which a mis-set BOOT_PART_x or
         * BOOT_LABEL_x produces. The code distinguishes the cases. */
        wolfBoot_printf("p%d: cannot mount (%d)\r\n", s->part, ret);
        return -1;
    }
    if (s->vol.type == FS_TYPE_RAW) {
        if (file != NULL) {
            wolfBoot_printf("p%d: no filesystem, reading raw\r\n", s->part);
        }
    }
    else if (file == NULL) {
        /* Falling back to a raw read here would parse the filesystem's own
         * boot sector as an image header. Say why instead. */
        wolfBoot_printf("p%d: %s filesystem but no boot file set\r\n",
            s->part, fs_type_name(&s->vol));
        return -1;
    }
    ret = fs_open(&s->vol, &s->f, file, max_size);
    if (ret != WOLFBOOT_FS_OK) {
        wolfBoot_printf("p%d: cannot open %s (%d)\r\n", s->part,
            (file != NULL) ? file : "image", ret);
        return -1;
    }
    if (s->vol.type != FS_TYPE_RAW) {
        wolfBoot_printf("p%d: %s, %s\r\n", s->part, fs_type_name(&s->vol),
            file);
    }
#else
    (void)label;
    (void)file;
    (void)max_size;
#endif

    s->ready = 1;
    return 0;
}

/**
 * @brief function for starting the boot process.
 *
 * This function starts the boot process by attempting to read and load
 * the OS image from disk partitions. It then verifies the integrity and
 * authenticity of the loaded image before initiating the boot.
 */
void RAMFUNCTION wolfBoot_start(void)
{
    uint8_t p_hdr[IMAGE_HEADER_SIZE] XALIGNED_STACK(16);
#ifdef DISK_ENCRYPT
    uint8_t dec_hdr[IMAGE_HEADER_SIZE] XALIGNED_STACK(16);
#endif
#ifdef WOLFBOOT_FSP
    struct stage2_parameter *stage2_params;
#endif
    struct wolfBoot_image os_image;
    struct boot_slot *slot;
    uint64_t slot_max;
    uint32_t pA_ver = 0U, pB_ver = 0U;
    uint32_t pA_ver_u = 0U, pB_ver_u = 0U;
    uint32_t cur_part = 0;
    int ret = -1;
    int selected;
    uint32_t *load_address;
    int failures = 0;
    uint32_t load_off;
    uint32_t max_ver;
    const uint8_t *hdr_ptr = NULL;
#if defined(MMU) || defined(WOLFBOOT_FDT)
    uint8_t *dts_addr = NULL;
    #ifdef WOLFBOOT_FDT
    uint32_t dts_size = 0;
    /* Validated view of the FIT staged at load_address. */
    fdt_ctx  fit_ctx;
    #endif
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
    /* BL31 (ARM-TF) entry, set when the boot FIT carries an "atf" sub-image. */
    uintptr_t bl31_entry = 0;
#endif
    char part_name[4] = {'P', ':', 'X', '\0'};
    BENCHMARK_DECLARE();

#ifdef DISK_ENCRYPT
    /* Initialize encryption - this sets up the cipher with key from storage */
    if (wolfBoot_initialize_encryption() != 0) {
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
        wolfBoot_printf("Error initializing encryption\r\n");
        wolfBoot_panic();
    }
    /* Retrieve encryption key and nonce for disk decryption */
    if (wolfBoot_get_encrypt_key(disk_encrypt_key, disk_encrypt_nonce) != 0) {
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
        wolfBoot_printf("Error getting encryption key\r\n");
        wolfBoot_panic();
    }
    wolfBoot_printf("Disk encryption enabled\r\n");
#endif

    ret = disk_init(BOOT_DISK);
    if (ret != 0) {
#ifdef DISK_ENCRYPT
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
#endif
        wolfBoot_panic();
    }

    /* (Removed) DDR self-test that PDMA/CPU-wrote 0xA5A5/0x5A5A patterns
     * into the image load region (0x82000000 / 0xC2000000).  It was a
     * debug aid for the now-fixed DDR write scramble (auto-init reorder
     * in run_training), and it pre-clobbered the first 64 words of the
     * load region, corrupting the image the integrity check then read. */

    if (disk_open(BOOT_DISK) < 0) {
#ifdef DISK_ENCRYPT
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
#endif
        wolfBoot_printf("Error opening disk %d\r\n", BOOT_DISK);
        wolfBoot_panic();
    }

#ifdef WOLFBOOT_FSP
    stage2_params = stage2_get_parameters();
#endif

#if !defined(WOLFBOOT_NO_LOAD_ADDRESS) && defined(WOLFBOOT_LOAD_ADDRESS)
    load_address = (uint32_t*)WOLFBOOT_LOAD_ADDRESS;
#else
    /* load the image just after wolfboot, 16 bytes aligned */
    load_address = (uint32_t *)((((uintptr_t)_end_wb) + 0xf) & ~0xf);
#endif

    wolfBoot_printf("Load address 0x%x\r\n", load_address);

    /* Upper bound on anything the media may claim about the image size.
     * The payload is copied into the load region before its signature is
     * checked, so this has to come from the build rather than the media.
     * The header sits ahead of the payload in the same file, hence the
     * IMAGE_HEADER_SIZE. */
#if defined(WOLFBOOT_FSP)
    /* Fail closed on an inverted tolum: the subtraction would otherwise wrap
     * to a near-2^64 bound, which is the opposite of a cap. */
    if ((uintptr_t)(stage2_params->tolum) > (uintptr_t)load_address) {
        slot_max = (uint64_t)(uintptr_t)(stage2_params->tolum) -
                   (uint64_t)(uintptr_t)load_address;
    }
    else {
        slot_max = 0;
    }
#else
    slot_max = (uint64_t)WOLFBOOT_RAMBOOT_MAX_SIZE +
               (uint64_t)IMAGE_HEADER_SIZE;
#endif

    (void)slot_prepare(&boot_slots[0], BOOT_PART_A, BOOT_LABEL_A,
        BOOT_FILE_A, slot_max);
    (void)slot_prepare(&boot_slots[1], BOOT_PART_B, BOOT_LABEL_B,
        BOOT_FILE_B, slot_max);

    wolfBoot_printf("Checking primary OS image in %d,%d...\r\n", BOOT_DISK,
            boot_slots[0].part);
    if (slot_read(&boot_slots[0], 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
#ifdef DISK_ENCRYPT
        if (decrypt_header(p_hdr, dec_hdr) == 0) {
            /* Use local version parser to avoid double-decryption issue
             * when EXT_ENCRYPTED && MMU is also defined */
            pA_ver = get_decrypted_blob_version(dec_hdr);
        }
#else
        pA_ver = wolfBoot_get_blob_version((uint8_t*)p_hdr);
#endif
    }

    wolfBoot_printf("Checking secondary OS image in %d,%d...\r\n", BOOT_DISK,
            boot_slots[1].part);
    if (slot_read(&boot_slots[1], 0, IMAGE_HEADER_SIZE, p_hdr)
            == IMAGE_HEADER_SIZE) {
#ifdef DISK_ENCRYPT
        if (decrypt_header(p_hdr, dec_hdr) == 0) {
            /* Use local version parser to avoid double-decryption issue
             * when EXT_ENCRYPTED && MMU is also defined */
            pB_ver = get_decrypted_blob_version(dec_hdr);
        }
#else
        pB_ver = wolfBoot_get_blob_version((uint8_t*)p_hdr);
#endif
    }

    if ((pB_ver == 0) && (pA_ver == 0)) {
#ifdef DISK_ENCRYPT
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
#endif
        wolfBoot_printf("No valid OS image found in either partition %d or %d\r\n",
            boot_slots[0].part, boot_slots[1].part);
        wolfBoot_panic();
    }

    if (pA_ver != 0U)
        pA_ver_u = pA_ver;
    if (pB_ver != 0U)
        pB_ver_u = pB_ver;

    wolfBoot_printf("Versions, A:%u B:%u\r\n", pA_ver_u, pB_ver_u);
    wolfBoot_printf("Load block size: %dKB\r\n", DISK_BLOCK_SIZE / 1024);
    max_ver = (pB_ver_u > pA_ver_u) ? pB_ver_u : pA_ver_u;

    /* Choose partition with higher version */
    selected = (pB_ver_u > pA_ver_u) ? 1 : 0;

    do {
        failures++;
        slot = &boot_slots[selected];
        cur_part = (uint32_t)slot->part;
#ifndef ALLOW_DOWNGRADE
        {
            uint32_t cur_ver = selected ? pB_ver_u : pA_ver_u;
            if ((max_ver > 0U) && (cur_ver < max_ver)) {
                wolfBoot_printf("Rollback to lower version not allowed\r\n");
#ifdef DISK_ENCRYPT
                disk_decrypted_header_clear(dec_hdr);
                disk_crypto_clear();
#endif
                wolfBoot_panic();
                return;
            }
        }
#endif

        part_name[2] = 'A' + selected;

        wolfBoot_printf("Attempting boot from %s\r\n", part_name);

        /* Fetch header only */
        if (slot_read(slot, 0, IMAGE_HEADER_SIZE, p_hdr)
            != IMAGE_HEADER_SIZE) {
            wolfBoot_printf("Error reading image header from disk: p%d\r\n",
                    cur_part);
            selected ^= 1;
            continue;
        }

        hdr_ptr = p_hdr;
#ifdef DISK_ENCRYPT
        /* Decrypt header to parse image size */
        if (decrypt_header(p_hdr, dec_hdr) != 0) {
            wolfBoot_printf("Error decrypting header for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
        hdr_ptr = dec_hdr;
#endif
        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)hdr_ptr);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }

        /* Bound the UNAUTHENTICATED image length before it drives the disk
         * read into the RAM load region. An attacker controlling the boot
         * media could otherwise declare an arbitrary fw_size and overrun the
         * load region before any integrity or signature check runs. Mirrors
         * the cap update_ram.c applies to RAMBOOT loads; on FSP the tolum
         * check below applies in addition (whichever is tighter wins). */
#ifdef WOLFBOOT_DISK_FS
        /* A file too short to hold what its header declares is a truncated
         * image. Failing the slot here reports the real reason, instead of
         * the short read further down that looks like an I/O error. */
        if ((slot->vol.type != FS_TYPE_RAW) &&
                (((uint64_t)os_image.fw_size + (uint64_t)IMAGE_HEADER_SIZE) >
                    fs_size(&slot->f))) {
            wolfBoot_printf("Image larger than file for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
#endif

#ifdef WOLFBOOT_RAMBOOT_MAX_SIZE
        if (os_image.fw_size > WOLFBOOT_RAMBOOT_MAX_SIZE) {
            wolfBoot_printf("Image size %u exceeds max RAM load size\r\n",
                os_image.fw_size);
            selected ^= 1;
            continue;
        }
#endif

#ifdef WOLFBOOT_FSP
        /* Verify image size fits in low memory */
        if (os_image.fw_size > ((uint32_t)(stage2_params->tolum) -
                                           (uint32_t)(uintptr_t)load_address)) {
            wolfBoot_printf("Image size %u doesn't fit in low memory\r\n",
                os_image.fw_size);
            selected ^= 1;
            continue;
        }
        /* Log memory load */
        x86_log_memory_load((uint32_t)(uintptr_t)load_address,
                            (uint32_t)(uintptr_t)load_address + os_image.fw_size,
                            part_name);
#endif

        /* Read the payload into RAM (skip header) */
        wolfBoot_printf("Loading image from disk...");
        BENCHMARK_START();
        load_off = 0;
        do {
            uint32_t chunk = os_image.fw_size - load_off;
            if (chunk > DISK_BLOCK_SIZE)
                chunk = DISK_BLOCK_SIZE;
            ret = slot_read(slot, IMAGE_HEADER_SIZE + load_off, chunk,
                ((uint8_t *)load_address) + load_off);
            if (ret <= 0)
                break;
            load_off += ret;
        } while (load_off < os_image.fw_size);

        /* A short read must fail here, as an I/O error. `ret == 0` breaks the
         * loop above without being negative, and a truncated load would
         * otherwise sail through to the integrity check and be reported as a
         * corrupt image -- pointing the operator at the wrong problem, and on
         * a system with anti-rollback leaving no bootable slot at all. */
        if (ret <= 0 || load_off != os_image.fw_size) {
            wolfBoot_printf("Error reading image from disk: p%d "
                    "(%u of %u bytes)\r\n", cur_part,
                    (unsigned int)load_off, (unsigned int)os_image.fw_size);
            selected ^= 1;
            continue;
        }
        BENCHMARK_END("done");

#ifdef DISK_ENCRYPT
        /* Decrypt the payload in RAM */
        wolfBoot_printf("Decrypting image...");
        BENCHMARK_START();
        if ((IMAGE_HEADER_SIZE % ENCRYPT_BLOCK_SIZE) != 0) {
            disk_decrypted_header_clear(dec_hdr);
            disk_crypto_clear();
            wolfBoot_printf("Encrypted disk images require aligned header size\r\n");
            wolfBoot_panic();
        }
        disk_crypto_set_iv(IMAGE_HEADER_SIZE / ENCRYPT_BLOCK_SIZE);
        crypto_decrypt((uint8_t*)load_address, (uint8_t*)load_address,
            os_image.fw_size);
        BENCHMARK_END("done");
#endif

        memset(&os_image, 0, sizeof(os_image));
        ret = wolfBoot_open_image_address(&os_image, (void*)hdr_ptr);
        if (ret < 0) {
            wolfBoot_printf("Error parsing loaded image\r\n");
            selected ^= 1;
            continue;
        }
        os_image.fw_base = (uint8_t*)load_address;

#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
        wolfBoot_printf("Checking image integrity...");
        BENCHMARK_START();
        if (wolfBoot_verify_integrity(&os_image) != 0) {
            wolfBoot_printf("Error validating integrity for %s\r\n", part_name);
            selected ^= 1;
            continue;
        }
        BENCHMARK_END("done");

        wolfBoot_printf("Verifying image signature...");
        BENCHMARK_START();
        if (wolfBoot_verify_authenticity(&os_image) != 0) {
            wolfBoot_printf("Error validating authenticity for %s\r\n",
                part_name);
            selected ^= 1;
            continue;
        } else {
            BENCHMARK_END("done");
            failures = 0;
            break; /* Success case */
        }
#else
        failures = 0;
        break; /* Skip verification, boot directly */
#endif
    } while (failures < MAX_FAILURES);

    if (failures) {
#ifdef DISK_ENCRYPT
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
#endif
        wolfBoot_printf("Unable to find a valid partition!\r\n");
        wolfBoot_panic();
        return;
    }

    /* NOTE: the boot disk is intentionally kept open here. A/B boot-slot
     * bookkeeping (hal_boot_slot_select) and the firmware-DTB / RAUC bootargs
     * path (hal_get_boot_dts) below still need to read/write the env partition;
     * disk_close(BOOT_DISK) is deferred to just before hal_prepare_boot(). */
    wolfBoot_printf("Firmware Valid.\r\n");

    load_address = (uint32_t*)os_image.fw_base;

#ifdef WOLFBOOT_FDT
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
                wolfBoot_printf("FIT: FPGA load failed\r\n");
#ifdef DISK_ENCRYPT
                disk_decrypted_header_clear(dec_hdr);
                disk_crypto_clear();
#endif
                wolfBoot_panic();
            }
        }
#else
        (void)fpga;
#endif
        if (kernel != NULL) {
            void *new_load = fit_load_image(fit, kernel, NULL);
            if (new_load == NULL) {
                wolfBoot_printf("FIT: failed to load kernel '%s'\r\n",
                    kernel);
#ifdef DISK_ENCRYPT
                disk_decrypted_header_clear(dec_hdr);
                disk_crypto_clear();
#endif
                wolfBoot_panic();
            }
            load_address = new_load;
        }
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
        /* Load BL31 (ARM-TF) from the FIT "atf" sub-image, if present. */
        atf_load = fit_load_image(fit, "atf", NULL);
        if (atf_load != NULL) {
            bl31_entry = (uintptr_t)atf_load;
            wolfBoot_printf("FIT: BL31 (atf) loaded at %p\r\n", atf_load);
        }
#endif
        if (flat_dt != NULL) {
            int dt_len = 0;
            uint8_t *dts_ptr = fit_load_image(fit, flat_dt, &dt_len);
            /* Bound the parse by the sub-image's own declared length,
             * the tightest bound available here. */
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
                if (wolfBoot_fit_memcpy(dts_addr, dts_ptr, dts_size) != 0) {
                    wolfBoot_printf("FIT: failed to load DTS\r\n");
#ifdef DISK_ENCRYPT
                    disk_decrypted_header_clear(dec_hdr);
                    disk_crypto_clear();
#endif
                    wolfBoot_panic();
                }
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
#endif

#if defined(WOLFBOOT_ELF) && !defined(WOLFBOOT_FSP)
    /* Load elf sections and return the new entry point */
    /* Skip for FSP, since it expects ELF image directly */
    if (elf_load_image_mmu((uint8_t*)load_address, os_image.fw_size,
            (uintptr_t*)&load_address, NULL) != 0){
        wolfBoot_printf("Invalid elf, falling back to raw binary\n");
    }
#endif

    wolfBoot_printf("Booting at %08lx\r\n", load_address);

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
        wolfBoot_printf("Error protecting bootloader flash region\r\n");
#ifdef DISK_ENCRYPT
        disk_decrypted_header_clear(dec_hdr);
        disk_crypto_clear();
#endif
        wolfBoot_panic();
    }
#endif
#ifdef MMU
    /* If the loaded image carried no FDT (e.g. a kernel-only FIT), fall back to
     * a DTB the boot firmware handed us. Done BEFORE hal_prepare_boot() so any
     * FDT relocation/fixup runs while the MMU/caches are still enabled (some
     * platforms, e.g. CM4, disable the MMU in hal_prepare_boot() and FDT edits
     * would then fault on Device-nGnRnE memory). */
    /* Run A/B boot-slot bookkeeping unconditionally (not gated on dts_addr), so
     * failover works whether or not the FIT embedded its own fdt. */
    (void)hal_boot_slot_select();
    if (dts_addr == NULL) {
        dts_addr = (uint8_t*)hal_get_boot_dts();
    }
#endif
    /* Deferred from just after verification (see NOTE above): close the boot
     * disk now that all env / DTB reads and writes are done, before handoff. */
    disk_close(BOOT_DISK);
    hal_prepare_boot();

#ifdef WOLFBOOT_HOOK_BOOT
    wolfBoot_hook_boot(&os_image);
#endif
#ifndef WOLFBOOT_SKIP_BOOT_VERIFY
    PART_SANITY_CHECK(&os_image);
#endif
#ifdef DISK_ENCRYPT
    disk_decrypted_header_clear(dec_hdr);
    disk_crypto_clear();
#endif
#if defined(WOLFBOOT_ZYNQMP_FSBL) && defined(MMU)
    if (bl31_entry != 0) {
        wolfBoot_printf("Handing off to BL31 at %p (kernel %p)\r\n",
            (void*)bl31_entry, (void*)load_address);
        zynqmp_atf_handoff(bl31_entry, (uintptr_t)load_address,
            (uintptr_t)dts_addr, ZYNQMP_ATF_EL2);
    }
#endif
    do_boot((uint32_t*)load_address
    #if defined(MMU) || defined(WOLFBOOT_FDT)
        ,(uint32_t*)dts_addr
    #endif
    );
}
#endif /* WOLFBOOT_UPDATE_DISK */
