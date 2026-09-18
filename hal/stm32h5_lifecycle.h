/* stm32h5_lifecycle.h
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WOLFBOOT_STM32H5_LIFECYCLE_H
#define WOLFBOOT_STM32H5_LIFECYCLE_H

#include <stdint.h>

#define FLASH_PRODUCT_STATE_OPEN             0xEDu
#define FLASH_PRODUCT_STATE_PROVISIONING     0x17u
#define FLASH_PRODUCT_STATE_IROT_PROVISIONED 0x2Eu
#define FLASH_PRODUCT_STATE_TZ_CLOSED        0xC6u
#define FLASH_PRODUCT_STATE_CLOSED           0x72u
#define FLASH_PRODUCT_STATE_LOCKED           0x5Cu

#define CORTEX_M_DAUTHSTATUS_ADDRESS          0xE000EFB8u
#define CORTEX_M_DAUTHSTATUS_NSID_SHIFT       0u
#define CORTEX_M_DAUTHSTATUS_NSNID_SHIFT      2u
#define CORTEX_M_DAUTHSTATUS_SID_SHIFT        4u
#define CORTEX_M_DAUTHSTATUS_SNID_SHIFT       6u
#define CORTEX_M_DAUTHSTATUS_FIELD_MASK       0x3u
#define CORTEX_M_DAUTHSTATUS_ENABLED          0x3u

static inline uint32_t stm32h5_product_state_to_psa_lifecycle(
    uint32_t productState)
{
    uint32_t lifecycle;

    switch (productState) {
        case FLASH_PRODUCT_STATE_OPEN:
            lifecycle = 0x1000u; /* PSA_LIFECYCLE_ASSEMBLY_AND_TEST */
            break;
        case FLASH_PRODUCT_STATE_PROVISIONING:
        case FLASH_PRODUCT_STATE_IROT_PROVISIONED:
            lifecycle = 0x2000u; /* PSA_LIFECYCLE_PSA_ROT_PROVISIONING */
            break;
        case FLASH_PRODUCT_STATE_TZ_CLOSED:
            lifecycle = 0x4000u; /* PSA_LIFECYCLE_NON_PSA_ROT_DEBUG */
            break;
        case FLASH_PRODUCT_STATE_CLOSED:
        case FLASH_PRODUCT_STATE_LOCKED:
            lifecycle = 0x3000u; /* PSA_LIFECYCLE_SECURED */
            break;
        default:
            lifecycle = 0x0000u; /* PSA_LIFECYCLE_UNKNOWN */
            break;
    }
    return lifecycle;
}

static inline int stm32h5_debug_status_field_enabled(uint32_t debugAuthStatus,
    uint32_t shift)
{
    return (((debugAuthStatus >> shift) &
        CORTEX_M_DAUTHSTATUS_FIELD_MASK) ==
        CORTEX_M_DAUTHSTATUS_ENABLED);
}

static inline uint32_t stm32h5_attestation_lifecycle(uint32_t productState,
    uint32_t debugAuthStatus)
{
    uint32_t lifecycle = stm32h5_product_state_to_psa_lifecycle(productState);

    if ((productState == FLASH_PRODUCT_STATE_CLOSED) ||
            (productState == FLASH_PRODUCT_STATE_LOCKED)) {
        if (stm32h5_debug_status_field_enabled(debugAuthStatus,
                CORTEX_M_DAUTHSTATUS_SID_SHIFT) ||
                stm32h5_debug_status_field_enabled(debugAuthStatus,
                CORTEX_M_DAUTHSTATUS_SNID_SHIFT)) {
            lifecycle = 0x5000u; /* PSA_LIFECYCLE_RECOVERABLE_PSA_ROT_DEBUG */
        }
        else if (stm32h5_debug_status_field_enabled(debugAuthStatus,
                CORTEX_M_DAUTHSTATUS_NSID_SHIFT) ||
                stm32h5_debug_status_field_enabled(debugAuthStatus,
                CORTEX_M_DAUTHSTATUS_NSNID_SHIFT)) {
            lifecycle = 0x4000u; /* PSA_LIFECYCLE_NON_PSA_ROT_DEBUG */
        }
    }
    return lifecycle;
}

#endif /* WOLFBOOT_STM32H5_LIFECYCLE_H */
