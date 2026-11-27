/* aurix_tc4xx.c
 *
 * Copyright (C) 2014-2026 wolfSSL Inc.
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
 * along with wolfBoot.  If not, see <http://www.gnu.org/licenses/>.
 */

/* wolfBoot HAL for the Infineon AURIX TC4xx host and CSRM cores.
 */

#include <stdint.h>
#include <string.h>

/* wolfBoot headers */
#include "hal.h"
#include "image.h"  /* for RAMFUNCTION */
#include "loader.h" /* for wolfBoot_panic */
#include "printf.h"

/* wolfLLD headers */
#include "tc4/tc4.h"
#include "tc4/tc4_clock.h"
#include "tc4/tc4_dmu.h"
#include "tc4/tc4_flash.h"
#include "tc4/tc4_uart.h"
#include "tc4/tc4_wtu.h"

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

/* wolfHSM headers */
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_error.h"
/* wolfHSM AURIX TC4xx port headers */
#include "tchsm_topology.h"
#include "tchsm_client.h"
#include "tchsm_notify.h"
#include "tchsm_spr_apu.h"

/* wolfHSM client ID for the HSM server.
 * Align with whnvmtool provisioning. */
#ifndef WOLFBOOT_WOLFHSM_CLIENT_ID
#error \
    "WOLFBOOT_WOLFHSM_CLIENT_ID is not defined. Set WOLFHSM_CLIENT_ID in your .config or on the make command line."
#endif

/* HAL symbols exported for wolfBoot wolfHSM client mode. */
whClientContext hsmClientCtx = {0};
/* The server hashes host flash by address. */
const int hsmDevIdHash   = WH_DEV_ID_DMA;
const int hsmDevIdPubKey = WH_DEV_ID;
/* Verify public key stored in wolfHSM NVM. */
const int hsmKeyIdPubKey = 0xFF;
#ifdef EXT_ENCRYPT
#error "AURIX TC4xx does not support firmware encryption with wolfHSM (yet)"
#endif

_Static_assert(WOLFBOOT_WOLFHSM_CLIENT_ID == TCHSM_HSMHOST_CLIENT_APP0,
               "WOLFHSM_CLIENT_ID must match the port's APP0 client ID");

#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */

#ifdef TARGET_aurix_tc4xx_csrm
#define TC4_PROG_CHUNK_SIZE TC4_PFLASH_PAGE_SIZE
#else
#define TC4_PROG_CHUNK_SIZE TC4_PFLASH_BURST_SIZE
#endif

/* GP_BKALLOC bit that gives the CSRM PFLASH bank to the CSRM */
#define TC4_BKALLOC_CSRMPF (1u << 18)

#define WOLFBOOT_AURIX_RESET_REASON (0x5742) /* "WB" */

/* Helper macros for the base address of the page or sector containing addr */
#define GET_PAGE_ADDR(addr) ((uintptr_t)(addr) & ~(TC4_PFLASH_PAGE_SIZE - 1))
#define GET_SECTOR_ADDR(addr) ((uintptr_t)(addr) & ~(WOLFBOOT_SECTOR_SIZE - 1))

_Static_assert(WOLFBOOT_SECTOR_SIZE == TC4_PFLASH_SECTOR_SIZE,
               "WOLFBOOT_SECTOR_SIZE must be the PFLASH erase sector size");

/* RAM buffer holding one flash sector for read-modify-write operations */
static uint32_t sectorBuffer[WOLFBOOT_SECTOR_SIZE / sizeof(uint32_t)];

/* Read an entire sector into sectorBuffer, substituting the erased-byte
 * value for erased pages so no erased cell is ever read. */
static void RAMFUNCTION cacheSector(uint32_t sectorAddress)
{
    uint32_t page;
    for (page = 0; page < WOLFBOOT_SECTOR_SIZE; page += TC4_PFLASH_PAGE_SIZE) {
        uint32_t* dst    = sectorBuffer + (page / sizeof(uint32_t));
        int       erased = tc4_flash_IsPageErased(sectorAddress + page);

        if (erased < 0) {
            wolfBoot_panic();
        }
        else if (erased == 1) {
            uint32_t i;
            for (i = 0; i < TC4_PFLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
                dst[i] = FLASH_WORD_ERASED;
            }
        }
        else {
            tc4_flash_Read(sectorAddress + page, (uint8_t*)dst,
                           TC4_PFLASH_PAGE_SIZE);
        }
    }
}

/* Program sectorBuffer back into an erased sector, chunk by chunk
 * (bursts on the host interface, single pages on the CSRM) */
