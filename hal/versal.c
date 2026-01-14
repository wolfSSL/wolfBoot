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
 * QSPI Flash Driver
 * ============================================================================
 * Bare-metal QSPI driver for Versal VMK180
 * Dual parallel MT25QU01GBBB (128MB each, 256MB total)
 */

#ifdef EXT_FLASH

/* Debug logging for QSPI driver */
#ifdef DEBUG_QSPI
    #define QSPI_DEBUG_PRINTF(...) wolfBoot_printf(__VA_ARGS__)
#else
    #define QSPI_DEBUG_PRINTF(...) do {} while(0)
#endif

/* QSPI device structure */
typedef struct {
    uint32_t mode;   /* GQSPI_GEN_FIFO_MODE_SPI/DSPI/QSPI */
    uint32_t bus;    /* GQSPI_GEN_FIFO_BUS_LOW/UP/BOTH */
    uint32_t cs;     /* GQSPI_GEN_FIFO_CS_LOWER/UPPER/BOTH */
    uint32_t stripe; /* 0 or GQSPI_GEN_FIFO_STRIPE for dual parallel */
} QspiDev_t;

static QspiDev_t qspiDev;
static int qspi_initialized = 0;

/* Forward declarations */
static int qspi_transfer(QspiDev_t *dev, const uint8_t *txData, uint32_t txLen,
                         uint8_t *rxData, uint32_t rxLen, uint32_t dummyClocks);
static int qspi_wait_ready(QspiDev_t *dev);

/* Wait for GenFIFO empty (all entries processed) with timeout */
static int qspi_wait_genfifo_empty(void)
{
    uint32_t timeout = GQSPI_TIMEOUT_TRIES;
    uint32_t isr;

    isr = GQSPI_ISR;
    while (!(isr & GQSPI_IXR_GEN_FIFO_EMPTY) && --timeout) {
        isr = GQSPI_ISR;
    }
    if (timeout == 0) {
        QSPI_DEBUG_PRINTF("QSPI: GenFIFO empty timeout\n");
        return -1;
    }
    return 0;
}

/* Wait for TX FIFO empty with timeout */
static int qspi_wait_tx_empty(void)
{
    uint32_t timeout = GQSPI_TIMEOUT_TRIES;
    while (!(GQSPI_ISR & GQSPI_IXR_TX_FIFO_EMPTY) && --timeout)
        ;
    if (timeout == 0) {
        QSPI_DEBUG_PRINTF("QSPI: TX empty timeout\n");
        return -1;
    }
    return 0;
}

/* Write to GenFIFO (without triggering - batch mode) */
static int qspi_gen_fifo_push(uint32_t entry)
{
    uint32_t timeout = GQSPI_TIMEOUT_TRIES;
    uint32_t isr;

    /* Wait for GenFIFO not full */
    isr = GQSPI_ISR;
    while (!(isr & GQSPI_IXR_GEN_FIFO_NOT_FULL) && --timeout) {
        isr = GQSPI_ISR;
    }
    if (timeout == 0) {
        QSPI_DEBUG_PRINTF("QSPI: GenFIFO full timeout\n");
        return -1;
    }

    /* Write the entry to GenFIFO */
    GQSPI_GEN_FIFO = entry;

    return 0;
}

/* Trigger GenFIFO processing and wait for completion */
static int qspi_gen_fifo_start_and_wait(void)
{
    uint32_t cfg;
    uint32_t timeout = GQSPI_TIMEOUT_TRIES;
    uint32_t isr;

    dsb();  /* Ensure all writes complete */

    /* Trigger GenFIFO processing by setting START_GEN_FIFO */
    cfg = GQSPI_CFG;
    cfg |= GQSPI_CFG_START_GEN_FIFO;
    GQSPI_CFG = cfg;
    dsb();

    /* Wait for GenFIFO to empty (all entries processed) */
    isr = GQSPI_ISR;
    while (!(isr & GQSPI_IXR_GEN_FIFO_EMPTY) && --timeout) {
        isr = GQSPI_ISR;
    }
    if (timeout == 0) {
        QSPI_DEBUG_PRINTF("QSPI: GenFIFO start timeout\n");
        return -1;
    }
    return 0;
}

