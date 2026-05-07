/* zynq7000.c
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

#ifdef TARGET_zynq7000

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"
#include "hal/zynq7000.h"

#ifndef ARCH_ARM
#   error "wolfBoot zynq7000 HAL: wrong architecture selected. Please compile with ARCH=ARM."
#endif

#ifdef DEBUG_UART
void uart_init(void)
{
    /* Disable interrupts */
    Z7_UART_IDR = Z7_UART_ISR_MASK;
    /* Disable TX/RX */
    Z7_UART_CR  = (Z7_UART_CR_TX_DIS | Z7_UART_CR_RX_DIS);
    /* Clear ISR */
    Z7_UART_ISR = Z7_UART_ISR_MASK;

    /* 8N1 */
    Z7_UART_MR  = Z7_UART_MR_8N1;

    /* Half-FIFO trigger levels (XUartPs FIFO depth = 64) */
    Z7_UART_RXWM = 32;
    Z7_UART_TXWM = 32;

    /* RX timeout disabled */
    Z7_UART_RXTOUT = 0;

    /* baud = ref_clk / (BR_GEN * (BR_DIV + 1)) */
    Z7_UART_BR_GEN = UART_CLK_REF / (DEBUG_UART_BAUD * (DEBUG_UART_DIV + 1));
    Z7_UART_BR_DIV = DEBUG_UART_DIV;

    /* Reset TX/RX paths */
    Z7_UART_CR = (Z7_UART_CR_TXRST | Z7_UART_CR_RXRST);
    /* Enable TX/RX */
    Z7_UART_CR = (Z7_UART_CR_TX_EN  | Z7_UART_CR_RX_EN);
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while (Z7_UART_SR & Z7_UART_SR_TXFULL)
                ;
            Z7_UART_FIFO = (uint32_t)'\r';
        }
        while (Z7_UART_SR & Z7_UART_SR_TXFULL)
            ;
        Z7_UART_FIFO = (uint32_t)c;
    }
    while (!(Z7_UART_SR & Z7_UART_SR_TXEMPTY))
        ;
}
#endif /* DEBUG_UART */

#ifdef EXT_FLASH
/* ===================== QSPI flash driver (XQspiPs) =====================
 * Bare-metal driver for the Zynq-7000 "Linear/Static" QSPI controller.
 * Used here in I/O mode (single-bit SPI) for read/write/erase. The Linear
 * QSPI XIP window at 0xFC000000 is not used by this driver - all reads go
 * through the controller's I/O FIFO so the same code path works whether or
 * not FSBL pre-configured linear mode.
 *
 * The driver assumes FSBL has already configured QSPI ref clock and MIO
 * pins (typical when wolfBoot is loaded by FSBL). qspi_init() resets and
 * reconfigures the controller itself. */

/* JEDEC SPI-NOR command codes (subset used by wolfBoot) */
#define SPI_CMD_RDID            0x9F
#define SPI_CMD_RDSR            0x05
#define SPI_CMD_WREN            0x06
#define SPI_CMD_WRDI            0x04
#define SPI_CMD_READ            0x03
#define SPI_CMD_FAST_READ       0x0B  /* requires 8 dummy clocks */
#define SPI_CMD_PAGE_PROGRAM    0x02
#define SPI_CMD_SECTOR_ERASE    0xD8  /* 64 KB erase */
#define SPI_STATUS_WIP          0x01  /* write-in-progress */
#define SPI_STATUS_WEL          0x02  /* write-enable latch */

#define SPI_NOR_PAGE_SIZE       256U
#define SPI_NOR_SECTOR_SIZE     0x10000U  /* 64 KB */

static void qspi_drain_rxfifo(void)
{
    while (Z7_QSPI_ISR & Z7_QSPI_ISR_RXNEMPTY)
        (void)Z7_QSPI_RXD;
}

static void qspi_cs_assert(void)
{
    /* PCS [13:10] = 0b1110 -> CS0 asserted. */
    Z7_QSPI_CR = (Z7_QSPI_CR & ~Z7_QSPI_CR_PCS_MASK) | Z7_QSPI_CR_PCS_CS0;
}

static void qspi_cs_release(void)
{
    /* PCS [13:10] = 0b1111 -> all CS deasserted. */
    Z7_QSPI_CR |= Z7_QSPI_CR_PCS_NONE;
}

/* Transfer up to 4 bytes. Uses TXD(n) for partial sends WITHOUT RX (so the
 * flash sees exactly n clock cycles of MOSI), and TXD0 (4-byte) when RX is
 * needed (so the controller pushes a full 4-byte RX FIFO entry we can
 * decode). Mirrors u-boot zynq_qspi.c (offsets[3] when rx_buf, offsets[len-1]
 * otherwise). */
