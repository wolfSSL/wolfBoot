/* nxp_s32k1xx.c
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
 * HAL for NXP S32K1xx (S32K142, S32K144, S32K146, S32K148)
 * Tested on S32K142: Cortex-M4F, 256KB Flash, 32KB SRAM
 */

#include <stdint.h>
#include <string.h>
#include "image.h"
#include "hal.h"

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")

#include "s32k1xx.h"

/* ============== Flash Configuration Field (FCF) ============== */
/* Located at 0x400-0x40F in flash */

#ifdef __WOLFBOOT
#define FCF_LEN (16)
const uint8_t __attribute__((section(".flash_config"))) flash_config[FCF_LEN] = {
    /* Backdoor comparison key (8 bytes) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* Program Flash Protection (4 bytes) - all unprotected */
    0xFF, 0xFF, 0xFF, 0xFF,
    /* Flash Security Byte */
    0xFE,  /* SEC=10 (unsecured), FSLACC=11, MEEN=11, KEYEN=11 */
    /* Flash Option Byte */
    0xFF,
    /* EEPROM Protection Byte */
    0xFF,
    /* Data Flash Protection Byte */
    0xFF
};
#endif

/* ============== Watchdog Functions ============== */

/* Disable watchdog - must be called within 128 bus clock cycles after reset
 * or after unlocking. The watchdog is enabled by default after reset.
 */
static void watchdog_disable(void)
{
    /* Unlock watchdog by writing unlock key to CNT register */
    WDOG_CNT = WDOG_CNT_UNLOCK;

    /* Wait for unlock to complete (ULK bit set) */
    while (!(WDOG_CS & WDOG_CS_ULK)) {}

    /* Set timeout to max and disable watchdog */
    WDOG_TOVAL = WDOG_TOVAL_DEFAULT;
    WDOG_CS = WDOG_CS_DISABLE_CFG;

    /* Wait for reconfiguration to complete (RCS bit set) */
    while (!(WDOG_CS & WDOG_CS_RCS)) {}
}

#ifdef WATCHDOG
/* Enable watchdog with specified timeout value
 * timeout_ms: timeout in milliseconds (max ~512ms with LPO clock)
 * LPO clock is 128kHz, so each tick is ~7.8125us
 * For longer timeouts, use PRES bit for 256x prescaler
 */
static void watchdog_enable(uint32_t timeout_ms)
{
    uint32_t toval;
    uint32_t cs_cfg = WDOG_CS_ENABLE_CFG;

    /* Calculate TOVAL from timeout_ms
     * LPO = 128kHz = 128 ticks/ms
     * With PRES=0: max timeout = 65535/128 = 512ms
     * With PRES=1: max timeout = 65535*256/128 = 131 seconds
     */
    if (timeout_ms > 512) {
        /* Use prescaler for longer timeouts */
        cs_cfg |= WDOG_CS_PRES;
        toval = (timeout_ms * 128) / 256;
    } else {
        toval = timeout_ms * 128;
    }

    /* Clamp to max value */
    if (toval > 0xFFFF) {
        toval = 0xFFFF;
    }

    /* Unlock watchdog */
    WDOG_CNT = WDOG_CNT_UNLOCK;
    while (!(WDOG_CS & WDOG_CS_ULK)) {}

    /* Configure and enable */
    WDOG_TOVAL = toval;
    WDOG_CS = cs_cfg;

    /* Wait for reconfiguration to complete */
    while (!(WDOG_CS & WDOG_CS_RCS)) {}
}

/* Refresh (kick) the watchdog to prevent reset
 * Must be called periodically before timeout expires
 */
static void watchdog_refresh(void)
{
    /* For CMD32EN mode, write refresh key as 32-bit value */
    WDOG_CNT = WDOG_CNT_REFRESH;
}
#endif /* WATCHDOG */

/* ============== Clock Configuration ============== */

static void clock_init_firc(void)
{
    /* Enable FIRC (48 MHz) */
    SCG_FIRCDIV = (1UL << 8) | (1UL << 0);  /* FIRCDIV1=/1, FIRCDIV2=/1 */
    SCG_FIRCCFG = 0;  /* Range 0: 48 MHz */
    SCG_FIRCCSR = SCG_FIRCCSR_FIRCEN;

    /* Wait for FIRC valid */
    while (!(SCG_FIRCCSR & SCG_FIRCCSR_FIRCVLD)) {}
}

