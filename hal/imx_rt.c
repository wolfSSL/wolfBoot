/* imx-rt.c
 *
 * Custom HAL implementation. Defines the
 * functions used by wolfboot for a specific target.
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
#include "image.h"
#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include "fsl_nor_flash.h"
#include "fsl_flexspi.h"
//#include "evkmimxrt1060_flexspi_nor_config.h"
#include "fsl_flexspi_nor_flash.h"
#include "imx_rt_nor.h"
#include "xip/fsl_flexspi_nor_boot.h"

#define FLASH_PAGE_SIZE WOLFBOOT_SECTOR_SIZE


#ifdef __WOLFBOOT

const flexspi_nor_config_t __attribute__((section(".flash_config"))) qspiflash_config = {
    .memConfig =
    {
        .tag              = FLEXSPI_CFG_BLK_TAG,
        .version          = FLEXSPI_CFG_BLK_VERSION,
        .readSampleClkSrc = kFlexSPIReadSampleClk_LoopbackFromDqsPad,
        .csHoldTime       = 3u,
        .csSetupTime      = 3u,
        .sflashPadType    = kSerialFlash_4Pads,
        .serialClkFreq    = kFlexSpiSerialClk_100MHz,
        .sflashA1Size     = 8u * 1024u * 1024u,
        .lookupTable =
        {
            // Read LUTs
            FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0xEB, RADDR_SDR, FLEXSPI_4PAD, 0x18),
            FLEXSPI_LUT_SEQ(DUMMY_SDR, FLEXSPI_4PAD, 0x06, READ_SDR, FLEXSPI_4PAD, 0x04),
        },
    },
    .pageSize           = 256u,
    .sectorSize         = 4u * 1024u,
    .blockSize          = 64u * 1024u,
    .isUniformBlockSize = false,
};

#ifndef __FLASH_BASE
#   define __FLASH_BASE 0x60000000
#endif
#ifndef FLASH_BASE
#define FLASH_BASE __FLASH_BASE
#define FLASH_SIZE 0x800000
#define PLUGIN_FLAG 0x0UL
#endif

const uint8_t dcd_data[1] = {0};
extern void isr_reset(void);

const ivt __attribute__((section(".image_vt"))) image_vector_table = {
  IVT_HEADER,                         /* IVT Header */
  (uint32_t)isr_reset,               /* Image Entry Function */
  IVT_RSVD,                           /* Reserved = 0 */
  (uint32_t)dcd_data,                 /* Address where DCD information is stored */
  (uint32_t)&boot_data,               /* Address where BOOT Data Structure is stored */
  (uint32_t)&image_vector_table,      /* Pointer to IVT Self (absolute address */
  (uint32_t)CSF_ADDRESS,              /* Address where CSF file is stored */
  IVT_RSVD                            /* Reserved = 0 */
};

const BOOT_DATA_T __attribute__((section(".boot_data"))) boot_data = {
  FLASH_BASE,                 /* boot start location */
  FLASH_SIZE,                 /* size */
  PLUGIN_FLAG,                /* Plugin flag*/
  0xFFFFFFFF                  /* empty - extra data word */
};

/*******************************************************************************
 * Variables for BOARD_BootClockRUN configuration
 ******************************************************************************/
const clock_arm_pll_config_t armPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 100,                       /* PLL loop divider, Fout = Fin * 50 */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_sys_pll_config_t sysPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 1,                         /* PLL loop divider, Fout = Fin * ( 20 + loopDivider*2 + numerator / denominator ) */
        .numerator = 0,                           /* 30 bit numerator of fractional loop divider */
        .denominator = 1,                         /* 30 bit denominator of fractional loop divider */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_usb_pll_config_t usb1PllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 0,                         /* PLL loop divider, Fout = Fin * 20 */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };
const clock_video_pll_config_t videoPllConfig_BOARD_BootClockRUN =
    {
        .loopDivider = 31,                        /* PLL loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .postDivider = 8,                         /* Divider after PLL */
        .numerator = 0,                           /* 30 bit numerator of fractional loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .denominator = 1,                         /* 30 bit denominator of fractional loop divider, Fout = Fin * ( loopDivider + numerator / denominator ) */
        .src = 0,                                 /* Bypass clock source, 0 - OSC 24M, 1 - CLK1_P and CLK1_N */
    };


void hal_init(void)
{
}

void hal_prepare_boot(void)
{
}

#endif

flexspi_mem_config_t flexcfg = {
    .deviceConfig =
        {
            .flexspiRootClk       = 120000000,
            .flashSize            = FLASH_SIZE,
            .CSIntervalUnit       = kFLEXSPI_CsIntervalUnit1SckCycle,
            .CSInterval           = 2,
            .CSHoldTime           = 3,
            .CSSetupTime          = 3,
            .dataValidTime        = 0,
            .columnspace          = 0,
            .enableWordAddress    = 0,
            .AHBWriteWaitUnit     = kFLEXSPI_AhbWriteWaitUnit2AhbCycle,
            .AHBWriteWaitInterval = 0,
        },
    .devicePort      = kFLEXSPI_PortA1,
    .deviceType      = kSerialNorCfgOption_DeviceType_ReadSFDP_SDR,
    .quadMode        = kSerialNorQuadMode_NotConfig,
    .transferMode    = kSerialNorTransferMode_SDR,
    .enhanceMode     = kSerialNorEnhanceMode_Disabled,
    .commandPads     = kFLEXSPI_1PAD,
    .queryPads       = kFLEXSPI_1PAD,
    .statusOverride  = 0,
    .busyOffset      = 0,
    .busyBitPolarity = 0,

};

static nor_config_t norConfig = {
    .memControlConfig = &flexcfg,
    .driverBaseAddr   = FLEXSPI,
};

static nor_handle_t norHandle = {NULL};

void FLEXSPI_ClockInit();

static int nor_flash_init(void)
{
    status_t status;
    FLEXSPI_ClockInit();
    return Nor_Flash_Init(&norConfig, &norHandle);
}



int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /*
    status_t status;
    status = Nor_Flash_Page_Program(&norHandle, address, data);
    if (kStatus_Success != status)
        return -1;
        */
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
    /*
    uint32_t end = address + len - 1;
    uint32_t p;
    status_t status;
    for (p = address; p <= end; p += FLASH_PAGE_SIZE) {
        status = Nor_Flash_Erase_Sector(&norHandle, address);
        if (status != kStatus_Success)
            return -1;
    }
    */
    return 0;
}

