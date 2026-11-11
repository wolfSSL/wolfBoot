/* fsp_nvs.c
 *
 * Cache of the FSP-M memory training data in SPI flash. Stage 1 hands a
 * valid saved copy back to FspMemInit through NvsBufferPtr so the memory
 * reference code takes its fast path; stage 2 saves the data FSP-M returned
 * in its non-volatile storage HOB whenever it differs from the saved copy.
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

#include <printf.h>
#include <stage2_params.h>
#include <x86/common.h>
#include <x86/hob.h>
#include <x86/fsp/FspCommon.h>
#include <x86/fsp_nvs.h>
#include <x86/tgl_fsp.h>

#if !defined(FSP_NVS_BASE) || !defined(FSP_NVS_SIZE)
#error "FSP_NVS_CACHE needs FSP_NVS_BASE and FSP_NVS_SIZE"
#endif

#define FSP_NVS_SECTOR_SIZE 0x1000U
#if (FSP_NVS_SIZE) == 0 || ((FSP_NVS_SIZE) % FSP_NVS_SECTOR_SIZE) != 0
#error "FSP_NVS_SIZE must be a non-zero multiple of the 4 KB erase size"
#endif
#ifdef BOOTLOADER_PARTITION_SIZE
#if (FSP_NVS_BASE) + (FSP_NVS_SIZE) > 0x100000000ULL - (BOOTLOADER_PARTITION_SIZE)
#error "the FSP_NVS area must lie below the wolfBoot flash window"
#endif
#endif

#ifndef FSP_NVS_CACHE_FLUSH
/* The flash window is cached; drop stale lines before the read-back */
#define FSP_NVS_CACHE_FLUSH() __asm__ volatile ("wbinvd" ::: "memory")
#endif

