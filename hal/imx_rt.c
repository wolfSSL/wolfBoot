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
#include "fsl_flexspi_nor_flash.h"
#include "imx_rt_nor.h"
#include "xip/fsl_flexspi_nor_boot.h"

#ifdef __WOLFBOOT

/** Built-in ROM API for bootloaders **/

typedef void rtwdog_config_t;
typedef void wdog_config_t;

/* Watchdog structures */
typedef struct
{
    void (*RTWDOG_GetDefaultConfig)(rtwdog_config_t *config);
    void (*RTWDOG_Init)(RTWDOG_Type *base, const rtwdog_config_t *config);
    void (*RTWDOG_Deinit)(RTWDOG_Type *base);
    void (*RTWDOG_Enable)(RTWDOG_Type *base);
    void (*RTWDOG_Disable)(RTWDOG_Type *base);
    void (*RTWDOG_EnableInterrupts)(RTWDOG_Type *base, uint32_t mask);
    void (*RTWDOG_DisableInterrupts)(RTWDOG_Type *base, uint32_t mask);
    uint32_t (*RTWDOG_GetStatusFlags)(RTWDOG_Type *base);
    void (*RTWDOG_ClearStatusFlags)(RTWDOG_Type *base, uint32_t mask);
    void (*RTWDOG_SetTimeoutValue)(RTWDOG_Type *base, uint16_t timeoutCount);
    void (*RTWDOG_SetWindowValue)(RTWDOG_Type *base, uint16_t windowValue);
    void (*RTWDOG_Unlock)(RTWDOG_Type *base);
    void (*RTWDOG_Refresh)(RTWDOG_Type *base);
    uint16_t (*RTWDOG_GetCounterValue)(RTWDOG_Type *base);
} rtwdog_driver_interface_t;

typedef struct
{
    void (*WDOG_GetDefaultConfig)(wdog_config_t *config);
    void (*WDOG_Init)(WDOG_Type *base, const wdog_config_t *config);
    void (*WDOG_Deinit)(WDOG_Type *base);
    void (*WDOG_Enable)(WDOG_Type *base);
    void (*WDOG_Disable)(WDOG_Type *base);
    void (*WDOG_EnableInterrupts)(WDOG_Type *base, uint16_t mask);
    uint16_t (*WDOG_GetStatusFlags)(WDOG_Type *base);
    void (*WDOG_ClearInterruptStatus)(WDOG_Type *base, uint16_t mask);
    void (*WDOG_SetTimeoutValue)(WDOG_Type *base, uint16_t timeoutCount);
    void (*WDOG_SetInterrputTimeoutValue)(WDOG_Type *base, uint16_t timeoutCount);
    void (*WDOG_DisablePowerDownEnable)(WDOG_Type *base);
    void (*WDOG_Refresh)(WDOG_Type *base);
} wdog_driver_interface_t;

/* Flex SPI op */
typedef enum _FlexSPIOperationType
{
    kFlexSpiOperation_Command, //!< FlexSPI operation: Only command, both TX and
    //! RX buffer are ignored.
    kFlexSpiOperation_Config, //!< FlexSPI operation: Configure device mode, the
    //! TX FIFO size is fixed in LUT.
    kFlexSpiOperation_Write, //!< FlexSPI operation: Write, only TX buffer is
    //! effective
    kFlexSpiOperation_Read, //!< FlexSPI operation: Read, only Rx Buffer is
    //! effective.
    kFlexSpiOperation_End = kFlexSpiOperation_Read,
} flexspi_operation_t;

/* FLEX SPI Xfer */
typedef struct _FlexSpiXfer
{
    flexspi_operation_t operation;
    uint32_t baseAddress;
    uint32_t seqId;
    uint32_t seqNum;
    bool isParallelModeEnable;
    uint32_t *txBuffer;
    uint32_t txSize;
    uint32_t *rxBuffer;
    uint32_t rxSize;
} flexspi_xfer_t;

