/* atsama5d3.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include <string.h>
#include <target.h>
#include "image.h"
#ifndef ARCH_ARM
#   error "wolfBoot atsama5d3 HAL: wrong architecture selected. Please compile with ARCH=ARM."
#endif

/* Fixed addresses */
extern void *kernel_addr, *update_addr, *dts_addr;

#if defined(EXT_FLASH) && defined(NAND_FLASH)

/* Constant for local buffers */
#define NAND_FLASH_PAGE_SIZE 0x800 /* 2KB */
#define NAND_FLASH_OOB_SIZE 0x40   /* 64B */

/* Address space mapping for atsama5d3 */
#define AT91C_BASE_DDRCS        0x20000000
#define AT91C_BASE_CS1          0x40000000
#define AT91C_BASE_CS2          0x50000000
#define AT91C_BASE_CS3          0x60000000
#define AT91C_BASE_NFC_CMD      0x70000000

/* NAND flash is mapped to CS3 */
#define NAND_BASE            AT91C_BASE_CS3

#define NAND_MASK_ALE        (1 << 21)
#define NAND_MASK_CLE        (1 << 22)
#define NAND_CMD  (*((volatile uint8_t *)(NAND_BASE | NAND_MASK_CLE)))
#define NAND_ADDR (*((volatile uint8_t *)(NAND_BASE | NAND_MASK_ALE)))
#define NAND_DATA (*((volatile uint8_t *)(NAND_BASE)))

/* Command set */
#define NAND_CMD_STATUS       0x70
#define NAND_CMD_READ1        0x00
#define NAND_CMD_READ2        0x30
#define NAND_CMD_READID       0x90
#define NAND_CMD_RESET        0xFF
#define NAND_CMD_ERASE1       0x60
#define NAND_CMD_ERASE2       0xD0
#define NAND_CMD_WRITE1       0x80
#define NAND_CMD_WRITE2       0x10

/* Small block */
#define NAND_CMD_READ_A0      0x00
#define NAND_CMD_READ_A1      0x01
#define NAND_CMD_READ_C       0x50
#define NAND_CMD_WRITE_A      0x00
#define NAND_CMD_WRITE_C      0x50


/* ONFI */
#define NAND_CMD_READ_ONFI  0xEC

/* Features set/get */
#define NAND_CMD_GET_FEATURES 0xEE
#define NAND_CMD_SET_FEATURES 0xEF

/* ONFI parameters and definitions */
#define ONFI_PARAMS_SIZE		256

#define PARAMS_POS_REVISION		4
#define		PARAMS_REVISION_1_0	(0x1 << 1)
#define		PARAMS_REVISION_2_0	(0x1 << 2)
#define		PARAMS_REVISION_2_1	(0x1 << 3)

#define PARAMS_POS_FEATURES		6
#define		PARAMS_FEATURE_BUSWIDTH		(0x1 << 0)
#define		PARAMS_FEATURE_EXTENDED_PARAM	(0x1 << 7)

#define PARAMS_POS_OPT_CMD		8
#define		PARAMS_OPT_CMD_SET_GET_FEATURES	(0x1 << 2)

#define PARAMS_POS_EXT_PARAM_PAGE_LEN	12
#define PARAMS_POS_PARAMETER_PAGE		14
#define PARAMS_POS_PAGESIZE		        80
#define PARAMS_POS_OOBSIZE		        84
#define PARAMS_POS_BLOCKSIZE		    92
#define PARAMS_POS_NBBLOCKS		        96
#define PARAMS_POS_ECC_BITS		        112

#define PARAMS_POS_TIMING_MODE	        129
#define		PARAMS_TIMING_MODE_0	    (1 << 0)
#define		PARAMS_TIMING_MODE_1	    (1 << 1)
#define		PARAMS_TIMING_MODE_2	    (1 << 2)
#define		PARAMS_TIMING_MODE_3	    (1 << 3)
#define		PARAMS_TIMING_MODE_4	    (1 << 4)
#define		PARAMS_TIMING_MODE_5	    (1 << 5)

#define PARAMS_POS_CRC		254

#define ONFI_CRC_BASE			0x4F4E

#define ONFI_MAX_SECTIONS		8

#define ONFI_SECTION_TYPE_0		0
#define ONFI_SECTION_TYPE_1		1
#define ONFI_SECTION_TYPE_2		2

