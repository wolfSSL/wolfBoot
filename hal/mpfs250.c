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
#include "disk.h"

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

/* TODO: Add support for reading uSD card with GPT (Global Partition Table) */
/* The partition ID's are determined using BOOT_PART_A and BOOT_PART_B. */
int disk_open(int drv)
{
    (void)drv;
    return 0;
}
int disk_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    (void)drv;
    (void)part;
    (void)off;
    (void)sz;
    (void)buf;
    return 0;
}
int disk_write(int drv, int part, uint64_t off, uint64_t sz, const uint8_t *buf)
{
    (void)drv;
    (void)part;
    (void)off;
    (void)sz;
    (void)buf;
    return 0;
}
int disk_find_partition_by_label(int drv, const char *label)
{
    (void)drv;
    (void)label;
    return 0;
}

#ifdef DEBUG_UART

#ifndef DEBUG_UART_BASE
#define DEBUG_UART_BASE MSS_UART1_LO_BASE
#endif

/* Configure baud divisors with fractional baud rate support.
 *
 * UART baud rate divisor formula: divisor = PCLK / (baudrate * 16)
 *
 * To support fractional divisors (6-bit, 0-63), we scale up the calculation:
 *   divisor_x128 = (PCLK * 8) / baudrate  (128x scaled for rounding precision)
 *   divisor_x64  = divisor_x128 / 2       (64x scaled for 6-bit fractional)
 *   integer_div  = divisor_x64 / 64       (integer portion of divisor)
 *   frac_div     = divisor_x64 % 64       (fractional portion, 0-63)
 *
 * The fractional part is then adjusted using the x128 value for rounding.
 */
static void uart_config_clk(uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;

    /* Scale up for precision: (PCLK * 128) / (baudrate * 16) */
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;

    /* Extract integer and fractional parts */
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);

    /* Apply rounding correction from x128 calculation */
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);

    if (div_int > (uint32_t)UINT16_MAX)
        return;

    /* Write 16-bit divisor: set DLAB, write high/low bytes, clear DLAB */
    MMUART_LCR(DEBUG_UART_BASE) |= DLAB_MASK;
    MMUART_DMR(DEBUG_UART_BASE) = (uint8_t)(div_int >> 8);
    MMUART_DLR(DEBUG_UART_BASE) = (uint8_t)div_int;
    MMUART_LCR(DEBUG_UART_BASE) &= ~DLAB_MASK;

    /* Enable fractional divisor if integer divisor > 1 */
    if (div_int > 1u) {
        MMUART_MM0(DEBUG_UART_BASE) |= EFBR_MASK;
        MMUART_DFR(DEBUG_UART_BASE) = (uint8_t)div_frac;
    }
    else {
        MMUART_MM0(DEBUG_UART_BASE) &= ~EFBR_MASK;
    }
}

void uart_init(void)
{
    /* Disable special modes: LIN, IrDA, SmartCard */
    MMUART_MM0(DEBUG_UART_BASE) &= ~ELIN_MASK;
    MMUART_MM1(DEBUG_UART_BASE) &= ~EIRD_MASK;
    MMUART_MM2(DEBUG_UART_BASE) &= ~EERR_MASK;

    /* Disable interrupts */
    MMUART_IER(DEBUG_UART_BASE) = 0u;

    /* Reset and configure FIFOs, enable RXRDYN/TXRDYN pins */
    MMUART_FCR(DEBUG_UART_BASE) = 0u;
    MMUART_FCR(DEBUG_UART_BASE) |= CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK;
    MMUART_FCR(DEBUG_UART_BASE) |= RXRDY_TXRDYN_EN_MASK;

    /* Disable loopback (local and remote) */
    MMUART_MCR(DEBUG_UART_BASE) &= ~(LOOP_MASK | RLOOP_MASK);

    /* Set LSB-first for TX/RX */
    MMUART_MM1(DEBUG_UART_BASE) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);

    /* Disable AFM, single wire mode */
    MMUART_MM2(DEBUG_UART_BASE) &= ~(EAFM_MASK | ESWM_MASK);

    /* Disable TX time guard, RX timeout, fractional baud */
    MMUART_MM0(DEBUG_UART_BASE) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);

    /* Clear timing registers */
    MMUART_GFR(DEBUG_UART_BASE) = 0u;
    MMUART_TTG(DEBUG_UART_BASE) = 0u;
    MMUART_RTO(DEBUG_UART_BASE) = 0u;

    /* Configure baud rate (115200) */
    uart_config_clk(115200);

    /* Set line config: 8N1 */
    MMUART_LCR(DEBUG_UART_BASE) = MSS_UART_DATA_8_BITS |
                                  MSS_UART_NO_PARITY |
                                  MSS_UART_ONE_STOP_BIT;
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
