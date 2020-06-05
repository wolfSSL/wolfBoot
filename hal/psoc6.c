/* psoc6.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
#include <target.h>
#include <string.h>
#include "image.h"

#include "cy_device_headers.h"

#include "cy_flash.h"
#include "cy_syspm.h"
#include "cy_sysclk.h"
#include "cy_syslib.h"
#include "cy_ipc_drv.h"
#ifdef WOLFSSL_PSOC6_CRYPTO
#include "wolfssl/wolfcrypt/port/cypress/psoc6_crypto.h"
#endif

#include "psoc6_02_config.h"

#define ROW_SIZE (WOLFBOOT_SECTOR_SIZE)
#define FLASH_BASE_ADDRESS (0x10000000)
#define CPU_FREQ (100000000)

uint8_t psoc6_write_buffer[ROW_SIZE];

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot psoc6 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

#ifdef __WOLFBOOT
static const cy_stc_pll_manual_config_t srss_0_clock_0_pll_0_pllConfig =
{
    .feedbackDiv = 100,
    .referenceDiv = 2,
    .outputDiv = 4,
    .lfMode = false,
    .outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO,
};

static void hal_set_pll(void)
{
    /*Set clock path 1 source to IMO, this feeds PLL1*/
    Cy_SysClk_ClkPathSetSource(1U, CY_SYSCLK_CLKPATH_IN_IMO);

    /*Set the input for CLK_HF0 to the output of the PLL, which is on clock path 1*/
    Cy_SysClk_ClkHfSetSource(0U, CY_SYSCLK_CLKHF_IN_CLKPATH1);
    Cy_SysClk_ClkHfSetDivider(0U, CY_SYSCLK_CLKHF_NO_DIVIDE);

    /*Set divider for CM4 clock to 0, might be able to lower this to save power if needed*/
    Cy_SysClk_ClkFastSetDivider(0U);

    /*Set divider for peripheral and CM0 clock to 0 - This must be 0 to get fastest clock to CM0*/
    Cy_SysClk_ClkPeriSetDivider(0U);

    /*Set divider for CM0 clock to 0*/
    Cy_SysClk_ClkSlowSetDivider(0U);

    /*Set flash memory wait states */
    Cy_SysLib_SetWaitStates(false, 100);

    /*Configure PLL for 100 MHz*/
    if (CY_SYSCLK_SUCCESS != Cy_SysClk_PllManualConfigure(1U, &srss_0_clock_0_pll_0_pllConfig))
    {
        while(1)
            ;
    }
    /*Enable PLL*/
    if (CY_SYSCLK_SUCCESS != Cy_SysClk_PllEnable(1U, 10000u))
    {
        while(1)
            ;
    }
}



void hal_init(void)
{
#define VTOR (*(volatile uint32_t *)(0xE000ED08))
    VTOR = FLASH_BASE_ADDRESS;
#undef VTOR

    Cy_PDL_Init(CY_DEVICE_CFG);
    Cy_Flash_Init();
    hal_set_pll();
#ifdef WOLFSSL_PSOC6_CRYPTO
    psoc6_crypto_port_init();
#endif
}

void hal_prepare_boot(void)
{
}

#endif


/* Only Row-aligned writes allowed. This is guaranteed by wolfBoot if NVM_CACHE is
 * in use (via NVM_FLASH_WRITEONCE=1), as unaligned writes become cached.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    const uint8_t *src = data;
    int ret;
    if (len < ROW_SIZE)
        return -1;
    if ((((uint32_t)data) & FLASH_BASE_ADDRESS) == FLASH_BASE_ADDRESS) {
        if (len != ROW_SIZE) {
            return -1;
        }
        memcpy(psoc6_write_buffer, data, len);
        src = psoc6_write_buffer;
    }
    while (len) {
        ret = Cy_Flash_ProgramRow(address, (const uint32_t *) src);
        if (ret)
            return ret;
        len -= ROW_SIZE;
        if ((len > 0) && (len < ROW_SIZE))
            return -1;
    }
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
    uint32_t end_address;
    int ret;
    uint32_t p = (uint32_t)address;
    if (len == 0)
        return -1;
    end_address = address + len;
    while ((end_address - p) >= ROW_SIZE) {
        ret = Cy_Flash_EraseRow(p);
        if (ret)
            return ret;
        p += ROW_SIZE;
    }
    return 0;
}

