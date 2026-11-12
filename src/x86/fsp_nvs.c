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

#ifndef BUILD_LOADER_STAGE1
/* set when a save left a record in flash that is not this boot's data */
static int fsp_nvs_stale;
#endif

void fsp_nvs_boot_ok(void)
{
#ifndef BUILD_LOADER_STAGE1
    /* the record stage 1 skipped is still there: keep skipping it */
    if (fsp_nvs_stale &&
        cmos_read(FSP_NVS_STATE_CMOS_IDX) == FSP_NVS_STATE_SKIP)
        return;
#endif
    cmos_write(FSP_NVS_STATE_CMOS_IDX, 0);
}

#ifdef WOLFBOOT_FSP_NVS_TPM
#include <wolfssl/wolfcrypt/sha256.h>

/* TPM NV index holding the SHA-256 of the record stage 2 last saved. It is
 * created by the platform hierarchy, which wolfBoot disables before handoff,
 * so only wolfBoot can write it. */
#ifndef FSP_NVS_TPM_NV_INDEX
#define FSP_NVS_TPM_NV_INDEX 0x01400410U
#endif
#define FSP_NVS_HASH_SIZE 32

/* Only an index the platform hierarchy created, and that it alone can
 * write, holds a hash wolfBoot stored. TPMA_NV bits: PPWRITE, OWNERWRITE,
 * AUTHWRITE, POLICYWRITE and PLATFORMCREATE. */
#define FSP_NVS_NV_ATTR_MASK 0x4000000FU
#define FSP_NVS_NV_ATTR 0x40000001U

static int fsp_nvs_record_hash(const struct fsp_nvs_record *rec, uint8_t *hash)
{
    wc_Sha256 sha;
    int ret;

    ret = wc_InitSha256(&sha);
    if (ret != 0)
        return ret;
    ret = wc_Sha256Update(&sha, (const uint8_t *)rec, sizeof(*rec) + rec->len);
    if (ret == 0)
        ret = wc_Sha256Final(&sha, hash);
    wc_Sha256Free(&sha);
    return ret;
}
#endif /* WOLFBOOT_FSP_NVS_TPM */

#ifdef BUILD_LOADER_STAGE1
#ifdef WOLFBOOT_FSP_NVS_TPM
/* Stage 1 has no writable data before memory is up, so wolfTPM cannot run
 * yet. Read the reference hash with a minimal FIFO exchange that keeps all
 * of its state on the stack. */
#ifndef FSP_NVS_TPM_BASE
#define FSP_NVS_TPM_BASE 0xFED40000U
#endif
#define TIS_ACCESS (FSP_NVS_TPM_BASE + 0x00)
#define TIS_STS (FSP_NVS_TPM_BASE + 0x18)
#define TIS_FIFO (FSP_NVS_TPM_BASE + 0x24)
#define TIS_ACCESS_VALID 0x80
#define TIS_ACCESS_ACTIVE 0x20
#define TIS_ACCESS_REQUEST 0x02
#define TIS_STS_VALID 0x80
#define TIS_STS_READY 0x40
#define TIS_STS_GO 0x20
#define TIS_STS_AVAIL 0x10
#define TIS_POLL_MAX 2000000U

static int tis_wait_sts(uint8_t mask)
{
    uint32_t i;

    for (i = 0; i < TIS_POLL_MAX; i++) {
        if ((mmio_read8(TIS_STS) & mask) == mask)
            return 0;
    }
    return -1;
}

static int tis_xfer(const uint8_t *cmd, uint32_t cmd_sz, uint8_t *rsp,
                    uint32_t rsp_max, uint32_t *rsp_sz)
{
    uint32_t i, n, total;
    int ret = -1;

    mmio_write8(TIS_ACCESS, TIS_ACCESS_REQUEST);
    for (i = 0; i < TIS_POLL_MAX; i++) {
        if ((mmio_read8(TIS_ACCESS) & (TIS_ACCESS_VALID | TIS_ACCESS_ACTIVE)) ==
            (TIS_ACCESS_VALID | TIS_ACCESS_ACTIVE))
            break;
    }
    if (i == TIS_POLL_MAX)
        return -1;
    mmio_write8(TIS_STS, TIS_STS_READY);
    if (tis_wait_sts(TIS_STS_READY) != 0)
        goto out;
    for (i = 0; i < cmd_sz; i++) {
        /* burst count, STS bits 8-23 */
        for (n = 0; n < TIS_POLL_MAX; n++) {
            if ((mmio_read32(TIS_STS) & 0x00FFFF00U) != 0)
                break;
        }
        if (n == TIS_POLL_MAX)
            goto out;
        mmio_write8(TIS_FIFO, cmd[i]);
    }
    mmio_write8(TIS_STS, TIS_STS_GO);
    total = 10;
    for (i = 0; i < total; i++) {
        if (tis_wait_sts(TIS_STS_VALID | TIS_STS_AVAIL) != 0 || i >= rsp_max)
            goto out;
        rsp[i] = mmio_read8(TIS_FIFO);
        if (i == 5) {
            total = ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) |
                    ((uint32_t)rsp[4] << 8) | rsp[5];
            if (total < 10 || total > rsp_max)
                goto out;
        }
    }
    *rsp_sz = total;
    ret = 0;
