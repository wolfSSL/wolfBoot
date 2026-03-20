/* max32666.h
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
 * Hardware register definitions for Analog Devices MAX32665/MAX32666
 * MAX32666: Dual Cortex-M4 @ 96 MHz, 1MB Flash, 560KB SRAM, BLE 5
 *
 * Register offsets verified against MSDK:
 *   ~/GitHub/msdk/Libraries/CMSIS/Device/Maxim/MAX32665/Include/
 */

#ifndef MAX32666_H
#define MAX32666_H

#include <stdint.h>

/* ============== Memory Map ============== */
#define FLASH_BASE              0x10000000UL
#define FLASH_SIZE              (1024 * 1024)       /* 1MB */
#define FLASH_PAGE_SIZE         8192                /* 8KB page erase */
#define FLASH_WRITE_SIZE        16                  /* 128-bit write unit */

#define SRAM_BASE               0x20000000UL
#define SRAM_SIZE               (560 * 1024)        /* 560KB */

/* ============== GCR - Global Control Registers ============== */
/* MSDK: gcr_regs.h */
#define GCR_BASE                0x40000000UL
#define GCR_SCON                (*(volatile uint32_t *)(GCR_BASE + 0x00UL))
#define GCR_RSTR0               (*(volatile uint32_t *)(GCR_BASE + 0x04UL))
#define GCR_CLKCN               (*(volatile uint32_t *)(GCR_BASE + 0x08UL))
#define GCR_PM                  (*(volatile uint32_t *)(GCR_BASE + 0x0CUL))
#define GCR_PCKDIV              (*(volatile uint32_t *)(GCR_BASE + 0x18UL))
#define GCR_PERCKCN0            (*(volatile uint32_t *)(GCR_BASE + 0x24UL))
#define GCR_MEMCKCN             (*(volatile uint32_t *)(GCR_BASE + 0x28UL))
#define GCR_MEMZCN              (*(volatile uint32_t *)(GCR_BASE + 0x2CUL))
#define GCR_SYSST               (*(volatile uint32_t *)(GCR_BASE + 0x40UL))
#define GCR_RSTR1               (*(volatile uint32_t *)(GCR_BASE + 0x44UL))
#define GCR_PERCKCN1            (*(volatile uint32_t *)(GCR_BASE + 0x48UL))
#define GCR_EVENTEN             (*(volatile uint32_t *)(GCR_BASE + 0x4CUL))
#define GCR_REVISION            (*(volatile uint32_t *)(GCR_BASE + 0x50UL))
#define GCR_SYSSIE              (*(volatile uint32_t *)(GCR_BASE + 0x54UL))

/* GCR_CLKCN fields (MSDK: CLKCN register) */
#define GCR_CLKCN_PSC_SHIFT             6           /* Prescaler: bits [8:6] */
#define GCR_CLKCN_PSC_MASK              (0x7UL << 6)
#define GCR_CLKCN_CLKSEL_SHIFT          9           /* Clock source: bits [11:9] */
#define GCR_CLKCN_CLKSEL_MASK           (0x7UL << 9)
#define GCR_CLKCN_CKRDY                 (1UL << 13) /* Clock ready */
#define GCR_CLKCN_X32M_EN               (1UL << 16) /* 32 MHz XTAL enable */
#define GCR_CLKCN_X32K_EN               (1UL << 17) /* 32.768 kHz XTAL enable */
#define GCR_CLKCN_HIRC_EN               (1UL << 18) /* 60 MHz HIRC enable */
#define GCR_CLKCN_HIRC96M_EN            (1UL << 19) /* 96 MHz HIRC enable */
#define GCR_CLKCN_HIRC8M_EN             (1UL << 20) /* 7.3728 MHz HIRC8M enable */
#define GCR_CLKCN_HIRC8M_VS             (1UL << 21) /* HIRC8M valid status */
#define GCR_CLKCN_X32M_RDY              (1UL << 24) /* 32 MHz XTAL ready */
#define GCR_CLKCN_X32K_RDY              (1UL << 25) /* 32.768 kHz XTAL ready */
#define GCR_CLKCN_HIRC_RDY              (1UL << 26) /* 60 MHz HIRC ready */
#define GCR_CLKCN_HIRC96M_RDY           (1UL << 27) /* 96 MHz HIRC ready */
#define GCR_CLKCN_HIRC8M_RDY            (1UL << 28) /* 7.3728 MHz HIRC8M ready */

