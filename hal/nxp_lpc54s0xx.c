/* nxp_lpc54s0xx.c
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
 * NXP LPC540xx / LPC54S0xx (SPIFI-boot) HAL for wolfBoot
 *
 * Covers the LPC540xx and LPC54S0xx subseries (LPC54005/54016/54018,
 * LPC54S005/54S016/54S018, and the in-package flash "M" variants
 * LPC54018M / LPC54S018M). None of these parts have internal NOR flash —
 * all code executes from external QSPI flash mapped via SPIFI at
 * address 0x10000000. Flash operations MUST run from RAM since XIP is
 * disabled during erase/write.
 *
 * Verified on the LPC54S018M-EVK (Winbond W25Q32JV, 4 MB). Other family
 * members should work after adjusting the SPIFI device configuration words
 * and sector/partition sizes to match the attached QSPI part.
 *
 * This HAL uses bare-metal register access — no NXP SDK dependencies.
 */

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"

/* -------------------------------------------------------------------------- */
/*  SPIFI controller registers (base 0x40080000)                              */
/* -------------------------------------------------------------------------- */
#define SPIFI_BASE          0x40080000
#define SPIFI_CTRL          (*(volatile uint32_t *)(SPIFI_BASE + 0x00))
#define SPIFI_CMD           (*(volatile uint32_t *)(SPIFI_BASE + 0x04))
#define SPIFI_ADDR          (*(volatile uint32_t *)(SPIFI_BASE + 0x08))
#define SPIFI_IDATA         (*(volatile uint32_t *)(SPIFI_BASE + 0x0C))
#define SPIFI_CLIMIT        (*(volatile uint32_t *)(SPIFI_BASE + 0x10))
#define SPIFI_DATA          (*(volatile uint32_t *)(SPIFI_BASE + 0x14))
#define SPIFI_MCMD          (*(volatile uint32_t *)(SPIFI_BASE + 0x18))
#define SPIFI_STAT          (*(volatile uint32_t *)(SPIFI_BASE + 0x1C))

/* STAT register bits */
#define SPIFI_STAT_MCINIT   (1 << 0)  /* Memory command init done */
#define SPIFI_STAT_CMD      (1 << 1)  /* Command active */
#define SPIFI_STAT_RESET    (1 << 4)  /* Reset in progress */

/* CMD register field positions */
#define SPIFI_CMD_DATALEN(n)    ((n) & 0x3FFF)
#define SPIFI_CMD_POLL          (1 << 14)
#define SPIFI_CMD_DOUT          (1 << 15)       /* 1=output, 0=input */
#define SPIFI_CMD_INTLEN(n)     (((n) & 7) << 16)
#define SPIFI_CMD_FIELDFORM(n)  (((n) & 3) << 19)
#define SPIFI_CMD_FRAMEFORM(n)  (((n) & 7) << 21)
#define SPIFI_CMD_OPCODE(n)     (((n) & 0xFF) << 24)

/* Frame/field format values */
#define FRAMEFORM_OPCODE_ONLY   1
#define FRAMEFORM_OPCODE_3ADDR  4
#define FIELDFORM_ALL_SERIAL    0
#define FIELDFORM_DATA_QUAD     2

/* W25Q32JV flash commands */
#define W25Q_CMD_WRITE_ENABLE   0x06
#define W25Q_CMD_READ_STATUS1   0x05
#define W25Q_CMD_PAGE_PROGRAM   0x02
#define W25Q_CMD_SECTOR_ERASE   0x20  /* 4KB sector erase */
#define W25Q_CMD_FAST_READ_QUAD_IO 0xEB  /* Quad I/O fast read */

/* W25Q status register bits */
#define W25Q_STATUS_BUSY        0x01

/* Flash geometry */
#define FLASH_PAGE_SIZE         0x100  /* 256 bytes */
#define SPIFI_FLASH_BASE        0x10000000

static uint8_t flash_page_cache[FLASH_PAGE_SIZE];

/* Pre-computed SPIFI CMD register values for each flash operation */
#define CMD_WRITE_ENABLE \
    (SPIFI_CMD_DOUT | SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_ONLY) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_WRITE_ENABLE))

#define CMD_READ_STATUS \
    (SPIFI_CMD_DATALEN(1) | SPIFI_CMD_POLL | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_ONLY) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_READ_STATUS1))

#define CMD_SECTOR_ERASE \
    (SPIFI_CMD_DOUT | SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_SECTOR_ERASE))

