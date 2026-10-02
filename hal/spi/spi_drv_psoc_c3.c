/* spi_drv_psoc_c3.c
 *
 * Driver for the SPI back-end on Infineon PSOC Control C3.
 *
 * Pinout: see spi_drv_psoc_c3.h
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
#include "spi_drv.h"

#ifdef TARGET_psoc_c3

#if defined(SPI_FLASH) || defined(WOLFBOOT_TPM)

#include "hal/spi/spi_drv_psoc_c3.h"

#ifndef PSOC_C3_PCLK_HZ
#define PSOC_C3_PCLK_HZ         48000000UL
#endif
/* The SCB oversamples the SPI clock by this factor in master mode. */
#define PSOC_C3_SPI_OVS         4UL

/* Bound every FIFO wait: an absent or misrouted TPM would otherwise wedge the
 * bootloader in a spin loop instead of letting the TPM layer report it. */
#ifndef PSOC_C3_SPI_TIMEOUT
#define PSOC_C3_SPI_TIMEOUT     (1000000UL)
#endif

static void spi_pins_setup(void)
{
    psoc_c3_pin_setup(PSOC_C3_SPI_PORT, PSOC_C3_SPI_SCK_PIN,
            PSOC_C3_SPI_HSIOM_SEL, GPIO_CFG_DM_STRONG);
    psoc_c3_pin_setup(PSOC_C3_SPI_PORT, PSOC_C3_SPI_MOSI_PIN,
            PSOC_C3_SPI_HSIOM_SEL, GPIO_CFG_DM_STRONG);
    psoc_c3_pin_setup(PSOC_C3_SPI_PORT, PSOC_C3_SPI_MISO_PIN,
            PSOC_C3_SPI_HSIOM_SEL, GPIO_CFG_DM_HIGHZ);
    /* Chip select stays a GPIO (mux 0) so it can be held across a transfer. */
    psoc_c3_pin_setup(PSOC_C3_SPI_PORT, PSOC_C3_SPI_CS_PIN,
            0, GPIO_CFG_DM_STRONG);
}

static int spi_clock_setup(void)
{
    return psoc_c3_pclk_setup(PSOC_C3_SPI_PCLK_GR, PSOC_C3_SPI_PCLK_IDX,
            PSOC_C3_SPI_PCLK_DIV, PERI_PCLK_DIV_TYPE_16_5, PSOC_C3_PCLK_HZ,
            PSOC_C3_SPI_HZ * PSOC_C3_SPI_OVS);
}

/* spi_write() and spi_read() cannot report a stalled FIFO through the shared
 * prototypes, so they record it here for spi_xfer() to return. */
static int spi_fault;
/* File scope so the transfer path can refuse to run when spi_init() bailed
 * out; an SCB whose divider never started bus-faults on first access. */
static int initialized = 0;

/* The shared spi_drv.h prototype names this "base"; on this part chip select
 * is a GPIO, so it carries a port index rather than a peripheral address. */
void RAMFUNCTION spi_cs_off(uint32_t base, int pin)
{
    if (!initialized)
        return;
    GPIO_PRT_OUT(base) |= (1UL << pin);
}

void RAMFUNCTION spi_cs_on(uint32_t base, int pin)
{
    if (!initialized)
        return;
    GPIO_PRT_OUT(base) &= ~(1UL << pin);
}

void RAMFUNCTION spi_write(const char byte)
{
    uint32_t timeout = PSOC_C3_SPI_TIMEOUT;

    /* SPI_FLASH drives these directly rather than through spi_xfer(), so the
     * guard belongs on each one and not only on the transfer path. */
    if (!initialized) {
        spi_fault = 1;
        return;
    }
    while (((SCB_TX_FIFO_STATUS(PSOC_C3_SPI_SCB_BASE) &
             SCB_TX_FIFO_USED_Msk) >= SCB_TX_FIFO_SAFE_DEPTH) &&
           (timeout > 0))
        timeout--;
    if (timeout == 0) {
        /* FIFO never drained; dropping beats a silent overflow. */
        spi_fault = 1;
        return;
    }
    SCB_TX_FIFO_WR(PSOC_C3_SPI_SCB_BASE) = (uint32_t)(uint8_t)byte;
}

