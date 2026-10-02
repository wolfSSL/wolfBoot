/* i2c_drv_psoc_c3.c
 *
 * Driver for the I2C back-end on Infineon PSOC Control C3.
 *
 * Pinout: see i2c_drv_psoc_c3.h
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
#include "i2c_drv.h"

#ifdef TARGET_psoc_c3
#ifdef WOLFBOOT_TPM_I2C

#ifndef PSOC_C3_PCLK_HZ
#define PSOC_C3_PCLK_HZ         48000000UL
#endif

/* The SCB oversamples the I2C clock by the sum of the low and high phase
 * counts. 8 + 8 is the vendor default for standard and fast mode. */
#define PSOC_C3_I2C_OVS_LOW     8UL
#define PSOC_C3_I2C_OVS_HIGH    8UL
#define PSOC_C3_I2C_OVS         (PSOC_C3_I2C_OVS_LOW + PSOC_C3_I2C_OVS_HIGH)

/* Bound every wait: a missing or held-down device must let the caller report
 * a failure rather than wedge the bootloader in a spin loop. */
#ifndef PSOC_C3_I2C_TIMEOUT
#define PSOC_C3_I2C_TIMEOUT     (1000000UL)
#endif

static int initialized = 0;

static void i2c_pins_setup(void)
{
    /* I2C is open drain and needs the input buffer on, both to read SDA and
     * to see a slave stretching the clock. */
    psoc_c3_pin_setup(PSOC_C3_I2C_PORT, PSOC_C3_I2C_SCL_PIN,
            PSOC_C3_I2C_HSIOM_SEL, GPIO_CFG_DM_OD_LOW);
    psoc_c3_pin_setup(PSOC_C3_I2C_PORT, PSOC_C3_I2C_SDA_PIN,
            PSOC_C3_I2C_HSIOM_SEL, GPIO_CFG_DM_OD_LOW);
}

/* Route the SCB's clock destination to an integer divider. The 16.5
 * fractional divider the console uses does not exist in every group, and the
 * group carrying most peripherals provides only integer ones. */
static int i2c_clock_setup(void)
{
    return psoc_c3_pclk_setup(PSOC_C3_I2C_PCLK_GR, PSOC_C3_I2C_PCLK_IDX,
            PSOC_C3_I2C_PCLK_DIV, PERI_PCLK_DIV_TYPE_16, PSOC_C3_PCLK_HZ,
            PSOC_C3_I2C_HZ * PSOC_C3_I2C_OVS);
}

/* Wait for any of the master interrupt causes in mask, then report which. The
 * flags are write-one-to-clear and are cleared before each transfer step. */
static int i2c_wait_m(uint32_t mask, uint32_t *got)
{
    uint32_t timeout = PSOC_C3_I2C_TIMEOUT;
    uint32_t intr;

    do {
        intr = SCB_INTR_M(PSOC_C3_I2C_SCB_BASE) & mask;
        if (intr != 0) {
            *got = intr;
            SCB_INTR_M(PSOC_C3_I2C_SCB_BASE) = intr;
            return I2C_OK;
        }
        timeout--;
    } while (timeout > 0);

    *got = 0;
    return I2C_ERR_TIMEOUT;
}

/* Send a start (or repeated start) and the address byte, and report whether
 * the device acknowledged it. */