uint32_t fsp_nvs_crc32(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    int i;

    crc = ~crc;
    while (len-- > 0) {
        crc ^= *buf++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

int fsp_nvs_record_valid(const struct fsp_nvs_record *rec, uint32_t key,
                         uint32_t area_size)
{
    if (area_size <= sizeof(*rec))
        return 0;
    if (rec->magic != FSP_NVS_MAGIC || rec->key != key)
        return 0;
    if (rec->len == 0 || rec->len > area_size - sizeof(*rec))
        return 0;
    return fsp_nvs_crc32(0, rec->data, rec->len) == rec->crc;
}

/* CMOS byte tracking the saved record across boots. Stage 1 sets IN_USE
 * before FspMemInit runs with the record, and stage 2 clears it once
 * FspSiliconInit has succeeded. A boot that finds IN_USE still set crashed or
 * hung with the record, so it sets SKIP: boots then train from scratch, across
 * the resets FSP requests, until FspSiliconInit succeeds and stage 2 has
 * replaced the record. Stage 1 runs from SPI flash, so it cannot erase the
 * record itself. */
#define FSP_NVS_STATE_CMOS_IDX 0x52
#define FSP_NVS_STATE_SKIP 0xA5
#define FSP_NVS_STATE_IN_USE 0x5A

void fsp_nvs_before_reset(void)
{
    /* An FSP-requested reset is part of a normal boot with the record */
    if (cmos_read(FSP_NVS_STATE_CMOS_IDX) == FSP_NVS_STATE_IN_USE)
        cmos_write(FSP_NVS_STATE_CMOS_IDX, 0);
}

void fsp_nvs_boot_ok(void)
{
    cmos_write(FSP_NVS_STATE_CMOS_IDX, 0);
}

#ifdef BUILD_LOADER_STAGE1
void fsp_nvs_reject(void)
{
    cmos_write(FSP_NVS_STATE_CMOS_IDX, FSP_NVS_STATE_SKIP);
}

/* Returns 1 when FspMemInit is given the saved record. */
int fsp_nvs_apply(uint8_t *udp_m, uint32_t udp_size, const uint8_t *fsp_m,
                  uint32_t fsp_m_size)
{
    const struct fsp_nvs_record *rec =
        (const struct fsp_nvs_record *)(uintptr_t)FSP_NVS_BASE;
    FSPM_ARCH_UPD *arch = (FSPM_ARCH_UPD *)(udp_m + sizeof(FSP_UPD_HEADER));
    uint32_t key;
    uint8_t state;

    /* The key is taken before NvsBufferPtr and BootMode are changed, so
     * stage 2 can match it against the UPDs this boot trained with. It
     * covers the whole FSP-M image: different FSP-M builds can report the
     * same revision. */
    key = fsp_nvs_crc32(0, fsp_m, fsp_m_size);
    key = fsp_nvs_crc32(key, udp_m, udp_size);
    stage2_get_parameters()->fsp_nvs_key = key;

    state = cmos_read(FSP_NVS_STATE_CMOS_IDX);
    if (state == FSP_NVS_STATE_IN_USE) {
        wolfBoot_printf("FSP NVS: last boot with saved training data did not "
                        "finish\r\n");
        fsp_nvs_reject();
        state = FSP_NVS_STATE_SKIP;
    }
    if (state == FSP_NVS_STATE_SKIP) {
        wolfBoot_printf("FSP NVS: saved training data failed, retraining\r\n");
        return 0;
    }
    if (!fsp_nvs_record_valid(rec, key, FSP_NVS_SIZE)) {
        wolfBoot_printf("FSP NVS: no saved training data\r\n");
        return 0;
    }
    cmos_write(FSP_NVS_STATE_CMOS_IDX, FSP_NVS_STATE_IN_USE);
    arch->NvsBufferPtr = (uint32_t)(uintptr_t)rec->data;
    arch->BootMode = BOOT_ASSUMING_NO_CONFIGURATION_CHANGES;
    wolfBoot_printf("FSP NVS: using saved training data (%d bytes)\r\n",
                    (int)rec->len);
    return 1;
}
#else
/* FSP_NON_VOLATILE_STORAGE_HOB_GUID */
static const struct efi_guid fsp_nvs_hob_guid = {
    0x721acf02, 0x4d77, 0x4c2a,
    { 0xb3, 0xdc, 0x27, 0x0b, 0x7b, 0xa9, 0xe4, 0xb0 }
};

static struct efi_hob_guid_type *fsp_nvs_find_hob(struct efi_hob *hob)
{
    while (hob != NULL &&
           hob_get_type(hob) != EFI_HOB_TYPE_END_OF_HOB_LIST) {
        if (hob_get_length(hob) < sizeof(struct efi_hob_generic_header))
            return NULL;
        if (hob_get_type(hob) == EFI_HOB_TYPE_GUID_EXTENSION &&
            hob_get_length(hob) > sizeof(struct efi_hob_guid_type) &&
            hob_guid_equals(&hob->u.guid.name,
                            (struct efi_guid *)&fsp_nvs_hob_guid))
            return &hob->u.guid;
        hob = hob_get_next(hob);
    }
    return NULL;
}

static int fsp_nvs_is_current(const struct fsp_nvs_record *rec, uint32_t key,
                              const uint8_t *data, uint32_t len)
{
    return fsp_nvs_record_valid(rec, key, FSP_NVS_SIZE) && rec->len == len &&
           memcmp(rec->data, data, len) == 0;
}

void fsp_nvs_save(void)
{
    const struct fsp_nvs_record *rec =
        (const struct fsp_nvs_record *)(uintptr_t)FSP_NVS_BASE;
    struct stage2_parameter *params = stage2_get_parameters();
    struct efi_hob_guid_type *hob;
    struct fsp_nvs_record hdr;
    const uint8_t *data;
    uint32_t len, erase_len;

    hob = fsp_nvs_find_hob((struct efi_hob *)(uintptr_t)params->hobList);
    if (hob == NULL) {
        wolfBoot_printf("FSP NVS: no training data HOB\r\n");
        return;
    }
    data = (const uint8_t *)(hob + 1);
    len = hob->header.hob_length - sizeof(*hob);
    if (len > FSP_NVS_SIZE - sizeof(hdr)) {
        wolfBoot_printf("FSP NVS: training data (%d bytes) exceeds the cache\r\n",
                        (int)len);
        return;
    }
    if (fsp_nvs_is_current(rec, params->fsp_nvs_key, data, len))
        return;

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = FSP_NVS_MAGIC;
    hdr.key = params->fsp_nvs_key;
    hdr.len = len;
    hdr.crc = fsp_nvs_crc32(0, data, len);
    erase_len = (sizeof(hdr) + len + FSP_NVS_SECTOR_SIZE - 1) &
                ~(FSP_NVS_SECTOR_SIZE - 1);
    /* The header goes last, so an interrupted save leaves no valid record */
    if (tgl_spi_flash_erase(FSP_NVS_BASE, erase_len) != 0 ||
        tgl_spi_flash_write(FSP_NVS_BASE + sizeof(hdr), data, len) != 0 ||
        tgl_spi_flash_write(FSP_NVS_BASE, (const uint8_t *)&hdr,
                            sizeof(hdr)) != 0) {
        wolfBoot_printf("FSP NVS: saving training data failed\r\n");
        return;
    }
    FSP_NVS_CACHE_FLUSH();
    if (!fsp_nvs_is_current(rec, hdr.key, data, len)) {
        wolfBoot_printf("FSP NVS: training data read-back mismatch\r\n");
        return;
    }
    wolfBoot_printf("FSP NVS: saved %d bytes of training data\r\n", (int)len);
}
#endif /* BUILD_LOADER_STAGE1 */
