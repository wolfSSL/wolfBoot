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
#include <stdarg.h>
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
/* TARGET_aurix_tc3xx_hsm: HSM core build, see hal/aurix_tc3xx_hsm.c */
#ifdef TARGET_aurix_tc3xx_hsm
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
#include "ccb_hsm.h"
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

/* wolfHSM client ID presented to the HSM server. Defined by the build system
 * (WOLFHSM_CLIENT_ID in options.mk, default 1); must match the client ID the
 * keys are provisioned under in the whnvmtool config
 * (tools/scripts/tc3xx/wolfBoot-wolfHSM-keys.nvminit). */
#ifndef WOLFBOOT_WOLFHSM_CLIENT_ID
#error "WOLFBOOT_WOLFHSM_CLIENT_ID is not defined. Set WOLFHSM_CLIENT_ID in your .config or on the make command line."
#endif

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
/* Set WOLFHSM_NVM_ROOT_CA_LIST=1,2,3 in .config (or pass on the make command
 * line) to override the default single-root list. */
#ifndef WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST
#define WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST 1
#endif
const whNvmId  hsmNvmIdCertRootCAList[] = { WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST };
const uint16_t hsmNvmIdCertRootCACount  =
    sizeof(hsmNvmIdCertRootCAList) / sizeof(hsmNvmIdCertRootCAList[0]);
#endif

#elif defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) /*WOLFBOOT_ENABLE_WOLFHSM_CLIENT*/

/* map wolfBoot HAL layer wofHSM exports to their tchsm config vals */
const int      hsmDevIdHash             = HSM_DEVID;
const int      hsmDevIdPubKey           = HSM_DEVID;
/* Set WOLFHSM_NVM_ROOT_CA_LIST=1,2,3 in .config (or pass on the make command
 * line) to override the default single-root list. */
#ifndef WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST
#define WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST 1
#endif
const whNvmId  hsmNvmIdCertRootCAList[] = { WOLFBOOT_WOLFHSM_NVM_ROOT_CA_LIST };
const uint16_t hsmNvmIdCertRootCACount  =
    sizeof(hsmNvmIdCertRootCAList) / sizeof(hsmNvmIdCertRootCAList[0]);
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
#ifndef __clang__
/* clang rejects adding attributes after loader.h's inline definition */
void wolfBoot_panic(void) TC3_LONGCALL;
#endif
#endif

#ifdef WOLFBOOT_ENABLE_WOLFHSM_CLIENT
/* Every flash command from this core must be wrapped in a park/unpark sequence
 * to force the HSM core to execute from RAM while the host-driven flash
 * command completes.
 * The server app must be running first to process a park request */
#define HSM_PARK() (void)tchsmHhHost_HsmPark()
#define HSM_RELEASE() (void)tchsmHhHost_HsmRelease()
#else
#define HSM_PARK()
#define HSM_RELEASE()
#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT */

/* TC3 PFLASH has ECC, so a page can be programmed only once after a sector
 * erase. This HAL refuses writes to programmed pages and partial-sector
 * erases, so core must obey the NVM_FLASH_ECC rules and use the journal. */
#if !defined(NVM_FLASH_ECC) || !defined(NVM_FLASH_JOURNAL)
#error "AURIX TC3xx needs NVM_FLASH_ECC=1 and NVM_FLASH_JOURNAL=1"
#endif
#if WOLFBOOT_FLASH_WRITE_UNIT != TC3_PFLASH_PAGE_SIZE
#error "AURIX TC3xx: WOLFBOOT_FLASH_WRITE_UNIT must be the PFLASH page size"
#endif

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
#ifndef TARGET_aurix_tc3xx_hsm
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
#ifndef TARGET_aurix_tc3xx_hsm
    wolfBoot_printf("Hello from TC3xx wolfBoot on Tricore: V%d\n",
                    WOLFBOOT_VERSION);
#else
    wolfBoot_printf("Hello from TC3xx wolfBoot on HSM: V%d\n",
                    WOLFBOOT_VERSION);
#endif
#endif /* DEBUG_UART */

    /* Catch bus errors due to ECC faults. Reenabled on application boot */
    TC3_CAPTURE_BUS_ERRORS();
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

#ifndef TARGET_aurix_tc3xx_hsm
    tc3tc_isr_Cleanup();
    tc3tc_traps_DeinitBTV();

    /* Undo pre-init*/
    tc3tc_UnpreInit();
#endif

    /* Reenable bus trap/exception masking */
    TC3_ENFORCE_BUS_ERRORS();
}

#ifndef TARGET_aurix_tc3xx_hsm
void do_boot(const uint32_t* app_offset)
{
    LED_OFF(LED_WOLFBOOT);
    TC3TC_JMPI((uint32_t)app_offset);
}
#endif