static void qspi_xfer4(const uint8_t *tx, uint8_t *rx, unsigned int nbytes)
{
    uint32_t txw = 0xFFFFFFFFU;
    uint32_t rxw;
    unsigned int i;

    if (nbytes > 4)
        nbytes = 4;
    if (nbytes == 0)
        return;

    if (tx != NULL) {
        for (i = 0; i < nbytes; i++) {
            txw &= ~((uint32_t)0xFFU << (i * 8));
            txw |= ((uint32_t)tx[i]) << (i * 8);
        }
    }

    qspi_drain_rxfifo();

    if (rx != NULL || nbytes == 4) {
        /* Receive path or full 4-byte send: use TXD0. */
        Z7_QSPI_TXD0 = txw;
    } else {
        /* Send-only short transfer: pick TXD1/TXD2/TXD3 to clock exactly
         * nbytes out the wire (no padding). The TX byte(s) are at the LSB. */
        switch (nbytes) {
            case 1: Z7_QSPI_TXD1 = txw; break;
            case 2: Z7_QSPI_TXD2 = txw; break;
            case 3: Z7_QSPI_TXD3 = txw; break;
            default: Z7_QSPI_TXD0 = txw; break;
        }
    }

    while (!(Z7_QSPI_ISR & Z7_QSPI_ISR_RXNEMPTY))
        ;
    rxw = Z7_QSPI_RXD;

#ifdef DEBUG_QSPI_BYTE
    {
        const char hex[] = "0123456789abcdef";
        char line[48];
        unsigned int p = 0;
        line[p++] = '[';
        for (i = 0; i < nbytes; i++) {
            uint8_t b = (uint8_t)(txw >> (i * 8));
            line[p++] = hex[(b >> 4) & 0xF];
            line[p++] = hex[(b >> 0) & 0xF];
        }
        line[p++] = ' ';
        line[p++] = '/';
        line[p++] = ' ';
        for (i = 0; i < nbytes; i++) {
            uint8_t b = (uint8_t)(rxw >> (i * 8));
            line[p++] = hex[(b >> 4) & 0xF];
            line[p++] = hex[(b >> 0) & 0xF];
        }
        line[p++] = ']';
        line[p++] = '\n';
        uart_write(line, p);
    }
#endif

    if (rx != NULL) {
        for (i = 0; i < nbytes; i++)
            rx[i] = (uint8_t)(rxw >> (i * 8));
    }
}

static int qspi_xfer(const uint8_t *tx, uint8_t *rx, unsigned int len)
{
    unsigned int off = 0;
    unsigned int chunk;

    qspi_cs_assert();
    while (off < len) {
        chunk = len - off;
        if (chunk > 4)
            chunk = 4;
        qspi_xfer4((tx != NULL) ? &tx[off] : NULL,
                   (rx != NULL) ? &rx[off] : NULL,
                   chunk);
        off += chunk;
    }
    qspi_cs_release();
    return 0;
}

/* I/O mode: used for short cmd-only ops (JEDEC, RDSR, WREN, sector erase,
 * page program initiation). Reads use Linear/XIP mode separately. */
static void qspi_io_mode_setup(void)
{
    Z7_QSPI_EN  = 0;
    Z7_QSPI_IDR = Z7_QSPI_ISR_MASK;
    qspi_drain_rxfifo();
    Z7_QSPI_ISR = Z7_QSPI_ISR_MASK;
    Z7_QSPI_LQSPI_CR = 0;          /* leave linear mode */
    Z7_QSPI_TXTHR = 1;
    Z7_QSPI_RXTHR = 1;
    Z7_QSPI_CR  = Z7_QSPI_CR_IFMODE
                | Z7_QSPI_CR_HOLD_B
                | Z7_QSPI_CR_SSFORCE
                | Z7_QSPI_CR_PCS_NONE
                | Z7_QSPI_CR_FIFO_WIDTH
                | Z7_QSPI_CR_BAUD_DIV_8
                | Z7_QSPI_CR_MSTREN;
    Z7_QSPI_EN  = Z7_QSPI_EN_VAL;
}

/* Linear (XIP) mode: hardware-managed reads. Controller asserts CS, sends
 * cmd+addr+dummy, returns data via memory-mapped accesses at 0xFC000000+.
 * Matches XQspiPs_LinearInit() in qspips_v3_14/src/xqspips_hw.c. */
static void qspi_linear_mode_setup(void)
{
    Z7_QSPI_EN  = 0;
    Z7_QSPI_IDR = Z7_QSPI_ISR_MASK;
    qspi_drain_rxfifo();
    Z7_QSPI_ISR = Z7_QSPI_ISR_MASK;

    /* CR: IFMODE=1, FIFO=32-bit, MSTREN=1, SSFORCE=1, HOLD_B=1, /4 baud,
     * MANSTRTEN=0 (auto-start), CPHA/CPOL=0, PCS=CS0 asserted explicitly
     * (the linear-mode controller still drives PCS even with SSFORCE; we
     * set the field deterministically rather than relying on the reset
     * default). */
    Z7_QSPI_CR  = Z7_QSPI_CR_IFMODE
                | Z7_QSPI_CR_HOLD_B
                | Z7_QSPI_CR_SSFORCE
                | Z7_QSPI_CR_PCS_CS0
                | Z7_QSPI_CR_FIFO_WIDTH
                | Z7_QSPI_CR_BAUD_DIV_4
                | Z7_QSPI_CR_MSTREN;
    /* Single-bit FAST_READ (0x0B) with 1 dummy byte. Avoids needing QE
     * bit set in the flash status register. */
    Z7_QSPI_LQSPI_CR = 0x8000010BU;
    Z7_QSPI_EN  = Z7_QSPI_EN_VAL;
}

static void qspi_init(void)
{
    qspi_io_mode_setup();
}

static int spi_flash_read_id(uint8_t out[3])
{
    uint8_t cmd[4] = { SPI_CMD_RDID, 0, 0, 0 };
    uint8_t rx[4]  = { 0, 0, 0, 0 };
    int rc = qspi_xfer(cmd, rx, sizeof(cmd));
    if (rc == 0) {
        out[0] = rx[1];
        out[1] = rx[2];
        out[2] = rx[3];
    }
    return rc;
}

static int spi_flash_status(uint8_t *status)
{
    uint8_t cmd[2] = { SPI_CMD_RDSR, 0 };
    uint8_t rx[2]  = { 0, 0 };
    int rc = qspi_xfer(cmd, rx, sizeof(cmd));
    if (rc == 0)
        *status = rx[1];
    return rc;
}

static int spi_flash_wait_ready(void)
{
    uint8_t status = 0xFF;
    /* Spin until WIP clears. No timeout: a stuck flash is a board issue. */
    do {
        if (spi_flash_status(&status) != 0)
            return -1;
    } while ((status & SPI_STATUS_WIP) != 0);
    return 0;
}