/* Read access modes */
#define NAND_MODE_DATAPAGE      1
#define NAND_MODE_INFO          2
#define NAND_MODE_DATABLOCK    3

/*
#define LOOKUP_TABLE_ALPHA_OFFSET        0x14000
#define LOOKUP_TABLE_INDEX_OFFSET        0x10000
#define LOOKUP_TABLE_ALPHA_OFFSET_1024   0x20000
#define LOOKUP_TABLE_INDEX_OFFSET_1024   0x18000
*/

#define nand_flash_read ext_flash_read
#define nand_flash_write ext_flash_write
#define nand_flash_erase ext_flash_erase
#define nand_flash_unlock ext_flash_unlock
#define nand_flash_lock ext_flash_lock

#define MAX_ECC_BYTES 8


/* Manual division operation */
int division(uint32_t dividend,
		uint32_t divisor,
		uint32_t *quotient,
		uint32_t *remainder)
{
	uint32_t shift;
	uint32_t divisor_shift;
	uint32_t factor = 0;
	unsigned char end_flag = 0;

	if (!divisor)
		return 0xffffffff;

	if (dividend < divisor) {
		*quotient = 0;
		*remainder = dividend;
		return 0;
	}

	while (dividend >= divisor) {
		for (shift = 0, divisor_shift = divisor;
			dividend >= divisor_shift;
			divisor_shift <<= 1, shift++) {
			if (dividend - divisor_shift < divisor_shift) {
				factor += 1 << shift;
				dividend -= divisor_shift;
				end_flag = 1;
				break;
			}
		}

		if (end_flag)
			continue;

		factor += 1 << (shift - 1);
		dividend -= divisor_shift >> 1;
	}

	if (quotient)
		*quotient = factor;

	if (remainder)
		*remainder = dividend;

	return 0;
}

uint32_t div(uint32_t dividend, uint32_t divisor)
{
	uint32_t quotient = 0;
	uint32_t remainder = 0;
	int ret;

	ret = division(dividend, divisor, &quotient, &remainder);
	if (ret)
		return 0xffffffff;

	return quotient;
}

uint32_t mod(uint32_t dividend, uint32_t divisor)
{
	uint32_t quotient = 0;
	uint32_t remainder = 0;
	int ret;

	ret = division(dividend, divisor, &quotient, &remainder);
	if (ret)
		return 0xffffffff;

	return remainder;
}

/* Static variables to hold nand info */
static uint8_t nand_manif_id;
static uint8_t nand_dev_id;
static char nand_onfi_id[4];



struct nand_flash {
    uint16_t revision;
    uint16_t features;
    uint16_t ext_page_len;
    uint16_t parameter_page;

    uint32_t page_size;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t pages_per_block;
    uint32_t pages_per_device;
    uint32_t total_size;

    uint16_t bad_block_pos;
    uint16_t ecc_bytes;
    uint16_t eccpos[MAX_ECC_BYTES];
    uint16_t eccwordsize;

    uint32_t bus_width;
    uint32_t oob_size;
} nand_flash = { 0 };

static void nand_wait_ready(void)
{
    NAND_CMD = NAND_CMD_STATUS;
    while (!(NAND_DATA & 0x40));
}


static void nand_read_id(uint8_t *manif_id, uint8_t *dev_id)
{
    NAND_CMD = NAND_CMD_READID;
    NAND_ADDR = 0x00;
    *manif_id = NAND_DATA;
    *dev_id = NAND_DATA;
}

static void nand_reset(void)
{
    NAND_CMD = NAND_CMD_RESET;
    nand_wait_ready();
}


static void write_column_address(uint32_t col_address)
{
    NAND_ADDR = col_address & 0xFF;
    NAND_ADDR = (col_address >> 8) & 0xFF;
    NAND_ADDR = (col_address >> 16) & 0xFF;
}

static void write_row_address(uint32_t row_address)
{
    NAND_ADDR = row_address & 0xFF;
    NAND_ADDR = (row_address >> 8) & 0xFF;
    NAND_ADDR = (row_address >> 16) & 0xFF;
    NAND_ADDR = (row_address >> 24) & 0xFF;
}