uint8_t RAMFUNCTION spi_read(void)
{
    uint32_t timeout = PSOC_C3_SPI_TIMEOUT;

    if (!initialized) {
        spi_fault = 1;
        return 0xFF;
    }
    /* Master mode clocks a byte in for every byte out. */
    while (((SCB_RX_FIFO_STATUS(PSOC_C3_SPI_SCB_BASE) &
             SCB_RX_FIFO_USED_Msk) == 0) && (timeout > 0))
        timeout--;
    if (timeout == 0) {
        spi_fault = 1;
        return 0xFF;
    }
    return (uint8_t)SCB_RX_FIFO_RD(PSOC_C3_SPI_SCB_BASE);
}

void spi_init(int polarity, int phase)
{
    if (initialized) {
        /* Every caller in the tree asks for mode 0, but a later call with a
         * different mode must not be silently dropped. */
        SCB_CTRL(PSOC_C3_SPI_SCB_BASE) &= ~SCB_CTRL_ENABLED;
        SCB_SPI_CTRL(PSOC_C3_SPI_SCB_BASE) = SCB_SPI_CTRL_MASTER |
            (polarity ? SCB_SPI_CTRL_CPOL : 0) |
            (phase ? SCB_SPI_CTRL_CPHA : 0);
        SCB_CTRL(PSOC_C3_SPI_SCB_BASE) |= SCB_CTRL_ENABLED;
    }
    if (!initialized) {
        psoc_c3_peri_init();
        spi_pins_setup();
        /* An SCB without a running divider bus-faults on first access. Leave
         * the flag clear so a later call can retry rather than run against an
         * unconfigured block. */
        if (spi_clock_setup() != 0)
            return;
        initialized++;
        spi_cs_off(PSOC_C3_SPI_PORT, PSOC_C3_SPI_CS_PIN);

        SCB_CTRL(PSOC_C3_SPI_SCB_BASE) = 0;
        SCB_SPI_CTRL(PSOC_C3_SPI_SCB_BASE) = SCB_SPI_CTRL_MASTER |
            (polarity ? SCB_SPI_CTRL_CPOL : 0) |
            (phase ? SCB_SPI_CTRL_CPHA : 0);
        SCB_TX_CTRL(PSOC_C3_SPI_SCB_BASE) =
            SCB_DATA_WIDTH(8) | SCB_CTRL_MSB_FIRST;
        SCB_RX_CTRL(PSOC_C3_SPI_SCB_BASE) =
            SCB_DATA_WIDTH(8) | SCB_CTRL_MSB_FIRST;
        SCB_TX_FIFO_CTRL(PSOC_C3_SPI_SCB_BASE) = 0x3F;
        SCB_RX_FIFO_CTRL(PSOC_C3_SPI_SCB_BASE) = 0x3F;
        SCB_CTRL(PSOC_C3_SPI_SCB_BASE) = SCB_CTRL_ENABLED |
            SCB_CTRL_MODE_SPI |
            ((PSOC_C3_SPI_OVS - 1UL) & SCB_CTRL_OVS_MASK);
    }
}

void spi_release(void)
{
    spi_cs_off(PSOC_C3_SPI_PORT, PSOC_C3_SPI_CS_PIN);
}

#ifdef WOLFBOOT_TPM
int spi_xfer(int cs, const uint8_t* tx, uint8_t* rx, uint32_t sz, int flags)
{
    uint32_t i;
    uint8_t byte;

    /* Only one chip select is wired, and spi_init() configured that pin, so
     * use it rather than the caller's number. */
    (void)cs;
    if (!initialized)
        return -1;
    spi_fault = 0;
    spi_cs_on(PSOC_C3_SPI_PORT, PSOC_C3_SPI_CS_PIN);
    for (i = 0; i < sz; i++) {
        spi_write((tx != NULL) ? (char)tx[i] : 0);
        /* A stalled transmit queues nothing, so the matching read would spin
         * its own full timeout for every byte left. Stop at the first. */
        if (spi_fault)
            break;
        byte = spi_read();
        if (rx != NULL)
            rx[i] = byte;
        if (spi_fault)
            break;
    }
    if (!(flags & SPI_XFER_FLAG_CONTINUE)) {
        spi_cs_off(PSOC_C3_SPI_PORT, PSOC_C3_SPI_CS_PIN);
    }
    /* A stalled bus must not read as a successful transfer, or the caller
     * treats the 0xFF fill as real data. */
    return (spi_fault != 0) ? -1 : 0;
}
#endif /* WOLFBOOT_TPM */

#endif /* SPI_FLASH || WOLFBOOT_TPM */
#endif /* TARGET_psoc_c3 */