/* Legacy wrapper for compatibility */
static int qspi_gen_fifo_write(uint32_t entry)
{
    int ret = qspi_gen_fifo_push(entry);
    if (ret == 0) {
        ret = qspi_gen_fifo_start_and_wait();
    }
    return ret;
}

/* Chip select control */
static int qspi_cs(QspiDev_t *dev, int assert)
{
    uint32_t entry;

    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) | GQSPI_GEN_FIFO_MODE_SPI;
    if (assert) {
        entry |= (dev->cs & GQSPI_GEN_FIFO_CS_MASK);
    }
    /* Idle clocks for CS setup/hold */
    entry |= GQSPI_GEN_FIFO_IMM(2);

    return qspi_gen_fifo_write(entry);
}

/* TX via FIFO (polling mode) */
static int qspi_fifo_tx(const uint8_t *data, uint32_t len)
{
    uint32_t tmp32;
    uint32_t timeout;

    while (len > 0) {
        /* Wait for TX FIFO not full */
        timeout = GQSPI_TIMEOUT_TRIES;
        while ((GQSPI_ISR & GQSPI_IXR_TX_FIFO_FULL) && --timeout)
            ;
        if (timeout == 0) {
            QSPI_DEBUG_PRINTF("QSPI: TX FIFO full timeout\n");
            return -1;
        }

        if (len >= 4) {
            tmp32 = *((uint32_t*)data);
            GQSPI_TXD = tmp32;
            data += 4;
            len -= 4;
        } else {
            tmp32 = 0;
            memcpy(&tmp32, data, len);
            GQSPI_TXD = tmp32;
            len = 0;
        }
    }
    return 0;
}

/* RX via FIFO (polling mode) */
static int qspi_fifo_rx(uint8_t *data, uint32_t len)
{
    uint32_t tmp32;
    uint32_t timeout;

    while (len > 0) {
        /* Wait for RX FIFO not empty */
        timeout = GQSPI_TIMEOUT_TRIES;
        while (!(GQSPI_ISR & GQSPI_IXR_RX_FIFO_NOT_EMPTY) && --timeout)
            ;
        if (timeout == 0) {
            QSPI_DEBUG_PRINTF("QSPI: RX FIFO empty timeout\n");
            return -1;
        }

        tmp32 = GQSPI_RXD;
        if (len >= 4) {
            *((uint32_t*)data) = tmp32;
            data += 4;
            len -= 4;
        } else {
            memcpy(data, &tmp32, len);
            len = 0;
        }
    }
    return 0;
}

