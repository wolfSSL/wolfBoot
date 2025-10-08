/* app_aurix_tc3xx.c
 *
 * Copyright (C) 2014-2025 wolfSSL Inc.
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
 * along with wolfBoot.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_TC3XX
#include "tc3_cfg.h"

#include <stdint.h>
#include <string.h>
#include "target.h"
#include "printf.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"

#define BASE_FW_VERSION 1

#ifdef TC3_CFG_HAVE_TRICORE
#include "tc3/tc3tc.h"

/* Invoked by wolfLLD CRT before main, but after CSA and stack pointer setup */
void tc3tc_crt_PreInit(void)
{
    tc3tc_PreInit();
}

/* This function is called by the BSP after CRT initialization */
void tc3tc_main(void)
{
    uint32_t coreIdx;
    TC3TC_GET_COREIDX(coreIdx);

    if (coreIdx != 0) {
        /* Application should only run on core0 */
        TC3_DEBUG();
        TC3_PANIC();
    }

    /* Update BTV to use RAM Trap Table */
    tc3tc_traps_InitBTV();

    /* setup ISR sub-system */
    tc3tc_isr_Init();

    /* setup clock system */
    tc3_clock_SetMax();

    /* disable external WATCHDOG on the board */
    bsp_board_wdg_Disable();

    uart_init();
    wolfBoot_printf("TC3xx Test Application\n");
    wolfBoot_printf("Version: %d\n", wolfBoot_current_firmware_version());

    if (wolfBoot_current_firmware_version() <= BASE_FW_VERSION) {
        /* We are booting into the base firmware, so stage the update */
        wolfBoot_update_trigger();
    }
    else {
        /* we are booting into the updated firmware so acknowledge the update
         * (to prevent rollback) */
        wolfBoot_success();
    }

    /* Main application loop */
    while(1) {
        /* spin forever */
    }
}
#elif defined(TC3_CFG_HAVE_ARM)
void tc3arm_main(void)
{
    /* setup clock system */
    tc3_clock_SetMax();

    /* disable external WATCHDOG on the board */
    bsp_board_wdg_Disable();

    uart_init();
    wolfBoot_printf("TC3xx HSM Test Application\n");
    wolfBoot_printf("Version: %d\n", wolfBoot_current_firmware_version());

    if (wolfBoot_current_firmware_version() <= BASE_FW_VERSION) {
        /* We are booting into the base firmware, so stage the update */
        wolfBoot_update_trigger();
    }
    else {
        /* we are booting into the updated firmware so acknowledge the update
         * (to prevent rollback) */
        wolfBoot_success();
    }

    /* Main application loop */
    while(1) {
        /* spin forever */
    }

}
#endif /* !TC3_CFG_HAVE_TRICORE */


#endif /* HAVE_TC3XX */
