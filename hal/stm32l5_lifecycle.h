/* stm32l5_lifecycle.h
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

#ifndef WOLFBOOT_STM32L5_LIFECYCLE_H
#define WOLFBOOT_STM32L5_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#define STM32L5_FLASH_OPTR_RDP_MASK   0xFFu
#define STM32L5_FLASH_OPTR_TZEN       (1u << 31)

#define STM32L5_RDP_LEVEL_0           0xAAu
#define STM32L5_RDP_LEVEL_0_5         0x55u
#define STM32L5_RDP_LEVEL_2           0xCCu

/* Maps FLASH_OPTR to a PSA lifecycle. Without TZEN there is no Secure
 * world to attest, so the state is reported as unknown (-1). */
static inline int stm32l5_optr_to_psa_lifecycle(uint32_t optr,
    uint32_t *lifecycle)
{
    int ret = 0;

    if (lifecycle == NULL) {
        ret = -1;
    }
    else if ((optr & STM32L5_FLASH_OPTR_TZEN) == 0u) {
        ret = -1;
    }
    else {
        switch (optr & STM32L5_FLASH_OPTR_RDP_MASK) {
            case STM32L5_RDP_LEVEL_0:
                *lifecycle = 0x2000u; /* PSA_LIFECYCLE_PSA_ROT_PROVISIONING */
                break;
            case STM32L5_RDP_LEVEL_0_5:
                *lifecycle = 0x4000u; /* PSA_LIFECYCLE_NON_PSA_ROT_DEBUG */
                break;
            case STM32L5_RDP_LEVEL_2:
                *lifecycle = 0x3000u; /* PSA_LIFECYCLE_SECURED */
                break;
            default:
                /* Any other value is RDP level 1 */
                *lifecycle = 0x5000u; /* PSA_LIFECYCLE_RECOVERABLE_PSA_ROT_DEBUG */
                break;
        }
    }
    return ret;
}

#endif /* WOLFBOOT_STM32L5_LIFECYCLE_H */