static int spi_flash_write_enable(void)
{
    uint8_t cmd = SPI_CMD_WREN;
    int rc;
    rc = qspi_xfer(&cmd, NULL, 1);
    if (rc != 0)
        return rc;
    /* Optional: confirm WEL bit set */
    {
        uint8_t status = 0;
        if (spi_flash_status(&status) != 0)
            return -1;
        if ((status & SPI_STATUS_WEL) == 0)
            return -1;
    }
    return 0;
}

static int spi_flash_sector_erase(uint32_t address)
{
    uint8_t cmd[4];
    int rc;

    rc = spi_flash_write_enable();
    if (rc != 0)
        return rc;

    cmd[0] = SPI_CMD_SECTOR_ERASE;
    cmd[1] = (uint8_t)((address >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((address >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((address >>  0) & 0xFFU);
    rc = qspi_xfer(cmd, NULL, sizeof(cmd));
    if (rc != 0)
        return rc;

    return spi_flash_wait_ready();
}

static int spi_flash_page_program(uint32_t address,
                                  const uint8_t *data,
                                  unsigned int len)
{
    /* len must be <= SPI_NOR_PAGE_SIZE and not cross a page boundary */
    uint8_t hdr[4];
    int rc;

    if (len == 0 || len > SPI_NOR_PAGE_SIZE)
        return -1;

    rc = spi_flash_write_enable();
    if (rc != 0)
        return rc;

    hdr[0] = SPI_CMD_PAGE_PROGRAM;
    hdr[1] = (uint8_t)((address >> 16) & 0xFFU);
    hdr[2] = (uint8_t)((address >>  8) & 0xFFU);
    hdr[3] = (uint8_t)((address >>  0) & 0xFFU);

    qspi_cs_assert();
    qspi_xfer4(hdr, NULL, 4);
    {
        unsigned int off = 0;
        while (off < len) {
            unsigned int chunk = len - off;
            if (chunk > 4)
                chunk = 4;
            qspi_xfer4(&data[off], NULL, chunk);
            off += chunk;
        }
    }
    qspi_cs_release();

    return spi_flash_wait_ready();
}

/* Reads use Linear/XIP mode: switch the controller to linear mode, do a
 * memcpy from the XIP window at 0xFC000000+offset, then return the
 * controller to I/O mode. Mirrors how the ZynqMP HAL splits CMD17 (single
 * block PIO) vs CMD18 (multi-block DMA) on SDHCI - here, short cmd ops use
 * I/O mode and bulk reads use linear/XIP. */
static int spi_flash_read(uint32_t address, uint8_t *data, unsigned int len)
{
    /* Issue 32-bit AXI reads to the XIP window. Single-byte loads are
     * unreliable - the linear-mode controller is burst-aware and wants
     * word transfers. Decompose each 32-bit word into bytes at the
     * destination so unaligned addr / unaligned dst / unaligned len all
     * work without faulting on devices with strict alignment. */
    const volatile uint32_t *xipw;
    uint32_t aligned_addr;
    uint32_t w;
    unsigned int byte_off;
    unsigned int i;

    if (len == 0)
        return 0;

    qspi_linear_mode_setup();

    aligned_addr = address & ~3U;
    byte_off = address & 3U;
    xipw = (const volatile uint32_t *)(Z7_QSPI_LINEAR_BASE + aligned_addr);
    /* Sacrificial first 32-bit read primes the controller pipeline. */
    (void)xipw[0];

    i = 0;
    if (byte_off != 0) {
        w = *xipw++;
        for (; byte_off < 4U && i < len; byte_off++, i++)
            data[i] = (uint8_t)(w >> (byte_off * 8U));
    }
    while (i + 4U <= len) {
        w = *xipw++;
        data[i++] = (uint8_t)(w >>  0);
        data[i++] = (uint8_t)(w >>  8);
        data[i++] = (uint8_t)(w >> 16);
        data[i++] = (uint8_t)(w >> 24);
    }
    if (i < len) {
        w = *xipw;
        for (byte_off = 0; i < len; byte_off++, i++)
            data[i] = (uint8_t)(w >> (byte_off * 8U));
    }

    qspi_io_mode_setup();
    return 0;
}
#if defined(TEST_EXT_FLASH) || defined(TEST_QSPI)
/* QSPI self-test (enable with -DTEST_EXT_FLASH or -DTEST_QSPI):
 *   1) Read JEDEC ID and print it
 *   2) Erase a 64 KB sector at TEST_EXT_FLASH_ADDR (default 2 MB offset)
 *   3) Page-program a 256 B pattern (i & 0xFF)
 *   4) Read back and verify
 * Mirrors the existing src/spi_flash.c test_ext_flash() logic. Output via
 * UART. Wired to fire from qspi_init() / hal_init() below. */
#ifndef TEST_EXT_FLASH_ADDR
#define TEST_EXT_FLASH_ADDR (2U * 1024U * 1024U)
#endif

static void qspi_print_hex_byte(uint8_t b)
{
    static const char hex[] = "0123456789abcdef";
    char buf[2];
    buf[0] = hex[(b >> 4) & 0xFU];
    buf[1] = hex[(b >> 0) & 0xFU];
    uart_write(buf, 2);
}

static void qspi_print_hex32(uint32_t v)
{
    qspi_print_hex_byte((uint8_t)(v >> 24));
    qspi_print_hex_byte((uint8_t)(v >> 16));
    qspi_print_hex_byte((uint8_t)(v >>  8));
    qspi_print_hex_byte((uint8_t)(v >>  0));
}

static void qspi_selftest(void)
{
    uint8_t  patbuf[SPI_NOR_PAGE_SIZE];
    uint8_t  rdback[SPI_NOR_PAGE_SIZE];
    uint8_t  id[3] = { 0, 0, 0 };
    unsigned int i;
    int rc;

    for (i = 0; i < SPI_NOR_PAGE_SIZE; i++)
        patbuf[i] = (uint8_t)(i & 0xFFU);

    uart_write("qspi: --- TEST_EXT_FLASH start ---\n", 35);

    /* 1) JEDEC ID */
    rc = spi_flash_read_id(id);
    uart_write("qspi: JEDEC ID = 0x", 19);
    qspi_print_hex_byte(id[0]);
    qspi_print_hex_byte(id[1]);
    qspi_print_hex_byte(id[2]);
    uart_write("  rc=", 5);
    qspi_print_hex_byte((uint8_t)rc);
    uart_write("\n", 1);
    if (id[0] == 0x00 || id[0] == 0xFF) {
        uart_write("qspi: JEDEC read returned blank - driver broken\n", 48);
        return;
    }

    /* 1b) Sanity read of known-programmed area at 0x100000 (signed image
     * staged via Vitis program_flash). Should start with 'WOLF' magic. */
    {
        uint8_t boot[8] = { 0 };
        spi_flash_read(0x00100000U, boot, sizeof(boot));
        uart_write("qspi: read @0x100000 = ", 23);
        for (i = 0; i < 8; i++) qspi_print_hex_byte(boot[i]);
        uart_write("\n", 1);
    }

    /* 2) Erase */
    uart_write("qspi: erase sector @ 0x", 23);
    qspi_print_hex32(TEST_EXT_FLASH_ADDR);
    uart_write(" ...\n", 5);
    rc = spi_flash_sector_erase(TEST_EXT_FLASH_ADDR);
    if (rc != 0) {
        uart_write("qspi: erase FAILED\n", 19);
        return;
    }

    /* 3) Page program */
    uart_write("qspi: page program ...\n", 23);
    rc = spi_flash_page_program(TEST_EXT_FLASH_ADDR, patbuf, SPI_NOR_PAGE_SIZE);
    if (rc != 0) {
        uart_write("qspi: program FAILED\n", 21);
        return;
    }

    /* 4a) Re-read JEDEC ID after program to confirm controller is alive */
    {
        uint8_t id2[3] = { 0, 0, 0 };
        spi_flash_read_id(id2);
        uart_write("qspi: post-program JEDEC = 0x", 29);
        qspi_print_hex_byte(id2[0]);
        qspi_print_hex_byte(id2[1]);
        qspi_print_hex_byte(id2[2]);
        uart_write("\n", 1);
    }

    /* 4b) Read back at TEST_EXT_FLASH_ADDR + compare */
    for (i = 0; i < SPI_NOR_PAGE_SIZE; i++)
        rdback[i] = 0;
    spi_flash_read(TEST_EXT_FLASH_ADDR, rdback, SPI_NOR_PAGE_SIZE);
    uart_write("qspi: rdback[0..7] = ", 21);
    for (i = 0; i < 8; i++) qspi_print_hex_byte(rdback[i]);
    uart_write("\n", 1);

    /* 4c) Linear-mode XIP sanity check: read 32-bit words then decode bytes.
     * Single-byte AXI accesses to the linear window confuse the controller -
     * the burst-aware controller wants 32-bit reads. */
    uart_write("qspi: xip32@0x200000 = ", 23);
    {
        volatile uint32_t *xipw;
        unsigned int j;
        uint32_t w;
        Z7_QSPI_EN = 0;
        Z7_QSPI_LQSPI_CR = 0x80000003U; /* LQ_MODE=1, INST=0x03 (READ), no dummy */
        Z7_QSPI_EN = Z7_QSPI_EN_VAL;
        xipw = (volatile uint32_t*)(Z7_QSPI_LINEAR_BASE + TEST_EXT_FLASH_ADDR);
        for (j = 0; j < 2; j++) {
            w = xipw[j];
            qspi_print_hex_byte((uint8_t)(w >>  0));
            qspi_print_hex_byte((uint8_t)(w >>  8));
            qspi_print_hex_byte((uint8_t)(w >> 16));
            qspi_print_hex_byte((uint8_t)(w >> 24));
        }
        uart_write("  xip32@0x100000 = ", 19);
        xipw = (volatile uint32_t*)(Z7_QSPI_LINEAR_BASE + 0x100000U);
        for (j = 0; j < 2; j++) {
            w = xipw[j];
            qspi_print_hex_byte((uint8_t)(w >>  0));
            qspi_print_hex_byte((uint8_t)(w >>  8));
            qspi_print_hex_byte((uint8_t)(w >> 16));
            qspi_print_hex_byte((uint8_t)(w >> 24));
        }
        uart_write("\n", 1);
        /* Restore I/O mode for any later transfers. */
        Z7_QSPI_EN = 0;
        Z7_QSPI_LQSPI_CR = 0;
        Z7_QSPI_EN = Z7_QSPI_EN_VAL;
    }
    for (i = 0; i < SPI_NOR_PAGE_SIZE; i++) {
        if (rdback[i] != patbuf[i]) {
            uart_write("qspi: MISMATCH @ idx 0x", 23);
            qspi_print_hex_byte((uint8_t)(i >> 8));
            qspi_print_hex_byte((uint8_t)(i & 0xFFU));
            uart_write("  got 0x", 8);
            qspi_print_hex_byte(rdback[i]);
            uart_write(" expected 0x", 12);
            qspi_print_hex_byte(patbuf[i]);
            uart_write("\n", 1);
            return;
        }
    }
    uart_write("qspi: --- TEST_EXT_FLASH PASS ---\n", 34);
}
#endif /* TEST_EXT_FLASH || TEST_QSPI */

#endif /* EXT_FLASH (qspi block) */

void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    {
        const char banner[] = "wolfBoot Zynq-7000 (ZC702) hal_init\n";
        uart_write(banner, sizeof(banner) - 1);
    }
    /* One-line boot config summary so the UART log identifies which
     * config / storage / payload / clock plan the running wolfBoot was
     * built for. Useful when juggling QSPI vs SD images. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    wolfBoot_printf("  storage : SDCard (Arasan SDHCI v2.0, %d MHz post-init)\n",
        SDHCI_CLK_50MHZ / 1000);
#elif defined(EXT_FLASH)
    wolfBoot_printf("  storage : QSPI (XQspiPs N25Q128, IO_PLL/40 ~12.5 MHz)\n");
#else
    wolfBoot_printf("  storage : NONE (JTAG-loaded)\n");
#endif
#if defined(WOLFBOOT_LINUX_PAYLOAD)
    wolfBoot_printf("  payload : ARM Linux ABI (r0=0, r1=~0, r2=DTB)\n");
#else
    wolfBoot_printf("  payload : bare-metal (r0=DTB, r1=r2=r3=0)\n");
#endif
    wolfBoot_printf("  partns  : load=0x%x boot=0x%x update=0x%x\n",
        (unsigned int)WOLFBOOT_LOAD_ADDRESS,
        (unsigned int)WOLFBOOT_PARTITION_BOOT_ADDRESS,
        (unsigned int)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    wolfBoot_printf("  cputmr  : Cortex-A9 GTimer @ %u Hz\n",
        (unsigned int)Z7_GTIMER_FREQ_HZ);
#endif /* DEBUG_UART */
#ifdef EXT_FLASH
    qspi_init();
#if defined(TEST_EXT_FLASH) || defined(TEST_QSPI)
    qspi_selftest();
#endif
#ifdef DEBUG_BOOTPART
    /* Dump first 16 bytes of the BOOT partition so we can see if the
     * QSPI driver is returning the signed-image header (magic 'WOLF'). */
    {
        uint8_t buf[16];
        const char hex[] = "0123456789abcdef";
        char line[3*16 + 2];
        unsigned int i;
        spi_flash_read(0x00100000U, buf, sizeof(buf));
        for (i = 0; i < sizeof(buf); i++) {
            line[i*3 + 0] = hex[(buf[i] >> 4) & 0xF];
            line[i*3 + 1] = hex[(buf[i] >> 0) & 0xF];
            line[i*3 + 2] = ' ';
        }
        line[sizeof(line) - 2] = '\n';
        line[sizeof(line) - 1] = 0;
        uart_write("QSPI[0x100000]: ", 16);
        uart_write(line, sizeof(line) - 1);
    }
#endif
#endif
}

/* Cortex-A9 cache teardown sequence used before do_boot(). FSBL hands off
 * with MMU+L1+L2 enabled; we clean and disable them so the next stage sees
 * a deterministic CPU state. Order follows ARM ARM B2.2.5: clean D-cache,
 * disable MMU, invalidate I-cache, ISB. */
static inline void z7_dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void z7_isb(void) { __asm__ volatile("isb sy" ::: "memory"); }

static void z7_l1_dcache_clean_invalidate_all(void)
{
    /* v7-A clean+invalidate by set/way - iterates the data cache levels in
     * CLIDR and walks each (set, way) issuing DCCISW. Adapted from ARMv7-A
     * Architecture Reference Manual B2.2.4 example. */
    __asm__ volatile (
        "dmb     sy                   \n"
        "mrc     p15, 1, r0, c0, c0, 1 \n"  /* CLIDR */
        "ands    r3, r0, #0x07000000   \n"
        "mov     r3, r3, lsr #23       \n"
        "beq     5f                    \n"
        "mov     r10, #0               \n"
        "1:                            \n"
        "add     r2, r10, r10, lsr #1  \n"
        "mov     r1, r0, lsr r2        \n"
        "and     r1, r1, #7            \n"
        "cmp     r1, #2                \n"
        "blt     4f                    \n"
        "mcr     p15, 2, r10, c0, c0, 0\n"  /* CSSELR */
        "isb                           \n"
        "mrc     p15, 1, r1, c0, c0, 0 \n"  /* CCSIDR */
        "and     r2, r1, #7            \n"
        "add     r2, r2, #4            \n"  /* line size */
        "ldr     r4, =0x3FF            \n"
        "ands    r4, r4, r1, lsr #3    \n"  /* assoc */
        "clz     r5, r4                \n"
        "ldr     r7, =0x7FFF           \n"
        "ands    r7, r7, r1, lsr #13   \n"  /* num sets */
        "2:                            \n"
        "mov     r9, r4                \n"
        "3:                            \n"
        "orr     r11, r10, r9, lsl r5  \n"
        "orr     r11, r11, r7, lsl r2  \n"
        "mcr     p15, 0, r11, c7, c14, 2 \n" /* DCCISW */
        "subs    r9, r9, #1            \n"
        "bge     3b                    \n"
        "subs    r7, r7, #1            \n"
        "bge     2b                    \n"
        "4:                            \n"
        "add     r10, r10, #2          \n"
        "cmp     r3, r10               \n"
        "bgt     1b                    \n"
        "5:                            \n"
        "dsb     sy                    \n"
        "isb                           \n"
        :
        :
        : "r0","r1","r2","r3","r4","r5","r7","r9","r10","r11","memory","cc"
    );
}

static void z7_l1_icache_invalidate_all(void)
{
    /* ICIALLU + branch predictor invalidate */
    __asm__ volatile (
        "mov     r0, #0                \n"
        "mcr     p15, 0, r0, c7, c5, 0 \n"   /* ICIALLU */
        "mcr     p15, 0, r0, c7, c5, 6 \n"   /* BPIALL  */
        "dsb     sy                    \n"
        "isb                           \n"
        : : : "r0","memory"
    );
}

static void z7_disable_mmu_and_caches(void)
{
    /* SCTLR: clear M (bit0), C (bit2), I (bit12). Leaves Z (branch predict)
     * alone since we cleared BPIALL above. */
    __asm__ volatile (
        "mrc     p15, 0, r0, c1, c0, 0 \n"
        "bic     r0, r0, #(1 << 0)     \n"
        "bic     r0, r0, #(1 << 2)     \n"
        "bic     r0, r0, #(1 << 12)    \n"
        "mcr     p15, 0, r0, c1, c0, 0 \n"
        "dsb     sy                    \n"
        "isb                           \n"
        : : : "r0","memory"
    );
}

void hal_prepare_boot(void)
{
    /* Disable IRQ + FIQ */
    __asm__ volatile("cpsid if" ::: "memory");
    z7_dsb();
    z7_l1_dcache_clean_invalidate_all();
    z7_disable_mmu_and_caches();
    z7_l1_icache_invalidate_all();
    z7_isb();
    /* PL310 L2: leave alone for first cut. FSBL on ZC702 typically does
     * not enable PL310 unless explicitly configured; if your FSBL does,
     * extend this routine with L2x0 clean-invalidate + disable. */
}

/* Internal flash operations are no-ops on Zynq-7000:
 * QSPI is treated as external flash via ext_flash_*. */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void) { }
void RAMFUNCTION hal_flash_lock(void)   { }

#ifdef EXT_FLASH
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    if (len <= 0)
        return 0;
    if (spi_flash_read((uint32_t)address, data, (unsigned int)len) != 0)
        return -1;
    return len;   /* wolfBoot's update_ram.c expects bytes-read on success */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* Split writes on SPI-NOR page boundaries (256 B). */
    uint32_t addr = (uint32_t)address;
    unsigned int remain = (unsigned int)((len > 0) ? len : 0);
    unsigned int off = 0;

    while (remain > 0) {
        unsigned int page_off = addr & (SPI_NOR_PAGE_SIZE - 1U);
        unsigned int chunk = SPI_NOR_PAGE_SIZE - page_off;
        if (chunk > remain)
            chunk = remain;
        if (spi_flash_page_program(addr, data + off, chunk) != 0)
            return -1;
        addr   += chunk;
        off    += chunk;
        remain -= chunk;
    }
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* Erase whole sectors covering [address, address+len). The caller is
     * expected to align to WOLFBOOT_SECTOR_SIZE (= SPI_NOR_SECTOR_SIZE). */
    uint32_t addr = (uint32_t)address;
    int remain = len;
    while (remain > 0) {
        if (spi_flash_sector_erase(addr) != 0)
            return -1;
        addr   += SPI_NOR_SECTOR_SIZE;
        remain -= (int)SPI_NOR_SECTOR_SIZE;
    }
    return 0;
}

void ext_flash_lock(void)   { }
void ext_flash_unlock(void) { }
#endif /* EXT_FLASH */

#ifdef MMU
/* Memory-mapped DTB fallback. Not used on Zynq-7000 with EXT_FLASH=1: the
 * signed DTB is opened as PART_DTS_BOOT in update_ram.c and read out of
 * QSPI via ext_flash_check_read. Return NULL so the fallback path is a
 * no-op when the DTS partition is missing. */
void *hal_get_dts_address(void)
{
    return NULL;
}

void *hal_get_dts_update_address(void)
{
    return NULL;
}
#endif /* MMU */

/* Microsecond timer using the Cortex-A9 Global Timer at PERIPHBASE+0x200.
 * 64-bit free-running counter, increments at PERIPHCLK = CPU_3x2x =
 * 333.33 MHz on ZC702 with the default FSBL clock plan (ARM_PLL =
 * 1.333 GHz, CPU_6x4x = 666.67 MHz). The actual divide constant lives
 * in Z7_GTIMER_FREQ_HZ in hal/zynq7000.h - override there if you reclock
 * the CPU. The Global Timer is started by the FSBL; if it isn't running
 * yet we kick it here. */
uint64_t hal_get_timer_us(void)
{
    uint32_t hi1, lo, hi2;
    uint64_t count;

    /* If the Global Timer hasn't been enabled yet, enable it. The Control
     * Register's bit 0 is the Timer Enable; one-time per power cycle. */
    if ((Z7_GTIMER_CTRL & Z7_GTIMER_CTRL_EN) == 0) {
        Z7_GTIMER_CTRL = Z7_GTIMER_CTRL_EN;
    }

    /* Read low/high atomically: read high, low, high again - if high changed,
     * a wrap happened mid-read and we must retry. */
    do {
        hi1 = Z7_GTIMER_HI;
        lo  = Z7_GTIMER_LO;
        hi2 = Z7_GTIMER_HI;
    } while (hi1 != hi2);

    count = ((uint64_t)hi2 << 32) | (uint64_t)lo;
    /* Convert ticks to microseconds. PERIPHCLK is fixed (FSBL clock plan),
     * so the divide is by a known constant - no 64x64 division needed. */
    return (count * 1000000ULL) / (uint64_t)Z7_GTIMER_FREQ_HZ;
}

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* ============================================================================
 * SDHCI (SD Card / eMMC) Platform Support
 * ============================================================================
 * The Zynq-7000 SDIO controller is an Arasan SDHCI v2.0, same IP family as
 * the v3.0 used on ZynqMP - register layout matches the SD Host Controller
 * Standard 1.00, mapped at 0xE0100000 (SD0) / 0xE0101000 (SD1).
 *
 * The generic SDHCI driver (src/sdhci.c) targets the Cadence SD4HC layout
 * which adds a HRS register block at 0x000-0x01F and shifts the standard
 * SRS registers to 0x200+. We translate between the two here, mirroring
 * the ZynqMP HAL block in hal/zynq.c.
 *
 * Differences from ZynqMP's translation:
 *   - Base addr: SD0 at 0xE0100000 (ZC702 SD card slot)
 *   - Clock/reset is via SLCR.SDIO_{CLK,RST} (Z7) instead of CRL_APB (ZynqMP)
 *   - No HV4E mode (32-bit ARM, only legacy SDMA via SRS00)
 *   - DMA cache ops use ARMv7 CP15 (mcr p15,0,Rt,c7,c10,1 etc.) instead of
 *     AArch64 dc cvac. Caches are typically off in our hal_prepare_boot
 *     path, but if a future config keeps them on, the generic-SDMA
 *     coherency path needs the ops.
 */
#include "sdhci.h"

#ifndef Z7_SDHCI_BASE
#define Z7_SDHCI_BASE   Z7_SDIO0_BASE
#endif

#define CADENCE_SRS_OFFSET      0x200

/* Standard SDHCI register offsets (byte addresses within the controller)
 * matching the Arasan v2.0 register map. */
#define STD_SDHCI_SDMA_ADDR     0x00  /* SDMA System Address (32-bit) */
#define STD_SDHCI_HOST_CTRL1    0x28  /* Host Control 1 (8-bit) */
#define STD_SDHCI_POWER_CTRL    0x29  /* Power Control (8-bit) */
#define STD_SDHCI_BLKGAP_CTRL   0x2A  /* Block Gap Control (8-bit) */
#define STD_SDHCI_WAKEUP_CTRL   0x2B  /* Wakeup Control (8-bit) */
#define STD_SDHCI_CLK_CTRL      0x2C  /* Clock Control (16-bit) */
#define STD_SDHCI_TIMEOUT_CTRL  0x2E  /* Timeout Control (8-bit) */
#define STD_SDHCI_SW_RESET      0x2F  /* Software Reset (8-bit) */
#define STD_SDHCI_HOST_CTRL2    0x3C  /* Auto CMD Err(16) + Host Ctrl 2(16) */

/* Software Reset register bits (at offset 0x2F, 8-bit register) */
#define STD_SDHCI_SRA           0x01  /* Software Reset for All */

/* Cadence HRS faux-read translation (the driver pokes HRS00/HRS04 during
 * init even though there's no HRS block on Arasan; emulate just enough). */
static uint32_t z7_sdhci_hrs_read(uint32_t hrs_offset)
{
    volatile uint8_t *base = (volatile uint8_t *)Z7_SDHCI_BASE;
    switch (hrs_offset) {
    case 0x000: { /* HRS00 - Software Reset (mirror std SRA) */
        uint8_t val = *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET));
        return (val & STD_SDHCI_SRA) ? 1U : 0U;
    }
    case 0x010: /* HRS04 - PHY access ACK (Cadence-specific). Return ACK so
                 * the driver's wait-for-ack loops complete. */
        return (1U << 26);
    default:
        return 0;
    }
}

