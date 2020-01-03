/* hifive1.c
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

/* SiFive HiFive1 HAL for wolfBoot */
/* Supports:
    * QSPI Flash Erase/Write
    * PLL Clock reconfigure
    * UART TX/RX
    * RTC Timer
 */

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#ifndef ARCH_RISCV
#   error "wolfBoot hifive1 HAL: wrong architecture selected. Please compile with ARCH=RISCV."
#endif

/* CLINT Registers (Core Local Interruptor) for time */
#define CLINT_BASE      0x02000000UL
#define CLINT_REG_MTIME (*((volatile uint32_t *)(CLINT_BASE + 0xBFF8)))
#define RTC_FREQ        32768UL

/* QSPI0 Registers */
#define QSPI0_CTRL       0x10014000UL
#define FESPI_REG_SCKDIV (*((volatile uint32_t *)(QSPI0_CTRL + 0x00)))
#define FESPI_REG_CSMODE (*((volatile uint32_t *)(QSPI0_CTRL + 0x18)))
#define FESPI_REG_FMT    (*((volatile uint32_t *)(QSPI0_CTRL + 0x40)))
#define FESPI_REG_TXDATA (*((volatile uint32_t *)(QSPI0_CTRL + 0x48)))
#define FESPI_REG_RXDATA (*((volatile uint32_t *)(QSPI0_CTRL + 0x4c)))
#define FESPI_REG_TXMARK (*((volatile uint32_t *)(QSPI0_CTRL + 0x50)))
#define FESPI_REG_RXMARK (*((volatile uint32_t *)(QSPI0_CTRL + 0x54)))
#define FESPI_REG_FCTRL  (*((volatile uint32_t *)(QSPI0_CTRL + 0x60)))
#define FESPI_REG_FFMT   (*((volatile uint32_t *)(QSPI0_CTRL + 0x64)))
#define FESPI_REG_IP     (*((volatile uint32_t *)(QSPI0_CTRL + 0x74)))

/* QSPI Fields */
#define FESPI_IP_TXWM             0x1
#define FESPI_RXDATA_FIFO_EMPTY   (1 << 31)
#define FESPI_TXDATA_FIFO_FULL    (1 << 31)
#define FESPI_FMT_DIR_TX          (1 << 3)

#define FESPI_CSMODE_AUTO         0x0UL
#define FESPI_CSMODE_HOLD         0x2UL
#define FESPI_CSMODE_MASK         0x3UL

#define FESPI_FCTRL_MODE_SEL      0x1UL

#define FESPI_FFMT_CMD_EN         0x1
#define FESPI_FFMT_ADDR_LEN(x)    (((x) & 0x7) << 1)
#define FESPI_FFMT_PAD_CNT(x)     (((x) & 0xf) << 4)
#define FESPI_FFMT_CMD_PROTO(x)   (((x) & 0x3) << 8)
#define FESPI_FFMT_ADDR_PROTO(x)  (((x) & 0x3) << 10)
#define FESPI_FFMT_DATA_PROTO(x)  (((x) & 0x3) << 12)
#define FESPI_FFMT_CMD_CODE(x)    (((x) & 0xff) << 16)
#define FESPI_FFMT_PAD_CODE(x)    (((x) & 0xff) << 24)

#define FESPI_SCKDIV_MASK         0xFFF

#define FESPI_TXMARK_MASK         0x3

/* FESPI_REG_FMT Fields */
/* SPI I/O direction */
#define FESPI_DIR_RX              0
#define FESPI_DIR_TX              1
/* Frame format */
#define FESPI_PROTO_S             0 /* Single */
#define FESPI_PROTO_D             1 /* Dual */
#define FESPI_PROTO_Q             2 /* Quad */

//#define SPI_QUAD_MODE
/* SPI Flash Commands */
#define FESPI_READ_ID             0xAB /* Read Flash Identification */
#define FESPI_READ_MID            0xAF /* Read Flash Identification, multi-io */
#define FESPI_READ_STATUS         0x05 /* Read Status Register */
#define FESPI_WRITE_ENABLE        0x06 /* Write Enable */
#define FESPI_PAGE_PROGRAM        0x02 /* Page Program */
#define FESPI_ROW_PROGRAM         0x62 /* Row Program */
#define FESPI_FAST_READ           0x0B /* Fast Read */
#define FESPI_READ                0x03 /* Normal Read */
#ifdef SPI_QUAD_MODE
#define FESPI_ERASE_SECTOR        0x20 /* Sector Erase */
#else
#define FESPI_ERASE_SECTOR        0xD7 /* Sector Erase */
#endif

