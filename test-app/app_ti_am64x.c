/* app_ti_am64x.c
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

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

/* wolfCrypt test/benchmark support */
#ifdef WOLFCRYPT_TEST
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/test/test.h>
int wolfcrypt_test(void *args);
#endif

#ifdef WOLFCRYPT_BENCHMARK
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfcrypt/benchmark/benchmark.h>
int benchmark_test(void *args);
#endif

extern void hal_init(void);

static void check_parts(
    uint32_t *pboot_ver, uint32_t *pupdate_ver,
    uint8_t *pboot_state, uint8_t *pupdate_state
)
{
#ifdef WOLFCRYPT_SECURE_MODE
    *pboot_ver = wolfBoot_nsc_current_firmware_version();
    *pupdate_ver = wolfBoot_nsc_update_firmware_version();
    if (wolfBoot_nsc_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_nsc_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#else
    *pboot_ver = wolfBoot_current_firmware_version();
    *pupdate_ver = wolfBoot_update_firmware_version();
    if (wolfBoot_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;
#endif

    wolfBoot_printf("    boot:   ver=0x%lx state=0x%02x\n", *pboot_ver, *pboot_state);
    wolfBoot_printf("    update: ver=0x%lx state=0x%02x\n", *pupdate_ver, *pupdate_state);
}

void main(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state, update_state;

    hal_init();

    wolfBoot_printf("\n==================================\n");
    wolfBoot_printf("TI AM64x wolfBoot demo Application\n");
    wolfBoot_printf("Copyright 2026 wolfSSL Inc\n");
    wolfBoot_printf("==================================\n");

//     check_parts(&boot_ver, &update_ver, &boot_state, &update_state);

//     if (
//         boot_ver != 0 &&
//         (boot_state == IMG_STATE_TESTING || boot_state == IMG_STATE_NEW)
//     )
//     {
// #ifdef WOLFCRYPT_SECURE_MODE
//         wolfBoot_printf("Calling wolfBoot_nsc_success()\n");
//         wolfBoot_nsc_success();
// #else
//         wolfBoot_printf("Calling wolfBoot_success()\n");
//         wolfBoot_success();
// #endif
//         check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
//     }

//     if (boot_ver == 1)
//     {
//         if (update_ver != 0) {
//             wolfBoot_printf("Update detected, version: 0x%lx\n", update_ver);
//             wolfBoot_printf("Triggering update...\n");
// #ifdef WOLFCRYPT_SECURE_MODE
//             wolfBoot_nsc_update_trigger();
// #else
//             wolfBoot_update_trigger();
// #endif
//             check_parts(&boot_ver, &update_ver, &boot_state, &update_state);
//             wolfBoot_printf("...done. Reboot to apply.\n");
//         }
//     }

#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    wolfCrypt_Init();

# ifdef WOLFCRYPT_TEST
    wolfBoot_printf("\nRunning wolfCrypt tests...\n");
    wolfcrypt_test(NULL);
    wolfBoot_printf("Tests complete.\n\n");
# endif

# ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("\nRunning wolfCrypt benchmarks...\n");
    benchmark_test(NULL);
    wolfBoot_printf("Benchmarks complete.\n\n");
# endif

    wolfCrypt_Cleanup();
#endif

    while (1) {
        __asm__ volatile ("wfi");
    }
}
