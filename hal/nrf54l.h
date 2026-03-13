/* nrf54l.h
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

#ifndef _HAL_NRF54L_H_
#define _HAL_NRF54L_H_

#include <stdint.h>

#include <string.h>

void uart_write_device(int device, const char* buf, unsigned int sz);


#if !defined(TZEN) || !defined(NONSECURE_APP)
    /* Use secure addresses */
#   define TZ_SECURE() 1
#else
    /* Use non-secure addresses */
#   define TZ_SECURE() 0
#endif

#define CPU_CLOCK 128000000UL

#define FLASH_BASE_ADDR (0x00000000UL)
#define FLASH_SIZE (1560576UL) /* 1524KB (nRF54L15) */
/* The RRAM doesn't have a page size, this is just for wolfBoot purposes */
#define FLASH_PAGE_SIZE (0x1000UL) /* 4KB granularity */
#define FLASH_END (FLASH_BASE_ADDR + FLASH_SIZE)

/* Assembly helpers */
#define DMB() __asm__ volatile ("dmb")
#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")
#define NOP() __asm__ volatile ("nop")

/* PSEL Port (bit 5) - used across UART/SPI GPIO muxing */
#define PSEL_PORT(n)         (((n) & 0x7) << 5)


#if TZ_SECURE()
    #define CLOCK_BASE_DEFAULT              (0x5010E000UL)
    #define OSCILLATORS_BASE_DEFAULT        (0x50120000UL)
#else
    #define CLOCK_BASE_DEFAULT              (0x4010E000UL)
    #define OSCILLATORS_BASE_DEFAULT        (0x40120000UL)
#endif

#define CLOCK_BASE              CLOCK_BASE_DEFAULT
#define OSCILLATORS_BASE        OSCILLATORS_BASE_DEFAULT
#define FICR_BASE               (0x00FFC000UL)

/* Clock control registers */
#define CLOCK_TASKS_XOSTART         *((volatile uint32_t *)(CLOCK_BASE + 0x000))
#define CLOCK_TASKS_LFCLKSTART      *((volatile uint32_t *)(CLOCK_BASE + 0x010))
#define CLOCK_EVENTS_XOSTARTED      *((volatile uint32_t *)(CLOCK_BASE + 0x100))
#define CLOCK_EVENTS_LFCLKSTARTED   *((volatile uint32_t *)(CLOCK_BASE + 0x108))
#define CLOCK_XO_STAT               *((volatile uint32_t *)(CLOCK_BASE + 0x40C))
#define CLOCK_LFCLK_SRC             *((volatile uint32_t *)(CLOCK_BASE + 0x440))
#define CLOCK_LFCLK_STAT            *((volatile uint32_t *)(CLOCK_BASE + 0x44C))

#define CLOCK_TASKS_XOSTART_TASKS_XOSTART_Trigger       0x1UL
#define CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger 0x1UL

#define CLOCK_XO_STAT_STATE_Pos        16UL
#define CLOCK_XO_STAT_STATE_Msk        (0x1UL << CLOCK_XO_STAT_STATE_Pos)
#define CLOCK_XO_STAT_STATE_NotRunning 0x0UL
#define CLOCK_XO_STAT_STATE_Running    0x1UL

#define CLOCK_LFCLK_SRC_SRC_Pos        0UL
#define CLOCK_LFCLK_SRC_SRC_Msk        (0x3UL << CLOCK_LFCLK_SRC_SRC_Pos)
#define CLOCK_LFCLK_SRC_SRC_LFRC       0x0UL
#define CLOCK_LFCLK_SRC_SRC_LFXO       0x1UL
#define CLOCK_LFCLK_SRC_SRC_LFSYNT     0x2UL

#define CLOCK_LFCLK_STAT_SRC_Pos       0UL
#define CLOCK_LFCLK_STAT_SRC_Msk       (0x3UL << CLOCK_LFCLK_STAT_SRC_Pos)
#define CLOCK_LFCLK_STAT_SRC_LFRC      0x0UL
#define CLOCK_LFCLK_STAT_SRC_LFXO      0x1UL
#define CLOCK_LFCLK_STAT_SRC_LFSYNT    0x2UL

#define CLOCK_LFCLK_STAT_STATE_Pos     16UL
#define CLOCK_LFCLK_STAT_STATE_Msk     (0x1UL << CLOCK_LFCLK_STAT_STATE_Pos)
#define CLOCK_LFCLK_STAT_STATE_NotRunning 0x0UL
#define CLOCK_LFCLK_STAT_STATE_Running 0x1UL

