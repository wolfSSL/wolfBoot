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

/* iLLD headers */
#include "Ifx_Types.h"
#include "IfxAsclin.h"
#include "IfxAsclin_PinMap.h"
#include "IfxCpu.h"
#include "IfxWtu.h"
#ifndef TARGET_aurix_tc4xx_csrm
#include "IfxClock.h"
#endif

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

#include "IfxApApu.h"
#include "IfxApProt.h"
#include "IfxSrc.h"

/* wolfHSM headers */
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_error.h"
/* wolfHSM AURIX TC4xx port headers */
#include "tchsm_hsmhost.h"
#include "tchsm_client.h"
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

#define TC4_PFLASH_PAGE_SIZE (32u)
#define TC4_PFLASH_BURST_SIZE (512u)
#ifdef TARGET_aurix_tc4xx_csrm
#define TC4_PROG_CHUNK_SIZE TC4_PFLASH_PAGE_SIZE
#else
#define TC4_PROG_CHUNK_SIZE TC4_PFLASH_BURST_SIZE
#endif

/* Convert between the cached (0x8...) segment used by the wolfBoot
 * partition configuration and the non-cached (0xA...) alias the command
 * interface requires. */
#define TC4_FLASH_NC(addr) (((uint32_t)(addr)) | 0x20000000u)

/* Select FCI based on CPU */
#ifdef TARGET_aurix_tc4xx_csrm
#define TC4_CMD_BASE (0xF80C0000u)
#else
#define TC4_CMD_BASE (0xF8080000u)
#endif

#define TC4_CMD_REG(off) ((volatile uint32_t*)(TC4_CMD_BASE | (off)))
#define TC4_CMD_MODE TC4_CMD_REG(0x5554u)     /* mode/status cycles */
#define TC4_CMD_LOAD2X32 TC4_CMD_REG(0x55F4u) /* page assembly, 2x32-bit */
#define TC4_CMD_ADDR TC4_CMD_REG(0xAA50u)     /* command word 1: address */
#define TC4_CMD_COUNT TC4_CMD_REG(0xAA58u)    /* command word 2: count */
#define TC4_CMD_CODE TC4_CMD_REG(0xAAA8u)     /* command code, written twice */

#define TC4_MODE_CLEAR_STATUS (0xFAu)
#define TC4_MODE_PF_PAGEMODE (0x50u)
#define TC4_MODE_RESET_READ (0xF0u)

/* DMU command interface status/error registers (CSCI mirrors HCI at +0x80) */
#define TC4_DMU_REG(addr) ((volatile uint32_t*)(addr))
#ifdef TARGET_aurix_tc4xx_csrm
#define TC4_HCI_STATUS TC4_DMU_REG(0xF8040084u)
#define TC4_HCI_ERR TC4_DMU_REG(0xF8040090u)
#define TC4_HCI_CLRERR TC4_DMU_REG(0xF8040094u)
#else
#define TC4_HCI_STATUS TC4_DMU_REG(0xF8040004u)
#define TC4_HCI_ERR TC4_DMU_REG(0xF8040010u)
#define TC4_HCI_CLRERR TC4_DMU_REG(0xF8040014u)
#endif
#define TC4_GP_BKALLOC TC4_DMU_REG(0xF8040A00u)
#define TC4_BKALLOC_CSRMPF (1u << 18)

/* HCI.STATUS fields */
#define TC4_STATUS_BANKS_BUSY (0x000F0FFFu) /* per-bank busy + host DF + FSI \
                                             */
#define TC4_STATUS_PFPAGE (1u << 25)
#define TC4_STATUS_REQDONE (1u << 31)

#define TC4_ERR_OPFAIL_MASK \
    (0x00010077u) /* ADER|SQER|PROER|ABER|CLER|PVER|OPER */
#define TC4_ERR_EVER (1u << 7)
#define TC4_CLRERR_ALL (0xF7u) /* OPER (bit16) has no clear bit */

/* Bounded wait limits for flash commands */
#define TC4_BUSY_SPIN_LIMIT (50000000u)
#define TC4_REQDONE_SPIN_LIMIT (1000000u)

#define WOLFBOOT_AURIX_RESET_REASON (0x5742) /* "WB" */

