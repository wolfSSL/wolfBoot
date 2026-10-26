/* app_psoc_e8.c
 *
 * Test bare-metal application for Infineon PSOC Edge E8.
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
#include "hal.h"
#include "printf.h"
#include "hal/psoc_e8.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#ifdef WOLFCRYPT_TZ_WOLFHSM
#include "wcs/wolfhsm_test.h"
#endif
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

/* With TrustZone the application is Non-secure and the update API is a set
 * of Secure gateway calls. */
#ifdef TZEN
#define APP_FW_VERSION()     wolfBoot_nsc_current_firmware_version()
#define APP_SUCCESS()        wolfBoot_nsc_success()
#define APP_UPDATE_TRIGGER() wolfBoot_nsc_update_trigger()
#else
#define APP_FW_VERSION()     wolfBoot_current_firmware_version()
#define APP_SUCCESS()        wolfBoot_success()
#define APP_UPDATE_TRIGGER() wolfBoot_update_trigger()
#endif

#ifdef WOLFCRYPT_TZ_WOLFHSM
/* newlib stdout for the wolfHSM client tests. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    uart_write(ptr, (unsigned int)len);
    return len;
}
#endif

void main(void)
{
    uint32_t version;
    int i;

    uart_init();
    version = APP_FW_VERSION();
    wolfBoot_printf("TEST APP\r\nApp version: %u\r\n", (unsigned)version);

#ifdef WOLFCRYPT_TZ_WOLFHSM
    wolfBoot_printf("Running wolfHSM client tests...\r\n");
    (void)cmd_wolfhsm_test(NULL);
#endif
#ifdef WOLFCRYPT_TEST
    wolfBoot_printf("Running wolfCrypt tests...\r\n");
    wolfCrypt_Init();
    wolfcrypt_test(NULL);
    wolfCrypt_Cleanup();
#endif
#ifdef WOLFCRYPT_BENCHMARK
    wolfBoot_printf("Running wolfCrypt benchmarks...\r\n");
    wolfCrypt_Init();
    benchmark_test(NULL);
    wolfCrypt_Cleanup();
#endif

    /* v1 asks for an update and resets so wolfBoot performs the swap.
     * v2 and later confirm the update so it is not rolled back. */
    if (version >= 2) {
        APP_SUCCESS();
        wolfBoot_printf("update OK -- success confirmed\r\n");
        while (1)
            ;
    }

    /* Pause before asking for the update, so a debugger can program the
     * update partition before the reset comes round. */
    for (i = 0; i < 5; i++) {
        psoc_e8_delay_us(400000);
        wolfBoot_printf(".");
    }
    wolfBoot_printf("\r\ntriggering update -> reset\r\n");
    APP_UPDATE_TRIGGER();
    arch_reboot();
}