static void nand_read_info(void)
{
    uint8_t onfi_data[ONFI_PARAMS_SIZE];
    uint32_t i;

    nand_reset();

    nand_read_id(&nand_manif_id, &nand_dev_id);
    NAND_CMD = NAND_CMD_READID;
    NAND_ADDR = 0x20;
    nand_onfi_id[0] = NAND_DATA;
    nand_onfi_id[1] = NAND_DATA;
    nand_onfi_id[2] = NAND_DATA;
    nand_onfi_id[3] = NAND_DATA;
    if (memcmp(nand_onfi_id, "ONFI", 4) != 0) {
        /* Fail: no ONFI support */
        asm("bkpt 0");
        return;
    }
    memset(&nand_flash, 0, sizeof(nand_flash));
    memset(nand_flash.eccpos, 0xFF, sizeof(nand_flash.eccpos));
    NAND_CMD = NAND_CMD_READ_ONFI;
    NAND_ADDR = 0x00;
    nand_wait_ready();
    NAND_CMD = NAND_CMD_READ1;
    for (i = 0; i < ONFI_PARAMS_SIZE; i++) {
        onfi_data[i] = NAND_DATA;
    }
    /* Store ONFI parameters in nand_flash struct */
    nand_flash.page_size = *(uint16_t *)(onfi_data + PARAMS_POS_PAGESIZE);
    nand_flash.pages_per_block = *(uint16_t *)(onfi_data + PARAMS_POS_BLOCKSIZE);
    nand_flash.block_size = nand_flash.page_size * nand_flash.pages_per_block;
    nand_flash.block_count = *(uint16_t *)(onfi_data + PARAMS_POS_NBBLOCKS);
    nand_flash.total_size = nand_flash.block_count * nand_flash.block_size;
    nand_flash.ecc_bytes = *(uint16_t *)(onfi_data + PARAMS_POS_ECC_BITS);
    nand_flash.bad_block_pos = (*(uint16_t *)(onfi_data + PARAMS_POS_FEATURES)) & 1;
    nand_flash.ext_page_len = *(uint16_t *)(onfi_data + PARAMS_POS_EXT_PARAM_PAGE_LEN);
    nand_flash.parameter_page = *(uint16_t *)(onfi_data + PARAMS_POS_PARAMETER_PAGE);
    nand_flash.pages_per_block = div(nand_flash.block_size, nand_flash.page_size);
    nand_flash.pages_per_device = nand_flash.pages_per_block * nand_flash.block_count;
    nand_flash.oob_size = *(uint16_t *)(onfi_data + PARAMS_POS_OOBSIZE);
    nand_flash.revision = *(uint16_t *)(onfi_data + PARAMS_POS_REVISION);
    nand_flash.features = *(uint16_t *)(onfi_data + PARAMS_POS_FEATURES);
    nand_flash.bus_width = (onfi_data[PARAMS_POS_FEATURES] & PARAMS_FEATURE_BUSWIDTH) ? 16 : 8;
    if (nand_flash.ecc_bytes <= MAX_ECC_BYTES) {
        for (int i = 0; i < nand_flash.ecc_bytes; i++) {
            nand_flash.eccpos[i] = *(uint16_t *)(onfi_data + PARAMS_POS_ECC_BITS + i * 2);
        }
    }
    if (nand_flash.page_size != NAND_FLASH_PAGE_SIZE) {
        /* Fail: unsupported page size */
        asm("bkpt 0");
    }
    if (nand_flash.oob_size != NAND_FLASH_OOB_SIZE) {
        /* Fail: unsupported oob size */
        asm("bkpt 0");
    }


}

static void set_col_addr(uint32_t col_address)
{
    uint32_t page_size = nand_flash.page_size;
    while (page_size > 0) {
        NAND_ADDR = col_address & 0xFF;
        col_address >>= 8;
        page_size >>= 8;
    }
}

static void set_row_addr(uint32_t row_address)
{
    uint32_t pages_per_device = nand_flash.pages_per_device;
    while (pages_per_device > 0) {
        NAND_ADDR = row_address & 0xFF;
        row_address >>= 8;
        pages_per_device >>= 8;
    }
}

