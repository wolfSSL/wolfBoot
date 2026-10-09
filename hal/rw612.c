/* rw612.c
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
#include "fsl_common.h"
#include "image.h"
#include "loader.h"

#include "fsl_clock.h"
#include "fsl_romapi_flexspi.h"

#ifdef DEBUG_UART
#include "fsl_io_mux.h"
#include "fsl_usart.h"
#endif

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
#include "fsl_trng.h"
#endif

#include "rw612.h"

#if (WOLFBOOT_SECTOR_SIZE % RW612_FLASH_SECTOR_SIZE) != 0
#error WOLFBOOT_SECTOR_SIZE must be a multiple of the 4KB NOR erase sector
#endif

uint32_t SystemCoreClock;

static flexspi_nor_config_t flash_cfg;
static const flexspi_nor_flash_driver_t *rom_flexspi;

#ifdef WOLFCRYPT_SECURE_MODE
void hal_trng_init(void);
int hal_trng_get_entropy(unsigned char *out, unsigned int len);
#endif

#ifdef TZEN
static void hal_sau_init(void)
{
    /* Non-secure callable area */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure: application flash area (boot partition) */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1, 0);

    /* Non-secure RAM, above the physical SRAM used by wolfBoot */
    sau_init_region(2, 0x20040000, 0x2012FFFF, 0);

    /* Group 0 clock/reset, FlexSPI, IO_MUX, DMA and crypto stay secure */
    sau_init_region(3, 0x40020000, 0x40021FFF, 0); /* RSTCTL1, CLKCTL1 */
    sau_init_region(4, 0x40100000, 0x40103FFF, 0); /* GPIO */
    sau_init_region(5, 0x40106000, 0x40109FFF, 0); /* FLEXCOMM0..3 */

    /* Their interrupts target the non-secure world */
    NVIC->ITNS[0] |= (1UL << GPIO_INTA_IRQn) | (1UL << GPIO_INTB_IRQn) |
        (0xFUL << FLEXCOMM0_IRQn);

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
}
#endif

#ifdef DEBUG_UART
void uart_init(void)
{
    usart_config_t config;

    CLOCK_AttachClk(kSFRO_to_FLEXCOMM3);
    IO_MUX_SetPinMux(IO_MUX_FC3_USART_DATA);

    USART_GetDefaultConfig(&config);
    config.baudRate_Bps = RW612_UART_BAUD;
    config.enableTx = true;
    config.enableRx = true;
    (void)USART_Init(USART3, &config, RW612_UART_CLK_HZ);
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            (void)USART_WriteBlocking(USART3, (const uint8_t *)"\r", 1U);
        }
        (void)USART_WriteBlocking(USART3, (const uint8_t *)&buf[i], 1U);
    }
}
#endif

#ifdef __WOLFBOOT
/* The boot ROM leaves the FlexSPI XIP cache off; enable it like SystemInit */
static void rw612_xip_cache_enable(void)
{
    if ((CACHE64_CTRL0->CCR & CACHE64_CTRL_CCR_ENCACHE_MASK) != 0U) {
        return;
    }
    CACHE64_CTRL0->CCR = CACHE64_CTRL_CCR_INVW1_MASK |
                         CACHE64_CTRL_CCR_INVW0_MASK;
    CACHE64_CTRL0->CCR |= CACHE64_CTRL_CCR_GO_MASK;
    while ((CACHE64_CTRL0->CCR & CACHE64_CTRL_CCR_GO_MASK) != 0U) {
    }
    CACHE64_CTRL0->CCR = CACHE64_CTRL_CCR_ENWRBUF_MASK |
                         CACHE64_CTRL_CCR_ENCACHE_MASK;
    CACHE64_POLSEL0->REG0_TOP = 0x07FFFC00U;
    CACHE64_POLSEL0->REG1_TOP = 0x0U;
    CACHE64_POLSEL0->POLSEL = 0x1U;
    __ISB();
    __DSB();
}
#endif

#if defined(BOOT_BENCHMARK) && defined(__WOLFBOOT)
/* DWT CYCCNT stops without a debugger; SysTick on the 1MHz LPOSC ticks in us */
uint64_t hal_get_timer_us(void)
{
    static uint64_t elapsed_us;
    static uint32_t last;
    static int started;
    uint32_t now;

    if (!started) {
        CLOCK_AttachClk(kLPOSC_to_SYSTICK_CLK);
        SysTick->LOAD = SysTick_LOAD_RELOAD_Msk;
        SysTick->VAL = 0U;
        SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
        last = SysTick->VAL;
        started = 1;
    }
    now = SysTick->VAL;
    elapsed_us += (last - now) & SysTick_LOAD_RELOAD_Msk;
    last = now;
    return elapsed_us;
}
#endif

void hal_init(void)
{
#ifdef __WOLFBOOT
    rw612_xip_cache_enable();
#endif
#if defined(__WOLFBOOT) && defined(DEBUG_UART)
    uart_init();
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
    hal_sau_init();
#endif
}

#ifdef __WOLFBOOT
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