#define CMD_PAGE_PROGRAM \
    (SPIFI_CMD_DATALEN(FLASH_PAGE_SIZE) | SPIFI_CMD_DOUT | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_PAGE_PROGRAM))

/* Memory-mode command: Quad I/O fast read (0xEB) — must match boot ROM config.
 * Boot ROM MCMD = 0xEB930000:
 *   opcode 0xEB, FRAMEFORM=4 (opcode+3addr), FIELDFORM=2 (addr+data quad),
 *   INTLEN=3 (3 intermediate/dummy bytes in quad mode) */
#define MCMD_READ_QUAD \
    (SPIFI_CMD_INTLEN(3) | SPIFI_CMD_FIELDFORM(FIELDFORM_DATA_QUAD) | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_FAST_READ_QUAD_IO))

#ifdef NVM_FLASH_WRITEONCE
#   error "wolfBoot LPC54S018M HAL: WRITEONCE not supported on SPIFI flash."
#endif

/* -------------------------------------------------------------------------- */
/*  SYSCON registers (shared across clock + UART)                             */
/* -------------------------------------------------------------------------- */
#define SYSCON_BASE              0x40000000
#define SYSCON_PDRUNCFGCLR0      (*(volatile uint32_t *)(SYSCON_BASE + 0x04C))
#define SYSCON_MAINCLKSELA       (*(volatile uint32_t *)(SYSCON_BASE + 0x280))
#define SYSCON_MAINCLKSELB       (*(volatile uint32_t *)(SYSCON_BASE + 0x284))
#define SYSCON_AHBCLKDIV         (*(volatile uint32_t *)(SYSCON_BASE + 0x380))
#define SYSCON_FROCTRL           (*(volatile uint32_t *)(SYSCON_BASE + 0x550))

/* FROCTRL bits */
#define FROCTRL_SEL_96MHZ        (1UL << 14)  /* 0=48MHz, 1=96MHz */
#define FROCTRL_HSPDCLK          (1UL << 30)  /* Enable FRO high-speed output */
#define FROCTRL_WRTRIM           (1UL << 31)  /* Write trim enable */

/* -------------------------------------------------------------------------- */
/*  UART via Flexcomm0 (bare-metal, no SDK)                                   */
/* -------------------------------------------------------------------------- */
#ifdef DEBUG_UART

/* SYSCON registers for clock gating and peripheral reset */
#define SYSCON_AHBCLKCTRL0  (*(volatile uint32_t *)(SYSCON_BASE + 0x200))
#define SYSCON_AHBCLKCTRL1  (*(volatile uint32_t *)(SYSCON_BASE + 0x204))
#define SYSCON_PRESETCTRL1  (*(volatile uint32_t *)(SYSCON_BASE + 0x104))
#define SYSCON_FCLKSEL0     (*(volatile uint32_t *)(SYSCON_BASE + 0x2B0))

#define AHBCLKCTRL0_IOCON   (1UL << 13)
#define AHBCLKCTRL1_FC0     (1UL << 11)
#define PRESETCTRL1_FC0     (1UL << 11)

/* IOCON pin mux registers */
#define IOCON_BASE          0x40001000
#define IOCON_PIO0_29       (*(volatile uint32_t *)(IOCON_BASE + 0x074))
#define IOCON_PIO0_30       (*(volatile uint32_t *)(IOCON_BASE + 0x078))
#define IOCON_FUNC1         1U
#define IOCON_DIGITAL_EN    (1U << 8)

/* Flexcomm0 USART registers */
#define FC0_BASE            0x40086000
#define FC0_CFG             (*(volatile uint32_t *)(FC0_BASE + 0x000))
#define FC0_BRG             (*(volatile uint32_t *)(FC0_BASE + 0x020))
#define FC0_OSR             (*(volatile uint32_t *)(FC0_BASE + 0x028))
#define FC0_FIFOCFG         (*(volatile uint32_t *)(FC0_BASE + 0xE00))
#define FC0_FIFOSTAT        (*(volatile uint32_t *)(FC0_BASE + 0xE04))
#define FC0_FIFOWR          (*(volatile uint32_t *)(FC0_BASE + 0xE20))
#define FC0_PSELID          (*(volatile uint32_t *)(FC0_BASE + 0xFF8))

/* USART CFG bits */
#define USART_CFG_ENABLE    (1U << 0)
#define USART_CFG_DATALEN8  (1U << 2)   /* 8-bit data */

