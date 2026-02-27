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

/* TC3 BSP specific headers */
#include "tc3_cfg.h"
#include "tc3/tc3.h"
#include "tc3/tc3_gpio.h"
#include "tc3/tc3_uart.h"
#include "tc3/tc3_flash.h"
#include "tc3/tc3_clock.h"
#ifdef TC3_CFG_HAVE_BOARD
#include "tc3/tc3_board.h"
#endif
#ifdef WOLFBOOT_AURIX_TC3XX_HSM
#include "tc3/tc3arm.h"
#else
#include "tc3/tc3tc.h"
#include "tc3/tc3tc_isr.h"
#include "tc3/tc3tc_traps.h"
#endif

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)

/* wolfHSM headers */
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_transport_mem.h"
/* wolfHSM AURIX port headers */
#include "tchsm_hsmhost.h"
#include "tchsm_config.h"
#include "tchsm_common.h"

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

#include "wolfhsm/wh_client.h"
/* wolfHSM AURIX port headers */
#include "tchsm_hh_host.h"
#include "hsm_ipc.h"

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)

#include "wolfhsm/wh_nvm_flash.h"
#include "tchsm_hh_hsm.h"
#include "port_halflash_df1.h"

#endif

#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT || WOLFBOOT_ENABLE_WOLFHSM_SERVER */

#define FLASH_MODULE (0)
#define UNUSED_PARAMETER (0)
#define WOLFBOOT_AURIX_RESET_REASON (0x5742) /* "WB" */

/* Helper macros to gets the base address of the page, wordline, or sector that
 * contains byteAddress */
#define GET_PAGE_ADDR(addr) \
    ((uintptr_t)(addr) & ~(TC3_PFLASH_PAGE_SIZE - 1))
#define GET_WORDLINE_ADDR(addr) \
    ((uintptr_t)(addr) & ~(TC3_PFLASH_WORDLINE_SIZE - 1))
#define GET_SECTOR_ADDR(addr) ((uintptr_t)(addr) & ~(WOLFBOOT_SECTOR_SIZE - 1))


/* wolfHSM client context and configuration */
#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)

static int _connectCb(void* context, whCommConnected connect);

/* Client configuration/contexts */
static whTransportMemClientContext tmcCtx[1] = {0};
static whTransportClientCb         tmcCb[1]  = {WH_TRANSPORT_MEM_CLIENT_CB};

/* Globally exported HAL symbols */
whClientContext hsmClientCtx = {0};
const int       hsmDevIdHash = WH_DEV_ID_DMA;
#ifdef WOLFBOOT_SIGN_ML_DSA
/* Use DMA for massive ML DSA keys/signatures, too big for shm transport */
const int hsmDevIdPubKey = WH_DEV_ID_DMA;
#else
const int hsmDevIdPubKey = WH_DEV_ID;
#endif
const int hsmKeyIdPubKey = 0xFF;
#ifdef EXT_ENCRYPT
#error "AURIX TC3xx does not support firmware encryption with wolfHSM (yet)"
const int hsmDevIdCrypt = WH_DEV_ID;
const int hsmKeyIdCrypt = 0xFF;
#endif
#ifdef WOLFBOOT_CERT_CHAIN_VERIFY
const whNvmId hsmNvmIdCertRootCA = 1;
#endif

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/

/* map wolfBoot HAL layer wofHSM exports to their tchsm config vals */
const int     hsmDevIdHash       = INVALID_DEVID; /*HSM_DEVID once CCB enabled*/
const int     hsmDevIdPubKey     = INVALID_DEVID; /*HSM_DEVID once CCB enabled*/
const whNvmId hsmNvmIdCertRootCA = 1;
#ifdef EXT_ENCRYPT
#error "AURIX does not support firmware encryption with wolfHSM(yet)"
const int     hsmDevIdCrypt      = INVALID_DEVID; /*HSM_DEVID once CCB enabled*/
const int     hsmKeyIdCrypt      = 0xFF;
#endif

int hal_hsm_server_init(void);
int hal_hsm_server_cleanup(void);

#endif /* WOLFBOOT_ENABLE_WOLFHSM_SERVER */

#ifdef TC3_CFG_HAVE_TRICORE
/* Force longcall on printf functions (called from panic) */
void uart_printf(const char* fmt, ...) TC3_LONGCALL;
void uart_vprintf(const char* fmt, va_list argp) TC3_LONGCALL;
void wolfBoot_panic(void) TC3_LONGCALL;
#endif