/* Serial NOR config option */
typedef struct _serial_nor_config_option
{
    union
    {
        struct
        {
            uint32_t max_freq : 4; //!< Maximum supported Frequency
            uint32_t misc_mode : 4; //!< miscellaneous mode
            uint32_t quad_mode_setting : 4; //!< Quad mode setting
            uint32_t cmd_pads : 4; //!< Command pads
            uint32_t query_pads : 4; //!< SFDP read pads
            uint32_t device_type : 4; //!< Device type
            uint32_t option_size : 4; //!< Option size, in terms of uint32_t, size = (option_size + 1) * 4
            uint32_t tag : 4; //!< Tag, must be 0x0E
        } B;
        uint32_t U;
    } option0;
    union
    {
        struct
        {
            uint32_t dummy_cycles : 8; //!< Dummy cycles before read
            uint32_t reserved0 : 8; //!< Reserved for future use
            uint32_t pinmux_group : 4; //!< The pinmux group selection
            uint32_t reserved1 : 8; //!< Reserved for future use
            uint32_t flash_connection : 4; //!< Flash connection option: 0 - Single Flash connected to port A
        } B;
        uint32_t U;
    } option1;
} serial_nor_config_option_t;

/* NOR flash API */
typedef struct
{
    uint32_t version;
    status_t (*init)(uint32_t instance, flexspi_nor_config_t *config);
    status_t (*program)(uint32_t instance, flexspi_nor_config_t *config, uint32_t
            dst_addr, const uint32_t *src);
    status_t (*erase_all)(uint32_t instance, flexspi_nor_config_t *config);
    status_t (*erase)(uint32_t instance, flexspi_nor_config_t *config, uint32_t start,
            uint32_t lengthInBytes);
    status_t (*read)(
            uint32_t instance, flexspi_nor_config_t *config, uint32_t *dst, uint32_t addr,
            uint32_t lengthInBytes);
    void (*clear_cache)(uint32_t instance);
    status_t (*xfer)(uint32_t instance, flexspi_xfer_t *xfer);
    status_t (*update_lut)(uint32_t instance, uint32_t seqIndex, const uint32_t
            *lutBase, uint32_t seqNumber);
    status_t (*get_config)(uint32_t instance, flexspi_nor_config_t *config,
            serial_nor_config_option_t *option);
} flexspi_nor_driver_interface_t;


/* Root pointer */
typedef struct
{
    const uint32_t version;                 //!< Bootloader version number
    const char *copyright;                  //!< Bootloader Copyright
    void (*runBootloader)(void *arg);       //!< Function to start the bootloader executing
    const uint32_t *reserved0;              //!< Reserved
    const flexspi_nor_driver_interface_t *flexSpiNorDriver; //!< FlexSPI NOR Flash API
    const uint32_t *reserved1;              //!< Reserved
    const rtwdog_driver_interface_t *rtwdogDriver;
    const wdog_driver_interface_t *wdogDriver;
    const uint32_t *reserved2;
} bootloader_api_entry_t;

bootloader_api_entry_t *g_bootloaderTree;
flexspi_nor_config_t flexspi_config;


/** Flash configuration in the .flash_config section of flash **/

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


static int nor_flash_init(void);

