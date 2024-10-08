/* nrf5340.c
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

/* Note: Also used by TARGET_nrf5340_net */
#ifdef TARGET_nrf5340

#include <stdint.h>

#include "image.h"
#include "string.h"
#include "printf.h"
#include "nrf5340.h"
#include "spi_flash.h"

/* TODO:
 * Key Storage: See 7.1.18.4.2 Key storage:
 * The key storage region of the UICR can contain multiple keys of different type, including symmetrical keys, hashes, public/private key pairs and other device secrets
 * Key headers are allocated an address range of 0x400 in the UICR memory map, allowing a total of 128 keys to be addressable inside the key storage region.
 * The key storage region contains multiple key slots, where each slot consists of a key header and an associated key value. The key value is limited to 128 bits.
 * Any key size greater than 128 bits must be divided and distributed over multiple key slot instances.
 */

#ifndef USE_RTC
    #define USE_RTC 0 /* Use RTC0 for sleep */
#endif

/* Network updates can be signed with "--id 2" and placed into the normal update partition,
 * or they can be placed into the external flash at offset 0x100000 */
/* Set Partition ID in .config using WOLFBOOT_PART_ID=2 */
#ifndef PART_NET_ID
#define PART_NET_ID 2 /* default */
#endif

/* Offset in external QSPI flash for network update
 * Comes from nrf5340_net.config WOLFBOOT_PARTITION_UPDATE_ADDRESS) */
#ifndef PART_NET_ADDR
#define PART_NET_ADDR 0x100000UL
#endif

/* IPC Channels 0/1 */
/* Channel 0: APP Send -> NET Recv
 * Channel 1: NET Send -> APP Recv */
#ifdef TARGET_nrf5340_app
    #define USE_IPC_SEND  0
    #define USE_IPC_RECV  1
#else
    #define USE_IPC_SEND  1
    #define USE_IPC_RECV  0
#endif

/* SHM: Shared Memory between network and application cores */
/* first 64KB (0x10000) is used by wolfBoot and limited in nrf5340.ld */
#ifndef SHARED_MEM_ADDR
    #define SHARED_MEM_ADDR (0x20000000UL + (64 * 1024))
#endif

/* Shared memory states (mask, easier to check) */
#define SHARED_STATUS_UNKNOWN      0x00
#define SHARED_STATUS_READY        0x01
#define SHARED_STATUS_VERSION      0x02
#define SHARED_STATUS_UPDATE_START 0x04
#define SHARED_STATUS_UPDATE_DONE  0x08
#define SHARED_STATUS_DO_BOOT      0x10
#define SHARED_STATUS_TIMEOUT      0x80

#define SHAREM_MEM_MAGIC 0x5753484D /* WSHM */

typedef struct {
    uint32_t magic;
    uint32_t status;
    uint32_t version; /* always refers to network core version */
    uint32_t size;
} ShmInfo_t;

typedef struct {
    ShmInfo_t net; /* network core write location */
    ShmInfo_t app; /* application core write location */
} SharedCores_t;

typedef struct {
    SharedCores_t core;

    /* Note: If enableShm=0 do not access below */
    /* application places firmware here */
    uint8_t  data[FLASH_SIZE_NET];
    /* used as "swap" */
    uint8_t  swap[FLASH_PAGESZ_NET];
} SharedMem_t;

static int enableShm = 0;
static int doUpdateNet = 0;

#ifdef TARGET_nrf5340_app
static SharedMem_t* shm = (SharedMem_t*)SHARED_MEM_ADDR;
#else
static SharedCores_t shm_shadow;
static SharedMem_t*  shm = (SharedMem_t*)&shm_shadow;
#endif


/* UART */
#ifdef DEBUG_UART
#ifndef UART_SEL
    #define UART_SEL 0 /* select UART 0 or 1 */
#endif
#if !defined(UART_PORT) && !defined(UART_PIN)
    #if defined(TARGET_nrf5340_app)
        #define UART_PORT 0
        #define UART_PIN  20
        #define UART_NET_PORT 1
        #define UART_NET_PIN  1
    #else
        #define UART_PORT 1
        #define UART_PIN  1
    #endif
#endif

