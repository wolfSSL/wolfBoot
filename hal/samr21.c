/* samr21.c
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
#include "image.h"
#include "hal.h"
#ifdef DEBUG_UART
#include "uart_drv.h"
#endif

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
/* NVMCMD_ERASE (0x02) is the NVMCTRL row erase: one command erases a
 * 256-byte row (4 pages), so erase loops stride by the row size. */
#define FLASH_ROW_SIZE      (4 * FLASH_PAGESIZE)

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
#define NVMCTRL_PARAM          *((volatile uint32_t *)(NVMCTRL_BASE + 0x08))
#define NVMCTRL_INTFLAG        *((volatile uint8_t  *)(NVMCTRL_BASE + 0x14))
#define NVMCTRL_STATUS         *((volatile uint16_t *)(NVMCTRL_BASE + 0x18))
#define NVMCTRL_ADDR           *((volatile uint32_t *)(NVMCTRL_BASE + 0x1c))
#define NVMCMD_KEY             (0xA500)
#define NVMCMD_ERASE           (0x02)
#define NVMCMD_WP              (0x04)
#define NVMCMD_LR              (0x40)
#define NVMCMD_PBC             (0x44)
#define NVMCTRL_INTFLAG_NVMREADY (1)
/* CTRLB.MANW: commit each page with an explicit NVMCMD_WP instead of
 * letting the controller write it out when the page buffer fills. */
#define NVMCTRLB_MANW          (1 << 7)
/* STATUS error flags, write-one-to-clear: PROGE, LOCKE, NVME */
#define NVMSTATUS_ERR          ((1 << 2) | (1 << 3) | (1 << 4))
/* PARAM reports the array geometry: NVMP pages of (8 << PSZ) bytes each. */
#define NVMPARAM_NVMP          (0xFFFF)
#define NVMPARAM_PSZ_SHIFT     (16)
#define NVMPARAM_PSZ           (7)
/* NVMCTRL divides the whole array into 16 lock regions, one per LOCK bit. */
#define NVM_LOCK_REGIONS       (16)


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
#define PAC_WP_NVMCTL         (1 << 2) /* Bit position for NVM in WPCLR/WPSET registers */


#define GCLK_WAITBUSY() { while (GCLK_STATUS & GCLK_STATUS_SYNCBUSY) {} }

void hal_init(void)
{
    uint32_t i, reg;

    WDT_CTRL &= (~WDT_EN);
#ifndef WOLFBOOT_UNIT_TEST_FLASH_WRITE
    __asm__ volatile ("cpsid i");
#endif
    /* enable clocks for the power, sysctrl and gclk modules */
    APBAMASK_REG = APBAMASK_PM_EN | APBAMASK_SYSCTRL_EN | APBAMASK_GCLK_EN;

    /* Set NVM wait states and take manual control of the page write. The
     * NVMCTRL APB clock is enabled out of reset and has to stay on: the
     * flash write and erase paths below drive these same registers. */
    APBBMASK_REG |= APBBMASK_NVM_EN;
    NVMCTRLB_REG |= ((WAITSTATES & 0x0f) << 1) | NVMCTRLB_MANW;


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

#ifdef DEBUG_UART
    uart_init(115200, 8, 'N', 1);
#endif
}

void hal_prepare_boot(void)
{
#ifdef WOLFBOOT_RESTORE_CLOCK
    /* Restore the NVM wait states and page write mode to their reset values */
    APBBMASK_REG |= APBBMASK_NVM_EN;
    NVMCTRLB_REG &= ~(((WAITSTATES & 0x0f) << 1) | NVMCTRLB_MANW);

    /* Reset clock controller */
    GCLK_CTRL = GCLK_CTRL_RESET;
    GCLK_WAITBUSY();
#endif
}


/* Wait for the controller to go idle, then report and clear any error it
 * latched. The STATUS error flags are write-one-to-clear. */