/* RAM buffer to hold the contents of an entire flash sector*/
static uint32_t sectorBuffer[WOLFBOOT_SECTOR_SIZE / sizeof(uint32_t)];

/* Directly reads a page from PFLASH using word-aligned reads/writes */
static void RAMFUNCTION readPage32Aligned(uint32_t pageAddr, uint32_t* data)
{
    /* Use the tc3_flash_Read API for bulk reading */
    tc3_flash_Read(pageAddr, (uint8_t*)data, TC3_PFLASH_PAGE_SIZE);
}

/* Returns true if any of the pages spanned by address and len are erased */
static int RAMFUNCTION containsErasedPage(uint32_t address, size_t len)
{
    const uint32_t startPage = GET_PAGE_ADDR(address);
    const uint32_t endPage   = GET_PAGE_ADDR(address + len - 1);
    uint32_t       page;
    int            ret;

    for (page = startPage; page <= endPage; page += TC3_PFLASH_PAGE_SIZE) {
        ret = tc3_flash_BlankCheck(page, TC3_PFLASH_PAGE_SIZE);
        if (ret == 0) {
            /* Page is erased */
            return 1;
        }
        else if (ret != TC3_FLASH_NOTBLANK) {
            /* Error during blank check */
            return -1;
        }
    }

    return 0;
}

/* reads an entire flash sector into the RAM cache, making sure to never read
 * any pages from flash that are erased */
static void RAMFUNCTION cacheSector(uint32_t sectorAddress)
{
    const uint32_t startPage = GET_PAGE_ADDR(sectorAddress);
    const uint32_t endPage =
        GET_PAGE_ADDR(sectorAddress + WOLFBOOT_SECTOR_SIZE - 1);
    uint32_t* pageInSectorBuffer;
    uint32_t  page;
    int       ret;

    /* Iterate over every page in the sector, caching its contents if not
     * erased, and caching 0xFF if erased */
    for (page = startPage; page <= endPage; page += TC3_PFLASH_PAGE_SIZE) {
        pageInSectorBuffer =
            sectorBuffer + ((page - sectorAddress) / sizeof(uint32_t));

        ret = tc3_flash_BlankCheck(page, TC3_PFLASH_PAGE_SIZE);
        if (ret == 0) {
            /* Page is erased, fill with erased value */
            {
                uint32_t i;
                for (i = 0; i < TC3_PFLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
                    pageInSectorBuffer[i] = FLASH_BYTE_ERASED;
                }
            }
        }
        else if (ret == TC3_FLASH_NOTBLANK) {
            /* Page has data, read it */
            readPage32Aligned(page, pageInSectorBuffer);
        }
        else {
            /* Error during blank check */
            wolfBoot_panic();
        }
    }
}

#ifdef WOLFBOOT_AURIX_GPIO_TIMING
#define LED_PROG (0)
#define LED_ERASE (1)
#define LED_READ (2)
#define LED_WOLFBOOT (5)

#ifndef SWAP_LED_POLARITY
#define LED_ON_VAL 1
#define LED_OFF_VAL 0
#else
#define LED_ON_VAL 0
#define LED_OFF_VAL 1
#endif
#define LED_ON(led) tc3_gpiopin_SetOutput(board_leds[led], LED_ON_VAL)
#define LED_OFF(led) tc3_gpiopin_SetOutput(board_leds[led], LED_OFF_VAL)
#else
#define LED_ON(led)
#define LED_OFF(led)
#endif /* WOLFBOOT_AURIX_GPIO_TIMING */


#if defined(DEBUG_UART) || defined(UART_FLASH)
/* API matches wolfBoot for UART_DEBUG */
int  uart_tx(const uint8_t c);
int  uart_rx(uint8_t* c);
void uart_init(void);
void uart_write(const char* buf, unsigned int sz);

int uart_tx(const uint8_t c)
{
    tc3_uart_Write8(board_uart, c);
    return 1;
}

int uart_rx(uint8_t* c)
{
    /* Return 1 when read is successful, 0 otherwise */
    return (tc3_uart_Read8(board_uart, c) == 0);
}

void uart_init(void)
{
    tc3_uart_Init(board_uart);
}