/* SPI flash status fields (from FESPI_READ_STATUS command) */
#define FESPI_RX_BSY              (1 << 0)
#define FESPI_RX_WE               (1 << 1)

/* QSPI Flash Sector Size */
#define FESPI_FLASH_SECTOR_SIZE   (4 * 1024)


/* PRCI Registers */
#define PRCI_BASE          0x10008000UL
#define PRCI_REG_HFROSCCFG (*((volatile uint32_t *)(PRCI_BASE + 0x00)))
#define PRCI_REG_HFXOSCCFG (*((volatile uint32_t *)(PRCI_BASE + 0x04)))
#define PRCI_REG_PLLCFG    (*((volatile uint32_t *)(PRCI_BASE + 0x08)))
#define PRCI_REG_PLLOUTDIV (*((volatile uint32_t *)(PRCI_BASE + 0x0c)))

#define PLLCFG_R           0x00000007UL
#define PLLCFG_F           0x000003F0UL
#define PLLCFG_Q           0x00000C00UL
#define PLLCFG_SEL         0x00010000UL
#define PLLCFG_REFSEL      0x00020000UL
#define PLLCFG_BYPASS      0x00040000UL
#define PLLCFG_LOCK        0x80000000UL
#define PLLCFG_R_SHIFT(r)  ((r << 0) &  PLLCFG_R)
#define PLLCFG_F_SHIFT(f)  ((f << 4) &  PLLCFG_F)
#define PLLCFG_Q_SHIFT(q)  ((q << 10) & PLLCFG_Q)

#define PLLOUTDIV_DIV      0x0000003FUL
#define PLLOUTDIV_DIV_BY_1 0x00000100UL
#define PLLOUTDIV_SHIFT(d) ((d << 0) & PLLOUTDIV_DIV)

#define HFROSCCFG_DIV            0x0000001FUL
#define HFROSCCFG_TRIM           0x001F0000UL
#define HFROSCCFG_EN             (1UL << 30)
#define HFROSCCFG_READY          (1UL << 31)
#define HFROSCCFG_DIV_SHIFT(d)   ((d << 0) & HFROSCCFG_TRIM)
#define HFROSCCFG_TRIM_SHIFT(t)  ((t << 16) & HFROSCCFG_TRIM)

#define HFXOSCCFG_EN             (1 << 30)


/* UART */
#define UART0_BASE              0x10013000UL
#define UART_REG_TXDATA         (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_REG_RXDATA         (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_REG_TXCTRL         (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART_REG_RXCTRL         (*(volatile uint32_t *)(UART0_BASE + 0x0c))
#define UART_REG_IE             (*(volatile uint32_t *)(UART0_BASE + 0x10))
#define UART_REG_IP             (*(volatile uint32_t *)(UART0_BASE + 0x14))
#define UART_REG_DIV            (*(volatile uint32_t *)(UART0_BASE + 0x18))

/* TXDATA Fields */
#define UART_TXEN               (1 <<  0)
#define UART_TXFULL             (1 << 31)

/* RXDATA Fields */
#define UART_RXEN               (1 <<  0)
#define UART_RXEMPTY            (1 << 31)

/* TXCTRL Fields */
#define UART_NSTOP              (1 <<  1)
#define UART_TXCNT(count)       ((0x7 & count) << 16)

/* IP Fields */
#define UART_TXWM               (1 <<  0)


/* Configuration Defaults */

/* Boot (default) Clock settings */
/* Use External PLL, 320MHz CPU and 50MHz flash */
#define PLLREF_FREQ    16000000
#ifndef CPU_FREQ
#define CPU_FREQ       320000000
#endif
#define MAX_CPU_FREQ   320000000
#define MAX_FLASH_FREQ  50000000

/* PLL Configuration */
/* R and Q are fixed values for this PLL code */
#define PLL_R (1)  /* First Divisor: By 2 (takes 16Mhz PLLREF / 2 = 8MHz) */
#define PLL_F(cpuHz) (((cpuHz / PLLREF_FREQ) * 2) - 1) /* Multiplier */
#define PLL_Q (1)  /* Second Divisor: By 2 */

/* SPI Serial clock divisor */
#define FESPI_SCKDIV_DEFAULT   0x03
#define FESPI_SCKDIV_VAL(cpuHz, flashHz) (cpuHz / ((2 * flashHz) - 1))

/* UART baud initialize value */
#ifndef UART_BAUD_INIT
#define UART_BAUD_INIT 115200
#endif