/* LFCLK source and oscillator trims */
#define OSCILLATORS_XOSC32KI_INTCAP *((volatile uint32_t *)(OSCILLATORS_BASE + 0x904))
#define OSCILLATORS_XOSC32KI_INTCAP_ResetValue   0x00000017UL
#define OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos      0UL
#define OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk      (0x1FUL << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos)

#define FICR_XOSC32KTRIM             *((volatile uint32_t *)(FICR_BASE + 0x624))
#define FICR_XOSC32KTRIM_ResetValue  0xFFFFFFFFUL
#define FICR_XOSC32KTRIM_OFFSET_Pos  16UL
#define FICR_XOSC32KTRIM_OFFSET_Msk  (0x3FFUL << FICR_XOSC32KTRIM_OFFSET_Pos)

/* RRAM controller */
#define RRAMC_BASE_DEFAULT            (0x5004B000UL)
#define RRAMC_BASE                    RRAMC_BASE_DEFAULT

#define RRAMC_TASKS_COMMITWRITEBUF    *((volatile uint32_t *)(RRAMC_BASE + 0x008))
#define RRAMC_READY                   *((volatile uint32_t *)(RRAMC_BASE + 0x400))
#define RRAMC_READYNEXT               *((volatile uint32_t *)(RRAMC_BASE + 0x404))
#define RRAMC_BUFSTATUS_WRITEBUFEMPTY *((volatile uint32_t *)(RRAMC_BASE + 0x418))
#define RRAMC_CONFIG                  *((volatile uint32_t *)(RRAMC_BASE + 0x500))

#define RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger 0x1UL

#define RRAMC_READY_READY_Pos         0UL
#define RRAMC_READY_READY_Msk         (0x1UL << RRAMC_READY_READY_Pos)

#define RRAMC_READYNEXT_READYNEXT_Pos 0UL
#define RRAMC_READYNEXT_READYNEXT_Msk (0x1UL << RRAMC_READYNEXT_READYNEXT_Pos)

#define RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Pos 0UL
#define RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk \
    (0x1UL << RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Pos)

#define RRAMC_CONFIG_WEN_Pos          0UL
#define RRAMC_CONFIG_WEN_Msk          (0x1UL << RRAMC_CONFIG_WEN_Pos)

/* TWIM */
#if TZ_SECURE()
    #define TWIM20_BASE_DEFAULT       (0x500C6000UL)
#else
    #define TWIM20_BASE_DEFAULT       (0x400C6000UL)
#endif

#define PMIC_TWIM_BASE                TWIM20_BASE_DEFAULT

#define TWIM_TASKS_STOP(base)         (*((volatile uint32_t *)((base) + 0x004)))
#define TWIM_TASKS_DMA_RX_START(base) (*((volatile uint32_t *)((base) + 0x028)))
#define TWIM_TASKS_DMA_TX_START(base) (*((volatile uint32_t *)((base) + 0x050)))
#define TWIM_EVENTS_STOPPED(base)     (*((volatile uint32_t *)((base) + 0x104)))
#define TWIM_EVENTS_ERROR(base)       (*((volatile uint32_t *)((base) + 0x114)))
#define TWIM_EVENTS_LASTRX(base)      (*((volatile uint32_t *)((base) + 0x134)))
#define TWIM_EVENTS_LASTTX(base)      (*((volatile uint32_t *)((base) + 0x138)))
#define TWIM_EVENTS_DMA_RX_END(base)  (*((volatile uint32_t *)((base) + 0x14C)))
#define TWIM_EVENTS_DMA_TX_END(base)  (*((volatile uint32_t *)((base) + 0x168)))
#define TWIM_SHORTS_REG(base)         (*((volatile uint32_t *)((base) + 0x200)))
#define TWIM_ERRORSRC_REG(base)       (*((volatile uint32_t *)((base) + 0x4C4)))
#define TWIM_ENABLE_REG(base)         (*((volatile uint32_t *)((base) + 0x500)))
#define TWIM_FREQUENCY_REG(base)      (*((volatile uint32_t *)((base) + 0x524)))
#define TWIM_ADDRESS_REG(base)        (*((volatile uint32_t *)((base) + 0x588)))
#define TWIM_PSEL_SCL_REG(base)       (*((volatile uint32_t *)((base) + 0x600)))
#define TWIM_PSEL_SDA_REG(base)       (*((volatile uint32_t *)((base) + 0x604)))
#define TWIM_DMA_RX_PTR(base)         (*((volatile uint32_t *)((base) + 0x704)))
#define TWIM_DMA_RX_MAXCNT(base)      (*((volatile uint32_t *)((base) + 0x708)))
#define TWIM_DMA_RX_TERMINATE(base)   (*((volatile uint32_t *)((base) + 0x71C)))
#define TWIM_DMA_TX_PTR(base)         (*((volatile uint32_t *)((base) + 0x73C)))
#define TWIM_DMA_TX_MAXCNT(base)      (*((volatile uint32_t *)((base) + 0x740)))
#define TWIM_DMA_TX_TERMINATE(base)   (*((volatile uint32_t *)((base) + 0x754)))