/* Helper macros for the base address of the page or sector containing addr */
#define GET_PAGE_ADDR(addr) ((uintptr_t)(addr) & ~(TC4_PFLASH_PAGE_SIZE - 1))
#define GET_SECTOR_ADDR(addr) ((uintptr_t)(addr) & ~(WOLFBOOT_SECTOR_SIZE - 1))

#define TC4_DSYNC() __asm volatile("dsync" ::: "memory")

/* RAM buffer holding one flash sector for read-modify-write operations */
static uint32_t sectorBuffer[WOLFBOOT_SECTOR_SIZE / sizeof(uint32_t)];

static void RAMFUNCTION flashClearStatus(void)
{
    *TC4_CMD_MODE = TC4_MODE_CLEAR_STATUS;
    TC4_DSYNC();
}

/* Wait for all host banks idle and for REQDONE (end of sequence status).
 * A timeout fails the operation.
 * Returns 0 on completion, -1 on timeout. */
static int RAMFUNCTION flashWaitDone(void)
{
    uint32_t spins;

    spins = TC4_BUSY_SPIN_LIMIT;
    while (((*TC4_HCI_STATUS & TC4_STATUS_BANKS_BUSY) != 0u) &&
           (--spins != 0u)) {
    }
    if (spins == 0u) {
        return -1;
    }

    spins = TC4_REQDONE_SPIN_LIMIT;
    while (((*TC4_HCI_STATUS & TC4_STATUS_REQDONE) == 0u) && (--spins != 0u)) {
    }
    if (spins == 0u) {
        return -1;
    }

    TC4_DSYNC();
    return 0;
}

/* Read and clear the error flags. Returns the raw error register value. */
static uint32_t RAMFUNCTION flashGetClearErrors(void)
{
    uint32_t err    = *TC4_HCI_ERR;
    *TC4_HCI_CLRERR = TC4_CLRERR_ALL;
    TC4_DSYNC();
    return err;
}

/* Issue a flash command and wait for completion. Returns the error flags on
 * completion or TC4_ERR_OPFAIL_MASK on timeout. */
static uint32_t RAMFUNCTION flashCommand(uint32_t address, uint32_t count,
                                         uint32_t code1, uint32_t code2)
{
    flashClearStatus();
    *TC4_CMD_ADDR  = TC4_FLASH_NC(address);
    *TC4_CMD_COUNT = count;
    *TC4_CMD_CODE  = code1;
    *TC4_CMD_CODE  = code2;
    TC4_DSYNC();

    if (flashWaitDone() != 0) {
        return TC4_ERR_OPFAIL_MASK;
    }
    return flashGetClearErrors();
}

/* Blank check one page via the hardware erase-verify command.
 * Returns 1 if erased, 0 if programmed, -1 on error. */
static int RAMFUNCTION flashIsPageErased(uint32_t pageAddr)
{
    uint32_t err = flashCommand(pageAddr, 0u, 0x80u, 0x56u);
    if ((err & TC4_ERR_OPFAIL_MASK) != 0u) {
        return -1;
    }
    return ((err & TC4_ERR_EVER) != 0u) ? 0 : 1;
}

/* Erase one 16KB logical sector. Sectors are erased one at a time due to issues
 * with multi-sector erases leaving the busy flag set. Returns 0 on success. */
static int RAMFUNCTION flashEraseSector(uint32_t sectorAddr)
{
    uint32_t err = flashCommand(sectorAddr, 1u, 0x80u, 0x50u);
    if ((err & TC4_ERR_OPFAIL_MASK) != 0u) {
        return -1;
    }
    return 0;
}

/* Program a naturally aligned group of pages (one page or one burst) that
 * is already erased. data must hold size bytes; size is either
 * TC4_PFLASH_PAGE_SIZE or TC4_PFLASH_BURST_SIZE. Returns 0 on success. */
