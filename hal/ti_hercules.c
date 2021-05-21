/* ti_hercules.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <string.h>
#include <target.h>
#include "image.h"
#include "hal.h"

#ifndef CORTEX_R5
#   error "wolfBoot TI Hercules HAL: wrong architecture selected. Please compile with TARGET=ti_hercules."
#endif

#include <F021.h>
#include <stdio.h>

//#define FLASH_DEMO 1

/* public HAL functions */
void hal_init(void)
{
    int freq_MHz = 16;
    int st = Fapi_initializeFlashBanks(freq_MHz);
    if (st != 0) {
        printf("Failed Fapi_initializeFlashBanks(%d) => (%d)\n", freq_MHz, st);
        return;
    }

#if FLASH_DEMO
    {
        uint32_t address = 0x1E0000;
        const char msg[] = "wolfBoot was here!";
        hal_flash_unlock();

        if (hal_flash_erase(address, 7)) {
            printf("failed to erase\n");
        }

        if(hal_flash_write(address, msg, sizeof(msg))) {
            printf("failed to program\n");
        }

        hal_flash_lock();

        if(memcmp(msg, (void*)address, sizeof(msg))) {
            printf("msg and flash don't match\n");
        }

        /* stall here to prevent accidently including this in production */
        while (1)
            ;
    }
#endif

}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int st = Fapi_issueProgrammingCommand((void*)address,
                                          data,
                                          len,
                                          NULL,
                                          0,
                                          Fapi_AutoEccGeneration);
    if (st != 0) {
        printf("Failed Fapi_issueProgrammingCommand() => (%d)\n", st);
        return -1;
    }

    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    /* enable all sectors  in flash bank 0*/
    Fapi_FlashBankType bank = Fapi_FlashBank0;
    uint16_t en = 0xffff;

    int st = Fapi_setActiveFlashBank(bank);
    if (st != 0) {
        printf("Failed Fapi_setActiveFlashBank(%d) => (%d)\n", bank, st);
        return;
    }

    st = Fapi_enableMainBankSectors(en);
    if (st != 0) {
        printf("Failed Fapi_enableMainBankSectors() => (%d)\n", st);
        return;
    }

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;
}

void RAMFUNCTION hal_flash_lock(void)
{
    Fapi_FlashBankType bank = Fapi_FlashBank0;

    int st = Fapi_setActiveFlashBank(bank);
    if (st != 0) {
        printf("Failed Fapi_setActiveFlashBank(%d) => (%d)\n", bank, st);
        return;
    }

    /* disable all sectors */
    st = Fapi_enableMainBankSectors(0);
    if (st != 0) {
        printf("Failed Fapi_enableMainBankSectors() => (%d)\n", st);
        return;
    }

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int st = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, (void*)address);
    if (st != 0) {
        printf("Failed Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, 0x%08x) => (%d)\n",
               address, st);
        return -1;
    }


    while(FAPI_CHECK_FSM_READY_BUSY == Fapi_Status_FsmBusy)
        ;

    if(FAPI_GET_FSM_STATUS != 0) {
        printf("failed to erase\n");
        return -1;
    }

    return 0;
}
