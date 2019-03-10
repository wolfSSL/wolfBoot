/* kinetis.c
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include "fsl_common.h"
#include "fsl_flash.h"
#include "fsl_ftfx_cache.h"

static flash_config_t pflash;
static ftfx_cache_config_t pcache;
static int flash_init = 0;

#ifdef __WOLFBOOT
#define CPU_CORE_CLOCK             120000000U

static void CLOCK_CONFIG_SetFllExtRefDiv(uint8_t frdiv)
{
    MCG->C1 = ((MCG->C1 & ~MCG_C1_FRDIV_MASK) | MCG_C1_FRDIV(frdiv));
}

static void do_flash_init(void);

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


/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    while(1)
        ;
}

#define MCG_PLL_DISABLE                                   0U  /*!< MCGPLLCLK disabled */
#define OSC_CAP0P                                         0U  /*!< Oscillator 0pF capacitor load */
#define OSC_ER_CLK_DISABLE                                0U  /*!< Disable external reference clock */
#define SIM_OSC32KSEL_RTC32KCLK_CLK                       2U  /*!< OSC32KSEL select: RTC32KCLK clock (32.768kHz) */
#define SIM_PLLFLLSEL_IRC48MCLK_CLK                       3U  /*!< PLLFLL select: IRC48MCLK clock */
#define SIM_PLLFLLSEL_MCGPLLCLK_CLK                       1U  /*!< PLLFLL select: MCGPLLCLK clock */

static void CLOCK_CONFIG_FllStableDelay(void)
{
    uint32_t i = 30000U;
    while (i--)
    {
        __NOP();
    }
}

/* Clock configuration for K64F */
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
    .pll0Config =
    {
        .enableMode = MCG_PLL_DISABLE,    /* MCGPLLCLK disabled */
        .prdiv = 0x13U,                   /* PLL Reference divider: divided by 20 */
        .vdiv = 0x18U,                    /* VCO divider: multiplied by 48 */
    },
};
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

void hal_init(void)
{
    // Disable Watchdog
    //  Write 0xC520 to watchdog unlock register
    *((volatile unsigned short *)0x4005200E) = 0xC520;
    //  Followed by 0xD928 to complete the unlock
    *((volatile unsigned short *)0x4005200E) = 0xD928;
    // Now disable watchdog via STCTRLH register
    *((volatile unsigned short *)0x40052000) = 0x01D2u;

    /* Set the system clock dividers in SIM to safe value. */
    CLOCK_SetSimSafeDivs();
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

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int w = 0;
    int ret;
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
            ret = FLASH_Program(&pflash, address_align, aligned_dword, 8);
            if (ret != kStatus_FTFx_Success)
                return -1;
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

void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}


int hal_flash_erase(uint32_t address, int len)
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