static int RAMFUNCTION flashProgramAligned(uint32_t addr, const uint32_t* data,
                                           uint32_t size)
{
    uint32_t i;
    uint32_t err;
    uint32_t spins;

    /* Enter page mode and wait for the assembly buffer to be ready */
    flashClearStatus();
    *TC4_CMD_MODE = TC4_MODE_PF_PAGEMODE;
    TC4_DSYNC();
    spins = TC4_REQDONE_SPIN_LIMIT;
    while (((*TC4_HCI_STATUS & TC4_STATUS_PFPAGE) == 0u) && (--spins != 0u)) {
    }
    if (spins == 0u) {
        return -1;
    }

    /* Fill the page assembly buffer, two 32-bit words per cycle */
    for (i = 0; i < (size / sizeof(uint32_t)); i += 2u) {
        *TC4_CMD_LOAD2X32 = data[i];
        *TC4_CMD_LOAD2X32 = data[i + 1u];
    }
    TC4_DSYNC();

    /* Write Page (0xAA) or Write Burst (0xA6) */
    err = flashCommand(addr, 0u, 0xA0u,
                       (size == TC4_PFLASH_BURST_SIZE) ? 0xA6u : 0xAAu);
    if ((err & TC4_ERR_OPFAIL_MASK) != 0u) {
        /* Leave page mode so the interface is not stuck */
        *TC4_CMD_MODE = TC4_MODE_RESET_READ;
        TC4_DSYNC();
        return -1;
    }
    return 0;
}

/* Read len bytes at address, which must not span an erased page (callers
 * blank-check first). Reads go through the non-cached alias so no stale
 * cache lines are involved. */
static void RAMFUNCTION flashRead(uint32_t address, uint8_t* data, uint32_t len)
{
    const volatile uint8_t* src =
        (const volatile uint8_t*)TC4_FLASH_NC(address);
    uint32_t i;
    for (i = 0; i < len; i++) {
        data[i] = src[i];
    }
}

/* Read an entire sector into sectorBuffer, substituting the erased-byte
 * value for erased pages so no erased cell is ever read. */
