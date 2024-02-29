/* same51.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include "image.h"
#include "hal.h"

/*
 * Clock settings for cpu same51 @ 120MHz
 */
#define CPU_FREQ (120000000)


/*
 * Flash settings for same51
 */
#define FLASH_SIZE          (1024 * 1024)
#define FLASH_PAGESIZE      512
#define FLASH_N_PAGES       4096


/*
 * Oscillator controller
 */
#define OSCCTRL_BASE         (0x40001000U)

/* Map only DPLL0 */
#define OSCCTRL_DPLL0CTRLA      *((volatile uint32_t *)(OSCCTRL_BASE + 0x30))
#define OSCCTRL_DPLL0RATIO      *((volatile uint32_t *)(OSCCTRL_BASE + 0x34))
#define OSCCTRL_DPLL0CTRLB      *((volatile uint32_t *)(OSCCTRL_BASE + 0x38))
#define OSCCTRL_DPLL0SYNCBUSY   *((volatile uint32_t *)(OSCCTRL_BASE + 0x3c))
#define OSCCTRL_DPLL0STATUS     *((volatile uint32_t *)(OSCCTRL_BASE + 0x40))

#define DPLL0CTRLA_ENABLE   (1u << 1)

#define DPLL0CTRLB_FILTER_MASK (0x0fu << 0)
#define DPLL0CTRLB_REFCLK_MASK (0x07u << 5)
#define DPLL0CTRLB_LTIME_MASK  (0x07u << 8)

#define DPLL0RATIO_LDR_MASK      (0x0fffu << 0)
#define DPLL0RATIO_LDRFRAC_MASK  (0xfu << 16)

#define DPLL0SYNCBUSY_ENABLE     (1u << 1)
#define DPLL0SYNCBUSY_RATIO      (1u << 2)

#define DPLL0STATUS_LOCK         (1u << 0)
#define DPLL0STATUS_CLKRDY       (1u << 1)

/*
 * 32KHz Oscillator controller
 */
#define OSC32KCTRL_BASE      (0x4001400U)
#define OSC32KCTRL_RTCCTRL     *((volatile uint32_t *)(OSC32KCTRL_BASE + 0x10))
#define RTCCTRL_RTCSEL_MASK  (0x03)

/*
 * Generic clock generator
 */
#define GCLK_BASE            (0x40001C00)
#define GCLK_CTRLA      *((volatile uint32_t *)(GCLK_BASE + 0x00))
#define GCLK_SYNCBUSY   *((volatile uint32_t *)(GCLK_BASE + 0x04))
#define CTRLA_SWRST     (1 << 0)
#define SYNCBUSY_SWRST  (1 << 0)
#define SYNCBUSY_GENCTRL(x) (1 << (2 + x))
#define GCLK_IS_BUSY(x) ((GCLK_SYNCBUSY & (SYNCBUSY_GENCTRL(x))) != 0)

/* Array of 12 GCLK_GENCTRL[x] 32-bit registers */
#define GCLK_GENCTRL ((volatile uint32_t *)(GCLK_BASE + 0x20))
#define GENCTRLx_SRC_DFLL  0x06u
#define GENCTRLx_SRC_PLL0  0x07u
#define GENCTRLx_GENEN     (1u << 8)
#define GENCTRLx_DIVSHIFT  (16)
#define GENCTRLx_SRC_MASK  (0x0Fu)
#define GENCTRLx_DIV_MASK  (0xFFFFu << 16)

/* Array of 48 GCLK_PCHCTRLx */
#define GCLK_PCHCTRL ((volatile uint32_t *)(GCLK_BASE + 0x80))
#define PCHCTRLx_CHEN   (1u << 6)

/* Main clock */
#define MCLK_BASE       (0x40000800)
#define MCLK_CPUDIV   *((volatile uint8_t *)(MCLK_BASE + 0x05))
#define MCLK_INTFLAG  *((volatile uint8_t *)(MCLK_BASE + 0x03))
#define MCLK_AHBMASK  *((volatile uint32_t *)(MCLK_BASE + 0x10))
#define MCLK_APBAMASK  *((volatile uint32_t *)(MCLK_BASE + 0x14))
#define MCLK_APBBMASK  *((volatile uint32_t *)(MCLK_BASE + 0x18))
#define MCLK_APBCMASK  *((volatile uint32_t *)(MCLK_BASE + 0x1c))
#define MCLK_APBDMASK  *((volatile uint32_t *)(MCLK_BASE + 0x20))


#define CKRDY (1u << 0)

/*
 * Watchdog controller
 */
#define WDT_CTRL *((volatile uint8_t *)(0x40002000))
#define WDT_EN (1u << 1)

/* Peripheral access control
 *
 */
#define PAC_BASE             (0x41000000)
#define PAC_WRCTRL          *((volatile uint32_t *)(PAC_BASE))
#define PAC_WRKEY_SET       (2 << 16U)
#define PAC_WRKEY_CLEAR     (1 << 16U)
#define PAC_PERID_NVMCTL    ((32 * 1) + 2)

