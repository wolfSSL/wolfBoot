/* sta1385.c
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#include <stdint.h>
#include <string.h>
#include <image.h>

#include "trace.h"
#include "utils.h"
#include "sdmmc.h"

static struct t_mmc_ctx *mmc;
static uint8_t block[MMC_BLOCK_SIZE];
static uint32_t cached_block;
static bool block_in_cache = false;

static bool is_sector_in_cache(uint32_t sector)
{
	return block_in_cache && (cached_block == sector);
}

static void mark_sector_as_cached(uint32_t sector)
{
	cached_block = sector;
	block_in_cache = true;
}

static void fill_sector_cache(uint32_t sector)
{
	uint32_t read;

	if (!is_sector_in_cache(sector)) {
		read = sdmmc_read(mmc, sector, &block[0], sizeof(block));
		TRACE_ASSERT(read == MMC_BLOCK_SIZE);
		mark_sector_as_cached(sector);
	}
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
	uint32_t sector, skip, read = 0, to_read;

	TRACE_INFO("%s: address %08x, data %08x, len %d\n", __func__,
		  (uint32_t)address, (uint32_t)data, len);
	sector = address / MMC_BLOCK_SIZE;
	skip = address % MMC_BLOCK_SIZE;
	if (skip) {
		fill_sector_cache(sector);
		read = MMC_BLOCK_SIZE - skip;
		if ((uint32_t)len < read)
			read = len;
		memcpy(data, &block[skip], read);
		to_read = len - read;
		if (to_read) {
			if (to_read <= MMC_BLOCK_SIZE) {
				fill_sector_cache(sector + 1);
				memcpy(data + read, &block[0], to_read);
				read += to_read;
			}
			else {
				read += sdmmc_read(mmc, sector + 1, data + read,
						   to_read);
			}
		}
	}
	else {
		if ((uint32_t)len <= MMC_BLOCK_SIZE) {
			fill_sector_cache(sector);
			memcpy(data, &block[0], len);
			read = len;
		}
		else {
			read = sdmmc_read(mmc, sector, data, len);
		}
	}
	return read;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
	TRACE_ERR("%s: address %08x, len %d\n", __func__, (uint32_t)address, len);
	/* TODO loop if MMC_TRANSFERT_MAX_SIZE is reached */
	TRACE_ASSERT((uint32_t)len <= MMC_TRANSFERT_MAX_SIZE);
	return sdmmc_write(mmc, address / MMC_BLOCK_SIZE, (uint8_t *)data, len);
}

void RAMFUNCTION ext_flash_unlock(void)
{
}

void RAMFUNCTION ext_flash_lock(void)
{
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
	/* TODO call func if exists */
	return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
	/* TODO call func */
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
	/* TODO call func if exists */
	return 0;
}

void hal_init(void)
{
	mmc = sdmmc_init(SDMMC1_PORT, false);
}

void hal_prepare_boot(void)
{
}
