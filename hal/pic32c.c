/* pic32c.c
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

#include "image.h"
#include "loader.h"
#include <stdint.h>
#include <string.h>

#if defined(TARGET_pic32cz)
#include "pic32cz_registers.h"
#elif defined(TARGET_pic32ck)
#include "pic32ck_registers.h"
#endif

#define FCW_CTRLA (*(volatile uint32_t *)(FCW_BASE + 0x00U))
#define FCW_CTRLB (*(volatile uint32_t *)(FCW_BASE + 0x04U))
#define FCW_MUTEX (*(volatile uint32_t *)(FCW_BASE + 0x08U))
#define FCW_INTENCLR (*(volatile uint32_t *)(FCW_BASE + 0x0CU))
#define FCW_INTENSET (*(volatile uint32_t *)(FCW_BASE + 0x10U))
#define FCW_INTFLAG (*(volatile uint32_t *)(FCW_BASE + 0x14U))
#define FCW_STATUS (*(volatile uint32_t *)(FCW_BASE + 0x18U))
#define FCW_KEY (*(volatile uint32_t *)(FCW_BASE + 0x1CU))
#define FCW_ADDR (*(volatile uint32_t *)(FCW_BASE + 0x20U))
#define FCW_SRCADDR (*(volatile uint32_t *)(FCW_BASE + 0x24U))
#define FCW_DATA ((volatile uint32_t *)(FCW_BASE + 0x28U))
#define FCW_SWAP (*(volatile uint32_t *)(FCW_BASE + 0x48U))
#define FCW_PWP ((volatile uint32_t *)(FCW_BASE + 0x4CU))
#define FCW_LBWP (*(volatile uint32_t *)(FCW_BASE + 0x6CU))
#define FCW_UBWP (*(volatile uint32_t *)(FCW_BASE + 0x70U))
#define FCW_UOWP (*(volatile uint32_t *)(FCW_BASE + 0x74U))
#define FCW_CWP (*(volatile uint32_t *)(FCW_BASE + 0x78U))
#define FCW_HSMINTENCLR (*(volatile uint32_t *)(FCW_BASE + 0x80U))
#define FCW_HSMINTENSET (*(volatile uint32_t *)(FCW_BASE + 0x84U))
#define FCW_HSMINTFLAG (*(volatile uint32_t *)(FCW_BASE + 0x88U))
#define FCW_HSMCWP (*(volatile uint32_t *)(FCW_BASE + 0x8CU))
#define FCW_HSMLDAT ((volatile uint32_t *)(FCW_BASE + 0x90U))
#define FCW_HSMUDAT ((volatile uint32_t *)(FCW_BASE + 0xB0U))

#define FCW_UNLOCK_WRKEY 0x91C32C01
#define FCW_UNLOCK_SWAPKEY 0x91C32C02
#define FCW_UNLOCK_CFGKEY 0x91C32C04
#define FCW_OP_ERASE_SECTOR 0x4
#define FCW_OP_QUAD_DOUBLE_WORD_WRITE 0x2
#define FCW_OP_NOOP 0x0

#define FCW_BUSY_MASK (1 << 0)
#define FCW_CTRLA_PREPG_BIT (1 << 7)
#define FCW_CTRLA_NVMOP_MASK ((1 << 4) - 1)
#define FCW_INTFLAG_DONE_BIT (1 << 0)
#define FCW_SWAP_PFSWAP (1 << 8)

/*
 * bit 0 lock
 * bit [1:2] owner (01 = mcu)
 */
#define FCW_OWN_MCU (0x1 << 1)
#define FCW_OWN_AND_LOCK 0x3
#define FCW_MUTEX_LOCK_MASK 0x1

#define FCW_WRITE_SIZE (4 * 8)
#define FCW_WRITE_WORD_SIZE (8)
static uint32_t pic32_last_err = 0;

#define OSCCTRL_STATUS (*(volatile uint32_t *)(OSCCTRL_BASE + 0x10U))
#define OSCCTRL_INTFLAG (*(volatile uint32_t *)(OSCCTRL_BASE + 0x0CU))
#define OSCCTRL_PLL0CTRL (*(volatile uint32_t *)(OSCCTRL_BASE + 0x40U))
#define OSCCTRL_PLL0FBDIV (*(volatile uint32_t *)(OSCCTRL_BASE + 0x44U))
#define OSCCTRL_PLL0REFDIV (*(volatile uint32_t *)(OSCCTRL_BASE + 0x48U))
#define OSCCTRL_PLL0POSTDIV0 (*(volatile uint32_t *)(OSCCTRL_BASE + 0x4CU))
#define OSCCTRL_FRACDIV0 (*(volatile uint32_t *)(OSCCTRL_BASE + 0x6CU))
#define OSCCTRL_SYNCBUSY (*(volatile uint32_t *)(OSCCTRL_BASE + 0x78U))