void uart_init(void)
{
    /* nRF5340-DK: (P0.20 or P1.01)
     * App: UART0=P0.20
     * Net: UART0=P1.01 */
    UART_ENABLE(UART_SEL) = 0;
    GPIO_PIN_CNF(UART_PORT, UART_PIN) = (GPIO_CNF_OUT
    #ifdef TARGET_nrf5340_net
        | GPIO_CNF_MCUSEL(1)
    #endif
    );
    UART_PSEL_TXD(UART_SEL) = (PSEL_PORT(UART_PORT) | UART_PIN);
    UART_BAUDRATE(UART_SEL) = BAUD_115200;
    UART_CONFIG(UART_SEL) = 0; /* Flow=Diabled, Stop=1-bit, Parity exclude */
    UART_ENABLE(UART_SEL) = 8;

    /* allow network core access to UART pin - must be set from app core */
#if defined(TARGET_nrf5340_app) && \
    defined(UART_NET_PORT) && defined(UART_NET_PIN)
    GPIO_PIN_CNF(UART_NET_PORT, UART_NET_PIN) =
        (GPIO_CNF_OUT | GPIO_CNF_MCUSEL(1));
#endif
}

#ifndef UART_TX_MAX_SZ
#define UART_TX_MAX_SZ 128
#endif
void uart_write_sz(const char* c, unsigned int sz)
{
    /* EasyDMA must be a RAM buffer */
    static uint8_t uartTxBuf[UART_TX_MAX_SZ];

    while (sz > 0) {
        unsigned int xfer = sz;
        if (xfer > sizeof(uartTxBuf))
            xfer = sizeof(uartTxBuf);
        memcpy(uartTxBuf, c, xfer);

        UART_EVENT_ENDTX(UART_SEL) = 0;

        UART_TXD_PTR(UART_SEL) = (uint32_t)uartTxBuf;
        UART_TXD_MAXCOUNT(UART_SEL) = xfer;
        UART_TASK_STARTTX(UART_SEL) = 1;
        while (UART_EVENT_ENDTX(UART_SEL) == 0);

        sz -= xfer;
        c += xfer;
    }
}

void uart_write(const char* buf, unsigned int sz)
{
    const char* line;
    unsigned int lineSz;
    do {
        /* find `\n` */
        line = memchr(buf, '\n', sz);
        if (line == NULL) {
            uart_write_sz(buf, sz);
            break;
        }
        lineSz = line - buf;
        if (lineSz > sz-1)
            lineSz = sz-1;

        uart_write_sz(buf, lineSz);
        uart_write_sz("\r\n", 2); /* handle CRLF */

        buf = line;
        sz -= lineSz + 1; /* skip \n, already sent */
    } while ((int)sz > 0);
}
#endif /* DEBUG_UART */

/* Non-volatile memory controller - use actual flash address */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t *src, *dst;
#ifdef DEBUG_FLASH
    wolfBoot_printf("Internal Flash Write: addr 0x%x, len %d\n", address, len);
#endif
    while (i < len) {
        if ((len - i > 3) && ((((address + i) & 0x03) == 0)  &&
                      ((((uint32_t)data) + i) & 0x03) == 0)) {
            src = (uint32_t *)data;
            dst = (uint32_t *)address;
            /* set both secure and non-secure registers */
            NVMC_CONFIG = NVMC_CONFIG_WEN;
            NVMC_CONFIGNS = NVMC_CONFIG_WEN;
            while (NVMC_READY == 0);
            dst[i >> 2] = src[i >> 2];
            while (NVMC_READY == 0);
            i+=4;
        } else {
            uint32_t val;
            uint8_t *vbytes = (uint8_t *)(&val);
            int off = (address + i) - (((address + i) >> 2) << 2);
            dst = (uint32_t *)(address - off);
            val = dst[i >> 2];
            vbytes[off] = data[i];
            /* set both secure and non-secure registers */
            NVMC_CONFIG = NVMC_CONFIG_WEN;
            NVMC_CONFIGNS = NVMC_CONFIG_WEN;
            while (NVMC_READY == 0);
            dst[i >> 2] = val;
            while (NVMC_READY == 0);
            i++;
        }
    }
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end = address + len - 1;
    uint32_t p;
