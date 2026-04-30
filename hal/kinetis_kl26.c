/* kinetis_kl26.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
#include <string.h>
#include <target.h>
#include "image.h"
#include "fsl_common.h"
#include "fsl_flash.h"
#include "fsl_lpsci.h"
#include "fsl_port.h"

static flash_config_t pflash;
static int flash_init_done = 0;

/* UART driver forward declarations - implementation at end of file */
int  uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop);
int  uart_tx(const uint8_t c);
int  uart_rx(uint8_t *c, int len);
#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len);
#endif

#ifndef NVM_FLASH_WRITEONCE
#   error "wolfBoot Kinetis KL26 HAL: no WRITEONCE support detected. Please define NVM_FLASH_WRITEONCE"
#endif

#ifdef __WOLFBOOT

/* Assert hook needed by Kinetis SDK */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    while (1)
        ;
}

/* Flash configuration field (FSEC/FOPT). Placed at 0x400 by the linker. */
#define NVTYPE_LEN (16)
const uint8_t __attribute__((section(".flash_config"))) NV_Flash_Config[NVTYPE_LEN] = {
    /* Backdoor comparison key (2 words) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* P-Flash protection 1/2 */
    0xFF, 0xFF, 0xFF, 0xFF,
    /* Flash security register: unsecured */
    0xFE,
    /* Flash option register */
    0xFF,
    /* EERAM protection register */
    0xFF,
    /* D-Flash protection register */
    0xFF
};

/* Clock configuration for FRDM-KL26Z: PEE mode, 48 MHz core / 24 MHz bus,
 * driven by the 8 MHz OSC0 reference. */
#define MCG_PLL_DISABLE          0U
#define OSC_CAP0P                0U
#define SIM_OSC32KSEL_LPO_CLK    3U
#define SIM_PLLFLLSEL_MCGPLLCLK  1U

static void CLOCK_CONFIG_SetFllExtRefDiv(uint8_t frdiv)
{
    MCG->C1 = ((MCG->C1 & ~MCG_C1_FRDIV_MASK) | MCG_C1_FRDIV(frdiv));
}

static const mcg_config_t mcgConfig_BOARD_BootClockRUN = {
    .mcgMode = kMCG_ModePEE,
    .irclkEnableMode = kMCG_IrclkEnable,
    .ircs = kMCG_IrcSlow,
    .fcrdiv = 0x0U,
    .frdiv = 0x0U,
    .drs = kMCG_DrsLow,
    .dmx32 = kMCG_Dmx32Default,
    .pll0Config = {
        .enableMode = MCG_PLL_DISABLE,
        .prdiv = 0x1U, /* /2 */
        .vdiv = 0x0U,  /* x24 -> 8MHz/2*24 = 96MHz VCO, /2 -> 48MHz */
    },
};

static const sim_clock_config_t simConfig_BOARD_BootClockRUN = {
    .pllFllSel = SIM_PLLFLLSEL_MCGPLLCLK,
    .er32kSrc  = SIM_OSC32KSEL_LPO_CLK,
    .clkdiv1   = 0x10010000U, /* OUTDIV1: /2, OUTDIV4: /2 */
};

static const osc_config_t oscConfig_BOARD_BootClockRUN = {
    .freq = 8000000U,
    .capLoad = OSC_CAP0P,
    .workMode = kOSC_ModeOscLowPower,
    .oscerConfig = {
        .enableMode = kOSC_ErClkEnable,
    },
};

static void do_flash_init(void);

void hal_init(void)
{
    /* Disable the COP watchdog */
    *((volatile uint32_t *)0x40048100) = 0;

    CLOCK_SetSimSafeDivs();
    CLOCK_InitOsc0(&oscConfig_BOARD_BootClockRUN);
    CLOCK_SetXtal0Freq(oscConfig_BOARD_BootClockRUN.freq);
    CLOCK_CONFIG_SetFllExtRefDiv(mcgConfig_BOARD_BootClockRUN.frdiv);
    CLOCK_BootToPeeMode(kMCG_OscselOsc,
                        kMCG_PllClkSelPll0,
                        &mcgConfig_BOARD_BootClockRUN.pll0Config);
    CLOCK_SetInternalRefClkConfig(mcgConfig_BOARD_BootClockRUN.irclkEnableMode,
                                  mcgConfig_BOARD_BootClockRUN.ircs,
                                  mcgConfig_BOARD_BootClockRUN.fcrdiv);
    CLOCK_SetSimConfig(&simConfig_BOARD_BootClockRUN);
    do_flash_init();

#ifdef DEBUG_UART
    uart_init(115200, 8, 'N', 1);
    uart_write("wolfBoot KL26 init\r\n", 20);
#endif
}