/* FIFO bits */
#define FIFOCFG_ENABLETX    (1U << 0)
#define FIFOCFG_ENABLERX    (1U << 1)
#define FIFOCFG_EMPTYTX     (1U << 16)
#define FIFOCFG_EMPTYRX     (1U << 17)
#define FIFOSTAT_TXEMPTY    (1U << 3)
#define FIFOSTAT_TXNOTFULL  (1U << 4)

/* Baud rate: FRO 12 MHz / (13 * 8) = 115384 (0.16% error from 115200) */
#define UART_OSR_VAL        12   /* oversampling = OSR + 1 = 13 */
#define UART_BRG_VAL        7    /* divisor = BRG + 1 = 8 */

/* Timeout for UART FIFO polling */
#define UART_TX_TIMEOUT     100000

/* SYSCON SET/CLR registers for atomic bit manipulation */
#define SYSCON_PRESETCTRLSET1  (*(volatile uint32_t *)(SYSCON_BASE + 0x124))
#define SYSCON_PRESETCTRLCLR1  (*(volatile uint32_t *)(SYSCON_BASE + 0x144))
#define SYSCON_AHBCLKCTRLSET1  (*(volatile uint32_t *)(SYSCON_BASE + 0x224))

static int uart_ready;

void uart_init(void)
{
    volatile int i;

    uart_ready = 0;

    /* Enable IOCON clock */
    SYSCON_AHBCLKCTRL0 |= AHBCLKCTRL0_IOCON;

    /* Pin mux: P0_29 = FC0_RXD, P0_30 = FC0_TXD (function 1, digital) */
    IOCON_PIO0_29 = IOCON_FUNC1 | IOCON_DIGITAL_EN;
    IOCON_PIO0_30 = IOCON_FUNC1 | IOCON_DIGITAL_EN;

    /* Select FRO 12 MHz as Flexcomm0 clock source */
    SYSCON_FCLKSEL0 = 0;

    /* Enable Flexcomm0 clock (use atomic SET register) */
    SYSCON_AHBCLKCTRLSET1 = AHBCLKCTRL1_FC0;

    /* Reset Flexcomm0: NXP PRESETCTRL polarity is bit=1 means IN reset,
     * bit=0 means OUT of reset. Use SET to assert, CLR to deassert. */
    SYSCON_PRESETCTRLSET1 = PRESETCTRL1_FC0;        /* Assert reset (bit→1) */
    while (!(SYSCON_PRESETCTRL1 & PRESETCTRL1_FC0)) /* Wait for bit=1 */
        ;
    SYSCON_PRESETCTRLCLR1 = PRESETCTRL1_FC0;        /* Deassert reset (bit→0) */
    while (SYSCON_PRESETCTRL1 & PRESETCTRL1_FC0)    /* Wait for bit=0 */
        ;

    /* Small delay after reset deassertion for peripheral to stabilize */
    for (i = 0; i < 100; i++)
        ;

    /* Select USART mode */
    FC0_PSELID = 1;

    /* Verify Flexcomm0 is accessible — if PSELID reads 0, peripheral is
     * not responding. Skip UART. */
    if ((FC0_PSELID & 0x71) == 0) {
        return;
    }

    /* Configure 8N1 (disabled initially) */
    FC0_CFG = USART_CFG_DATALEN8;

    /* Set baud rate */
    FC0_OSR = UART_OSR_VAL;
    FC0_BRG = UART_BRG_VAL;

    /* Enable and flush FIFOs */
    FC0_FIFOCFG = FIFOCFG_ENABLETX | FIFOCFG_ENABLERX |
                  FIFOCFG_EMPTYTX | FIFOCFG_EMPTYRX;

    /* Enable USART */
    FC0_CFG |= USART_CFG_ENABLE;

    uart_ready = 1;
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;
    uint32_t timeout;

    if (!uart_ready)
        return;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            timeout = UART_TX_TIMEOUT;
            while (!(FC0_FIFOSTAT & FIFOSTAT_TXNOTFULL) && --timeout)
                ;
            if (timeout == 0)
                return;
            FC0_FIFOWR = '\r';
        }
        timeout = UART_TX_TIMEOUT;
        while (!(FC0_FIFOSTAT & FIFOSTAT_TXNOTFULL) && --timeout)
            ;
        if (timeout == 0)
            return;
        FC0_FIFOWR = (uint32_t)buf[i];
    }
    /* Wait for transmit to complete */
    timeout = UART_TX_TIMEOUT;
    while (!(FC0_FIFOSTAT & FIFOSTAT_TXEMPTY) && --timeout)
        ;
}

