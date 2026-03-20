/* max32666.c
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
 *
 * HAL for Analog Devices MAX32665/MAX32666
 * Tested on MAX32666FTHR: Cortex-M4 @ 96MHz, 1MB Flash, 560KB SRAM
 */

#include <stdint.h>
#include <string.h>
#include "image.h"
#include "hal.h"
#include "printf.h"

/* Override RAMFUNCTION for test-app when RAM_CODE is set */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")

#define __disable_irq() __asm__ volatile ("cpsid i" ::: "memory")
#define __enable_irq()  __asm__ volatile ("cpsie i" ::: "memory")

#include "max32666.h"

/* Helper to access FLC registers by base + offset */
#define FLC_REG(base, off)  (*(volatile uint32_t *)((uint32_t)(base) + (off)))

/* ============== Flash Bank Helper ============== */

/* Determine which FLC bank controls the given address */
static volatile uint32_t* flc_base_for_addr(uint32_t address)
{
    if (address < (FLASH_BASE + (FLASH_SIZE / 2))) {
        return (volatile uint32_t*)FLC0_BASE;
    }
    return (volatile uint32_t*)FLC1_BASE;
}

/* ============== Watchdog Functions ============== */

static void watchdog_disable(void)
{
    /* Disable WDT0 */
    WDT0_CTRL &= ~(WDT_CTRL_EN | WDT_CTRL_RST_EN | WDT_CTRL_INT_EN);
}

/* ============== Clock Configuration ============== */

static void clock_init(void)
{
    /* Enable HIRC96M (96 MHz) */
    GCR_CLKCN |= GCR_CLKCN_HIRC96M_EN;

    /* Wait for HIRC96M to be ready */
    while (!(GCR_CLKCN & GCR_CLKCN_HIRC96M_RDY)) {}

    /* Select HIRC96 as system clock, no prescaler (PSC=0 = div1) */
    GCR_CLKCN = (GCR_CLKCN & ~(GCR_CLKCN_CLKSEL_MASK | GCR_CLKCN_PSC_MASK)) |
                GCR_CLKCN_CLKSEL_HIRC96 |
                GCR_CLKCN_HIRC96M_EN |
                GCR_CLKCN_HIRC8M_EN;

    /* Wait for clock switch to complete */
    while (!(GCR_CLKCN & GCR_CLKCN_CKRDY)) {}

    /* Enable HIRC8M (7.3728 MHz) for UART baud rate generation */
    GCR_CLKCN |= GCR_CLKCN_HIRC8M_EN;
    while (!(GCR_CLKCN & GCR_CLKCN_HIRC8M_RDY)) {}
}

/* ============== ICC (Instruction Cache) Functions ============== */

static void RAMFUNCTION icc_disable(void)
{
    ICC0_CTRL &= ~ICC_CTRL_EN;
}

static void RAMFUNCTION icc_enable(void)
{
    /* Invalidate and re-enable cache */
    ICC0_INVALIDATE = 1;
    ICC0_CTRL |= ICC_CTRL_EN;
    while (!(ICC0_CTRL & ICC_CTRL_RDY)) {}
}

/* ============== Flash Functions ============== */

static void RAMFUNCTION flc_unlock(volatile uint32_t *flc_base)
{
    FLC_REG(flc_base, FLC_ACNTL_OFF) = FLC_ACNTL_UNLOCK_KEY1;
    FLC_REG(flc_base, FLC_ACNTL_OFF) = FLC_ACNTL_UNLOCK_KEY2;

    /* Set unlock bits in CN register */
    FLC_REG(flc_base, FLC_CN_OFF) =
        (FLC_REG(flc_base, FLC_CN_OFF) & ~FLC_CN_UNLOCK_MASK) |
        FLC_CN_UNLOCK_UNLOCKED;
}

static void RAMFUNCTION flc_lock(volatile uint32_t *flc_base)
{
    FLC_REG(flc_base, FLC_ACNTL_OFF) = 0;
}

static void RAMFUNCTION flc_wait_done(volatile uint32_t *flc_base)
{
    /* Wait for any pending operation */
    while (FLC_REG(flc_base, FLC_CN_OFF) &
           (FLC_CN_WR | FLC_CN_PGE | FLC_CN_ME)) {}

    /* Check done flag */
    if (FLC_REG(flc_base, FLC_INTR_OFF) & FLC_INTR_DONE) {
        /* Clear done flag (write 1 to clear) */
        FLC_REG(flc_base, FLC_INTR_OFF) = FLC_INTR_DONE;
    }
}