static int RAMFUNCTION nvm_wait(void)
{
    uint16_t status;

    while (!(NVMCTRL_INTFLAG & NVMCTRL_INTFLAG_NVMREADY)) { }
    status = (NVMCTRL_STATUS & NVMSTATUS_ERR);
    if (status != 0) {
        NVMCTRL_STATUS = status;
        return -1;
    }
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;

    if (len <= 0)
        return 0;

    /* Clear page buffer */
    NVMCTRLA_REG  = (NVMCMD_PBC | NVMCMD_KEY);
    if (nvm_wait() != 0)
        return -1;
    while (i < len) {
        if ((len - i > 3) && ((((address + i) & 0x03) == 0)  && ((((uint32_t)data) + i) & 0x03) == 0)) {
            dst = (uint32_t *)(address + i);
            src = (uint32_t *)(data + i);
            *dst = *src;
            i+=4;
        } else {
            uint32_t val;
            uint8_t *vbytes = (uint8_t *)(&val);
            uint32_t off = ((address + i) % 4);
            dst = (uint32_t *)(address + i - off);
            val = *dst;
            while (off < 4) {
                if (i < len)
                    vbytes[off++] = data[i++];
                else
                    off++;
            }
            *dst = val;
        }
        /* The page buffer holds one page, and MANW stops the controller
         * from writing it out by itself, so commit at every page boundary
         * and once more for a trailing partial page. */
        if ((i == len) || (((address + i) % FLASH_PAGESIZE) == 0)) {
            NVMCTRLA_REG = (NVMCMD_WP | NVMCMD_KEY);
            if (nvm_wait() != 0)
                return -1;
            if (i < len) {
                NVMCTRLA_REG = (NVMCMD_PBC | NVMCMD_KEY);
                if (nvm_wait() != 0)
                    return -1;
            }
        }
    }
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
        if (nvm_wait() != 0)
            return -1;
        address += FLASH_ROW_SIZE;
        len -= FLASH_ROW_SIZE;
    }
    return 0;
}

/* Lock the NVMCTRL regions covering [address, address+len).
 *
 * NVMCMD_LR only clears a bit in NVMCTRL.LOCK, which reloads from the user
 * row at every reset, so the lock has to be re-applied on each boot. That is
 * what the caller does: this runs on the way out to the application, after
 * any swap or self-update has finished writing. The persistent BOOTPROT fuse
 * is deliberately left to the production programmer, since setting it here
 * would erase and rewrite the user row on every single boot.
 *
 * A region is one sixteenth of the array, so the geometry comes from PARAM
 * rather than FLASH_SIZE: the 128KB parts have 8KB regions where the 256KB
 * parts have 16KB. Protection is granted in whole regions, so a partial
 * region at either end is locked whole - the locked range may come out wider
 * than requested, never narrower.
 */
int RAMFUNCTION hal_flash_protect(haladdr_t address, int len)
{
    uint32_t param, flash_size, region_size, shift, start, end, i;

    if (len < 0)
        return -1;
    /* An empty range locks nothing. Return before the region math: the
     * round-up below would otherwise widen it to one whole region. */
    if (len == 0)
        return 0;

    param = NVMCTRL_PARAM;
    flash_size = (param & NVMPARAM_NVMP) *
        (8U << ((param >> NVMPARAM_PSZ_SHIFT) & NVMPARAM_PSZ));
    region_size = flash_size / NVM_LOCK_REGIONS;
    /* Index the regions by shifting rather than dividing. Every SAMD21 array
     * and page size is a power of two, so the region size is one too; the
     * Cortex-M0+ has no divide instruction and a division by this runtime
     * value would pull the soft-divide routine into the bootloader. The
     * equality check below is what rules out a part this does not hold for,
     * a PARAM of zero among them. */
    for (shift = 0; (region_size >> shift) > 1U; shift++)
        ;
    if ((1U << shift) != region_size)
        return -1;

    start = (uint32_t)address;
    if (start >= flash_size)
        return -1;
    /* Clamp a range that runs off the end of the array rather than failing:
     * locking the part of it that exists is the safe reading. */
    if ((start + (uint32_t)len) > flash_size)
        len = (int)(flash_size - start);
    end = start + (uint32_t)len;

    /* The caller has already finished writing flash, so the PAC is guarding
     * NVMCTRL again by now and the command register would not take a write. */
    hal_flash_unlock();
    for (i = (start >> shift);
            i < ((end + region_size - 1) >> shift); i++) {
        NVMCTRL_ADDR = ((i << shift) >> 1);
        NVMCTRLA_REG = NVMCMD_LR | NVMCMD_KEY;
        if (nvm_wait() != 0) {
            hal_flash_lock();
            return -1;
        }
    }
    hal_flash_lock();
    return 0;
}