/* Clock source selection values for CLKSEL field */
#define GCR_CLKCN_CLKSEL_HIRC           (0UL << 9)  /* 60 MHz HIRC */
#define GCR_CLKCN_CLKSEL_XTAL32M        (2UL << 9)  /* 32 MHz XTAL */
#define GCR_CLKCN_CLKSEL_LIRC8          (3UL << 9)  /* 8 kHz LIRC */
#define GCR_CLKCN_CLKSEL_HIRC96         (4UL << 9)  /* 96 MHz HIRC */
#define GCR_CLKCN_CLKSEL_HIRC8          (5UL << 9)  /* 7.3728 MHz HIRC8M */
#define GCR_CLKCN_CLKSEL_XTAL32K        (6UL << 9)  /* 32.768 kHz XTAL */

/* GCR_PERCKCN0 bits (1 = clock disabled) - MSDK: PERCKCN0 register */
#define GCR_PERCKCN0_GPIO0D    (1UL << 0)
#define GCR_PERCKCN0_GPIO1D    (1UL << 1)
#define GCR_PERCKCN0_USBD      (1UL << 3)
#define GCR_PERCKCN0_DMAD      (1UL << 5)
#define GCR_PERCKCN0_SPI1D     (1UL << 6)
#define GCR_PERCKCN0_SPI2D     (1UL << 7)
#define GCR_PERCKCN0_UART0D    (1UL << 9)
#define GCR_PERCKCN0_UART1D    (1UL << 10)
#define GCR_PERCKCN0_I2C0D     (1UL << 13)
#define GCR_PERCKCN0_CRYPTOD   (1UL << 14)  /* TPU/Crypto engine */
#define GCR_PERCKCN0_TIMER0D   (1UL << 15)
#define GCR_PERCKCN0_TIMER1D   (1UL << 16)
#define GCR_PERCKCN0_TIMER2D   (1UL << 17)
#define GCR_PERCKCN0_I2C1D     (1UL << 28)

/* GCR_PERCKCN1 bits */
#define GCR_PERCKCN1_UART2D    (1UL << 1)
#define GCR_PERCKCN1_FLCD      (1UL << 3)
#define GCR_PERCKCN1_ICCD      (1UL << 11)
#define GCR_PERCKCN1_WDT0D     (1UL << 27)
#define GCR_PERCKCN1_WDT1D     (1UL << 28)

/* ============== FLC - Flash Controller ============== */
/* Bank 0: 0x10000000 - 0x1007FFFF (512KB) */
/* Bank 1: 0x10080000 - 0x100FFFFF (512KB) */
/* MSDK: flc_regs.h */
#define FLC0_BASE               0x40029000UL
#define FLC1_BASE               0x40029400UL

/* FLC register offsets */
#define FLC_ADDR_OFF            0x00UL
#define FLC_CLKDIV_OFF          0x04UL
#define FLC_CN_OFF              0x08UL
#define FLC_INTR_OFF            0x024UL
#define FLC_DATA_OFF            0x030UL     /* DATA[0..3] = 128-bit write data */
#define FLC_ACNTL_OFF           0x040UL

/* FLC0 registers */
#define FLC0_ADDR               (*(volatile uint32_t *)(FLC0_BASE + FLC_ADDR_OFF))
#define FLC0_CLKDIV             (*(volatile uint32_t *)(FLC0_BASE + FLC_CLKDIV_OFF))
#define FLC0_CN                 (*(volatile uint32_t *)(FLC0_BASE + FLC_CN_OFF))
#define FLC0_INTR               (*(volatile uint32_t *)(FLC0_BASE + FLC_INTR_OFF))
#define FLC0_ACNTL              (*(volatile uint32_t *)(FLC0_BASE + FLC_ACNTL_OFF))