/* TIME for HiFive1 CLIENT RTC timer */
/* sleep ticks function */
void sleep(uint32_t ticks)
{
    uint32_t start = CLINT_REG_MTIME;
    while((CLINT_REG_MTIME - start) < ticks) {
        asm("nop");
    }
}
/* delay milliseconds */
void delay_ms(uint32_t msec)
{
    uint32_t ticks = msec * (RTC_FREQ / 1000);
    sleep(ticks);
}

/* UART functions for HiFive1 UART */
void uart_write(char c)
{
    /* wait for space in TX FIFO */
    while ((UART_REG_TXDATA & UART_TXFULL) != 0);
    UART_REG_TXDATA = (uint32_t)c;
}
char uart_read(void)
{
    uint32_t ch;
    /* wait for RX to have data */
    do {
        ch = UART_REG_RXDATA;
    } while ((ch & UART_RXEMPTY) != 0);
    return (char)(ch & 0xFF);
}

void uart_init(uint32_t cpu_clock, uint32_t baud_rate)
{
    uint32_t div_val = (cpu_clock / baud_rate) - 1;
    UART_REG_DIV = div_val;
    UART_REG_TXCTRL |= UART_TXEN;
    UART_REG_RXCTRL |= UART_RXEN;
}

void uart_flush(void)
{
    uint32_t bits_per_symbol, cycles_to_wait;
    volatile uint32_t x;

    /* Detect when the TXDATA is empty by setting the transmit watermark count
     * to one and waiting until an interrupt is pending */
    UART_REG_TXCTRL &= ~(UART_TXCNT(0x7));
    UART_REG_TXCTRL |= UART_TXCNT(1);
    while((UART_REG_IP & UART_TXWM) == 0);

    /* When the TXDATA clears, the UART is still shifting out the last byte.
     * Calculate the time we must drain to finish transmitting and then wait
     * that long. */
    bits_per_symbol = (UART_REG_TXCTRL & (1 << 1)) ? 9 : 10;
    cycles_to_wait = bits_per_symbol * (UART_REG_DIV + 1);
    for(x = 0; x < cycles_to_wait; x++) {
        asm volatile ("nop");
    }
}

/* QSPI FESPI functions */
void fespi_init(uint32_t cpu_clock, uint32_t flash_freq)
{
    /* Setup desired flash clock divisor */
    FESPI_REG_SCKDIV &= ~FESPI_SCKDIV_MASK;
    FESPI_REG_SCKDIV |= FESPI_SCKDIV_VAL(cpu_clock, flash_freq);
}

static RAMFUNCTION void fespi_swmode(void)
{
    asm volatile("fence");
    asm volatile("fence.i");
    if (FESPI_REG_FCTRL & FESPI_FCTRL_MODE_SEL)
        FESPI_REG_FCTRL &= ~FESPI_FCTRL_MODE_SEL;
}

static RAMFUNCTION void fespi_hwmode(void)
{
    uint32_t x;
    if ((FESPI_REG_FCTRL & FESPI_FCTRL_MODE_SEL) == 0)
        FESPI_REG_FCTRL |= FESPI_FCTRL_MODE_SEL;
    asm volatile("fence");
    asm volatile("fence.i");
    /* Wait two milliseconds for the eSPI device
     * to reboot into hw-mapped mode and link to the 
     * instruction cache
     */
    for(x = 0; x < CPU_FREQ / 500; x++) {
        asm volatile ("nop");
    }
}

static RAMFUNCTION void fespi_csmode_hold(void)
{
    uint32_t reg = FESPI_REG_CSMODE & ~FESPI_CSMODE_MASK;
    FESPI_REG_CSMODE = reg | FESPI_CSMODE_HOLD;
}

static RAMFUNCTION void fespi_csmode_auto(void)
{
    uint32_t reg = FESPI_REG_CSMODE & ~FESPI_CSMODE_MASK;
    FESPI_REG_CSMODE = reg | FESPI_CSMODE_AUTO;
}

static RAMFUNCTION void fespi_wait_txwm(void)
{
    while((FESPI_REG_IP & FESPI_IP_TXWM) == 0)
        ;
}

static RAMFUNCTION void fespi_sw_tx(uint8_t b)
{
    while((FESPI_REG_TXDATA & FESPI_TXDATA_FIFO_FULL) != 0)
        ;
    FESPI_REG_TXDATA = b;
}