out:
    mmio_write8(TIS_STS, TIS_STS_READY);
    mmio_write8(TIS_ACCESS, TIS_ACCESS_ACTIVE);
    return ret;
}

static uint32_t tpm_rsp_code(const uint8_t *rsp)
{
    return ((uint32_t)rsp[6] << 24) | ((uint32_t)rsp[7] << 16) |
           ((uint32_t)rsp[8] << 8) | rsp[9];
}

/* Returns 0 with the reference hash, or -1 when there is none to trust. */
static int fsp_nvs_tpm_ref(uint8_t *hash)
{
    static const uint8_t startup[] = {
        0x80, 0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00
    };
    uint8_t nv_read[35] = {
        0x80, 0x02, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x01, 0x4e,
        0, 0, 0, 0, /* authHandle: the index */
        0, 0, 0, 0, /* nvIndex */
        0x00, 0x00, 0x00, 0x09, /* authorization size */
        0x40, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, /* empty PW */
        0x00, FSP_NVS_HASH_SIZE, 0x00, 0x00 /* size, offset */
    };
    uint8_t nv_public[14] = {
        0x80, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x01, 0x69,
        0, 0, 0, 0 /* nvIndex */
    };
    uint8_t rsp[96];
    uint32_t rsp_sz = 0, rc, attr, i;

    for (i = 0; i < 4; i++) {
        nv_read[10 + i] = nv_read[14 + i] = nv_public[10 + i] =
            (uint8_t)(FSP_NVS_TPM_NV_INDEX >> (24 - 8 * i));
    }
    /* TPM_RC_INITIALIZE (0x100): already started */
    if (tis_xfer(startup, sizeof(startup), rsp, sizeof(rsp), &rsp_sz) != 0)
        return -1;
    rc = tpm_rsp_code(rsp);
    if (rc != 0 && rc != 0x100)
        return -1;
    /* TPMS_NV_PUBLIC: index, name algorithm, then the attributes */
    if (tis_xfer(nv_public, sizeof(nv_public), rsp, sizeof(rsp),
                 &rsp_sz) != 0 || tpm_rsp_code(rsp) != 0 || rsp_sz < 22)
        return -1;
    attr = ((uint32_t)rsp[18] << 24) | ((uint32_t)rsp[19] << 16) |
           ((uint32_t)rsp[20] << 8) | rsp[21];
    if ((attr & FSP_NVS_NV_ATTR_MASK) != FSP_NVS_NV_ATTR)
        return -1;
    if (tis_xfer(nv_read, sizeof(nv_read), rsp, sizeof(rsp), &rsp_sz) != 0)
        return -1;
    if (tpm_rsp_code(rsp) != 0 || rsp_sz < 16 + FSP_NVS_HASH_SIZE ||
        rsp[14] != 0 || rsp[15] != FSP_NVS_HASH_SIZE)
        return -1;
    memcpy(hash, rsp + 16, FSP_NVS_HASH_SIZE);
    return 0;
}

static int fsp_nvs_tpm_check(const struct fsp_nvs_record *rec)
{
    uint8_t ref[FSP_NVS_HASH_SIZE], hash[FSP_NVS_HASH_SIZE];
    uint8_t diff = 0;
    int i;

    if (fsp_nvs_tpm_ref(ref) != 0) {
        wolfBoot_printf("FSP NVS: no TPM reference hash\r\n");
        return -1;
    }
    if (fsp_nvs_record_hash(rec, hash) != 0)
        return -1;
    for (i = 0; i < FSP_NVS_HASH_SIZE; i++)
        diff |= ref[i] ^ hash[i];
    if (diff != 0) {
        wolfBoot_printf("FSP NVS: saved training data does not match the "
                        "TPM reference hash\r\n");
        return -1;
    }
    return 0;
}
#endif /* WOLFBOOT_FSP_NVS_TPM */

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
#ifdef WOLFBOOT_FSP_NVS_TPM
    if (fsp_nvs_tpm_check(rec) != 0)
        return 0;
#endif
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

