/* ti_am64x.c
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

// static flash_config_t pflash;
// static const uint32_t pflash_page_size = 512U;
// uint32_t SystemCoreClock; /* set in clock_config.c */

// static void hal_flash_fix_ecc(void)
// {
//     uint8_t page_buf[512];
//     uint32_t addr;
//     uint32_t start = WOLFBOOT_PARTITION_BOOT_ADDRESS;
//     uint32_t end   = WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE;

//     memset(page_buf, 0xFF, sizeof(page_buf));

//     for (addr = start; addr < end; addr += pflash_page_size) {
//         if (FLASH_VerifyErase(&pflash, addr, pflash_page_size)
//                 == kStatus_FLASH_Success) {
//             FLASH_Program(&pflash, addr, page_buf, pflash_page_size);
//         }
//     }
// }

#include "DebugP.h"
#include "drivers/sciclient.h"
#include "drivers/bootloader/soc/am64x_am243x/bootloader_soc.h"
#include "kernel/dpl/ClockP.h"
#include "security/security_common/drivers/crypto/rng/rng.h"
#include "security/security_common/drivers/crypto/sa2ul/sa2ul.h"

void System_init(void);
void Drivers_open(void);
void Drivers_udmaOpen(void);
void Drivers_uartOpen(void);
extern ClockP_Config gClockConfig;

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

void hal_init(void)
{
#ifdef __WOLFBOOT

//    asm(
//        "_debug_loop_start:\n"
//        "  b _debug_loop_start\n"
//    );

    Sciclient_waitForBootNotification();

    /* Warm Reset Workaround to prevent CPSW register lockup */
    if (!Bootloader_socIsMCUResetIsoEnabled())
    {
        Bootloader_socResetWorkaround();
    }

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

        /* Enable MCU PLL. MCU PLL will not be enabled by DMSC when devGrp is set
        to Main in boardCfg */
        Bootloader_enableMCUPLL();
    }

    System_init();

    Bootloader_socOpenFirewalls();
    Bootloader_socNotifyFirewallOpen();

    Drivers_udmaOpen();
    Drivers_uartOpen();

    Sciclient_getVersionCheck(1);

#else
    gClockConfig.timerBaseAddr = 0x0e080000UL; /* fixes sdk syscfg bug */
    System_init();
    Drivers_open();
#endif /* __WOLFBOOT */

#ifdef __WOLFBOOT

# ifdef DEBUG_UART
    uart_init();
    uart_write("ti_am64x init\n", 14);
# endif

#endif /* __WOLFBOOT */

#if defined(__WOLFBOOT)
    // memset(&pflash, 0, sizeof(pflash));
    // FLASH_Init(&pflash);
    // hal_flash_fix_ecc();
#endif
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

#ifdef EXT_FLASH
int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    XMEMCPY((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */


#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/CacheP.h>

#ifdef __WOLFBOOT
void do_boot(const uint32_t *app_offset)
{
    HwiP_disable();
    CacheP_wbInvAll(CacheP_TYPE_ALL);
    Bootloader_socCpuResetReleaseSelf();
}
#endif /* __WOLFBOOT */
