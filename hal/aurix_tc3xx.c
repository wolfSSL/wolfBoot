/* aurix_tc3xx.c
 *
 * Copyright (C) 2014-2024 wolfSSL Inc.
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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* wolfBoot headers */
#include "hal.h"
#include "image.h"  /* for RAMFUNCTION */
#include "loader.h" /* for wolfBoot_panic */

/* ILLD headers */
#include "IfxCpu_reg.h"    /* for CPU0_FLASHCON1 */
#include "IfxFlash.h"      /* for IfxFlash_eraseMultipleSectors, */
#include "IfxPort.h"       /* for IfxPort_*  */
#include "IfxScuRcu.h"     /* for IfxScuRcu_performReset */
#include "Ifx_Ssw_Infra.h" /* for Ifx_Ssw_jumpToFunction */
#if defined(DEBUG_UART)
#include "IfxAsclin_Asc.h"
#include "Cpu/Irq/IfxCpu_Irq.h"
#include "IfxCpu.h"
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
/* wolfHSM headers */
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_transport_mem.h"

/* wolfHSM AURIX port headers */
#include "tchsm_hh_host.h"
#include "tchsm_hsmhost.h"
#include "tchsm_config.h"
#include "tchsm_common.h"
#include "hsm_ipc.h"
#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */

#define FLASH_MODULE                (0)
#define UNUSED_PARAMETER            (0)
#define WOLFBOOT_AURIX_RESET_REASON (0x5742) /* "WB" */

/* Helper macros to gets the base address of the page, wordline, or sector that
 * contains byteAddress */
#define GET_PAGE_ADDR(addr) \
    ((uintptr_t)(addr) & ~(IFXFLASH_PFLASH_PAGE_LENGTH - 1))
#define GET_WORDLINE_ADDR(addr) \
    ((uintptr_t)(addr) & ~(IFXFLASH_PFLASH_WORDLINE_LENGTH - 1))
#define GET_SECTOR_ADDR(addr) ((uintptr_t)(addr) & ~(WOLFBOOT_SECTOR_SIZE - 1))

/* RAM buffer to hold the contents of an entire flash sector*/
static uint32_t sectorBuffer[WOLFBOOT_SECTOR_SIZE / sizeof(uint32_t)];

#define LED_PROG     (0)
#define LED_ERASE    (1)
#define LED_READ     (2)
#define LED_WOLFBOOT (5)

#ifdef WOLFBOOT_AURIX_GPIO_TIMING
#ifndef SWAP_LED_POLARITY
#define LED_ON(led)  IfxPort_setPinLow(&MODULE_P00, (led))
#define LED_OFF(led) IfxPort_setPinHigh(&MODULE_P00, (led))
#else
#define LED_ON(led)  IfxPort_setPinHigh(&MODULE_P00, (led))
#define LED_OFF(led) IfxPort_setPinLow(&MODULE_P00, (led))
#endif
#else
#define LED_ON(led)
#define LED_OFF(led)
#endif /* WOLFBOOT_AURIX_GPIO_TIMING */

#if defined(DEBUG_UART)
#define UART_PIN_RX IfxAsclin0_RXA_P14_1_IN /* RX pin of the board */
#define UART_PIN_TX IfxAsclin0_TX_P14_0_OUT /* TX pin of the board */
#define UART_BAUDRATE 115200
static Ifx_ASCLIN* g_asclinRegs = &MODULE_ASCLIN0;
static int         uartInit(void);
static int         uartTx(const uint8_t c);
#endif /* DEBUG_UART */

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
int hal_hsm_init_connect(void);
int hal_hsm_disconnect(void);
#endif

