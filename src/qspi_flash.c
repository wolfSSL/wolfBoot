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

#include "spi_drv.h"
#include "spi_flash.h"

#if defined(QSPI_FLASH) || defined(OCTOSPI_FLASH)

#include "string.h"
#include "printf.h"

#ifdef DEBUG_UART
#define DEBUG_QSPI 1
#endif

/* Flash Parameters:
 * Winbond W25Q128FV 128Mbit serial flash
 */
#ifndef FLASH_DEVICE_SIZE
#define FLASH_DEVICE_SIZE      (16 * 1024 * 1024)
#endif
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE        SPI_FLASH_PAGE_SIZE
#endif
#ifndef FLASH_NUM_PAGES
#define FLASH_NUM_PAGES        (FLASH_DEVICE_SIZE/FLASH_PAGE_SIZE)
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE      SPI_FLASH_SECTOR_SIZE
#endif
#define FLASH_NUM_SECTORS      (FLASH_DEVICE_SIZE/FLASH_SECTOR_SIZE)


/* QSPI Configuration - Use single/dual/quad mode for data transfers */
#ifndef QSPI_DATA_MODE
#define QSPI_DATA_MODE         QSPI_DATA_MODE_SPI
#endif
#ifndef QSPI_ADDR_SZ
#define QSPI_ADDR_SZ           3
#endif
#ifndef QSPI_DUMMY_READ
#define QSPI_DUMMY_READ        8 /* Number of dummy clock cycles for reads */
#endif
#ifndef QSPI_FLASH_READY_TRIES
#define QSPI_FLASH_READY_TRIES 10000
#endif


/* Flash Commands */
#define WRITE_ENABLE_CMD       0x06U
#define READ_SR_CMD            0x05U
#define READ_SR2_CMD           0x35U
#define WRITE_SR_CMD           0x01U
#define WRITE_SR2_CMD          0x31U
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
#define QUAD_PROG_CMD          0x32U

#define PAGE_PROG_4B_CMD       0x12U
#define DUAL_PROG_4B_CMD       0x12U
#define QUAD_PROG_4B_CMD       0x34U

#define SEC_ERASE_CMD          0x20U /* 4KB */
#define BLOCK_ERASE_CMD        0xD8U /* 64KB */
#define RESET_ENABLE_CMD       0x66U
#define RESET_MEMORY_CMD       0x99U

#define FLASH_SR_WRITE_EN      0x02 /* 1=Write Enabled, 0=Write Disabled */
#define FLASH_SR_BUSY          0x01 /* 1=Busy, 0=Ready */

#define FLASH_SR2_QE           0x02 /* 1=Quad Enable (QE) */

/* Read Command */
#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI && QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD  QUAD_READ_4B_CMD
#elif QSPI_DATA_MODE == QSPI_DATA_MODE_DSPI && QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD DUAL_READ_4B_CMD
#elif QSPI_ADDR_SZ == 4
#define FLASH_READ_CMD FAST_READ_4B_CMD
#elif QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
#define FLASH_READ_CMD QUAD_READ_CMD
#undef  QSPI_DUMMY_READ
#define QSPI_DUMMY_READ 4
#define QSPI_ADDR_MODE QSPI_DATA_MODE_QSPI
#elif QSPI_DATA_MODE == QSPI_DATA_MODE_DSPI
#define FLASH_READ_CMD DUAL_READ_CMD
#else
#define FLASH_READ_CMD FAST_READ_CMD
#endif

/* Write Command */
#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
#define FLASH_WRITE_CMD QUAD_PROG_CMD
#elif QSPI_DATA_MODE == QSPI_DATA_MODE_DSPI
#define FLASH_WRITE_CMD DUAL_PROG_CMD
#else
#define FLASH_WRITE_CMD PAGE_PROG_CMD
#endif

/* default to single SPI mode for address */
#ifndef QSPI_ADDR_MODE
#define QSPI_ADDR_MODE QSPI_DATA_MODE_SPI
#endif


/* forward declarations */
static int qspi_wait_ready(void);
static int qspi_status(uint8_t* status);
#ifdef TEST_FLASH
static int test_flash(void);
#endif

