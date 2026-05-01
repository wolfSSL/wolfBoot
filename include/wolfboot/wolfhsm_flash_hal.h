/* wolfhsm_flash_hal.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

/*
 * Adapter that exposes wolfBoot's hal_flash_*() API as a wolfHSM whFlashCb,
 * letting the secure-side wolfHSM server persist its NVM in real flash
 * instead of the ramsim used during bring-up.
 */

#ifndef WOLFBOOT_WOLFHSM_FLASH_HAL_H
#define WOLFBOOT_WOLFHSM_FLASH_HAL_H

#ifdef WOLFCRYPT_TZ_WOLFHSM

#include <stdint.h>

#include "wolfhsm/wh_flash.h"

typedef struct {
    uint32_t base;           /* Absolute flash address of the wolfHSM NVM
                                region (must be 8 KiB-aligned) */
    uint32_t size;           /* Size of the region in bytes (>= 2 *
                                partition_size, multiple of 8 KiB) */
    uint32_t partition_size; /* Per-partition size in bytes; wolfHSM uses
                                two partitions (active + backup) for
                                journaling. Must be a multiple of 8 KiB. */
} whFlashH5Ctx;

extern const whFlashCb whFlashH5_Cb;

#endif /* WOLFCRYPT_TZ_WOLFHSM */

#endif /* WOLFBOOT_WOLFHSM_FLASH_HAL_H */
