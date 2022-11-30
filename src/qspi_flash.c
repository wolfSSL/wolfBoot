/* qspi_flash.c
 *
 * Generic implementation of the read/write/erase
 * functionalities, on top of the spi_drv.h HAL.
 *
 *
 * Copyright (C) 2022 wolfSSL Inc.
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

#ifdef QSPI_FLASH

#include "string.h"
#include "printf.h"

#ifdef DEBUG_UART
#define DEBUG_QSPI
#endif

/* Flash Parameters:
 * Winbond W25Q128FV 128Mbit serial flash
 */
#ifndef FLASH_DEVICE_SIZE
#define FLASH_DEVICE_SIZE      (16 * 1024 * 1024)
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE        256
#endif
#ifndef FLASH_NUM_PAGES
#define FLASH_NUM_PAGES        0x10000
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE      WOLFBOOT_SECTOR_SIZE
#endif
#define FLASH_NUM_SECTORS      (FLASH_DEVICE_SIZE/FLASH_SECTOR_SIZE)

/* QSPI Configuration */
#ifndef QSPI_ADDR_MODE
#define QSPI_ADDR_MODE         QSPI_ADDR_MODE_QSPI
#endif
#ifndef QSPI_ADDR_SZ
#define QSPI_ADDR_SZ           3
#endif
#ifndef QSPI_DUMMY_READ
#define QSPI_DUMMY_READ        (8) /* Number of dummy clock cycles for reads */
#endif
#ifndef QSPI_FLASH_READY_TRIES
#define QSPI_FLASH_READY_TRIES 1000
#endif


/* Flash Commands */
#define WRITE_ENABLE_CMD       0x06U
#define READ_SR_CMD            0x05U
#define WRITE_DISABLE_CMD      0x04U
#define READ_ID_CMD            0x9FU

#define ENTER_QSPI_MODE_CMD    0x38U
#define EXIT_QSPI_MODE_CMD     0xFFU

#define ENTER_4B_ADDR_MODE_CMD 0xB7U
#define EXIT_4B_ADDR_MODE_CMD  0xE9U

#define FAST_READ_CMD          0x0BU
#define DUAL_READ_CMD          0x3BU
#define QUAD_READ_CMD          0xEBU
#define FAST_READ_4B_CMD       0x0CU
#define DUAL_READ_4B_CMD       0x3CU
#define QUAD_READ_4B_CMD       0x6CU

#define PAGE_PROG_CMD          0x02U
#define DUAL_PROG_CMD          0xA2U
#define QUAD_PROG_CMD          0x22U
#define PAGE_PROG_4B_CMD       0x12U
#define DUAL_PROG_4B_CMD       0x12U
#define QUAD_PROG_4B_CMD       0x34U

#define SEC_ERASE_CMD          0x20U /* 4KB */
#define BLOCK_ERASE_CMD        0xD8U /* 64KB */
#define RESET_ENABLE_CMD       0x66U
#define RESET_MEMORY_CMD       0x99U

#define FLASH_SR_WRITE_EN      0x02 /* 1=Write Enabled, 0=Write Disabled */
#define FLASH_SR_BUSY          0x01 /* 1=Busy, 0=Ready */

#if QSPI_ADDR_MODE == QSPI_ADDR_MODE_QSPI && QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD QUAD_READ_4B_CMD
#elif QSPI_ADDR_MODE == QSPI_ADDR_MODE_DSPI && QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD DUAL_READ_4B_CMD
#elif QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD FAST_READ_4B_CMD
#elif QSPI_ADDR_MODE == QSPI_ADDR_MODE_QSPI
#define FLASH_READ_CMD QUAD_READ_CMD
#elif QSPI_ADDR_MODE == QSPI_ADDR_MODE_DSPI
#define FLASH_READ_CMD DUAL_READ_CMD
#else
#define FLASH_READ_CMD FAST_READ_CMD
#endif


/* forward declarations */
static int qspi_wait_ready(void);
static int qspi_status(uint8_t* status);
static int qspi_wait_we(void);
#ifdef TEST_FLASH
static int test_flash(void);
#endif

static int qspi_flash_read_id(uint8_t* id, uint32_t idSz)
{
    int ret;
    uint8_t data[4]; /* size multiple of uint32_t */
    uint32_t status = 0;

    memset(data, 0, sizeof(data));
    ret = qspi_transfer(READ_ID_CMD, 0, 0, NULL, 0, data, 3, 0,
        QSPI_ADDR_MODE_SPI);
    qspi_status((uint8_t*)&status);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash: Ret %d: ID: %x, Cmd %x, status %x\n",
        ret, (uint32_t)data, FLASH_READ_CMD, status);
#endif

    /* optionally return id data */
    if (ret == 0 && id) {
        if (idSz > sizeof(data))
            idSz = sizeof(data);
        memcpy(id, data, idSz);
    }

    return ret;
}