#define TWIM_TASKS_STOP_TASKS_STOP_Trigger           0x1UL
#define TWIM_TASKS_DMA_RX_START_START_Trigger        0x1UL
#define TWIM_TASKS_DMA_TX_START_START_Trigger        0x1UL
#define TWIM_ENABLE_ENABLE_Disabled                  0x0UL
#define TWIM_ENABLE_ENABLE_Enabled                   0x6UL
#define TWIM_FREQUENCY_FREQUENCY_K100                0x01980000UL
#define TWIM_DMA_RX_TERMINATEONBUSERROR_ENABLE_Enabled 0x1UL
#define TWIM_DMA_TX_TERMINATEONBUSERROR_ENABLE_Enabled 0x1UL
#define TWIM_SHORTS_LASTTX_DMA_RX_START_Msk          (0x1UL << 7)
#define TWIM_SHORTS_LASTTX_STOP_Msk                  (0x1UL << 9)
#define TWIM_SHORTS_LASTRX_STOP_Msk                  (0x1UL << 12)



/* GPIO */
#if TZ_SECURE()
    #define GPIO_P0_S_BASE               (0x5010A000UL)
    #define GPIO_P1_S_BASE               (0x500D8200UL)
    #define GPIO_P2_S_BASE               (0x50050400UL)
#else
    #define GPIO_P0_NS_BASE              (0x4010A000UL)
    #define GPIO_P1_NS_BASE              (0x400D8200UL)
    #define GPIO_P2_NS_BASE              (0x40050400UL)
#endif

/* GPIO configuration */
#if TZ_SECURE()
    #define GPIO_PORT0_BASE_DEFAULT     GPIO_P0_S_BASE
    #define GPIO_PORT1_BASE_DEFAULT     GPIO_P1_S_BASE
    #define GPIO_PORT2_BASE_DEFAULT     GPIO_P2_S_BASE
#else
    #define GPIO_PORT0_BASE_DEFAULT     GPIO_P0_NS_BASE
    #define GPIO_PORT1_BASE_DEFAULT     GPIO_P1_NS_BASE
    #define GPIO_PORT2_BASE_DEFAULT     GPIO_P2_NS_BASE
#endif

#define GPIO0_BASE     GPIO_PORT0_BASE_DEFAULT
#define GPIO1_BASE     GPIO_PORT1_BASE_DEFAULT
#define GPIO2_BASE     GPIO_PORT2_BASE_DEFAULT

static inline uintptr_t hal_gpio_port_base(unsigned int port)
{
    switch (port) {
    case 0:
        return (uintptr_t)GPIO0_BASE;
    case 1:
        return (uintptr_t)GPIO1_BASE;
    case 2:
        return (uintptr_t)GPIO2_BASE;
    default:
        return (uintptr_t)GPIO0_BASE;
    }
}

#define GPIO_BASE(n)        hal_gpio_port_base((unsigned int)(n))
#define GPIO_OUT(n)         *((volatile uint32_t *)(GPIO_BASE(n) + 0x000))
#define GPIO_OUTSET(n)      *((volatile uint32_t *)(GPIO_BASE(n) + 0x004))
#define GPIO_OUTCLR(n)      *((volatile uint32_t *)(GPIO_BASE(n) + 0x008))
#define GPIO_DIRSET(n)      *((volatile uint32_t *)(GPIO_BASE(n) + 0x014))
#define GPIO_DIRCLR(n)      *((volatile uint32_t *)(GPIO_BASE(n) + 0x018))
#define GPIO_PIN_CNF(n, p)  *((volatile uint32_t *)(GPIO_BASE(n) + 0x080 + ((p) * 0x4)))