static inline int qspi_command_simple(uint8_t fmode, uint8_t cmd,
    uint8_t* data, uint32_t dataSz)
{
    uint32_t dmode = QSPI_DATA_MODE_NONE;
    if (dataSz > 0) {
        dmode = QSPI_DATA_MODE_SPI;
    }
    return qspi_transfer(fmode, cmd,
        0, 0, QSPI_DATA_MODE_NONE,   /* Address */
        0, 0, QSPI_DATA_MODE_NONE,   /* Alternate Bytes */
        0,                           /* Dummy Cycles */
        data, dataSz, dmode          /* Data */
    );
}

static int qspi_flash_read_id(uint8_t* id, uint32_t idSz)
{
    int ret;
    uint8_t data[4]; /* size multiple of uint32_t */
    uint32_t status = 0;

    memset(data, 0, sizeof(data));
    ret = qspi_command_simple(QSPI_MODE_READ, READ_ID_CMD, data, 3);

#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash ID (ret %d): 0x%x\n",
        ret, *((uint32_t*)data));
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
    uint32_t timeout;
    uint8_t status = 0;

    /* send write enable */
    ret = qspi_command_simple(QSPI_MODE_WRITE, WRITE_ENABLE_CMD, NULL, 0);
#if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
    wolfBoot_printf("Write Enable: Ret %d\n", ret);
#endif

    /* wait until write enabled and not busy */
    timeout = 0;
    while (++timeout < QSPI_FLASH_READY_TRIES) {
        ret = qspi_status(&status);
        if (ret == 0 && (status & FLASH_SR_WRITE_EN)
                     && (status & FLASH_SR_BUSY) == 0) {
            break;
        }
    }
    if (timeout >= QSPI_FLASH_READY_TRIES) {
#ifdef DEBUG_QSPI
        wolfBoot_printf("Flash WE Timeout!\n");
#endif
        return -1; /* timeout */
    }

#if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
    wolfBoot_printf("Write Enabled: %s\n",
        (status & FLASH_SR_WRITE_EN) ? "yes" : "no");
#endif

    return ret;
}

static int qspi_write_disable(void)
{
    int ret = qspi_command_simple(QSPI_MODE_WRITE, WRITE_DISABLE_CMD, NULL, 0);
#if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
    wolfBoot_printf("Write Disable: Ret %d\n", ret);
#endif
    return ret;
}