#endif /* DEBUG_UART */

/* -------------------------------------------------------------------------- */
/*  Boot-time initialization (runs from flash / XIP)                          */
/* -------------------------------------------------------------------------- */

#ifdef __WOLFBOOT

/* Assert hook (in case any remaining SDK code uses assert) */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    while (1)
        ;
}

/* Forward declaration — defined later in the file as RAMFUNCTION */
static void RAMFUNCTION spifi_enter_memmode(void);

/*
 * Boost main clock from FRO 12MHz to FRO_HF 96MHz (8x speedup).
 * Must run from RAM because changing MAINCLK affects the SPIFI XIP clock.
 * UART is unaffected: FCLKSEL0=0 selects FRO 12MHz for Flexcomm0 independently.
 */
static void RAMFUNCTION hal_clock_boost(void)
{
    /* Ensure FRO, ROM, and VD6 (OTP) power domains are enabled.
     * Boot ROM usually leaves these on, but clearing is idempotent. */
    SYSCON_PDRUNCFGCLR0 = (1UL << 4) | (1UL << 17) | (1UL << 29);

    /* Confirm main clock is FRO 12MHz (safety before frequency change). */
    SYSCON_MAINCLKSELA = 0U;
    SYSCON_MAINCLKSELB = 0U;

    /* Enable FRO_HF directly via FROCTRL (bypass ROM API which faults
     * on this silicon). Set HSPDCLK + SEL=96MHz with FREQTRIM=0; the FRO
     * will operate at nominal 96MHz with reduced accuracy (no OTP trim),
     * which is fine for crypto acceleration. */
    SYSCON_FROCTRL = FROCTRL_HSPDCLK | FROCTRL_SEL_96MHZ;

    /* Brief delay for FRO_HF to stabilize */
    {
        volatile int i;
        for (i = 0; i < 1000; i++) ;
    }

    /* AHB divider = /1 (96MHz AHB clock). */
    SYSCON_AHBCLKDIV = 0U;

    /* Switch main clock to FRO_HF. SPIFI clock (SPIFICLKSEL=MAIN_CLK,
     * SPIFICLKDIV=/1) auto-scales to 96MHz — within W25Q32JV quad I/O
     * limit of 104MHz. Boot ROM's MCMD already has 6 dummy cycles
     * (INTLEN=3 in quad mode) which covers the full speed range. */
    SYSCON_MAINCLKSELA = 3U;

    /* Re-enter SPIFI memory mode at new clock. */
    spifi_enter_memmode();
}

void hal_init(void)
{
    /* Boost from FRO 12MHz to FRO_HF 96MHz before anything else.
     * Runs from RAM because changing MAINCLK affects SPIFI XIP. */
    hal_clock_boost();

#ifdef DEBUG_UART
    uart_init();
#endif
    wolfBoot_printf("wolfBoot HAL init\n");
}

void hal_prepare_boot(void)
{
}

#endif /* __WOLFBOOT */

/* -------------------------------------------------------------------------- */
/*  SPIFI flash helper functions — all MUST run from RAM                      */
/* -------------------------------------------------------------------------- */

/*
 * Issue a SPIFI command. Exits memory mode if active, waits for ready,
 * then writes CMD register. Entirely register-based — no SDK calls.
 */
static void RAMFUNCTION spifi_set_cmd(uint32_t cmd_val)
{
    /* If in memory mode (MCINIT set), reset to exit.
     * The SPIFI reset clears CTRL and CLIMIT — save and restore
     * the boot ROM's configuration. */
    if (SPIFI_STAT & SPIFI_STAT_MCINIT) {
        uint32_t ctrl = SPIFI_CTRL;
        uint32_t climit = SPIFI_CLIMIT;
        SPIFI_STAT = SPIFI_STAT_RESET;
        while (SPIFI_STAT & SPIFI_STAT_RESET)
            ;
        SPIFI_CTRL = ctrl;
        SPIFI_CLIMIT = climit;
    }

    /* Wait for any active command to complete */
    while (SPIFI_STAT & SPIFI_STAT_CMD)
        ;

    SPIFI_CMD = cmd_val;
}

/*
 * Enter memory-mapped (XIP) mode using quad output fast read.
 */
