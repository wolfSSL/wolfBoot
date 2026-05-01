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

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_flash.h"

#define WHFH5_SECTOR_SIZE (8U * 1024U)

/* Sector-cached read-modify-erase-write, mirroring psa_store.c. STM32H5
 * flash programs in 16-byte quad-words with ECC; each quad-word can be
 * programmed exactly once between erases. wolfHSM issues 8-byte unit
 * writes which would otherwise re-program neighbouring qwords, so every
 * Program call here loads the affected sector into RAM, modifies it, and
 * rewrites the whole 8 KiB sector after an erase. */
static uint8_t cached_sector[WHFH5_SECTOR_SIZE];

static int _Init(void *context, const void *config)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;

    if (ctx == NULL || config == NULL) {
        return WH_ERROR_BADARGS;
    }
    *ctx = *((const whFlashH5Ctx *)config);

    if (ctx->base == 0U || ctx->size == 0U || ctx->partition_size == 0U ||
        (ctx->base % WHFH5_SECTOR_SIZE) != 0U ||
        (ctx->size % WHFH5_SECTOR_SIZE) != 0U ||
        (ctx->partition_size % WHFH5_SECTOR_SIZE) != 0U ||
        ctx->size < (uint32_t)2 * ctx->partition_size) {
        return WH_ERROR_BADARGS;
    }
    return WH_ERROR_OK;
}

static int _Cleanup(void *context)
{
    if (context == NULL) {
        return WH_ERROR_BADARGS;
    }
    return WH_ERROR_OK;
}

static uint32_t _PartitionSize(void *context)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    return (ctx == NULL) ? 0U : ctx->partition_size;
}

static int _WriteLock(void *context, uint32_t offset, uint32_t size)
{
    (void)context;
    (void)offset;
    (void)size;
    return WH_ERROR_OK;
}

static int _WriteUnlock(void *context, uint32_t offset, uint32_t size)
{
    (void)context;
    (void)offset;
    (void)size;
    return WH_ERROR_OK;
}

static int _Read(void *context, uint32_t offset, uint32_t size, uint8_t *data)
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

static int _Program(void *context, uint32_t offset, uint32_t size,
                    const uint8_t *data)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    uint32_t      written = 0U;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size == 0U) {
        return WH_ERROR_OK;
    }

    while (written < size) {
        uint32_t in_sector_off = (offset + written) % WHFH5_SECTOR_SIZE;
        uint32_t sector_base   = (offset + written) - in_sector_off;
        uint32_t chunk         = WHFH5_SECTOR_SIZE - in_sector_off;
        if (chunk > size - written) {
            chunk = size - written;
        }

        memcpy(cached_sector,
               (const uint8_t *)(ctx->base + sector_base),
               WHFH5_SECTOR_SIZE);
        memcpy(cached_sector + in_sector_off, data + written, chunk);

        hal_flash_unlock();
        hal_flash_erase(ctx->base + sector_base, WHFH5_SECTOR_SIZE);
        hal_flash_write(ctx->base + sector_base, cached_sector,
                        WHFH5_SECTOR_SIZE);
        hal_flash_lock();

        written += chunk;
    }
    return WH_ERROR_OK;
}

static int _Erase(void *context, uint32_t offset, uint32_t size)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    int rc;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if ((offset % WHFH5_SECTOR_SIZE) != 0U ||
        (size % WHFH5_SECTOR_SIZE) != 0U) {
        return WH_ERROR_BADARGS;
    }
    if (size == 0U) {
        return WH_ERROR_OK;
    }

    hal_flash_unlock();
    rc = hal_flash_erase(ctx->base + offset, (int)size);
    hal_flash_lock();
    return (rc == 0) ? WH_ERROR_OK : WH_ERROR_ABORTED;
}

static int _Verify(void *context, uint32_t offset, uint32_t size,
                   const uint8_t *data)
{
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size > 0U &&
        memcmp((const uint8_t *)(ctx->base + offset), data, size) != 0) {
        return WH_ERROR_NOTVERIFIED;
    }
    return WH_ERROR_OK;
}

static int _BlankCheck(void *context, uint32_t offset, uint32_t size)
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
    .Init          = _Init,
    .Cleanup       = _Cleanup,
    .PartitionSize = _PartitionSize,
    .WriteLock     = _WriteLock,
    .WriteUnlock   = _WriteUnlock,
    .Read          = _Read,
    .Program       = _Program,
    .Erase         = _Erase,
    .Verify        = _Verify,
    .BlankCheck    = _BlankCheck,
};

#endif /* WOLFCRYPT_TZ_WOLFHSM */
