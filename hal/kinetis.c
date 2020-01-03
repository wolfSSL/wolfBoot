/* kinetis.c
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
#include "fsl_flash.h"
#include "fsl_ftfx_cache.h"
#include "fsl_sysmpu.h"

#if defined(CPU_MK82FN256VLL15) && defined(FREESCALE_USE_LTC)
#include <wolfssl/wolfcrypt/port/nxp/ksdk_port.h>
#endif

static flash_config_t pflash;
static ftfx_cache_config_t pcache;
static int flash_init = 0;

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot Kinetis HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

#ifdef __WOLFBOOT

static void CLOCK_CONFIG_SetFllExtRefDiv(uint8_t frdiv)
{
    MCG->C1 = ((MCG->C1 & ~MCG_C1_FRDIV_MASK) | MCG_C1_FRDIV(frdiv));
}

static void do_flash_init(void);

/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

/* This are the registers for the NV flash configuration area.
 * Access these field by setting the relative flags in NV_Flash_Config.
 */
#define NVTYPE_LEN (16)

const uint8_t __attribute__((section(".flash_config"))) NV_Flash_Config[NVTYPE_LEN] = {
    /* Backdoor comparison key (2 words) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,

    /* P-Flash protection 1 */
    0xFF, 0xFF,
    /* P-Flash protection 2 */
    0xFF, 0xFF,

    /* Flash security register */
    ((0xFE)),
    /* Flash option register */
    0xFF,
    /* EERAM protection register */
    0xFF,
    /* D-Flash protection register */
    0xFF
};

#if defined(CPU_MK82FN256VLL15)
struct stage1_config
{
    uint32_t tag;
    uint32_t crcStartAddress;
    uint32_t crcByteCount;
    uint32_t crcExpectedValue;
    uint8_t  enabledPeripherals;
    uint8_t  i2cSlaveAddress;
    uint16_t peripheralDetectionTimeoutMs;
    uint16_t usbVid;
    uint16_t usbPid;
    uint32_t usbStringsPointer;
    uint8_t  clockFlags;
    uint8_t  clockDivider;
    uint8_t  bootFlags;
    uint8_t  RESERVED1;
    uint32_t mmcauConfigPointer;
    uint32_t keyBlobPointer;
    uint8_t  RESERVED2[8];
    uint32_t qspiConfigBlockPtr;
    uint8_t  RESERVED3[12];
};

const struct stage1_config __attribute__((section(".stage1_config")))
 NV_Stage1_Config = {
    .tag = 0x6766636BU,                      /* Magic Number */
    .crcStartAddress = 0xFFFFFFFFU,          /* Disable CRC check */
    .crcByteCount = 0xFFFFFFFFU,             /* Disable CRC check */
    .crcExpectedValue = 0xFFFFFFFFU,         /* Disable CRC check */
    .enabledPeripherals = 0x17,              /* Enable all peripherals */
    .i2cSlaveAddress = 0xFF,                 /* Use default I2C address */
    .peripheralDetectionTimeoutMs = 0x01F4U, /* Use default timeout */
    .usbVid = 0xFFFFU,                       /* Use default USB Vendor ID */
    .usbPid = 0xFFFFU,                       /* Use default USB Product ID */
    .usbStringsPointer = 0xFFFFFFFFU,        /* Use default USB Strings */
    .clockFlags = 0x01,                      /* Enable High speed mode */
    .clockDivider = 0xFF,                    /* Use clock divider 1 */
    .bootFlags = 0x01,                       /* Enable communication with host */
    .mmcauConfigPointer = 0xFFFFFFFFU,       /* No MMCAU configuration */
    .keyBlobPointer = 0x000001000,           /* keyblob data is at 0x1000 */
    .qspiConfigBlockPtr = 0xFFFFFFFFU        /* No QSPI configuration */
};
#endif


