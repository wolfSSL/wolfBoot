/* wolfhsm_flash_hal.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifndef WOLFBOOT_WOLFHSM_FLASH_HAL_H
#define WOLFBOOT_WOLFHSM_FLASH_HAL_H

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>

#include "wolfhsm/wh_flash.h"

/* Per-call config / context for the adapter. base/size/partition_size are
 * the only client-visible fields; the cache lives inside the static
 * implementation in wolfhsm_flash_hal.c (mirroring psa_store.c). */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t partition_size;
} whFlashH5Ctx;

extern const whFlashCb whFlashH5_Cb;

#endif /* WOLFCRYPT_TZ_WOLFHSM */

#endif /* WOLFBOOT_WOLFHSM_FLASH_HAL_H */