#define GPIO_CNF_IN                 0       // input
#define GPIO_CNF_OUT                1       // output
#define GPIO_CNF_IN_DIS             2       // input, disconnect input buffer
#define GPIO_CNF_OUT_DIS            3       // output, disconnect input buffer
#define GPIO_CNF_PULL_DIS           0
#define GPIO_CNF_PULL_UP            (3UL << 2)
#define GPIO_CNF_PULL_DOWN          (1UL << 2)
#define GPIO_CNF_STD_DRIVE_0        0
#define GPIO_CNF_HIGH_DRIVE_0       (1UL << 8)     // High drive
#define GPIO_CNF_EXTRA_HIGH_DRIVE_0 (3UL << 8)     // Extra-High drive
#define GPIO_CNF_STD_DRIVE_1        0
#define GPIO_CNF_HIGH_DRIVE_1       (1UL << 10)     // High drive
#define GPIO_CNF_EXTRA_HIGH_DRIVE_1 (3UL << 10)     // Extra-High drive
#define GPIO_CNF_SENSE_NONE         0
#define GPIO_CNF_MCUSEL(n)          (((n) & 0x7) << 28)


/* UART */

// UART Device     P0   P1   P2 
// -----------------------------
// NRF_UARTE00               XX  (P2 dedicated)
// NRF_UARTE20          XX  (XX) (P2 cross-domain only)
// NRF_UARTE21          XX  (XX) (P2 cross-domain only)
// NRF_UARTE22          XX
// NRF_UARTE30     XX

#define UARTE20_S_BASE     0x500C6000UL   // monitor (secure)
#define UARTE30_S_BASE     0x50104000UL   // download (secure)
#define UARTE20_NS_BASE    0x400C6000UL   // monitor (non-secure)
#define UARTE30_NS_BASE    0x40104000UL   // download (non-secure)

#define DEVICE_MONITOR     1
#define DEVICE_DOWNLOAD    2

// setup for DK board
#define PORT_MONITOR       1
#define PIN_TX_MONITOR     4
#define PIN_RX_MONITOR     5
#if TZ_SECURE()
#define BASE_ADDR_MONITOR  UARTE20_S_BASE
#else
#define BASE_ADDR_MONITOR  UARTE20_NS_BASE
#endif

// setup for DK board
#define PORT_DOWNLOAD      0
#define PIN_TX_DOWNLOAD    0
#define PIN_RX_DOWNLOAD    1
#if TZ_SECURE()
#define BASE_ADDR_DOWNLOAD  UARTE30_S_BASE
#else
#define BASE_ADDR_DOWNLOAD  UARTE30_NS_BASE
#endif

static inline uintptr_t hal_uart_port_base(int device)
{
    switch (device) {
    case DEVICE_DOWNLOAD:
        return (uintptr_t) BASE_ADDR_DOWNLOAD;
    case DEVICE_MONITOR:
    default:
        return (uintptr_t) BASE_ADDR_MONITOR;
    }
}

static inline int hal_uart_port_num(int device)
{
    switch (device) {
    case DEVICE_DOWNLOAD:
        return (int) PORT_DOWNLOAD;
    case DEVICE_MONITOR:
    default:
        return (int) PORT_MONITOR;
    }
}

static inline int hal_uart_pin_num_tx(int device)
{
    switch (device) {
    case DEVICE_DOWNLOAD:
        return (int) PIN_TX_DOWNLOAD;
    case DEVICE_MONITOR:
    default:
        return (int) PIN_TX_MONITOR;
    }
}

static inline int hal_uart_pin_num_rx(int device)
{
    switch (device) {
    case DEVICE_DOWNLOAD:
        return (int) PIN_RX_DOWNLOAD;
    case DEVICE_MONITOR:
    default:
        return (int) PIN_RX_MONITOR;
    }
}

/* UART Addressing */
#define UART_PORT_NUM(n)               hal_uart_port_num((unsigned int)(n))
#define UART_PIN_NUM_TX(n)             hal_uart_pin_num_tx((unsigned int)(n))
#define UART_PIN_NUM_RX(n)             hal_uart_pin_num_rx((unsigned int)(n))
#define UART_BASE(n)                   hal_uart_port_base((unsigned int)(n))

#define UART_PSEL_TXD(n)               *((volatile uint32_t *)(UART_BASE(n) + 0x604 + 0x000))
#define UART_PSEL_CTS(n)               *((volatile uint32_t *)(UART_BASE(n) + 0x604 + 0x004))
#define UART_PSEL_RXD(n)               *((volatile uint32_t *)(UART_BASE(n) + 0x604 + 0x008))
#define UART_PSEL_RTS(n)               *((volatile uint32_t *)(UART_BASE(n) + 0x604 + 0x00C))
#define UART_ENABLE(n)                 *((volatile uint32_t *)(UART_BASE(n) + 0x500))
#define UART_BAUDRATE(n)               *((volatile uint32_t *)(UART_BASE(n) + 0x524))
#define UART_CONFIG(n)                 *((volatile uint32_t *)(UART_BASE(n) + 0x56C))

