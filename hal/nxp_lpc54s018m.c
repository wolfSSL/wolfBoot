/* nxp_lpc54s018m.c
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
 *
 * NXP LPC54S018M HAL for wolfBoot
 *
 * The LPC54S018M has no internal NOR flash. All code executes from
 * on-package SPIFI QSPI flash (W25Q32JV, 4MB) mapped at 0x10000000.
 * Flash operations MUST run from RAM since XIP is disabled during
 * erase/write.
 *
 * This HAL uses bare-metal register access — no NXP SDK dependencies.
 */

#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"

/* -------------------------------------------------------------------------- */
/*  SPIFI controller registers (base 0x40080000)                              */
/* -------------------------------------------------------------------------- */
#define SPIFI_BASE          0x40080000
#define SPIFI_CTRL          (*(volatile uint32_t *)(SPIFI_BASE + 0x00))
#define SPIFI_CMD           (*(volatile uint32_t *)(SPIFI_BASE + 0x04))
#define SPIFI_ADDR          (*(volatile uint32_t *)(SPIFI_BASE + 0x08))
#define SPIFI_IDATA         (*(volatile uint32_t *)(SPIFI_BASE + 0x0C))
#define SPIFI_CLIMIT        (*(volatile uint32_t *)(SPIFI_BASE + 0x10))
#define SPIFI_DATA          (*(volatile uint32_t *)(SPIFI_BASE + 0x14))
#define SPIFI_MCMD          (*(volatile uint32_t *)(SPIFI_BASE + 0x18))
#define SPIFI_STAT          (*(volatile uint32_t *)(SPIFI_BASE + 0x1C))

/* STAT register bits */
#define SPIFI_STAT_MCINIT   (1 << 0)  /* Memory command init done */
#define SPIFI_STAT_CMD      (1 << 1)  /* Command active */
#define SPIFI_STAT_RESET    (1 << 4)  /* Reset in progress */

/* CMD register field positions */
#define SPIFI_CMD_DATALEN(n)    ((n) & 0x3FFF)
#define SPIFI_CMD_POLL          (1 << 14)
#define SPIFI_CMD_DOUT          (1 << 15)       /* 1=output, 0=input */
#define SPIFI_CMD_INTLEN(n)     (((n) & 7) << 16)
#define SPIFI_CMD_FIELDFORM(n)  (((n) & 3) << 19)
#define SPIFI_CMD_FRAMEFORM(n)  (((n) & 7) << 21)
#define SPIFI_CMD_OPCODE(n)     (((n) & 0xFF) << 24)

/* Frame/field format values */
#define FRAMEFORM_OPCODE_ONLY   1
#define FRAMEFORM_OPCODE_3ADDR  4
#define FIELDFORM_ALL_SERIAL    0
#define FIELDFORM_DATA_QUAD     2

/* W25Q32JV flash commands */
#define W25Q_CMD_WRITE_ENABLE   0x06
#define W25Q_CMD_READ_STATUS1   0x05
#define W25Q_CMD_PAGE_PROGRAM   0x02
#define W25Q_CMD_SECTOR_ERASE   0x20  /* 4KB sector erase */
#define W25Q_CMD_FAST_READ_QUAD 0x6B  /* Quad output fast read */

/* W25Q status register bits */
#define W25Q_STATUS_BUSY        0x01

/* Flash geometry */
#define FLASH_PAGE_SIZE         0x100  /* 256 bytes */
#define SPIFI_FLASH_BASE        0x10000000

static uint8_t flash_page_cache[FLASH_PAGE_SIZE];

/* Pre-computed SPIFI CMD register values for each flash operation */
#define CMD_WRITE_ENABLE \
    (SPIFI_CMD_DOUT | SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_ONLY) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_WRITE_ENABLE))

#define CMD_READ_STATUS \
    (SPIFI_CMD_DATALEN(1) | SPIFI_CMD_POLL | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_ONLY) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_READ_STATUS1))

#define CMD_SECTOR_ERASE \
    (SPIFI_CMD_DOUT | SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_SECTOR_ERASE))

#define CMD_PAGE_PROGRAM \
    (SPIFI_CMD_DATALEN(FLASH_PAGE_SIZE) | SPIFI_CMD_DOUT | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_PAGE_PROGRAM))

/* Memory-mode command: quad output fast read with 1 intermediate (dummy) byte */
#define MCMD_READ_QUAD \
    (SPIFI_CMD_INTLEN(1) | SPIFI_CMD_FIELDFORM(FIELDFORM_DATA_QUAD) | \
     SPIFI_CMD_FRAMEFORM(FRAMEFORM_OPCODE_3ADDR) | \
     SPIFI_CMD_OPCODE(W25Q_CMD_FAST_READ_QUAD))

#ifdef NVM_FLASH_WRITEONCE
#   error "wolfBoot LPC54S018M HAL: WRITEONCE not supported on SPIFI flash."
#endif