void uart_write(const char* buf, unsigned int sz)
{
    while (sz > 0) {
        /* If newline character is detected, send carriage return first */
        if (*buf == '\n') {
            (void)uart_tx('\r');
        }
        (void)uart_tx(*buf++);
        sz--;
    }
}
#endif /* DEBUG_UART || UART_FLASH */


/* This function is called by the bootloader at the very beginning of the
 * execution. Ideally, the implementation provided configures the clock settings
 * for the target microcontroller, to ensure that it runs at at the required
 * speed to shorten the time required for the cryptography primitives to verify
 * the firmware images*/
void hal_init(void)
{
#ifndef WOLFBOOT_AURIX_TC3XX_HSM
    /* Update BTV to use RAM Trap Table */
    tc3tc_traps_InitBTV();

    /* setup ISR sub-system */
    tc3tc_isr_Init();
#endif

    /* setup clock system */
    tc3_clock_SetMax();

    /* disable external WATCHDOG on the board */
    bsp_board_wdg_Disable();

#ifdef WOLFBOOT_AURIX_GPIO_TIMING
    tc3_gpiopin_led_Init(board_leds, board_led_count, LED_OFF_VAL);
#endif /* WOLFBOOT_AURIX_GPIO_TIMING */

    LED_ON(LED_WOLFBOOT);
    LED_OFF(LED_PROG);
    LED_OFF(LED_ERASE);
    LED_OFF(LED_READ);

#ifdef DEBUG_UART
    uart_init();
#ifndef WOLFBOOT_AURIX_TC3XX_HSM
    wolfBoot_printf("Hello from TC3xx wolfBoot on Tricore: V%d\n",
                    WOLFBOOT_VERSION);
#else
    wolfBoot_printf("Hello from TC3xx wolfBoot on HSM: V%d\n",
                    WOLFBOOT_VERSION);
#endif
#endif /* DEBUG_UART */
}

/* This function is called by the bootloader at a very late stage, before
 * chain-loading the firmware in the next stage. This can be used to revert all
 * the changes made to the clock settings, to ensure that the state of the
 * microcontroller is restored to its original settings */
void hal_prepare_boot(void)
{

#ifdef WOLFBOOT_AURIX_GPIO_TIMING
    tc3_gpiopin_led_Deinit(board_leds, board_led_count);
#endif /* WOLFBOOT_AURIX_GPIO_TIMING */

#ifdef DEBUG_UART
    /* One final printf so we can block on transmit completion. Prevents reset
     * before last byte is transmitted */
    wolfBoot_printf("hal_prepare_boot\n");
    tc3_uart_BlockOnTC(board_uart);
    tc3_uart_Cleanup(board_uart);
#endif

    tc3_clock_SetBoot();

#ifndef WOLFBOOT_AURIX_TC3XX_HSM
    tc3tc_isr_Cleanup();
    tc3tc_traps_DeinitBTV();

    /* Undo pre-init*/
    tc3tc_UnpreInit();
#endif
}

#ifndef WOLFBOOT_AURIX_TC3XX_HSM
void do_boot(const uint32_t* app_offset)
{
    LED_OFF(LED_WOLFBOOT);
    TC3TC_JMPI((uint32_t)app_offset);
}
#endif

void arch_reboot(void)
{
    tc3_Scu_TriggerSwReset(1, WOLFBOOT_AURIX_RESET_REASON);
}

/* Programs unaligned input data to flash, assuming the underlying memory is
 * erased */
static int RAMFUNCTION programBytesToErasedFlash(uint32_t       address,
                                                 const uint8_t* data, int size)
{
    uint32_t pageBuffer[TC3_PFLASH_PAGE_SIZE / sizeof(uint32_t)];
    uint32_t pageAddress;
    uint32_t offset;
    uint32_t toWrite;
    int ret = 0;

    pageAddress = address & ~(TC3_PFLASH_PAGE_SIZE - 1);
    offset      = address % TC3_PFLASH_PAGE_SIZE;

    while (size > 0) {
        /* Calculate the number of bytes to write in the current page */
        toWrite = TC3_PFLASH_PAGE_SIZE - offset;
        if (toWrite > (uint32_t)size) {
            toWrite = (uint32_t)size;
        }

        /* Fill the page buffer with the erased byte value */
        {
            uint32_t i;
            for (i = 0; i < TC3_PFLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
                pageBuffer[i] = FLASH_BYTE_ERASED;
            }
        }

        /* Copy the new data into the page buffer at the correct offset */
        memcpy((uint8_t*)pageBuffer + offset, data, toWrite);

        /* Write the modified page buffer back to flash */
        ret = tc3_flash_Program(pageAddress, pageBuffer, TC3_PFLASH_PAGE_SIZE);
        if (ret != 0) {
            break;
        }

        size -= toWrite;
        data += toWrite;
        address += toWrite;
        pageAddress = address & ~(TC3_PFLASH_PAGE_SIZE - 1);
        offset      = address % TC3_PFLASH_PAGE_SIZE;
    }
    return ret;
}