/* Returns the SDK flash type enum based on the address */
static IfxFlash_FlashType getFlashTypeFromAddr(uint32_t addr)
{
    IfxFlash_FlashType type;

    if (addr >= IFXFLASH_DFLASH_START && addr <= IFXFLASH_DFLASH_END) {
        /* Assuming D0 for simplicity */
        type = IfxFlash_FlashType_D0;
    }
    else if (addr >= IFXFLASH_PFLASH_P0_START
             && addr <= IFXFLASH_PFLASH_P0_END) {
        type = IfxFlash_FlashType_P0;
    }
    else if (addr >= IFXFLASH_PFLASH_P1_START
             && addr <= IFXFLASH_PFLASH_P1_END) {
        type = IfxFlash_FlashType_P1;
    }
    else {
        /* bad address, panic for now */
        wolfBoot_panic();
    }

    return type;
}

/* Programs a single page in flash */
static void RAMFUNCTION programPage(uint32_t           address,
                                    const uint32_t*    data,
                                    IfxFlash_FlashType type)
{
    const uint16 endInitSafetyPassword =
        IfxScuWdt_getSafetyWatchdogPasswordInline();
    size_t offset;

    if (address % IFXFLASH_PFLASH_PAGE_LENGTH != 0) {
        wolfBoot_panic();
    }

    IfxFlash_enterPageMode(address);
    IfxFlash_waitUnbusy(FLASH_MODULE, type);

    for (offset = 0; offset < IFXFLASH_PFLASH_PAGE_LENGTH / sizeof(uint32_t);
         offset += 2) {
        IfxFlash_loadPage2X32(address, data[offset], data[offset + 1]);
    }

    IfxScuWdt_clearSafetyEndinitInline(endInitSafetyPassword);
    IfxFlash_writePage(address);
    IfxScuWdt_setSafetyEndinitInline(endInitSafetyPassword);

    IfxFlash_waitUnbusy(FLASH_MODULE, type);
}

/* Performs a hardware erase verify check on the range specified by address and
 * len. Returns true if the region is erased */
static int RAMFUNCTION flashIsErased(uint32_t           address,
                                     int                len,
                                     IfxFlash_FlashType type)
{
    uint32_t base = 0;

    IfxFlash_clearStatus(UNUSED_PARAMETER);

    /* sector granularity */
    if (len > IFXFLASH_PFLASH_WORDLINE_LENGTH) {
        base = GET_SECTOR_ADDR(address);
        IfxFlash_eraseVerifySector(base);
        IfxFlash_waitUnbusy(FLASH_MODULE, type);
    }
    /* wordline granularity */
    else if (len > IFXFLASH_PFLASH_PAGE_LENGTH) {
        base = GET_WORDLINE_ADDR(address);
        IfxFlash_verifyErasedWordLine(base);
        IfxFlash_waitUnbusy(FLASH_MODULE, type);
    }
    /* page granularity */
    else if (len > 0) {
        base = GET_PAGE_ADDR(address);
        IfxFlash_verifyErasedPage(base);
        IfxFlash_waitUnbusy(FLASH_MODULE, type);
    }
    /* error on 0 len for now */
    else {
        wolfBoot_panic();
    }

    /* No erase verify error means block is erased */
    return (DMU_HF_ERRSR.B.EVER == 0) ? 1 : 0;
}

/* Returns true if any of the pages spanned by address and len are erased */
static int RAMFUNCTION containsErasedPage(uint32_t           address,
                                          size_t             len,
                                          IfxFlash_FlashType type)
{
    const uint32_t startPage = GET_PAGE_ADDR(address);
    const uint32_t endPage   = GET_PAGE_ADDR(address + len - 1);
    uint32_t       page;

    for (page = startPage; page <= endPage;
         page += IFXFLASH_PFLASH_PAGE_LENGTH) {
        if (flashIsErased(page, IFXFLASH_PFLASH_PAGE_LENGTH, type)) {
            return 1;
        }
    }

    return 0;
}