static void RAMFUNCTION programCachedSector(uint32_t sectorAddress)
{
    uint32_t off;
    for (off = 0; off < WOLFBOOT_SECTOR_SIZE; off += TC4_PROG_CHUNK_SIZE) {
        if (tc4_flash_Program(sectorAddress + off,
                              sectorBuffer + (off / sizeof(uint32_t)),
                              TC4_PROG_CHUNK_SIZE) != 0) {
            wolfBoot_panic();
        }
    }
}

/* Program unaligned data into erased flash, page by page */
static int RAMFUNCTION programBytesToErasedFlash(uint32_t       address,
                                                 const uint8_t* data, int size)
{
    uint32_t pageBuffer[TC4_PFLASH_PAGE_SIZE / sizeof(uint32_t)];
    uint32_t pageAddress = GET_PAGE_ADDR(address);
    uint32_t offset      = address % TC4_PFLASH_PAGE_SIZE;

    while (size > 0) {
        uint32_t toWrite = TC4_PFLASH_PAGE_SIZE - offset;
        uint32_t i;

        if (toWrite > (uint32_t)size) {
            toWrite = (uint32_t)size;
        }

        for (i = 0; i < TC4_PFLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
            pageBuffer[i] = FLASH_WORD_ERASED;
        }
        memcpy((uint8_t*)pageBuffer + offset, data, toWrite);

        if (tc4_flash_Program(pageAddress, pageBuffer,
                              TC4_PFLASH_PAGE_SIZE) != 0) {
            return -1;
        }

        size -= toWrite;
        data += toWrite;
        address += toWrite;
        pageAddress = GET_PAGE_ADDR(address);
        offset      = address % TC4_PFLASH_PAGE_SIZE;
    }
    return 0;
}

#ifdef UART_FLASH
#error "AURIX TC4xx does not support UART_FLASH"
#endif

#ifdef DEBUG_UART

#define TC4_UART_BAUD (115200u)

/* ASCLIN0 on P14.0 (TX) and P14.1 (RX). The CSRM may run before the host
 * has set up the clock tree, so the divider follows the live fASCLINF. */
static tc4_uartpin_t tc4Uart = {
    .asclin    = 0u,
    .txport    = 14u,
    .txpin     = 0u,
    .txalt     = 2u,
    .rxport    = 14u,
    .rxpin     = 1u,
    .rxsel     = 0u,
};

int  uart_tx(const uint8_t c);
void uart_init(void);
void uart_write(const char* buf, unsigned int sz);

static int tc4UartReady = 0;

void uart_init(void)
{
    tc4UartReady =
        (tc4_uart_SetBaud(&tc4Uart, tc4_clock_GetAsclinFastFrequency(),
                          TC4_UART_BAUD) == 0) &&
        (tc4_uart_Init(&tc4Uart) == 0);
}

int uart_tx(const uint8_t c)
{
    if (!tc4UartReady) {
        return 0;
    }
    /* A stalled transmitter takes the console down after one timeout */
    if (tc4_uart_Write8(&tc4Uart, c) != 0) {
        tc4UartReady = 0;
        return 0;
    }
    return 1;
}

void uart_write(const char* buf, unsigned int sz)
{
    while (sz > 0) {
        if (*buf == '\n') {
            (void)uart_tx('\r');
        }
        (void)uart_tx(*buf++);
        sz--;
    }
}

#endif /* DEBUG_UART */

/*
 * wolfBoot HAL entry points
 */

void hal_init(void)
{
#ifdef TARGET_aurix_tc4xx_csrm
    if (tc4_wtu_Disable(TC4_WTU_WDTSEC) != 0) {
        wolfBoot_panic();
    }

    /* The CSRM PFLASH bank must be allocated to the CSRM or the CSCI
     * command writes will fail silently */
    if ((TC4_REG32(TC4_DMU_GP_BKALLOC) & TC4_BKALLOC_CSRMPF) == 0u) {
        wolfBoot_panic();
    }
#else
    if ((tc4_wtu_Disable(TC4_WTU_WDTCPU(0u)) != 0) ||
        (tc4_wtu_Disable(TC4_WTU_WDTSYS) != 0)) {
        wolfBoot_panic();
    }

    /* Refuse to run if a host PFLASH bank has been reallocated to CSRM. */
    if ((TC4_REG32(TC4_DMU_GP_BKALLOC) & 0xFu) != 0u) {
        wolfBoot_panic();
    }
#endif

#ifdef DEBUG_UART
#ifdef TARGET_aurix_tc4xx_csrm
    /* Delay to allow host-side unlock of UART during parallel boot */
    {
        volatile uint32_t i;
        for (i = 0; i < 100000u; i++) {
        }
    }
#endif
    uart_init();
#ifdef TARGET_aurix_tc4xx_csrm
    wolfBoot_printf("Hello from TC4xx wolfBoot on CSRM: V%d\n",
                    WOLFBOOT_VERSION);
#else
    wolfBoot_printf("Hello from TC4xx wolfBoot on TriCore CPU0: V%d\n",
                    WOLFBOOT_VERSION);
#endif
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    /* Final print, then drain so the clock switch below cannot corrupt
     * in-flight characters */
    wolfBoot_printf("hal_prepare_boot\n");
    if (tc4UartReady) {
        (void)tc4_uart_BlockOnTC(&tc4Uart);
    }
#endif

#ifndef TARGET_aurix_tc4xx_csrm
    /* The application starts on the backup clock; the PLLs stay powered */
    if (tc4_clock_SwitchToBackup() != 0) {
        wolfBoot_panic();
    }
#endif
}