static uint32_t RAMFUNCTION rw612_flash_offset(uint32_t address)
{
    return (address & RW612_FLASH_ALIAS_MASK) - RW612_FLASH_BASE;
}

static void RAMFUNCTION rw612_flash_cache_flush(void)
{
    FLEXSPI->AHBCR |= FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;
    FLEXSPI->AHBCR &= ~FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;

    CACHE64_CTRL0->CCR |= CACHE64_CTRL_CCR_INVW0_MASK |
                          CACHE64_CTRL_CCR_INVW1_MASK |
                          CACHE64_CTRL_CCR_GO_MASK;
    while ((CACHE64_CTRL0->CCR & CACHE64_CTRL_CCR_GO_MASK) != 0U) {
    }
    CACHE64_CTRL0->CCR &= ~(CACHE64_CTRL_CCR_INVW0_MASK |
                            CACHE64_CTRL_CCR_INVW1_MASK);
    __DSB();
    __ISB();
}

static int RAMFUNCTION rw612_flash_init(void)
{
    uint32_t tree;
    uint32_t primask;
    status_t st = kStatus_Success;

    if (rom_flexspi != NULL) {
        return 0;
    }

    tree = ((SOCCTRL->CHIP_INFO & 0x0FU) == 0U) ?
        RW612_ROM_API_TREE_A0 : RW612_ROM_API_TREE_A1;
    memcpy(&flash_cfg, (const void *)RW612_FCB_ADDRESS, sizeof(flash_cfg));

    primask = __get_PRIMASK();
    __disable_irq();
    rom_flexspi = (const flexspi_nor_flash_driver_t *)(uintptr_t)
        ((const uint32_t *)(uintptr_t)tree)[RW612_ROM_API_FLEXSPI_IDX];
    st = rom_flexspi->init(RW612_FLEXSPI_INSTANCE, &flash_cfg);
    rw612_flash_cache_flush();
    __set_PRIMASK(primask);

    if (st != kStatus_Success) {
        rom_flexspi = NULL;
        return -1;
    }
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t page[RW612_FLASH_PAGE_SIZE / sizeof(uint32_t)];
    uint32_t offset;
    uint32_t page_off;
    uint32_t chunk;
    uint32_t primask;
    uint32_t i;
    status_t st = kStatus_Success;
    int ret = 0;

    if (len < 0) {
        return -1;
    }
    if (rw612_flash_init() != 0) {
        return -1;
    }

    offset = rw612_flash_offset(address);
    while ((len > 0) && (ret == 0)) {
        page_off = offset & (RW612_FLASH_PAGE_SIZE - 1U);
        chunk = RW612_FLASH_PAGE_SIZE - page_off;
        if (chunk > (uint32_t)len) {
            chunk = (uint32_t)len;
        }

        /* Pad with 0xFF in a loop: memset is not in RAM during self-update */
        for (i = 0; i < (RW612_FLASH_PAGE_SIZE / sizeof(uint32_t)); i++) {
            page[i] = 0xFFFFFFFFU;
        }
        memcpy((uint8_t *)page + page_off, data, chunk);

        primask = __get_PRIMASK();
        __disable_irq();
        st = rom_flexspi->page_program(RW612_FLEXSPI_INSTANCE, &flash_cfg,
                offset - page_off, page, false);
        rw612_flash_cache_flush();
        __set_PRIMASK(primask);

        if (st != kStatus_Success) {
            ret = -1;
        }
        offset += chunk;
        data += chunk;
        len -= (int)chunk;
    }

    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t offset;
    uint32_t end;
    uint32_t primask;
    status_t st = kStatus_Success;
    int ret = 0;

    if (len <= 0) {
        return -1;
    }
    if (rw612_flash_init() != 0) {
        return -1;
    }

    offset = rw612_flash_offset(address);
    end = offset + (uint32_t)len;
    offset -= offset % RW612_FLASH_SECTOR_SIZE;

    while ((offset < end) && (ret == 0)) {
        primask = __get_PRIMASK();
        __disable_irq();
        st = rom_flexspi->erase_sector(RW612_FLEXSPI_INSTANCE, &flash_cfg,
                offset);
        rw612_flash_cache_flush();
        __set_PRIMASK(primask);

        if (st != kStatus_Success) {
            ret = -1;
        }
        offset += RW612_FLASH_SECTOR_SIZE;
    }

    return ret;
}

#if defined(WOLFCRYPT_SECURE_MODE) && !defined(NONSECURE_APP)
static int trng_ready;

void hal_trng_init(void)
{
    trng_config_t config;

    trng_ready = 0;
    if ((TRNG_GetDefaultConfig(&config) == kStatus_Success) &&
            (TRNG_Init(TRNG, &config) == kStatus_Success)) {
        trng_ready = 1;
    }
}

void hal_trng_fini(void)
{
    trng_ready = 0;
}

int hal_trng_get_entropy(unsigned char *out, unsigned int len)
{
    if (!trng_ready) {
        return -1;
    }
    return (TRNG_GetRandomData(TRNG, out, len) == kStatus_Success) ? 0 : -1;
}
#endif