#define UART_DMA_TX_PTR(n)             *((volatile uint32_t *)(UART_BASE(n) + 0x700 + 0x038 + 0x004))
#define UART_DMA_TX_MAXCNT(n)          *((volatile uint32_t *)(UART_BASE(n) + 0x700 + 0x038 + 0x008))

#define UART_DMA_RX_PTR(n)             *((volatile uint32_t *)(UART_BASE(n) + 0x700 + 0x000 + 0x004))
#define UART_DMA_RX_MAXCNT(n)          *((volatile uint32_t *)(UART_BASE(n) + 0x700 + 0x000 + 0x008))
#define UART_DMA_RX_AMOUNT(n)          *((volatile uint32_t *)(UART_BASE(n) + 0x700 + 0x000 + 0x00C))

#define UART_EVENTS_DMA_TX_END(n)      *((volatile uint32_t *)(UART_BASE(n) + 0x14C + 0x01C + 0x000))
#define UART_EVENTS_DMA_TX_BUSERROR(n) *((volatile uint32_t *)(UART_BASE(n) + 0x14C + 0x01C + 0x008))
#define UART_EVENTS_DMA_RX_END(n)      *((volatile uint32_t *)(UART_BASE(n) + 0x14C + 0x000 + 0x000))
#define UART_EVENTS_DMA_RX_BUSERROR(n) *((volatile uint32_t *)(UART_BASE(n) + 0x14C + 0x000 + 0x008))

#define UART_TASKS_DMA_TX_START(n)     *((volatile uint32_t *)(UART_BASE(n) + 0x028 + 0x028 + 0x000))
#define UART_TASKS_DMA_TX_STOP(n)      *((volatile uint32_t *)(UART_BASE(n) + 0x028 + 0x028 + 0x004))

#define UART_TASKS_DMA_RX_START(n)     *((volatile uint32_t *)(UART_BASE(n) + 0x028 + 0x000 + 0x000))
#define UART_TASKS_DMA_RX_STOP(n)      *((volatile uint32_t *)(UART_BASE(n) + 0x028 + 0x000 + 0x004))

/* UART Settings */
#define UART_ENABLE_ENABLE_Enabled             0x8UL
#define UART_ENABLE_ENABLE_Disabled            0x0UL

#define UART_PSEL_TXD_PIN_Pos                  0UL
#define UART_PSEL_TXD_PIN_Msk                  (0x1FUL << UART_PSEL_TXD_PIN_Pos)
#define UART_PSEL_TXD_PORT_Pos                 5UL
#define UART_PSEL_TXD_PORT_Msk                 (0x7UL << UART_PSEL_TXD_PORT_Pos)

#define UART_PSEL_RXD_PIN_Pos                  0UL
#define UART_PSEL_RXD_PIN_Msk                  (0x1FUL << UART_PSEL_RXD_PIN_Pos)
#define UART_PSEL_RXD_PORT_Pos                 0x5UL
#define UART_PSEL_RXD_PORT_Msk                 (0x7UL << UART_PSEL_RXD_PORT_Pos)

#define UART_PSEL_CTS_CONNECT_Disconnected     (0x1UL << 31)
#define UART_PSEL_RTS_CONNECT_Disconnected     (0x1UL << 31)

#define UART_CONFIG_VALUE(framesize, parity, stop) \
        ((framesize << 9UL) | \
        ((parity == 'E') ? 0x00E : \
         (parity == 'O') ? 0x10E : \
                           0x000) | \
        ((stop == 2) ? 0x10 : \
                       0x00))

#define UART_TASKS_DMA_TX_START_START_Trigger  0x1UL
#define UART_TASKS_DMA_TX_STOP_STOP_Trigger    0x1UL

#define UART_TASKS_DMA_RX_START_START_Trigger  0x1UL
#define UART_TASKS_DMA_RX_STOP_STOP_Trigger    0x1UL

#define UART_BAUDRATE_VALUE(rate) (rate == 1200)    ? 0x0004F000 : \
                            (rate == 2400)    ? 0x0009D000 : \
                            (rate == 4800)    ? 0x0013B000 : \
                            (rate == 9600)    ? 0x00275000 : \
                            (rate == 14400)   ? 0x003AF000 : \
                            (rate == 19200)   ? 0x004EA000 : \
                            (rate == 28800)   ? 0x0075C000 : \
                            (rate == 31250)   ? 0x00800000 : \
                            (rate == 38400)   ? 0x009D0000 : \
                            (rate == 56000)   ? 0x00E50000 : \
                            (rate == 57600)   ? 0x00EB0000 : \
                            (rate == 76800)   ? 0x013A9000 : \
                            (rate == 115200)  ? 0x01D60000 : \
                            (rate == 230400)  ? 0x03B00000 : \
                            (rate == 250000)  ? 0x04000000 : \
                            (rate == 460800)  ? 0x07400000 : \
                            (rate == 921600)  ? 0x0F000000 : \
                            (rate == 1000000) ? 0x10000000 : 0