#if defined(DEBUG_FLASH) && DEBUG_FLASH > 1
    wolfBoot_printf("Internal Flash Erase: addr 0x%x, len %d\n", address, len);
#endif
    /* mask to page start address */
    address &= ~(FLASH_PAGE_SIZE-1);
    for (p = address; p <= end; p += FLASH_PAGE_SIZE) {
        /* set both secure and non-secure registers */
        NVMC_CONFIG = NVMC_CONFIG_EEN;
        NVMC_CONFIGNS = NVMC_CONFIG_EEN;
        while (NVMC_READY == 0);
        *(volatile uint32_t *)p = 0xFFFFFFFF;
        while (NVMC_READY == 0);
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Internal Flash Erase: page 0x%x\n", p);
    #endif
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

#ifdef TARGET_nrf5340_net
/* external flash is access application core shared memory directly */

/* calculates location in shared memory */
static uintptr_t ext_flash_addr_calc(uintptr_t address)
{
    /* offset external flash addresses by the update partition address */
    address -= WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    /* check address */
    if (address >= (FLASH_SIZE_NET + FLASH_PAGESZ_NET)) {
        address = 0;
    }
    return address;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uintptr_t addr = ext_flash_addr_calc(address);
#ifdef DEBUG_FLASH
    wolfBoot_printf("Ext Write: Len %d, Addr 0x%x (off 0x%x) -> 0x%x\n",
        len, address, addr, data);
#endif
    if (enableShm)
        memcpy(shm->data + addr, data, len);
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uintptr_t addr = ext_flash_addr_calc(address);
#ifdef DEBUG_FLASH
    wolfBoot_printf("Ext Read: Len %d, Addr 0x%x (off 0x%x) -> %p\n",
        len, address, addr, data);
#endif
    if (enableShm)
        memcpy(data, shm->data + addr, len);
    else
        memset(data, FLASH_BYTE_ERASED, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    uintptr_t addr = ext_flash_addr_calc(address);
#ifdef DEBUG_FLASH
    wolfBoot_printf("Ext Erase: Len %d, Addr 0x%x (off 0x%x)\n",
        len, address, addr);
#endif
    if (enableShm)
        memset(shm->data + addr, FLASH_BYTE_ERASED, len);
    return 0;
}

void ext_flash_lock(void)
{
    /* no op */
}
void ext_flash_unlock(void)
{
    /* no op */
}
#endif /* TARGET_nrf5340_net */

static void clock_init(void)
{
#ifdef TARGET_nrf5340_app
    CLOCK_HFCLKSRC = 1; /* use external high frequency clock */
    CLOCK_HFCLKSTART = 1;
    /* wait for high frequency clock startup */
    while (CLOCK_HFCLKSTARTED == 0);
#endif
    /* Start low frequency clock - used by RTC */
    CLOCK_LFCLKSRC = 0; /* internal low power */
    CLOCK_LFCLKSTART = 1;
    /* wait for high frequency clock startup */
    while (CLOCK_LFCLKSTARTED == 0);

    RTC_PRESCALER(USE_RTC) = 0; /* 32768 per second */
}

void sleep_us(uint32_t usec)
{
    /* Calculate number ticks to wait */
    uint32_t comp = ((usec * 32768UL) / 1000000);
    if (comp == 0)
        comp = 1; /* wait at least 1 tick */
    if (comp > RTC_OVERFLOW)
        comp = RTC_OVERFLOW; /* max wait (512 seconds with prescaler=0) */

    RTC_CLEAR(USE_RTC) = 1;
    RTC_EVTENSET(USE_RTC) = RTC_EVTENSET_CC0;
    RTC_EVENT_CC(USE_RTC, 0) = 0; /* clear compare event */
    RTC_CC(USE_RTC, 0) = comp;
    RTC_START(USE_RTC) = 1;
    /* wait for compare event */
    while (RTC_EVENT_CC(USE_RTC, 0) == 0);
    RTC_STOP(USE_RTC) = 1;
}

#ifdef TARGET_nrf5340_app
void hal_net_core(int hold) /* 1=hold, 0=release */
{
    if (hold) {
        /* stop the network core from booting */
        NETWORK_FORCEOFF = NETWORK_FORCEOFF_HOLD;
    }
    else {
        /* release network core - errata 161 network core release */
        NETWORK_ERRATA_161 = 1;
        NETWORK_FORCEOFF = NETWORK_FORCEOFF_RELEASE;
        sleep_us(5);
        NETWORK_FORCEOFF = NETWORK_FORCEOFF_HOLD;
        sleep_us(1);
        NETWORK_FORCEOFF = NETWORK_FORCEOFF_RELEASE;
        NETWORK_ERRATA_161 = 0;
    }
}
#endif

static uint8_t* get_image_hdr(struct wolfBoot_image* img)
{
#ifdef EXT_FLASH
    return img->hdr_cache;
#else
    return img->hdr;
#endif
}
static uint16_t get_image_partition_id(struct wolfBoot_image* img)
{
    return wolfBoot_get_blob_type(get_image_hdr(img)) & HDR_IMG_TYPE_PART_MASK;
}

#define IMAGE_IS_NET_CORE(img) ( \
    (get_image_partition_id(img) == PART_NET_ID) && \
    (img->fw_size < (FLASH_SIZE_NET - IMAGE_HEADER_SIZE)))
static int hal_net_get_image(struct wolfBoot_image* img, ShmInfo_t* info)
{
    int ret;
#ifdef TARGET_nrf5340_app
    /* check the update partition for a network core update */
    ret = wolfBoot_open_image(img, PART_UPDATE);
    if (ret == 0 && !IMAGE_IS_NET_CORE(img)) {
        ret = -1;
    }
    /* if external flash is enabled, try an alternate location */
    #ifdef EXT_FLASH
    if (ret != 0) {
        ret = wolfBoot_open_image_external(img, PART_UPDATE,
            (uint8_t*)PART_NET_ADDR);
        if (ret == 0 && !IMAGE_IS_NET_CORE(img)) {
            ret = -1;
        }
    }
    #endif
#else /* TARGET_nrf5340_net */
    ret = wolfBoot_open_image(img, PART_BOOT);
#endif /* TARGET_nrf5340_* */
    if (ret == 0) {
        uint32_t ver = wolfBoot_get_blob_version(get_image_hdr(img));
        /* Note: network core fault writing to shared memory means application
         *       core did not enable access at run-time yet */
        info->version = ver;
        info->size = IMAGE_HEADER_SIZE + img->fw_size;
        wolfBoot_printf("Network Image: Ver 0x%x, Size %d\n",
            ver, img->fw_size);
    }
    else {
        info->version = 0; /* not known */
        wolfBoot_printf("Network Image: Update not found\n");
    }
    return ret;
}

static void hal_shm_init(void)
{
    IPC_SEND_CNF(USE_IPC_SEND)    = (1 << USE_IPC_SEND);
    IPC_RECEIVE_CNF(USE_IPC_RECV) = (1 << USE_IPC_RECV);
    IPC_EVENTS_RECEIVE(USE_IPC_SEND) = 0;
    IPC_EVENTS_RECEIVE(USE_IPC_RECV) = 0;

#ifdef TARGET_nrf5340_app
    /* Allow the network core to access shared SDRAM at 0x2000_0000 */
    SPU_EXTDOMAIN_PERM(0) =
        (SPU_EXTDOMAIN_PERM_SECATTR_SECURE | SPU_EXTDOMAIN_PERM_UNLOCK);
#endif
}

static void hal_shm_status_set(ShmInfo_t* info, uint32_t status)
{
    IPC_TASKS_SEND(USE_IPC_SEND) = 1;
    if (info != NULL) {
        info->magic = SHAREM_MEM_MAGIC;
        info->status = status;
    }
}

static uint32_t hal_shm_status_wait(ShmInfo_t* info, uint32_t status,
    uint32_t timeout_ms)
{
    uint32_t status_ret = SHARED_STATUS_UNKNOWN;
    int ret = 0;

    do {
        /* see if status shared already */
        if (info != NULL && (info->magic == SHAREM_MEM_MAGIC &&
                (info->status & status) != 0)) {
            status_ret = info->status;
            break;
        }
        /* Wait for event */
        while (IPC_EVENTS_RECEIVE(USE_IPC_RECV) == 0 && --timeout_ms > 0) {
            sleep_us(1000);
        }
        if (timeout_ms == 0) {
            status_ret = SHARED_STATUS_TIMEOUT;
            break;
        }
        /* clear event */
        IPC_EVENTS_RECEIVE(USE_IPC_RECV) = 0;
        /* if we got an event and "info" not provided, just return status to
         * signal event occurred */
        if (info == NULL) {
            status_ret = status;
            break;
        }
    } while (1);
    return status_ret;
}

static void hal_shm_cleanup(void)
{
#ifdef TARGET_nrf5340_app
    /* Restore defaults preventing network core from accessing shared SDRAM */
    SPU_EXTDOMAIN_PERM(0) =
        (SPU_EXTDOMAIN_PERM_SECATTR_NONSECURE | SPU_EXTDOMAIN_PERM_UNLOCK);
#endif
}

static const char* hal_shm_status_string(uint32_t status)
{
    switch (status) {
        case SHARED_STATUS_READY:
            return "Ready";
        case SHARED_STATUS_VERSION:
            return "Version";
        case SHARED_STATUS_UPDATE_START:
            return "Update Start";
        case SHARED_STATUS_UPDATE_DONE:
            return "Update Done";
        case SHARED_STATUS_DO_BOOT:
            return "Do boot";
        case SHARED_STATUS_TIMEOUT:
            return "Timeout";
        default:
            break;
    }
    return "Unknown";
}

static int hal_net_signal_wait_ready(uint32_t timeout_ms)
{
    int ret = 0;
    uint32_t status;

    /* wait for network core ready */
    do {
        hal_shm_status_set(NULL, SHARED_STATUS_READY);
        status = hal_shm_status_wait(NULL, SHARED_STATUS_READY, 1);
    } while (status == SHARED_STATUS_TIMEOUT && --timeout_ms > 0);
    if (timeout_ms == 0 && status == SHARED_STATUS_TIMEOUT) {
        ret = -1;
    }
    return ret;
}
/* Handles network core updates */
static void hal_net_check_version(void)
{
    int ret;
    struct wolfBoot_image img;
    uint32_t timeout, status = 0;

#ifdef TARGET_nrf5340_app
    /* check the network core version */
    hal_net_get_image(&img, &shm->core.app);

    /* release network core - issue boot command */
    hal_net_core(0);

    wolfBoot_printf("Waiting for ready from net core...\n");

    /* wait for ready status from network core */
    ret = hal_net_signal_wait_ready(500);
    if (ret == 0) {
        enableShm = 1;
        wolfBoot_printf("Net core ready\n");

        /* wait for version */
        status = hal_shm_status_wait(&shm->core.net,
            SHARED_STATUS_VERSION, 2*1000);
    }
    else {
        wolfBoot_printf("Net core timeout, disable shared mem\n");
    }

    /* check if network core can continue booting or needs to wait for update */
    if (ret != 0 || shm->core.app.version <= shm->core.net.version) {
        wolfBoot_printf("Network Core: Releasing for boot\n");
    }
    else {
        wolfBoot_printf("Found Network Core update: Ver %d->%d, Size %d->%d\n",
            shm->core.net.version, shm->core.app.version,
            shm->core.net.size, shm->core.app.size);

        /* validate the update is valid */
        if (wolfBoot_verify_integrity(&img) == 0 &&
            wolfBoot_verify_authenticity(&img) == 0)
        {
            wolfBoot_printf("Network image valid, loading into shared mem\n");
            /* initialize remainder of shared memory with 0xFF (erased) */
            memset(shm->data + shm->core.app.size, FLASH_BYTE_ERASED,
                sizeof(shm->data) - shm->core.app.size);
            /* relocate image to shared ram */
        #ifdef EXT_FLASH
            ret = ext_flash_read(PART_NET_ADDR, shm->data, shm->core.app.size);
        #else
            memcpy(shm->data, img.hdr, shm->core.app.size);
        #endif
            if (ret >= 0) {
                doUpdateNet = 1;

                /* signal network core to do update */
                hal_shm_status_set(&shm->core.app, SHARED_STATUS_UPDATE_START);

            #ifndef NRF_SYNC_CORES
                wolfBoot_printf("Waiting for net core update to finish...\n");

                /* wait for update_done - 30 seconds */
                status = hal_shm_status_wait(&shm->core.net,
                    SHARED_STATUS_UPDATE_DONE, 30*1000);
                if (status == SHARED_STATUS_UPDATE_DONE) {
                    wolfBoot_printf("Network core firmware update done\n");
                }
            #else
                wolfBoot_printf("Continuing boot while network core updates\n");
            #endif
            }
        }
        else {
            wolfBoot_printf("Network image failed: Hdr %d, Hash %d, Sig %d\n",
                img.hdr_ok, img.sha_ok, img.signature_ok);
        }
    }
    /* inform network core to boot */
    hal_shm_status_set(&shm->core.app, SHARED_STATUS_DO_BOOT);
#else /* TARGET_nrf5340_net */
    /* wait for IPC event indicating application core exists */
    ret = hal_net_signal_wait_ready(500);
    if (ret == 0) {
        /* enable use of shared memory */
        shm = (SharedMem_t*)SHARED_MEM_ADDR;
        enableShm = 1;
    }
    else {
        wolfBoot_printf("App core timeout, disable shared mem\n");
    }

    /* inform application core we are ready */
    hal_shm_status_set(&shm->core.net, SHARED_STATUS_READY);

    if (enableShm) {
        wolfBoot_printf("App core ready\n");

        hal_net_get_image(&img, &shm->core.net);
        hal_shm_status_set(&shm->core.net, SHARED_STATUS_VERSION);

        /* wait for do_boot or update from app core - 2 seconds */
        wolfBoot_printf("Waiting for update or boot from app core...\n");
        status = hal_shm_status_wait(&shm->core.app,
            (SHARED_STATUS_UPDATE_START | SHARED_STATUS_DO_BOOT), 2*1000);

        /* are we updating? */
        if (status == SHARED_STATUS_UPDATE_START) {
            wolfBoot_printf("Starting update: Ver %d->%d, Size %d->%d\n",
                shm->core.net.version, shm->core.app.version,
                shm->core.net.size, shm->core.app.size);
            doUpdateNet = 1;

            /* trigger update */
            wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);
        }
    }
    /* proceed to update_flash routines */
#endif /* TARGET_nrf5340_* */
exit:
    wolfBoot_printf("Status: App %s (ver %d), Net %s (ver %d)\n",
        hal_shm_status_string(shm->core.app.status), shm->core.app.version,
        hal_shm_status_string(shm->core.net.status), shm->core.net.version);
}