/* Core QSPI transfer function using GenFIFO */
static int qspi_transfer(QspiDev_t *dev, const uint8_t *txData, uint32_t txLen,
                         uint8_t *rxData, uint32_t rxLen, uint32_t dummyClocks)
{
    int ret = 0;
    uint32_t entry;
    uint32_t i;

    /* Enable GQSPI controller */
    GQSPI_EN = 1;
    dsb();

    /* Base entry: bus + CS + SPI mode */
    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
            (dev->cs & GQSPI_GEN_FIFO_CS_MASK) |
            GQSPI_GEN_FIFO_MODE_SPI;

    /* === CS Assert + TX Phase (batch all entries, then trigger) === */

    /* CS assertion entry - just set CS with some idle clocks */
    ret = qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(1));

    /* TX Phase - send command bytes via immediate data */
    for (i = 0; i < txLen && ret == 0; i++) {
        uint32_t txEntry = entry | GQSPI_GEN_FIFO_TX |
                           GQSPI_GEN_FIFO_IMM(txData[i]);
        ret = qspi_gen_fifo_push(txEntry);
    }

    /* Trigger and wait for TX to complete */
    if (ret == 0) {
        ret = qspi_gen_fifo_start_and_wait();
    }

    /* Dummy clocks phase (for fast read commands) */
    if (ret == 0 && dummyClocks > 0) {
        ret = qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(dummyClocks));
        if (ret == 0) {
            ret = qspi_gen_fifo_start_and_wait();
        }
    }

    /* === RX Phase === */
    if (dev->stripe) {
        /* Striped mode: IMM(1) reads 1 byte from each flash = 2 bytes total */
        for (i = 0; i < rxLen && ret == 0; i += 2) {
            uint32_t rxEntry = entry | GQSPI_GEN_FIFO_RX |
                               GQSPI_GEN_FIFO_DATA_XFER |
                               GQSPI_GEN_FIFO_STRIPE |
                               GQSPI_GEN_FIFO_IMM(1);

            ret = qspi_gen_fifo_push(rxEntry);
            if (ret == 0) {
                ret = qspi_gen_fifo_start_and_wait();
            }
            if (ret == 0) {
                /* Read 2 bytes (one from each flash, interleaved) */
                ret = qspi_fifo_rx(&rxData[i], 2);
            }
        }
    } else {
        /* Single flash: read 1 byte at a time */
        for (i = 0; i < rxLen && ret == 0; i++) {
            uint32_t rxEntry = entry | GQSPI_GEN_FIFO_RX |
                               GQSPI_GEN_FIFO_DATA_XFER |
                               GQSPI_GEN_FIFO_IMM(1);

            ret = qspi_gen_fifo_push(rxEntry);
            if (ret == 0) {
                ret = qspi_gen_fifo_start_and_wait();
            }
            if (ret == 0) {
                ret = qspi_fifo_rx(&rxData[i], 1);
            }
        }
    }

    /* === CS Deassert === */
    /* Remove CS bits from entry for deassert */
    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) | GQSPI_GEN_FIFO_MODE_SPI;
    qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(1));
    qspi_gen_fifo_start_and_wait();

    /* Disable controller */
    GQSPI_EN = 0;
    dsb();

    return ret;
}

/* Write page data to flash (for page programming) */
static int qspi_write_page(QspiDev_t *dev, const uint8_t *cmd, uint32_t cmdLen,
                           const uint8_t *data, uint32_t dataLen)
{
    int ret = 0;
    uint32_t entry;
    uint32_t i;

    /* Enable GQSPI controller */
    GQSPI_EN = 1;
    dsb();

    /* Base entry: bus + CS + SPI mode (page program uses SPI mode) */
    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
            (dev->cs & GQSPI_GEN_FIFO_CS_MASK) |
            GQSPI_GEN_FIFO_MODE_SPI;

    /* CS assertion */
    ret = qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(1));

    /* TX Phase - send command bytes (includes address) via immediate mode */
    for (i = 0; i < cmdLen && ret == 0; i++) {
        uint32_t txEntry = entry | GQSPI_GEN_FIFO_TX |
                           GQSPI_GEN_FIFO_IMM(cmd[i]);
        ret = qspi_gen_fifo_push(txEntry);
    }

    /* Trigger and wait for command to complete */
    if (ret == 0) {
        ret = qspi_gen_fifo_start_and_wait();
    }

    /* TX Phase - send data via TX FIFO (not immediate mode) */
    if (ret == 0 && dataLen > 0) {
        uint32_t txEntry = entry | GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_DATA_XFER;

        /* Note: stripe mode is handled externally by de-interleaving data
         * and writing to each flash separately */
        while (dataLen > 0 && ret == 0) {
            uint32_t chunkLen = (dataLen > 255) ? 255 : dataLen;
            uint32_t chunkEntry = txEntry | GQSPI_GEN_FIFO_IMM(chunkLen);

            ret = qspi_gen_fifo_push(chunkEntry);
            if (ret != 0) break;

            /* Start GenFIFO processing so it drains TX FIFO as we fill it */
            GQSPI_CFG |= GQSPI_CFG_START_GEN_FIFO;
            dsb();

            /* Push data to TX FIFO */
            ret = qspi_fifo_tx(data, chunkLen);
            if (ret != 0) break;

            /* Wait for GenFIFO to complete */
            ret = qspi_wait_genfifo_empty();

            data += chunkLen;
            dataLen -= chunkLen;
        }
    }

    /* CS Deassert */
    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) | GQSPI_GEN_FIFO_MODE_SPI;
    qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(1));
    qspi_gen_fifo_start_and_wait();

    /* Disable controller */
    GQSPI_EN = 0;
    dsb();

    return ret;
}

