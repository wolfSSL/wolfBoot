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
#include "printf.h"

#ifndef CORTEX_R5
#   error "wolfBoot TI Hercules HAL: wrong architecture selected. Please compile with TARGET=ti_hercules."
#endif

#include <F021.h>
#include <stdio.h>

/* #define FLASH_DEMO 1 */

/* public HAL functions */
void hal_init(void)
{
    int freq_MHz = 16;
    int st = Fapi_initializeFlashBanks(freq_MHz);
    if (st != 0) {
        wolfBoot_printf("Failed Fapi_initializeFlashBanks(%d) => (%d)\n", freq_MHz, st);
        return;
    }

#if defined(FLASH_DEMO) && FLASH_DEMO
    {
        uint32_t address = 0x1E0000;
        const char msg[] = "wolfBoot was here!";
        hal_flash_unlock();

        if (hal_flash_erase(address, 7)) {
            wolfBoot_printf("failed to erase\n");
        }

        if(hal_flash_write(address, msg, sizeof(msg))) {
            wolfBoot_printf("failed to program\n");
        }

        hal_flash_lock();

        if(memcmp(msg, (void*)address, sizeof(msg))) {
            wolfBoot_printf("msg and flash don't match\n");
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

static inline Fapi_FlashBankType RAMFUNCTION f021_lookup_bank(uint32_t address) {
    Fapi_FlashBankType bank = Fapi_FlashBank0;

    if (address >= 0x200000) {
        bank = Fapi_FlashBank1;
    }

    return bank;
}

static inline int RAMFUNCTION hal_flash_unlock_helper(uint32_t address) {
    uint16_t en = 0xffff;
    Fapi_FlashBankType bank = f021_lookup_bank(address);

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;

    int st = Fapi_setActiveFlashBank(bank);
    if (st != 0) {
        wolfBoot_printf("Failed Fapi_setActiveFlashBank(%d) => (%d)\n", bank, st);
        return -1;
    }

    st = Fapi_enableMainBankSectors(en);
    if (st != 0) {
        wolfBoot_printf("Failed Fapi_enableMainBankSectors() => (%d)\n", st);
        return -1;
    }

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;

    return 0;
}

#define WRITE_BLOCK_SIZE FLASHBUFFER_SIZE

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int st = 0;
    int off = 0;
    int blk_size = WRITE_BLOCK_SIZE;
    uint8_t temp[WRITE_BLOCK_SIZE];

    hal_flash_unlock_helper(address);

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;

    if(len < WRITE_BLOCK_SIZE) {
        memcpy(temp, (void*)(address - (address%WRITE_BLOCK_SIZE)), WRITE_BLOCK_SIZE);
        memcpy(temp + (address%WRITE_BLOCK_SIZE), data, len);
        st = Fapi_issueProgrammingCommand((void*)(address - (address%WRITE_BLOCK_SIZE)),
                                          (uint8_t*)temp,
                                          WRITE_BLOCK_SIZE,
                                          NULL,
                                          0,
                                          Fapi_AutoEccGeneration);
    } else {
        off = 0;
        blk_size = WRITE_BLOCK_SIZE;
        while(off < len && st == 0) {
            blk_size = WRITE_BLOCK_SIZE;
            if (len-off < blk_size) {
                blk_size = len - off;
            }

            st = Fapi_issueProgrammingCommand((void*)((uint8_t*)(address) + off),
                                              (uint8_t*)data + off,
                                              blk_size,
                                              NULL,
                                              0,
                                              Fapi_AutoEccGeneration);

            while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
                    ;

            off += blk_size;
        }
    }

    if (st != 0) {
        wolfBoot_printf("Failed Fapi_issueProgrammingCommand() => (%d)\n", st);
        return -1;
    }


    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    hal_flash_unlock_helper(0);
}

void RAMFUNCTION hal_flash_lock(void)
{

    /* disable all sectors */
    int st = Fapi_enableMainBankSectors(0);
    if (st != 0) {
        wolfBoot_printf("Failed Fapi_enableMainBankSectors() => (%d)\n", st);
        return;
    }

    while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
        ;
}

static inline int RAMFUNCTION f021_flash_erase(uint32_t address) {
    hal_flash_unlock_helper(address);

    int st = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, (void*)address);
    if (st != 0) {
        wolfBoot_printf("Failed Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, 0x%08x) => (%d)\n",
               address, st);
        return -1;
    }

    while(FAPI_CHECK_FSM_READY_BUSY == Fapi_Status_FsmBusy)
        ;

    if(FAPI_GET_FSM_STATUS != 0) {
        wolfBoot_printf("failed to erase\n");
        return -1;
    }

    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret = 0;
    int found_start = 0;
    int i;
    Fapi_FlashBankType bank = f021_lookup_bank(address);
    Fapi_FlashBankSectorsType bank_sectors;

    uint32_t cur = 0;
    uint32_t end = address + len;
    Fapi_StatusType st = Fapi_getBankSectors(bank, &bank_sectors);
    if (st != Fapi_Status_Success) {
        return -1;
    }
    cur = bank_sectors.u32BankStartAddress;

    hal_flash_unlock_helper(address);
    for(i=0; i < bank_sectors.u32NumberOfSectors; i++) {
        /* perfectly done */
        if (end == cur && found_start) {
                   ret = 0;
                   break;
        }
        /* would erase past end */
        else if (end < cur + bank_sectors.au16SectorSizes[i] * 1024) {
            ret = -2;
            break;
        }
        else if (address <= cur) {
            if (address == cur) {
                found_start = 1;
            }
            /* start address doesn't align with start of real sector */
            if (!found_start && address < cur) {
                ret = -3;
                break;
            }

            ret = f021_flash_erase(cur);
            if (ret != 0) {
                break;
            }
        }
        else {
            /* intentionally fall through as loop hasn't found the start */
        }

        cur += bank_sectors.au16SectorSizes[i] * 1024;
    }

    return ret;
}
