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
#include "printf.h"

/* Override RAMFUNCTION for test-app: when RAM_CODE is set but not __WOLFBOOT,
 * we still need flash functions to run from RAM for self-programming. */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")

/* PRIMASK helpers for critical sections */
#define __get_PRIMASK() ({ \
    uint32_t primask; \
    __asm__ volatile ("mrs %0, primask" : "=r" (primask)); \
    primask; \
})

#define __set_PRIMASK(primask) \
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory")

#define __disable_irq() \
    __asm__ volatile ("cpsid i" ::: "memory")

#define __enable_irq() \
    __asm__ volatile ("cpsie i" ::: "memory")

#include "s32k1xx.h"

/* ============== Flash Configuration Field (FCF) ============== */
/* Located at 0x400-0x40F in flash */

#ifdef __WOLFBOOT

/* Flash Option Byte - override with CFLAGS_EXTRA+=-DWOLFBOOT_FOPT=0xF7 */
#ifndef WOLFBOOT_FOPT
#define WOLFBOOT_FOPT 0xFF
#endif

#define FCF_LEN (16)
const uint8_t __attribute__((section(".flash_config"))) flash_config[FCF_LEN] = {
    /* Backdoor comparison key (8 bytes) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* Program Flash Protection (4 bytes) - all unprotected */
    0xFF, 0xFF, 0xFF, 0xFF,
    /* Flash Security Byte */
    0xFE,  /* SEC=10 (unsecured), FSLACC=11, MEEN=11, KEYEN=11 */
    /* Flash Option Byte */
    WOLFBOOT_FOPT,
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

#endif /* WATCHDOG */

/* ============== Clock Configuration ============== */

/* SIRC - Slow Internal RC (8 MHz) register fields */
#define SCG_SIRCCSR_SIRCEN      (1UL << 0)
#define SCG_SIRCCSR_SIRCVLD     (1UL << 24)
#define SCG_xCCR_SCS_SIRC       (2UL << SCG_xCCR_SCS_SHIFT)
#define SCG_CSR_SCS_SIRC        (2UL << SCG_CSR_SCS_SHIFT)

#ifdef WOLFBOOT_RESTORE_CLOCK
/* Restore clock to safe default (SIRC 8 MHz) before booting application.
 * This allows the application to configure clocks from a known state.
 */
static void clock_restore_sirc(void)
{
    /* Enable SIRC (8 MHz) if not already enabled */
    SCG_SIRCDIV = (1UL << 8) | (1UL << 0);  /* SIRCDIV1=/1, SIRCDIV2=/1 */
    SCG_SIRCCFG = 0;  /* Range 0: 2 MHz (default) - actually S32K uses 8MHz SIRC */
    SCG_SIRCCSR = SCG_SIRCCSR_SIRCEN;

    /* Wait for SIRC valid */
    while (!(SCG_SIRCCSR & SCG_SIRCCSR_SIRCVLD)) {}

    /* Switch to SIRC as system clock
     * SCS = SIRC (2)
     * DIVCORE = /1 (8 MHz)
     * DIVBUS = /1 (8 MHz)
     * DIVSLOW = /1 (8 MHz)
     */
    SCG_RCCR = SCG_xCCR_SCS_SIRC |
               (0UL << SCG_xCCR_DIVCORE_SHIFT) |
               (0UL << SCG_xCCR_DIVBUS_SHIFT) |
               (0UL << SCG_xCCR_DIVSLOW_SHIFT);

    /* Wait for clock switch */
    while ((SCG_CSR & SCG_CSR_SCS_MASK) != SCG_CSR_SCS_SIRC) {}

    /* Disable FIRC to save power (application can re-enable if needed) */
    SCG_FIRCCSR &= ~SCG_FIRCCSR_FIRCEN;
}
#endif /* WOLFBOOT_RESTORE_CLOCK */

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
    /* S32K1xx SPLL requires SOSC as source (FIRC cannot be used directly).
     * For 112 MHz with 8 MHz SOSC: PREDIV=0, MULT=28 -> VCO=224 MHz, SPLL=112 MHz
     * VCO range: 180-320 MHz, SPLL_CLK = VCO / 2
     *
     * Currently using FIRC 48 MHz directly. TODO: Add SOSC + SPLL for 112 MHz.
     */
    SCG_SPLLCSR &= ~SCG_SPLLCSR_SPLLEN;
    SCG_SPLLDIV = (2UL << 8) | (4UL << 0);  /* SPLLDIV1=/2, SPLLDIV2=/4 */
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

    /* Enable clock to TX and RX port(s)
     * Note: If TX and RX use different ports, both need clock enabled
     */
    DEBUG_UART_TX_PCC_PORT |= PCC_CGC;
#if !DEBUG_UART_SAME_PORT
    DEBUG_UART_RX_PCC_PORT |= PCC_CGC;
#endif

    /* Configure pins for selected LPUART */
    DEBUG_UART_RX_PCR = DEBUG_UART_RX_MUX;
    DEBUG_UART_TX_PCR = DEBUG_UART_TX_MUX;

    /* Enable clock to selected LPUART, source = FIRC (48 MHz) */
    PCC_LPUART = 0;  /* Disable before changing source */
    PCC_LPUART = PCC_PCS_FIRC | PCC_CGC;

    /* Calculate baud rate:
     * SBR = UART_CLK / (BAUD * OSR)
     */
    sbr = uart_clock / (UART_BAUDRATE * osr);

    /* Disable TX/RX before configuration */
    LPUART_CTRL = 0;

    /* Configure baud rate */
    LPUART_BAUD = ((osr - 1) << LPUART_BAUD_OSR_SHIFT) |
                  (sbr << LPUART_BAUD_SBR_SHIFT);

    /* Enable transmitter and receiver */
    LPUART_CTRL = LPUART_CTRL_TE | LPUART_CTRL_RE;
}

