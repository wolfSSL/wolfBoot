/* wolfhsm_flash_hal.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "wolfboot/wolfhsm_flash_hal.h"

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/misc.h"

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_flash.h"

#define WHFH5_SECTOR_SIZE (8U * 1024U)

/* Sector-cached RMW, mirrors psa_store.c. H5 flash programs as 16-byte
 * qwords ECC; one program per erase. wolfHSM 8-byte writes would clobber
 * neighbours, so Program loads the sector, modifies, erases, rewrites.
 *
 * Single static cache: non-reentrant by design (one secure-side server,
 * synchronous per-NSC dispatch, no threads or interrupts on this path).
 * Residual plaintext may remain in cache for one programming cycle if a
 * synchronous fault aborts hal_flash_write; disable debug in production. */
static uint8_t cached_sector[WHFH5_SECTOR_SIZE];

static int whFlashH5_Init(void *context, const void *config)
{
    const whFlashH5Ctx *cfg = (const whFlashH5Ctx *)config;
    whFlashH5Ctx       *ctx = (whFlashH5Ctx *)context;

    if (ctx == NULL || cfg == NULL) {
        return WH_ERROR_BADARGS;
    }

    if (cfg->base == 0U || cfg->size == 0U || cfg->partition_size == 0U ||
        (cfg->base % WHFH5_SECTOR_SIZE) != 0U ||
        (cfg->size % WHFH5_SECTOR_SIZE) != 0U ||
        (cfg->partition_size % WHFH5_SECTOR_SIZE) != 0U ||
        cfg->partition_size > cfg->size / 2U) {
        return WH_ERROR_BADARGS;
    }

    *ctx = *cfg;
    return WH_ERROR_OK;
}

static int whFlashH5_Cleanup(void *context)
{
    if (context == NULL) {
        return WH_ERROR_BADARGS;
    }
    return WH_ERROR_OK;
}

static uint32_t whFlashH5_PartitionSize(void *context)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    return (ctx == NULL) ? 0U : ctx->partition_size;
}

static int whFlashH5_WriteLock(void *context, uint32_t offset, uint32_t size)
{
    (void)context;
    (void)offset;
    (void)size;
    return WH_ERROR_OK;
}

static int whFlashH5_WriteUnlock(void *context, uint32_t offset, uint32_t size)
{
    (void)context;
    (void)offset;
    (void)size;
    return WH_ERROR_OK;
}

static int whFlashH5_Read(void *context, uint32_t offset, uint32_t size,
                          uint8_t *data)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size > 0U) {
        memcpy(data, (const uint8_t *)(ctx->base + offset), size);
    }
    return WH_ERROR_OK;
}

static int whFlashH5_Program(void *context, uint32_t offset, uint32_t size,
                             const uint8_t *data)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    uint32_t      written = 0U;
    int           hrc     = 0;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size == 0U) {
        return WH_ERROR_OK;
    }

    /* defensive wipe in case a prior call faulted before the per-iter wipe */
    wc_ForceZero(cached_sector, sizeof(cached_sector));

    hal_flash_unlock();
    while (written < size) {
        uint32_t in_sector_off = (offset + written) % WHFH5_SECTOR_SIZE;
        uint32_t sector_offset = (offset + written) - in_sector_off;
        uint32_t chunk         = WHFH5_SECTOR_SIZE - in_sector_off;
        if (chunk > size - written) {
            chunk = size - written;
        }

        memcpy(cached_sector,
               (const uint8_t *)(ctx->base + sector_offset),
               WHFH5_SECTOR_SIZE);
        memcpy(cached_sector + in_sector_off, data + written, chunk);

        hrc = hal_flash_erase(ctx->base + sector_offset, WHFH5_SECTOR_SIZE);
        if (hrc == 0) {
            hrc = hal_flash_write(ctx->base + sector_offset, cached_sector,
                                  WHFH5_SECTOR_SIZE);
        }

        /* Per-iteration wipe so a fault between sectors doesn't strand
         * plaintext keystore bytes in the static cache. */
        wc_ForceZero(cached_sector, sizeof(cached_sector));

        if (hrc != 0) {
            break;
        }
        written += chunk;
    }
    hal_flash_lock();

    return (hrc == 0) ? WH_ERROR_OK : WH_ERROR_ABORTED;
}

static int whFlashH5_Erase(void *context, uint32_t offset, uint32_t size)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    int rc;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size == 0U) {
        return WH_ERROR_OK;
    }
    if ((offset % WHFH5_SECTOR_SIZE) != 0U ||
        (size % WHFH5_SECTOR_SIZE) != 0U) {
        return WH_ERROR_BADARGS;
    }

    /* hal_flash_erase takes int; loop over sector-sized chunks so the cast
     * stays well-defined regardless of how large size grows. */
    hal_flash_unlock();
    {
        uint32_t erased = 0U;
        rc = 0;
        while (erased < size) {
            rc = hal_flash_erase(ctx->base + offset + erased,
                                 (int)WHFH5_SECTOR_SIZE);
            if (rc != 0) {
                break;
            }
            erased += WHFH5_SECTOR_SIZE;
        }
    }
    hal_flash_lock();
    return (rc == 0) ? WH_ERROR_OK : WH_ERROR_ABORTED;
}

static int whFlashH5_Verify(void *context, uint32_t offset, uint32_t size,
                            const uint8_t *data)
{
    whFlashH5Ctx  *ctx = (whFlashH5Ctx *)context;
    const uint8_t *p;
    uint8_t        acc = 0;
    uint32_t       i;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    /* constant-time compare; verified data may be key material */
    p = (const uint8_t *)(ctx->base + offset);
    for (i = 0U; i < size; i++) {
        acc |= (uint8_t)(p[i] ^ data[i]);
    }
    return (acc == 0U) ? WH_ERROR_OK : WH_ERROR_NOTVERIFIED;
}

static int whFlashH5_BlankCheck(void *context, uint32_t offset, uint32_t size)
{
    whFlashH5Ctx  *ctx = (whFlashH5Ctx *)context;
    const uint8_t *p;
    uint32_t       i;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    p = (const uint8_t *)(ctx->base + offset);
    for (i = 0U; i < size; i++) {
        if (p[i] != 0xFFU) {
            return WH_ERROR_NOTBLANK;
        }
    }
    return WH_ERROR_OK;
}

const whFlashCb whFlashH5_Cb = {
    .Init          = whFlashH5_Init,
    .Cleanup       = whFlashH5_Cleanup,
    .PartitionSize = whFlashH5_PartitionSize,
    .WriteLock     = whFlashH5_WriteLock,
    .WriteUnlock   = whFlashH5_WriteUnlock,
    .Read          = whFlashH5_Read,
    .Program       = whFlashH5_Program,
    .Erase         = whFlashH5_Erase,
    .Verify        = whFlashH5_Verify,
    .BlankCheck    = whFlashH5_BlankCheck,
};

#endif /* WOLFCRYPT_TZ_WOLFHSM */