#define OSCCTRL_SYNCBUSY_FRACDIV0_MASK (1 << 6)

#define OSCCTRL_FRACDIV0_REMDIV_SHIFT (7)
#define OSCCTRL_FRACDIV0_REMDIV(X) ((X) << OSCCTRL_FRACDIV0_REMDIV_SHIFT)
#define OSCCTRL_FRACDIV0_INTDIV_SHIFT (16)
#define OSCCTRL_FRACDIV0_INTDIV(X) ((X) << OSCCTRL_FRACDIV0_INTDIV_SHIFT)

#define OSCCTRL_PLL0POSTDIV0_EN (1 << 0x7)

#define OSCCTRL_PLL0CTRL_BWSEL_SHIFT (11)
#define OSCCTRL_PLL0CTRL_BWSEL(X) ((X) << OSCCTRL_PLL0CTRL_BWSEL_SHIFT)
#define OSCCTRL_PLL0CTRL_REFSEL_SHIFT (8)
#define OSCCTRL_PLL0CTRL_REFSEL(X) ((X) << OSCCTRL_PLL0CTRL_REFSEL_SHIFT)
#define OSCCTRL_PLL0CTRL_EN (1 << 1)

#define OSCCTRL_STATUS_PLL0LOCK (1 << 24)
#define OSCCTRL_INTFLAG_PLL0LOCKR (1 << 24)

#define OSCCTRL_FRACDIV0 (*(volatile uint32_t *)(OSCCTRL_BASE + 0x6CU))
#define OSCCTRL_FRACDIV0_REMDIV_SHIFT (7)
#define OSCCTRL_FRACDIV0_REMDIV(X) ((X) << OSCCTRL_FRACDIV0_REMDIV_SHIFT)
#define OSCCTRL_FRACDIV0_INTDIV_SHIFT (16)
#define OSCCTRL_FRACDIV0_INTDIV(X) ((X) << OSCCTRL_FRACDIV0_INTDIV_SHIFT)

#define OSCCTRL_SYNCBUSY (*(volatile uint32_t *)(OSCCTRL_BASE + 0x78U))
#define OSCCTRL_SYNCBUSY_FRACDIV0_MASK (1 << 6)

#define GCLK_CTRLA (*(volatile uint32_t *)(GCLK_BASE + 0x00U))
#define GCLK_SYNCBUSY (*(volatile uint32_t *)(GCLK_BASE + 0x4U))
#define GCLK_GENCTRL ((volatile uint32_t *)(GCLK_BASE + 0x20U))

#define GCLK_GENCTRL_SRC_PLL0 (6)
#define GCLK_GENCTRL_GENEN (1 << 8)
#define GCLK_GENCTRL_DIV_SHIFT (16)
#define GCLK_GENCTRL_DIV(X) ((X) << GCLK_GENCTRL_DIV_SHIFT)
#define GCLK_SYNCBUSY_GENCTRL0 (1 << 2)
#define GCLK_CTRLA_SWRST (1)

#define MCLK_INTFLAG (*(volatile uint32_t *)(MCLK_BASE + 0x08U))
#define MCLK_DIV0 (*(volatile uint32_t *)(MCLK_BASE + 0x0CU))
#define MCLK_DIV1 (*(volatile uint32_t *)(MCLK_BASE + 0x10U))
#define MCLK_INTFLAG_CKRDY (1)

void pic32_fcw_grab(void)
{
    do {
        while (FCW_MUTEX & FCW_MUTEX_LOCK_MASK) {
            /* wait for ownership */

            /* if mutex is locked by us, we can unlock it */
            if ((FCW_MUTEX & FCW_OWN_MCU) == FCW_OWN_MCU) {
                FCW_MUTEX = FCW_OWN_MCU;
            }
        }
        FCW_MUTEX = FCW_OWN_AND_LOCK;
    } while (FCW_MUTEX != FCW_OWN_AND_LOCK);
}

