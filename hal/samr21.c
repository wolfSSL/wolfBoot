/* samr21.c
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
#include "image.h"

/* Clock settings for cpu samd21g18a @ 48MHz */
#define CPU_FREQ (48000000)
#define GCLK_CTRL_RESET (1)
#define GCLK_GENDIV_DIVSHIFT (8)
#define GCLK_CLKCTRL_GENSHIFT (8)
#define GCLK_STATUS_SYNCBUSY (1 << 7)
#define GCLK_GENCTRL_SRC_OSC8M (6 << 8)
#define GCLK_GENCTRL_EN (1 << 16)
#define GCLK_GENCTRL_SRC_FDPLL (1 << 11)
#define GCLK_CLKCTRL_GEN_CLK7 (7 << 8)
#define GCLK_CLKCTRL_CLKEN    (1 << 14)
#define WAITSTATES (1)

/* Flash settings for samd21g18a */
#define FLASH_SIZE          (256 * 1024)
#define FLASH_PAGESIZE      64
#define FLASH_N_PAGES       4096

#define WDT_CTRL *((volatile uint8_t *)(0x40001000))
#define WDT_EN (1 << 1)


#define APBAMASK_REG                 *((volatile uint32_t *)(0x40000418))
#define APBAMASK_PM_EN               (1 << 1)
#define APBAMASK_SYSCTRL_EN          (1 << 2)
#define APBAMASK_GCLK_EN             (1 << 3)

#define APBBMASK_REG                 *((volatile uint32_t *)(0x4000041C))
#define APBBMASK_NVM_EN              (1 << 2)

#define NVMCTRL_BASE           (0x41004000)
#define NVMCTRLA_REG           *((volatile uint16_t *)(NVMCTRL_BASE))
#define NVMCTRLB_REG           *((volatile uint32_t *)(NVMCTRL_BASE + 4))
#define NVMCTRL_INTFLAG        *((volatile uint8_t  *)(NVMCTRL_BASE + 0x14))
#define NVMCTRL_ADDR           *((volatile uint32_t *)(NVMCTRL_BASE + 0x1c))
#define NVMCMD_KEY             (0xA500)
#define NVMCMD_ERASE           (0x02)
#define NVMCMD_WP              (0x04)
#define NVMCMD_PBC             (0x44)
#define NVMCTRL_INTFLAG_NVMREADY (1)


#define GCLK_BASE              (0x40000C00)
#define GCLK_CTRL              *((volatile uint8_t  *)(GCLK_BASE))
#define GCLK_STATUS            *((volatile uint8_t  *)(GCLK_BASE + 1))
#define GCLK_CLKCTRL           *((volatile uint16_t *)(GCLK_BASE + 2))
#define GCLK_GENCTRL           *((volatile uint32_t *)(GCLK_BASE + 4))
#define GCLK_GENDIV            *((volatile uint32_t *)(GCLK_BASE + 8))

#define SYSCTRL_OSC8M           *((volatile uint32_t *)(0x40000820))
#define SYSCTRL_OSC8M_ENABLE    (1 << 1)
#define SYSCTRL_OSC8M_ONDEMAND  (1 << 7)
#define SYSCTRL_OSC8M_PRESC_MASK (3 << 8)
#define SYSCTRL_OSC8M_RUNSTDBY    (1 << 6)

#define SYSCTRL_PLLK_SR         *((volatile uint32_t *)(0x4000080c))
#define PLLK_SR_OSC8M_RDY       (1 << 3)
#define OSC8M_WAITBUSY()        { while (!(SYSCTRL_PLLK_SR & (PLLK_SR_OSC8M_RDY))) {} }

#define SYSCTRL_DPLL          (0x40000844)
#define SYSCTRL_DPLLCTRLA     *((volatile uint8_t *)(0x40000844))
#define SYSCTRL_DPLLRATIO     *((volatile uint32_t *)(0x40000848))
#define SYSCTRL_DPLLCTRLB     *((volatile uint32_t *)(0x4000084C))
#define SYSCTRL_DPLLSTATUS    *((volatile uint8_t *)(0x40000850))

#define DPLLCTRLA_ENABLE (1 << 1)
#define DPLLCTRLB_REFCLK_GCLK (1 << 5)
#define DPLLSTATUS_CLKRDY (1 << 1)
#define DPLLSTATUS_LOCK (1 << 0)
#define SYSCTRL_DPLLSTATUS_WAITLOCK() { while (!(SYSCTRL_DPLLSTATUS & (DPLLSTATUS_CLKRDY | DPLLSTATUS_LOCK))) {} }