/* FLC1 registers */
#define FLC1_ADDR               (*(volatile uint32_t *)(FLC1_BASE + FLC_ADDR_OFF))
#define FLC1_CLKDIV             (*(volatile uint32_t *)(FLC1_BASE + FLC_CLKDIV_OFF))
#define FLC1_CN                 (*(volatile uint32_t *)(FLC1_BASE + FLC_CN_OFF))
#define FLC1_INTR               (*(volatile uint32_t *)(FLC1_BASE + FLC_INTR_OFF))
#define FLC1_ACNTL              (*(volatile uint32_t *)(FLC1_BASE + FLC_ACNTL_OFF))

/* FLC_CN (Control) register bits */
#define FLC_CN_WR               (1UL << 0)          /* Start write */
#define FLC_CN_ME               (1UL << 1)          /* Mass erase */
#define FLC_CN_PGE              (1UL << 2)          /* Page erase */
#define FLC_CN_ERASE_CODE_SHIFT 8                   /* Erase code: bits [15:8] */
#define FLC_CN_ERASE_CODE_MASK  (0xFFUL << 8)
#define FLC_CN_ERASE_CODE_PGE   (0x55UL << 8)      /* Page erase code */
#define FLC_CN_ERASE_CODE_ME    (0xAAUL << 8)      /* Mass erase code */
#define FLC_CN_PEND             (1UL << 24)         /* Operation pending */
#define FLC_CN_UNLOCK_SHIFT     28                  /* Unlock: bits [31:28] */
#define FLC_CN_UNLOCK_MASK      (0xFUL << 28)
#define FLC_CN_UNLOCK_UNLOCKED  (0x2UL << 28)      /* Unlock value */
#define FLC_CN_UNLOCK_LOCKED    (0x3UL << 28)      /* Lock value */

/* FLC_INTR register bits */
#define FLC_INTR_DONE           (1UL << 0)          /* Operation done */
#define FLC_INTR_AF             (1UL << 1)          /* Access fault */
#define FLC_INTR_DONEIE         (1UL << 8)          /* Done interrupt enable */
#define FLC_INTR_AFIE           (1UL << 9)          /* Fault interrupt enable */

/* FLC unlock sequence (ACNTL register) */
#define FLC_ACNTL_UNLOCK_KEY1   0x3A7F5200UL
#define FLC_ACNTL_UNLOCK_KEY2   0xA1E34F20UL

/* FLC clock divider: flash requires 1 MHz clock
 * CLKDIV = SystemClock / 1MHz
 * At 96 MHz: CLKDIV = 96
 */
#define FLC_CLKDIV_VALUE        96

/* ============== ICC - Internal Cache Controller ============== */
/* MSDK: icc_regs.h */
#define ICC0_BASE               0x4002A000UL
#define ICC0_INFO               (*(volatile uint32_t *)(ICC0_BASE + 0x0000UL))
#define ICC0_SZ                 (*(volatile uint32_t *)(ICC0_BASE + 0x0004UL))
#define ICC0_CTRL               (*(volatile uint32_t *)(ICC0_BASE + 0x0100UL))
#define ICC0_INVALIDATE         (*(volatile uint32_t *)(ICC0_BASE + 0x0700UL))

#define ICC_CTRL_EN             (1UL << 0)          /* Cache enable */
#define ICC_CTRL_RDY            (1UL << 16)         /* Cache ready */

/* ============== WDT - Watchdog Timer ============== */
/* MSDK: wdt_regs.h */
#define WDT0_BASE               0x40003000UL
#define WDT0_CTRL               (*(volatile uint32_t *)(WDT0_BASE + 0x00UL))
#define WDT0_RST                (*(volatile uint32_t *)(WDT0_BASE + 0x04UL))