#define PAC_WR

/*
 * NVM controller */

#define NVMCTRL_BASE           (0x41004000)
#define NVMCTRLA               *((volatile uint16_t *)(NVMCTRL_BASE))
#define NVMCTRLB               *((volatile uint32_t *)(NVMCTRL_BASE + 0x04))
#define NVMCTRL_INTFLAG        *((volatile uint16_t *)(NVMCTRL_BASE + 0x10))
#define NVMCTRL_STATUS         *((volatile uint16_t *)(NVMCTRL_BASE + 0x12))
#define NVMCTRL_ADDR           *((volatile uint32_t *)(NVMCTRL_BASE + 0x14))
#define NVMCTRL_SEESTAT        *((volatile uint32_t *)(NVMCTRL_BASE + 0x2c))


/* Extra NVMCTRL options (unused: leaving default values) */
#define NVMCTRLA_DISABLE_CACHES (0xC000)
#define NVMCTRLA_RWS_MASK       (0x0F00)
#define NVMCTRLA_RWS_SHIFT      8
#define NVMCTRLA_AUTOWS         (1 << 2)

#define NVMCMD_KEY               ((0xA5) << 8)
#define NVMCMD_ERASE_PAGE        (0x00)
#define NVMCMD_ERASE_BLOCK       (0x01)
#define NVMCMD_WP                (0x03)
#define NVMCMD_WQW               (0x04)
#define NVMCMD_PBC               (0x15)
#define NVMCMD_SSB               (0x16)  /* Set secure bit */
#define NVMCMD_BKSWRST           (0x17)  /* Bank swap + reset */
#define NVMCTRL_INTFLAG_CMD_DONE (1)

#define NVMSTATUS_AFIRST         (1 << 4)

volatile uint32_t psize, bsize;

/* Clock initialization */
static void clock_init(void)
{
    uint32_t reg;

    /* Prepare 32K oscillator */
    OSC32KCTRL_RTCCTRL &= ~RTCCTRL_RTCSEL_MASK;

    /* Set Generic Clock generator #2 */
    reg = GCLK_GENCTRL[2];

    /*   - Clear clock and source selection */
    reg &= ~(GENCTRLx_DIV_MASK | GENCTRLx_SRC_MASK);

    /*   - Select DFLL48M oscillator output as source
     *   - Set divisor to 48
     *   - Enable the generator
     */
    GCLK_GENCTRL[2] = reg | GENCTRLx_SRC_DFLL | (48u << GENCTRLx_DIVSHIFT) |
        GENCTRLx_GENEN;

    while(GCLK_IS_BUSY(2))
        ;

    /* Connect peripheral '1' (FDPLL0) to clock generator '2' and enable */
    reg = GCLK_PCHCTRL[1] & (~ 0x0F);
    GCLK_PCHCTRL[1] = reg | 0x02 | PCHCTRLx_CHEN;

    /* Wait until enabled */
    while ((GCLK_PCHCTRL[1] & PCHCTRLx_CHEN) == 0)
        ;

    /* Reset PLL parameters */
    OSCCTRL_DPLL0CTRLB = 0;

    /* Set PLL ratio LDR */
    OSCCTRL_DPLL0RATIO = (120 - 1);

    /* Wait until ratio has been set */
    while ((OSCCTRL_DPLL0SYNCBUSY & DPLL0SYNCBUSY_RATIO) != 0)
        ;

    /* Enable PLL */
    OSCCTRL_DPLL0CTRLA = DPLL0CTRLA_ENABLE;

    /* Wait until the PLL is enabled */
    while ((OSCCTRL_DPLL0SYNCBUSY & DPLL0SYNCBUSY_ENABLE) != 0)
        ;

    /* Wait for final lock + clock ready */
    while ((OSCCTRL_DPLL0STATUS & (DPLL0STATUS_LOCK | DPLL0STATUS_CLKRDY)) !=
            (DPLL0STATUS_LOCK | DPLL0STATUS_CLKRDY) )
        ;

    /* Set main clock divisor */
    MCLK_CPUDIV = 0x01u;

    /* Wait until ready */
    while ((MCLK_INTFLAG & CKRDY) == 0)
        ;

    /* generic clock generator #0: set source to FDPLL200M0, div 1 and enable */
    reg = GCLK_GENCTRL[0];
    reg &= ~(GENCTRLx_SRC_MASK | GENCTRLx_DIV_MASK);
    GCLK_GENCTRL[0] = reg | GENCTRLx_SRC_PLL0 | (1 << GENCTRLx_DIVSHIFT) |
        GENCTRLx_GENEN;
    while (GCLK_IS_BUSY(0))
        ;

    /* generic clock generator #1: set source to FDPLL200M0, div 2 and enable */
    reg = GCLK_GENCTRL[1];
    reg &= ~(GENCTRLx_SRC_MASK | GENCTRLx_DIV_MASK);
    GCLK_GENCTRL[1] = reg | GENCTRLx_SRC_PLL0 | (2 << GENCTRLx_DIVSHIFT) |
        GENCTRLx_GENEN;
    while (GCLK_IS_BUSY(1))
        ;

    /* select clock generator for EIC */
    reg = GCLK_PCHCTRL[4] & (~0x0F);
    GCLK_PCHCTRL[4] = reg | 0x01 | PCHCTRLx_CHEN;

    /* Wait until enabled */
    while ((GCLK_PCHCTRL[4] & PCHCTRLx_CHEN) == 0)
        ;

    /* select clock generator for SERCOM5_CORE */
    reg = GCLK_PCHCTRL[35] & (~0x0F);
    GCLK_PCHCTRL[35] = reg | 0x01 | PCHCTRLx_CHEN;

    /* wait until enabled */
    while ((GCLK_PCHCTRL[35] & PCHCTRLx_CHEN) == 0)
        ;

}