#define MCG_PLL_DISABLE                                   0U  /*!< MCGPLLCLK disabled */
#define OSC_CAP0P                                         0U  /*!< Oscillator 0pF capacitor load */
#define OSC_ER_CLK_DISABLE                                0U  /*!< Disable external reference clock */
#define SIM_OSC32KSEL_RTC32KCLK_CLK                       2U  /*!< OSC32KSEL select: RTC32KCLK clock (32.768kHz) */
#define SIM_PLLFLLSEL_IRC48MCLK_CLK                       3U  /*!< PLLFLL select: IRC48MCLK clock */
#define SIM_PLLFLLSEL_MCGPLLCLK_CLK                       1U  /*!< PLLFLL select: MCGPLLCLK clock */
#define SIM_CLKDIV1_RUN_MODE_MAX_CORE_DIV 1U    /*!< SIM CLKDIV1 maximum run mode core/system divider configurations */
#define SIM_CLKDIV1_RUN_MODE_MAX_BUS_DIV 3U     /*!< SIM CLKDIV1 maximum run mode bus divider configurations */
#define SIM_CLKDIV1_RUN_MODE_MAX_FLEXBUS_DIV 3U /*!< SIM CLKDIV1 maximum run mode flexbus divider configurations */
#define SIM_CLKDIV1_RUN_MODE_MAX_FLASH_DIV 7U   /*!< SIM CLKDIV1 maximum run mode flash divider configurations */

static void CLOCK_CONFIG_FllStableDelay(void)
{
    uint32_t i = 30000U;
    while (i--)
    {
        __NOP();
    }
}

const mcg_config_t mcgConfig_BOARD_BootClockRUN =
{
    .mcgMode = kMCG_ModePEE,                  /* PEE - PLL Engaged External */
    .irclkEnableMode = kMCG_IrclkEnable,      /* MCGIRCLK enabled, MCGIRCLK disabled in STOP mode */
    .ircs = kMCG_IrcSlow,                     /* Slow internal reference clock selected */
    .fcrdiv = 0x0U,                           /* Fast IRC divider: divided by 1 */
    .frdiv = 0x0U,                            /* FLL reference clock divider: divided by 32 */
    .drs = kMCG_DrsLow,                       /* Low frequency range */
    .dmx32 = kMCG_Dmx32Default,               /* DCO has a default range of 25% */
    .oscsel = kMCG_OscselOsc,                 /* Selects System Oscillator (OSCCLK) */
#if defined(CPU_MK64FN1M0VLL12)
    .pll0Config =
    {
        .enableMode = MCG_PLL_DISABLE,    /* MCGPLLCLK disabled */
        .prdiv = 0x13U,                   /* PLL Reference divider: divided by 20 */
        .vdiv = 0x18U,                    /* VCO divider: multiplied by 48 */
    },
#elif defined(CPU_MK82FN256VLL15)
    .pll0Config =
    {
        .enableMode = MCG_PLL_DISABLE, /* MCGPLLCLK disabled */
        .prdiv = 0x0U,                 /* PLL Reference divider: divided by 1 */
        .vdiv = 0x9U,                  /* VCO divider: multiplied by 25 */
    },
#else
#       error("The selected Kinetis MPU does not have a clock line configuration. Please edit hal/kinetis.c")
#endif

};

#if defined(CPU_MK64FN1M0VLL12)
const sim_clock_config_t simConfig_BOARD_BootClockRUN =
{
    .pllFllSel = SIM_PLLFLLSEL_MCGPLLCLK_CLK, /* PLLFLL select: MCGPLLCLK clock */
    .er32kSrc = SIM_OSC32KSEL_RTC32KCLK_CLK,  /* OSC32KSEL select: RTC32KCLK clock (32.768kHz) */
    .clkdiv1 = 0x1240000U,                    /* SIM_CLKDIV1 - OUTDIV1: /1, OUTDIV2: /2, OUTDIV3: /3, OUTDIV4: /5 */
};

const osc_config_t oscConfig_BOARD_BootClockRUN =
{
    .freq = 50000000U,                        /* Oscillator frequency: 50000000Hz */
    .capLoad = (OSC_CAP0P),                   /* Oscillator capacity load: 0pF */
    .workMode = kOSC_ModeExt,                 /* Use external clock */
    .oscerConfig =
    {
        .enableMode = kOSC_ErClkEnable,   /* Enable external reference clock, disable external reference clock in STOP mode */
    }
};