/* Programs the contents of the cached sector buffer to flash */
static void RAMFUNCTION programCachedSector(uint32_t sectorAddress)
{
    uint32_t pageAddr;
    size_t   bufferIdx;
    int      ret;

    /* Program the whole sector page by page from sectorBuffer */
    for (bufferIdx = 0, pageAddr = sectorAddress;
         bufferIdx < WOLFBOOT_SECTOR_SIZE / sizeof(uint32_t);
         bufferIdx += TC3_PFLASH_PAGE_SIZE / sizeof(uint32_t),
        pageAddr += TC3_PFLASH_PAGE_SIZE) {

        ret = tc3_flash_Program(pageAddr, &sectorBuffer[bufferIdx],
                                TC3_PFLASH_PAGE_SIZE);
        if (ret != 0) {
            wolfBoot_panic();
        }
    }
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
    int      ret               = 0;
    uint32_t currentAddress    = address;
    int      remainingSize     = size;
    int      bytesWrittenTotal = 0;

    LED_ON(LED_PROG);

    /* Process the data sector by sector */
    while (remainingSize > 0) {
        uint32_t currentSectorAddress = GET_SECTOR_ADDR(currentAddress);
        uint32_t offsetInSector       = currentAddress - currentSectorAddress;
        uint32_t bytesInThisSector    = WOLFBOOT_SECTOR_SIZE - offsetInSector;

        /* Adjust bytes to write if this would overflow the current sector */
        if (bytesInThisSector > (uint32_t)remainingSize) {
            bytesInThisSector = remainingSize;
        }

        /* Determine the range of pages affected in this sector */
        const uint32_t startPage = GET_PAGE_ADDR(currentAddress);
        const uint32_t endPage =
            GET_PAGE_ADDR(currentAddress + bytesInThisSector - 1);
        uint32_t page;
        int      needsSectorRmw = 0;

        /* Check if any page within the range is not erased */
        for (page = startPage; page <= endPage; page += TC3_PFLASH_PAGE_SIZE) {
            ret = tc3_flash_BlankCheck(page, TC3_PFLASH_PAGE_SIZE);
            if (ret == TC3_FLASH_NOTBLANK) {
                needsSectorRmw = 1;
                break;
            }
            else if (ret != 0) {
                /* Error during blank check */
                ret = -1;
                LED_OFF(LED_PROG);
                return ret;
            }
        }

        /* If a page within the range is not erased, we need to
         * read-modify-write the sector */
        if (needsSectorRmw) {
            /* Read entire sector into RAM */
            cacheSector(currentSectorAddress);

            /* Erase the entire sector */
            ret = hal_flash_erase(currentSectorAddress, WOLFBOOT_SECTOR_SIZE);
            if (ret != 0) {
                break;
            }

            /* Modify the relevant part of the RAM sector buffer */
            memcpy((uint8_t*)sectorBuffer + offsetInSector,
                   data + bytesWrittenTotal, bytesInThisSector);

            /* Program the modified sector back into flash */
            programCachedSector(currentSectorAddress);
        }
        else {
            /* All affected pages are erased, program the data directly */
            ret = programBytesToErasedFlash(currentAddress,
                                            data + bytesWrittenTotal,
                                            bytesInThisSector);
            if (ret != 0) {
                ret = -1;
                break;
            }
        }

        /* Update pointers and counters */
        bytesWrittenTotal += bytesInThisSector;
        currentAddress += bytesInThisSector;
        remainingSize -= bytesInThisSector;
    }

    LED_OFF(LED_PROG);

    return ret;
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

    const uint32_t startSectorAddr = GET_SECTOR_ADDR(address);
    const uint32_t endAddress      = address + len - 1;
    const uint32_t endSectorAddr   = GET_SECTOR_ADDR(endAddress);
    uint32_t       currentSectorAddr;
    int            ret = 0;

    /* If address and len are both sector-aligned, perform simple bulk erase */
    if ((address == startSectorAddr) &&
        (endAddress == endSectorAddr + WOLFBOOT_SECTOR_SIZE - 1)) {

        ret = tc3_flash_Erase(startSectorAddr, endSectorAddr - startSectorAddr +
                                                   WOLFBOOT_SECTOR_SIZE);
        if (ret != 0) {
            ret = -1;
        }
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
                cacheSector(currentSectorAddr);

                /* Calculate which bytes within the sector to erase */
                uint32_t eraseStartOffset =
                    isPartialStart ? (address - currentSectorAddr) : 0;

                uint32_t eraseEndOffset = isPartialEnd
                                              ? (endAddress - currentSectorAddr)
                                              : (WOLFBOOT_SECTOR_SIZE - 1);

                uint32_t eraseLen = eraseEndOffset - eraseStartOffset + 1;

                /* Fill the section to be erased with the erased byte value */
                {
                    uint32_t i;
                    for (i = 0; i < eraseLen; i++) {
                        ((uint8_t*)sectorBuffer)[eraseStartOffset + i] =
                            FLASH_BYTE_ERASED;
                    }
                }

                /* Erase the sector */
                ret = tc3_flash_Erase(currentSectorAddr, WOLFBOOT_SECTOR_SIZE);
                if (ret != 0) {
                    ret = -1;
                    break;
                }

                /* Program the modified buffer back */
                programCachedSector(currentSectorAddr);
            }
            /* For full sector erase, just erase directly */
            else {
                ret = tc3_flash_Erase(currentSectorAddr, WOLFBOOT_SECTOR_SIZE);
                if (ret != 0) {
                    ret = -1;
                    break;
                }
            }
        }
    }

    LED_OFF(LED_ERASE);
    return ret;
}


