/* nrf54lm20.c
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

#ifdef TARGET_nrf54lm20

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hal.h"
#include "image.h"
#include "nrf54lm20.h"
#include "printf.h"
#include "wolfboot/wolfboot.h"

#ifndef DEBUG_UART
#define DEBUG_UART      1
#endif

/* UART */

#ifdef DEBUG_UART

#define UART_WRITE_BUF_SIZE 128

void sleep_us(uint32_t usec);

static void uart_init_device(int device, uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    int port = UART_PORT_NUM(device);
    int pinTx = UART_PIN_NUM_TX(device);
    int pinRx = UART_PIN_NUM_RX(device);

    UART_ENABLE(device) = UART_ENABLE_ENABLE_Disabled;

    /* Configure TX pin */
    GPIO_PIN_CNF(device, pinTx) = (GPIO_CNF_OUT | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_MCUSEL(0));
    /* Configure RX pin */
    GPIO_PIN_CNF(device, pinRx) = (GPIO_CNF_IN | GPIO_CNF_STD_DRIVE_0 | GPIO_CNF_MCUSEL(0));

    UART_PSEL_TXD(device) = ((pinTx << UART_PSEL_TXD_PIN_Pos) & UART_PSEL_TXD_PIN_Msk) |
                     ((port << UART_PSEL_TXD_PORT_Pos) & UART_PSEL_TXD_PORT_Msk);
    UART_PSEL_RXD(device) = ((pinRx << UART_PSEL_RXD_PIN_Pos) & UART_PSEL_RXD_PIN_Msk) |
                     ((port << UART_PSEL_RXD_PORT_Pos) & UART_PSEL_RXD_PORT_Msk);
    UART_PSEL_CTS(device) = UART_PSEL_CTS_CONNECT_Disconnected;
    UART_PSEL_RTS(device) = UART_PSEL_RTS_CONNECT_Disconnected;
    UART_BAUDRATE(device) = UART_BAUDRATE_BAUDRATE_Baud115200;
    UART_CONFIG(device) = 0; /* 8N1, no HW flow control */

    UART_ENABLE(device) = UART_ENABLE_ENABLE_Enabled;
}

void uart_write_raw(int device, const char* buffer, unsigned int sz)
{
    /* EasyDMA requires a RAM buffer */
    static uint8_t uartTxBuf[UART_WRITE_BUF_SIZE];

    while (sz > 0) {
        /*
         *  loop until all bytes written,
         *  but only write UART_WRITE_BUF_SIZE max chars at once
         */
        unsigned int xfer = sz;
        if (xfer > sizeof(uartTxBuf))
            xfer = sizeof(uartTxBuf);
        memcpy(uartTxBuf, buffer, xfer);

        UART_EVENTS_DMA_TX_END(device) = 0;
        UART_EVENTS_DMA_TX_BUSERROR(device) = 0;

        UART_DMA_TX_PTR(device) = (uint32_t)uartTxBuf;
        UART_DMA_TX_MAXCNT(device) = xfer;
        UART_TASKS_DMA_TX_START(device) = UART_TASKS_DMA_TX_START_START_Trigger;

        /* Avoid an infinite wait: break on end, bus error, or timeout */
        uint32_t guard = 0;
        uint32_t maxGuard = xfer; // num char times
        while((UART_EVENTS_DMA_TX_END(device) == 0) && (UART_EVENTS_DMA_TX_BUSERROR(device) == 0))
        {
            if (guard > maxGuard) {
                UART_TASKS_DMA_TX_STOP(device) = UART_TASKS_DMA_TX_STOP_STOP_Trigger;
                break;
            }
            ++guard;
            sleep_us(100);  /* at 115200, a char takes ~ 86us, round up to 100 */
        }

        sz -= xfer;
        buffer += xfer;
    }
}

void uart_write_device(int device, const char* buf, unsigned int sz)
{
    static char buffer[UART_WRITE_BUF_SIZE];
    int bufsz = 0;

    for(int i=0; i<(int)sz && bufsz < UART_WRITE_BUF_SIZE; i++)
    {
        char ch = (char) buf[i];
        if(ch == '\r')
            continue;
        if(ch == '\n')
            buffer[bufsz++] = '\r';
        buffer[bufsz++] = ch;
    }
    uart_write_raw(device, buffer, bufsz);
}

void uart_write(const char* buf, unsigned int sz)
{
   uart_write_device(DEVICE_MONITOR, buf, sz);
}

#endif /* DEBUG_UART */

