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
 *   - UART driver (ARM PL011 UART / UARTPSV)
 *   - ARM Generic Timer
 *   - QSPI flash driver (GQSPI - dual parallel MT25QU01GBBB)
 *
 * QSPI Driver Notes:
 *   This driver is a port of the ZynqMP GQSPI driver (hal/zynq.c) with the
 *   following Versal-specific adaptations:
 *
 *   1. Different base address (0xF1030000 vs 0xFF0F0000)
 *   2. Tap delay bypass register is in QSPI block (not IOU_SLCR)
 *   3. Preserves PLM's QSPI configuration instead of full reset
 *   4. UART init skips MIO/clock setup when EL2 (PLM already did it)
 *
 *   The register layout, GenFIFO format, and DMA interface are identical
 *   to ZynqMP since both use the same Xilinx GQSPI IP block.
 *
 *   See hal/versal.h for detailed comparison with ZynqMP.
 */

#ifdef TARGET_versal

#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hal/versal.h"
#include "image.h"
#include "printf.h"
#include "fdt.h"

#ifndef ARCH_AARCH64
#   error "wolfBoot versal HAL: wrong architecture. Please compile with ARCH=AARCH64."
#endif

/* ============================================================================
 * Linux Boot Arguments
 * ============================================================================
 * DTB fixup for kernel command line. Override LINUX_BOOTARGS or
 * LINUX_BOOTARGS_ROOT in your config to customize.
 */

/* Linux kernel command line arguments */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p2"
#endif

#define LINUX_BOOTARGS \
    "earlycon root=" LINUX_BOOTARGS_ROOT " rootwait"
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

/* Get current timer count (physical counter) */
static inline uint64_t timer_get_count(void)
{
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    return cntpct;
}

/* Get timer frequency with fallback to TIMER_CLK_FREQ if not configured */
static inline uint64_t timer_get_freq(void)
{
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    return cntfrq ? cntfrq : TIMER_CLK_FREQ;
}

/* Get current time in milliseconds */
uint64_t hal_timer_ms(void)
{
    return (timer_get_count() * 1000ULL) / timer_get_freq();
}

/* Delay for specified number of microseconds */
void hal_delay_us(uint32_t us)
{
    uint64_t freq = timer_get_freq();
    uint64_t target = timer_get_count() + ((uint64_t)us * freq) / 1000000ULL;

    while (timer_get_count() < target)
        ;
}

/* Get current time in microseconds (for benchmarking) */
uint64_t hal_get_timer_us(void)
{
    return (timer_get_count() * 1000000ULL) / timer_get_freq();
}


/* ============================================================================
 * QSPI Flash Driver (GQSPI)
 * ============================================================================
 * Bare-metal QSPI driver for Versal VMK180.
 * Hardware: Dual parallel MT25QU01GBBB (128MB each, 256MB total).
 *
 * This driver is adapted from the ZynqMP GQSPI driver (hal/zynq.c).
 * Both platforms use the same Xilinx GQSPI IP block with identical:
 *   - Register offsets (GQSPI at +0x100, DMA at +0x800 from base)
 *   - GenFIFO entry format (command, address, data, stripe bits)
 *   - Interrupt status bits and DMA interface
 *
 * Versal-specific differences from ZynqMP:
 *   - Base address: 0xF1030000 (vs 0xFF0F0000 on ZynqMP)
 *   - Tap delay register: In QSPI block (vs IOU_SLCR on ZynqMP)
 *   - Initialization: Preserves PLM config (vs full reset on ZynqMP)
 *
 * Supported modes (same as ZynqMP):
 *   - DMA mode (default) or IO polling mode (GQSPI_MODE_IO)
 *   - Quad SPI (4-bit), Dual SPI (2-bit), or Standard SPI (1-bit)
 *   - 4-byte addressing for flash >16MB (GQPI_USE_4BYTE_ADDR)
 *   - Dual parallel with hardware striping (GQPI_USE_DUAL_PARALLEL)
 *   - EXP (exponent) length mode for large transfers
 *
 * Clock: 300MHz ref / (2 << DIV) = 75MHz default (DIV=1)
 *        MT25QU01GBBB supports up to 133MHz for Quad Output Read.
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

/* Macros to configure QspiDev_t for single-chip access in dual-parallel mode */
#define QSPI_DEV_LOWER(tmpDev, srcDev) do { \
    (tmpDev) = *(srcDev); \
    (tmpDev).bus = GQSPI_GEN_FIFO_BUS_LOW; \
    (tmpDev).cs = GQSPI_GEN_FIFO_CS_LOWER; \
    (tmpDev).stripe = 0; \
} while(0)

#define QSPI_DEV_UPPER(tmpDev) do { \
    (tmpDev).bus = GQSPI_GEN_FIFO_BUS_UP; \
    (tmpDev).cs = GQSPI_GEN_FIFO_CS_UPPER; \
} while(0)

static QspiDev_t qspiDev;
static int qspi_initialized = 0;