/* If the IAP interface of the flash memory of the target requires it, this
 * function is called before every write and erase operations to unlock write
 * access to the flash. On some targets, this function may be empty. */
void hal_flash_unlock(void) {}

/* If the IAP interface of the flash memory requires locking/unlocking, this
 * function restores the flash write protection by excluding write accesses.
 * This function is called by the bootloader at the end of every write and erase
 * operations. */
void hal_flash_lock(void) {}

int ext_flash_write(uintptr_t address, const uint8_t* data, int len)
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

    LED_ON(LED_READ);

    bytesRead = 0;
    while (bytesRead < len) {
        uint32_t pageAddress;
        uint32_t offset;
        int      isErased;
        int      ret;

        pageAddress = GET_PAGE_ADDR(address);
        offset      = address % TC3_PFLASH_PAGE_SIZE;
        ret         = tc3_flash_BlankCheck(pageAddress, TC3_PFLASH_PAGE_SIZE);
        if ((ret != 0) && (ret != TC3_FLASH_NOTBLANK)) {
            /* Error during blank check */
            LED_OFF(LED_READ);
            return -1;
        }
        isErased = (ret == 0);

        /* Calculate how many bytes to read from this page */
        uint32_t bytesInThisPage = TC3_PFLASH_PAGE_SIZE - offset;
        if (bytesInThisPage > (uint32_t)(len - bytesRead)) {
            bytesInThisPage = len - bytesRead;
        }

        if (isErased) {
            /* Page is erased, fill with erased value */
            {
                uint32_t i;
                for (i = 0; i < bytesInThisPage; i++) {
                    data[bytesRead + i] = FLASH_BYTE_ERASED;
                }
            }
        }
        else {
            /* Page has data, read it in bulk */
            ret = tc3_flash_Read(address, data + bytesRead, bytesInThisPage);
            if (ret != 0 && ret != TC3_FLASH_ERROR_DSE) {
                /* Error reading flash (ignore DSE errors) */
                LED_OFF(LED_READ);
                return -1;
            }
        }

        address += bytesInThisPage;
        bytesRead += bytesInThisPage;
    }

    LED_OFF(LED_READ);
    return 0;
}