void pic32_fcw_release(void)
{
    FCW_MUTEX = FCW_OWN_MCU;
}

static void pic32_fcw_start_op(uint32_t op)
{
    FCW_CTRLA = FCW_CTRLA_PREPG_BIT | (op & FCW_CTRLA_NVMOP_MASK);
}

static uint32_t pic32_get_errs(void)
{
    return FCW_INTFLAG;
}

static void pic32_clear_errs(void)
{
    FCW_INTFLAG = 0xffffffff;
}

static void pic32_fcw_wait_complete(void)
{
    while (FCW_STATUS & FCW_BUSY_MASK) {}
}

static int pic32_write_dqword_aligned(uint32_t addr, const uint32_t *data)
{
    uint32_t err;
    uint32_t i;

    pic32_fcw_wait_complete();
    FCW_ADDR = addr;
    for (i = 0; i < 8; i++) {
        FCW_DATA[i] = data[i];
    }
    FCW_KEY = FCW_UNLOCK_WRKEY;
    pic32_fcw_start_op(FCW_OP_QUAD_DOUBLE_WORD_WRITE);
    pic32_fcw_wait_complete();
    err = pic32_get_errs();
    pic32_last_err = err;
    if (!(err & FCW_INTFLAG_DONE_BIT)) {
        err = -1;
    } else {
        err &= ~FCW_INTFLAG_DONE_BIT;
    }
    pic32_clear_errs();
    return err;
}

static int pic32_addr_is_dqword_aligned(uint32_t addr)
{
    return ((addr & 0x1F) == 0);
}

static uint32_t pic32_addr_dqword_align(uint32_t addr)
{
    return (addr & ~0x1F);
}

static int pic32_fcw_erase_sector(uint32_t addr)
{
    uint32_t err;
    pic32_fcw_wait_complete();
    FCW_ADDR = addr;
    FCW_KEY = FCW_UNLOCK_WRKEY;
    pic32_fcw_start_op(FCW_OP_ERASE_SECTOR);
    pic32_fcw_wait_complete();
    err = pic32_get_errs();
    pic32_last_err = err;
    if (!(err & FCW_INTFLAG_DONE_BIT)) {
        err = -1;
    } else {
        err &= ~FCW_INTFLAG_DONE_BIT;
    }
    pic32_clear_errs();
    return err;
}

static void pic32_delay_cnt(uint32_t ticks)
{
    uint32_t i = 0;
    for (i = 0; i < ticks; i++) {
        __asm__("nop");
    }
}

static uint8_t pic32_mask_zeros(uint8_t programmed, uint8_t to_program)
{
    return to_program | (~programmed);
}

int pic32_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t buff[FCW_WRITE_WORD_SIZE], curr[FCW_WRITE_WORD_SIZE];
    uint8_t  *p_buff, *p_curr;
    uint32_t _addr;
    uint8_t i;
    int ret;

    while (len > 0) {
        if (!pic32_addr_is_dqword_aligned(address) || len < FCW_WRITE_SIZE) {
            _addr = pic32_addr_dqword_align(address);
            /* Setup an aligned buffer with the following rules:
             * - For addresses outside the writing range: 0xFF (no change)
             * - For addresses inside the writing range: data | !current_data
             *
             * This approach ensures we only flip bits from 1 to 0 when writing
             * without an erase operation. When the address is aligned and length
             * is at least WOLFBOOT_SECTOR_SIZE, an erase was already performed,
             * so we can write data directly.
             */
            memcpy(curr, (uint8_t*)(uintptr_t)_addr, sizeof(curr));
            memset(buff, 0xff, sizeof(buff));
            i = address - _addr;
            p_curr = (uint8_t*)curr;
            p_buff = (uint8_t*)buff;
            for (; i < FCW_WRITE_SIZE && len > 0; i++, len--) {
                p_buff[i] = pic32_mask_zeros(p_curr[i], *data);
                data++;
                address++;
            }
            ret = pic32_write_dqword_aligned(_addr, buff);
            if (ret != 0)
                return ret;
            continue;
        }

        /* move data in aligned buffer */
        if (!pic32_addr_is_dqword_aligned((uint32_t)(uintptr_t)data)) {
            memcpy(buff, data, sizeof(buff));
            ret = pic32_write_dqword_aligned(address, buff);
        } else {
            ret = pic32_write_dqword_aligned(address, (uint32_t*)data);
        }

        if (ret != 0)
            return ret;
        address += FCW_WRITE_SIZE;
        data += FCW_WRITE_SIZE;
        len -= FCW_WRITE_SIZE;
    }

    return 0;
}