static void z7_sdhci_hrs_write(uint32_t hrs_offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)Z7_SDHCI_BASE;
    if (hrs_offset == 0x000 && (val & 1U)) {
        /* Software Reset for All - byte write to std SRA. */
        *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) = STD_SDHCI_SRA;
    }
    /* HRS01 (debounce), HRS02, HRS06 (eMMC mode) etc. - not applicable on
     * Arasan; ignore. */
    (void)val;
}

uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)Z7_SDHCI_BASE;

    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;

        /* SRS22 (0x58) -> SRS00: Legacy SDMA address register */
        if (std_off == 0x58) {
            return *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR));
        }
        /* SRS23 (0x5C) -> 0: no 64-bit SDMA on Arasan v2.0 */
        if (std_off == 0x5C) {
            return 0;
        }
        return *((volatile uint32_t *)(base + std_off));
    }
    return z7_sdhci_hrs_read(offset);
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)Z7_SDHCI_BASE;

    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;

        /* Cadence SRS10 = std 0x28..0x2B (HostCtrl1/PowerCtrl/BlkGap/Wakeup).
         * Mask out HSE (High Speed Enable, bit 2) before writing HostCtrl1
         * - Arasan v2.0 + 3.3V SDHC cards stay in single-edge default-speed
         * timing (max 25 MHz); the driver tries to push HSE+50MHz which the
         * card can't follow, causing DTOE on data transfers. */
        if (std_off == 0x28) {
            uint8_t host_ctrl1 = (uint8_t)(val & 0xFF);
            host_ctrl1 &= (uint8_t)~0x04U; /* clear HSE */
            *((volatile uint8_t *)(base + STD_SDHCI_HOST_CTRL1)) = host_ctrl1;
            *((volatile uint8_t *)(base + STD_SDHCI_POWER_CTRL)) =
                (uint8_t)((val >> 8) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_BLKGAP_CTRL)) =
                (uint8_t)((val >> 16) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_WAKEUP_CTRL)) =
                (uint8_t)((val >> 24) & 0xFF);
            return;
        }
        /* Cadence SRS11 = std 0x2C..0x2F (ClkCtrl/TimeoutCtrl/SwReset) */
        if (std_off == 0x2C) {
            *((volatile uint16_t *)(base + STD_SDHCI_CLK_CTRL)) =
                (uint16_t)(val & 0xFFFF);
            *((volatile uint8_t *)(base + STD_SDHCI_TIMEOUT_CTRL)) =
                (uint8_t)((val >> 16) & 0xFF);
            *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) =
                (uint8_t)((val >> 24) & 0xFF);
            return;
        }
        /* SRS22 (0x58) -> SRS00: Legacy SDMA address register */
        if (std_off == 0x58) {
            *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR)) = val;
            return;
        }
        if (std_off == 0x5C) {
            return; /* no 64-bit SDMA */
        }
        /* SRS15 (0x3C): mask out v3-only bits the driver tries to set.
         * Arasan SDHCI v2.0 (Zynq-7000) is 3.3V-only with no UHS-I
         * support; writing UHS Mode Select / 1.8V Enable / Sampling Clock
         * Select / HV4E / A64 either reserved-faults or puts the
         * controller into an invalid signaling mode that breaks all
         * subsequent data transfers (DTOE on the first PIO read). */
        if (std_off == STD_SDHCI_HOST_CTRL2) {
            val &= ~((7U << 0)   /* UMS[2:0] (UHS Mode Select) */
                   | (1U << 3)   /* 1.8V Signaling Enable */
                   | (7U << 4)   /* Driver Strength Select */
                   | (1U << 7)   /* Sampling Clock Select */
                   | (1U << 12)  /* HV4E (Host Version 4 Enable) */
                   | (1U << 13)); /* A64 (64-bit Addressing) */
        }
        *((volatile uint32_t *)(base + std_off)) = val;
        return;
    }
    z7_sdhci_hrs_write(offset, val);
}