int ext_flash_erase(uintptr_t address, int len)
{
    return hal_flash_erase(address, len);
}

void ext_flash_lock(void)
{
    hal_flash_lock();
}

void ext_flash_unlock(void)
{
    hal_flash_unlock();
}


#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT

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
    uint32_t* req = (uint32_t*)hsmShmCore0CommBuf;
    uint32_t* resp =
        (uint32_t*)hsmShmCore0CommBuf + HSM_SHM_CORE0_COMM_BUF_WORDS / 2;
    whTransportMemConfig tmcCfg[1] = {{
        .req       = req,
        .req_size  = HSM_SHM_CORE0_COMM_BUF_SIZE / 2,
        .resp      = resp,
        .resp_size = HSM_SHM_CORE0_COMM_BUF_SIZE / 2,
    }};


    /* Client configuration/contexts */
    whCommClientConfig cc_conf[1] = {{
        .transport_cb      = tmcCb,
        .transport_context = (void*)tmcCtx,
        .transport_config  = (void*)tmcCfg,
        .client_id         = 1,
        .connect_cb        = _connectCb,
    }};

    whClientConfig c_conf[1] = {{
        .comm     = cc_conf,
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

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/

/* #include "ccb_hsm.h" */
static whTransportServerCb transportMemCb[1] = {WH_TRANSPORT_MEM_SERVER_CB};
static whTransportMemServerContext transportMemCtx[1] = {{0}};

/* HAL Flash state and configuration */
static HalFlashDf1Context tchsmFlashCtx[1]   = {{0}};
static whFlashCb    tchsmFlashCb[1] = {HAL_FLASH_DF1_CB};
static whNvmFlashContext nvmFlashCtx[1]  = {{0}};
static whNvmCb           nvmCb[1] = {WH_NVM_FLASH_CB};
static whNvmContext      nvmCtx[1] = {0};

static whServerCryptoContext cryptoCtx[1] = {{
    .devId = INVALID_DEVID, /* HSM_DEVID once CCB enabled */
}};

/* Global server context */
whServerContext hsmServerCtx = {0};

int hal_hsm_server_init(void)
{
    int rc = 0;

    /* Dummy request and response buffers */
    static uint8_t req[] = {0};
    static uint8_t resp[] = {0};
    /* Dummy transport config */
    whTransportMemConfig        transportMemCfg[1] = {{
           .req       = (whTransportMemCsr*)req,
           .req_size  = sizeof(req),
           .resp      = (whTransportMemCsr*)resp,
           .resp_size = sizeof(resp),
    }};
    /* Dummy comm config */
    whCommServerConfig commServerConfig[1] = {{
        .transport_cb      = transportMemCb,
        .transport_context = (void*)&transportMemCtx[0],
        .transport_config  = (void*)&transportMemCfg[0],
        .server_id         = 0,
    }};

    /* NVM callbacks and config */
    HalFlashDf1Config  tchsmFlashCfg[1]   = {{0}};
    /* NVM Configuration using tricore HAL Flash */
    whNvmFlashConfig  nvmFlashCfg[1]  = {{
          .config  = tchsmFlashCfg,
          .context = tchsmFlashCtx,
          .cb      = tchsmFlashCb,
    }};
    whNvmConfig nvmCfg[1] = {{
         .config  = nvmFlashCfg,
         .context = nvmFlashCtx,
         .cb      = nvmCb,
    }};

    whServerConfig serverCfg[1] = {{
            .comm_config = commServerConfig,
            .nvm         = nvmCtx,
            .crypto      = cryptoCtx,
            .devId       = INVALID_DEVID, /*HSM_DEVID once CCB enabled */
    }};

    rc = wh_Nvm_Init(nvmCtx, nvmCfg);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    (void)wolfCrypt_Init();

    rc = wc_InitRng_ex(cryptoCtx->rng, NULL, INVALID_DEVID);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    rc = wh_Server_Init(&hsmServerCtx, serverCfg);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    return rc;
}

int hal_hsm_server_cleanup(void) {
    int rc = 0;

    rc = wh_Server_Cleanup(&hsmServerCtx);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    rc = wc_FreeRng(cryptoCtx->rng);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    rc = wolfCrypt_Cleanup();
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    return rc;
}

#endif /* WOLFBOOT_ENABLE_WOLFHSM_SERVER */