/* WDT_CTRL bits */
#define WDT_CTRL_INT_PERIOD_SHIFT   0
#define WDT_CTRL_RST_PERIOD_SHIFT   4
#define WDT_CTRL_EN                 (1UL << 8)
#define WDT_CTRL_INT_FLAG           (1UL << 9)
#define WDT_CTRL_INT_EN             (1UL << 10)
#define WDT_CTRL_RST_EN             (1UL << 11)
#define WDT_CTRL_RST_FLAG           (1UL << 31)

/* WDT reset sequence */
#define WDT_RST_SEQ1            0x00A5
#define WDT_RST_SEQ2            0x005A

/* ============== UART ============== */
/* MSDK: uart_regs.h */
#define UART0_BASE              0x40042000UL
#define UART1_BASE              0x40043000UL
#define UART2_BASE              0x40044000UL

/* UART register offsets (verified against MSDK) */
#define UART_CTRL_OFF           0x00UL
#define UART_THRESH_CTRL_OFF    0x04UL
#define UART_STATUS_OFF         0x08UL
#define UART_INT_EN_OFF         0x0CUL
#define UART_INT_FL_OFF         0x10UL
#define UART_BAUD0_OFF          0x14UL
#define UART_BAUD1_OFF          0x18UL
#define UART_FIFO_OFF           0x1CUL
#define UART_DMA_OFF            0x20UL
#define UART_TX_FIFO_OFF        0x24UL

/* UART0 registers */
#define UART0_CTRL              (*(volatile uint32_t *)(UART0_BASE + UART_CTRL_OFF))
#define UART0_STATUS            (*(volatile uint32_t *)(UART0_BASE + UART_STATUS_OFF))
#define UART0_INT_EN            (*(volatile uint32_t *)(UART0_BASE + UART_INT_EN_OFF))
#define UART0_INT_FL            (*(volatile uint32_t *)(UART0_BASE + UART_INT_FL_OFF))
#define UART0_BAUD0             (*(volatile uint32_t *)(UART0_BASE + UART_BAUD0_OFF))
#define UART0_BAUD1             (*(volatile uint32_t *)(UART0_BASE + UART_BAUD1_OFF))
#define UART0_FIFO              (*(volatile uint32_t *)(UART0_BASE + UART_FIFO_OFF))

/* UART1 registers */
#define UART1_CTRL              (*(volatile uint32_t *)(UART1_BASE + UART_CTRL_OFF))
#define UART1_STATUS            (*(volatile uint32_t *)(UART1_BASE + UART_STATUS_OFF))
#define UART1_INT_EN            (*(volatile uint32_t *)(UART1_BASE + UART_INT_EN_OFF))
#define UART1_INT_FL            (*(volatile uint32_t *)(UART1_BASE + UART_INT_FL_OFF))
#define UART1_BAUD0             (*(volatile uint32_t *)(UART1_BASE + UART_BAUD0_OFF))
#define UART1_BAUD1             (*(volatile uint32_t *)(UART1_BASE + UART_BAUD1_OFF))
#define UART1_FIFO              (*(volatile uint32_t *)(UART1_BASE + UART_FIFO_OFF))

/* UART_CTRL fields */
#define UART_CTRL_ENABLE        (1UL << 0)
#define UART_CTRL_PARITY_EN     (1UL << 1)
#define UART_CTRL_TX_FLUSH      (1UL << 5)
#define UART_CTRL_RX_FLUSH      (1UL << 6)
#define UART_CTRL_CHAR_SZ_SHIFT 8                    /* Character size: bits [9:8] */
#define UART_CTRL_CHAR_SZ_8     (3UL << 8)           /* 8-bit characters */
#define UART_CTRL_STOPBITS      (1UL << 10)          /* 1=2 stop bits */
#define UART_CTRL_FLOW_CTRL     (1UL << 11)
#define UART_CTRL_CLKSEL        (1UL << 15)          /* 1=HIRC8M (7.3728MHz) as baud clock */

