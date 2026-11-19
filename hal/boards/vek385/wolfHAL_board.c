/* wolfHAL_board.c
 *
 * AMD VEK385 board on wolfHAL: PL011 console, NOR flash on the Cadence
 * OSPI (optional DMA reads), generic timer, DTS addresses.
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
#include <stddef.h>

#include "hal.h"
#include "hal/versal2.h"
#include "printf.h"
#include "loader.h"
#include "aarch64_arch.h"
#include "wolfHAL_board.h"

void hal_delay_us(uint32_t us);
uint64_t hal_get_timer_us(void);

/* wolfHAL devices */

/* wolfHAL timeout tick: generic timer in us. Wraps after ~71 min; the
 * timeout check handles it. */
static uint32_t vek385_tick_us(void)
{
    return (uint32_t)hal_get_timer_us();
}

/* Bounds for the waits. A status wait covers the slowest erase, at most
 * 1 s (datasheet Table 48); a DMA read covers a whole image. */
#define VEK385_UART_TIMEOUT_US   1000000U
#define VEK385_OSPI_TIMEOUT_US   1000000U
#define VEK385_FLASH_TIMEOUT_US  60000000U
#define VEK385_DMA_TIMEOUT_US    60000000U

/* Console: 8N1 at DEBUG_UART_BAUD from UART_CLK_REF. Init sets it up
 * even when the PLM or BL31 already did. */
static whal_Timeout vek385_uart_timeout = {
    VEK385_UART_TIMEOUT_US, 0, vek385_tick_us
};
whal_Uart g_wbUart = {
    .base = DEBUG_UART_BASE,
    .cfg = &(whal_Pl011_Uart_Cfg) {
        .ibrd = WHAL_PL011_UART_IBRD(UART_CLK_REF, DEBUG_UART_BAUD),
        .fbrd = WHAL_PL011_UART_FBRD(UART_CLK_REF, DEBUG_UART_BAUD),
        .lcrH = WHAL_PL011_UART_LCRH_8N1_FIFO,
        .timeout = &vek385_uart_timeout,
    },
};

#ifdef WOLFBOOT_OSPI_DMA
/* Cache upkeep for the DMA buffer. DMA writes memory behind the cache, so
 * flush the buffer before the transfer and drop stale lines after it. */
static void versal2_dcache_clean_inval(void *buf, size_t len);
static void versal2_dcache_inval(void *buf, size_t len);

static whal_Timeout vek385_dma_timeout = {
    VEK385_DMA_TIMEOUT_US, 0, vek385_tick_us
};
static whal_Versal2_OspiDma_ChCfg versal2_dma_xfer;
static whal_Dma versal2_dma = {
    .base = WHAL_VERSAL2_OSPI_BASE,
    .driver = &whal_Versal2_OspiDma_Driver,
    .cfg = &(whal_Versal2_OspiDma_Cfg) {
        .srcRdAddr = WHAL_VERSAL2_OSPI_DMA_SRC_RD_ADDR,
        .dstCtrl = WHAL_VERSAL2_OSPI_DMA_DST_CTRL,
        .CacheCleanInval = versal2_dcache_clean_inval,
        .CacheInval = versal2_dcache_inval,
        .timeout = &vek385_dma_timeout,
        .xfer = &versal2_dma_xfer,
    },
};

/* Point the OSPI DMA at a read buffer: the OSPI driver calls this for the
 * DMA part of a read. */
static whal_Error vek385_dma_set_read_buf(whal_Dma *dma, size_t ch,
                                          void *buf, size_t len)
{
    whal_Versal2_OspiDma_ChCfg x = { .buf = buf, .len = len };

    return whal_Dma_Configure(dma, ch, &x);
}

#define VEK385_DMA (&versal2_dma)
#define VEK385_DMA_SET_BUF vek385_dma_set_read_buf
#define VEK385_DMA_MAX_LEN WHAL_VERSAL2_OSPI_DMA_MAX_LEN
/* The DMA buffer and length must be whole cache lines (no shared lines). */
#define VEK385_DMA_ALIGN CACHE_LINE_SIZE
#else
#define VEK385_DMA NULL
#define VEK385_DMA_SET_BUF NULL
#define VEK385_DMA_MAX_LEN 0
#define VEK385_DMA_ALIGN 0
#endif