static int RAMFUNCTION flc_write128(uint32_t address, const uint32_t *data,
    volatile uint32_t *flc_base)
{
    /* Skip if data is all 0xFF (erased) */
    if (data[0] == 0xFFFFFFFF && data[1] == 0xFFFFFFFF &&
        data[2] == 0xFFFFFFFF && data[3] == 0xFFFFFFFF) {
        return 0;
    }

    flc_unlock(flc_base);
    flc_wait_done(flc_base);

    /* Clear any previous errors */
    if (FLC_REG(flc_base, FLC_INTR_OFF) & FLC_INTR_AF) {
        FLC_REG(flc_base, FLC_INTR_OFF) = FLC_INTR_AF;
    }

    /* Set flash clock divider for 1 MHz operation */
    FLC_REG(flc_base, FLC_CLKDIV_OFF) = FLC_CLKDIV_VALUE;

    /* Set address */
    FLC_REG(flc_base, FLC_ADDR_OFF) = address;

    /* Load 128-bit data (4 x 32-bit words) */
    *(volatile uint32_t *)((uint32_t)flc_base + FLC_DATA_OFF + 0x00) = data[0];
    *(volatile uint32_t *)((uint32_t)flc_base + FLC_DATA_OFF + 0x04) = data[1];
    *(volatile uint32_t *)((uint32_t)flc_base + FLC_DATA_OFF + 0x08) = data[2];
    *(volatile uint32_t *)((uint32_t)flc_base + FLC_DATA_OFF + 0x0C) = data[3];

    /* Trigger 128-bit write */
    DSB();
    FLC_REG(flc_base, FLC_CN_OFF) |= FLC_CN_WR;

    /* Wait for completion */
    flc_wait_done(flc_base);

    flc_lock(flc_base);

    /* Check for access fault */
    if (FLC_REG(flc_base, FLC_INTR_OFF) & FLC_INTR_AF) {
        FLC_REG(flc_base, FLC_INTR_OFF) = FLC_INTR_AF;
        return -1;
    }

    return 0;
}

static int RAMFUNCTION flc_page_erase(uint32_t address,
    volatile uint32_t *flc_base)
{
    flc_unlock(flc_base);
    flc_wait_done(flc_base);

    /* Clear any previous errors */
    if (FLC_REG(flc_base, FLC_INTR_OFF) & FLC_INTR_AF) {
        FLC_REG(flc_base, FLC_INTR_OFF) = FLC_INTR_AF;
    }

    /* Set flash clock divider */
    FLC_REG(flc_base, FLC_CLKDIV_OFF) = FLC_CLKDIV_VALUE;

    /* Set address (any address within the page) */
    FLC_REG(flc_base, FLC_ADDR_OFF) = address;

    /* Set erase code and trigger page erase */
    FLC_REG(flc_base, FLC_CN_OFF) =
        (FLC_REG(flc_base, FLC_CN_OFF) & ~FLC_CN_ERASE_CODE_MASK) |
        FLC_CN_ERASE_CODE_PGE;
    DSB();
    FLC_REG(flc_base, FLC_CN_OFF) |= FLC_CN_PGE;

    /* Wait for completion */
    flc_wait_done(flc_base);

    /* Clear erase bits */
    FLC_REG(flc_base, FLC_CN_OFF) &=
        ~(FLC_CN_PGE | FLC_CN_ERASE_CODE_MASK);

    flc_lock(flc_base);

    /* Check for access fault */
    if (FLC_REG(flc_base, FLC_INTR_OFF) & FLC_INTR_AF) {
        FLC_REG(flc_base, FLC_INTR_OFF) = FLC_INTR_AF;
        return -1;
    }

    return 0;
}

/* ============== UART Functions ============== */

#ifdef DEBUG_UART

void uart_init(void)
{
    /* Enable GPIO1 clock for pin muxing */
    GCR_PERCKCN0 &= ~GCR_PERCKCN0_GPIO1D;

    /* Enable UART peripheral clock (clear disable bit) */
    GCR_PERCKCN0 &= ~DEBUG_UART_PCLKDIS;

    /* Enable HIRC8M for UART baud rate clock */
    GCR_CLKCN |= GCR_CLKCN_HIRC8M_EN;
    while (!(GCR_CLKCN & GCR_CLKCN_HIRC8M_RDY)) {}

#if DEBUG_UART_NUM == 1
    /* UART1 MAP_B: P1.12 (RX), P1.13 (TX) = AF3
     * AF3: EN0=0, EN1=0, EN2=1 (per MSDK gpio_reva.c) */
    GPIO1_EN0_CLR = UART1B_PINS;
    GPIO1_EN1_CLR = UART1B_PINS;
    *(volatile uint32_t *)(GPIO1_BASE + GPIO_EN2_SET_OFF) = UART1B_PINS;
#endif

    /* Disable UART before configuration */
    DEBUG_UART_CTRL = 0;

    /* Configure: 8-bit, no parity, 1 stop bit, HIRC8M clock source */
    DEBUG_UART_CTRL = UART_CTRL_CHAR_SZ_8 | UART_CTRL_CLKSEL;

    /* Set baud rate using HIRC8M (7.3728 MHz)
     * baud = clk / (IBAUD * (128 >> FACTOR))
     * For 115200 with FACTOR=2 (div 32): IBAUD = 7372800 / (115200*32) = 2
     */
    DEBUG_UART_BAUD0 = (2UL << UART_BAUD0_IBAUD_SHIFT) |
                       UART_BAUD0_FACTOR_32;
    DEBUG_UART_BAUD1 = 0; /* No fractional adjustment */

    /* Disable all interrupts */
    DEBUG_UART_INT_EN = 0;

    /* Clear any pending interrupt flags */
    DEBUG_UART_INT_FL = DEBUG_UART_INT_FL;

    /* Enable UART */
    DEBUG_UART_CTRL |= UART_CTRL_ENABLE;
}