static int i2c_start(uint8_t addr, int read)
{
    uint32_t got;
    int ret;

    SCB_INTR_M(PSOC_C3_I2C_SCB_BASE) = SCB_INTR_M_ALL;
    SCB_TX_FIFO_WR(PSOC_C3_I2C_SCB_BASE) =
        (uint32_t)((addr << 1) | (read ? 1U : 0U));
    /* Unconditional start, not START_ON_IDLE: the bus-busy detector reads
     * busy until it has observed a stop condition, so a controller that has
     * just been enabled waits for an idle that never arrives. */
    SCB_I2C_M_CMD(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_M_CMD_M_START;

    ret = i2c_wait_m(SCB_INTR_M_I2C_ACK | SCB_INTR_M_I2C_NACK |
            SCB_INTR_M_I2C_ARB_LOST | SCB_INTR_M_I2C_BUS_ERROR, &got);
    if (ret != I2C_OK)
        return ret;
    if ((got & (SCB_INTR_M_I2C_ARB_LOST | SCB_INTR_M_I2C_BUS_ERROR)) != 0)
        return I2C_ERR_BUS;
    if ((got & SCB_INTR_M_I2C_NACK) != 0)
        return I2C_ERR_NACK;
    return I2C_OK;
}

static int i2c_stop(void)
{
    uint32_t got;
    int ret;

    SCB_I2C_M_CMD(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_M_CMD_M_STOP;
    ret = i2c_wait_m(SCB_INTR_M_I2C_STOP | SCB_INTR_M_I2C_BUS_ERROR, &got);
    if (ret != I2C_OK)
        return ret;
    /* i2c_wait_m() reports only that one of the masked causes fired, so a bus
     * error would otherwise be indistinguishable from a clean stop. */
    if ((got & SCB_INTR_M_I2C_BUS_ERROR) != 0)
        return I2C_ERR_BUS;
    return I2C_OK;
}

void i2c_init(void)
{
    if (initialized)
        return;

    psoc_c3_peri_init();
    i2c_pins_setup();
    /* Leave the block alone if its clock never started: an SCB without a
     * running divider bus-faults on the first register access. Transfers then
     * report a bus error rather than taking the fault. */
    if (i2c_clock_setup() != 0)
        return;
    initialized = 1;

    SCB_CTRL(PSOC_C3_I2C_SCB_BASE) = 0;
    /* The clock phases live here, not in CTRL.OVS, which I2C mode ignores. */
    SCB_I2C_CTRL(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_CTRL_MASTER_MODE |
        ((PSOC_C3_I2C_OVS_HIGH - 1UL) << SCB_I2C_CTRL_HIGH_PHASE_Pos) |
        ((PSOC_C3_I2C_OVS_LOW - 1UL) << SCB_I2C_CTRL_LOW_PHASE_Pos);
    SCB_I2C_CFG(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_CFG_DEFAULT;
    SCB_TX_CTRL(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_TX_CTRL_VAL;
    SCB_RX_CTRL(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_RX_CTRL_VAL;
    SCB_TX_FIFO_CTRL(PSOC_C3_I2C_SCB_BASE) = 0;
    SCB_RX_FIFO_CTRL(PSOC_C3_I2C_SCB_BASE) = 0;
    SCB_INTR_M_MASK(PSOC_C3_I2C_SCB_BASE) = 0;   /* polled, never interrupts */
    SCB_INTR_M(PSOC_C3_I2C_SCB_BASE) = SCB_INTR_M_ALL;
    SCB_CTRL(PSOC_C3_I2C_SCB_BASE) = SCB_CTRL_ENABLED | SCB_CTRL_MODE_I2C;
}

void i2c_release(void)
{
    uint32_t got;

    if (!initialized)
        return;
    /* Leave the bus idle rather than mid-transfer if a caller gave up. */
    if ((SCB_I2C_STATUS(PSOC_C3_I2C_SCB_BASE) & SCB_I2C_STATUS_BUS_BUSY) != 0) {
        SCB_I2C_M_CMD(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_M_CMD_M_STOP;
        (void)i2c_wait_m(SCB_INTR_M_I2C_STOP | SCB_INTR_M_I2C_BUS_ERROR, &got);
    }
}

int i2c_write(uint8_t addr, const uint8_t *buf, uint32_t len, int stop)
{
    uint32_t got;
    uint32_t i;
    int ret;

    if ((buf == NULL) && (len > 0))
        return I2C_ERR_ARG;
    if (!initialized)
        return I2C_ERR_BUS;

    ret = i2c_start(addr, 0);
    if (ret != I2C_OK) {
        /* An unacknowledged address still leaves the bus owned. */
        (void)i2c_stop();
        return ret;
    }

    for (i = 0; i < len; i++) {
        SCB_TX_FIFO_WR(PSOC_C3_I2C_SCB_BASE) = (uint32_t)buf[i];
        ret = i2c_wait_m(SCB_INTR_M_I2C_ACK | SCB_INTR_M_I2C_NACK |
                SCB_INTR_M_I2C_ARB_LOST | SCB_INTR_M_I2C_BUS_ERROR, &got);
        if (ret != I2C_OK) {
            (void)i2c_stop();
            return ret;
        }
        if ((got & (SCB_INTR_M_I2C_ARB_LOST | SCB_INTR_M_I2C_BUS_ERROR)) != 0) {
            (void)i2c_stop();
            return I2C_ERR_BUS;
        }
        if ((got & SCB_INTR_M_I2C_NACK) != 0) {
            (void)i2c_stop();
            return I2C_ERR_NACK;
        }
    }

    if (stop)
        return i2c_stop();
    return I2C_OK;
}

int i2c_read(uint8_t addr, uint8_t *buf, uint32_t len, int stop)
{
    uint32_t timeout;
    uint32_t got;
    uint32_t i;
    int ret;

    if ((buf == NULL) || (len == 0))
        return I2C_ERR_ARG;
    if (!initialized)
        return I2C_ERR_BUS;

    ret = i2c_start(addr, 1);
    if (ret != I2C_OK) {
        (void)i2c_stop();
        return ret;
    }

    for (i = 0; i < len; i++) {
        /* Acknowledge after the byte lands, not before: an acknowledge
         * issued early requests nothing and the FIFO never fills. */
        timeout = PSOC_C3_I2C_TIMEOUT;
        while (((SCB_RX_FIFO_STATUS(PSOC_C3_I2C_SCB_BASE) &
                 SCB_RX_FIFO_USED_Msk) == 0) && (timeout > 0))
            timeout--;
        if (timeout == 0) {
            (void)i2c_stop();
            return I2C_ERR_TIMEOUT;
        }
        buf[i] = (uint8_t)SCB_RX_FIFO_RD(PSOC_C3_I2C_SCB_BASE);

        if ((i + 1U) < len) {
            SCB_I2C_M_CMD(PSOC_C3_I2C_SCB_BASE) = SCB_I2C_M_CMD_M_ACK;
        }
        else {
            /* The last byte is NACKed so the device releases the bus. The
             * stop must ride in the same command; issued separately while
             * the NACK is in flight it never completes. */
            SCB_I2C_M_CMD(PSOC_C3_I2C_SCB_BASE) = stop ?
                (SCB_I2C_M_CMD_M_NACK | SCB_I2C_M_CMD_M_STOP) :
                SCB_I2C_M_CMD_M_NACK;
        }
    }

    if (stop) {
        ret = i2c_wait_m(SCB_INTR_M_I2C_STOP | SCB_INTR_M_I2C_BUS_ERROR, &got);
        if (ret != I2C_OK)
            return ret;
        if ((got & SCB_INTR_M_I2C_BUS_ERROR) != 0)
            return I2C_ERR_BUS;
    }
    return I2C_OK;
}


#endif /* WOLFBOOT_TPM_I2C */
#endif /* TARGET_psoc_c3 */