/* OSPI controller, 1-bit: clock /32, CS 0, board read capture. Init turns
 * off what the PLM may leave on (DTR, PHY). */
static whal_Timeout vek385_ospi_timeout = {
    VEK385_OSPI_TIMEOUT_US, 0, vek385_tick_us
};
whal_SpiMem g_wbOspi = {
    .base = WHAL_VERSAL2_OSPI_BASE,
    .cfg = &(whal_CadenceOspi_SpiMem_Cfg) {
        .devSize = WHAL_VERSAL2_OSPI_DEV_SIZE_VEK385,
        .baudDiv = VERSAL2_OSPI_BAUD_DIV_32,
        .csLines = VERSAL2_OSPI_CS_LINES,
        .rdCapture = VERSAL2_OSPI_RD_CAPTURE,
        .indTrigBase = WHAL_VERSAL2_OSPI_IND_TRIG_BASE,
        .indTrigRange = WHAL_VERSAL2_OSPI_IND_TRIG_RANGE,
        .sramPartition = WHAL_VERSAL2_OSPI_SRAM_PARTITION,
        .dmaPeriphCfg = WHAL_VERSAL2_OSPI_DMA_PERIPH_CFG,
        .dma = VEK385_DMA,
        .dmaCh = WHAL_VERSAL2_OSPI_DMA_CH,
        .dmaAlign = VEK385_DMA_ALIGN,
        .dmaMaxLen = VEK385_DMA_MAX_LEN,
        .SetDmaReadBuf = VEK385_DMA_SET_BUF,
        .timeout = &vek385_ospi_timeout,
    },
};

/* MT35XU02G, 1-bit, 4-byte opcodes (Table 21). Only this ID; resets (octal
 * DDR first: the PLM leaves it there) and volatile 1-bit (81h), never B1h. */
static whal_Timeout vek385_flash_timeout = {
    VEK385_FLASH_TIMEOUT_US, 0, vek385_tick_us
};
whal_Flash g_wbExtFlash = {
    .cfg = &(whal_SpiMemNor_Flash_Cfg) {
        .spiMemDev = &g_wbOspi,
        .capacity = 0x10000000,         /* 256 MB */
        .pageSz = 256,
        .eraseSz = 0x20000,             /* 128 KB */
        .addrBytes = 4,
        .readOp = OSPI_OP_READ4,
        .readDummy = 0,
        .progOp = OSPI_OP_PAGE_PROGRAM4,
        .eraseOp = OSPI_OP_ERASE4_128K,
        .flags = WHAL_SPIMEM_NOR_FLAG_RESET |
                 WHAL_SPIMEM_NOR_FLAG_RESET_OCTAL_DTR,
        .id = VERSAL2_FLASH_JEDEC_ID,
        .errOp = WHAL_SPIMEM_NOR_MT35X_ERR_OP,
        .errMask = WHAL_SPIMEM_NOR_MT35X_ERR_MASK,
        .errClearOp = WHAL_SPIMEM_NOR_MT35X_ERR_CLEAR_OP,
        .rstRegOp = WHAL_SPIMEM_NOR_MT35X_RST_REG_OP,
        .rstRegAddrBytes = WHAL_SPIMEM_NOR_MT35X_RST_REG_ADDR_BYTES,
        .rstRegAddr = WHAL_SPIMEM_NOR_MT35X_RST_REG_ADDR,
        .rstRegVal = WHAL_SPIMEM_NOR_MT35X_RST_REG_VAL,
        .timeout = &vek385_flash_timeout,
    },
};

#ifdef WOLFBOOT_OSPI_DMA
/* Flush and drop the buffer's cache lines (dc civac). The test-app has no
 * flush_dcache_range(). After DMA, flushing is safe: no line is dirty. */