/* Transmit a single byte (raw, no conversion) - RAMFUNCTION for use during flash ops */
void RAMFUNCTION uart_tx(uint8_t byte)
{
    while (!(LPUART1_STAT & LPUART_STAT_TDRE)) {}
    LPUART1_DATA = byte;
    while (!(LPUART1_STAT & LPUART_STAT_TC)) {}
}

/* Used for sending ASCII and CRLF conversions */
void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        /* Handle newline -> CRLF conversion */
        if (buf[i] == '\n') {
            uart_tx('\r');
        }
        uart_tx(buf[i]);
    }

    /* Wait for transmission complete */
    while (!(LPUART_STAT & LPUART_STAT_TC)) {}
}

/* Read a single character from UART (non-blocking) - RAMFUNCTION for use during flash ops
 * Returns: 1 if character read, 0 if no data available
 */
int RAMFUNCTION uart_read(char* c)
{
    uint32_t stat = LPUART1_STAT;

    /* Clear any error flags first */
    if (stat & (LPUART_STAT_OR | LPUART_STAT_NF | LPUART_STAT_FE | LPUART_STAT_PF)) {
        LPUART1_STAT = stat;  /* Write 1 to clear flags */
    }

    /* Check if data available - read even if there was an error */
    if (stat & LPUART_STAT_RDRF) {
        *c = (char)(LPUART1_DATA & 0xFF);
        return 1;
    }

    return 0;  /* No data available */
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
    /* Skip if phrase is all 0xFF (erased) */
    if (data[0] == 0xFF && data[1] == 0xFF && data[2] == 0xFF && data[3] == 0xFF &&
        data[4] == 0xFF && data[5] == 0xFF && data[6] == 0xFF && data[7] == 0xFF) {
        return 0;
    }

    /* Wait for previous command to complete */
    flash_wait_complete();
    flash_clear_errors();

    /* Set up Program Phrase command (0x07) */
    /* Programs 8 bytes at the specified address */
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
    WDOG_CNT = WDOG_CNT_REFRESH;
#endif

    /* Check for errors */
    if (FTFC_FSTAT & (FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL | FTFC_FSTAT_MGSTAT0)) {
        return -1;
    }

    return 0;
}