/* Returns 1 when flash holds the training data of this boot */
static int fsp_nvs_store(void)
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
        return 0;
    }
    data = (const uint8_t *)(hob + 1);
    len = hob->header.hob_length - sizeof(*hob);
    if (len > FSP_NVS_SIZE - sizeof(hdr)) {
        wolfBoot_printf("FSP NVS: training data (%d bytes) exceeds the cache\r\n",
                        (int)len);
        return 0;
    }
    if (fsp_nvs_is_current(rec, params->fsp_nvs_key, data, len))
        return 1;

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
        return 0;
    }
    FSP_NVS_CACHE_FLUSH();
    if (!fsp_nvs_is_current(rec, hdr.key, data, len)) {
        wolfBoot_printf("FSP NVS: training data read-back mismatch\r\n");
        return 0;
    }
    wolfBoot_printf("FSP NVS: saved %d bytes of training data\r\n", (int)len);
    return 1;
}

void fsp_nvs_save(void)
{
    const struct fsp_nvs_record *rec =
        (const struct fsp_nvs_record *)(uintptr_t)FSP_NVS_BASE;

    fsp_nvs_stale = !fsp_nvs_store() &&
        fsp_nvs_record_valid(rec, stage2_get_parameters()->fsp_nvs_key,
                             FSP_NVS_SIZE);
}

#ifdef WOLFBOOT_FSP_NVS_TPM
#include "tpm.h"
#include <wolftpm/tpm2_wrap.h>

/* Keep the TPM reference hash equal to the record in flash, but only when
 * that record holds the data FspMemInit returned on this boot: if the save
 * failed, flash may still hold an older record, one stage 1 perhaps rejected.
 * Must run before the platform hierarchy is disabled. */
void fsp_nvs_tpm_sync(void)
{
    const struct fsp_nvs_record *rec =
        (const struct fsp_nvs_record *)(uintptr_t)FSP_NVS_BASE;
    WOLFTPM2_DEV *dev = &wolftpm_dev;
    WOLFTPM2_HANDLE parent;
    WOLFTPM2_NV nv;
    NV_Write_In in;
    struct stage2_parameter *params = stage2_get_parameters();
    struct efi_hob_guid_type *hob;
    uint8_t hash[FSP_NVS_HASH_SIZE], ref[FSP_NVS_HASH_SIZE];
    word32 attr = 0, sz = sizeof(ref);
    int rc;

    hob = fsp_nvs_find_hob((struct efi_hob *)(uintptr_t)params->hobList);
    if (hob == NULL ||
        !fsp_nvs_is_current(rec, params->fsp_nvs_key,
                            (const uint8_t *)(hob + 1),
                            hob->header.hob_length - sizeof(*hob)) ||
        fsp_nvs_record_hash(rec, hash) != 0)
        return;

    memset(&nv, 0, sizeof(nv));
    nv.handle.hndl = FSP_NVS_TPM_NV_INDEX;
    if (wolfTPM2_NVOpen(dev, &nv, FSP_NVS_TPM_NV_INDEX, NULL, 0) == 0) {
        if ((nv.attributes & FSP_NVS_NV_ATTR_MASK) != FSP_NVS_NV_ATTR) {
            wolfBoot_printf("FSP NVS: TPM reference index is not "
                            "platform-owned\r\n");
            return;
        }
        wolfTPM2_SetAuthHandle(dev, 0, &nv.handle);
        rc = wolfTPM2_NVReadAuth(dev, &nv, FSP_NVS_TPM_NV_INDEX, ref, &sz, 0);
        wolfTPM2_UnsetAuth(dev, 0);
        if (rc == 0 && sz == sizeof(ref) &&
            memcmp(ref, hash, sizeof(ref)) == 0)
            return;
    }

    memset(&parent, 0, sizeof(parent));
    parent.hndl = TPM_RH_PLATFORM;
    wolfTPM2_GetNvAttributesTemplate(TPM_RH_PLATFORM, &attr);
    attr &= ~TPMA_NV_AUTHWRITE;
    rc = wolfTPM2_NVCreateAuth(dev, &parent, &nv, FSP_NVS_TPM_NV_INDEX, attr,
                               sizeof(hash), NULL, 0);
    if (rc == TPM_RC_NV_DEFINED)
        rc = 0;
    wolfTPM2_UnsetAuth(dev, 1);
    if (rc == 0) {
        memset(&in, 0, sizeof(in));
        in.authHandle = TPM_RH_PLATFORM;
        in.nvIndex = FSP_NVS_TPM_NV_INDEX;
        in.data.size = sizeof(hash);
        memcpy(in.data.buffer, hash, sizeof(hash));
        wolfTPM2_SetAuthPassword(dev, 0, NULL);
        rc = TPM2_NV_Write(&in);
        wolfTPM2_UnsetAuth(dev, 0);
    }
    if (rc != 0)
        wolfBoot_printf("FSP NVS: TPM reference hash update failed 0x%x\r\n",
                        rc);
    else
        wolfBoot_printf("FSP NVS: TPM reference hash updated\r\n");
}
#endif /* WOLFBOOT_FSP_NVS_TPM */
#endif /* BUILD_LOADER_STAGE1 */