static void clock_init_spll(void)
{
    /* Disable SPLL before configuration */
    SCG_SPLLCSR &= ~SCG_SPLLCSR_SPLLEN;

    /* Configure SPLL:
     * Using FIRC (48 MHz) as source
     * For 112 MHz: PREDIV=0 (/1), MULT=12 (x28)
     *   VCO = 48 MHz * 28 = 1344 MHz (wait, that's too high)
     *
     * Actually S32K uses SOSC as SPLL source typically.
     * With FIRC we need different approach.
     *
     * For HSRUN at 112 MHz from FIRC:
     *   Use FIRC directly for core at 48 MHz in RUN mode
     *   Then switch to SPLL for HSRUN
     *
     * SPLL: VCO range is 180-320 MHz
     *   SPLL_CLK = VCO / 2
     *
     * If using 8 MHz SOSC: PREDIV=0, MULT=28 -> VCO=224 MHz, SPLL_CLK=112 MHz
     *
     * For this bare-metal impl, we'll use FIRC at 48 MHz as a safe default,
     * and configure SPLL with SOSC if available.
     *
     * For simplicity: Use FIRC 48MHz with appropriate dividers.
     * HSRUN mode allows higher frequencies but requires SPLL.
     */

    /* SPLL dividers */
    SCG_SPLLDIV = (2UL << 8) | (4UL << 0);  /* SPLLDIV1=/2, SPLLDIV2=/4 */

    /* SPLL configuration: MULT and PREDIV
     * PREDIV: 0-7 -> divide by 1-8
     * MULT: 0-31 -> multiply by 16-47
     * VCO = (FIRC/PREDIV) * MULT, must be 180-320 MHz
     * SPLL_CLK = VCO / 2
     *
     * For 112 MHz with 48 MHz FIRC:
     *   Need VCO = 224 MHz
     *   48 / 1 * 28 = 1344 MHz (too high, FIRC can't be SPLL source directly)
     *
     * S32K1xx SPLL source is SOSC only. For bare-metal without crystal,
     * we run from FIRC at 48 MHz maximum.
     *
     * If SOSC 8 MHz is available:
     *   8 / 1 * 28 = 224 MHz VCO, 112 MHz SPLL_CLK
     */

    /* For now, skip SPLL and use FIRC directly */
    /* TODO: Add SOSC + SPLL support for true 112 MHz operation */
}

static void clock_init(void)
{
    /* Initialize FIRC to 48 MHz */
    clock_init_firc();

    /* Configure Run mode clock control:
     * SCS = FIRC (3)
     * DIVCORE = /1 (48 MHz)
     * DIVBUS = /1 (48 MHz)
     * DIVSLOW = /2 (24 MHz for flash)
     */
    SCG_RCCR = SCG_xCCR_SCS_FIRC |
               (0UL << SCG_xCCR_DIVCORE_SHIFT) |
               (0UL << SCG_xCCR_DIVBUS_SHIFT) |
               (1UL << SCG_xCCR_DIVSLOW_SHIFT);

    /* Wait for clock switch */
    while ((SCG_CSR & SCG_CSR_SCS_MASK) != SCG_CSR_SCS_FIRC) {}

#ifdef S32K1XX_CLOCK_HSRUN
    /* HSRUN mode (112 MHz) - requires SOSC + SPLL (not fully implemented yet)
     * TODO: Add SOSC initialization and SPLL configuration for true 112 MHz
     * Currently this enters HSRUN mode but still uses FIRC at 48 MHz
     */

    /* Enable HSRUN mode */
    SMC_PMPROT = SMC_PMPROT_AHSRUN;

    /* Configure HSRUN clock control (same as RUN for now with FIRC) */
    SCG_HCCR = SCG_xCCR_SCS_FIRC |
               (0UL << SCG_xCCR_DIVCORE_SHIFT) |
               (0UL << SCG_xCCR_DIVBUS_SHIFT) |
               (1UL << SCG_xCCR_DIVSLOW_SHIFT);

    /* Enter HSRUN mode */
    SMC_PMCTRL = (SMC_PMCTRL & ~(3UL << SMC_PMCTRL_RUNM_SHIFT)) | SMC_PMCTRL_RUNM_HSRUN;

    /* Wait for HSRUN */
    while ((SMC_PMSTAT & 0xFF) != SMC_PMSTAT_HSRUN) {}
#endif
}

