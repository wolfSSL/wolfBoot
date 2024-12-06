/* Cpu0_Main.c
 *
 * Copyright (C) 2014-2024 wolfSSL Inc.
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
#include "Bsp.h"
#include "IfxCpu.h"
#include "IfxPort.h"
#include "IfxScuWdt.h"
#include "Ifx_Types.h"

#include "target.h"
#include "wolfboot/wolfboot.h"

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;


#define LED               &MODULE_P00, 5 /* LED: Port, Pin definition  */
#define BLINK_TIME_BASE   500 /* Wait time constant in milliseconds   */
#define BLINK_TIME_UPDATE 100 /* Wait time constant in milliseconds   */

#define BASE_FW_VERSION 1

/* This function initializes the port pin which drives the LED */
static void initLED(void)
{
    /* Initialization of the LED used in this example */
    IfxPort_setPinModeOutput(LED,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);

    /* Switch OFF the LED (low-level active) */
    IfxPort_setPinLow(LED);
}

void core0_main(void)
{
    size_t blinkTime;

    IfxCpu_enableInterrupts();

    /* !!WATCHDOG0 AND SAFETY WATCHDOG ARE DISABLED HERE!!
     * Enable the watchdogs and service them periodically if it is required
     */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Wait for CPU sync event */
    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);

    initLED();

    if (wolfBoot_current_firmware_version() <= BASE_FW_VERSION) {
        /* We are booting into the base firmware, so stage the update and set
         * the LED to blink slow */
        wolfBoot_update_trigger();
        blinkTime = BLINK_TIME_BASE;
    }
    else {
        /* we are booting into the updated firmware so acknowledge the update
         * (to prevent rollback) and set the LED to blink fast */
        wolfBoot_success();
        blinkTime = BLINK_TIME_UPDATE;
    }

    while (1) {
        IfxPort_togglePin(LED);
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, blinkTime));
    }
}