#elif defined(CPU_MK82FN256VLL15)

const sim_clock_config_t simConfig_BOARD_BootClockRUN = {
    .pllFllSel = SIM_PLLFLLSEL_MCGPLLCLK_CLK, /* PLLFLL select: MCGPLLCLK clock */
    .pllFllDiv = 0,                           /* PLLFLLSEL clock divider divisor: divided by 1 */
    .pllFllFrac = 0,                          /* PLLFLLSEL clock divider fraction: multiplied by 1 */
    .er32kSrc = SIM_OSC32KSEL_RTC32KCLK_CLK,  /* OSC32KSEL select: RTC32KCLK clock (32.768kHz) */
    .clkdiv1 = 0x1150000U,                    /* SIM_CLKDIV1 - OUTDIV1: /1, OUTDIV2: /2, OUTDIV3: /2, OUTDIV4: /6 */
};

const osc_config_t oscConfig_BOARD_BootClockRUN = {
    .freq = 12000000U,                /* Oscillator frequency: 12000000Hz */
    .capLoad = (OSC_CAP0P),           /* Oscillator capacity load: 0pF */
    .workMode = kOSC_ModeOscLowPower, /* Oscillator low power */
    .oscerConfig = {
        .enableMode =
            kOSC_ErClkEnable, /* Enable external reference clock, disable external reference clock in STOP mode */
        .erclkDiv = 0,        /* Divider for OSCERCLK: divided by 1 */
    }
};
#endif


void hal_init(void)
{
    /* Disable MPU */
    SYSMPU_Enable(SYSMPU, false);
#if defined(CPU_MK82FN256VLL15) && defined(FREESCALE_USE_LTC)
    ksdk_port_init();
#endif

    /* Set the system clock dividers in SIM to safe value. */
#if defined(CPU_MK64FN1M0VLL12)
    CLOCK_SetSimSafeDivs();
#elif defined(CPU_MK82FN256VLL15)
    CLOCK_SetOutDiv(SIM_CLKDIV1_RUN_MODE_MAX_CORE_DIV, SIM_CLKDIV1_RUN_MODE_MAX_BUS_DIV,
                    SIM_CLKDIV1_RUN_MODE_MAX_FLEXBUS_DIV, SIM_CLKDIV1_RUN_MODE_MAX_FLASH_DIV);
#endif
    /* Initializes OSC0 according to board configuration. */
    CLOCK_InitOsc0(&oscConfig_BOARD_BootClockRUN);
    CLOCK_SetXtal0Freq(oscConfig_BOARD_BootClockRUN.freq);
    /* Configure the Internal Reference clock (MCGIRCLK). */
    CLOCK_SetInternalRefClkConfig(mcgConfig_BOARD_BootClockRUN.irclkEnableMode,
            mcgConfig_BOARD_BootClockRUN.ircs,
            mcgConfig_BOARD_BootClockRUN.fcrdiv);
    /* Configure FLL external reference divider (FRDIV). */
    CLOCK_CONFIG_SetFllExtRefDiv(mcgConfig_BOARD_BootClockRUN.frdiv);
    /* Set MCG to PEE mode. */
    CLOCK_BootToPeeMode(mcgConfig_BOARD_BootClockRUN.oscsel,
            kMCG_PllClkSelPll0,
            &mcgConfig_BOARD_BootClockRUN.pll0Config);
    /* Set the clock configuration in SIM module. */
    CLOCK_SetSimConfig(&simConfig_BOARD_BootClockRUN);
    do_flash_init();
}
#if 0
void BOARD_BootClockHSRUN(void)
{
    /* In HSRUN mode, the maximum allowable change in frequency of the system/bus/core/flash is
    * restricted to x2, to follow this restriction, enter HSRUN mode should follow:
 * 1.set CLKDIV1 to safe divider value.
    * 2.set the PLL or FLL output target frequency for HSRUN mode.
    * 3.switch to HSRUN mode.
    * 4.switch to HSRUN mode target requency value.
    */

    /* Set the system clock dividers in SIM to safe value. */
    CLOCK_SetOutDiv(SIM_CLKDIV1_RUN_MODE_MAX_CORE_DIV, SIM_CLKDIV1_RUN_MODE_MAX_BUS_DIV,
                    SIM_CLKDIV1_RUN_MODE_MAX_FLEXBUS_DIV, SIM_CLKDIV1_RUN_MODE_MAX_FLASH_DIV);
    /* Initializes OSC0 according to board configuration. */
    CLOCK_InitOsc0(&oscConfig_BOARD_BootClockHSRUN);
    CLOCK_SetXtal0Freq(oscConfig_BOARD_BootClockHSRUN.freq);
    /* Configure the Internal Reference clock (MCGIRCLK). */
    CLOCK_SetInternalRefClkConfig(mcgConfig_BOARD_BootClockHSRUN.irclkEnableMode, mcgConfig_BOARD_BootClockHSRUN.ircs,
                                  mcgConfig_BOARD_BootClockHSRUN.fcrdiv);
    /* Configure FLL external reference divider (FRDIV). */
    CLOCK_CONFIG_SetFllExtRefDiv(mcgConfig_BOARD_BootClockHSRUN.frdiv);
    /* Set MCG to PEE mode. */
    CLOCK_BootToPeeMode(mcgConfig_BOARD_BootClockHSRUN.oscsel, kMCG_PllClkSelPll0,
                        &mcgConfig_BOARD_BootClockHSRUN.pll0Config);

    /* Set HSRUN power mode */
    SMC_SetPowerModeProtection(SMC, kSMC_AllowPowerModeAll);
    SMC_SetPowerModeHsrun(SMC);
    while (SMC_GetPowerModeState(SMC) != kSMC_PowerStateHsrun)
    {
    }

    /* Set the clock configuration in SIM module. */
    CLOCK_SetSimConfig(&simConfig_BOARD_BootClockHSRUN);
    /* Set SystemCoreClock variable. */
    SystemCoreClock = BOARD_BOOTCLOCKHSRUN_CORE_CLOCK;
}
#endif