#if (defined DEBUG_UART || UART_FLASH)
#define UART_RX_TIMEOUT 1000000UL
int uart_read(int device, uint8_t* buf, unsigned int sz)
{
    if ((buf == NULL) || (sz == 0))
        return -1;

    UART_EVENTS_DMA_RX_END(device) = 0;
    UART_EVENTS_DMA_RX_BUSERROR(device) = 0;

    UART_DMA_RX_PTR(device) = (uint32_t)buf;
    UART_DMA_RX_MAXCNT(device) = sz;

    UART_TASKS_DMA_RX_START(device) = UART_TASKS_DMA_RX_START_START_Trigger;

    for (uint32_t guard = 0; UART_EVENTS_DMA_RX_END(device) == 0; guard++) {
        if (UART_EVENTS_DMA_RX_BUSERROR(device) != 0) {
            UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
            return -1;
        }
        if (guard > UART_RX_TIMEOUT) {
            UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
            return 0;
        }
    }

    UART_TASKS_DMA_RX_STOP(device) = UART_TASKS_DMA_RX_STOP_STOP_Trigger;
    return (int)UART_DMA_RX_AMOUNT(device);
}
#endif   /* DEBUG_UART || UART_FLASH */

static void RAMFUNCTION flash_wait_ready(void)
{
    while ((RRAMC_READY & RRAMC_READY_READY_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_wait_ready_next(void)
{
    while ((RRAMC_READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_wait_buf_empty(void)
{
    while ((RRAMC_BUFSTATUS_WRITEBUFEMPTY &
            RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk) == 0U)
        ;
}

static void RAMFUNCTION flash_commit_writebuf(void)
{
    if ((RRAMC_BUFSTATUS_WRITEBUFEMPTY &
         RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk) == 0U) {
        RRAMC_TASKS_COMMITWRITEBUF =
            RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger;
        flash_wait_ready();
        flash_wait_buf_empty();
    }
}

static void RAMFUNCTION flash_write_enable(int enable)
{
    uint32_t cfg = RRAMC_CONFIG;
    if (enable != 0)
        cfg |= RRAMC_CONFIG_WEN_Msk;
    else
        cfg &= ~RRAMC_CONFIG_WEN_Msk;
    RRAMC_CONFIG = cfg;
    flash_wait_ready();
}

static int RAMFUNCTION flash_program_range(uint32_t address,
                                           const uint8_t *data, int len)
{
    int i = 0;

    while (i < len) {
        flash_wait_ready_next();

        if ((((address + i) & 0x3U) == 0U) &&
            ((((uintptr_t)(data + i)) & 0x3U) == 0U) &&
            (len - i) >= 4) {
            const uint32_t *src = (const uint32_t *)(data + i);
            volatile uint32_t *dst = (volatile uint32_t *)(address + i);
            *dst = *src;
            i += 4;
        }
        else {
            uint32_t word;
            volatile uint32_t *dst =
                (volatile uint32_t *)((address + i) & ~0x3U);
            int offset = (int)((address + i) & 0x3U);

            word = *dst;
            ((uint8_t *)&word)[offset] = data[i];
            *dst = word;
            i++;
        }
    }

    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    flash_write_enable(1);
    flash_program_range(address, data, len);
    flash_commit_writebuf();
    flash_write_enable(0);
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + (uint32_t)len;
    uint8_t blank[64];

    memset(blank, 0xFF, sizeof(blank));

    flash_write_enable(1);
    while (address < end) {
        int chunk = (int)(end - address);
        if (chunk > (int)sizeof(blank))
            chunk = (int)sizeof(blank);
        flash_program_range(address, blank, chunk);
        address += (uint32_t)chunk;
    }
    flash_commit_writebuf();
    flash_write_enable(0);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_write_enable(1);
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_write_enable(0);
}

#if (UART_FLASH)
int uart_tx(const uint8_t c)
{

    uart_write((const char *)&c, 1);
    return 0;
}

int uart_rx(uint8_t *c)
{
    return uart_read(DEVICE_DOWNLOAD, c, 1);
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uart_init_device(DEVICE_DOWNLOAD, bitrate, data, parity, stop);
    return 0;
}
#else
/* NOTE: this is defined differently when UART_FLASH is not defined */
void uart_init(void)
{
    uart_init_device(DEVICE_DOWNLOAD, 115200, 8, 'N', 1);
}
#endif

static uintptr_t ext_flash_addr_calc(uintptr_t address)
{
    /* offset external flash addresses by the update partition address */
    address -= WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    return address;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
#ifdef DEBUG_FLASH
    uintptr_t addr = ext_flash_addr_calc(address);
    wolfBoot_printf("Ext Write: Len %d, Addr 0x%x (off 0x%x) -> 0x%x\n",
        len, address, addr, data);
#endif
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
#ifdef DEBUG_FLASH
    uintptr_t addr = ext_flash_addr_calc(address);
    wolfBoot_printf("Ext Read: Len %d, Addr 0x%x (off 0x%x) -> %p\n",
        len, address, addr, data);
#endif
    memset(data, FLASH_BYTE_ERASED, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
#ifdef DEBUG_FLASH
    uintptr_t addr = ext_flash_addr_calc(address);
    wolfBoot_printf("Ext Erase: Len %d, Addr 0x%x (off 0x%x)\n",
        len, address, addr);
#endif
    return 0;
}

void ext_flash_lock(void)
{
    /* no op */
}

void ext_flash_unlock(void)
{
    /* no op */
}

static void high_freq_clock_init(void)
{
    /* Start the HFXO and wait until it is running */
    CLOCK_EVENTS_XOSTARTED = 0;
    CLOCK_TASKS_XOSTART = CLOCK_TASKS_XOSTART_TASKS_XOSTART_Trigger;

    while ((CLOCK_EVENTS_XOSTARTED == 0) ||
           ((CLOCK_XO_STAT & CLOCK_XO_STAT_STATE_Msk) ==
            (CLOCK_XO_STAT_STATE_NotRunning << CLOCK_XO_STAT_STATE_Pos))) {
        /* wait */
    }
}

static void low_freq_clock_init(void)
{
    /* Configure the 32.768 kHz crystal load caps using factory trim when present */
    uint32_t intcap = OSCILLATORS_XOSC32KI_INTCAP_ResetValue &
                      OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    if (FICR_XOSC32KTRIM != FICR_XOSC32KTRIM_ResetValue) {
        uint32_t trim = (FICR_XOSC32KTRIM & FICR_XOSC32KTRIM_OFFSET_Msk) >>
                        FICR_XOSC32KTRIM_OFFSET_Pos;
        intcap = trim & (OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk >>
                         OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos);
    }

    OSCILLATORS_XOSC32KI_INTCAP =
        (intcap << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    /* Start the LFCLK from the external LFXO and wait until it is running */
    CLOCK_EVENTS_LFCLKSTARTED = 0;
    CLOCK_LFCLK_SRC = CLOCK_LFCLK_SRC_SRC_LFXO;
    CLOCK_TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;

    while ((CLOCK_EVENTS_LFCLKSTARTED == 0) ||
           ((CLOCK_LFCLK_STAT & CLOCK_LFCLK_STAT_SRC_Msk) !=
            (CLOCK_LFCLK_STAT_SRC_LFXO << CLOCK_LFCLK_STAT_SRC_Pos)) ||
           ((CLOCK_LFCLK_STAT & CLOCK_LFCLK_STAT_STATE_Msk) ==
            (CLOCK_LFCLK_STAT_STATE_NotRunning << CLOCK_LFCLK_STAT_STATE_Pos))) {
        /* wait */
    }
}

static void clock_init(void)
{
    high_freq_clock_init();
    low_freq_clock_init();
}

static void clock_deinit(void)
{
}

static void grtc_counter_init(void)
{
    static bool grtc_started;
    if (!grtc_started) {
        GRTC_MODE |= (GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
        GRTC_TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
        GRTC_SYSCOUNTER_ACTIVE(0) = GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active;
        while ((GRTC_STATUS_LFTIMER & GRTC_STATUS_LFTIMER_READY_Msk) == 0U)
            ;
        grtc_started = true;
    }
}

static uint64_t grtc_counter_read_us(void)
{
    const uint32_t idx = 0U;
    uint32_t high1, high2, low;

    while (1) {
        high1 = (GRTC_SYSCOUNTERH(idx) & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk) >>
                GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos;
        low = GRTC_SYSCOUNTERL(idx);
        high2 = (GRTC_SYSCOUNTERH(idx) & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk) >>
                GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos;
        if (high1 == high2)
            break;
    }

    return (((uint64_t)high2 << 32) | (uint64_t)low);
}

void sleep_us(uint32_t usec)
{
    if (usec == 0U)
        return;

    uint64_t start = grtc_counter_read_us();
    while (1) {
        uint64_t now = grtc_counter_read_us();
        if (((now - start) & GRTC_COUNTER_MASK) >= (uint64_t)usec)
            break;
    }
}

void sleep_ms(uint32_t msec)
{
    sleep_us(msec * 1000);
}

void hal_monitor(void)
{
#if (USE_MONITOR)
    monitor_loop();
#endif
}

void hal_init(void)
{
#ifdef DEBUG_UART
    const char* bootStr = "wolfBoot HAL Init\n";
#endif

    clock_init();
    grtc_counter_init();

#if USE_PMIC_LED
    if (!npm1300_configure_led_power())
        pmic_led_power_control(true);
#endif

#ifdef DEBUG_UART
    uart_init_device(DEVICE_MONITOR, 115200, 8, 'N', 1);
    uart_write(bootStr, strlen(bootStr));
#endif
}

/* enable write protection for the region of flash specified */
static int hal_flash_protect(uint32_t start, uint32_t len)
{
    return 0;
}

void hal_prepare_boot(void)
{
    /* Write protect bootloader region of flash */
    hal_flash_protect(WOLFBOOT_ORIGIN, BOOTLOADER_PARTITION_SIZE);

    clock_deinit();
}

#endif /* TARGET_nrf54lm20 */