/* Nordic PMIC */
#define PMIC_TWIM_PORT                1
#define PMIC_TWIM_SDA_PIN             2
#define PMIC_TWIM_SCL_PIN             3
#define PMIC_TWIM_TIMEOUT             1000000UL
#define PMIC_REG_PAYLOAD_MAX          8U
#define PMIC_I2C_ADDRESS              0x6BU      // from pdf p122 (110 1011)

#define LED_PWR_CTRL_PORT             1
#define LED_PWR_CTRL_PIN              13

#define NPM1300_REG_TASK_LDSW2_SET    0x0802U
#define NPM1300_REG_TASK_LDSW2_CLR    0x0803U
#define NPM1300_REG_LDSW2_GPISEL      0x0806U
#define NPM1300_REG_LDSWCONFIG        0x0807U
#define NPM1300_REG_LDSW2LDOSEL       0x0809U
#define NPM1300_REG_GPIOMODE(n)       (0x0600U + (uint16_t)(n))
#define NPM1300_REG_GPIOPUEN(n)       (0x060AU + (uint16_t)(n))
#define NPM1300_REG_GPIOPDEN(n)       (0x060FU + (uint16_t)(n))

/* SPIM */
#if TZ_SECURE()
    #define SPIM00_BASE_DEFAULT       (0x5004A000UL)
#else
    #define SPIM00_BASE_DEFAULT       (0x4004A000UL)
#endif

#define SPI_BASE                    SPIM00_BASE_DEFAULT

#define SPI_TASKS_START             (*((volatile uint32_t *)(SPI_BASE + 0x000)))
#define SPI_TASKS_STOP              (*((volatile uint32_t *)(SPI_BASE + 0x004)))

#define SPI_EVENTS_STARTED          (*((volatile uint32_t *)(SPI_BASE + 0x100)))
#define SPI_EVENTS_STOPPED          (*((volatile uint32_t *)(SPI_BASE + 0x104)))
#define SPI_EVENTS_END              (*((volatile uint32_t *)(SPI_BASE + 0x108)))

#define SPI_EVENTS_DMA_RX_END       (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x000)))
#define SPI_EVENTS_DMA_RX_READY     (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x004)))
#define SPI_EVENTS_DMA_RX_BUSERROR  (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x008)))
#define SPI_EVENTS_DMA_TX_END       (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x01C)))
#define SPI_EVENTS_DMA_TX_READY     (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x020)))
#define SPI_EVENTS_DMA_TX_BUSERROR  (*((volatile uint32_t *)(SPI_BASE + 0x14C + 0x024)))

#define SPI_ENABLE_REG              (*((volatile uint32_t *)(SPI_BASE + 0x500)))
#define SPI_PRESCALER_REG           (*((volatile uint32_t *)(SPI_BASE + 0x52C)))
#define SPI_CONFIG_REG              (*((volatile uint32_t *)(SPI_BASE + 0x554)))
#define SPI_IFTIMING_RXDELAY        (*((volatile uint32_t *)(SPI_BASE + 0x5AC)))
#define SPI_IFTIMING_CSNDUR         (*((volatile uint32_t *)(SPI_BASE + 0x5B0)))

#define SPI_PSEL_SCK                (*((volatile uint32_t *)(SPI_BASE + 0x600)))
#define SPI_PSEL_MOSI               (*((volatile uint32_t *)(SPI_BASE + 0x604)))
#define SPI_PSEL_MISO               (*((volatile uint32_t *)(SPI_BASE + 0x608)))
#define SPI_PSEL_CSN                (*((volatile uint32_t *)(SPI_BASE + 0x610)))

#define SPI_DMA_RX_PTR              (*((volatile uint32_t *)(SPI_BASE + 0x704)))
#define SPI_DMA_RX_MAXCNT           (*((volatile uint32_t *)(SPI_BASE + 0x708)))
#define SPI_DMA_RX_LIST             (*((volatile uint32_t *)(SPI_BASE + 0x714)))

#define SPI_DMA_TX_PTR              (*((volatile uint32_t *)(SPI_BASE + 0x73C)))
#define SPI_DMA_TX_MAXCNT           (*((volatile uint32_t *)(SPI_BASE + 0x740)))
#define SPI_DMA_TX_LIST             (*((volatile uint32_t *)(SPI_BASE + 0x74C)))