/* UART_STATUS fields */
#define UART_STATUS_TX_BUSY     (1UL << 0)
#define UART_STATUS_RX_BUSY     (1UL << 1)
#define UART_STATUS_RX_EMPTY    (1UL << 4)           /* RX FIFO empty */
#define UART_STATUS_RX_FULL     (1UL << 5)           /* RX FIFO full */
#define UART_STATUS_TX_EMPTY    (1UL << 6)           /* TX FIFO empty */
#define UART_STATUS_TX_FULL     (1UL << 7)           /* TX FIFO full */

/* UART_BAUD0 fields */
#define UART_BAUD0_IBAUD_SHIFT  0                    /* Integer baud divisor: bits [11:0] */
#define UART_BAUD0_IBAUD_MASK   (0xFFFUL << 0)
#define UART_BAUD0_FACTOR_SHIFT 16                   /* Baud factor: bits [17:16] */
#define UART_BAUD0_FACTOR_MASK  (0x3UL << 16)
#define UART_BAUD0_FACTOR_128   (0UL << 16)          /* Oversampling 128 */
#define UART_BAUD0_FACTOR_64    (1UL << 16)          /* Oversampling 64 */
#define UART_BAUD0_FACTOR_32    (2UL << 16)          /* Oversampling 32 */
#define UART_BAUD0_FACTOR_16    (3UL << 16)          /* Oversampling 16 */

/* Baud rate: baud = uart_clk / (IBAUD * (128 >> FACTOR))
 * Using HIRC8M (7.3728 MHz) for exact standard baud rates.
 * For 115200 with FACTOR=2 (div 32): IBAUD = 7372800 / (115200 * 32) = 2
 * For 115200 with FACTOR=3 (div 16): IBAUD = 7372800 / (115200 * 16) = 4
 */
#define HIRC8M_FREQ             7372800UL
#define HIRC96M_FREQ            96000000UL
#define HIRC_FREQ               60000000UL

#ifndef UART_BAUDRATE
#define UART_BAUDRATE           115200
#endif

/* Select debug UART instance
 * MAX32666FTHR uses UART1 MAP_B (P1.12 RX, P1.13 TX) through PICO adapter
 */
#ifndef DEBUG_UART_NUM
#define DEBUG_UART_NUM          1
#endif

#if DEBUG_UART_NUM == 0
    #define DEBUG_UART_CTRL     UART0_CTRL
    #define DEBUG_UART_STATUS   UART0_STATUS
    #define DEBUG_UART_INT_EN   UART0_INT_EN
    #define DEBUG_UART_INT_FL   UART0_INT_FL
    #define DEBUG_UART_BAUD0    UART0_BAUD0
    #define DEBUG_UART_BAUD1    UART0_BAUD1
    #define DEBUG_UART_FIFO     UART0_FIFO
    #define DEBUG_UART_PCLKDIS  GCR_PERCKCN0_UART0D
#elif DEBUG_UART_NUM == 1
    #define DEBUG_UART_CTRL     UART1_CTRL
    #define DEBUG_UART_STATUS   UART1_STATUS
    #define DEBUG_UART_INT_EN   UART1_INT_EN
    #define DEBUG_UART_INT_FL   UART1_INT_FL
    #define DEBUG_UART_BAUD0    UART1_BAUD0
    #define DEBUG_UART_BAUD1    UART1_BAUD1
    #define DEBUG_UART_FIFO     UART1_FIFO
    #define DEBUG_UART_PCLKDIS  GCR_PERCKCN0_UART1D
#endif

/* ============== GPIO ============== */
/* MSDK: gpio_regs.h */
#define GPIO0_BASE              0x40008000UL
#define GPIO1_BASE              0x40009000UL