/* Read flash ID */
static int qspi_read_id(QspiDev_t *dev, uint8_t *id, uint32_t len)
{
    uint8_t cmd[1];
    int ret;

    cmd[0] = FLASH_CMD_READ_ID;
    ret = qspi_transfer(dev, cmd, 1, id, len, 0);

    return ret;
}

/* Read flash status register */
static int qspi_read_status(QspiDev_t *dev, uint8_t *status)
{
    uint8_t cmd[1];
    uint8_t data[4];  /* Space for 2 bytes from each chip */
    int ret;
    QspiDev_t tmpDev;

    /* For dual parallel, read status from each chip separately */
    if (dev->stripe) {
        /* Read from lower chip */
        tmpDev = *dev;
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        tmpDev.stripe = 0;
        cmd[0] = FLASH_CMD_READ_STATUS;
        ret = qspi_transfer(&tmpDev, cmd, 1, &data[0], 1, 0);
        if (ret != 0) return ret;

        /* Read from upper chip */
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_transfer(&tmpDev, cmd, 1, &data[1], 1, 0);
        if (ret != 0) return ret;

        /* AND the status from both chips */
        *status = data[0] & data[1];
        return 0;
    }

    cmd[0] = FLASH_CMD_READ_STATUS;
    ret = qspi_transfer(dev, cmd, 1, data, 1, 0);
    if (ret == 0) {
        *status = data[0];
    }
    return ret;
}

/* Read flash flag status register */
static int qspi_read_flag_status(QspiDev_t *dev, uint8_t *status)
{
    uint8_t cmd[1];
    uint8_t data[4];
    int ret;
    QspiDev_t tmpDev;

    /* For dual parallel, read status from each chip separately */
    if (dev->stripe) {
        /* Read from lower chip */
        tmpDev = *dev;
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        tmpDev.stripe = 0;
        cmd[0] = FLASH_CMD_READ_FLAG_STATUS;
        ret = qspi_transfer(&tmpDev, cmd, 1, &data[0], 1, 0);
        if (ret != 0) return ret;

        /* Read from upper chip */
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_transfer(&tmpDev, cmd, 1, &data[1], 1, 0);
        if (ret != 0) return ret;

        /* AND the status from both chips */
        *status = data[0] & data[1];
        return 0;
    }

    cmd[0] = FLASH_CMD_READ_FLAG_STATUS;
    ret = qspi_transfer(dev, cmd, 1, data, 1, 0);
    if (ret == 0) {
        *status = data[0];
    }
    return ret;
}

/* Wait for flash ready (not busy) */
static int qspi_wait_ready(QspiDev_t *dev)
{
    uint8_t status = 0;
    uint32_t timeout = GQSPI_FLASH_READY_TRIES;
    int ret;

    while (timeout-- > 0) {
        ret = qspi_read_flag_status(dev, &status);
        if (ret == 0 && (status & FLASH_FSR_READY)) {
            return 0;
        }
    }
    QSPI_DEBUG_PRINTF("QSPI: Flash ready timeout\n");
    return -1;
}