#define PAC1_BASE             (0x41000000)
#define PAC1_WPCLR            *((volatile uint32_t *)(PAC1_BASE)) /* Register to clear write protection (unlock flash) */
#define PAC1_WPSET            *((volatile uint32_t *)(PAC1_BASE + 4)) /* Register to set write protection (lock flash) */
#define PAC_WP_NVMCTL         (1 << 1) /* Bit position for NVM in WPCLR/WPSET registers */


#define GCLK_WAITBUSY() { while (GCLK_STATUS & GCLK_STATUS_SYNCBUSY) {} }

void hal_init(void)
{

    WDT_CTRL &= (~WDT_EN);
    __asm__ volatile ("cpsid i");
    uint32_t i, reg;
    /* enable clocks for the power, sysctrl and gclk modules */
    APBAMASK_REG = APBAMASK_PM_EN | APBAMASK_SYSCTRL_EN | APBAMASK_GCLK_EN;

    /* set NVM wait states */
    APBBMASK_REG |= APBBMASK_NVM_EN;
    NVMCTRLB_REG |= ((WAITSTATES & 0x0f) << 1);
    APBBMASK_REG &= ~APBBMASK_NVM_EN;


    /* Set 8MHz oscillator */
    reg = SYSCTRL_OSC8M & (~(SYSCTRL_OSC8M_PRESC_MASK | SYSCTRL_OSC8M_RUNSTDBY));
    SYSCTRL_OSC8M = reg | SYSCTRL_OSC8M_ENABLE | SYSCTRL_OSC8M_ONDEMAND;
    OSC8M_WAITBUSY();

    /* Set PLL config */
    GCLK_CTRL = GCLK_CTRL_RESET;
    GCLK_WAITBUSY();
    GCLK_GENDIV = (8 << GCLK_GENDIV_DIVSHIFT) | 1;
    GCLK_GENCTRL = GCLK_GENCTRL_EN | GCLK_GENCTRL_SRC_OSC8M | 1;
    GCLK_CLKCTRL = (1 << GCLK_CLKCTRL_GENSHIFT) | GCLK_CLKCTRL_CLKEN | 1;
    GCLK_WAITBUSY();
    SYSCTRL_DPLLRATIO = (47);
    SYSCTRL_DPLLCTRLB = DPLLCTRLB_REFCLK_GCLK;
    SYSCTRL_DPLLCTRLA = DPLLCTRLA_ENABLE;
    SYSCTRL_DPLLSTATUS_WAITLOCK();
    GCLK_GENDIV = (1 << GCLK_GENDIV_DIVSHIFT) | 0;
    GCLK_GENCTRL = GCLK_GENCTRL_EN | GCLK_GENCTRL_SRC_FDPLL;
    GCLK_WAITBUSY();
    for (i = 3; i <= 34; i++) {
        GCLK_CLKCTRL = GCLK_CLKCTRL_GEN_CLK7 | i;
        GCLK_WAITBUSY();
    }
}

void hal_prepare_boot(void)
{
    /* Reset NVM wait states */
    APBBMASK_REG |= APBBMASK_NVM_EN;
    NVMCTRLB_REG &= ~((WAITSTATES & 0x0f) << 1);
    APBBMASK_REG &= ~APBBMASK_NVM_EN;

    /* Reset clock controller */
    GCLK_CTRL = GCLK_CTRL_RESET;
    GCLK_WAITBUSY();
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    if (len <= 0)
        return 0;

    /* Clear page buffer */
    NVMCTRLA_REG  = (NVMCMD_PBC | NVMCMD_KEY);
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
    }
    /* Enable write protection */
    NVMCTRLA_REG = (NVMCMD_WP | NVMCMD_KEY);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    PAC1_WPCLR |= (PAC_WP_NVMCTL);
}

void RAMFUNCTION hal_flash_lock(void)
{
    PAC1_WPSET |= (PAC_WP_NVMCTL);
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    while (len > 0) {
        NVMCTRL_ADDR = (address >> 1); /* This register holds the address of a 16-bit row */
        NVMCTRLA_REG = NVMCMD_ERASE | NVMCMD_KEY;
        while(!(NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_NVMREADY))
        len -= FLASH_PAGESIZE;
    }
    return 0;
}