/* ============== UART Functions ============== */

#ifdef DEBUG_UART

#ifndef UART_BAUDRATE
#define UART_BAUDRATE 115200
#endif

void uart_init(void)
{
    uint32_t sbr;
    uint32_t osr = 16;  /* Oversampling ratio */
    uint32_t uart_clock = 48000000UL;  /* FIRC 48 MHz */

    /* Enable clock to PORTC */
    PCC_PORTC |= PCC_CGC;

    /* Configure pins for LPUART1:
     * PTC6 = LPUART1_RX (ALT2)
     * PTC7 = LPUART1_TX (ALT2)
     */
    PORTC_PCR6 = PORT_PCR_MUX_ALT2;
    PORTC_PCR7 = PORT_PCR_MUX_ALT2;

    /* Enable clock to LPUART1, source = FIRC (48 MHz) */
    PCC_LPUART1 = 0;  /* Disable before changing source */
    PCC_LPUART1 = PCC_PCS_FIRC | PCC_CGC;

    /* Calculate baud rate:
     * SBR = UART_CLK / (BAUD * OSR)
     */
    sbr = uart_clock / (UART_BAUDRATE * osr);

    /* Disable TX/RX before configuration */
    LPUART1_CTRL = 0;

    /* Configure baud rate */
    LPUART1_BAUD = ((osr - 1) << LPUART_BAUD_OSR_SHIFT) |
                   (sbr << LPUART_BAUD_SBR_SHIFT);

    /* Enable transmitter and receiver */
    LPUART1_CTRL = LPUART_CTRL_TE | LPUART_CTRL_RE;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        /* Handle newline -> CRLF conversion */
        if (buf[i] == '\n') {
            /* Wait for transmit buffer empty */
            while (!(LPUART1_STAT & LPUART_STAT_TDRE)) {}
            LPUART1_DATA = '\r';
        }

        /* Wait for transmit buffer empty */
        while (!(LPUART1_STAT & LPUART_STAT_TDRE)) {}
        LPUART1_DATA = buf[i];
    }

    /* Wait for transmission complete */
    while (!(LPUART1_STAT & LPUART_STAT_TC)) {}
}

#endif /* DEBUG_UART */

/* ============== Flash Functions ============== */

static void RAMFUNCTION flash_wait_complete(void)
{
    /* Wait for command complete */
    while (!(FTFC_FSTAT & FTFC_FSTAT_CCIF)) {}
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* Clear error flags by writing 1 */
    if (FTFC_FSTAT & (FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL)) {
        FTFC_FSTAT = FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL;
    }
}

static int RAMFUNCTION flash_program_phrase(uint32_t address, const uint8_t *data)
{
    /* Wait for previous command to complete */
    flash_wait_complete();
    flash_clear_errors();

    /* Set up Program Phrase command (0x07)
     * Programs 8 bytes at the specified address
     */
    FTFC_FCCOB0 = FTFC_CMD_PROGRAM_PHRASE;
    FTFC_FCCOB1 = (uint8_t)(address >> 16);
    FTFC_FCCOB2 = (uint8_t)(address >> 8);
    FTFC_FCCOB3 = (uint8_t)(address);

    /* Data bytes (big-endian order in FCCOB registers) */
    FTFC_FCCOB4 = data[3];
    FTFC_FCCOB5 = data[2];
    FTFC_FCCOB6 = data[1];
    FTFC_FCCOB7 = data[0];
    FTFC_FCCOB8 = data[7];
    FTFC_FCCOB9 = data[6];
    FTFC_FCCOBA = data[5];
    FTFC_FCCOBB = data[4];

    /* Launch command */
    DSB();
    ISB();
    FTFC_FSTAT = FTFC_FSTAT_CCIF;

    /* Wait for completion */
    flash_wait_complete();

#ifdef WATCHDOG
    /* Refresh watchdog after flash operation */
    watchdog_refresh();
#endif

    /* Check for errors */
    if (FTFC_FSTAT & (FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL | FTFC_FSTAT_MGSTAT0)) {
        return -1;
    }

    return 0;
}