#ifdef __WOLFBOOT
#ifdef DUALBANK_SWAP
#define BANKA_BASE 0x00000000
#define BANKB_BASE 0x00080000

#include <string.h>

static void RAMFUNCTION fork_bootloader(void)
{
    uint32_t r;
    uint32_t len = (uint32_t)(WOLFBOOT_PARTITION_BOOT_ADDRESS - BANKA_BASE);
    if (memcmp((void *)BANKA_BASE, (void *)BANKB_BASE, len) == 0)
        return;
    hal_flash_unlock();
    hal_flash_erase(BANKB_BASE, len);
    for (r = 0; r < len; r += WOLFBOOT_SECTOR_SIZE) {
        hal_flash_write(BANKB_BASE + r, (void *)(BANKA_BASE + r), WOLFBOOT_SECTOR_SIZE);
    }
    hal_flash_lock();
}
#endif /* DUALBANK_SWAP */

void hal_init(void)
{
    /* Turn off watchdog */
    WDT_CTRL &= (~WDT_EN);
    /* Run the bootloader with interrupts off */
    __asm__ volatile ("cpsid i");

    /* Initialize clock */
    clock_init();

    /* enable all the AHB clocks */
    MCLK_AHBMASK = 0xffffffU;

    /* Enable flash memory controller via APBB */
    MCLK_APBBMASK |= (1 << 2);

    /* enable all the APBA clocks */
    MCLK_APBAMASK = 0x7ffU;

    /* enable all the APBD clocks */
    MCLK_APBDMASK = 0x2U;

#ifdef DUALBANK_SWAP
    fork_bootloader();
#endif
}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    hal_flash_unlock();
    NVMCTRLB = NVMCMD_BKSWRST | NVMCMD_KEY;

    /* Next loop should never be reached: system is restarted */
    while(!(NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_CMD_DONE))
        ;
    while( 1 )
        ;
}


void RAMFUNCTION hal_prepare_boot(void)
{
    /* Reset clock controller */
    GCLK_CTRLA |= CTRLA_SWRST;

    /* Wait until reset is complete */
    while ((GCLK_SYNCBUSY & SYNCBUSY_SWRST) != 0)
        ;

    /* Disable PLL */
    OSCCTRL_DPLL0CTRLA = 0;
    /* Wait until the PLL is enabled */
    while ((OSCCTRL_DPLL0SYNCBUSY & DPLL0SYNCBUSY_ENABLE) != 0)
        ;

    /* Clear PLL options */
    OSCCTRL_DPLL0CTRLB = 0;
}

#endif /* __WOLFBOOT */

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    if (len <= 0)
        return 0;

    /* Clear page buffer */
    NVMCTRLB  = (NVMCMD_PBC | NVMCMD_KEY);
    while (i < len) {
        if ((len - i > 3) && ((((address + i) & 0x03) == 0)  && ((((uint32_t)data) + i) & 0x03) == 0)) {
            dst = (uint32_t *)address;
            src = (uint32_t *)data;
            dst[i >> 2] = src[i >> 2];
            i+=4;
        } else {
            uint32_t val;
            uint8_t *vbytes = (uint8_t *)(&val);
            uint32_t off = (address % 4);
            dst = (uint32_t *)(address - off);
            uint32_t dst_idx = (i + off) >> 2;
            val = dst[dst_idx];
            while (off < 4) {
                if (i < len)
                    vbytes[off++] = data[i++];
                else
                    off++;
            }
            dst[dst_idx] = val;
        }
        if ((i == len) || ((i % 16)== 0))
            NVMCTRLB = (NVMCMD_WQW | NVMCMD_KEY);
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    PAC_WRCTRL = PAC_WRKEY_CLEAR | PAC_PERID_NVMCTL;
}

void RAMFUNCTION hal_flash_lock(void)
{
    PAC_WRCTRL = PAC_WRKEY_SET | PAC_PERID_NVMCTL;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    while (len > 0) {
        NVMCTRL_ADDR = (address);
        NVMCTRLB = NVMCMD_ERASE_BLOCK | NVMCMD_KEY;
        while(!(NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_CMD_DONE))
            ;
        len -= WOLFBOOT_SECTOR_SIZE;
        address += WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}


