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

/*
 * STM32H5 page (= erase) size and program-quad-word size. The dual-bank
 * H5 erase granularity is 8 KiB; flash programming happens in 16-byte
 * quad-word units.
 */
#define WHFH5_SECTOR_SIZE       (8U * 1024U)
#define WHFH5_PROGRAM_UNIT      16U


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
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->partition_size;
}

/*
 * STM32H5 has a single global flash unlock; per-region lock/unlock isn't
 * available. Program/Erase wrap the unlock+op+lock cycle themselves, so
 * the wh_FlashUnit_Program helper's "WriteUnlock around batch of writes"
 * pattern is satisfied without per-call hardware action here.
 */
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
    int rc;

    if (ctx == NULL || (size != 0U && data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (offset > ctx->size || size > ctx->size - offset) {
        return WH_ERROR_BADARGS;
    }
    if (size == 0U) {
        return WH_ERROR_OK;
    }
    /* hal_flash_write programs in H5 quad-word (16 byte) chunks; partial
     * quad-words at either end fold the existing flash content so any
     * `size` is acceptable here. The H5 ECC rule ("each quad-word may be
     * programmed at most once between erases") is satisfied as long as
     * wolfHSM's unit writes don't share a quad-word, which holds for the
     * 32 KiB-aligned partitions / 8-byte units we use. */
    hal_flash_unlock();
    rc = hal_flash_write(ctx->base + offset, data, (int)size);
    hal_flash_lock();
    return (rc == 0) ? WH_ERROR_OK : WH_ERROR_ABORTED;
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
    whFlashH5Ctx *ctx = (whFlashH5Ctx *)context;
    const uint8_t *p;
    uint32_t i;

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