/* GPIO register offsets */
#define GPIO_EN0_OFF            0x00UL    /* GPIO function enable (1=GPIO, 0=AF) */
#define GPIO_EN0_SET_OFF        0x04UL
#define GPIO_EN0_CLR_OFF        0x08UL
#define GPIO_OUT_EN_OFF         0x0CUL    /* Output enable */
#define GPIO_OUT_EN_SET_OFF     0x10UL
#define GPIO_OUT_EN_CLR_OFF     0x14UL
#define GPIO_OUT_OFF            0x18UL    /* Output data */
#define GPIO_OUT_SET_OFF        0x1CUL
#define GPIO_OUT_CLR_OFF        0x20UL
#define GPIO_IN_OFF             0x24UL    /* Input data */
#define GPIO_EN1_OFF            0x68UL    /* Alternate function select */
#define GPIO_EN1_SET_OFF        0x6CUL
#define GPIO_EN1_CLR_OFF        0x70UL
#define GPIO_EN2_OFF            0x74UL
#define GPIO_EN2_SET_OFF        0x78UL
#define GPIO_EN2_CLR_OFF        0x7CUL

/* GPIO0 registers */
#define GPIO0_EN0               (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN0_OFF))
#define GPIO0_EN0_SET           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN0_SET_OFF))
#define GPIO0_EN0_CLR           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN0_CLR_OFF))
#define GPIO0_EN1               (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN1_OFF))
#define GPIO0_EN1_SET           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN1_SET_OFF))
#define GPIO0_EN1_CLR           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_EN1_CLR_OFF))
#define GPIO0_OUT_EN            (*(volatile uint32_t *)(GPIO0_BASE + GPIO_OUT_EN_OFF))
#define GPIO0_OUT_SET           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_OUT_SET_OFF))
#define GPIO0_OUT_CLR           (*(volatile uint32_t *)(GPIO0_BASE + GPIO_OUT_CLR_OFF))
#define GPIO0_IN                (*(volatile uint32_t *)(GPIO0_BASE + GPIO_IN_OFF))

/* GPIO1 registers */
#define GPIO1_EN0               (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN0_OFF))
#define GPIO1_EN0_SET           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN0_SET_OFF))
#define GPIO1_EN0_CLR           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN0_CLR_OFF))
#define GPIO1_EN1               (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN1_OFF))
#define GPIO1_EN1_SET           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN1_SET_OFF))
#define GPIO1_EN1_CLR           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_EN1_CLR_OFF))
#define GPIO1_OUT_EN            (*(volatile uint32_t *)(GPIO1_BASE + GPIO_OUT_EN_OFF))
#define GPIO1_OUT_SET           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_OUT_SET_OFF))
#define GPIO1_OUT_CLR           (*(volatile uint32_t *)(GPIO1_BASE + GPIO_OUT_CLR_OFF))
#define GPIO1_IN                (*(volatile uint32_t *)(GPIO1_BASE + GPIO_IN_OFF))

/* Pin definitions for MAX32666FTHR:
 * UART1 MAP_B: P1.12 = RX, P1.13 = TX (AF3: EN0=1, EN1=1)
 * UART0 MAP_A: P0.0 = TX, P0.1 = RX (AF1: EN0=0, EN1=0, default)
 */
#define UART1B_TX_PIN           (1UL << 13) /* P1.13 */
#define UART1B_RX_PIN           (1UL << 12) /* P1.12 */
#define UART1B_PINS             (UART1B_TX_PIN | UART1B_RX_PIN)

/* ============== ARM Cortex-M4 System Registers ============== */
#define SCB_BASE                0xE000ED00UL
#define SCB_CPUID               (*(volatile uint32_t *)(SCB_BASE + 0x00UL))
#define SCB_ICSR                (*(volatile uint32_t *)(SCB_BASE + 0x04UL))
#define SCB_VTOR                (*(volatile uint32_t *)(SCB_BASE + 0x08UL))
#define SCB_AIRCR               (*(volatile uint32_t *)(SCB_BASE + 0x0CUL))

#define AIRCR_VECTKEY           (0x05FAUL << 16)
#define AIRCR_SYSRESETREQ       (1UL << 2)

/* ============== Function Declarations ============== */

#ifdef DEBUG_UART
void uart_init(void);
void uart_write(const char* buf, unsigned int sz);
int uart_read(char* c);
#endif

#endif /* MAX32666_H */