#define SPIM_TASKS_START_TASKS_START_Pos     0UL
#define SPIM_TASKS_START_TASKS_START_Msk \
    (0x1UL << SPIM_TASKS_START_TASKS_START_Pos)
#define SPIM_TASKS_START_TASKS_START_Trigger 0x1UL

#define SPIM_TASKS_STOP_TASKS_STOP_Pos       0UL
#define SPIM_TASKS_STOP_TASKS_STOP_Msk \
    (0x1UL << SPIM_TASKS_STOP_TASKS_STOP_Pos)
#define SPIM_TASKS_STOP_TASKS_STOP_Trigger   0x1UL

#define SPIM_ENABLE_ENABLE_Pos               0UL
#define SPIM_ENABLE_ENABLE_Msk               (0xFUL << SPIM_ENABLE_ENABLE_Pos)
#define SPIM_ENABLE_ENABLE_Disabled          0x0UL
#define SPIM_ENABLE_ENABLE_Enabled           0x7UL

#define SPIM_CONFIG_ORDER_Pos                0UL
#define SPIM_CONFIG_ORDER_Msk                (0x1UL << SPIM_CONFIG_ORDER_Pos)
#define SPIM_CONFIG_ORDER_MsbFirst           0x0UL
#define SPIM_CONFIG_ORDER_LsbFirst           0x1UL

#define SPIM_CONFIG_CPHA_Pos                 1UL
#define SPIM_CONFIG_CPHA_Msk                 (0x1UL << SPIM_CONFIG_CPHA_Pos)
#define SPIM_CONFIG_CPHA_Leading             0x0UL
#define SPIM_CONFIG_CPHA_Trailing            0x1UL

#define SPIM_CONFIG_CPOL_Pos                 2UL
#define SPIM_CONFIG_CPOL_Msk                 (0x1UL << SPIM_CONFIG_CPOL_Pos)
#define SPIM_CONFIG_CPOL_ActiveHigh          0x0UL
#define SPIM_CONFIG_CPOL_ActiveLow           0x1UL

#define SPI_PRESCALER_DIV                    0x08UL

/* SPU - Security Processing Unit
 *
 * nRF54L has four SPU instances, one per power domain:
 *   SPU00 (MCU domain)   0x50040000  covers peripherals at 0x5004xxxx-0x5007xxxx
 *   SPU10 (RADIO domain) 0x50080000  covers peripherals at 0x5008xxxx-0x500Bxxxx
 *   SPU20 (PERI domain)  0x500C0000  covers peripherals at 0x500Cxxxx-0x500Fxxxx
 *   SPU30 (LP domain)    0x50100000  covers peripherals at 0x5010xxxx-0x5013xxxx
 */
#define SPU00_BASE  (0x50040000UL)  /* MCU domain   */
#define SPU10_BASE  (0x50080000UL)  /* RADIO domain */
#define SPU20_BASE  (0x500C0000UL)  /* PERI domain  */
#define SPU30_BASE  (0x50100000UL)  /* LP domain    */

/* Derive which SPU instance governs a peripheral */
#define SPU_BASE_FOR(paddr)     (0x50000000UL | ((paddr) & 0x00FC0000UL))
/* Derive the slave index within that SPU */
#define SPU_SLAVE_IDX(paddr)    (((paddr) & 0x0003F000UL) >> 12)

#define SPU_PERIPH_PERM(spu, n) \
    (*((volatile uint32_t *)((spu) + 0x500UL + (uint32_t)(n) * 4UL)))

#define SPU_FEATURE_GPIO_PIN(spu, g, p) \
    (*((volatile uint32_t *)((spu) + 0x800UL + (uint32_t)(g) * 0x80UL + \
                              (uint32_t)(p) * 4UL)))

#define SPU_PERIPH_PERM_SECATTR (1UL << 4)
#define SPU_FEATURE_SECATTR     (1UL << 4)

/* Non-secure RAM layout */
#define NS_RAM_BASE  (0x20010000UL)
#define NS_RAM_SIZE  (0x00030000UL)

/* MPC00 - Memory Privilege Controller */
#define MPC00_BASE  (0x50041000UL)

#define MPC_OVERRIDE_CONFIG(n) \
    (*((volatile uint32_t *)(MPC00_BASE + 0x800UL + (uint32_t)(n) * 0x20UL)))
#define MPC_OVERRIDE_STARTADDR(n) \
    (*((volatile uint32_t *)(MPC00_BASE + 0x804UL + (uint32_t)(n) * 0x20UL)))
#define MPC_OVERRIDE_ENDADDR(n) \
    (*((volatile uint32_t *)(MPC00_BASE + 0x808UL + (uint32_t)(n) * 0x20UL)))