void hal_prepare_boot(void)
{
}

#endif /* __WOLFBOOT */

static void do_flash_init(void)
{
    if (flash_init_done)
        return;
    flash_init_done++;
    memset(&pflash, 0, sizeof(pflash));
    FLASH_Init(&pflash);
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int w = 0;
    const uint8_t empty_word[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    do_flash_init();

    while (len > 0) {
        if ((len < 4) || (address & 0x03)) {
            uint8_t aligned_word[4];
            uint32_t address_align = address & ~0x03U;
            uint32_t start_off = address - address_align;
            uint32_t i;
            memcpy(aligned_word, (const void*)address_align, 4);
            for (i = start_off; (i < 4) && (i < start_off + (uint32_t)len); i++)
                aligned_word[i] = data[w++];
            if (memcmp(aligned_word, empty_word, 4) != 0) {
                if (FLASH_Program(&pflash, address_align,
                                  (uint32_t *)aligned_word, 4)
                    != kStatus_FLASH_Success)
                    return -1;
            }
            address = address_align + i;
            len -= (int)(i - start_off);
        } else {
            uint32_t len_align = (uint32_t)len & ~0x03U;
            if (FLASH_Program(&pflash, address,
                              (uint32_t *)(data + w), len_align)
                != kStatus_FLASH_Success)
                return -1;
            w += (int)len_align;
            address += len_align;
            len -= (int)len_align;
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
    int idx = 0;
    do_flash_init();
    do {
        if (FLASH_Erase(&pflash, address + WOLFBOOT_SECTOR_SIZE * idx,
                        WOLFBOOT_SECTOR_SIZE, kFLASH_ApiEraseKey)
            != kStatus_FLASH_Success)
            return -1;
        len -= WOLFBOOT_SECTOR_SIZE;
        idx++;
    } while (len > 0);
    return 0;
}

/* UART driver (FRDM-KL26Z OpenSDA virtual COM port)
 * Pinout: TX = A2 (alt 2), RX = A1 (alt 2). */

#define LPSCI_SRC_PLLFLLCLK 1U

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    lpsci_config_t cfg;

    CLOCK_EnableClock(kCLOCK_PortA);
    PORT_SetPinMux(PORTA, 1U, kPORT_MuxAlt2); /* UART0_RX */
    PORT_SetPinMux(PORTA, 2U, kPORT_MuxAlt2); /* UART0_TX */

    CLOCK_SetLpsci0Clock(LPSCI_SRC_PLLFLLCLK);

    LPSCI_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = bitrate;
    cfg.enableTx     = true;
    cfg.enableRx     = true;
    switch (parity) {
        case 'E': cfg.parityMode = kLPSCI_ParityEven;     break;
        case 'O': cfg.parityMode = kLPSCI_ParityOdd;      break;
        default:  cfg.parityMode = kLPSCI_ParityDisabled; break;
    }
#if defined(FSL_FEATURE_LPSCI_HAS_STOP_BIT_CONFIG_SUPPORT) && \
    FSL_FEATURE_LPSCI_HAS_STOP_BIT_CONFIG_SUPPORT
    cfg.stopBitCount = (stop > 1U) ? kLPSCI_TwoStopBit : kLPSCI_OneStopBit;
#else
    (void)stop;
#endif
    (void)data;

    return (LPSCI_Init(UART0, &cfg, CLOCK_GetPllFllSelClkFreq())
            == kStatus_Success) ? 0 : -1;
}

int uart_tx(const uint8_t c)
{
    while ((LPSCI_GetStatusFlags(UART0) & kLPSCI_TxDataRegEmptyFlag) == 0)
        ;
    LPSCI_WriteByte(UART0, c);
    return 1;
}

int uart_rx(uint8_t *c, int len)
{
    (void)len;
    if (LPSCI_GetStatusFlags(UART0) & kLPSCI_RxDataRegFullFlag) {
        *c = LPSCI_ReadByte(UART0);
        return 1;
    }
    return 0;
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    if (len > 0)
        LPSCI_WriteBlocking(UART0, (const uint8_t *)buf, len);
}
#endif