static void RAMFUNCTION spifi_enter_memmode(void)
{
    uint32_t ctrl = SPIFI_CTRL;
    uint32_t climit = SPIFI_CLIMIT;

    /* Wait for any active command to complete */
    while (SPIFI_STAT & SPIFI_STAT_CMD)
        ;

    /* Reset to clear stale command/POLL state, restore config, enter
     * memory mode. */
    SPIFI_STAT = SPIFI_STAT_RESET;
    while (SPIFI_STAT & SPIFI_STAT_RESET)
        ;
    SPIFI_CTRL = ctrl;
    SPIFI_CLIMIT = climit;

    SPIFI_MCMD = MCMD_READ_QUAD;

    /* Wait for memory mode to initialize */
    while (!(SPIFI_STAT & SPIFI_STAT_MCINIT))
        ;

    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}

static void RAMFUNCTION spifi_write_enable(void)
{
    spifi_set_cmd(CMD_WRITE_ENABLE);
}

static void RAMFUNCTION spifi_wait_busy(void)
{
    /* Use SPIFI POLL mode with properly configured IDATA/CLIMIT.
     *
     * The boot ROM leaves CLIMIT[7:0]=0x00 which makes the POLL comparison
     * always succeed immediately. We must set CLIMIT[7:0] to mask the BUSY
     * bit and IDATA[7:0] to the expected value (0 = not busy).
     *
     * CLIMIT also serves as the cache limit register (upper bits), so we
     * preserve those bits and only modify the lower byte used for POLL mask.
     */
    uint32_t saved_climit = SPIFI_CLIMIT;

    SPIFI_IDATA = 0x00;                        /* expect BUSY=0 */
    SPIFI_CLIMIT = (saved_climit & 0xFFFFFF00) | W25Q_STATUS_BUSY; /* mask bit 0 */

    spifi_set_cmd(CMD_READ_STATUS);             /* POLL mode command */

    /* SPIFI hardware polls flash status internally.
     * CMD bit clears when (status & mask) == (IDATA & mask). */
    while (SPIFI_STAT & SPIFI_STAT_CMD)
        ;

    SPIFI_CLIMIT = saved_climit;                /* restore cache limit */
}

/*
 * Flash write — 256-byte page program via SPIFI
 *
 * Handles unaligned writes by decomposing into page-aligned operations.
 * All flash data goes through a RAM page cache to ensure proper alignment.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int idx = 0;
    uint32_t page_address;
    uint32_t offset;
    int size;
    int i;

    while (idx < len) {
        page_address = ((address + idx) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
        if ((address + idx) > page_address)
            offset = (address + idx) - page_address;
        else
            offset = 0;
        size = FLASH_PAGE_SIZE - offset;
        if (size > (len - idx))
            size = len - idx;
        if (size > 0) {
            /* Read current page content (flash is memory-mapped) */
            memcpy(flash_page_cache, (void *)(uintptr_t)page_address,
                   FLASH_PAGE_SIZE);
            memcpy(flash_page_cache + offset, data + idx, size);

            /* Write enable */
            spifi_write_enable();

            /* Set address and issue page program command */
            SPIFI_ADDR = page_address - SPIFI_FLASH_BASE;
            spifi_set_cmd(CMD_PAGE_PROGRAM);

            /* Write page data as 32-bit words */
            for (i = 0; i < FLASH_PAGE_SIZE; i += 4) {
                uint32_t word;
                memcpy(&word, &flash_page_cache[i], 4);
                SPIFI_DATA = word;
            }

            /* Wait for program to complete */
            spifi_wait_busy();

            /* Re-enter memory mode */
            spifi_enter_memmode();
        }
        idx += size;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/*
 * Flash erase — 4KB sector erase via SPIFI
 *
 * Address must be aligned to WOLFBOOT_SECTOR_SIZE (4KB).
 * Length must be a multiple of WOLFBOOT_SECTOR_SIZE.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + len;

    while (address < end) {
        /* Write enable before each sector erase */
        spifi_write_enable();

        /* Set address and issue sector erase command */
        SPIFI_ADDR = address - SPIFI_FLASH_BASE;
        spifi_set_cmd(CMD_SECTOR_ERASE);

        /* Wait for erase to complete */
        spifi_wait_busy();

        address += WOLFBOOT_SECTOR_SIZE;
    }

    /* Re-enter memory mode */
    spifi_enter_memmode();

    return 0;
}