/* Write Enable */
static int qspi_write_enable(QspiDev_t *dev)
{
    uint8_t cmd[1];
    uint8_t status = 0;
    int ret;
    uint32_t timeout = GQSPI_FLASH_READY_TRIES;
    QspiDev_t tmpDev;

    cmd[0] = FLASH_CMD_WRITE_ENABLE;

    /* For dual parallel, send write enable to both chips separately */
    if (dev->stripe) {
        /* Send to lower chip */
        tmpDev = *dev;
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        tmpDev.stripe = 0;
        ret = qspi_transfer(&tmpDev, cmd, 1, NULL, 0, 0);
        if (ret != 0) return ret;

        /* Send to upper chip */
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_transfer(&tmpDev, cmd, 1, NULL, 0, 0);
        if (ret != 0) return ret;
    } else {
        ret = qspi_transfer(dev, cmd, 1, NULL, 0, 0);
        if (ret != 0) return ret;
    }

    /* Wait for WEL bit to be set */
    while (timeout-- > 0) {
        ret = qspi_read_status(dev, &status);
        if (ret == 0 && (status & FLASH_SR_WEL)) {
            return 0;
        }
    }
    QSPI_DEBUG_PRINTF("QSPI: Write enable timeout\n");
    return -1;
}

/* Write Disable */
static int qspi_write_disable(QspiDev_t *dev)
{
    uint8_t cmd[1];

    cmd[0] = FLASH_CMD_WRITE_DISABLE;
    return qspi_transfer(dev, cmd, 1, NULL, 0, 0);
}

/* Enter 4-byte address mode */
static int qspi_enter_4byte_addr(QspiDev_t *dev)
{
    uint8_t cmd[1];
    int ret;

    qspi_wait_ready(dev);
    ret = qspi_write_enable(dev);
    if (ret != 0) return ret;

    cmd[0] = FLASH_CMD_ENTER_4B_MODE;
    ret = qspi_transfer(dev, cmd, 1, NULL, 0, 0);
    QSPI_DEBUG_PRINTF("QSPI: Enter 4-byte mode: ret=%d\n", ret);

    if (ret == 0) {
        ret = qspi_wait_ready(dev);
    }
    qspi_write_disable(dev);
    return ret;
}

#ifdef TEST_EXT_FLASH
#ifndef TEST_EXT_ADDRESS
#define TEST_EXT_ADDRESS 0x2800000 /* 40MB */
#endif

static int test_ext_flash(QspiDev_t* dev)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE * 4];

    (void)dev;
    wolfBoot_printf("Testing ext flash at 0x%x...\n", TEST_EXT_ADDRESS);

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, FLASH_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i = 0; i < sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_EXT_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);
    if (ret != 0) {
        wolfBoot_printf("Flash read failed!\n");
        return ret;
    }

    /* Print first 32 bytes of data */
    wolfBoot_printf("Data: ");
    for (i = 0; i < 32 && i < sizeof(pageData); i++) {
        wolfBoot_printf("%02x ", pageData[i]);
    }
    wolfBoot_printf("...\n");

#ifndef TEST_FLASH_READONLY
    wolfBoot_printf("Checking pattern...\n");
    /* Check data */
    for (i = 0; i < sizeof(pageData); i++) {
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed: got 0x%02x, expected 0x%02x\n",
                           i, pageData[i], (i & 0xff));
            return -1;
        }
    }
    wolfBoot_printf("Flash Test Passed!\n");
#else
    wolfBoot_printf("Flash Read Test Complete (readonly mode)\n");
#endif

    return ret;
}
#endif /* TEST_EXT_FLASH */