static int RAMFUNCTION flash_erase_sector_internal(uint32_t address)
{
    /* Wait for previous command to complete */
    flash_wait_complete();
    flash_clear_errors();

    /* Set up Erase Sector command (0x09) */
    FTFC_FCCOB0 = FTFC_CMD_ERASE_SECTOR;
    FTFC_FCCOB1 = (uint8_t)(address >> 16);
    FTFC_FCCOB2 = (uint8_t)(address >> 8);
    FTFC_FCCOB3 = (uint8_t)(address);

    /* Launch command */
    DSB();
    ISB();
    FTFC_FSTAT = FTFC_FSTAT_CCIF;

    /* Wait for completion */
    flash_wait_complete();

#ifdef WATCHDOG
    /* Refresh watchdog after potentially long flash operation */
    watchdog_refresh();
#endif

    /* Check for errors */
    if (FTFC_FSTAT & (FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL | FTFC_FSTAT_MGSTAT0)) {
        return -1;
    }

    return 0;
}

/* ============== HAL Interface Functions ============== */

void hal_init(void)
{
    /* Disable watchdog first - must be done early after reset */
    watchdog_disable();

    /* Initialize clocks */
    clock_init();

    /* Enable clock to flash controller */
    PCC_FTFC |= PCC_CGC;

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif

#ifdef WATCHDOG
    /* Re-enable watchdog with configured timeout */
#ifndef WATCHDOG_TIMEOUT_MS
#define WATCHDOG_TIMEOUT_MS 1000  /* Default 1 second timeout */
#endif
    watchdog_enable(WATCHDOG_TIMEOUT_MS);
#endif
}

void hal_prepare_boot(void)
{
    /* Nothing special needed before jumping to app */
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret = 0;
    int i = 0;
    uint8_t phrase_buf[FLASH_PHRASE_SIZE];
    const uint8_t empty_phrase[FLASH_PHRASE_SIZE] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };

    while (len > 0) {
        /* Handle unaligned start or partial phrase */
        if ((len < FLASH_PHRASE_SIZE) || (address & (FLASH_PHRASE_SIZE - 1))) {
            uint32_t aligned_addr = address & ~(FLASH_PHRASE_SIZE - 1);
            uint32_t offset = address - aligned_addr;
            int bytes_to_copy;

            /* Read current phrase data */
            memcpy(phrase_buf, (void*)aligned_addr, FLASH_PHRASE_SIZE);

            /* Calculate bytes to copy */
            bytes_to_copy = FLASH_PHRASE_SIZE - offset;
            if (bytes_to_copy > len) {
                bytes_to_copy = len;
            }

            /* Merge new data */
            memcpy(phrase_buf + offset, data + i, bytes_to_copy);

            /* Only program if not all 0xFF */
            if (memcmp(phrase_buf, empty_phrase, FLASH_PHRASE_SIZE) != 0) {
                ret = flash_program_phrase(aligned_addr, phrase_buf);
                if (ret != 0) {
                    return ret;
                }
            }

            address += bytes_to_copy;
            i += bytes_to_copy;
            len -= bytes_to_copy;
        }
        else {
            /* Program full phrases */
            while (len >= FLASH_PHRASE_SIZE) {
                /* Only program if not all 0xFF */
                if (memcmp(data + i, empty_phrase, FLASH_PHRASE_SIZE) != 0) {
                    ret = flash_program_phrase(address, data + i);
                    if (ret != 0) {
                        return ret;
                    }
                }

                address += FLASH_PHRASE_SIZE;
                i += FLASH_PHRASE_SIZE;
                len -= FLASH_PHRASE_SIZE;
            }
        }
    }

    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret;

    /* Align address to sector boundary */
    if (address % FLASH_SECTOR_SIZE) {
        address -= (address % FLASH_SECTOR_SIZE);
    }

    while (len > 0) {
        ret = flash_erase_sector_internal(address);
        if (ret != 0) {
            return ret;
        }

        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }

    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    /* Flash is always accessible on S32K1xx after reset */
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* No explicit lock needed */
}