/* Forward declarations */
static int qspi_transfer(QspiDev_t *dev, const uint8_t *txData, uint32_t txLen,
                         uint8_t *rxData, uint32_t rxLen, uint32_t dummyClocks,
                         const uint8_t *writeData, uint32_t writeLen);
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

/* Calculate EXP mode for large transfers (returns actual transfer size)
 * For transfers > 255 bytes, use exponent mode where IMM = power of 2
 * Pattern from zynq.c qspi_calc_exp() */
static uint32_t qspi_calc_exp(uint32_t xferSz, uint32_t *reg_genfifo)
{
    uint32_t expval;

    *reg_genfifo &= ~(GQSPI_GEN_FIFO_IMM_MASK | GQSPI_GEN_FIFO_EXP);

    if (xferSz > GQSPI_GEN_FIFO_IMM_MASK) {
        /* Use exponent mode (max is 2^28 for DMA) */
        for (expval = 28; expval >= 8; expval--) {
            /* Find highest power of 2 that fits */
            if (xferSz >= (1UL << expval)) {
                *reg_genfifo |= GQSPI_GEN_FIFO_EXP;
                *reg_genfifo |= GQSPI_GEN_FIFO_IMM(expval);
                xferSz = (1UL << expval);
                break;
            }
        }
    } else {
        /* Use immediate length mode */
        *reg_genfifo |= GQSPI_GEN_FIFO_IMM(xferSz);
    }
    return xferSz;
}

/* Chip select control */
static int qspi_cs(QspiDev_t *dev, int assert)
{
    uint32_t entry;
    int ret;

    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) | GQSPI_GEN_FIFO_MODE_SPI;
    if (assert) {
        entry |= (dev->cs & GQSPI_GEN_FIFO_CS_MASK);
    }
    /* Idle clocks for CS setup/hold */
    entry |= GQSPI_GEN_FIFO_IMM(2);

    ret = qspi_gen_fifo_push(entry);
    if (ret == 0) {
        ret = qspi_gen_fifo_start_and_wait();
    }
    return ret;
}

/* DMA temporary buffer for unaligned transfers (DMA is default, IO is optional) */
#ifndef GQSPI_MODE_IO
static uint8_t XALIGNED(GQSPI_DMA_ALIGN) dma_tmpbuf[GQSPI_DMA_TMPSZ];