static int nand_device_read(uint32_t row_address, uint8_t *data, int mode)
{
    uint32_t col_address = 0x00;
    uint32_t tot_len = 0;
    uint32_t page_size = nand_flash.page_size;
    uint32_t pages_per_device = nand_flash.pages_per_device;
    uint32_t i;

    if (mode == NAND_MODE_DATAPAGE) {
        tot_len = nand_flash.page_size;
    } else if (mode == NAND_MODE_INFO) {
        tot_len = nand_flash.oob_size;
        col_address = nand_flash.page_size;
    } else if (mode == NAND_MODE_DATABLOCK) {
        tot_len = nand_flash.block_size;
    } else {
        /* Fail: unknown mode */
        return -1;
    }
    NAND_CMD = NAND_CMD_READ1;

    set_col_addr(col_address);
    set_row_addr(row_address);

    NAND_CMD = NAND_CMD_READ2;
    nand_wait_ready();
    NAND_CMD = NAND_CMD_READ1;
    for (i = 0; i < tot_len; i++) {
        data[i] = NAND_DATA;
    }
    return 0;
}

static int nand_read_page(uint32_t block, uint32_t page, uint8_t *data)
{
    uint32_t row_address = block * nand_flash.pages_per_block + page;
    return nand_device_read(row_address, data, NAND_MODE_DATAPAGE);
}

static int nand_check_bad_block(uint32_t block)
{
    uint32_t row_address = block * nand_flash.pages_per_block;
    uint8_t oob[NAND_FLASH_OOB_SIZE];
    uint32_t page;
    for (page = 0; page < 2; page++) {
        nand_device_read(row_address + page, oob, NAND_MODE_INFO);
        if (oob[0] != 0xFF) {
            return -1;
        }
    }
    return 0;
}



int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uint8_t buffer_page[NAND_FLASH_PAGE_SIZE];
    uint32_t block = div(address, nand_flash.block_size); /* The block where the address falls in */
    uint32_t page = div(address, nand_flash.page_size); /* The page where the address falls in */
    uint32_t start_page_in_block = mod(page, nand_flash.pages_per_block); /* The start page within this block */
    uint32_t in_block_offset = mod(address, nand_flash.block_size); /* The offset of the address within the block */
    uint32_t remaining = nand_flash.block_size - in_block_offset; /* How many bytes remaining to read in the first block */
    uint32_t len_to_read = len;
    uint8_t *buffer = data;
    uint32_t i;
    int copy = 0;
    int ret;

    if (len < (int)nand_flash.page_size) {
        buffer = buffer_page;
        copy = 1;
        len_to_read = nand_flash.page_size;
    }

    while (len_to_read > 0) {
        uint32_t sz = len_to_read;
        uint32_t pages_to_read;
        if (sz > remaining)
            sz = remaining;

        do {
             ret = nand_check_bad_block(block);
            if (ret < 0) {
                /* Block is bad, skip it */
                block++;
            }
        } while (ret < 0);

        /* Amount of pages to be read from this block */
        pages_to_read = div((sz + nand_flash.page_size - 1), nand_flash.page_size);

        if (pages_to_read * nand_flash.page_size > remaining)
            pages_to_read--;

        /* Read (remaining) pages off a block */
        for (i = 0; i < pages_to_read; i++) {
            nand_read_page(block, start_page_in_block + i, buffer);
            len_to_read -= sz;
            buffer += sz;
        }
        /* The block is over, move to the next one */
        block++;
        start_page_in_block = 0;
        remaining = nand_flash.block_size;
    }
    if (copy) {
        uint32_t *dst = (uint32_t *)data;
        uint32_t *src = (uint32_t *)buffer_page;
        uint32_t tot_len = (uint32_t)len;
        for (i = 0; i < (tot_len >> 2); i++) {
            dst[i] = src[i];
        }
    }
    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    return 0;
}

/* SAMA5D3 NAND flash does not have an enable pin */
void ext_flash_unlock(void)
{
}

void ext_flash_lock(void)
{
}
#endif

void* hal_get_dts_address(void)
{
  return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
  return NULL; /* Not yet supported */
}

/* QSPI functions */
void qspi_init(uint32_t cpu_clock, uint32_t flash_freq)
{
}


void zynq_init(uint32_t cpu_clock)
{
}


/* public HAL functions */
void hal_init(void)
{
    nand_read_info();
}

void hal_prepare_boot(void)
{
}


int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}


int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    return 0;
}