/* -------------------------------------------------------------------------- */
/*  Boot-time initialization (runs from flash / XIP)                          */
/* -------------------------------------------------------------------------- */

#ifdef __WOLFBOOT

/* Assert hook (in case any remaining SDK code uses assert) */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    while (1)
        ;
}

void hal_init(void)
{
    /* The boot ROM has already configured basic clocks and SPIFI for XIP.
     * We must NOT reconfigure clocks or SPIFI from flash (XIP) because
     * changing the clock source or SPIFI controller while executing from
     * SPIFI flash will cause an instruction fetch fault.
     *
     * Clock and SPIFI reconfiguration can only be done from RAM functions.
     * The flash erase/write paths (all RAMFUNCTION) handle SPIFI mode
     * switching as needed.
     */
}

void hal_prepare_boot(void)
{
}

#endif /* __WOLFBOOT */

/* -------------------------------------------------------------------------- */
/*  SPIFI flash helper functions — all MUST run from RAM                      */
/* -------------------------------------------------------------------------- */

/*
 * Issue a SPIFI command. Exits memory mode if active, waits for ready,
 * then writes CMD register. Entirely register-based — no SDK calls.
 */
static void RAMFUNCTION spifi_set_cmd(uint32_t cmd_val)
{
    /* If in memory mode (MCINIT set), reset to exit */
    if (SPIFI_STAT & SPIFI_STAT_MCINIT) {
        SPIFI_STAT = SPIFI_STAT_RESET;
        while (SPIFI_STAT & SPIFI_STAT_RESET)
            ;
    }

    /* Wait for any active command to complete */
    while (SPIFI_STAT & SPIFI_STAT_CMD)
        ;

    SPIFI_CMD = cmd_val;
}

/*
 * Enter memory-mapped (XIP) mode using quad output fast read.
 */
static void RAMFUNCTION spifi_enter_memmode(void)
{
    /* Wait for any active command */
    while (SPIFI_STAT & SPIFI_STAT_CMD)
        ;

    SPIFI_MCMD = MCMD_READ_QUAD;

    /* Wait for memory mode to initialize */
    while (!(SPIFI_STAT & SPIFI_STAT_MCINIT))
        ;
}

static void RAMFUNCTION spifi_write_enable(void)
{
    spifi_set_cmd(CMD_WRITE_ENABLE);
}

static void RAMFUNCTION spifi_wait_busy(void)
{
    uint8_t status;

    /* Issue read-status command in poll mode */
    spifi_set_cmd(CMD_READ_STATUS);
    do {
        status = *(volatile uint8_t *)&SPIFI_DATA;
    } while (status & W25Q_STATUS_BUSY);
}

/*
 * Flash write — 256-byte page program via SPIFI
 *
 * Handles unaligned writes by decomposing into page-aligned operations.
 * All flash data goes through a RAM page cache to ensure proper alignment.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int idx = 0;
    uint32_t page_address;
    uint32_t offset;
    int size;
    int i;

    while (idx < len) {
        page_address = ((address + idx) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
        if ((address + idx) > page_address)
            offset = (address + idx) - page_address;
        else
            offset = 0;
        size = FLASH_PAGE_SIZE - offset;
        if (size > (len - idx))
            size = len - idx;
        if (size > 0) {
            /* Read current page content (flash is memory-mapped) */
            memcpy(flash_page_cache, (void *)(uintptr_t)page_address,
                   FLASH_PAGE_SIZE);
            memcpy(flash_page_cache + offset, data + idx, size);

            /* Write enable */
            spifi_write_enable();

            /* Set address and issue page program command */
            SPIFI_ADDR = page_address - SPIFI_FLASH_BASE;
            spifi_set_cmd(CMD_PAGE_PROGRAM);

            /* Write page data as 32-bit words */
            for (i = 0; i < FLASH_PAGE_SIZE; i += 4) {
                uint32_t word;
                memcpy(&word, &flash_page_cache[i], 4);
                SPIFI_DATA = word;
            }

            /* Wait for program to complete */
            spifi_wait_busy();

            /* Re-enter memory mode */
            spifi_enter_memmode();
        }
        idx += size;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/*
 * Flash erase — 4KB sector erase via SPIFI
 *
 * Address must be aligned to WOLFBOOT_SECTOR_SIZE (4KB).
 * Length must be a multiple of WOLFBOOT_SECTOR_SIZE.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + len;

    while (address < end) {
        /* Write enable before each sector erase */
        spifi_write_enable();

        /* Set address and issue sector erase command */
        SPIFI_ADDR = address - SPIFI_FLASH_BASE;
        spifi_set_cmd(CMD_SECTOR_ERASE);

        /* Wait for erase to complete */
        spifi_wait_busy();

        address += WOLFBOOT_SECTOR_SIZE;
    }

    /* Re-enter memory mode */
    spifi_enter_memmode();

    return 0;
}
