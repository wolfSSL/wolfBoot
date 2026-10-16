/* ti_am64x_r5.c
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
#include <string.h>
#include <target.h>
#include "image.h"
#include "loader.h"

#include <wolfssl/wolfcrypt/types.h>

#include "DebugP.h"
#include "board/flash.h"
#include "drivers/sciclient.h"
#include "drivers/ospi.h"
#include "drivers/bootloader/soc/am64x_am243x/bootloader_soc.h"
#include "kernel/dpl/ClockP.h"
#include "security/security_common/drivers/crypto/rng/rng.h"
#include "security/security_common/drivers/crypto/sa2ul/sa2ul.h"

void System_init(void);
void Drivers_open(void);
int32_t Board_driversOpen(void);
extern OSPI_Handle gOspiHandle[1];
extern Flash_Handle gFlashHandle[1];

#ifndef __WOLFBOOT
#include <stdio.h>
void putchar__(char character);
void putchar__(char character)
{
    /* Output to CCS console */
    putchar(character);
    /* Output to UART console */
    if (character == '\n')
        DebugP_uartLogWriterPutChar('\r');
    DebugP_uartLogWriterPutChar(character);
}
#endif

static void flashFixUpOspiBoot(OSPI_Handle oHandle)
{
    int32_t status = SystemP_FAILURE;
    OSPI_setProtocol(oHandle, OSPI_NOR_PROTOCOL(8,8,8,1));
    OSPI_enableDDR(oHandle);
    OSPI_setDualOpCodeMode(oHandle);

    /* Do a soft reset of the OSPI flash */
    OSPI_WriteCmdParams wrParams;

    OSPI_WriteCmdParams_init(&wrParams);
    wrParams.cmd = 0x66;
    status = OSPI_writeCmd(oHandle, &wrParams);
    if(status == SystemP_SUCCESS)
    {
        wrParams.cmd = 0x99;
        status = OSPI_writeCmd(oHandle, &wrParams);
    }
    /* Wait for the flash to reset */
    ClockP_usleep(100);

    OSPI_enableSDR(oHandle);
    OSPI_clearDualOpCodeMode(oHandle);
    OSPI_setProtocol(oHandle, OSPI_NOR_PROTOCOL(1,1,1,0));
}

void hal_init(void)
{
#ifdef __WOLFBOOT

#if 0
    asm(
        "_debug_loop_start:\n"
        "  b _debug_loop_start\n"
    );
#endif

    Sciclient_waitForBootNotification();

    if (!Bootloader_socIsMCUResetIsoEnabled())
    {
        /* Update devGrp to ALL to initialize MCU domain when reset isolation is
        not enabled */
        Sciclient_BoardCfgPrms_t boardCfgPrms_pm =
            {
                .boardConfigLow = (uint32_t)0,
                .boardConfigHigh = 0,
                .boardConfigSize = 0,
                .devGrp = DEVGRP_ALL,
            };

        (void)Sciclient_boardCfgPm(&boardCfgPrms_pm);

        Sciclient_BoardCfgPrms_t boardCfgPrms_rm =
        {
            .boardConfigLow = (uint32_t)0,
            .boardConfigHigh = 0,
            .boardConfigSize = 0,
            .devGrp = DEVGRP_ALL,
        };

        (void)Sciclient_boardCfgRm(&boardCfgPrms_rm);

        Bootloader_enableMCUPLL();
    }

    System_init();
    Bootloader_socOpenFirewalls();
    Drivers_open();

    uart_write("ti_am64x_r5 init\n", 14);
    Sciclient_getVersionCheck(1);
#else

#if 0
    asm(
        "_debug_loop_start:\n"
        "  b _debug_loop_start\n"
    );
#endif

    System_init();
    Drivers_open();
#endif /* __WOLFBOOT */

    flashFixUpOspiBoot(gOspiHandle[0]);
    Board_driversOpen();
    OSPI_enableDacMode(gOspiHandle[0]);
}

#ifdef __WOLFBOOT
/* Assert hook needed by SDK assert() macro. */
void __assert_func(const char *a, int b, const char *c, const char *d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    while (1) {
    }
}

void hal_prepare_boot(void)
{

}
#endif

