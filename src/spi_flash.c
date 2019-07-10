/* spi_flash.c
 *
 * Generic implementation of the read/write/erase
 * functionalities, on top of the spi_drv.h HAL.
 *
 *
 * Copyright (C) 2019 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "spi_drv.h"
#include "spi_flash.h"

#define MDID            0x90
#define RDSR            0x05
#define WRSR            0x01
#   define ST_BUSY (1 << 0)
#   define ST_WEL  (1 << 1)
#   define ST_BP0  (1 << 2)
#   define ST_BP1  (1 << 3)
#   define ST_BP2  (1 << 4)
#   define ST_BP3  (1 << 5)
#   define ST_AAI  (1 << 6)
#   define ST_BRO  (1 << 7)
#define WREN            0x06
#define WRDI            0x04
#define SECTOR_ERASE    0x20
#define BYTE_READ       0x03
#define BYTE_WRITE      0x02
#define AUTOINC         0xAD
#define EWSR            0x50
#define EBSY            0x70
#define DBSY            0x80


static enum write_mode {
    WB_WRITEPAGE = 0x00,
    SST_SINGLEBYTE = 0x01
} chip_write_mode = WB_WRITEPAGE;

static void write_address(uint32_t address)
{
    spi_write((address & 0xFF0000) >> 16);
    spi_read();
    spi_write((address & 0xFF00) >> 8);
    spi_read();
    spi_write((address & 0xFF));
    spi_read();
}

static uint8_t read_status(void)
{
    uint8_t status;
    spi_cs_on();
    spi_write(RDSR);
    spi_read();
    spi_write(0xFF);
    status = spi_read();
    spi_cs_off();
    return status;
}

static void spi_cmd(uint8_t cmd)
{
    spi_cs_on();
    spi_write(cmd);
    spi_read();
    spi_cs_off();
}

static void flash_write_enable(void)
{
    uint8_t status;
    do {
        spi_cmd(WREN);
        status = read_status();
    } while ((status & ST_WEL) == 0);
}

static void flash_write_disable(void)
{
    uint8_t status;
    spi_cmd(WRDI);
}
static void wait_busy(void)
{
    uint8_t status;
    do {
        status = read_status();
    } while(status & ST_BUSY);
}

static int spi_flash_write_page(uint32_t address, const void *data, int len)
{
    const uint8_t *buf = data;
    int j = 0;
    while (len > 0) {
        wait_busy();
        flash_write_enable();
        wait_busy();

        spi_cs_on();
        spi_write(BYTE_WRITE);
        spi_read();
        write_address(address);
        do {
            spi_write(buf[j++]);
            address++;
            spi_read();
            len--;
        } while ((address & (SPI_FLASH_PAGE_SIZE - 1)) != 0);
        spi_cs_off();
    }
    wait_busy();
    return j;
}

static int spi_flash_write_sb(uint32_t address, const void *data, int len)
{
    const uint8_t *buf = data;
    const uint8_t verify;
    int j = 0;
    wait_busy();
    if (len < 1)
        return -1;
    while (len > 0) {
        flash_write_enable();
        spi_cs_on();
        spi_write(BYTE_WRITE);
        spi_read();
        write_address(address);
        spi_write(buf[j]);
        spi_read();
        spi_cs_off();
        wait_busy();
        spi_flash_read(address, &verify, 1);
        if ((verify & ~(buf[j])) == 0) {
            if (verify != buf[j])
                return -1;
            j++;
            len--;
            address++;
        }
        wait_busy();
    }
    return 0;
}

/* --- */

uint16_t spi_flash_probe(void)
{
    uint8_t manuf, product, b0;
    int i;
    spi_init(0,0);
    wait_busy();
    spi_cs_on();
    spi_write(MDID);
    b0 = spi_read();

    write_address(0);
    spi_write(0xFF);
    manuf = spi_read();
    spi_write(0xFF);
    product = spi_read();
    spi_cs_off();
    if (manuf == 0xBF)
        chip_write_mode = SST_SINGLEBYTE;
    if (manuf == 0xEF)
        chip_write_mode = WB_WRITEPAGE;

#ifndef READONLY
    spi_cmd(EWSR);
    spi_cs_on();
    spi_write(WRSR);
    spi_read();
    spi_write(0x00);
    spi_read();
    spi_cs_off();
#endif
    return (uint16_t)(manuf << 8 | product);
}


void spi_flash_sector_erase(uint32_t address)
{
    uint8_t status;
    address &= (~(SPI_FLASH_SECTOR_SIZE - 1));

    wait_busy();
    flash_write_enable();
    spi_cs_on();
    spi_write(SECTOR_ERASE);
    spi_read();
    write_address(address);
    spi_cs_off();
    wait_busy();
}

int spi_flash_read(uint32_t address, void *data, int len)
{
    uint8_t *buf = data;
    int i = 0;
    wait_busy();
    spi_cs_on();
    spi_write(BYTE_READ);
    spi_read();
    write_address(address);
    while (len > 0) {
        spi_write(0xFF);
        buf[i++] = spi_read();
        len--;
    }
    spi_cs_off();
    return i;
}

int spi_flash_write(uint32_t address, const void *data, int len)
{
    if (chip_write_mode == SST_SINGLEBYTE)
        return spi_flash_write_sb(address, data, len);
    if (chip_write_mode == WB_WRITEPAGE)
        return spi_flash_write_page(address, data, len);
    return -1;
}
