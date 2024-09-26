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

#ifdef TEST_FLASH
static int test_flash(void);
#endif

/* Network updates can be signed with "--id 2" and placed into the normal update partition,
 * or they can be placed into the external flash at offset 0x100000 */
#ifndef PART_NET_ID
#define PART_NET_ID 2
#endif
#ifndef PART_NET_ADDR
#define PART_NET_ADDR 0x100000UL
#endif

/* Shared Memory between network and application cores */
/* first 64KB (0x10000) is used by wolfBoot and limited in nrf5340.ld */
#ifndef SHARED_MEM_ADDR
    #define SHARED_MEM_ADDR (0x20000000UL + (64 * 1024))
    #define SHARED_MEM_SIZE (256 * 1024) /* enable access to full 256KB for entire network update image */
#endif
/* Shared memory states */
#define SHARED_STATUS_UNKNOWN      0
#define SHARED_STATUS_READY        1
#define SHARED_STATUS_UPDATE_START 2
#define SHARED_STATUS_UPDATE_DONE  3
#define SHARED_STATUS_DO_BOOT      4

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

    /* application places firmware here */
    uint8_t  data[0];
} SharedMem_t;
static SharedMem_t* shm = (SharedMem_t*)SHARED_MEM_ADDR;


#ifdef DEBUG_UART
#ifndef UART_SEL
    #define UART_SEL 0 /* select UART 0 or 1 */
#endif
#if !defined(UART_PORT) && !defined(UART_PIN)
    #if UART_SEL == 0 && !defined(TARGET_nrf5340_net)
        #define UART_PORT 0
        #define UART_PIN  20
    #else
        #define UART_PORT 1
        #define UART_PIN  1
    #endif
#endif