int pic32_flash_erase(uint32_t addr, int len)
{
    int err;

    while (len > 0) {
        if (len < WOLFBOOT_SECTOR_SIZE) {
            return -1;
        }
        err = pic32_fcw_erase_sector(addr);
        if (err != 0)
            return err;
        addr += WOLFBOOT_SECTOR_SIZE;
        len -= WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}

#ifdef DUALBANK_SWAP

static int pic32_fcw_pfswap_get()
{
    return !!(FCW_SWAP & FCW_SWAP_PFSWAP);
}

static void pic32_fcw_pfswap_set(int sw)
{
    uint32_t reg;

    reg = FCW_SWAP;
    reg &= FCW_SWAP_PFSWAP;
    if (sw)
        reg |= FCW_SWAP_PFSWAP;
    FCW_KEY = FCW_UNLOCK_SWAPKEY;
    FCW_SWAP = reg;
}

void pic32_flash_dualbank_swap(void)
{
    uint32_t sw;
    uint32_t reg;

    pic32_fcw_wait_complete();
    sw = pic32_fcw_pfswap_get();
    pic32_fcw_pfswap_set(!sw);
}
#endif /* DUALBANK_SWAP */

void pic32_clock_fracdiv0_set(int intdiv, int remdiv)
{
    OSCCTRL_FRACDIV0 =
        OSCCTRL_FRACDIV0_INTDIV(intdiv) | OSCCTRL_FRACDIV0_REMDIV(remdiv);

    while (OSCCTRL_SYNCBUSY & OSCCTRL_SYNCBUSY_FRACDIV0_MASK) {}
}

void pic32_clock_pll0_init(int refdiv, int fbdiv, int bw, int postdiv)
{
    uint32_t reg;

    /* configure pll0 */
    OSCCTRL_PLL0CTRL = 0;
    OSCCTRL_PLL0REFDIV = refdiv;
    OSCCTRL_PLL0FBDIV = fbdiv;

    /* enable PLL0 output 0 divied by 3 (300Mhz) */
    OSCCTRL_PLL0POSTDIV0 = OSCCTRL_PLL0POSTDIV0_EN | postdiv;

    reg = OSCCTRL_PLL0CTRL;
    /* set the bandwith value */
    reg |= OSCCTRL_PLL0CTRL_BWSEL(bw);
    reg |= OSCCTRL_PLL0CTRL_REFSEL(0x2);
    reg |= OSCCTRL_PLL0CTRL_EN;
    OSCCTRL_PLL0CTRL |= reg;

    /* wait to the PLL to lock */
#if defined(TARGET_pic32cz)
    while (!(OSCCTRL_STATUS & OSCCTRL_STATUS_PLL0LOCK)) {}
#endif
#if defined(TARGET_pic32ck)
    while (!(OSCCTRL_INTFLAG & OSCCTRL_INTFLAG_PLL0LOCKR)) {}
#endif
}

void pic32_clock_gclk_gen0(int mclk_div1, int cpudiv)
{
    /* setup clock division before changing the generator */
    if (mclk_div1 != 1)
        MCLK_DIV1 = mclk_div1;

    while (!(MCLK_INTFLAG & MCLK_INTFLAG_CKRDY)) {}

    GCLK_GENCTRL[0] =
        GCLK_GENCTRL_SRC_PLL0 | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_DIV(cpudiv);
    while (GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL0) {}
}

void pic32_clock_reset(void)
{
    /* reset GCLK module */
    GCLK_CTRLA = GCLK_CTRLA_SWRST;

    /* wait for the reset to complete */
    while (GCLK_CTRLA & GCLK_CTRLA_SWRST) {}

    /* reset MCLK_DIV1 to reset value */
    MCLK_DIV1 = 1;
    while (!(MCLK_INTFLAG & MCLK_INTFLAG_CKRDY)) {}

    /* turn off PLL0 */
    OSCCTRL_PLL0CTRL = 0;
    OSCCTRL_PLL0POSTDIV0 = 0x20202020;

    /* reset PLL0 values */
    OSCCTRL_PLL0REFDIV = 0;
    OSCCTRL_PLL0FBDIV = 0;

    /* reset the fracdiv0 to reset value */
    pic32_clock_fracdiv0_set(32, 0);
}
#if defined(TEST_FLASH)
int hal_flash_test_align(void);
int hal_flash_test_write_once(void);
int hal_flash_test(void);
int hal_flash_test_dualbank(void);
int hal_flash_test_unaligned_src(void);
void pic32_flash_test(void)
{
    int ret;

    ret = hal_flash_test();
    if (ret != 0)
        wolfBoot_panic();
    ret = hal_flash_test_align();
    if (ret != 0)
        wolfBoot_panic();
    ret = hal_flash_test_write_once();
    if (ret != 0)
        wolfBoot_panic();
    /* enable unaligned access fault for testing */
    ret = *(volatile uint32_t*)0xE000ED14;
    *(volatile uint32_t*)0xE000ED14 = ret | 8;
    ret = hal_flash_test_unaligned_src();
    if (ret != 0)
        wolfBoot_panic();
#ifdef DUALBANK_SWAP
    ret = hal_flash_test_dualbank();
    if (ret != 0)
        wolfBoot_panic();
#endif
}
#endif /* TEST_FLASH */

#ifdef TEST_CLOCK

/* SysTick registers and constants */
#define SYSTICK_BASE (0xE000E010U)

#define SYSTICK_RVR_MASK (0x00FFFFFF)
#define SYSTICK_CSR (*(volatile uint32_t *)(SYSTICK_BASE + 0x00U))
#define SYSTICK_RVR (*(volatile uint32_t *)(SYSTICK_BASE + 0x04U))
#define SYSTICK_CVR (*(volatile uint32_t *)(SYSTICK_BASE + 0x08U))
#define SYSTICK_CSR_ENABLE (1 << 0)
#define SYSTICK_CSR_CLKSOURCE (1 << 2)
#define SYSTICK_CSR_COUNTFLAG (1 << 16)

#if defined(TARGET_pic32ck)
#define PORT_BASE (0x44801000U)
#define LED_NO 25
#elif defined(TARGET_pic32cz)
#define PORT_BASE (0x44840000U)
#define LED_NO 21
#endif

#define PORTB_BASE (PORT_BASE + 0x80 * 1)
#define PORTB_DIRSET (*(volatile uint32_t *)(PORTB_BASE + 0x08))
#define PORTB_DIRSET_OUT(X) (1 << (X))
#define PORTB_OUTTGL (*(volatile uint32_t *)(PORTB_BASE + 0x1C))
#define PORTB_OUTTGL_PIN(X) (1 << (X))

static int systick_init_1ms(uint32_t cpu_freq)
{
    /* Calculate the reload value for 1ms period */
    uint32_t reload_value = (cpu_freq / 1000) - 1;

    /* Check if reload value fits in 24 bits */
    if (reload_value > SYSTICK_RVR_MASK) {
        return -1;
    }
    /* Set reload value */
    SYSTICK_RVR = reload_value;
    /* Clear current value */
    SYSTICK_CVR = 0;
    /* Configure SysTick: enable counter, no interrupt, use CPU clock */
    SYSTICK_CSR = SYSTICK_CSR_ENABLE | SYSTICK_CSR_CLKSOURCE;

    return 0;
}

static void systick_delay_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0; i < ms; i++) {
        /* Wait until COUNTFLAG is set */
        while (!(SYSTICK_CSR & SYSTICK_CSR_COUNTFLAG)) {
            /* Wait */
        }
    }
}

/**
 * Tests clock configuration using SysTick. Initializes 1ms ticks,
 * toggles LED every 1s for 10s, then every 100ms for 2s.
 */
void pic32_clock_test(unsigned long cpu_freq)
{
    int i;
    PORTB_DIRSET = PORTB_DIRSET_OUT(LED_NO);
    /* Initialize SysTick with 1ms period based on target frequency */
    if (systick_init_1ms(cpu_freq) != 0) {
        wolfBoot_panic();
    }

    for (i = 0; i < 10; i++) {
        systick_delay_ms(1000);
        /* Wait for 1 second */
        PORTB_OUTTGL = PORTB_OUTTGL_PIN(LED_NO);
    }
    /* end test by fast toggling */
    for (i = 0; i < 20; i++) {
        systick_delay_ms(100);
        /* Wait for 1 second */
        PORTB_OUTTGL = PORTB_OUTTGL_PIN(LED_NO);
    }
}
#endif /* TEST_CLOCK */