static int qspi_status(uint8_t* status)
{
    int ret;
    uint8_t data[4]; /* size multiple of uint32_t */
#if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
    static uint8_t last_status = 0;
#endif

    memset(data, 0, sizeof(data));
    ret = qspi_command_simple(QSPI_MODE_READ, READ_SR_CMD, data, 1);
#if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
    if (status == NULL || last_status != data[0]) {
        wolfBoot_printf("Status (ret %d): %02x -> %02x\n",
            ret, last_status, data[0]);
    }
    last_status = data[0];
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

#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
static int qspi_quad_enable(void)
{
    int ret;
    uint8_t data[4]; /* size multiple of uint32_t */

    memset(data, 0, sizeof(data));
    ret = qspi_command_simple(QSPI_MODE_READ, READ_SR2_CMD, data, 1);
#ifdef DEBUG_QSPI
    wolfBoot_printf("Status Reg 2: Ret %d, 0x%x (Quad Enabled: %s)\n",
        ret, data[0], (data[0] & FLASH_SR2_QE) ? "Yes" : "No");
#endif
    if (ret == 0 && (data[0] & FLASH_SR2_QE) == 0) {
        ret = qspi_write_enable();
        if (ret == 0) {
            memset(data, 0, sizeof(data));
            data[0] |= FLASH_SR2_QE;
            ret = qspi_command_simple(QSPI_MODE_WRITE, WRITE_SR2_CMD, data, 1);
#ifdef DEBUG_QSPI
            wolfBoot_printf("Setting Quad Enable: Ret %d, SR2 0x%x\n",
                ret, data[0]);
#endif

            qspi_wait_ready();
            qspi_write_disable();
        }
    }
    return ret;
}
#endif

#if QSPI_ADDR_SZ == 4
static int qspi_enter_4byte_addr(void)
{
    int ret = qspi_write_enable();
    if (ret == 0) {
        ret = qspi_command_simple(QSPI_MODE_WRITE, ENTER_4B_ADDR_MODE_CMD, NULL, 0);
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
        ret = qspi_command_simple(QSPI_MODE_WRITE, EXIT_4B_ADDR_MODE_CMD, NULL, 0);
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

#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
    qspi_quad_enable();
#endif
#if QSPI_ADDR_SZ == 4
    qspi_enter_4byte_addr();
#endif

#ifdef TEST_FLASH
    test_flash();
#endif
    return 0;
}

/* Called for each sector from hal.h inline ext_flash_erase function
 * Use SPI_FLASH_SECTOR_SIZE to adjust for QSPI sector size */
int spi_flash_sector_erase(uint32_t address)
{
    int ret;
    uint32_t idx = 0;

    ret = qspi_write_enable();
    if (ret == 0) {
        /* ------ Erase Flash ------ */
        ret = qspi_transfer(QSPI_MODE_WRITE, SEC_ERASE_CMD,
            address, QSPI_ADDR_SZ, QSPI_DATA_MODE_SPI,     /* Address */
            0, 0, QSPI_DATA_MODE_NONE,                     /* Alternate Bytes */
            0,                                             /* Dummy */
            NULL, 0, QSPI_DATA_MODE_NONE                   /* Data */
        );
#ifdef DEBUG_QSPI
        wolfBoot_printf("Flash Erase: Ret %d, Address 0x%x\n", ret, address);
#endif
        if (ret == 0) {
            ret = qspi_wait_ready(); /* Wait for not busy */
        }
        /* write disable is automatic */
    }
    return ret;
}

int spi_flash_read(uint32_t address, void *data, int len)
{
    int ret;
#if QSPI_DATA_MODE == QSPI_DATA_MODE_QSPI
    const uint32_t altByte = 0xF0; /* enable continuous read */
    uint32_t altSz = 1;
    uint32_t altMode = QSPI_ADDR_MODE;
#else
    const uint32_t altByte = 0x00;
    uint32_t altSz = 0;
    uint32_t altMode = QSPI_DATA_MODE_NONE;
#endif

    if (address > FLASH_DEVICE_SIZE) {
#ifdef DEBUG_QSPI
        wolfBoot_printf("Flash Read: Invalid address (0x%x > 0x%x max)\n",
            address, FLASH_DEVICE_SIZE);
#endif
        return -1;
    }

    /* ------ Read Flash ------ */
    ret = qspi_transfer(QSPI_MODE_READ, FLASH_READ_CMD,
        address, QSPI_ADDR_SZ, QSPI_ADDR_MODE,             /* Address */
        altByte, altSz, altMode,                           /* Alternate Bytes */
        QSPI_DUMMY_READ,                                   /* Dummy */
        data, len, QSPI_DATA_MODE                          /* Data */
    );

#ifdef DEBUG_QSPI
    wolfBoot_printf("Flash Read: Ret %d, Address 0x%x, Len %d, Cmd 0x%x\n",
        ret, address, len, FLASH_READ_CMD);
#endif

    return ret;
}

int spi_flash_write(uint32_t address, const void *data, int len)
{
    int ret = 0;
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
            ret = qspi_transfer(QSPI_MODE_WRITE, FLASH_WRITE_CMD,
                addr, QSPI_ADDR_SZ, QSPI_DATA_MODE_SPI,    /* Address */
                0, 0, QSPI_DATA_MODE_NONE,                 /* Alternate Bytes */
                0,                                         /* Dummy */
                ((uint8_t*)data + (page * FLASH_PAGE_SIZE)),
                xferSz, QSPI_DATA_MODE                     /* Data */
            );
#ifdef DEBUG_QSPI
            wolfBoot_printf("Flash Write: Ret %d, Addr 0x%x, Len %d, Cmd 0x%x\n",
                ret, addr, xferSz, FLASH_WRITE_CMD);
#endif
            if (ret != 0)
                break;

            ret = qspi_wait_ready(); /* Wait for not busy */
            if (ret != 0) {
                break;
            }
            /* write disable is automatic */
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

#endif /* QSPI_FLASH || OCTOSPI_FLASH */


#ifdef TEST_FLASH
/* Start Address for test - 2MB */
#define TEST_ADDRESS (2 * 1024 * 1024)

static int test_flash(void)
{
    int ret;
    uint32_t i;
    uint8_t pageData[FLASH_PAGE_SIZE];
    uint32_t wait = 0;

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, FLASH_SECTOR_SIZE);
    wolfBoot_printf("Sector Erase: Ret %d\n", ret);

    /* Write Page */
    for (i=0; i<sizeof(pageData); i++) {
        pageData[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Write: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, pageData, sizeof(pageData));
    wolfBoot_printf("Page Read: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
    #if defined(DEBUG_QSPI) && DEBUG_QSPI > 1
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
    #endif
        if (pageData[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -1;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* TEST_FLASH */
