/* mcxw.c
 *
 * Stubs for custom HAL implementation. Defines the 
 * functions used by wolfboot for a specific target.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <target.h>
#include "image.h"
/* FSL includes */
#include "fsl_common.h"

/* Clock + RAM voltage settings */
#include "fsl_clock.h"
#include "fsl_spc.h"

/* Flash driver */
#include "fsl_device_registers.h"
#include "fsl_flash_api.h"
#include "fsl_lpspi_flash.h"

/*!< Core clock frequency: 96000000Hz */
#define BOARD_BOOTCLOCKFRO96M_CORE_CLOCK 96000000UL
static flash_config_t pflash;


#ifdef __WOLFBOOT
/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

void BOARD_BootClockFRO96M(void)
{
    uint32_t coreFreq;
    scg_sys_clk_config_t curConfig;
    spc_active_mode_core_ldo_option_t ldoOption;

    /* Unlock FIRC, SIRC, ROSC and SOSC control status registers */
    CLOCK_UnlockFircControlStatusReg();
    CLOCK_UnlockSircControlStatusReg();
    CLOCK_UnlockRoscControlStatusReg();
    CLOCK_UnlockSysOscControlStatusReg();

    /* Get the CPU Core frequency */
    coreFreq = CLOCK_GetSysClkFreq(kSCG_SysClkCore);

    if (coreFreq <= BOARD_BOOTCLOCKRUN_CORE_CLOCK) {
        /* Set the LDO_CORE VDD regulator level */
        ldoOption.CoreLDOVoltage = kSPC_CoreLDO_NormalVoltage;
        ldoOption.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength;
        (void)SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldoOption);
        /* Configure Flash to support different voltage level and frequency */
        FMU0->FCTRL = (FMU0->FCTRL & ~((uint32_t)FMU_FCTRL_RWSC_MASK)) | (FMU_FCTRL_RWSC(0x2U));
        /* Specifies the operating voltage for the SRAM's read/write timing margin */
        SPC_SetSRAMOperateVoltage(SPC0, kSPC_SRAM_OperatVoltage1P1V);
    }

    /* Config 32k Crystal Oscillator */
    CCM32K_Set32kOscConfig(CCM32K, kCCM32K_Enable32kHzCrystalOsc, &g_ccm32kOscConfig_BOARD_BootClockRUN);
    /* Monitor is disabled */
    CLOCK_SetRoscMonitorMode(kSCG_RoscMonitorDisable);
    /* Wait for the 32kHz crystal oscillator to be stable */
    while ((CCM32K_GetStatusFlag(CCM32K) & CCM32K_STATUS_OSC32K_RDY_MASK) == 0UL)
    {
    }
    /* OSC32K clock output is selected as clock source */
    CCM32K_SelectClockSource(CCM32K, kCCM32K_ClockSourceSelectOsc32k);
    /* Disable the FRO32K clock */
    CCM32K_Enable32kFro(CCM32K, false);
    /* Wait for RTC Oscillator to be Valid */
    while (!CLOCK_IsRoscValid())
    {
    }

    CLOCK_SetXtal32Freq(BOARD_BOOTCLOCKRUN_ROSC_CLOCK);

    /* Init FIRC */
    CLOCK_CONFIG_FircSafeConfig(&g_scgFircConfig_BOARD_BootClockRUN);
    /* Set SCG to FIRC mode */
    CLOCK_SetRunModeSysClkConfig(&g_sysClkConfig_BOARD_BootClockRUN);
    /* Wait for clock source switch finished */
    do
    {
        CLOCK_GetCurSysClkConfig(&curConfig);
    } while (curConfig.src != g_sysClkConfig_BOARD_BootClockRUN.src);
    /* Initializes SOSC according to board configuration */
    (void)CLOCK_InitSysOsc(&g_scgSysOscConfig_BOARD_BootClockRUN);
    /* Set the XTAL0 frequency based on board settings */
    CLOCK_SetXtal0Freq(g_scgSysOscConfig_BOARD_BootClockRUN.freq);
    /* Init SIRC */
    (void)CLOCK_InitSirc(&g_scgSircConfig_BOARD_BootClockRUN);
    /* Set SystemCoreClock variable */
    SystemCoreClock = BOARD_BOOTCLOCKRUN_CORE_CLOCK;

    if (coreFreq > BOARD_BOOTCLOCKRUN_CORE_CLOCK) {
        /* Configure Flash to support different voltage level and frequency */
        FMU0->FCTRL = (FMU0->FCTRL & ~((uint32_t)FMU_FCTRL_RWSC_MASK)) | (FMU_FCTRL_RWSC(0x2U));
        /* Specifies the operating voltage for the SRAM's read/write timing margin */
        SPC_SetSRAMOperateVoltage(SPC0, kSPC_SRAM_OperatVoltage1P1V);
        /* Set the LDO_CORE VDD regulator level */
        ldoOption.CoreLDOVoltage = kSPC_CoreLDO_NormalVoltage;
        ldoOption.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength;
        (void)SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldoOption);
    }

    /* Set SCG CLKOUT selection. */
    CLOCK_CONFIG_SetScgOutSel(kClockClkoutSelScgSlow);
}

void hal_init(void)
{
    /* Clock setting  */
    BOARD_BootClockFRO96M();

    /* Flash driver init */
    flash_config_t pflash;

    /* Clear the FLASH configuration structure */
    memset(&pflash, 0, sizeof(pflash));
    /* FLASH driver init */
    FLASH_Init(&pflash);
}

void hal_prepare_boot(void)
{

}

#endif

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret;
    int w = 0;
    const uint8_t empty_qword[16] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    while (len > 0) {
        if ((len < 16) || address & 0x0F) {
            uint8_t aligned_qword[16];
            uint32_t address_align = address - (address & 0x0F);
            uint32_t start_off = address - address_align;
            int i;

            memcpy(aligned_qword, (void*)address_align, 16);
            for (i = start_off; ((i < 16) && (i < len + (int)start_off)); i++) {
                aligned_qword[i] = data[w++];
            }
            if (memcmp(aligned_qword, empty_qword, 16) != 0) {
                ret = FLASH_Program(&pflash, address_align, aligned_qword, 16);
                if (ret != kStatus_Success)
                    return -1;
            }
            address += i;
            len -= i;
        }
        else {
            uint32_t len_align = len - (len & 0x0F);
            ret = FLASH_Program(&pflash, address, (uint8_t*)data + w, len_align);
            if (ret != kStatus_Success)
                return -1;
            len -= len_align;
            address += len_align;
        }
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
    while ((address % 4) != 0)
        address --;
    if (FLASH_VerifyEraseSector(&pflash, address, len, kFLASH_ApiEraseKey) != kStatus_Success)
        return -1;
    return 0;
}