RAMFUNCTION void arch_reboot(void)
{
#ifdef TARGET_aurix_tc3xx_hsm
    tc3arm_HsmBridgeSysReset();
#else
    tc3_Scu_TriggerSwReset(1, WOLFBOOT_AURIX_RESET_REASON);
#endif
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

        /* Fill the page buffer with the erased word value */
        {
            uint32_t i;
            for (i = 0; i < TC3_PFLASH_PAGE_SIZE / sizeof(uint32_t); i++) {
                pageBuffer[i] = FLASH_WORD_ERASED;
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

/* Write, erase and the blank-checked read below are each split into a worker
 * and a public wrapper that parks the HSM for the whole call. */

/* Program data into blank pages only. A page can be programmed once after an
 * erase, so a write that touches a programmed page fails and changes nothing. */
static int RAMFUNCTION _flashWrite(uint32_t address, const uint8_t* data,
                                   int size)
{
    uint32_t start;
    int      ret;

    if (size <= 0) {
        return 0;
    }

    LED_ON(LED_PROG);
    start = GET_PAGE_ADDR(address);
    ret   = tc3_flash_BlankCheck(start,
        GET_PAGE_ADDR(address + size - 1) + TC3_PFLASH_PAGE_SIZE - start);
    if (ret == 0) {
        ret = programBytesToErasedFlash(address, data, size);
    }
    LED_OFF(LED_PROG);

    return (ret == 0) ? 0 : -1;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t* data, int size)
{
    int ret;

    HSM_PARK();
    ret = _flashWrite(address, data, size);
    HSM_RELEASE();

    return ret;
}

/* Erase whole sectors only. A range that is not sector-aligned fails, because
 * a partial erase needs a read-modify-write that is not safe if power fails. */
static int RAMFUNCTION _flashErase(uint32_t address, int len)
{
    int ret;

    if (len <= 0) {
        return 0;
    }
    if ((address != GET_SECTOR_ADDR(address)) ||
        ((len % WOLFBOOT_SECTOR_SIZE) != 0)) {
        return -1;
    }

    LED_ON(LED_ERASE);
    ret = tc3_flash_Erase(address, (uint32_t)len);
    LED_OFF(LED_ERASE);

    return (ret == 0) ? 0 : -1;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret;

    HSM_PARK();
    ret = _flashErase(address, len);
    HSM_RELEASE();

    return ret;
}


/* If the IAP interface of the flash memory of the target requires it, this
 * function is called before every write and erase operations to unlock write
 * access to the flash. On some targets, this function may be empty. */
RAMFUNCTION void hal_flash_unlock(void) {}

/* If the IAP interface of the flash memory requires locking/unlocking, this
 * function restores the flash write protection by excluding write accesses.
 * This function is called by the bootloader at the end of every write and erase
 * operations. */
RAMFUNCTION void hal_flash_lock(void) {}

RAMFUNCTION int ext_flash_write(uintptr_t address, const uint8_t* data, int len)
{
    return hal_flash_write(address, data, len);
}

/*
 * Reads data from flash memory, first checking if the data is erased and
 * returning dummy erased byte values to prevent ECC errors. Returns the
 * number of bytes read, or -1 on error
 */
static int RAMFUNCTION _extFlashRead(uintptr_t address, uint8_t* data,
                                     int len)
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
    return bytesRead;
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t* data, int len)
{
    int ret;

    HSM_PARK();
    ret = _extFlashRead(address, data, len);
    HSM_RELEASE();

    return ret;
}

/* Erased check with the DMU Verify Erased command. A plain read cannot tell,
 * because reading erased PFLASH is an ECC error. */
int RAMFUNCTION hal_flash_is_erased(uint32_t address, int len)
{
    int ret;

    if (len <= 0) {
        return 1;
    }

    HSM_PARK();
    ret = tc3_flash_BlankCheck(address, (uint32_t)len);
    HSM_RELEASE();

    if (ret == 0) {
        return 1;
    }
    return (ret == TC3_FLASH_NOTBLANK) ? 0 : -1;
}

int RAMFUNCTION ext_flash_is_erased(uintptr_t address, int len)
{
    return hal_flash_is_erased((uint32_t)address, len);
}

RAMFUNCTION int ext_flash_erase(uintptr_t address, int len)
{
    return hal_flash_erase(address, len);
}

RAMFUNCTION void ext_flash_lock(void)
{
    hal_flash_lock();
}

RAMFUNCTION void ext_flash_unlock(void)
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
        .client_id         = WOLFBOOT_WOLFHSM_CLIENT_ID,
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

static whServerCryptoContext cryptoCtx[1] = {0};

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
            .devId       = HSM_DEVID,
    }};

    rc = wh_Nvm_Init(nvmCtx, nvmCfg);
    if (rc != WH_ERROR_OK) {
        wolfBoot_panic();
    }

    (void)wolfCrypt_Init();
    rc = wc_CryptoCb_RegisterDevice(HSM_DEVID, hsmCryptoCb, NULL);
    if (rc != 0) {
        wolfBoot_printf(
            "[ERROR] cryptocb registration for HASH failed, rc=%d\n", rc);
        wolfBoot_panic();
    }
#ifdef WOLF_CRYPTO_CB_FIND
    /* Route INVALID_DEVID calls (cert manager, DRBG) to hardware since the
     * CB-only ECC build has no software ECC to fall back to */
    wc_CryptoCb_SetDeviceFindCb(hsmCryptoCbFind);
#endif

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