/* Initialize QSPI controller */
static void qspi_init(void)
{
    uint32_t cfg;
    uint8_t id[4];
    int ret;

    QSPI_DEBUG_PRINTF("QSPI: Initializing (base=0x%lx)...\n",
                      (unsigned long)VERSAL_QSPI_BASE);

    /* Read initial state left by PLM */
    cfg = GQSPI_CFG;
    QSPI_DEBUG_PRINTF("QSPI: PLM state - CFG=0x%08x ISR=0x%08x\n",
                      cfg, GQSPI_ISR);

    /* Disable controller during reconfiguration */
    GQSPI_EN = 0;
    dsb();

    /* Select GQSPI mode (not linear LQSPI) */
    GQSPI_SEL = GQSPI_SEL_GQSPI;
    dsb();

    /* Don't reset FIFOs - just drain any stale data by reading RXD */
    while (GQSPI_ISR & GQSPI_IXR_RX_FIFO_NOT_EMPTY) {
        (void)GQSPI_RXD;  /* Discard any stale RX data */
    }

    /* Clear all interrupt status bits */
    GQSPI_ISR = GQSPI_IXR_ALL_MASK;
    dsb();

    /* Preserve PLM's CFG but switch to IO mode for our transfers
     * PLM: 0xA0080010 = DMA mode | manual start | WP_HOLD | CLK_POL
     * Key: Keep manual start mode (bit 29) and clock settings */
    cfg = (cfg & ~GQSPI_CFG_MODE_EN_MASK);  /* Clear mode bits */
    cfg |= GQSPI_CFG_MODE_EN_IO;            /* Set IO mode */
    GQSPI_CFG = cfg;
    dsb();

    /* Set thresholds */
    GQSPI_TX_THRESH = 1;
    GQSPI_RX_THRESH = 1;
    GQSPI_GF_THRESH = 16;

    QSPI_DEBUG_PRINTF("QSPI: After config - CFG=0x%08x\n", GQSPI_CFG);

    /* Configure device for single flash (lower) first */
    qspiDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
    qspiDev.stripe = 0;

    memset(id, 0, sizeof(id));
    ret = qspi_read_id(&qspiDev, id, 3);
    wolfBoot_printf("QSPI: Lower ID: %02x %02x %02x\n", id[0], id[1], id[2]);

    /* Enter 4-byte address mode for lower flash */
    ret = qspi_enter_4byte_addr(&qspiDev);
    if (ret != 0) {
        QSPI_DEBUG_PRINTF("QSPI: 4-byte mode failed (lower)\n");
    }

    /* Read ID from upper flash */
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_UP;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_UPPER;

    memset(id, 0, sizeof(id));
    ret = qspi_read_id(&qspiDev, id, 3);
    wolfBoot_printf("QSPI: Upper ID: %02x %02x %02x\n", id[0], id[1], id[2]);

    /* Enter 4-byte address mode for upper flash */
    ret = qspi_enter_4byte_addr(&qspiDev);
    if (ret != 0) {
        QSPI_DEBUG_PRINTF("QSPI: 4-byte mode failed (upper)\n");
    }

    /* Configure for dual parallel operation */
    qspiDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_BOTH;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_BOTH;
    qspiDev.stripe = GQSPI_GEN_FIFO_STRIPE;

    /* QSPI bare-metal driver */
    wolfBoot_printf("QSPI Init: Ref=%dMHz, Div=%d, Bus=%d, IO=%s\n",
        GQSPI_CLK_REF/1000000,
        (2 << GQSPI_CLK_DIV),
        (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)),
    #ifdef GQSPI_MODE_IO
        "Poll"
    #else
        "DMA"
    #endif
    );

    qspi_initialized = 1;

#ifdef TEST_EXT_FLASH
    test_ext_flash(&qspiDev);
#endif
}

#endif /* EXT_FLASH */


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

#ifdef EXT_FLASH
    qspi_init();
#endif
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
 * External Flash Interface
 * ============================================================================
 */

#ifdef EXT_FLASH

void ext_flash_lock(void)
{
    /* No-op - flash protection handled elsewhere */
}

void ext_flash_unlock(void)
{
    /* No-op - flash protection handled elsewhere */
}