/* Programs the contents of the cached sector buffer to flash */
static void RAMFUNCTION programCachedSector(uint32_t           sectorAddress,
                                            IfxFlash_FlashType type)
{
    const uint16 endInitSafetyPassword =
        IfxScuWdt_getSafetyWatchdogPasswordInline();
    uint32_t pageAddr;
    size_t   burstIdx;
    size_t   bufferIdx;
    size_t   offset;

    pageAddr = sectorAddress;

    /* Burst program the whole sector with values from sectorBuffer */
    for (burstIdx = 0;
         burstIdx < WOLFBOOT_SECTOR_SIZE / IFXFLASH_PFLASH_BURST_LENGTH;
         burstIdx++) {
        IfxFlash_enterPageMode(pageAddr);

        /* Wait until page mode is entered */
        IfxFlash_waitUnbusy(FLASH_MODULE, type);

        /* Load a burst worth of data into the page */
        for (offset = 0;
             offset < IFXFLASH_PFLASH_BURST_LENGTH / (2 * sizeof(uint32_t));
             offset++) {
            bufferIdx =
                burstIdx * (IFXFLASH_PFLASH_BURST_LENGTH / sizeof(uint32_t))
                + (offset * 2);

            IfxFlash_loadPage2X32(UNUSED_PARAMETER,
                                  sectorBuffer[bufferIdx],
                                  sectorBuffer[bufferIdx + 1]);
        }

        /* Write the page */
        IfxScuWdt_clearSafetyEndinitInline(endInitSafetyPassword);
        IfxFlash_writeBurst(pageAddr);
        IfxScuWdt_setSafetyEndinitInline(endInitSafetyPassword);

        /* Wait until the page is written in the Program Flash memory */
        IfxFlash_waitUnbusy(FLASH_MODULE, type);

        pageAddr += IFXFLASH_PFLASH_BURST_LENGTH;
    }
}

/* Programs unaligned input data to flash, assuming the underlying memory is
 * erased */
void RAMFUNCTION programBytesToErasedFlash(uint32_t           address,
                                           const uint8_t*     data,
                                           int                size,
                                           IfxFlash_FlashType type)
{
    uint32_t pageBuffer[IFXFLASH_PFLASH_PAGE_LENGTH / sizeof(uint32_t)];
    uint32_t pageAddress;
    uint32_t offset;
    uint32_t toWrite;

    pageAddress = address & ~(IFXFLASH_PFLASH_PAGE_LENGTH - 1);
    offset      = address % IFXFLASH_PFLASH_PAGE_LENGTH;

    while (size > 0) {
        /* Calculate the number of bytes to write in the current page */
        toWrite = IFXFLASH_PFLASH_PAGE_LENGTH - offset;
        if (toWrite > size) {
            toWrite = size;
        }

        /* Fill the page buffer with the erased byte value */
        memset(pageBuffer, FLASH_BYTE_ERASED, IFXFLASH_PFLASH_PAGE_LENGTH);

        /* Copy the new data into the page buffer at the correct offset */
        memcpy((uint8_t*)pageBuffer + offset, data, toWrite);

        /* Write the modified page buffer back to flash */
        programPage(pageAddress, pageBuffer, type);

        size -= toWrite;
        data += toWrite;
        address += toWrite;
        pageAddress = address & ~(IFXFLASH_PFLASH_PAGE_LENGTH - 1);
        offset      = address % IFXFLASH_PFLASH_PAGE_LENGTH;
    }
}

/* Directly reads a page from PFLASH using word-aligned reads/writes */
static void readPage32Aligned(uint32_t pageAddr, uint32_t* data)
{
    uint32_t* ptr;
    size_t    i;
    ptr = (uint32_t*)pageAddr;

    for (i = 0; i < IFXFLASH_PFLASH_PAGE_LENGTH / sizeof(uint32_t); i++) {
        *data = *ptr;
        data++;
        ptr++;
    }
}

/* reads an entire flash sector into the RAM cache, making sure to never read
 * any pages from flash that are erased */
