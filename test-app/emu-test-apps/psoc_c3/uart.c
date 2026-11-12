/* uart.c
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

#include <stdint.h>
#include "emu_app.h"
#include "hal/psoc_c3.h"

/* The kit console: SCB3 on P6.2/P6.3, the same block, pins and clock
 * destination hal/psoc_c3.c drives. The app runs Secure, so the secure
 * peripheral alias from the HAL header applies. */
#define UART_SCB_BASE       PSOC_C3_SCB_BASE(3)
#define UART_PORT           6
#define UART_RX_PIN         2
#define UART_TX_PIN         3
#define UART_HSIOM_SEL      16
#define UART_PCLK_GR        4
#define UART_PCLK_IDX       6
#define UART_PCLK_DIV       1
#define UART_PCLK_HZ        48000000UL
#define UART_BAUD           115200UL
#define UART_OVS            8UL
#define UART_TX_POLL_LIMIT  100000u

static int uart_ready;

/* The HAL source is not linked into the app, so its ungate, pin claim and
 * divider start are repeated here with the same registers. */
static void peri_init(void)
{
    int g;

    for (g = 1; g < PERI_GR_COUNT; g++) {
        PERI_GR_SL_CTL2(g) = 0;
        PERI_GR_SL_CTL(g) = 0xFFFFFFFFUL;
    }
    DSB();
}

static void pin_setup(int port, int pin, uint32_t sel, uint32_t drive)
{
    uint32_t v;

    HSIOM_SECURE_PRT_NSMASK(port) &= ~(1UL << pin);
    v = HSIOM_PORT_SEL(port, pin);
    v &= ~(0xFFUL << HSIOM_SEL_SHIFT(pin));
    v |= (sel << HSIOM_SEL_SHIFT(pin));
    HSIOM_PORT_SEL(port, pin) = v;
    v = GPIO_PRT_CFG(port);
    v &= ~(GPIO_CFG_DM_MASK << ((uint32_t)pin * 4U));
    v |= (drive << ((uint32_t)pin * 4U));
    GPIO_PRT_CFG(port) = v;
}

static int pclk_setup(void)
{
    uint32_t timeout = PSOC_C3_PERI_TIMEOUT;
    uint32_t int_div;
    uint32_t frac_div;

    psoc_c3_div16_5(UART_PCLK_HZ, UART_BAUD * UART_OVS, &int_div, &frac_div);
    PERI_PCLK_DIV_16_5_CTL(UART_PCLK_GR, UART_PCLK_DIV) =
        (int_div << PERI_PCLK_DIV_16_5_INT_Pos) |
        (frac_div << PERI_PCLK_DIV_16_5_FRAC_Pos);
    PERI_PCLK_CLOCK_CTL(UART_PCLK_GR, UART_PCLK_IDX) =
        (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_CLOCK_CTL_TYPE_Pos) | UART_PCLK_DIV;
    PERI_PCLK_DIV_CMD(UART_PCLK_GR) = PERI_PCLK_DIV_CMD_ENABLE |
        PERI_PCLK_DIV_CMD_PA_NONE |
        (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_DIV_CMD_TYPE_Pos) | UART_PCLK_DIV;
    while (timeout > 0) {
        if ((PERI_PCLK_DIV_16_5_CTL(UART_PCLK_GR, UART_PCLK_DIV) &
             PERI_PCLK_DIV_16_5_EN) != 0) {
            return 0;
        }
        timeout--;
    }
    return -1;
}

void emu_uart_init(void)
{
    peri_init();
    pin_setup(UART_PORT, UART_RX_PIN, UART_HSIOM_SEL, GPIO_CFG_DM_HIGHZ);
    pin_setup(UART_PORT, UART_TX_PIN, UART_HSIOM_SEL, GPIO_CFG_DM_STRONG);
    if (pclk_setup() != 0) {
        return;
    }
    if ((SCB_CTRL(UART_SCB_BASE) & SCB_CTRL_ENABLED) == 0u) {
        SCB_UART_CTRL(UART_SCB_BASE) = 0;
        SCB_UART_TX_CTRL(UART_SCB_BASE) = 1;
        SCB_UART_RX_CTRL(UART_SCB_BASE) = 1;
        SCB_TX_CTRL(UART_SCB_BASE) = SCB_DATA_WIDTH(8);
        SCB_RX_CTRL(UART_SCB_BASE) = SCB_DATA_WIDTH(8);
        SCB_TX_FIFO_CTRL(UART_SCB_BASE) = 0x3F;
        SCB_RX_FIFO_CTRL(UART_SCB_BASE) = 0x3F;
        SCB_CTRL(UART_SCB_BASE) = SCB_CTRL_ENABLED | SCB_CTRL_MODE_UART |
            ((UART_OVS - 1UL) & SCB_CTRL_OVS_MASK);
    }
    uart_ready = 1;
}

void emu_uart_write(uint8_t c)
{
    uint32_t t = UART_TX_POLL_LIMIT;

    if (!uart_ready) {
        return;
    }
    while (((SCB_TX_FIFO_STATUS(UART_SCB_BASE) & SCB_TX_FIFO_USED_Msk) >=
            SCB_TX_FIFO_SAFE_DEPTH) && (t > 0u)) {
        t--;
    }
    if (t != 0u) {
        SCB_TX_FIFO_WR(UART_SCB_BASE) = (uint32_t)c;
    }
}

int emu_uart_read(uint8_t *c)
{
    if (!uart_ready) {
        return 0;
    }
    if ((SCB_RX_FIFO_STATUS(UART_SCB_BASE) & SCB_RX_FIFO_USED_Msk) == 0u) {
        return 0;
    }
    *c = (uint8_t)(SCB_RX_FIFO_RD(UART_SCB_BASE) & 0xFFu);
    return 1;
}