static void RAMFUNCTION cacheSector(uint32_t sectorAddress)
{
    uint32_t page;
    for (page = 0; page < WOLFBOOT_SECTOR_SIZE; page += TC4_PFLASH_PAGE_SIZE) {
        uint32_t* dst    = sectorBuffer + (page / sizeof(uint32_t));
        int       erased = flashIsPageErased(sectorAddress + page);

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
            flashRead(sectorAddress + page, (uint8_t*)dst,
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
        if (flashProgramAligned(sectorAddress + off,
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

        if (flashProgramAligned(pageAddress, pageBuffer,
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

#if defined(DEBUG_UART) || defined(UART_FLASH)

#define TC4_UART (&MODULE_ASCLIN0)
#define TC4_UART_BAUD (115200u)
#define TC4_UART_FIFO_SIZE (16u)

int  uart_tx(const uint8_t c);
int  uart_rx(uint8_t* c);
void uart_init(void);
void uart_write(const char* buf, unsigned int sz);

void uart_init(void)
{
    Ifx_ASCLIN* u = TC4_UART;

    IfxAsclin_enableModule(u);
    IfxAsclin_setClockSource(u, IfxAsclin_ClockSource_noClock);
    IfxAsclin_setFrameMode(u, IfxAsclin_FrameMode_initialise);
    IfxAsclin_setPrescaler(u, 1);
    IfxAsclin_setClockSource(u, IfxAsclin_ClockSource_ascFastClock);
    (void)IfxAsclin_setBitTiming(
        u, (float32)TC4_UART_BAUD, IfxAsclin_OversamplingFactor_16,
        IfxAsclin_SamplePointPosition_8, IfxAsclin_SamplesPerBit_three);
    IfxAsclin_setClockSource(u, IfxAsclin_ClockSource_noClock);

    IfxAsclin_enableParity(u, FALSE);
    IfxAsclin_setStopBit(u, IfxAsclin_StopBit_1);
    IfxAsclin_setShiftDirection(u, IfxAsclin_ShiftDirection_lsbFirst);
    IfxAsclin_setDataLength(u, IfxAsclin_DataLength_8);
    IfxAsclin_setTxFifoInletWidth(u, IfxAsclin_TxFifoInletWidth_1);
    IfxAsclin_setRxFifoOutletWidth(u, IfxAsclin_RxFifoOutletWidth_1);
    IfxAsclin_setFrameMode(u, IfxAsclin_FrameMode_asc);

    IfxAsclin_initTxPin(&IfxAsclin0_TX_F_P14_0_OUT, IfxPort_OutputMode_pushPull,
                        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxAsclin_initRxPin(&IfxAsclin0_RXA_F_P14_1_IN, IfxPort_InputMode_pullUp,
                        IfxPort_PadDriver_cmosAutomotiveSpeed1);

    IfxAsclin_setClockSource(u, IfxAsclin_ClockSource_ascFastClock);
    IfxAsclin_disableAllFlags(u);
    IfxAsclin_clearAllFlags(u);
    IfxAsclin_flushTxFifo(u);
    IfxAsclin_enableTxFifoOutlet(u, TRUE);
}

int uart_tx(const uint8_t c)
{
    Ifx_ASCLIN* u = TC4_UART;
    while (IfxAsclin_getTxFifoFillLevel(u) >= TC4_UART_FIFO_SIZE) {
    }
    IfxAsclin_writeTxData(u, c);
    return 1;
}

int uart_rx(uint8_t* c)
{
    (void)c;
    return 0;
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

/* Block until the TX FIFO has fully drained onto the wire */
static void uart_flush(void)
{
    Ifx_ASCLIN*       u = TC4_UART;
    volatile uint32_t i;

    while (IfxAsclin_getTxFifoFillLevel(u) != 0u) {
    }
    /* Delay in case TC flag is still set from an earlier idle
     * period while the last frame sits in the shift reg. */
    for (i = 0; i < 200000u; i++) {
    }
}

#endif /* DEBUG_UART || UART_FLASH */

/*
 * wolfBoot HAL entry points
 */

void hal_init(void)
{
#ifdef TARGET_aurix_tc4xx_csrm
    IfxWtu_disableSecurityWatchdog(IfxWtu_getSecurityWatchdogPassword());

    /* The CSRM PFLASH bank must be allocated to the CSRM or the CSCI
     * command writes will fail silently */
    if ((*TC4_GP_BKALLOC & TC4_BKALLOC_CSRMPF) == 0u) {
        wolfBoot_panic();
    }
#else
    IfxWtu_disableCpuWatchdog(IfxWtu_getCpuWatchdogPassword());
    IfxWtu_disableSystemWatchdog(IfxWtu_getSystemWatchdogPassword());

    /* Refuse to run if a host PFLASH bank has been reallocated to CSRM. */
    if ((*TC4_GP_BKALLOC & 0xFu) != 0u) {
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
    uart_flush();
    IfxAsclin_setClockSource(TC4_UART, IfxAsclin_ClockSource_noClock);
    IfxAsclin_disableModule(TC4_UART);
#endif

#ifndef TARGET_aurix_tc4xx_csrm
    /* Host cores must return the clock tree to the backup clock and power the
     * PLLs down, as the iLLD SSW can't handle already initialized PLLs. */
    (void)IfxClock_switchToBackupClock(&IfxClock_defaultClockConfig);
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
    IfxCpu_triggerSwReset();
    while (1) {
    }
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
            int erased = flashIsPageErased(page);
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

            if (flashEraseSector(currentSectorAddr) != 0) {
                ret = -1;
                break;
            }

            programCachedSector(currentSectorAddr);
        }
        else {
            if (flashEraseSector(currentSectorAddr) != 0) {
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

        erased = flashIsPageErased(pageAddress);
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
            flashRead(address, data + bytesRead, bytesInThisPage);
        }

        address += bytesInThisPage;
        bytesRead += bytesInThisPage;
    }

    return bytesRead;
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
    {
        IfxApApu_ApuConfig apConfig;
        IfxApApu_initConfig(&apConfig);
        const unsigned long srcTagMask =
            (1UL << IfxApProt_TagId_cpu0d) | (1UL << IfxApProt_TagId_cpu0ds) |
            (1UL << IfxApProt_TagId_cpu1d) | (1UL << IfxApProt_TagId_cpu1ds) |
            (1UL << IfxApProt_TagId_cpucsd) | (1UL << IfxApProt_TagId_cpucsds);
        apConfig.wraTagId = srcTagMask;
        apConfig.rdaTagId = srcTagMask;
        apConfig.wrbTagId = 0U;
        apConfig.rdbTagId = 0U;
        IfxSrc_configureAccessToSrcs(&apConfig);
    }

    /* Probe SR0 to confirm SRC ACCEN writes are enabled. */
    SRC_GPSR4_SR0.U = 0x00006014u;
    if (((SRC_GPSR4_SR0.U >> 12) & 0xFu) != 6u) {
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

    /* The S2H wake ISR needs interrupts. */
    IfxCpu_enableInterrupts();

    rc = tc4_hsmc_hw_init();
    if (rc != 0) {
        g_tc4_hsmc_hw_rc = rc;
        return rc;
    }

    tchsm_client_init(TCHSM_HSMHOST_CLIENT_APP0);

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