/* Flush data cache for DMA coherency */
static void flush_dcache_range(uintptr_t start, uintptr_t end)
{
    /* ARM64: Clean and invalidate by virtual address to PoC */
    uintptr_t addr;
    for (addr = (start & ~(GQSPI_DMA_ALIGN - 1)); addr < end;
         addr += GQSPI_DMA_ALIGN) {
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

/* Wait for DMA completion
 * Returns: 0 on success, -1 on timeout
 */
static int qspi_dma_wait(void)
{
    uint32_t timeout = GQSPIDMA_TIMEOUT_TRIES;

    while (!(GQSPIDMA_ISR & GQSPIDMA_ISR_DONE) && --timeout)
        ;

    if (timeout == 0) {
        QSPI_DEBUG_PRINTF("QSPI: DMA timeout\n");
        /* Clear any pending interrupts */
        GQSPIDMA_ISR = GQSPIDMA_ISR_ALL_MASK;
        return -1;
    }

    /* Clear DMA done interrupt */
    GQSPIDMA_ISR = GQSPIDMA_ISR_DONE;
    return 0;
}
#endif /* !GQSPI_MODE_IO */

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

/* RX using FIFO polling (IO mode) - helper to avoid code duplication */
static int qspi_rx_io_mode(uint8_t *rxData, uint32_t rxLen, uint32_t *rxEntry)
{
    int ret = 0;
    uint32_t remaining = rxLen;
    uint32_t offset = 0;
    uint32_t xferSz;

    while (ret == 0 && remaining > 0) {
        xferSz = qspi_calc_exp(remaining, rxEntry);
        ret = qspi_gen_fifo_push(*rxEntry);
        if (ret == 0) {
            ret = qspi_gen_fifo_start_and_wait();
        }
        if (ret == 0) {
            ret = qspi_fifo_rx(&rxData[offset], xferSz);
        }
        offset += xferSz;
        remaining -= xferSz;
    }
    return ret;
}

/* Core QSPI transfer function using GenFIFO */
static int qspi_transfer(QspiDev_t *dev, const uint8_t *txData, uint32_t txLen,
                         uint8_t *rxData, uint32_t rxLen, uint32_t dummyClocks,
                         const uint8_t *writeData, uint32_t writeLen)
{
    int ret = 0;
    uint32_t entry;
    uint32_t i;
    uint32_t chunkLen;
    uint32_t txEntry, chunkEntry;
    const uint8_t *writePtr;
    uint32_t remaining, xferSz;
    uint32_t rxEntry;

    /* Enable GQSPI controller */
    /* Set DMA mode for fast/quad reads (indicated by dummyClocks > 0) unless IO mode forced */
    if (dummyClocks > 0 && rxLen > 0) {
#ifndef GQSPI_MODE_IO
        GQSPI_CFG = (GQSPI_CFG & ~GQSPI_CFG_MODE_EN_MASK) | GQSPI_CFG_MODE_EN_DMA;
#endif
    }
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

    /* Dummy clocks phase (for fast read commands)
     * Use QSPI mode if dummy clocks are present (indicates Quad Read) */
    if (ret == 0 && dummyClocks > 0) {
        uint32_t dummyEntry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
                              (dev->cs & GQSPI_GEN_FIFO_CS_MASK) |
                              GQSPI_QSPI_MODE |
                              GQSPI_GEN_FIFO_DATA_XFER |
                              GQSPI_GEN_FIFO_IMM(dummyClocks);
        ret = qspi_gen_fifo_push(dummyEntry);
        if (ret == 0) {
            ret = qspi_gen_fifo_start_and_wait();
        }
    }

    /* === TX Write Data Phase === */
    if (ret == 0 && writeLen > 0 && writeData != NULL) {
        txEntry = entry | GQSPI_GEN_FIFO_TX | GQSPI_GEN_FIFO_DATA_XFER |
                  (dev->stripe & GQSPI_GEN_FIFO_STRIPE);
        writePtr = writeData;
        chunkLen = writeLen;

        while (chunkLen > 0 && ret == 0) {
            uint32_t chunk = (chunkLen > 255) ? 255 : chunkLen;
            chunkEntry = txEntry | GQSPI_GEN_FIFO_IMM(chunk);

            ret = qspi_gen_fifo_push(chunkEntry);
            if (ret != 0) break;

            /* Start GenFIFO processing so it drains TX FIFO as we fill it */
            GQSPI_CFG |= GQSPI_CFG_START_GEN_FIFO;
            dsb();

            /* Push data to TX FIFO */
            ret = qspi_fifo_tx(writePtr, chunk);
            if (ret != 0) break;

            /* Wait for GenFIFO to complete */
            ret = qspi_wait_genfifo_empty();

            writePtr += chunk;
            chunkLen -= chunk;
        }
    }

    /* === RX Phase === */
    if (ret == 0 && rxLen > 0) {
        /* Use QSPI mode for RX if dummy clocks were used (Quad Read) */
        if (dummyClocks > 0) {
            rxEntry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) |
                      (dev->cs & GQSPI_GEN_FIFO_CS_MASK) |
                      GQSPI_QSPI_MODE |
                      GQSPI_GEN_FIFO_RX |
                      GQSPI_GEN_FIFO_DATA_XFER |
                      (dev->stripe & GQSPI_GEN_FIFO_STRIPE);

#ifndef GQSPI_MODE_IO
            /* DMA mode: Use DMA for RX phase */
            if ((GQSPI_CFG & GQSPI_CFG_MODE_EN_MASK) == GQSPI_CFG_MODE_EN_DMA) {
                uint8_t *dmaPtr;
                uint32_t dmaLen;
                int useTemp = 0;

                /* Check alignment - DMA requires cache-line aligned buffer.
                 * If unaligned or not a multiple of 4 bytes, use temp buffer.
                 * CRITICAL: GenFIFO transfer size must match DMA size! */
                if (((uintptr_t)rxData & (GQSPI_DMA_ALIGN - 1)) || (rxLen & 3)) {
                    /* Use temp buffer for unaligned data */
                    dmaPtr = dma_tmpbuf;
                    /* Bounds check before alignment to prevent integer overflow */
                    if (rxLen > sizeof(dma_tmpbuf)) {
                        dmaLen = sizeof(dma_tmpbuf);
                    } else {
                        dmaLen = (rxLen + GQSPI_DMA_ALIGN - 1) & ~(GQSPI_DMA_ALIGN - 1);
                        if (dmaLen > sizeof(dma_tmpbuf)) {
                            dmaLen = sizeof(dma_tmpbuf);
                        }
                    }
                    useTemp = 1;
                } else {
                    dmaPtr = rxData;
                    dmaLen = rxLen;
                }

                /* GenFIFO must request the same number of bytes as DMA expects */
                remaining = dmaLen;

                /* Setup DMA destination */
                GQSPIDMA_DST = ((uintptr_t)dmaPtr & 0xFFFFFFFFUL);
                GQSPIDMA_DST_MSB = ((uintptr_t)dmaPtr >> 32);
                GQSPIDMA_SIZE = dmaLen;

                /* Enable DMA done interrupt */
                GQSPIDMA_IER = GQSPIDMA_ISR_DONE;

                /* Flush dcache for DMA coherency */
                flush_dcache_range((uintptr_t)dmaPtr, (uintptr_t)dmaPtr + dmaLen);

                /* Push all GenFIFO entries first (use EXP mode for large transfers) */
                while (ret == 0 && remaining > 0) {
                    xferSz = qspi_calc_exp(remaining, &rxEntry);
                    ret = qspi_gen_fifo_push(rxEntry);
                    remaining -= xferSz;
                }

                /* Trigger GenFIFO */
                if (ret == 0) {
                    GQSPI_CFG |= GQSPI_CFG_START_GEN_FIFO;
                    dsb();
                }

                /* Wait for DMA completion */
                if (ret == 0) {
                    ret = qspi_dma_wait();
                }

                /* Invalidate cache after DMA */
                flush_dcache_range((uintptr_t)dmaPtr, (uintptr_t)dmaPtr + dmaLen);

                /* Copy from temp buffer if needed (only copy requested bytes) */
                if (ret == 0 && useTemp) {
                    memcpy(rxData, dmaPtr, rxLen);
                }
            } else {
                /* IO mode: Use FIFO polling (fallback when DMA mode not enabled) */
                ret = qspi_rx_io_mode(rxData, rxLen, &rxEntry);
            }
#else /* GQSPI_MODE_IO */
            /* IO mode: Use FIFO polling */
            ret = qspi_rx_io_mode(rxData, rxLen, &rxEntry);
#endif /* !GQSPI_MODE_IO */
        } else {
            /* SPI mode for simple reads */
            rxEntry = entry | GQSPI_GEN_FIFO_RX |
                      GQSPI_GEN_FIFO_DATA_XFER |
                      (dev->stripe & GQSPI_GEN_FIFO_STRIPE) |
                      GQSPI_GEN_FIFO_IMM(1);
            uint32_t readSz = dev->stripe ? 2 : 1;

            for (i = 0; i < rxLen && ret == 0; i += readSz) {
                ret = qspi_gen_fifo_push(rxEntry);
                if (ret == 0) {
                    ret = qspi_gen_fifo_start_and_wait();
                }
                if (ret == 0) {
                    ret = qspi_fifo_rx(&rxData[i], readSz);
                }
            }
        }
    }

    /* === CS Deassert === */
    /* Remove CS bits from entry for deassert */
    entry = (dev->bus & GQSPI_GEN_FIFO_BUS_MASK) | GQSPI_GEN_FIFO_MODE_SPI;
    qspi_gen_fifo_push(entry | GQSPI_GEN_FIFO_IMM(1));
    qspi_gen_fifo_start_and_wait();

    /* Switch back to IO mode if DMA was used and disable controller */
#ifndef GQSPI_MODE_IO
    if ((GQSPI_CFG & GQSPI_CFG_MODE_EN_MASK) == GQSPI_CFG_MODE_EN_DMA) {
        GQSPI_CFG = (GQSPI_CFG & ~GQSPI_CFG_MODE_EN_MASK) | GQSPI_CFG_MODE_EN_IO;
    }
#endif
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
    ret = qspi_transfer(dev, cmd, 1, id, len, 0, NULL, 0);

    return ret;
}

/* Generic flash register read helper (handles dual parallel) */
static int qspi_read_register(QspiDev_t *dev, uint8_t cmd, uint8_t *status)
{
    uint8_t cmdByte[1];
    uint8_t data[2];
    int ret;
    QspiDev_t tmpDev;

    cmdByte[0] = cmd;

    /* For dual parallel, read from each chip separately and AND the results */
    if (dev->stripe) {
        QSPI_DEV_LOWER(tmpDev, dev);
        ret = qspi_transfer(&tmpDev, cmdByte, 1, &data[0], 1, 0, NULL, 0);
        if (ret != 0) return ret;

        QSPI_DEV_UPPER(tmpDev);
        ret = qspi_transfer(&tmpDev, cmdByte, 1, &data[1], 1, 0, NULL, 0);
        if (ret != 0) return ret;

        *status = data[0] & data[1];
        return 0;
    }

    /* Single chip mode */
    ret = qspi_transfer(dev, cmdByte, 1, data, 1, 0, NULL, 0);
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
        ret = qspi_read_register(dev, FLASH_CMD_READ_FLAG_STATUS, &status);
        if (ret == 0 && (status & FLASH_FSR_READY)) {
            return 0;
        }
        /* Add small delay every 100 polls to reduce bus traffic during erase/write ops */
        if ((timeout % 100) == 0) {
            hal_delay_us(10);
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
        QSPI_DEV_LOWER(tmpDev, dev);
        ret = qspi_transfer(&tmpDev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
        if (ret != 0) return ret;

        QSPI_DEV_UPPER(tmpDev);
        ret = qspi_transfer(&tmpDev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
        if (ret != 0) return ret;
    } else {
        ret = qspi_transfer(dev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
        if (ret != 0) return ret;
    }

    /* Wait for WEL bit to be set */
    while (timeout-- > 0) {
        ret = qspi_read_register(dev, FLASH_CMD_READ_STATUS, &status);
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
    return qspi_transfer(dev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
}

#if GQPI_USE_4BYTE_ADDR == 1
/* Enter 4-byte address mode */
static int qspi_enter_4byte_addr(QspiDev_t *dev)
{
    uint8_t cmd[1];
    int ret;

    qspi_wait_ready(dev);
    ret = qspi_write_enable(dev);
    if (ret != 0) return ret;

    cmd[0] = FLASH_CMD_ENTER_4B_MODE;
    ret = qspi_transfer(dev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
    QSPI_DEBUG_PRINTF("QSPI: Enter 4-byte mode: ret=%d\n", ret);

    if (ret == 0) {
        ret = qspi_wait_ready(dev);
    }
    qspi_write_disable(dev);
    return ret;
}

/* Exit 4-byte address mode */
static int qspi_exit_4byte_addr(QspiDev_t *dev)
{
    uint8_t cmd[1];
    int ret;

    ret = qspi_write_enable(dev);
    if (ret != 0) return ret;

    cmd[0] = FLASH_CMD_EXIT_4B_MODE;
    ret = qspi_transfer(dev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);
    QSPI_DEBUG_PRINTF("QSPI: Exit 4-byte mode: ret=%d\n", ret);

    if (ret == 0) {
        ret = qspi_wait_ready(dev);
    }
    qspi_write_disable(dev);
    return ret;
}
#endif

#ifdef TEST_EXT_FLASH
#ifndef TEST_EXT_ADDRESS
#define TEST_EXT_ADDRESS 0x2800000 /* 40MB */
#endif
#ifndef TEST_EXT_SIZE
#define TEST_EXT_SIZE (FLASH_PAGE_SIZE * 4)
#endif

static int test_ext_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[TEST_EXT_SIZE];

    wolfBoot_printf("Testing ext flash at 0x%x...\n", TEST_EXT_ADDRESS);

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_EXT_ADDRESS, WOLFBOOT_SECTOR_SIZE);
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
    if (ret < 0) {
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

    /* Preserve PLM's CFG but set IO mode for initial commands (ID read, etc.)
     * PLM: 0xA0080010 = DMA mode | manual start | WP_HOLD | CLK_POL
     * Key: Keep manual start mode (bit 29) and clock settings
     * Note: ext_flash_read() will switch to DMA mode for reads if not in IO mode */
    cfg = (cfg & ~GQSPI_CFG_MODE_EN_MASK);  /* Clear mode bits */
    cfg |= GQSPI_CFG_MODE_EN_IO;            /* Set IO mode for init */
    GQSPI_CFG = cfg;
    dsb();

    /* Set thresholds */
    GQSPI_TX_THRESH = 1;
    GQSPI_RX_THRESH = 1;
    GQSPI_GF_THRESH = 16;

#ifndef GQSPI_MODE_IO
    /* Initialize DMA controller - this was missing compared to zynq.c!
     * Without this, DMA transfers can hang or timeout because the DMA
     * controller is in an undefined state after PLM handoff.
     */
    GQSPIDMA_CTRL = GQSPIDMA_CTRL_DEF;
    GQSPIDMA_CTRL2 = GQSPIDMA_CTRL2_DEF;
    GQSPIDMA_ISR = GQSPIDMA_ISR_ALL_MASK;  /* Clear all pending interrupts */
    GQSPIDMA_IER = GQSPIDMA_ISR_ALL_MASK;  /* Enable all interrupts */
    dsb();
#endif

    /* Configure device for single flash (lower) first */
    qspiDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_LOW;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_LOWER;
    qspiDev.stripe = 0;

    memset(id, 0, sizeof(id));
    (void)qspi_read_id(&qspiDev, id, 3);
    wolfBoot_printf("QSPI: Lower ID: %02x %02x %02x\n", id[0], id[1], id[2]);

#if GQPI_USE_4BYTE_ADDR == 1
    /* Enter 4-byte address mode for lower flash */
    ret = qspi_enter_4byte_addr(&qspiDev);
    if (ret != 0) {
        QSPI_DEBUG_PRINTF("QSPI: 4-byte mode failed (lower)\n");
    }
#endif

#if GQPI_USE_DUAL_PARALLEL == 1
    /* Read ID from upper flash */
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_UP;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_UPPER;

    memset(id, 0, sizeof(id));
    (void)qspi_read_id(&qspiDev, id, 3);
    wolfBoot_printf("QSPI: Upper ID: %02x %02x %02x\n", id[0], id[1], id[2]);

#if GQPI_USE_4BYTE_ADDR == 1
    /* Enter 4-byte address mode for upper flash */
    ret = qspi_enter_4byte_addr(&qspiDev);
    if (ret != 0) {
        QSPI_DEBUG_PRINTF("QSPI: 4-byte mode failed (upper)\n");
    }
#endif

    /* Configure for dual parallel operation */
    qspiDev.mode = GQSPI_GEN_FIFO_MODE_SPI;
    qspiDev.bus = GQSPI_GEN_FIFO_BUS_BOTH;
    qspiDev.cs = GQSPI_GEN_FIFO_CS_BOTH;
    qspiDev.stripe = GQSPI_GEN_FIFO_STRIPE;
#endif

    /* QSPI bare-metal driver info */
    {
    #if GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_QSPI
        const char *mode_str = "Quad";
    #elif GQSPI_QSPI_MODE == GQSPI_GEN_FIFO_MODE_DSPI
        const char *mode_str = "Dual";
    #else
        const char *mode_str = "SPI";
    #endif
    #ifdef GQSPI_MODE_IO
        const char *xfer_str = "Poll";
    #else
        const char *xfer_str = "DMA";
    #endif
        wolfBoot_printf("QSPI: %dMHz, %s, %s\n",
            (GQSPI_CLK_REF / (2 << GQSPI_CLK_DIV)) / 1000000, mode_str, xfer_str);
    }

    qspi_initialized = 1;

#ifdef TEST_EXT_FLASH
    test_ext_flash();
#endif
}

#endif /* EXT_FLASH */

/* ============================================================================
 * HAL Public Interface
 * ============================================================================
 */

void hal_init(void)
{
    uart_init();

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    wolfBoot_printf(
        "\n========================================\n"
        "wolfBoot Secure Boot - AMD Versal\n"
#ifndef WOLFBOOT_REPRODUCIBLE_BUILD
        "Build: " __DATE__ " " __TIME__ "\n"
#endif
        "========================================\n");
    wolfBoot_printf("Current EL: %d\n", current_el());
#endif

#ifdef EXT_FLASH
    qspi_init();
#endif
}

void hal_prepare_boot(void)
{
#if defined(EXT_FLASH) && GQPI_USE_4BYTE_ADDR == 1
    /* Exit 4-byte address mode before handing off to application */
    qspi_exit_4byte_addr(&qspiDev);
#endif

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

    /* Clean and invalidate caches for the loaded application.
     * The application was written to RAM via D-cache, but the CPU will
     * fetch instructions via I-cache from main memory. We must:
     * 1. Clean D-cache (flush dirty data to memory)
     * 2. Invalidate I-cache (ensure fresh instruction fetch)
     */

    /* Clean entire D-cache to Point of Coherency */
    __asm__ volatile("dsb sy");

    /* Clean D-cache for application region */
    {
        uintptr_t addr;
        uintptr_t end = WOLFBOOT_LOAD_ADDRESS + APP_CACHE_FLUSH_SIZE;
        for (addr = WOLFBOOT_LOAD_ADDRESS; addr < end; addr += CACHE_LINE_SIZE) {
            /* DC CVAC - Clean data cache line by VA to PoC */
            __asm__ volatile("dc cvac, %0" : : "r"(addr));
        }
    }

    /* Data synchronization barrier - ensure clean completes */
    __asm__ volatile("dsb sy");

    /* Invalidate instruction cache to ensure fresh code is fetched */
    __asm__ volatile("ic iallu");

    /* Ensure cache invalidation completes before jumping */
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
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

#ifdef __WOLFBOOT
/**
 * Fixup Device Tree before booting Linux
 *
 * This function modifies the DTB to set bootargs for the kernel.
 * Called from do_boot() before jumping to the kernel.
 *
 * @param dts_addr: Pointer to the device tree blob in memory
 * @return: 0 on success, negative error code on failure
 */
int hal_dts_fixup(void* dts_addr)
{
    int off, ret;
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;

    /* Verify FDT header */
    ret = fdt_check_header(dts_addr);
    if (ret != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", ret);
        return ret;
    }

    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* Expand total size to allow adding/modifying properties */
    fdt_set_totalsize(fdt, fdt_totalsize(fdt) + 512);

    /* Find /chosen node */
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off < 0) {
        /* Create /chosen node if it doesn't exist */
        off = fdt_add_subnode(fdt, 0, "chosen");
    }

    if (off >= 0) {
        /* Set bootargs property */
        wolfBoot_printf("FDT: Setting bootargs: %s\n", LINUX_BOOTARGS);
        fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS);
    } else {
        wolfBoot_printf("FDT: Failed to find/create chosen node (%d)\n", off);
        return off;
    }

    return 0;
}
#endif /* __WOLFBOOT */
#endif /* MMU */

#if defined(WOLFBOOT_DUALBOOT) && !defined(WOLFBOOT_NO_PARTITIONS)
/**
 * Get the primary (boot) partition address in flash
 * Returns the flash address where the boot partition starts
 */
void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

/**
 * Get the update partition address in flash
 * Returns the flash address where the update partition starts
 */
void* hal_get_update_address(void)
{
    return (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}
#endif /* WOLFBOOT_DUALBOOT && !WOLFBOOT_NO_PARTITIONS */

/* ============================================================================
 * Flash Functions (STUBS)
 * ============================================================================
 * There is no "internal flash" on the Versal, so these are stubs.
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
    return -1;
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address;
    (void)len;
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

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int ret = 0;
    uint8_t cmd[5];
    uint32_t xferSz, page, pages;
    uintptr_t addr;
    const uint8_t *pageData;

    if (!qspi_initialized) {
        return -1;
    }

    /* Validate flash address bounds */
    if (address >= FLASH_TOTAL_SIZE || (address + len) > FLASH_TOTAL_SIZE) {
        QSPI_DEBUG_PRINTF("ext_flash_write: address 0x%lx+%d exceeds flash size\n",
                          (unsigned long)address, len);
        return -1;
    }

    QSPI_DEBUG_PRINTF("ext_flash_write: addr=0x%lx, len=%d\n",
                      (unsigned long)address, len);

    /* Write by page */
    pages = ((len + (FLASH_PAGE_SIZE - 1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages && ret == 0; page++) {
        ret = qspi_write_enable(&qspiDev);
        if (ret != 0) break;

        xferSz = len;
        if (xferSz > FLASH_PAGE_SIZE)
            xferSz = FLASH_PAGE_SIZE;

        addr = address + (page * FLASH_PAGE_SIZE);
        if (qspiDev.stripe) {
            /* For dual parallel the address is divided by 2 */
            addr /= 2;
        }

        /* Page Program with 4-byte address */
        cmd[0] = FLASH_CMD_PAGE_PROG_4B;
        cmd[1] = (addr >> 24) & 0xFF;
        cmd[2] = (addr >> 16) & 0xFF;
        cmd[3] = (addr >> 8) & 0xFF;
        cmd[4] = addr & 0xFF;

        pageData = data + (page * FLASH_PAGE_SIZE);
        ret = qspi_transfer(&qspiDev, cmd, sizeof(cmd), NULL, 0, 0, pageData, xferSz);

        QSPI_DEBUG_PRINTF("Flash Page %d Write: Ret %d\n", page, ret);
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
    int ret = 0;
    uintptr_t addr = address;

    if (!qspi_initialized) {
        return -1;
    }

    /* Validate flash address bounds */
    if (address >= FLASH_TOTAL_SIZE || (address + len) > FLASH_TOTAL_SIZE) {
        QSPI_DEBUG_PRINTF("ext_flash_read: address 0x%lx+%d exceeds flash size\n",
                          (unsigned long)address, len);
        return -1;
    }

    QSPI_DEBUG_PRINTF("ext_flash_read: addr=0x%lx len=%d\n",
                      (unsigned long)address, len);

    if (qspiDev.stripe) {
        /* For dual parallel the address is divided by 2 */
        addr /= 2;
    }

    /* Use Quad Read command (0x6C) with 4-byte address */
    cmd[0] = FLASH_CMD_QUAD_READ_4B;
    cmd[1] = (addr >> 24) & 0xFF;
    cmd[2] = (addr >> 16) & 0xFF;
    cmd[3] = (addr >> 8) & 0xFF;
    cmd[4] = addr & 0xFF;

    ret = qspi_transfer(&qspiDev, cmd, sizeof(cmd), data, len, GQSPI_DUMMY_READ, NULL, 0);

    /* On error, fill buffer with 0xFF to simulate unwritten flash */
    if (ret != 0) {
        memset(data, 0xFF, len);
    }

    QSPI_DEBUG_PRINTF("ext_flash_read: ret=%d\n", ret);
    return (ret == 0) ? len : ret;
}

int ext_flash_erase(uintptr_t address, int len)
{
    int ret = 0;
    uint8_t cmd[5];
    uintptr_t addr;

    if (!qspi_initialized) {
        return -1;
    }

    /* Validate flash address bounds */
    if (address >= FLASH_TOTAL_SIZE || (address + len) > FLASH_TOTAL_SIZE) {
        QSPI_DEBUG_PRINTF("ext_flash_erase: address 0x%lx+%d exceeds flash size\n",
                          (unsigned long)address, len);
        return -1;
    }

    QSPI_DEBUG_PRINTF("ext_flash_erase: addr=0x%lx, len=%d\n",
                      (unsigned long)address, len);

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
        ret = qspi_transfer(&qspiDev, cmd, sizeof(cmd), NULL, 0, 0, NULL, 0);

        QSPI_DEBUG_PRINTF(" Flash Erase: Ret %d, Address 0x%x\n",
            ret, address);

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


/* ============================================================================
 * SD Card Support (SDHCI)
 * ============================================================================
 * The Versal uses an Arasan SDHCI controller with standard register layout,
 * unlike PolarFire which uses a Cadence SD4HC controller. The generic SDHCI
 * driver (src/sdhci.c) expects Cadence register offsets (HRS at 0x000,
 * SRS at 0x200), so we translate in the HAL register access functions.
 *
 * SD1 at 0xF1050000 is the external SD card slot on VMK180.
 * PLM already initializes the SD controller, so platform init is minimal.
 * Initial implementation uses polling mode (no GIC setup required).
 */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"

/* SD controller base address selection:
 *   SD0 (VERSAL_SD0_BASE = 0xF1040000) - internal, typically eMMC
 *   SD1 (VERSAL_SD1_BASE = 0xF1050000) - external SD card slot on VMK180
 * Note: VMK180 board does not have eMMC hardware, only SD1 is used. */
#define VERSAL_SDHCI_BASE  VERSAL_SD1_BASE

/* ============================================================================
 * Register Translation: Cadence SD4HC -> Standard SDHCI (Arasan)
 * ============================================================================
 * The generic SDHCI driver (src/sdhci.c) uses Cadence SD4HC register offsets:
 *   - HRS registers at 0x000-0x01F (Cadence-specific: reset, PHY, eMMC mode)
 *   - SRS registers at 0x200-0x2FF (standard SDHCI mapped at offset +0x200)
 *
 * Versal uses the Arasan SDHCI controller with standard register layout:
 *   - Standard SDHCI registers at 0x000-0x0FF (no 0x200 offset)
 *
 * Translation:
 *   - SRS offsets (>= 0x200): subtract 0x200 to get standard offset
 *   - HRS00 (0x000): map SWR bit to standard Software Reset All (SRA)
 *   - HRS01, HRS04, HRS06: Cadence-specific, not applicable on Versal
 */
#define CADENCE_SRS_OFFSET      0x200

/* Standard SDHCI Software Reset is in the Clock/Timeout/Reset register */
#define STD_SDHCI_RESET_REG     0x2C  /* Clock Control / Timeout / SW Reset */
#define STD_SDHCI_SRA           (1U << 24) /* Software Reset for All */

/* Handle reads from Cadence HRS registers (0x000-0x1FF) */
static uint32_t versal_sdhci_hrs_read(uint32_t hrs_offset)
{
    volatile uint8_t *base = (volatile uint8_t *)VERSAL_SDHCI_BASE;

    switch (hrs_offset) {
    case 0x000: /* HRS00 - Software Reset */
    {
        /* Map standard SRA (bit 24 of 0x2C) to Cadence SWR (bit 0) */
        uint32_t val = *((volatile uint32_t *)(base + STD_SDHCI_RESET_REG));
        return (val & STD_SDHCI_SRA) ? 1U : 0U;
    }
    case 0x010: /* HRS04 - PHY access (Cadence-specific) */
        /* Return ACK set to prevent wait loops from hanging */
        return (1U << 26); /* SDHCI_HRS04_UIS_ACK */
    default:
        /* HRS01 (debounce), HRS02, HRS06 (eMMC mode) - not applicable */
        return 0;
    }
}

/* Handle writes to Cadence HRS registers (0x000-0x1FF) */
static void versal_sdhci_hrs_write(uint32_t hrs_offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)VERSAL_SDHCI_BASE;

    switch (hrs_offset) {
    case 0x000: /* HRS00 - Software Reset */
        if (val & 1U) { /* SWR bit -> standard SRA */
            uint32_t reg = *((volatile uint32_t *)(base + STD_SDHCI_RESET_REG));
            reg |= STD_SDHCI_SRA;
            *((volatile uint32_t *)(base + STD_SDHCI_RESET_REG)) = reg;
        }
        break;
    default:
        /* HRS01, HRS04, HRS06 - not applicable on Versal, ignore */
        break;
    }
}

/* Register access functions for generic SDHCI driver.
 * Translates Cadence SD4HC register offsets to standard Arasan SDHCI layout. */
uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)VERSAL_SDHCI_BASE;

    /* Cadence SRS registers (0x200+) -> standard SDHCI (subtract 0x200) */
    if (offset >= CADENCE_SRS_OFFSET) {
        return *((volatile uint32_t *)(base + offset - CADENCE_SRS_OFFSET));
    }
    /* Cadence HRS registers (0x000-0x1FF) -> translate to standard equivalents */
    return versal_sdhci_hrs_read(offset);
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)VERSAL_SDHCI_BASE;

    /* Cadence SRS registers (0x200+) -> standard SDHCI (subtract 0x200) */
    if (offset >= CADENCE_SRS_OFFSET) {
        *((volatile uint32_t *)(base + offset - CADENCE_SRS_OFFSET)) = val;
        return;
    }
    /* Cadence HRS registers (0x000-0x1FF) -> translate to standard equivalents */
    versal_sdhci_hrs_write(offset, val);
}

/* Platform initialization - called from sdhci_init()
 * PLM already initializes the SD controller on Versal when booting from SD card,
 * so we don't need to configure clocks/reset (CRL registers are protected at EL2).
 * We verify the SDHCI controller is accessible via standard register reads. */
void sdhci_platform_init(void)
{
#ifdef DEBUG_SDHCI
    volatile uint8_t *base = (volatile uint8_t *)VERSAL_SDHCI_BASE;
    uint32_t val;

    wolfBoot_printf("sdhci_platform_init: SD1 at 0x%x\n",
        (unsigned int)VERSAL_SDHCI_BASE);

    /* Read standard SDHCI registers to verify controller access */
    val = *((volatile uint32_t *)(base + 0x24));  /* Present State */
    wolfBoot_printf("  Present State: 0x%x\n", (unsigned int)val);

    val = *((volatile uint32_t *)(base + 0x40));  /* Capabilities */
    wolfBoot_printf("  Capabilities:  0x%x\n", (unsigned int)val);
    (void)val;
#endif
    /* PLM already configured SD1 - no clock/reset setup needed */
}

/* Platform interrupt setup - called from sdhci_init()
 * Using polling mode for simplicity - no GIC setup needed */
void sdhci_platform_irq_init(void)
{
    /* Polling mode: no interrupt setup required
     * GIC interrupt support can be added later if needed */
#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_irq_init: Using polling mode\n");
#endif
}

/* Platform bus mode selection - called from sdhci_init() */
void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
#ifdef DEBUG_SDHCI
    wolfBoot_printf("sdhci_platform_set_bus_mode: is_emmc=%d\n", is_emmc);
#endif
    /* Nothing additional needed for Versal - mode is set in generic driver */
}
#endif /* DISK_SDCARD || DISK_EMMC */


#endif /* TARGET_versal */