void hal_init(void)
{
#ifdef DEBUG_UART
    const char* bootStr = "wolfBoot HAL Init (" CORE_STR " core)\n";
#endif

    clock_init();

#ifdef DEBUG_UART
    uart_init();
    uart_write(bootStr, strlen(bootStr));
#endif

    hal_shm_init();

    /* need early init of external flash to support checking network core */
    spi_flash_probe();

    hal_net_check_version();
}


void hal_prepare_boot(void)
{
    /* TODO: Protect bootloader region of flash using SPU_FLASHREGION_PERM */
    //WOLFBOOT_ORIGIN
    //BOOTLOADER_PARTITION_SIZE
    //FLASHREGION[n].PERM

    if (enableShm) {
    #ifdef TARGET_nrf5340_net
        if (doUpdateNet) {
            /* signal application core update done */
            struct wolfBoot_image img;
            /* Reopen image and refresh information */
            hal_net_get_image(&img, &shm->core.net);
            wolfBoot_printf("Network version (after update): 0x%x\n",
                shm->core.net.version);
            hal_shm_status_set(&shm->core.net, SHARED_STATUS_UPDATE_DONE);
        }
        else {
            hal_shm_status_set(&shm->core.net, SHARED_STATUS_DO_BOOT);
        }
    #endif

    #if defined(TARGET_nrf5340_app) && defined(NRF_SYNC_CORES)
        /* if core synchronization enabled,
         * then wait for update_done or do_boot (5 seconds, 30 for update) */
        wolfBoot_printf("Waiting for network core...\n");
        (void)hal_shm_status_wait(&shm->core.net,
            (SHARED_STATUS_UPDATE_DONE | SHARED_STATUS_DO_BOOT),
            doUpdateNet ? 30*1000 : 5*1000);
    #endif
    }

    hal_shm_cleanup();
}

#endif /* TARGET_* */
