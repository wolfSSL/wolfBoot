/* versal.c
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
 * AMD Versal ACAP HAL implementation for wolfBoot
 * Target: VMK180 Evaluation Board (VM1802 Versal Prime)
 *
 * Features:
 *   - UART driver (Cadence UART)
 *   - ARM Generic Timer
 *   - Flash stubs (OSPI/SD to be implemented)
 */

#ifdef TARGET_versal

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hal/versal.h"
#include "image.h"
#include "printf.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot versal HAL: wrong architecture. Please compile with ARCH=AARCH64."
#endif


/* ============================================================================
 * UART Driver
 * ============================================================================
 * ARM PL011 UART controller
 * Note: In JTAG boot mode, the PLM doesn't run so UART may be inaccessible.
 * Timeouts are added to prevent infinite loops.
 */

#ifdef DEBUG_UART

/* Timeout to prevent infinite loops if UART is inaccessible (e.g., JTAG boot) */
#define UART_TIMEOUT 10000

/**
 * Calculate baud rate divisors for ARM PL011 UART
 * Formula: baud = UART_CLK / (16 * divisor)
 *          divisor = IBRD + (FBRD / 64)
 *          IBRD = integer part of (UART_CLK / (16 * baud))
 *          FBRD = integer part of ((fractional * 64) + 0.5)
 */
static void uart_calc_baud(uint32_t ref_clk, uint32_t baud,
                           uint32_t *ibrd, uint32_t *fbrd)
{
    uint32_t divisor_x64;

    /* Calculate divisor * 64 to get fractional part */
    /* divisor = ref_clk / (16 * baud) */
    /* divisor_x64 = (ref_clk * 64) / (16 * baud) = (ref_clk * 4) / baud */
    divisor_x64 = (ref_clk * 4) / baud;

    /* Integer part: divisor_x64 / 64 */
    *ibrd = divisor_x64 >> 6;

    /* Fractional part: divisor_x64 % 64 (already in correct format) */
    *fbrd = divisor_x64 & 0x3F;
}

void uart_init(void)
{
#if defined(EL2_HYPERVISOR) && EL2_HYPERVISOR == 1
    /* When booting via PLM -> BL31 -> wolfBoot (EL2), UART is already
     * fully configured by PLM. Do NOT reinitialize - just use it as-is.
     * Any reconfiguration at EL2 may fail or corrupt the UART state. */
    (void)0; /* UART already configured by PLM - nothing to do */
#else
    /* Full UART initialization for JTAG boot mode or EL3 boot */
    uint32_t ibrd, fbrd;
    uint32_t lcr;
    volatile uint32_t timeout;
    volatile uint32_t *uart_clk_ctrl;
    volatile uint32_t *uart_rst_ctrl;
    int rx_pin, tx_pin;

    /* Select which UART to use */
#if defined(DEBUG_UART_NUM) && DEBUG_UART_NUM == 1
    uart_clk_ctrl = (volatile uint32_t*)&CRL_UART1_REF_CTRL;
    uart_rst_ctrl = (volatile uint32_t*)&CRL_RST_UART1;
    rx_pin = MIO_UART1_RX_PIN;
    tx_pin = MIO_UART1_TX_PIN;
#else
    uart_clk_ctrl = (volatile uint32_t*)&CRL_UART0_REF_CTRL;
    uart_rst_ctrl = (volatile uint32_t*)&CRL_RST_UART0;
    rx_pin = MIO_UART0_RX_PIN;
    tx_pin = MIO_UART0_TX_PIN;
#endif

    /* Configure MIO pins for UART (required in JTAG boot mode) */
    PMC_IOU_SLCR_MIO_PIN(tx_pin) = MIO_UART_TX_CFG;
    PMC_IOU_SLCR_MIO_PIN(rx_pin) = MIO_UART_RX_CFG;

    /* Ensure clock is enabled with proper divisor */
    *uart_clk_ctrl = 0x02000600;

    /* Clear UART reset */
    *uart_rst_ctrl = 0;

    /* Delay to let reset clear and clock stabilize */
    for (timeout = 1000; timeout > 0; timeout--)
        __asm__ volatile("nop");

    /* ===== Step 1: Disable UART before configuration (per TRM) ===== */
    UART_CR = 0;

    /* Wait for UART to finish any current TX (with timeout) */
    timeout = UART_TIMEOUT;
    while ((UART_FR & UART_FR_BUSY) && --timeout)
        ;

    /* ===== Step 2: Flush FIFOs by disabling FEN in LCR ===== */
    UART_LCR = 0;

    /* ===== Step 3: Clear all pending interrupts ===== */
    UART_IMSC = 0;           /* Disable all interrupts */
    UART_ICR = UART_INT_ALL; /* Clear any pending */

    /* ===== Step 4: Calculate and set baud rate divisors ===== */
    uart_calc_baud(UART_CLK_REF, DEBUG_UART_BAUD, &ibrd, &fbrd);
    UART_IBRD = ibrd;
    UART_FBRD = fbrd;

    /* ===== Step 5: Write LCR to latch baud rate (REQUIRED per TRM!) =====
     * The TRM states: "do write of LCR after writing to baud rate registers"
     * Configure: 8 data bits, 1 stop bit, no parity, FIFOs enabled */
    lcr = UART_LCR_WLEN_8 | UART_LCR_FEN;
    UART_LCR = lcr;

    /* ===== Step 6: Set FIFO trigger levels ===== */
    UART_IFLS = UART_IFLS_RXIFLSEL_1_2 | UART_IFLS_TXIFLSEL_1_2;

    /* ===== Step 7: Enable UART with TX and RX ===== */
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;

    /* Small delay to let UART stabilize */
    for (timeout = 100; timeout > 0; timeout--)
        __asm__ volatile("nop");
#endif /* EL2_HYPERVISOR */
}