/* SDHCI controller bring-up. ZC702 boots from SD with FSBL having already
 * configured SDIO_CLK / pinmux / power, so this is mostly a no-op. For
 * JTAG-loaded development we kick the SLCR clock + reset just in case. */
void sdhci_platform_init(void)
{
    volatile int i;

    /* Unlock SLCR. */
    Z7_SLCR_UNLOCK = Z7_SLCR_UNLOCK_KEY;

    /* APER clock for SDIO0 (AHB bus clock to the controller). */
    Z7_SLCR_APER_CLK |= Z7_SLCR_APER_SDIO0;

    /* SDIO_CLK_CTRL: enable CLKACT0, IO_PLL source, divisor for ~50 MHz
     * ref. IO_PLL=1 GHz default, /20 = 50 MHz. Skip if FSBL already set
     * something sensible. */
    if ((Z7_SLCR_SDIO_CLK & Z7_SLCR_SDIO_CLK_ACT0) == 0) {
        uint32_t v = Z7_SLCR_SDIO_CLK;
        v &= ~Z7_SLCR_SDIO_CLK_DIV_MSK;
        v |= (20U << Z7_SLCR_SDIO_CLK_DIV_SH);
        v |= Z7_SLCR_SDIO_CLK_ACT0;
        Z7_SLCR_SDIO_CLK = v;
    }

    /* Pulse SDIO0 reset (REF + CPU) so the controller picks up clock and
     * caps cleanly. */
    Z7_SLCR_SDIO_RST |= (Z7_SLCR_SDIO_RST_REF0 | Z7_SLCR_SDIO_RST_CPU0);
    for (i = 0; i < 256; i++) { /* short delay */ }
    Z7_SLCR_SDIO_RST &= ~(Z7_SLCR_SDIO_RST_REF0 | Z7_SLCR_SDIO_RST_CPU0);
    for (i = 0; i < 1024; i++) { /* wait for controller ready */ }

    Z7_SLCR_LOCK = Z7_SLCR_LOCK_KEY;
}