void uart_init(void)
{
    /* nRF5340-DK:
     * App: UART0=P1.01, UART1=P0.20 */
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

    /* allow network core access to P1.01 - must be set from application core */
#ifdef TARGET_nrf5340_app
    GPIO_PIN_CNF(1, 1) = (GPIO_CNF_OUT | GPIO_CNF_MCUSEL(1));
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
        line = memchr(buf, sz, '\n');
        if (line == NULL) {
            uart_write_sz(buf, sz);
            break;
        }
        lineSz = line - buf;

        uart_write_sz(line, lineSz);
        uart_write_sz("\r", 1); /* handle CRLF */

        buf = line;
        sz -= lineSz;
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
    uint32_t page_sz = (address < FLASH_BASE_NET) ?
        FLASH_PAGESZ_APP :
        FLASH_PAGESZ_NET;
#ifdef DEBUG_FLASH
    wolfBoot_printf("Internal Flash Erase: addr 0x%x, len %d\n", address, len);
#endif
    for (p = address; p <= end; p += page_sz) {
        /* set both secure and non-secure registers */
        NVMC_CONFIG = NVMC_CONFIG_EEN;
        NVMC_CONFIGNS = NVMC_CONFIG_EEN;
        while (NVMC_READY == 0);
        *(volatile uint32_t *)p = 0xFFFFFFFF;
        while (NVMC_READY == 0);
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

static void clock_init(void)
{
#ifndef TARGET_nrf5340_net
    CLOCK_HFCLKSRC = 1; /* use external high frequency clock */
    CLOCK_HFCLKSTART = 1;
    /* wait for high frequency clock startup */
    while (CLOCK_HFCLKSTARTED == 0);
#endif
}

void sleep_us(unsigned int us)
{
    /* Calculate ops per us (128MHz=128 instructions per 1us */
    unsigned long nop_us = (CPU_CLOCK / 10000000);
    nop_us *= us;
    /* instruction for each iteration */
#ifdef DEBUG
    nop_us /= 5;
#else
    nop_us /= 2;
#endif
    while (nop_us-- > 0) {
        NOP();
    }
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

#define IMAGE_IS_NET_CORE(img) ( \
    (img->type & HDR_IMG_TYPE_PART_MASK) == PART_NET_ID && \
     img->fw_size < FLASH_SIZE_NET)
static int hal_net_get_image(struct wolfBoot_image* img)
{
    /* check the update partition for a network core update */
    int ret = wolfBoot_open_image(img, PART_UPDATE);
    if (ret == 0 && IMAGE_IS_NET_CORE(img)) {
        return 0;
    }
    /* if external flash is enabled, try an alternate location */
#ifdef EXT_FLASH
    ret = wolfBoot_open_image_external(img, PART_UPDATE, PART_NET_ADDR);
    if (ret == 0 && IMAGE_IS_NET_CORE(img)) {
        return 0;
    }
#endif
    return (ret != 0) ? ret : -1;
}

static void hal_net_check_version(void)
{
    int ret;
    struct wolfBoot_image img;
    uint32_t timeout;

#ifdef TARGET_nrf5340_app
    /* check the network core version */
    ret = hal_net_get_image(&img);
    if (ret == 0) {
        shm->app.version = img.fw_ver;
        shm->app.size = img.fw_size;
        wolfBoot_printf("Network: Ver 0x%x, Size %d\n",
            shm->app.version, shm->app.size);
    }
    else {
        wolfBoot_printf("Failed finding net core update on ext flash 0x%x\n",
            PART_NET_ADDR);
    }
    shm->app.magic = SHAREM_MEM_MAGIC;
    shm->app.status = SHARED_STATUS_READY;

    /* release network core - issue boot command */
    hal_net_core(0);

    /* wait for ready status from network core */
    timeout = 1000000;
    while (shm->net.magic != SHAREM_MEM_MAGIC &&
           shm->net.status != SHARED_STATUS_READY &&
           --timeout > 0) {
        /* wait */
    };
    if (timeout == 0) {
        wolfBoot_printf("Timeout: network core ready!\n");
    }

    /* check if network core can continue booting or needs to wait for update */
    if (shm->app.version == shm->net.version) {
        shm->app.status = SHARED_STATUS_DO_BOOT;
    }
#else /* net */
    ret = wolfBoot_open_image(&img, PART_BOOT);
    if (ret == 0) {
        shm->net.version = img.fw_ver;
        shm->net.size = img.fw_size;
        wolfBoot_printf("Network: Ver 0x%x, Size %d\n",
            shm->net.version, shm->net.size);
    }
    else {
        wolfBoot_printf("Error getting boot partition info\n");
    }
    shm->net.magic = SHAREM_MEM_MAGIC;
    shm->net.status = SHARED_STATUS_READY;

    wolfBoot_printf("Network version: 0x%x\n", shm->net.version);

    /* wait for do_boot or update */
    timeout = 1000000;
    while (shm->app.magic == SHAREM_MEM_MAGIC &&
           shm->app.status == SHARED_STATUS_READY &&
           --timeout > 0) {
            /* wait */
    };
    if (timeout == 0) {
        wolfBoot_printf("Timeout: app core boot signal!\n");
    }
#endif
exit:
    wolfBoot_printf("Status: App %d (ver %d), Net %d (ver %d)\n",
        shm->app.status, shm->app.version, shm->net.status, shm->net.version);
}

#ifdef TARGET_nrf5340_app
void hal_net_check_update(void)
{
    int ret;
    uint32_t timeout;
    struct wolfBoot_image img;

    /* handle update for network core */
    ret = hal_net_get_image(&img);
    if (ret == 0 && img.fw_ver > shm->net.version) {
        /* validate the update is valid */
        if (wolfBoot_verify_integrity(&img) == 0 &&
            wolfBoot_verify_authenticity(&img) == 0)
        {
            /* relocate image to ram */
            ret = spi_flash_read(PART_NET_ADDR, shm->data, img.fw_size);
            if (ret >= 0) {
                /* signal network core to do update */
                shm->app.status = SHARED_STATUS_UPDATE_START;

                /* wait for update_done */
                timeout = 1000000;
                while (shm->net.magic == SHAREM_MEM_MAGIC &&
                       shm->net.status < SHARED_STATUS_UPDATE_DONE &&
                       --timeout > 0) {
                    sleep_us(1);
                };
                if (timeout == 0) {
                    wolfBoot_printf("Timeout: net core update done!\n");
                }
            }
        }
        else {
            wolfBoot_printf("Network image failed: Hdr %d, Hash %d, Sig %d\n",
                img.hdr_ok, img.sha_ok, img.signature_ok);
        }
    }
    /* inform network core to boot */
    shm->app.status = SHARED_STATUS_DO_BOOT;
}
#endif

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

#ifdef TARGET_nrf5340_app
    /* Allow the network core to access shared SDRAM at 0x2000_0000 */
    SPU_EXTDOMAIN_PERM(0) =
        (SPU_EXTDOMAIN_PERM_SECATTR_SECURE | SPU_EXTDOMAIN_PERM_UNLOCK);
#endif

    spi_flash_probe();

    hal_net_check_version();

#ifdef TEST_FLASH
    if (test_flash() != 0) {
        wolfBoot_printf("Internal flash Test Failed!\n");
    }
#endif
}


void hal_prepare_boot(void)
{
    /* TODO: Protect bootloader region of flash using SPU_FLASHREGION_PERM */
    //WOLFBOOT_ORIGIN
    //BOOTLOADER_PARTITION_SIZE

#ifdef TARGET_nrf5340_app
    hal_net_check_update();

    /* Restore defaults preventing network core from accessing shared SDRAM */
    SPU_EXTDOMAIN_PERM(0) =
        (SPU_EXTDOMAIN_PERM_SECATTR_NONSECURE | SPU_EXTDOMAIN_PERM_UNLOCK);
#endif
}

/* Test for internal flash erase/write */
/* Use TEST_EXT_FLASH to test external QSPI flash (see qspi_flash.c) */
#ifdef TEST_FLASH

#ifndef TEST_ADDRESS
    #define TEST_ADDRESS (FLASH_BASE_ADDR + (FLASH_SIZE - WOLFBOOT_SECTOR_SIZE))
#endif

/* #define TEST_FLASH_READONLY */

static int test_flash(void)
{
    int ret = 0;
    uint32_t i, len;
    uint8_t* pagePtr = (uint8_t*)TEST_ADDRESS;
    static uint8_t pageData[WOLFBOOT_SECTOR_SIZE];

    wolfBoot_printf("Internal flash test at 0x%x\n", TEST_ADDRESS);

    /* Setup test data */
    for (i=0; i<sizeof(pageData); i++) {
        ((uint8_t*)pageData)[i] = (i & 0xff);
    }

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    hal_flash_unlock();
    ret = hal_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    hal_flash_lock();
    if (ret != 0) {
        wolfBoot_printf("Erase Sector failed: Ret %d\n", ret);
        return ret;
    }

    /* Write Page */
    ret = hal_flash_write(TEST_ADDRESS, (uint8_t*)pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Compare Page */
    ret = memcmp((void*)TEST_ADDRESS, pageData, sizeof(pageData));
    if (ret != 0) {
        wolfBoot_printf("Check Data @ %d failed\n", ret);
        return ret;
    }

    wolfBoot_printf("Internal Flash Test Passed\n");
    return ret;
}
#endif /* TEST_FLASH */

#endif /* TARGET_* */