void RAMFUNCTION uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            while (DEBUG_UART_STATUS & UART_STATUS_TX_FULL) {}
            DEBUG_UART_FIFO = '\r';
        }
        while (DEBUG_UART_STATUS & UART_STATUS_TX_FULL) {}
        DEBUG_UART_FIFO = buf[i];
    }
    /* Wait for transmission complete */
    while (DEBUG_UART_STATUS & UART_STATUS_TX_BUSY) {}
}

int RAMFUNCTION uart_read(char* c)
{
    if (DEBUG_UART_STATUS & UART_STATUS_RX_EMPTY) {
        return 0;
    }
    *c = (char)(DEBUG_UART_FIFO & 0xFF);
    return 1;
}

#endif /* DEBUG_UART */

/* ============== HAL Interface Functions ============== */

void hal_init(void)
{
    /* Disable watchdog first */
    watchdog_disable();

    /* Initialize clocks to 96 MHz */
    clock_init();

    /* Set FLC clock dividers for both banks */
    FLC0_CLKDIV = FLC_CLKDIV_VALUE;
    FLC1_CLKDIV = FLC_CLKDIV_VALUE;

    /* Enable instruction cache */
    icc_enable();

#ifdef DEBUG_UART
    uart_init();

#ifdef __WOLFBOOT
#ifdef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("wolfBoot Version: %s\n", LIBWOLFBOOT_VERSION_STRING);
#else
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING, __DATE__, __TIME__);
#endif
#endif /* __WOLFBOOT */
#endif /* DEBUG_UART */
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    /* Wait for UART to finish transmitting */
    while (DEBUG_UART_STATUS & UART_STATUS_TX_BUSY) {}

    /* Disable UART for clean handoff to application */
    DEBUG_UART_CTRL = 0;
#endif
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret;
    int i = 0;
    uint32_t write_buf[4]; /* 128-bit (16-byte) write buffer */
    volatile uint32_t *flc_base;

    icc_disable();

    while (len > 0) {
        flc_base = flc_base_for_addr(address);

        if ((len < FLASH_WRITE_SIZE) || (address & (FLASH_WRITE_SIZE - 1))) {
            /* Handle unaligned start or partial write with RMW */
            uint32_t aligned_addr = address & ~(FLASH_WRITE_SIZE - 1);
            uint32_t offset = address - aligned_addr;
            int bytes_to_copy = FLASH_WRITE_SIZE - offset;
            if (bytes_to_copy > len)
                bytes_to_copy = len;

            memcpy(write_buf, (void*)aligned_addr, FLASH_WRITE_SIZE);
            memcpy((uint8_t*)write_buf + offset, data + i, bytes_to_copy);

            ret = flc_write128(aligned_addr, write_buf, flc_base);
            if (ret != 0) {
                icc_enable();
                return ret;
            }

            address += bytes_to_copy;
            i += bytes_to_copy;
            len -= bytes_to_copy;
        } else {
            /* Write full 128-bit aligned words */
            while (len >= FLASH_WRITE_SIZE) {
                flc_base = flc_base_for_addr(address);
                memcpy(write_buf, data + i, FLASH_WRITE_SIZE);

                ret = flc_write128(address, write_buf, flc_base);
                if (ret != 0) {
                    icc_enable();
                    return ret;
                }

                address += FLASH_WRITE_SIZE;
                i += FLASH_WRITE_SIZE;
                len -= FLASH_WRITE_SIZE;
            }
        }
    }

    icc_enable();
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret;
    volatile uint32_t *flc_base;

    /* Align to page boundary */
    if (address & (FLASH_PAGE_SIZE - 1)) {
        address &= ~(FLASH_PAGE_SIZE - 1);
    }

    icc_disable();

    while (len > 0) {
        flc_base = flc_base_for_addr(address);

        ret = flc_page_erase(address, flc_base);
        if (ret != 0) {
            icc_enable();
            return ret;
        }

        address += FLASH_PAGE_SIZE;
        len -= FLASH_PAGE_SIZE;
    }

    icc_enable();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flc_unlock((volatile uint32_t*)FLC0_BASE);
    flc_unlock((volatile uint32_t*)FLC1_BASE);
}

void RAMFUNCTION hal_flash_lock(void)
{
    flc_lock((volatile uint32_t*)FLC0_BASE);
    flc_lock((volatile uint32_t*)FLC1_BASE);
}