static void clock_init(void)
{
    if (CCM_ANALOG->PLL_ARM & CCM_ANALOG_PLL_ARM_BYPASS_MASK)
    {
        // Configure ARM_PLL
        CCM_ANALOG->PLL_ARM =
            CCM_ANALOG_PLL_ARM_BYPASS(1) | CCM_ANALOG_PLL_ARM_ENABLE(1) | CCM_ANALOG_PLL_ARM_DIV_SELECT(24);
        // Wait Until clock is locked
        while ((CCM_ANALOG->PLL_ARM & CCM_ANALOG_PLL_ARM_LOCK_MASK) == 0)
        {
        }

        // Configure PLL_SYS
        CCM_ANALOG->PLL_SYS &= ~CCM_ANALOG_PLL_SYS_POWERDOWN_MASK;
        // Wait Until clock is locked
        while ((CCM_ANALOG->PLL_SYS & CCM_ANALOG_PLL_SYS_LOCK_MASK) == 0)
        {
        }

        // Configure PFD_528
        CCM_ANALOG->PFD_528 = CCM_ANALOG_PFD_528_PFD0_FRAC(24) | CCM_ANALOG_PFD_528_PFD1_FRAC(24) |
            CCM_ANALOG_PFD_528_PFD2_FRAC(19) | CCM_ANALOG_PFD_528_PFD3_FRAC(24);

        // Configure USB1_PLL
        CCM_ANALOG->PLL_USB1 =
            CCM_ANALOG_PLL_USB1_DIV_SELECT(0) | CCM_ANALOG_PLL_USB1_POWER(1) | CCM_ANALOG_PLL_USB1_ENABLE(1);
        while ((CCM_ANALOG->PLL_USB1 & CCM_ANALOG_PLL_USB1_LOCK_MASK) == 0)
        {
        }
        CCM_ANALOG->PLL_USB1 &= ~CCM_ANALOG_PLL_USB1_BYPASS_MASK;

        // Configure PFD_480
        CCM_ANALOG->PFD_480 = CCM_ANALOG_PFD_480_PFD0_FRAC(35) | CCM_ANALOG_PFD_480_PFD1_FRAC(35) |
            CCM_ANALOG_PFD_480_PFD2_FRAC(26) | CCM_ANALOG_PFD_480_PFD3_FRAC(15);

        // Configure Clock PODF
        CCM->CACRR = CCM_CACRR_ARM_PODF(1);

        CCM->CBCDR = (CCM->CBCDR & (~(CCM_CBCDR_SEMC_PODF_MASK | CCM_CBCDR_AHB_PODF_MASK | CCM_CBCDR_IPG_PODF_MASK))) |
            CCM_CBCDR_SEMC_PODF(2) | CCM_CBCDR_AHB_PODF(2) | CCM_CBCDR_IPG_PODF(2);

        // Configure FLEXSPI2 CLOCKS
        CCM->CBCMR =
            (CCM->CBCMR &
             (~(CCM_CBCMR_PRE_PERIPH_CLK_SEL_MASK | CCM_CBCMR_FLEXSPI2_CLK_SEL_MASK | CCM_CBCMR_FLEXSPI2_PODF_MASK))) |
            CCM_CBCMR_PRE_PERIPH_CLK_SEL(3) | CCM_CBCMR_FLEXSPI2_CLK_SEL(1) | CCM_CBCMR_FLEXSPI2_PODF(7);

        // Confgiure FLEXSPI CLOCKS
        CCM->CSCMR1 = ((CCM->CSCMR1 &
                    ~(CCM_CSCMR1_FLEXSPI_CLK_SEL_MASK | CCM_CSCMR1_FLEXSPI_PODF_MASK | CCM_CSCMR1_PERCLK_PODF_MASK |
                        CCM_CSCMR1_PERCLK_CLK_SEL_MASK)) |
                CCM_CSCMR1_FLEXSPI_CLK_SEL(3) | CCM_CSCMR1_FLEXSPI_PODF(7) | CCM_CSCMR1_PERCLK_PODF(1));

        // Finally, Enable PLL_ARM, PLL_SYS and PLL_USB1
        CCM_ANALOG->PLL_ARM &= ~CCM_ANALOG_PLL_ARM_BYPASS_MASK;
        CCM_ANALOG->PLL_SYS &= ~CCM_ANALOG_PLL_SYS_BYPASS_MASK;
        CCM_ANALOG->PLL_USB1 &= ~CCM_ANALOG_PLL_USB1_BYPASS_MASK;
    }
}


extern void ARM_MPU_Disable(void);
extern int wc_dcp_init(void);

void hal_init(void)
{
#ifdef WOLFSSL_IMXRT_DCP
    wc_dcp_init();
#endif
    ARM_MPU_Disable();
    g_bootloaderTree = (bootloader_api_entry_t *)*(uint32_t *)0x0020001c;
    clock_init();
    nor_flash_init();
}

void hal_prepare_boot(void)
{
}

#endif /* __WOLFBOOT */

static nor_handle_t norHandle = {NULL};
static serial_nor_config_option_t flexspi_cfg_option = {};

static int nor_flash_init(void)
{
    flexspi_cfg_option.option0.U = 0xC0000007; /* QuadSPI-NOR, f = default */
    g_bootloaderTree->flexSpiNorDriver->get_config(0, &flexspi_config, &flexspi_cfg_option);
    g_bootloaderTree->flexSpiNorDriver->init(0, &flexspi_config);
    return 0;
}

#define FLASH_PAGE_SIZE 0x100
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    status_t status;
    uint32_t wbuf[FLASH_PAGE_SIZE / 4];
    int i;
    for (i = 0; i < len; i+= FLASH_PAGE_SIZE) {
        memcpy(wbuf, data + i, FLASH_PAGE_SIZE);
        status = g_bootloaderTree->flexSpiNorDriver->program(0, &flexspi_config, (address + i) - FLASH_BASE, wbuf);
        if (kStatus_Success != status)
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
    status_t status;
    status = g_bootloaderTree->flexSpiNorDriver->erase(0, &flexspi_config, address - FLASH_BASE, len);
    if (status != kStatus_Success)
        return -1;
    return 0;
}