static void cacheSector(uint32_t sectorAddress, IfxFlash_FlashType type)
{
    const uint32_t startPage = GET_PAGE_ADDR(sectorAddress);
    const uint32_t endPage =
        GET_PAGE_ADDR(sectorAddress + WOLFBOOT_SECTOR_SIZE - 1);
    uint32_t* pageInSectorBuffer;
    uint32_t  page;

    /* Iterate over every page in the sector, caching its contents if not
     * erased, and caching 0s if erased */
    for (page = startPage; page <= endPage;
         page += IFXFLASH_PFLASH_PAGE_LENGTH) {
        pageInSectorBuffer =
            sectorBuffer + ((page - sectorAddress) / sizeof(uint32_t));

        if (flashIsErased(page, IFXFLASH_PFLASH_PAGE_LENGTH, type)) {
            memset(pageInSectorBuffer,
                   FLASH_BYTE_ERASED,
                   IFXFLASH_PFLASH_PAGE_LENGTH);
        }
        else {
            readPage32Aligned(page, pageInSectorBuffer);
        }
    }
}

/* This function is called by the bootloader at the very beginning of the
 * execution. Ideally, the implementation provided configures the clock settings
 * for the target microcontroller, to ensure that it runs at at the required
 * speed to shorten the time required for the cryptography primitives to verify
 * the firmware images*/