static int RAMFUNCTION flash_erase_sector_internal(uint32_t address)
{
    uint32_t primask;

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

    /* Disable interrupts during flash operation to prevent code fetch from flash */
    primask = __get_PRIMASK();
    __disable_irq();

    FTFC_FSTAT = FTFC_FSTAT_CCIF;

    /* Wait for completion */
    flash_wait_complete();

    /* Re-enable interrupts */
    __set_PRIMASK(primask);

#ifdef WATCHDOG
    /* Refresh watchdog after potentially long flash operation */
    WDOG_CNT = WDOG_CNT_REFRESH;
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

#ifdef __WOLFBOOT
#ifdef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("wolfBoot Version: %s\n", LIBWOLFBOOT_VERSION_STRING);
#else
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING,__DATE__, __TIME__);
#endif
#endif
#endif

#ifdef WATCHDOG
    watchdog_enable(WATCHDOG_TIMEOUT_MS);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    /* Wait for any pending UART transmission to complete */
    while (!(LPUART_STAT & LPUART_STAT_TC)) {}

    /* Disable UART before jumping to application.
     * This gives the application a clean UART state to initialize from.
     * Without this, the application may have issues reinitializing the UART.
     */
    LPUART_CTRL = 0;
#endif

#ifdef WOLFBOOT_RESTORE_CLOCK
    /* Restore clock to SIRC (8 MHz) before booting application.
     * This gives the application a known clock state to start from.
     */
    clock_restore_sirc();
#endif

    /* Re-enable watchdog before booting application.
     * The watchdog is enabled by default after reset, so the application
     * may expect it to be running. Use a generous timeout to give the
     * application time to reconfigure or disable the watchdog.
     */
#ifndef WOLFBOOT_DISABLE_WATCHDOG_ON_BOOT
    {
        /* Unlock watchdog */
        WDOG_CNT = WDOG_CNT_UNLOCK;
        while (!(WDOG_CS & WDOG_CS_ULK)) {}

        /* Enable watchdog with ~2 second timeout (256k ticks at 128kHz LPO)
         * Application should either service or reconfigure the watchdog
         */
        WDOG_TOVAL = 0xFFFF;  /* Max timeout ~512ms without prescaler */
        WDOG_CS = WDOG_CS_EN | WDOG_CS_UPDATE | WDOG_CS_CMD32EN |
                  WDOG_CS_CLK_LPO | WDOG_CS_PRES;  /* With prescaler: ~131 sec */

        /* Wait for reconfiguration to complete */
        while (!(WDOG_CS & WDOG_CS_RCS)) {}
    }
#endif
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret, i = 0;
    uint8_t phrase_buf[FLASH_PHRASE_SIZE];

    while (len > 0) {
        if ((len < FLASH_PHRASE_SIZE) || (address & (FLASH_PHRASE_SIZE - 1))) {
            /* Handle unaligned start or partial phrase */
            uint32_t aligned_addr = address & ~(FLASH_PHRASE_SIZE - 1);
            uint32_t offset = address - aligned_addr;
            int bytes_to_copy = FLASH_PHRASE_SIZE - offset;
            if (bytes_to_copy > len)
                bytes_to_copy = len;

            memcpy(phrase_buf, (void*)aligned_addr, FLASH_PHRASE_SIZE);
            memcpy(phrase_buf + offset, data + i, bytes_to_copy);

            ret = flash_program_phrase(aligned_addr, phrase_buf);
            if (ret != 0)
                return ret;

            address += bytes_to_copy;
            i += bytes_to_copy;
            len -= bytes_to_copy;
        } else {
            /* Program full phrases */
            while (len >= FLASH_PHRASE_SIZE) {
                ret = flash_program_phrase(address, data + i);
                if (ret != 0)
                    return ret;
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
    /* Ensure flash controller clock is enabled */
    PCC_FTFC |= PCC_CGC;

    /* Clear any pending errors */
    if (FTFC_FSTAT & (FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL)) {
        FTFC_FSTAT = FTFC_FSTAT_ACCERR | FTFC_FSTAT_FPVIOL;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* No explicit lock needed */
}