void hal_prepare_boot(void)
{
}


#endif

static void do_flash_init(void)
{
    if (flash_init)
        return;
    flash_init++;
    memset(&pflash, 0, sizeof(pflash));
    memset(&pcache, 0, sizeof(pcache));
    FLASH_Init(&pflash);
    FTFx_CACHE_Init(&pcache);
    FTFx_CACHE_ClearCachePrefetchSpeculation(&pcache, 1);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int w = 0;
    int ret;
    const uint8_t empty_dword[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    do_flash_init();

    while (len > 0) {
        if ((len < 8) || address & 0x07) {
            uint8_t aligned_dword[8];
            uint32_t address_align = address - (address & 0x07);
            uint32_t start_off = address - address_align;
            int i;
            memcpy(aligned_dword, address_align, 8);
            for (i = start_off; ((i < 8) && (i < len + start_off)); i++)
                aligned_dword[i] = data[w++];
            if (memcmp(aligned_dword, empty_dword, 8) != 0) {
                    ret = FLASH_Program(&pflash, address_align, aligned_dword, 8);
                if (ret != kStatus_FTFx_Success)
                    return -1;
            }
            address += i;
            len -= i;
        } else {
            uint32_t len_align = len - (len & 0x07);
            ret = FLASH_Program(&pflash, address, data + w, len_align);
            if (ret != kStatus_FTFx_Success)
                return -1;
            len -= len_align;
            address += len_align;
        }
    }
    FTFx_CACHE_ClearCachePrefetchSpeculation(&pcache, 1);
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
    int idx = 0;
    do_flash_init();
    do {
        if (FLASH_Erase(&pflash, address + WOLFBOOT_SECTOR_SIZE * idx, WOLFBOOT_SECTOR_SIZE, kFTFx_ApiEraseKey) != kStatus_FTFx_Success)
            return -1;
        len -= WOLFBOOT_SECTOR_SIZE;
        idx++;
    } while (len > 0);
    FTFx_CACHE_ClearCachePrefetchSpeculation(&pcache, 1);
    return 0;
}



