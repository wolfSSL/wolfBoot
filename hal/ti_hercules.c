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

#ifndef CORTEX_R5
#   error "wolfBoot TI Hercules HAL: wrong architecture selected. Please compile with TARGET=ti_hercules."
#endif

// used for dynamic memory allocation
uint32_t __SYSMEM_SIZE = 0x800;


#include <F021.h>

void flash_demo (void) {
  Fapi_LibraryInfoType libinfo = Fapi_getLibraryInfo();
  Fapi_DeviceInfoType devinfo = Fapi_getDeviceInfo();

  Fapi_FlashBankSectorsType bankSectors;
  Fapi_StatusType st;
  uint32_t *addr = 0;

  st = Fapi_getBankSectors(Fapi_FlashBank1,
                            &bankSectors);
  printf("st: %d\n", st);
  addr = bankSectors.u32BankStartAddress;

  st = Fapi_initializeFlashBanks(134);
  printf("st: %d\n", st);


  st = Fapi_setActiveFlashBank(Fapi_FlashBank1);
  printf("st: %d\n", st);

  st = Fapi_enableMainBankSectors(1);
  printf("st: %d\n", st);

  while(FAPI_CHECK_FSM_READY_BUSY != Fapi_Status_FsmReady)
    ;

  st = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, addr);
  printf("st: %d\n", st);


  while(FAPI_CHECK_FSM_READY_BUSY == Fapi_Status_FsmBusy)
    ;

  if(FAPI_GET_FSM_STATUS != 0) {
    printf("failed to erase\n");
  }

  uint8_t data[] = "wolfSSL";
  st = Fapi_issueProgrammingCommand(addr,
                                    data,
                                    sizeof(data),
                                    NULL,
                                    0,
                                    Fapi_DataOnly);
  printf("st: %d\n", st);


  while(FAPI_CHECK_FSM_READY_BUSY == Fapi_Status_FsmBusy)
    ;

  if(FAPI_GET_FSM_STATUS != 0) {
    printf("failed to program\n");
  }


}

/* public HAL functions */
void hal_init(void)
{
  flash_demo();
}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    return 0;
}
