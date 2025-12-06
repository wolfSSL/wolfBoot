/* mpfs250.c
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

/* Microchip PolarFire SoC MPFS250T HAL for wolfBoot */
/* Supports:
 *   RISC-V 64-bit architecture
 *   External flash operations
 *   UART communication
 *   System initialization
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <target.h>

#include "mpfs250.h"
#include "image.h"
#ifndef ARCH_RISCV64
#   error "wolfBoot mpfs250 HAL: wrong architecture selected. Please compile with ARCH=RISCV64."
#endif

#include "printf.h"
#include "loader.h"

/* Placeholder functions - to be implemented */
void hal_init(void)
{

}

void hal_prepare_boot(void)
{

}

void RAMFUNCTION hal_flash_unlock(void)
{

}

void RAMFUNCTION hal_flash_lock(void)
{

}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

#ifdef EXT_FLASH
/* External flash support */
void ext_flash_lock(void)
{
    /* TODO: Lock external flash */
}

void ext_flash_unlock(void)
{
    /* TODO: Unlock external flash */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    /* TODO: Write to external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    /* TODO: Read from external flash */
    (void)address;
    (void)data;
    (void)len;
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    /* TODO: Erase external flash sectors */
    (void)address;
    (void)len;
    return 0;
}
#endif /* EXT_FLASH */

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif

#ifdef DEBUG_UART

#ifndef DEBUG_UART_BASE
#define DEBUG_UART_BASE MSS_UART1_LO_BASE
#endif

/***************************************************************************//**
 * Configure baud divisors using fractional baud rate if possible.
 */
static void config_baud_divisors(uint32_t baudrate)
{
    uint32_t baud_value;
    uint32_t baud_value_by_64;
    uint32_t baud_value_by_128;
    uint32_t fractional_baud_value;
    uint64_t pclk_freq;

    /* Use the system clock value from hw_platform.h */
    pclk_freq = MSS_APB_AHB_CLK;

    /*
    * Compute baud value based on requested baud rate and PCLK frequency.
    * The baud value is computed using the following equation:
    *      baud_value = PCLK_Frequency / (baud_rate * 16)
    */
    baud_value_by_128 = (uint32_t)((8UL * pclk_freq) / baudrate);
    baud_value_by_64 = baud_value_by_128 / 2u;
    baud_value = baud_value_by_64 / 64u;
    fractional_baud_value = baud_value_by_64 - (baud_value * 64u);
    fractional_baud_value += (baud_value_by_128 - (baud_value * 128u))
                            - (fractional_baud_value * 2u);

    if (baud_value <= (uint32_t)UINT16_MAX) {
        if (baud_value > 1u) {
            /*
             * Use Fractional baud rate divisors
             */
            /* set divisor latch */
            MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;

            /* baud value */
            MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(baud_value >> 8);
            MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)baud_value;

            /* reset divisor latch */
            MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

            /* Enable Fractional baud rate */
            MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;

            /* Load the fractional baud rate register */
            MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)fractional_baud_value;
        }
        else {
            /*
            * Do NOT use Fractional baud rate divisors.
            */
            /* set divisor latch */
            MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;

            /* baud value */
            MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(baud_value >> 8u);
            MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)baud_value;

            /* reset divisor latch */
            MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

            /* Disable Fractional baud rate */
            MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
        }
    }
}

void uart_init(void)
{
    uint32_t baud_rate = 115200;
    uint32_t line_config = (
        MSS_UART_DATA_8_BITS |
        MSS_UART_NO_PARITY |
        MSS_UART_ONE_STOP_BIT
    );

    /* Disable LIN mode */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ELIN_MASK;

    /* Disable IrDA mode */
    MMUART_MM1(DEBUG_UART_BASE) &= ~EIRD_MASK;

    /* Disable SmartCard Mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~EERR_MASK;

    /* disable interrupts */
    MMUART_IER(DEBUG_UART_BASE) = 0u;

    /* FIFO configuration */
    MMUART_FCR(DEBUG_UART_BASE) = 0u;

    /* clear receiver FIFO */
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_RX_FIFO_MASK;

    /* clear transmitter FIFO */
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_TX_FIFO_MASK;

    /* set default READY mode : Mode 0*/
    /* enable RXRDYN and TXRDYN pins. The earlier FCR write to set the TX FIFO
     * trigger level inadvertently disabled the FCR_RXRDY_TXRDYN_EN bit. */
    MMUART_FCR(DEBUG_UART_BASE) |= RXRDY_TXRDYN_EN_MASK;

    /* disable loopback : local * remote */
    MMUART_MCR(DEBUG_UART_BASE) &= ~LOOP_MASK;

    MMUART_MCR(DEBUG_UART_BASE) &= ~RLOOP_MASK;

    /* set default TX endian */
    MMUART_MM1(DEBUG_UART_BASE) &= ~E_MSB_TX_MASK;

    /* set default RX endian */
    MMUART_MM1(DEBUG_UART_BASE) &= ~E_MSB_RX_MASK;

    /* default AFM : disabled */
    MMUART_MM2(DEBUG_UART_BASE) &= ~EAFM_MASK;

    /* disable TX time guard */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ETTG_MASK;

    /* set default RX timeout */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ERTO_MASK;

    /* disable fractional baud-rate */
    MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;

    /* disable single wire mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~ESWM_MASK;

    /* set filter to minimum value */
    MMUART_GFR(DEBUG_UART_BASE) = 0u;

    /* set default TX time guard */
    MMUART_TTG(DEBUG_UART_BASE) = 0u;

    /* set default RX timeout */
    MMUART_RTO(DEBUG_UART_BASE) = 0u;

    /*
     * Configure baud rate divisors. This uses the fractional baud rate divisor
     * where possible to provide the most accurate baud rat possible.
     */
    config_baud_divisors(baud_rate);

    /* set the line control register (bit length, stop bits, parity) */
    MMUART_LCR(DEBUG_UART_BASE) = line_config;

}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}
#endif /* DEBUG_UART */
