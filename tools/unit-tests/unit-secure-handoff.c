/* unit-secure-handoff.c
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

#include <stdint.h>
#include <string.h>

#define WOLFBOOT_HASH_SHA256
#include "wolfboot/secure_handoff.h"
#include "../../hal/stm32h5_lifecycle.h"

static int test_secure_handoff_record(void)
{
    wolfBoot_secure_handoff_t handoff;
    uint8_t measurement[WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE];
    uint32_t i;

    for (i = 0u; i < sizeof(measurement); ++i) {
        measurement[i] = (uint8_t)(i + 1u);
    }
    (void)memset(&handoff, 0xA5, sizeof(handoff));

    if (wolfBoot_secure_handoff_build(&handoff, measurement, 7u,
            WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_SECURED) != 0) {
        return -1;
    }
    if ((handoff.magic != WOLFBOOT_SECURE_HANDOFF_MAGIC) ||
            (handoff.magic_inverse != ~WOLFBOOT_SECURE_HANDOFF_MAGIC) ||
            (handoff.version != WOLFBOOT_SECURE_HANDOFF_VERSION) ||
            (handoff.size != sizeof(handoff)) ||
            (handoff.lifecycle !=
                WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_SECURED) ||
            (handoff.image_version != 7u) ||
            (handoff.hash_algorithm !=
                WOLFBOOT_SECURE_HANDOFF_HASH_SHA256) ||
            (handoff.measurement_size !=
                WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE) ||
            (sizeof(handoff) != 56u)) {
        return -1;
    }
    for (i = 0u; i < sizeof(measurement); ++i) {
        if (handoff.measurement[i] != measurement[i]) {
            return -1;
        }
    }
    return 0;
}

static int test_secure_handoff_invalid_input(void)
{
    wolfBoot_secure_handoff_t handoff;
    uint8_t measurement[WOLFBOOT_SECURE_HANDOFF_DIGEST_SIZE] = { 0u };

    if (wolfBoot_secure_handoff_build(NULL, measurement, 1u,
            WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_UNKNOWN) != -1) {
        return -1;
    }
    if (wolfBoot_secure_handoff_build(&handoff, NULL, 1u,
            WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_UNKNOWN) != -1) {
        return -1;
    }
    return 0;
}

static int test_stm32h5_product_state_lifecycle_map(void)
{
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_OPEN) != 0x1000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_PROVISIONING) != 0x2000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_IROT_PROVISIONED) != 0x2000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_TZ_CLOSED) != 0x4000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_CLOSED) != 0x3000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(
            FLASH_PRODUCT_STATE_LOCKED) != 0x3000u) {
        return -1;
    }
    if (stm32h5_product_state_to_psa_lifecycle(0xFFu) !=
            WOLFBOOT_SECURE_HANDOFF_LIFECYCLE_UNKNOWN) {
        return -1;
    }
    return 0;
}

static int test_stm32h5_debug_lifecycle_map(void)
{
    uint32_t disabled = 0xAAu;

    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            disabled) != 0x3000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            CORTEX_M_DAUTHSTATUS_ENABLED <<
            CORTEX_M_DAUTHSTATUS_NSID_SHIFT) != 0x4000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            CORTEX_M_DAUTHSTATUS_ENABLED <<
            CORTEX_M_DAUTHSTATUS_NSNID_SHIFT) != 0x4000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            CORTEX_M_DAUTHSTATUS_ENABLED <<
            CORTEX_M_DAUTHSTATUS_SID_SHIFT) != 0x5000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            CORTEX_M_DAUTHSTATUS_ENABLED <<
            CORTEX_M_DAUTHSTATUS_SNID_SHIFT) != 0x5000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_CLOSED,
            (CORTEX_M_DAUTHSTATUS_ENABLED <<
                CORTEX_M_DAUTHSTATUS_NSID_SHIFT) |
            (CORTEX_M_DAUTHSTATUS_ENABLED <<
                CORTEX_M_DAUTHSTATUS_SID_SHIFT)) != 0x5000u) {
        return -1;
    }
    if (stm32h5_attestation_lifecycle(FLASH_PRODUCT_STATE_OPEN,
            0xFFu) != 0x1000u) {
        return -1;
    }
    return 0;
}

int main(void)
{
    if (test_secure_handoff_record() != 0) {
        return 1;
    }
    if (test_secure_handoff_invalid_input() != 0) {
        return 1;
    }
    if (test_stm32h5_product_state_lifecycle_map() != 0) {
        return 1;
    }
    if (test_stm32h5_debug_lifecycle_map() != 0) {
        return 1;
    }
    return 0;
}