void do_boot(const uint32_t* app_offset)
{
    __asm volatile("ji %0" ::"a"(app_offset));
}

void RAMFUNCTION arch_reboot(void)
{
    (void)WOLFBOOT_AURIX_RESET_REASON;
#ifdef TARGET_aurix_tc4xx_csrm
    /* No reboot impl needed on CSRM yet */
    while (1) {
    }
#else
    tc4_TriggerSwReset();
#endif
}

/*
 * Flash HAL. Addresses arrive in the cached (0x8...) segment from the
 * partition configuration. All command sequences convert to the
 * non-cached alias internally.
 */

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t* data, int size)
{
    int      ret               = 0;
    uint32_t currentAddress    = address;
    int      remainingSize     = size;
    int      bytesWrittenTotal = 0;

    while (remainingSize > 0) {
        uint32_t currentSectorAddress = GET_SECTOR_ADDR(currentAddress);
        uint32_t offsetInSector       = currentAddress - currentSectorAddress;
        uint32_t bytesInThisSector    = WOLFBOOT_SECTOR_SIZE - offsetInSector;
        uint32_t page;
        int      needsSectorRmw = 0;

        if (bytesInThisSector > (uint32_t)remainingSize) {
            bytesInThisSector = remainingSize;
        }

        /* If any affected page already has data, read-modify-write the
         * whole sector */
        const uint32_t startPage = GET_PAGE_ADDR(currentAddress);
        const uint32_t endPage =
            GET_PAGE_ADDR(currentAddress + bytesInThisSector - 1);
        for (page = startPage; page <= endPage; page += TC4_PFLASH_PAGE_SIZE) {
            int erased = tc4_flash_IsPageErased(page);
            if (erased < 0) {
                return -1;
            }
            if (erased == 0) {
                needsSectorRmw = 1;
                break;
            }
        }

        if (needsSectorRmw) {
            cacheSector(currentSectorAddress);

            ret = hal_flash_erase(currentSectorAddress, WOLFBOOT_SECTOR_SIZE);
            if (ret != 0) {
                break;
            }

            memcpy((uint8_t*)sectorBuffer + offsetInSector,
                   data + bytesWrittenTotal, bytesInThisSector);

            programCachedSector(currentSectorAddress);
        }
        else {
            ret = programBytesToErasedFlash(
                currentAddress, data + bytesWrittenTotal, bytesInThisSector);
            if (ret != 0) {
                break;
            }
        }

        bytesWrittenTotal += bytesInThisSector;
        currentAddress += bytesInThisSector;
        remainingSize -= bytesInThisSector;
    }

    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t currentSectorAddr;
    uint32_t startSectorAddr;
    uint32_t endAddress;
    uint32_t endSectorAddr;
    int      ret = 0;

    if (len <= 0) {
        return 0;
    }

    startSectorAddr = GET_SECTOR_ADDR(address);
    endAddress      = address + len - 1;
    endSectorAddr   = GET_SECTOR_ADDR(endAddress);

    for (currentSectorAddr = startSectorAddr;
         currentSectorAddr <= endSectorAddr;
         currentSectorAddr += WOLFBOOT_SECTOR_SIZE) {

        const int isFirstSector  = (currentSectorAddr == startSectorAddr);
        const int isLastSector   = (currentSectorAddr == endSectorAddr);
        const int isPartialStart = isFirstSector && (address > startSectorAddr);
        const int isPartialEnd =
            isLastSector &&
            (endAddress < (endSectorAddr + WOLFBOOT_SECTOR_SIZE - 1));

        if (isPartialStart || isPartialEnd) {
            /* Partial sector: read-modify-write with the target range
             * filled with the erased value */
            uint32_t eraseStartOffset =
                isPartialStart ? (address - currentSectorAddr) : 0;
            uint32_t eraseEndOffset = isPartialEnd
                                          ? (endAddress - currentSectorAddr)
                                          : (WOLFBOOT_SECTOR_SIZE - 1);
            uint32_t eraseLen       = eraseEndOffset - eraseStartOffset + 1;
            uint32_t i;

            cacheSector(currentSectorAddr);

            for (i = 0; i < eraseLen; i++) {
                ((uint8_t*)sectorBuffer)[eraseStartOffset + i] =
                    FLASH_BYTE_ERASED;
            }

            if (tc4_flash_EraseSector(currentSectorAddr) != 0) {
                ret = -1;
                break;
            }

            programCachedSector(currentSectorAddr);
        }
        else {
            if (tc4_flash_EraseSector(currentSectorAddr) != 0) {
                ret = -1;
                break;
            }
        }
    }

    return ret;
}