void sdhci_platform_irq_init(void)
{
    /* Polling-mode driver: nothing to do. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
    /* SD vs eMMC bus mode: nothing extra needed for SD - the generic driver
     * sets up SD bus width / clock via the standard SDHCI registers, which
     * our reg_read/write translation handles transparently. */
}

/* DMA cache maintenance for SDMA. wolfBoot's hal_prepare_boot disables D-cache
 * before do_boot, but SDHCI runs BEFORE that during the load+verify phase
 * with caches potentially live. ARMv7 cache ops (clean / clean+invalidate
 * by MVA) keep DMA buffers coherent. */
void sdhci_platform_dma_prepare(void *buf, uint32_t sz, int is_write)
{
    uintptr_t start = (uintptr_t)buf & ~31U;     /* L1 D-cache line = 32B */
    uintptr_t end   = ((uintptr_t)buf + sz + 31U) & ~31U;
    uintptr_t addr;

    if (is_write) {
        /* DCCMVAC - clean by MVA: dirty CPU lines flushed so DMA reads them */
        for (addr = start; addr < end; addr += 32U) {
            __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(addr) : "memory");
        }
    } else {
        /* DCCIMVAC - clean+invalidate by MVA: discard stale CPU lines so
         * DMA write data shows up on subsequent CPU reads */
        for (addr = start; addr < end; addr += 32U) {
            __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(addr) : "memory");
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

void sdhci_platform_dma_complete(void *buf, uint32_t sz, int is_write)
{
    if (!is_write) {
        /* Post-DMA-read: invalidate-only (DCIMVAC, c7,c6,1). The DMA
         * controller has just written fresh data into DRAM; we must
         * discard any stale CPU lines (including ones the prefetcher
         * may have pulled in between dma_prepare and dma_complete) so
         * subsequent CPU reads see the new data. clean+invalidate
         * (DCCIMVAC) would write back the stale lines first, partially
         * overwriting the controller's data. */
        uintptr_t start = (uintptr_t)buf & ~31U;
        uintptr_t end   = ((uintptr_t)buf + sz + 31U) & ~31U;
        uintptr_t addr;
        for (addr = start; addr < end; addr += 32U) {
            __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" : : "r"(addr) : "memory");
        }
        __asm__ volatile("dsb sy" : : : "memory");
    }
}
#endif /* DISK_SDCARD || DISK_EMMC */

#endif /* TARGET_zynq7000 */