static int qspi_write_enable(void)
{
    int ret;

    ret = qspi_transfer(WRITE_ENABLE_CMD, 0, 0, NULL, 0, NULL, 0, 0,
        QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Write Enable: Ret %d\n", ret);
#endif

    qspi_wait_ready();

    ret = qspi_wait_we();
#ifdef DEBUG_QSPI
    wolfBoot_printf("Write Enabled: %s\n", ret == 0 ? "yes" : "no");
#endif

    return ret;
}

static int qspi_write_disable(void)
{
    int ret = qspi_transfer(WRITE_DISABLE_CMD, 0, 0, NULL, 0, NULL, 0, 0,
        QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Write Disable: Ret %d\n", ret);
#endif
    return ret;
}

static int qspi_status(uint8_t* status)
{
    int ret;
    uint8_t data[4]; /* size multiple of uint32_t */

    memset(data, 0, sizeof(data));
    ret = qspi_transfer(READ_SR_CMD, 0, 0, NULL, 0, data, 1, 0,
        QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Status (ret %d): %02x\n", ret, data[0]);
#endif
    if (ret == 0 && status) {
        *status = data[0];
    }
    return ret;
}

static int qspi_wait_ready(void)
{
    int ret;
    uint32_t timeout;
    uint8_t status = 0;

    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_status(&status);
        if (ret == 0 && (status & FLASH_SR_BUSY) == 0) {
            return ret;
        }
    }

#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash Ready Timeout!\n");
#endif
    return -1;
}

static int qspi_wait_we(void)
{
    int ret;
    uint32_t timeout;
    uint8_t status = 0;

    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_status(&status);
        if (ret == 0 && (status & FLASH_SR_WRITE_EN)) {
            return ret;
        }
    }

#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash WE Timeout!\n");
#endif
    return -1;
}

#if QSPI_ADDR_SZ == 4
static int qspi_enter_4byte_addr(void)
{
    int ret = qspi_write_enable();
    if (ret == 0) {
        ret = qspi_transfer(ENTER_4B_ADDR_MODE_CMD, 0, 0,
            NULL, 0, NULL, 0,
            0, QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
        wolfBoot_printf("Enter 4-byte address mode: Ret %d\n", ret);
#endif
        if (ret == 0) {
            ret = qspi_wait_ready(); /* Wait for not busy */
        }
        qspi_write_disable();
    }
    return ret;
}
static int qspi_exit_4byte_addr(void)
{
    int ret = qspi_write_enable();
    if (ret == 0) {
        ret = qspi_transfer(EXIT_4B_ADDR_MODE_CMD, 0, 0,
            NULL, 0, NULL, 0,
            0, QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
        wolfBoot_printf("Enter 4-byte address mode: Ret %d\n", ret);
#endif
        if (ret == 0) {
            ret = qspi_wait_ready(); /* Wait for not busy */
        }
        qspi_write_disable();
    }
    return ret;
}
#endif


uint16_t spi_flash_probe(void)
{
    spi_init(0,0);
    qspi_flash_read_id(NULL, 0);

#if QSPI_ADDR_SZ == 4
    qspi_enter_4byte_addr();
#endif

#ifdef TEST_FLASH
    test_flash();
#endif
    return 0;
}

int spi_flash_sector_erase(uint32_t address)
{
    int ret;
    uint32_t idx = 0;

    ret = qspi_write_enable();
    if (ret == 0) {
        /* ------ Erase Flash ------ */
        ret = qspi_transfer(SEC_ERASE_CMD, address, QSPI_ADDR_SZ,
            NULL, 0, NULL, 0,
            0, QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
        wolfBoot_printf("Flash Erase: Ret %d\n", ret);
#endif
        if (ret == 0) {
            ret = qspi_wait_ready(); /* Wait for not busy */
        }
        qspi_write_disable();
    }
    return ret;
}

int spi_flash_read(uint32_t address, void *data, int len)
{
    int ret;

    /* ------ Read Flash ------ */
    ret = qspi_transfer(FLASH_READ_CMD, address, QSPI_ADDR_SZ,
        NULL, 0, data, len,
        QSPI_DUMMY_READ, QSPI_ADDR_MODE);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash Read: Ret %d\r\n", ret);
#endif

    return ret;
}

int spi_flash_write(uint32_t address, const void *data, int len)
{
    int ret = 0;
    uint8_t cmd[8]; /* size multiple of uint32_t */
    uint32_t xferSz, page, pages, idx = 0;
    uintptr_t addr;

    /* write by page */
    pages = ((len + (FLASH_PAGE_SIZE-1)) / FLASH_PAGE_SIZE);
    for (page = 0; page < pages; page++) {
        ret = qspi_write_enable();
        if (ret == 0) {
            xferSz = len;
            if (xferSz > FLASH_PAGE_SIZE)
                xferSz = FLASH_PAGE_SIZE;

            addr = address + (page * FLASH_PAGE_SIZE);

            /* ------ Write Flash (page at a time) ------ */
            ret = qspi_transfer(PAGE_PROG_CMD, addr, QSPI_ADDR_SZ,
                (const uint8_t*)(data + (page * FLASH_PAGE_SIZE)), xferSz,
                NULL, 0, 0, QSPI_ADDR_MODE_SPI);
#ifdef DEBUG_QSPI
            wolfBoot_printf("Flash Page %d Write: Ret %d\n", page, ret);
#endif
            if (ret != 0)
                break;

            ret = qspi_wait_ready(); /* Wait for not busy */
            if (ret != 0) {
                break;
            }
            qspi_write_disable();
        }
    }

    return ret;
}

void spi_flash_release(void)
{
#if QSPI_ADDR_SZ == 4
    qspi_exit_4byte_addr();
#endif

    spi_release();
}

#endif /* QSPI_FLASH */




#ifdef TEST_FLASH
#define TEST_ADDRESS (2 * 1024 * 1024) /* 2MB */
static int test_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];
    uint32_t wait = 0;

    /* long wait */
    while (++wait < 1000000);

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -1;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_FLASH */