void hal_init(void)
{
#ifdef WOLFBOOT_AURIX_GPIO_TIMING
    IfxPort_setPinModeOutput(&MODULE_P00,
                             LED_WOLFBOOT,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(&MODULE_P00,
                             LED_PROG,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(&MODULE_P00,
                             LED_ERASE,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(&MODULE_P00,
                             LED_READ,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
#endif /* WOLFBOOT_AURIX_GPIO_TIMING */

    LED_ON(LED_WOLFBOOT);
    LED_OFF(LED_PROG);
    LED_OFF(LED_ERASE);
    LED_OFF(LED_READ);

#if defined(DEBUG_UART)
    uartInit();
#endif
}

/*
 * This function provides an implementation of the flash write function, using
 * the target's IAP interface. address is the offset from the beginning of the
 * flash area, data is the payload to be stored in the flash using the IAP
 * interface, and len is the size of the payload. hal_flash_write should return
 * 0 upon success, or a negative value in case of failure.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t* data, int size)
{
    const IfxFlash_FlashType type              = getFlashTypeFromAddr(address);
    uint32_t                 currentAddress    = address;
    int                      remainingSize     = size;
    int                      bytesWrittenTotal = 0;

    LED_ON(LED_PROG);

    /* Process the data sector by sector */
    while (remainingSize > 0) {
        uint32_t currentSectorAddress = GET_SECTOR_ADDR(currentAddress);
        uint32_t offsetInSector       = currentAddress - currentSectorAddress;
        uint32_t bytesInThisSector    = WOLFBOOT_SECTOR_SIZE - offsetInSector;

        /* Adjust bytes to write if this would overflow the current sector */
        if (bytesInThisSector > remainingSize) {
            bytesInThisSector = remainingSize;
        }

        /* Determine the range of pages affected in this sector */
        const uint32_t startPage = GET_PAGE_ADDR(currentAddress);
        const uint32_t endPage =
            GET_PAGE_ADDR(currentAddress + bytesInThisSector - 1);
        uint32_t page;
        int      needsSectorRmw = 0;

        /* Check if any page within the range is not erased */
        for (page = startPage; page <= endPage;
             page += IFXFLASH_PFLASH_PAGE_LENGTH) {
            if (!flashIsErased(page, IFXFLASH_PFLASH_PAGE_LENGTH, type)) {
                needsSectorRmw = 1;
                break;
            }
        }

        /* If a page within the range is not erased, we need to
         * read-modify-write the sector */
        if (needsSectorRmw) {
            /* Read entire sector into RAM */
            cacheSector(currentSectorAddress, type);

            /* Erase the entire sector */
            hal_flash_erase(currentSectorAddress, WOLFBOOT_SECTOR_SIZE);

            /* Modify the relevant part of the RAM sector buffer */
            memcpy((uint8_t*)sectorBuffer + offsetInSector,
                   data + bytesWrittenTotal, bytesInThisSector);

            /* Program the modified sector back into flash */
            programCachedSector(currentSectorAddress, type);
        }
        else {
            /* All affected pages are erased, program the data directly */
            programBytesToErasedFlash(currentAddress, data + bytesWrittenTotal,
                                      bytesInThisSector, type);
        }

        /* Update pointers and counters */
        bytesWrittenTotal += bytesInThisSector;
        currentAddress += bytesInThisSector;
        remainingSize -= bytesInThisSector;
    }

    LED_OFF(LED_PROG);

    return 0;
}

/* Called by the bootloader to erase part of the flash memory to allow
 * subsequent boots. Erase operations must be performed via the specific IAP
 * interface of the target microcontroller. address marks the start of the area
 * that the bootloader wants to erase, and len specifies the size of the area to
 * be erased. This function must take into account the geometry of the flash
 * sectors, and erase all the sectors in between. */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    LED_ON(LED_ERASE);

    /* Handle zero length case */
    if (len <= 0) {
        LED_OFF(LED_ERASE);
        return 0;
    }

    const uint32_t           startSectorAddr = GET_SECTOR_ADDR(address);
    const uint32_t           endAddress      = address + len - 1;
    const uint32_t           endSectorAddr   = GET_SECTOR_ADDR(endAddress);
    const IfxFlash_FlashType type            = getFlashTypeFromAddr(address);
    const uint16             endInitSafetyPassword =
        IfxScuWdt_getSafetyWatchdogPasswordInline();
    uint32_t currentSectorAddr;

    /* If address and len are both sector-aligned, perform simple bulk erase */
    if ((address == startSectorAddr) &&
        (endAddress == endSectorAddr + WOLFBOOT_SECTOR_SIZE - 1)) {
        const size_t numSectors =
            (endSectorAddr - startSectorAddr) / WOLFBOOT_SECTOR_SIZE + 1;

        /* Disable ENDINIT protection */
        IfxScuWdt_clearSafetyEndinitInline(endInitSafetyPassword);

        IfxFlash_eraseMultipleSectors(startSectorAddr, numSectors);

        /* Reenable ENDINIT protection */
        IfxScuWdt_setSafetyEndinitInline(endInitSafetyPassword);

        IfxFlash_waitUnbusy(FLASH_MODULE, type);
    }
    /* For non-sector aligned erases, handle each sector carefully */
    else {
        /* Process each affected sector */
        for (currentSectorAddr = startSectorAddr;
             currentSectorAddr <= endSectorAddr;
             currentSectorAddr += WOLFBOOT_SECTOR_SIZE) {

            /* Check if this is a partial sector erase */
            const int isFirstSector = (currentSectorAddr == startSectorAddr);
            const int isLastSector  = (currentSectorAddr == endSectorAddr);
            const int isPartialStart =
                isFirstSector && (address > startSectorAddr);
            const int isPartialEnd =
                isLastSector &&
                (endAddress < (endSectorAddr + WOLFBOOT_SECTOR_SIZE - 1));

            /* For partial sectors, need to read-modify-write */
            if (isPartialStart || isPartialEnd) {
                /* Read the sector into the sector buffer */
                cacheSector(currentSectorAddr, type);

                /* Calculate which bytes within the sector to erase */
                uint32_t eraseStartOffset =
                    isPartialStart ? (address - currentSectorAddr) : 0;

                uint32_t eraseEndOffset = isPartialEnd
                                              ? (endAddress - currentSectorAddr)
                                              : (WOLFBOOT_SECTOR_SIZE - 1);

                uint32_t eraseLen = eraseEndOffset - eraseStartOffset + 1;

                /* Fill the section to be erased with the erased byte value */
                memset((uint8_t*)sectorBuffer + eraseStartOffset,
                       FLASH_BYTE_ERASED, eraseLen);

                /* Erase the sector */
                IfxScuWdt_clearSafetyEndinitInline(endInitSafetyPassword);
                IfxFlash_eraseSector(currentSectorAddr);
                IfxScuWdt_setSafetyEndinitInline(endInitSafetyPassword);
                IfxFlash_waitUnbusy(FLASH_MODULE, type);

                /* Program the modified buffer back */
                programCachedSector(currentSectorAddr, type);
            }
            /* For full sector erase, just erase directly */
            else {
                IfxScuWdt_clearSafetyEndinitInline(endInitSafetyPassword);
                IfxFlash_eraseSector(currentSectorAddr);
                IfxScuWdt_setSafetyEndinitInline(endInitSafetyPassword);
                IfxFlash_waitUnbusy(FLASH_MODULE, type);
            }
        }
    }

    LED_OFF(LED_ERASE);
    return 0;
}

/* This function is called by the bootloader at a very late stage, before
 * chain-loading the firmware in the next stage. This can be used to revert all
 * the changes made to the clock settings, to ensure that the state of the
 * microcontroller is restored to its original settings */
void hal_prepare_boot(void)
{
}

/* If the IAP interface of the flash memory of the target requires it, this
 * function is called before every write and erase operations to unlock write
 * access to the flash. On some targets, this function may be empty. */
void hal_flash_unlock(void)
{
}

/* If the IAP interface of the flash memory requires locking/unlocking, this
 * function restores the flash write protection by excluding write accesses.
 * This function is called by the bootloader at the end of every write and erase
 * operations. */
void hal_flash_lock(void)
{
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t* data, int len)
{
    return hal_flash_write(address, data, len);
}

/*
 * Reads data from flash memory, first checking if the data is erased and
 * returning dummy erased byte values to prevent ECC errors
 */
int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t* data, int len)
{
    int bytesRead;

    const IfxFlash_FlashType type = getFlashTypeFromAddr(address);

    bytesRead = 0;
    while (bytesRead < len) {
        uint32_t pageAddress;
        uint32_t offset;
        int      isErased;

        pageAddress = GET_PAGE_ADDR(address);
        offset      = address % IFXFLASH_PFLASH_PAGE_LENGTH;
        isErased =
            flashIsErased(pageAddress, IFXFLASH_PFLASH_PAGE_LENGTH, type);

        while (offset < IFXFLASH_PFLASH_PAGE_LENGTH && bytesRead < len) {
            if (isErased) {
                data[bytesRead] = FLASH_BYTE_ERASED;
            }
            else {
                data[bytesRead] = *((uint8_t*)address);
            }
            address++;
            bytesRead++;
            offset++;
        }
    }

    LED_OFF(LED_READ);

    return 0;
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    return hal_flash_erase(address, len);
}

void RAMFUNCTION ext_flash_lock(void)
{
    hal_flash_lock();
}

void RAMFUNCTION ext_flash_unlock(void)
{
    hal_flash_unlock();
}

void do_boot(const uint32_t* app_offset)
{
    LED_OFF(LED_WOLFBOOT);
    Ifx_Ssw_jumpToFunction((void (*)(void))app_offset);
}

void arch_reboot(void)
{
    IfxScuRcu_performReset(IfxScuRcu_ResetType_system,
                           WOLFBOOT_AURIX_RESET_REASON);
}


#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
static int _cancelCb(uint16_t cancelSeq);
static int _connectCb(void* context, whCommConnected connect);

 /* Client configuration/contexts */
static whTransportMemClientContext tmcCtx[1]  = {0};
static whTransportClientCb         tmcCb[1]   = {WH_TRANSPORT_MEM_CLIENT_CB};

/* Globally exported HAL symbols */
whClientContext hsmClientCtx         = {0};
const int       hsmClientDevIdHash   = WH_DEV_ID_DMA;
#ifdef WOLFBOOT_SIGN_ML_DSA
/* Use DMA for massive ML DSA keys/signatures, too big for shm transport */
const int       hsmClientDevIdPubKey = WH_DEV_ID_DMA;
#else
const int       hsmClientDevIdPubKey = WH_DEV_ID;
#endif
const int       hsmClientKeyIdPubKey = 0xFF;
#ifdef EXT_ENCRYPT
#error "AURIX TC3xx does not support firmware encryption with wolfHSM (yet)"
const int       hsmClientDevIdCrypt = WH_DEV_ID;
const int       hsmClientKeyIdCrypt = 0xFF;
#endif


static int _cancelCb(uint16_t cancelSeq)
{
    HSM_SHM_CORE0_CANCEL_SEQ = cancelSeq;
    (void)tchsmHhHost2Hsm_Notify(TCHSM_HOST2HSM_NOTIFY_CANCEL);
    return 0;
}

static int _connectCb(void* context, whCommConnected connect)
{
    int ret;

    switch (connect) {
        case WH_COMM_CONNECTED:
            ret = tchsmHhHost2Hsm_Notify(TCHSM_HOST2HSM_NOTIFY_CONNECT);
            break;
        case WH_COMM_DISCONNECTED:
            ret = tchsmHhHost2Hsm_Notify(TCHSM_HOST2HSM_NOTIFY_DISCONNECT);
            break;
        default:
            ret = WH_ERROR_BADARGS;
            break;
    }

    return ret;
}

int hal_hsm_init_connect(void)
{
    int    rc = 0;
    size_t i;

    /* init shared memory buffers */
    uint32_t* req  = (uint32_t*)hsmShmCore0CommBuf;
    uint32_t* resp = (uint32_t*)hsmShmCore0CommBuf + HSM_SHM_CORE0_COMM_BUF_WORDS / 2;
    whTransportMemConfig tmcCfg[1] = {{
        .req       = req,
        .req_size  = HSM_SHM_CORE0_COMM_BUF_SIZE / 2,
        .resp      = resp,
        .resp_size = HSM_SHM_CORE0_COMM_BUF_SIZE / 2,
    }};


     /* Client configuration/contexts */
    whCommClientConfig          cc_conf[1] = {{
                  .transport_cb      = tmcCb,
                  .transport_context = (void*)tmcCtx,
                  .transport_config  = (void*)tmcCfg,
                  .client_id         = 1,
                  .connect_cb        = _connectCb,
     }};

     whClientConfig c_conf[1] = {{
         .comm = cc_conf,
         .cancelCb = _cancelCb,
     }};

    rc = hsm_ipc_init();
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    /* init shared memory buffers */
    for (i = 0; i < HSM_SHM_CORE0_COMM_BUF_WORDS; i++) {
        hsmShmCore0CommBuf[i] = 0;
    }

    rc = wh_Client_Init(&hsmClientCtx, c_conf);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    rc = wh_Client_CommInit(&hsmClientCtx, NULL, NULL);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    return rc;
}

int hal_hsm_disconnect(void)
{
    int rc;

    rc = wh_Client_CommClose(&hsmClientCtx);
    if (rc != 0) {
        wolfBoot_panic();
    }

    rc = wh_Client_Cleanup(&hsmClientCtx);
    if (rc != 0) {
        wolfBoot_panic();
    }

    return 0;
}


#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */

#if defined(DEBUG_UART)

static int uartInit(void)
{
    /* Define local pin structures for init function */
    IfxAsclin_Rx_In  uartRxPin = UART_PIN_RX;
    IfxAsclin_Tx_Out uartTxPin = UART_PIN_TX;

    IfxAsclin_enableModule(g_asclinRegs);
    IfxAsclin_setClockSource(g_asclinRegs, IfxAsclin_ClockSource_noClock);

    IfxAsclin_initRxPin(&uartRxPin, IfxPort_InputMode_pullUp,
                        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxAsclin_initTxPin(&uartTxPin, IfxPort_OutputMode_pushPull,
                        IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxAsclin_setFrameMode(g_asclinRegs, IfxAsclin_FrameMode_initialise);

    /* Configure baudrate - must temporarily enable clocks */
    IfxAsclin_setClockSource(g_asclinRegs, IfxAsclin_ClockSource_ascFastClock);
    IfxAsclin_setPrescaler(g_asclinRegs, 1);
    if (IfxAsclin_setBitTiming(
            g_asclinRegs, (float32)UART_BAUDRATE,
            IfxAsclin_OversamplingFactor_16,
            IfxAsclin_SamplePointPosition_9,           /* Sample point 9 */
            IfxAsclin_SamplesPerBit_three) == FALSE) { /* Three samples */
        IfxAsclin_disableModule(g_asclinRegs);
        return -1;
    }
    IfxAsclin_setClockSource(g_asclinRegs, IfxAsclin_ClockSource_noClock);

    IfxAsclin_setDataLength(g_asclinRegs, IfxAsclin_DataLength_8);
    IfxAsclin_enableParity(g_asclinRegs, FALSE);
    IfxAsclin_setStopBit(g_asclinRegs, IfxAsclin_StopBit_1);
    IfxAsclin_setIdleDelay(g_asclinRegs, IfxAsclin_IdleDelay_0);

    IfxAsclin_enableLoopBackMode(g_asclinRegs, FALSE);
    IfxAsclin_setShiftDirection(g_asclinRegs,
                                IfxAsclin_ShiftDirection_lsbFirst);

    IfxAsclin_setRxFifoOutletWidth(g_asclinRegs, IfxAsclin_RxFifoOutletWidth_1);
    IfxAsclin_setRxFifoInterruptLevel(g_asclinRegs,
                                      IfxAsclin_RxFifoInterruptLevel_1);
    IfxAsclin_setRxFifoInterruptMode(g_asclinRegs,
                                     IfxAsclin_FifoInterruptMode_combined);
    IfxAsclin_setTxFifoInletWidth(g_asclinRegs, IfxAsclin_TxFifoInletWidth_1);
    IfxAsclin_setTxFifoInterruptLevel(g_asclinRegs,
                                      IfxAsclin_TxFifoInterruptLevel_15);
    IfxAsclin_setTxFifoInterruptMode(g_asclinRegs,
                                     IfxAsclin_FifoInterruptMode_combined);

    IfxAsclin_setFrameMode(g_asclinRegs, IfxAsclin_FrameMode_asc);

    IfxAsclin_setClockSource(g_asclinRegs, IfxAsclin_ClockSource_ascFastClock);

    IfxAsclin_disableAllFlags(g_asclinRegs);
    IfxAsclin_clearAllFlags(g_asclinRegs);

    IfxAsclin_enableRxFifoInlet(g_asclinRegs, TRUE);
    IfxAsclin_enableTxFifoOutlet(g_asclinRegs, TRUE);

    IfxAsclin_flushRxFifo(g_asclinRegs);
    IfxAsclin_flushTxFifo(g_asclinRegs);

    return 0; /* Success */
}

static int uartTx(const uint8_t c)
{
    /* Write the data value to the ASCLIN peripheral's transmit FIFO. */
    /* Note: IfxAsclin_write8 takes a pointer */
    uint8 data_to_send = c;
    IfxAsclin_write8(g_asclinRegs, &data_to_send, 1);

    /* Wait (poll) for the transmit FIFO to be empty */
    /* TODO: Consider adding a timeout mechanism here if necessary */
    while (IfxAsclin_getTxFifoFillLevel(g_asclinRegs) != 0) {
        /* Busy wait */
    }

    return 0; /* Success */
}

void uart_write(const char* buf, unsigned int sz)
{
    static char       rambuf[512];
    const static char lf = '\r';
    unsigned int      i;

    if (sz > sizeof(rambuf)) {
        sz = sizeof(rambuf);
    }
    memcpy(rambuf, buf, sz);
    buf = rambuf;

    for (i = 0; i < sz; i++) {
        /* If newline character is detected, send carriage return first */
        if (buf[i] == '\n') {
            /* Send carriage return before newline */
            if (uartTx(lf) != 0) {
                /* Handle error if needed */
                break;
            }
        }

        /* Call uart_tx for each byte, which now polls until TX FIFO is empty */
        if (uartTx((uint8_t)buf[i]) != 0) {
            /* Handle error if needed */
            break;
        }
    }
    /* No final wait needed here, as uart_tx waits after each byte */
}

#endif /* DEBUG_UART */
