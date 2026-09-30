/* i2c_drv_bitbang.c
 *
 * Bit-banged I2C master behind the generic interface in include/i2c_drv.h.
 *
 * This is the fallback for a target whose I2C controller is unavailable,
 * unreliable, or simply not worth configuring for the handful of transfers a
 * bootloader makes. It needs nothing but two open-drain GPIOs, supplied by the
 * target through i2c_bitbang.h.
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
#include "i2c_bitbang.h"

/* Half a bit period, as a spin count rather than a calibrated delay. The bus
 * rate that results depends on the core clock, so it is approximate; I2C has
 * no minimum clock rate, so erring slow is always safe and only costs time.
 * Tune per target if the resulting rate matters. */
#ifndef I2C_BITBANG_DELAY
#define I2C_BITBANG_DELAY   200
#endif

/* How long to wait for a stretched clock to rise, in spin iterations. */
#ifndef I2C_BITBANG_STRETCH
#define I2C_BITBANG_STRETCH 10000
#endif

static int initialized = 0;

static void bb_delay(void)
{
    volatile int i;

    for (i = 0; i < I2C_BITBANG_DELAY; i++)
        ;
}

static void bb_scl_low(void)
{
    i2c_bitbang_scl(0);
    bb_delay();
}

static void bb_sda_set(int high)
{
    i2c_bitbang_sda(high);
    bb_delay();
}

/* Release SCL and wait for it to actually rise, so a slave holding the clock
 * down stretches the transfer instead of corrupting it. */
static int bb_scl_release(void)
{
    uint32_t timeout = I2C_BITBANG_STRETCH;

    i2c_bitbang_scl(1);
    while ((timeout > 0) && (i2c_bitbang_scl_read() == 0))
        timeout--;
    bb_delay();
    return (timeout > 0) ? I2C_OK : I2C_ERR_TIMEOUT;
}

/* Works for a repeated start too: both lines are raised first, so the
 * high-to-low edge on SDA lands while SCL is high either way. */
static int bb_start(void)
{
    int ret;

    bb_sda_set(1);
    ret = bb_scl_release();
    if (ret != I2C_OK)
        return ret;
    bb_sda_set(0);
    bb_scl_low();
    return I2C_OK;
}

static int bb_stop(void)
{
    int ret;

    bb_sda_set(0);
    ret = bb_scl_release();
    if (ret != I2C_OK)
        return ret;
    bb_sda_set(1);
    return I2C_OK;
}

/* Returns I2C_OK when the slave pulled SDA low for the acknowledge. */
static int bb_write_byte(uint8_t b)
{
    int ret;
    int ack;
    int i;

    for (i = 7; i >= 0; i--) {
        bb_sda_set((b >> i) & 1);
        ret = bb_scl_release();
        if (ret != I2C_OK)
            return ret;
        bb_scl_low();
    }
    /* Release SDA so the slave can drive the acknowledge bit. */
    bb_sda_set(1);
    ret = bb_scl_release();
    if (ret != I2C_OK)
        return ret;
    ack = i2c_bitbang_sda_read();
    bb_scl_low();
    return (ack == 0) ? I2C_OK : I2C_ERR_NACK;
}

static int bb_read_byte(uint8_t *out, int ack)
{
    uint8_t v = 0;
    int ret;
    int i;

    /* SDA stays released for the whole byte so the slave drives it. */
    bb_sda_set(1);
    for (i = 7; i >= 0; i--) {
        ret = bb_scl_release();
        if (ret != I2C_OK)
            return ret;
        v = (uint8_t)(v | ((uint8_t)(i2c_bitbang_sda_read() & 1) << i));
        bb_scl_low();
    }
    /* The master acknowledges every byte but the last; a NAK is what tells
     * the slave to stop driving and release the bus. */
    bb_sda_set(ack ? 0 : 1);
    ret = bb_scl_release();
    if (ret != I2C_OK)
        return ret;
    bb_scl_low();
    bb_sda_set(1);
    *out = v;
    return I2C_OK;
}

static int bb_address(uint8_t addr, int read)
{
    int ret;

    ret = bb_start();
    if (ret != I2C_OK)
        return ret;
    return bb_write_byte((uint8_t)((addr << 1) | (read ? 1 : 0)));
}

void i2c_init(void)
{
    if (!initialized) {
        i2c_bitbang_pins_init();
        initialized = 1;
    }
}

void i2c_release(void)
{
    if (!initialized)
        return;
    /* Leave the bus idle rather than mid-transfer if a caller gave up. A stop
     * is only well formed from SCL low, which is where every transfer here
     * leaves it. */
    (void)bb_stop();
}

int i2c_write(uint8_t addr, const uint8_t *buf, uint32_t len, int stop)
{
    uint32_t i;
    int ret;

    if ((buf == NULL) && (len != 0))
        return I2C_ERR_ARG;
    if (!initialized)
        return I2C_ERR_BUS;

    ret = bb_address(addr, 0);
    for (i = 0; (ret == I2C_OK) && (i < len); i++)
        ret = bb_write_byte(buf[i]);

    /* Always release the bus on the way out of a failure, or the next
     * transfer starts against a slave still holding SDA. */
    if ((ret != I2C_OK) || stop) {
        if (bb_stop() != I2C_OK && ret == I2C_OK)
            ret = I2C_ERR_TIMEOUT;
    }
    return ret;
}

int i2c_read(uint8_t addr, uint8_t *buf, uint32_t len, int stop)
{
    uint32_t i;
    int ret;

    if ((buf == NULL) || (len == 0))
        return I2C_ERR_ARG;
    if (!initialized)
        return I2C_ERR_BUS;

    ret = bb_address(addr, 1);
    for (i = 0; (ret == I2C_OK) && (i < len); i++)
        ret = bb_read_byte(&buf[i], ((i + 1U) < len));

    if ((ret != I2C_OK) || stop) {
        if (bb_stop() != I2C_OK && ret == I2C_OK)
            ret = I2C_ERR_TIMEOUT;
    }
    return ret;
}