static void versal2_dcache_clean_inval(void *buf, size_t len)
{
    uintptr_t addr = (uintptr_t)buf;
    uintptr_t p = addr & ~(uintptr_t)(CACHE_LINE_SIZE - 1);

    for (; p < addr + len; p += CACHE_LINE_SIZE) {
        __asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

static void versal2_dcache_inval(void *buf, size_t len)
{
    versal2_dcache_clean_inval(buf, len);
}
#endif /* WOLFBOOT_OSPI_DMA */

/* OSPI bring-up */
#ifdef EXT_FLASH
static int vek385_ospi_init(void)
{
    uint8_t id[3] = { 0, 0, 0 };
    int ret = 0;

#ifdef WOLFBOOT_OSPI_DMA
    /* Clear DMA status an earlier stage may have left. */
    if (whal_Dma_Init(&versal2_dma) != WHAL_SUCCESS) {
        wolfBoot_printf("OSPI: DMA init failed\n");
        return -1;
    }
#endif

    /* Turn on the controller. No controller reset; the NOR driver's Init
     * fixes the flash mode. */
    if (whal_SpiMem_Init(&g_wbOspi) != WHAL_SUCCESS) {
        wolfBoot_printf("OSPI: controller init failed\n");
        return -1;
    }

    /* Check the flash ID (reset the flash and retry if needed) and clear
     * old flag status error bits, so the first write can't fail on them. */
    if (whal_Flash_Init(&g_wbExtFlash) != WHAL_SUCCESS) {
        ret = -1;
    }
    (void)whal_SpiMemNor_Flash_ReadId(&g_wbExtFlash, id);
    if (ret != 0) {
        wolfBoot_printf("OSPI: flash ID %02X %02X %02X is not the "
                        "MT35XU02G (2C 5B 1C), or no flash\n",
                        id[0], id[1], id[2]);
        return -1;
    }
    wolfBoot_printf("OSPI: flash ID %02X %02X %02X (Micron MT35XU02G)\n",
                    id[0], id[1], id[2]);
    return 0;
}
#endif /* EXT_FLASH */

/* Timer (ARM generic timer) */
uint64_t hal_timer_ms(void)
{
    return (timer_get_count() * 1000ULL) / timer_get_freq();
}

void hal_delay_us(uint32_t us)
{
    uint64_t deadline = timer_deadline_us(us);

    while (!timer_expired(deadline)) {
    }
}

uint64_t hal_get_timer_us(void)
{
    return (timer_get_count() * 1000000ULL) / timer_get_freq();
}

/* HAL entry points */

/* Firmware must set the timer frequency (CNTFID0, AM028). If CNTFRQ_EL0
 * is 0, print a warning that TIMER_CLK_FREQ is used instead. */
#if defined(DEBUG_UART)
static void vek385_timer_freq_warn(void)
{
    uint32_t freq;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) {
        wolfBoot_printf("Timer: CNTFRQ_EL0=0, assuming %lu Hz "
                        "(firmware must program CNTFID0)\n",
                        (unsigned long)TIMER_CLK_FREQ);
    }
}
#endif /* DEBUG_UART */

void hal_init(void)
{
#ifdef DEBUG_UART
    (void)whal_Uart_Init(&g_wbUart);
#endif

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    vek385_timer_freq_warn();
    wolfBoot_printf(
        "\n========================================\n"
        "wolfBoot Secure Boot - AMD Versal Gen 2\n"
#ifndef WOLFBOOT_REPRODUCIBLE_BUILD
        "Build: " __DATE__ " " __TIME__ "\n"
#endif
        "========================================\n");
    wolfBoot_printf("Current EL: %d\n", current_el());
#endif

#ifdef EXT_FLASH
    if (vek385_ospi_init() != 0) {
        wolfBoot_printf("OSPI: init failed - no flash\n");
        wolfBoot_panic();
    }
#endif
}

void hal_prepare_boot(void)
{
    /* uart_write() already waits for the bytes; do_boot() flushes the
     * caches. */
}

#ifdef MMU
void* hal_get_dts_address(void)
{
#ifdef WOLFBOOT_LOAD_DTS_ADDRESS
    return (void*)WOLFBOOT_LOAD_DTS_ADDRESS;
#else
    return NULL;
#endif
}

void* hal_get_dts_update_address(void)
{
#ifdef WOLFBOOT_DTS_UPDATE_ADDRESS
    return (void*)WOLFBOOT_DTS_UPDATE_ADDRESS;
#else
    return NULL;
#endif
}
#endif /* MMU */