#define PAGE_SIZE 256

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int status = 0;
    uint32_t page_addr;
    uint32_t offset;
    uint32_t chunk;
    uint8_t page_buf[PAGE_SIZE];

    while (len > 0) {
        page_addr = address & ~(PAGE_SIZE - 1);
        offset = address - page_addr;
        chunk = PAGE_SIZE - offset;
        if ((uint32_t)len < chunk)
            chunk = (uint32_t)len;

        memcpy(page_buf, (void *)page_addr, PAGE_SIZE);
        memcpy(page_buf + offset, data, chunk);

        OSPI_disableDacMode(gOspiHandle[0]);
        status |= Flash_write(gFlashHandle[0], page_addr - ARCH_FLASH_OFFSET, page_buf, PAGE_SIZE);
        OSPI_enableDacMode(gOspiHandle[0]);

        address += chunk;
        data += chunk;
        len -= (int)chunk;
    }

    return status;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int status = 0;

    if (len == 0)
        return 0;

    address -= ARCH_FLASH_OFFSET;

    OSPI_disableDacMode(gOspiHandle[0]);
    while (len > 0) {
        uint32_t blk, page;
        uint32_t block = address & ~(WOLFBOOT_SECTOR_SIZE - 1);
        uint32_t offset = address - block;
        uint32_t chunk = WOLFBOOT_SECTOR_SIZE - offset;
        if ((uint32_t)len < chunk)
            chunk = (uint32_t)len;
        status = Flash_offsetToBlkPage(gFlashHandle[0], address, &blk, &page);
        if (status != 0)
            break;
        status = Flash_eraseBlk(gFlashHandle[0], blk);
        if (status != 0)
            break;
        address += chunk;
        len -= (int)chunk;
    }
    OSPI_enableDacMode(gOspiHandle[0]);

    return status;
}

#ifdef WOLFCRYPT_SECURE_MODE
static RNG_Handle rngHandle = NULL;

void hal_trng_init(void)
{
    RNG_Handle handle = NULL;
    if (gRngConfig[0].attrs->isOpen == 0) {
        SA2UL_engineEnable(CSL_CP_ACE_CMD_STATUS_TRNG_EN_MASK);
        handle = RNG_open(0);
        if (handle != NULL) {
            if (RNG_setup(handle) == RNG_RETURN_SUCCESS) {
                rngHandle = handle;
            }
            else {
                RNG_close(handle);
            }
        }
    }
    else {
        /* already opened -- use existing handle */
        rngHandle = (RNG_Handle)&gRngConfig[0];
    }
}

void hal_trng_fini(void)
{
}

#define RNG_NUM_DWORDS  (4u)
int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    if (out == NULL && len != 0)
        return -1;

    while (len) {
        uint32_t random[RNG_NUM_DWORDS];
        uint8_t *ptr = (uint8_t *)random;
        int copy_len;
        if (RNG_read(rngHandle, random) != RNG_RETURN_SUCCESS)
            return -1;
        copy_len = RNG_NUM_DWORDS * 4;
        if (len < copy_len)
            copy_len = len;
        XMEMCPY(out, ptr, copy_len);
        out += copy_len;
        len -= copy_len;
    }

    return 0;
}
#endif /* WOLFCRYPT_SECURE_MODE */


#ifdef DEBUG_UART
void uart_init(void)
{

}

void DebugP_uartLogWriterPutLine(uint8_t *buf, uint16_t num_bytes);

void uart_write(const char *buf, unsigned int sz)
{
    const char *line;
    unsigned int line_sz;

    while (sz > 0)
    {
        line = memchr(buf, '\n', sz);
        if (line == NULL) {
            DebugP_uartLogWriterPutLine((uint8_t *)buf, (uint16_t)sz);
            break;
        }
        line_sz = (unsigned int)(line - buf);
        if (line_sz > sz - 1U) {
            line_sz = sz - 1U;
        }
        if (line_sz > 0)
            DebugP_uartLogWriterPutLine((uint8_t *)buf, (uint16_t)line_sz);
        DebugP_uartLogWriterPutLine((uint8_t *)"\r\n", (uint16_t)2U);
        buf = line + 1;
        sz -= line_sz + 1U;
    }
}
#endif


#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/CacheP.h>

#ifdef __WOLFBOOT
void do_boot(const uint32_t *app_offset)
{
    HwiP_disable();
    CacheP_wbInvAll(CacheP_TYPE_ALL);
    Bootloader_socCpuResetReleaseSelf();
    while (1) {
        asm("nop");
    }
}
#endif /* __WOLFBOOT */