/* Write to a single flash chip - used for dual parallel writes */
static int ext_flash_write_chip(uintptr_t addr, const uint8_t *data, int len,
                                 uint32_t bus, uint32_t cs)
{
    int ret = 0;
    uint8_t cmd[5];
    uint32_t xferSz, page, pages;
    QspiDev_t tmpDev;

    tmpDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    tmpDev.bus = bus;
    tmpDev.cs = cs;
    tmpDev.stripe = 0;

    /* Write by page */
    pages = ((len + (FLASH_PAGE_SIZE - 1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages && ret == 0; page++) {
        ret = qspi_write_enable(&tmpDev);
        if (ret != 0) break;

        xferSz = len;
        if (xferSz > FLASH_PAGE_SIZE)
            xferSz = FLASH_PAGE_SIZE;

        /* Page Program with 4-byte address */
        cmd[0] = FLASH_CMD_PAGE_PROG_4B;
        cmd[1] = ((addr + page * FLASH_PAGE_SIZE) >> 24) & 0xFF;
        cmd[2] = ((addr + page * FLASH_PAGE_SIZE) >> 16) & 0xFF;
        cmd[3] = ((addr + page * FLASH_PAGE_SIZE) >> 8) & 0xFF;
        cmd[4] = (addr + page * FLASH_PAGE_SIZE) & 0xFF;

        /* Send command + data */
        ret = qspi_write_page(&tmpDev, cmd, 5,
                              data + (page * FLASH_PAGE_SIZE), xferSz);
        if (ret != 0) break;

        ret = qspi_wait_ready(&tmpDev);
        qspi_write_disable(&tmpDev);
        len -= xferSz;
    }

    return ret;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int ret = 0;
    uint8_t cmd[5];
    uint32_t xferSz, page, pages;
    uintptr_t addr;
    uint32_t i;

    if (!qspi_initialized) {
        return -1;
    }

    if (qspiDev.stripe) {
        /* For dual parallel: split data and write to each flash separately */
        uint8_t *lower_data = NULL;
        uint8_t *upper_data = NULL;
        uint32_t half_len = (len + 1) / 2;
        uintptr_t flash_addr = address / 2;

        /* Allocate temp buffers for split data */
        /* Note: For production, would use static buffer or stack */
        static uint8_t lower_buf[FLASH_PAGE_SIZE * 4];
        static uint8_t upper_buf[FLASH_PAGE_SIZE * 4];

        if (len > (int)sizeof(lower_buf) * 2) {
            QSPI_DEBUG_PRINTF("ext_flash_write: len too large\n");
            return -1;
        }

        lower_data = lower_buf;
        upper_data = upper_buf;

        /* De-interleave data: even bytes to lower, odd bytes to upper */
        for (i = 0; i < (uint32_t)len; i++) {
            if (i & 1) {
                upper_data[i / 2] = data[i];
            } else {
                lower_data[i / 2] = data[i];
            }
        }

        /* Write to lower flash */
        ret = ext_flash_write_chip(flash_addr, lower_data, half_len,
                                   GQSPI_GEN_FIFO_BUS_LOW, GQSPI_GEN_FIFO_CS_LOWER);
        if (ret != 0) return ret;

        /* Write to upper flash */
        ret = ext_flash_write_chip(flash_addr, upper_data, half_len,
                                   GQSPI_GEN_FIFO_BUS_UP, GQSPI_GEN_FIFO_CS_UPPER);
        return ret;
    }

    /* Single flash write */
    pages = ((len + (FLASH_PAGE_SIZE - 1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages && ret == 0; page++) {
        ret = qspi_write_enable(&qspiDev);
        if (ret != 0) break;

        xferSz = len;
        if (xferSz > FLASH_PAGE_SIZE)
            xferSz = FLASH_PAGE_SIZE;

        addr = address + (page * FLASH_PAGE_SIZE);

        /* Page Program with 4-byte address */
        cmd[0] = FLASH_CMD_PAGE_PROG_4B;
        cmd[1] = (addr >> 24) & 0xFF;
        cmd[2] = (addr >> 16) & 0xFF;
        cmd[3] = (addr >> 8) & 0xFF;
        cmd[4] = addr & 0xFF;

        /* Send command + data */
        ret = qspi_write_page(&qspiDev, cmd, 5,
                              data + (page * FLASH_PAGE_SIZE), xferSz);
        if (ret != 0) break;

        ret = qspi_wait_ready(&qspiDev);
        qspi_write_disable(&qspiDev);
        len -= xferSz;
    }

    return ret;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uint8_t cmd[5];
    int ret;
    uintptr_t addr = address;
    uint32_t i;

    if (!qspi_initialized) {
        return -1;
    }

    if (qspiDev.stripe) {
        /* For dual parallel: read from each chip separately and interleave */
        static uint8_t lower_buf[FLASH_PAGE_SIZE * 4];
        static uint8_t upper_buf[FLASH_PAGE_SIZE * 4];
        uint32_t half_len = (len + 1) / 2;
        QspiDev_t tmpDev;

        if (len > (int)sizeof(lower_buf) * 2) {
            return -1;
        }

        addr = address / 2;  /* Flash address for each chip */

        cmd[0] = FLASH_CMD_READ_4B;
        cmd[1] = (addr >> 24) & 0xFF;
        cmd[2] = (addr >> 16) & 0xFF;
        cmd[3] = (addr >> 8) & 0xFF;
        cmd[4] = addr & 0xFF;

        /* Read from lower chip */
        tmpDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
        tmpDev.stripe = 0;
        ret = qspi_transfer(&tmpDev, cmd, 5, lower_buf, half_len, 0);
        if (ret != 0) return ret;

        /* Read from upper chip */
        tmpDev.bus = GQSPI_GEN_FIFO_BUS_UP;
        tmpDev.cs = GQSPI_GEN_FIFO_CS_UPPER;
        ret = qspi_transfer(&tmpDev, cmd, 5, upper_buf, half_len, 0);
        if (ret != 0) return ret;

        /* Interleave: even positions from lower, odd positions from upper */
        for (i = 0; i < (uint32_t)len; i++) {
            if (i & 1) {
                data[i] = upper_buf[i / 2];
            } else {
                data[i] = lower_buf[i / 2];
            }
        }
        return 0;
    }

    /* Single flash read */
    cmd[0] = FLASH_CMD_READ_4B;
    cmd[1] = (addr >> 24) & 0xFF;
    cmd[2] = (addr >> 16) & 0xFF;
    cmd[3] = (addr >> 8) & 0xFF;
    cmd[4] = addr & 0xFF;

    ret = qspi_transfer(&qspiDev, cmd, 5, data, len, 0);

    return ret;
}

int ext_flash_erase(uintptr_t address, int len)
{
    int ret = 0;
    uint8_t cmd[5];
    uintptr_t addr;

    if (!qspi_initialized) {
        return -1;
    }

    while (len > 0 && ret == 0) {
        addr = address;
        if (qspiDev.stripe) {
            /* For dual parallel the address divide by 2 */
            addr /= 2;
        }

        ret = qspi_write_enable(&qspiDev);
        if (ret != 0) break;

        /* Sector Erase with 4-byte address */
        cmd[0] = FLASH_CMD_SECTOR_ERASE_4B;
        cmd[1] = (addr >> 24) & 0xFF;
        cmd[2] = (addr >> 16) & 0xFF;
        cmd[3] = (addr >> 8) & 0xFF;
        cmd[4] = addr & 0xFF;

        ret = qspi_transfer(&qspiDev, cmd, 5, NULL, 0, 0);
        QSPI_DEBUG_PRINTF("ext_flash_erase: addr=0x%lx\n", (unsigned long)address);

        if (ret == 0) {
            ret = qspi_wait_ready(&qspiDev);
        }
        qspi_write_disable(&qspiDev);

        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }

    return ret;
}

#endif /* EXT_FLASH */


#endif /* TARGET_versal */