static RAMFUNCTION uint8_t fespi_sw_rx(void)
{
    volatile uint32_t reg;
    do {
        reg = FESPI_REG_RXDATA;
    } while ((reg & FESPI_RXDATA_FIFO_EMPTY) != 0);
    return (uint8_t)(reg & 0xFF);
}

static RAMFUNCTION void fespi_sw_setdir(int tx)
{
    if (tx)
        FESPI_REG_FMT |= FESPI_FMT_DIR_TX;
    else
        FESPI_REG_FMT &= ~FESPI_FMT_DIR_TX;
}

static RAMFUNCTION void fespi_write_address(uint32_t address)
{
    fespi_sw_tx((address & 0xFF0000) >> 16);
    fespi_sw_tx((address & 0xFF00) >> 8);
    fespi_sw_tx((address & 0xFF));
    fespi_wait_txwm();
}

static RAMFUNCTION void fespi_wait_write_disabled(void)
{
    uint8_t rx;
    fespi_sw_setdir(FESPI_DIR_RX);
    fespi_csmode_hold();
    fespi_sw_tx(FESPI_READ_STATUS);
    rx = fespi_sw_rx();
    while (1) {
        fespi_sw_tx(0);
        rx = fespi_sw_rx();
        if ((rx & FESPI_RX_WE) == 0) {
            break;
        }
    }
    fespi_csmode_auto();
    fespi_sw_setdir(FESPI_DIR_TX);
}

static RAMFUNCTION void fespi_write_enable(void)
{
    uint8_t rx;
    int i;
    while(1) {
        fespi_sw_tx(FESPI_WRITE_ENABLE);
        fespi_wait_txwm();
        fespi_sw_setdir(FESPI_DIR_RX);
        fespi_csmode_hold();
        fespi_sw_tx(FESPI_READ_STATUS);
        rx = fespi_sw_rx();
        for (i = 0; i < 3; i++) {
            fespi_sw_tx(0);
            rx = fespi_sw_rx();
            if ((rx & FESPI_RX_WE) == FESPI_RX_WE) {
                fespi_csmode_auto();
                fespi_sw_setdir(FESPI_DIR_TX);
                return;
            }
        }
        fespi_csmode_auto();
        fespi_sw_setdir(FESPI_DIR_TX);
    }
}

static RAMFUNCTION void fespi_wait_flash_busy(void)
{
    uint8_t rx;
    fespi_sw_setdir(FESPI_DIR_RX);
    fespi_csmode_hold();
    fespi_sw_tx(FESPI_READ_STATUS);
    while (1) {
        fespi_sw_tx(0);
        rx = fespi_sw_rx();
        if ((rx & FESPI_RX_BSY) == 0) {
            break;
        }
    }
    fespi_csmode_auto();
    fespi_sw_setdir(FESPI_DIR_TX);
}

static uint32_t fespi_flash_probe(void);

void hifive1_init(uint32_t cpu_clock, uint32_t uart_baud)
{
    uint32_t config_val = 0;

    /* don't exceed maximum frequency for chip */
    if (cpu_clock > MAX_CPU_FREQ)
        cpu_clock = MAX_CPU_FREQ;

    /* Flush UART */
    uart_flush();

    /* Enforce initial default value for QSPI flash clock divisor */
    FESPI_REG_SCKDIV = FESPI_SCKDIV_DEFAULT;

    /* Make sure internal high frequency oscillator is enabled */
    PRCI_REG_HFROSCCFG = (HFROSCCFG_EN |
        HFROSCCFG_DIV_SHIFT(0x4) |
        HFROSCCFG_TRIM_SHIFT(0x10));
    /* Wait for ready */
    while((PRCI_REG_HFROSCCFG & HFROSCCFG_READY) == 0);

    /* If PLL on then switch off before configuring it */
    if (PRCI_REG_PLLCFG & PLLCFG_SEL)
        PRCI_REG_PLLCFG &= ~PLLCFG_SEL;

    /* Enable external reference */
    PRCI_REG_PLLCFG |= PLLCFG_REFSEL;

    /* Set R */
    PRCI_REG_PLLCFG &= ~PLLCFG_R;
    PRCI_REG_PLLCFG |= PLLCFG_R_SHIFT(PLL_R);
    /* Calculate and Set F */
    config_val = PLL_F(cpu_clock);
    PRCI_REG_PLLCFG &= ~PLLCFG_F;
    PRCI_REG_PLLCFG |= PLLCFG_F_SHIFT(config_val);
    /* Set Q */
    PRCI_REG_PLLCFG &= ~PLLCFG_Q;
    PRCI_REG_PLLCFG |= PLLCFG_Q_SHIFT(PLL_Q);

    /* Disable final divider */
    PRCI_REG_PLLOUTDIV |= PLLOUTDIV_DIV_BY_1;
    PRCI_REG_PLLOUTDIV &= ~PLLOUTDIV_DIV;
    PRCI_REG_PLLOUTDIV |= PLLOUTDIV_SHIFT(1);

    /* Disable bypass */
    PRCI_REG_PLLCFG &= ~PLLCFG_BYPASS;

    /* Wait for PLL to lock */
    while ((PRCI_REG_PLLCFG & PLLCFG_LOCK) == 0);

    /* Enable the PLL */
    PRCI_REG_PLLCFG |= PLLCFG_SEL;

    /* Reconfigure the SPI to maximum frequency */
    fespi_init(cpu_clock, MAX_FLASH_FREQ);
    
    /* Reconfigure the UART */
    uart_init(cpu_clock, uart_baud);
}