static void uart_tx(uint8_t c)
{
    volatile uint32_t timeout = UART_TIMEOUT;

    /* Wait for TX FIFO to have space (not full) with timeout */
    while ((UART_FR & UART_FR_TXFF) && --timeout)
        ;

    /* Write character to data register */
    UART_DR = c;
}

void uart_write(const char *buf, uint32_t len)
{
    uint32_t i;
    volatile uint32_t timeout;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            uart_tx('\r');
        }
        uart_tx((uint8_t)buf[i]);
    }

    /* Wait for transmit FIFO to empty (with timeout) */
    timeout = UART_TIMEOUT;
    while (!(UART_FR & UART_FR_TXFE) && --timeout)
        ;

    /* Wait for UART to finish transmitting (with timeout) */
    timeout = UART_TIMEOUT;
    while ((UART_FR & UART_FR_BUSY) && --timeout)
        ;
}

#else
#define uart_init() do {} while(0)
#endif /* DEBUG_UART */


/* ============================================================================
 * Timer Functions (ARM Generic Timer)
 * ============================================================================
 */

/**
 * Get current timer count (physical counter)
 */
static inline uint64_t timer_get_count(void)
{
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    return cntpct;
}

/**
 * Get timer frequency
 */
static inline uint64_t timer_get_freq(void)
{
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    return cntfrq;
}

/**
 * Get current time in milliseconds
 */
uint64_t hal_timer_ms(void)
{
    uint64_t cntpct = timer_get_count();
    uint64_t cntfrq = timer_get_freq();

    if (cntfrq == 0)
        cntfrq = TIMER_CLK_FREQ;

    /* Convert to milliseconds: (count * 1000) / freq */
    return (cntpct * 1000ULL) / cntfrq;
}

/**
 * Delay for specified number of microseconds
 */
void hal_delay_us(uint32_t us)
{
    uint64_t cntfrq = timer_get_freq();
    uint64_t start, target;

    if (cntfrq == 0)
        cntfrq = TIMER_CLK_FREQ;

    start = timer_get_count();
    target = start + ((uint64_t)us * cntfrq) / 1000000ULL;

    while (timer_get_count() < target)
        ;
}


/* ============================================================================
 * HAL Public Interface
 * ============================================================================
 */

void hal_init(void)
{
    const char *banner = "\n"
        "========================================\n"
        "wolfBoot Secure Boot - AMD Versal\n"
        "========================================\n";

#ifdef DEBUG_UART
    uart_init();
#endif

    wolfBoot_printf("%s", banner);
    wolfBoot_printf("Current EL: %d\n", current_el());
    wolfBoot_printf("Timer Freq: %lu Hz\n", (unsigned long)timer_get_freq());

    /* TODO: Initialize flash controller (OSPI/SD) */
}

void hal_prepare_boot(void)
{
    /* Flush any pending UART output (with timeout) */
#ifdef DEBUG_UART
    {
        volatile uint32_t timeout = UART_TIMEOUT;
        while (!(UART_FR & UART_FR_TXFE) && --timeout)
            ;
        timeout = UART_TIMEOUT;
        while ((UART_FR & UART_FR_BUSY) && --timeout)
            ;
    }
#endif

    /* Memory barriers before jumping to application */
    dsb();
    isb();
}

#ifdef MMU
/**
 * Get the Device Tree address for the boot partition
 * Returns the DTS load address in RAM
 */
void* hal_get_dts_address(void)
{
#ifdef WOLFBOOT_LOAD_DTS_ADDRESS
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
#else
    return NULL;
#endif
}

/**
 * Get the update Device Tree address
 */
void* hal_get_dts_update_address(void)
{
#ifdef WOLFBOOT_DTS_UPDATE_ADDRESS
    return (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
#else
    return NULL;
#endif
}
#endif /* MMU */


/* ============================================================================
 * Flash Functions (STUBS)
 * ============================================================================
 * These are placeholder implementations.
 * Real implementation will depend on boot media:
 *   - OSPI flash (VERSAL_OSPI_BASE)
 *   - SD/eMMC via SDHCI (VERSAL_SD0_BASE / VERSAL_SD1_BASE)
 */

void RAMFUNCTION hal_flash_unlock(void)
{
    /* Stub - no-op for now */
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* Stub - no-op for now */
}

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    /* Stub - flash write not implemented */
    wolfBoot_printf("hal_flash_write: STUB (addr=0x%lx, len=%d)\n",
                    (unsigned long)address, len);
    return -1;
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;

    /* Stub - flash erase not implemented */
    wolfBoot_printf("hal_flash_erase: STUB (addr=0x%lx, len=%d)\n",
                    (unsigned long)address, len);
    return -1;
}


/* ============================================================================
 * External Flash Support (STUBS)
 * ============================================================================
 */

#ifdef EXT_FLASH

void ext_flash_lock(void)
{
    /* Stub */
}

void ext_flash_unlock(void)
{
    /* Stub */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    wolfBoot_printf("ext_flash_write: STUB\n");
    return -1;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;

    wolfBoot_printf("ext_flash_read: STUB\n");
    return -1;
}

int ext_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;

    wolfBoot_printf("ext_flash_erase: STUB\n");
    return -1;
}

#endif /* EXT_FLASH */


#endif /* TARGET_versal */