#define MPC_OVERRIDE_PERM(n) \
    (*((volatile uint32_t *)(MPC00_BASE + 0x810UL + (uint32_t)(n) * 0x20UL)))
#define MPC_OVERRIDE_PERMMASK(n) \
    (*((volatile uint32_t *)(MPC00_BASE + 0x814UL + (uint32_t)(n) * 0x20UL)))

#define MPC_CONFIG_ENABLE   (1UL << 9)
#define MPC_PERM_READ       (1UL << 0)
#define MPC_PERM_WRITE      (1UL << 1)
#define MPC_PERM_EXECUTE    (1UL << 2)
#define MPC_PERM_SECURE     (1UL << 3)  /* 0 = NonSecure, 1 = Secure */

/* TAMPC - Tamper controller (access port protection) */
#define TAMPC_BASE                              (0x500DC000UL)
#define TAMPC_PROTECT_DOMAIN0_DBGEN_CTRL   (*(volatile uint32_t *)(TAMPC_BASE + 0x500))
#define TAMPC_PROTECT_DOMAIN0_NIDEN_CTRL   (*(volatile uint32_t *)(TAMPC_BASE + 0x508))
#define TAMPC_PROTECT_DOMAIN0_SPIDEN_CTRL  (*(volatile uint32_t *)(TAMPC_BASE + 0x510))
#define TAMPC_PROTECT_DOMAIN0_SPNIDEN_CTRL (*(volatile uint32_t *)(TAMPC_BASE + 0x518))
#define TAMPC_PROTECT_AP0_DBGEN_CTRL       (*(volatile uint32_t *)(TAMPC_BASE + 0x700))

/* KEY=0x50FA at bits[31:16], WRITEPROTECTION_Clear=0xF at bits[7:4] */
#define TAMPC_SIGNAL_CLEAR_WRITEPROTECTION  (0x50FA00F0UL)
/* KEY=0x50FA at bits[31:16], VALUE=High at bit[0], LOCK=Disabled at bit[1] */
#define TAMPC_SIGNAL_OPEN                   (0x50FA0001UL)
#define TAMPC_SIGNAL_LOCK_Msk               (1UL << 1)

/* CRACEN (Crypto Accelerator) */
#define CRACEN_BASE                  (0x50048000UL)
#define CRACENCORE_BASE              (0x51800000UL)

/* CRACEN.ENABLE register (offset 0x400) */
#define CRACEN_ENABLE                (*((volatile uint32_t *)(CRACEN_BASE + 0x400)))
#define CRACEN_ENABLE_RNG_Msk        (0x1UL << 1)

#define CRACENCORE_RNGCTRL_BASE      (CRACENCORE_BASE + 0x1000UL)

#define CRACENCORE_RNG_CONTROL       (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x000)))
#define CRACENCORE_RNG_FIFOLEVEL     (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x004)))
#define CRACENCORE_RNG_KEY(n)        (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x010 + (n)*4)))
#define CRACENCORE_RNG_STATUS        (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x030)))
#define CRACENCORE_RNG_INITWAITVAL   (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x034)))
#define CRACENCORE_RNG_SWOFFTMRVAL   (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x040)))
#define CRACENCORE_RNG_CLKDIV        (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x044)))
#define CRACENCORE_RNG_FIFO          (*((volatile uint32_t *)(CRACENCORE_RNGCTRL_BASE + 0x080)))

#define CRACENCORE_RNG_CONTROL_ENABLE_Msk         (0x1UL << 0)
#define CRACENCORE_RNG_CONTROL_SOFTRST_Msk        (0x1UL << 8)
#define CRACENCORE_RNG_CONTROL_NB128BITBLOCKS_Pos 16
#define CRACENCORE_RNG_CONTROL_NB128BITBLOCKS_Msk (0xFUL << CRACENCORE_RNG_CONTROL_NB128BITBLOCKS_Pos)

#define CRACENCORE_RNG_STATUS_STATE_Pos     1
#define CRACENCORE_RNG_STATUS_STATE_Msk     (0x7UL << CRACENCORE_RNG_STATUS_STATE_Pos)
#define CRACENCORE_RNG_STATUS_STATE_RESET   0x0UL
#define CRACENCORE_RNG_STATUS_STATE_STARTUP 0x1UL

/* TRNG configuration values recommended by Nordic */
#define CRACENCORE_RNG_INITWAITVAL_DEFAULT  512UL
#define CRACENCORE_RNG_NB128BITBLOCKS_DEFAULT 4UL

#endif /* _HAL_NRF54L_H_ */