/* public HAL functions */
void hal_init(void)
{
    hifive1_init(CPU_FREQ, UART_BAUD_INIT);
}
void hal_prepare_boot(void)
{
}

#define FLASH_PAGE_SIZE 256
#define FLASH_BASE 0x20000000UL

/* Flash functions must be relocated to RAM for execution */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t i, j = 0;
    uint32_t off, page; 
    const uint8_t *src;
    uint8_t data_copy[FLASH_PAGE_SIZE];
    int swmode = 0;


    if (address >= FLASH_BASE)
        address -= FLASH_BASE;
    off = address & 0xFF;
    page = address >> 8;

    while (j < (uint32_t)len) {
        if ((off > 0) || (len < FLASH_PAGE_SIZE)) {
            uint8_t *orig = (uint8_t *)(FLASH_BASE + (page << 8));
            int rel_len;
            rel_len = FLASH_PAGE_SIZE - off;
            if (swmode) {
                fespi_hwmode();
                swmode = 0;
            }
            if (rel_len > len)
                rel_len = len;
            for (i = 0; i < off; i++)
                data_copy[i] = orig[i];
            for (i = off; i < off + rel_len; i++)
                data_copy[i] = data[j++];
            for (i = off + rel_len; i < FLASH_PAGE_SIZE; i++)
                data_copy[i] = orig[i];
            src = data_copy;
        } else {
            src = (data + j);
            j += FLASH_PAGE_SIZE;
        }
        if (!swmode) {
            FESPI_REG_TXMARK = 1;
            fespi_swmode();
            fespi_wait_flash_busy();
            swmode++;
        }
        fespi_write_enable();
        fespi_csmode_hold();
        fespi_sw_tx(FESPI_PAGE_PROGRAM);
        fespi_wait_txwm();
        fespi_write_address((page << 8));
        for(i = 0; i < FLASH_PAGE_SIZE; i++) {
            fespi_sw_tx(src[i]);
        }
        fespi_csmode_auto();
        page++;
        off = 0;
    }
    fespi_hwmode();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

static uint32_t RAMFUNCTION fespi_flash_probe(void)
{
    uint32_t rx;

    FESPI_REG_TXMARK = 1;
    fespi_sw_setdir(FESPI_DIR_RX);
    fespi_swmode();

    fespi_wait_txwm();
    fespi_wait_flash_busy();
    fespi_sw_setdir(FESPI_DIR_RX);
    fespi_csmode_hold();
    fespi_sw_tx(FESPI_READ_ID);
    fespi_sw_tx(0);
    fespi_sw_tx(0);
    fespi_sw_tx(0);
    rx = fespi_sw_rx();
    rx |= fespi_sw_rx() << 8;
    rx |= fespi_sw_rx() << 16;
    fespi_csmode_auto();
    fespi_sw_setdir(FESPI_DIR_TX);
    return rx;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end;
    uint32_t p;
    if (address >= FLASH_BASE)
        address -= FLASH_BASE;
    end = address + len - 1;

    
    FESPI_REG_TXMARK = 1;
    fespi_wait_txwm();
    fespi_swmode();
    fespi_wait_flash_busy();


    for (p = address; p <= end; p += FESPI_FLASH_SECTOR_SIZE) {
        fespi_write_enable();
        fespi_csmode_hold();
        fespi_sw_tx(FESPI_ERASE_SECTOR);
        fespi_write_address(p);
        fespi_wait_txwm();
        fespi_csmode_auto();
        fespi_wait_flash_busy();
    }
    fespi_hwmode();
    return 0;
}