void RAMFUNCTION hal_flash_unlock(void) {}

void RAMFUNCTION hal_flash_lock(void) {}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t* data, int len)
{
    return hal_flash_write((uint32_t)address, data, len);
}

/* Reads flash, spoofing the erased-byte value for erased pages
 * (reading them directly would raise an uncorrectable ECC bus error).
 * Returns the number of bytes read, or -1 on error. */
int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t* data, int len)
{
    int bytesRead = 0;

    while (bytesRead < len) {
        uint32_t pageAddress     = GET_PAGE_ADDR(address);
        uint32_t offset          = address % TC4_PFLASH_PAGE_SIZE;
        uint32_t bytesInThisPage = TC4_PFLASH_PAGE_SIZE - offset;
        int      erased;

        if (bytesInThisPage > (uint32_t)(len - bytesRead)) {
            bytesInThisPage = len - bytesRead;
        }

        erased = tc4_flash_IsPageErased(pageAddress);
        if (erased < 0) {
            return -1;
        }

        if (erased == 1) {
            uint32_t i;
            for (i = 0; i < bytesInThisPage; i++) {
                data[bytesRead + i] = FLASH_BYTE_ERASED;
            }
        }
        else {
            tc4_flash_Read(address, data + bytesRead, bytesInThisPage);
        }

        address += bytesInThisPage;
        bytesRead += bytesInThisPage;
    }

    return bytesRead;
}

/* Erased check with the hardware erase-verify command, page by page. A plain
 * read cannot tell, because reading erased PFLASH is an ECC error. */
int RAMFUNCTION hal_flash_is_erased(uint32_t address, int len)
{
    uint32_t page;

    if (len <= 0) {
        return 1;
    }
    for (page = GET_PAGE_ADDR(address); page < address + (uint32_t)len;
         page += TC4_PFLASH_PAGE_SIZE) {
        int erased = tc4_flash_IsPageErased(page);
        if (erased != 1) {
            return erased;
        }
    }
    return 1;
}

int RAMFUNCTION ext_flash_is_erased(uintptr_t address, int len)
{
    return hal_flash_is_erased((uint32_t)address, len);
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    return hal_flash_erase((uint32_t)address, len);
}

void RAMFUNCTION ext_flash_lock(void)
{
    hal_flash_lock();
}

void RAMFUNCTION ext_flash_unlock(void)
{
    hal_flash_unlock();
}

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT

/* wolfHSM client connection over the port shared-memory transport. */

/* Nonzero hardware setup return code for debugger inspection. */
volatile int g_tc4_hsmc_hw_rc;

/* CPU0 hardware setup for HsmHost transport. */
static int tc4_hsmc_hw_init(void)
{
    const uint32_t srcTagMask =
        TCHSM_APPROT_TAGS_FOR_CORE(0u) | TCHSM_APPROT_TAGS_FOR_CORE(1u) |
        TCHSM_APPROT_TAGS_FOR_CORE(TC4_CORE_ID_CPUCS);

    if (tchsm_Notify_OpenSrcs(TCHSM_HSMHOST_CLIENT_APP0, srcTagMask) !=
        WH_ERROR_OK) {
        return -1;
    }

    if (tchsm_SprApu_OpenDspr() != 0u) {
        return -2;
    }
    return 0;
}

int hal_hsm_init_connect(void)
{
    int rc;

    rc = tc4_hsmc_hw_init();
    if (rc != 0) {
        g_tc4_hsmc_hw_rc = rc;
        return rc;
    }

    rc = tchsm_client_init(TCHSM_HSMHOST_CLIENT_APP0);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    rc = tchsm_client_wait_ready(TCHSM_HSMHOST_CLIENT_APP0, 10000u);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    rc = wh_Client_Init(&hsmClientCtx,
                        tchsm_client_get_config(TCHSM_HSMHOST_CLIENT_APP0));
    if (rc != WH_ERROR_OK) {
        return rc;
    }
    return wh_Client_CommInit(&hsmClientCtx, NULL, NULL);
}

int hal_hsm_disconnect(void)
{
    int rc  = wh_Client_CommClose(&hsmClientCtx);
    int rc2 = wh_Client_Cleanup(&hsmClientCtx);
    return (rc != WH_ERROR_OK) ? rc : rc2;
}

#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */
