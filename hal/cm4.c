/* cm4.c
 *
 * HAL for the Raspberry Pi Compute Module 4 (CM4): Broadcom BCM2711,
 * quad-core Cortex-A72 (ARMv8-A).
 *
 * The VideoCore GPU firmware loads wolfBoot (as kernel8.img) to 0x80000 and
 * releases the A72 cores; wolfBoot verifies the appended signed payload and
 * boots it from RAM. hal_flash_* are no-ops (no in-place flash in this mode).
 * Optional eMMC/SD A/B via the generic SDHCI driver is at the end of the file.
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"
#include "hal/cm4.h"
#ifndef ARCH_AARCH64
#   error "wolfBoot cm4 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

/* Hardware register map (bases, UART, EMMC2/SDHCI) is in hal/cm4.h */

/* Fixed addresses (provided by the linker script) */
extern void *kernel_addr, *update_addr, *dts_addr;

#if defined(DEBUG_UART)
static void uart_tx(char c)
{
    while (*UART0_FR & 0x20) /* TXFF: wait while FIFO full */
        ;
    *UART0_DR = c;
}

void uart_write(const char* buf, uint32_t sz)
{
    while (sz-- > 0 && *buf)
        uart_tx(*buf++);
}

void uart_init(void)
{
    /* The VideoCore firmware has already routed the PL011 to GPIO14/15
     * (dtoverlay=disable-bt) and set init_uart_clock=48MHz. Program the PL011
     * for 115200 8N1 directly, without the VideoCore mailbox (a mailbox poll
     * that never returns post-handoff would hang before any output).
     * 48MHz UARTCLK: BAUDDIV = 48e6/(16*115200) = 26.04 -> IBRD 26, FBRD 3. */
    *UART0_CR = 0;
    *UART0_ICR = 0x7FF;
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6); /* FIFO, 8-bit */
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);   /* enable UART, TX, RX */
}
#endif /* DEBUG_UART */

void* hal_get_primary_address(void)
{
    return (void*)&kernel_addr;
}

void* hal_get_update_address(void)
{
    return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
    return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
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

void hal_init(void)
{
#if defined(DEBUG_UART)
    unsigned long el;
    uart_init();
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    wolfBoot_printf("wolfBoot CM4 (BCM2711 Cortex-A72) hal_init, EL%d\n",
        (int)((el >> 2) & 0x3));
#endif
}

void hal_prepare_boot(void)
{
}

#if defined(HAVE_FIPS)
/* FIPS DRBG entropy seed from the BCM2711 RNG200 hardware TRNG. Registered via
 * CUSTOM_RAND_GENERATE_SEED in include/user_settings.h. The RNG200 has NIST
 * SP800-90B startup/continuous health tests in hardware. */
int wolfBoot_fips_seed(unsigned char* output, unsigned int sz)
{
    static int rng_inited = 0;
    unsigned int pos = 0;
    unsigned int guard;

    if (!rng_inited) {
        /* iproc-rng200 bring-up: disable RBG, soft-reset the RBG then RNG
         * cores, clear pending interrupt status, then re-enable the RBG. */
        *RNG_CTRL = 0;
        *RNG_RBG_SOFT_RESET = 1;
        *RNG_RBG_SOFT_RESET = 0;
        *RNG_SOFT_RESET = 1;
        *RNG_SOFT_RESET = 0;
        *RNG_INT_STATUS = 0xFFFFFFFF; /* write-1-to-clear all pending status */
        *RNG_CTRL = RNG200_CTRL_RBGEN;
        rng_inited = 1;
    }

    while (pos < sz) {
        unsigned int word, n, i;
        /* wait for at least one 32-bit word in the FIFO (bounded) */
        guard = 0;
        while ((*RNG_FIFO_COUNT & 0xFF) == 0) {
            if (++guard > 200000000U) {
#if defined(DEBUG_UART)
                wolfBoot_printf("RNG200 FIFO timeout int=0x%08x ctrl=0x%08x\n",
                    (unsigned)*RNG_INT_STATUS, (unsigned)*RNG_CTRL);
#endif
                return -1;
            }
        }
        word = *RNG_FIFO_DATA;
        n = (sz - pos) < 4 ? (sz - pos) : 4;
        for (i = 0; i < n; i++)
            output[pos++] = (unsigned char)(word >> (i * 8));
    }

#if defined(DEBUG_UART)
    {
        static int traced = 0;
        if (!traced) {
            wolfBoot_printf("RNG200 seed %02x %02x %02x %02x %02x %02x %02x %02x\n",
                output[0], output[1], output[2], output[3],
                output[4], output[5], output[6], output[7]);
            traced = 1;
        }
    }
#endif
    return 0;
}
#endif /* HAVE_FIPS */

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* BCM2711 EMMC2 platform glue for the generic SDHCI driver (src/sdhci.c).
 * EMMC2 is a standard SDHCI v3.0 Arasan block at 0xFE340000. The driver uses
 * Cadence-style SRS offsets (0x200 + std); translate them to the standard
 * Arasan layout, mirroring the ZynqMP path in hal/zynq.c. NOTE: not yet
 * hardware-validated; clock/caps/card-detect quirks may be required once
 * validated on hardware. */
#include "sdhci.h"

uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;

    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;
        uint32_t val;

        if (std_off == 0x58) /* SRS22 -> legacy SDMA address (SRS00) */
            return *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR));
        if (std_off == 0x5C) /* SRS23: no 64-bit addressing on v3.0 */
            return 0;
        val = *((volatile uint32_t *)(base + std_off));
        if (std_off == 0x40) /* SRS16 Capabilities: mask A64S (no HV4E) */
            val &= ~SDHCI_SRS16_A64S;
        return val;
    }
    return 0; /* HRS region not present on this Arasan block */
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;
    uint32_t std_off;

    if (offset < CADENCE_SRS_OFFSET)
        return; /* HRS region not present */
    std_off = offset - CADENCE_SRS_OFFSET;

    /* SRS10 (0x28): Host Control 1 / Power / Block Gap / Wakeup (8-bit each) */
    if (std_off == 0x28) {
        *((volatile uint8_t *)(base + STD_SDHCI_HOST_CTRL1)) = (uint8_t)val;
        *((volatile uint8_t *)(base + STD_SDHCI_POWER_CTRL))  = (uint8_t)(val >> 8);
        *((volatile uint8_t *)(base + STD_SDHCI_BLKGAP_CTRL)) = (uint8_t)(val >> 16);
        *((volatile uint8_t *)(base + STD_SDHCI_WAKEUP_CTRL)) = (uint8_t)(val >> 24);
        return;
    }
    /* SRS11 (0x2C): Clock Control (16-bit) / Timeout / Software Reset */
    if (std_off == 0x2C) {
        *((volatile uint16_t *)(base + STD_SDHCI_CLK_CTRL))   = (uint16_t)val;
        *((volatile uint8_t *)(base + STD_SDHCI_TIMEOUT_CTRL)) = (uint8_t)(val >> 16);
        *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET))    = (uint8_t)(val >> 24);
        return;
    }
    if (std_off == 0x58) { /* SRS22 -> legacy SDMA address; write restarts DMA */
        *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR)) = val;
        return;
    }
    if (std_off == 0x5C) /* SRS23: no 64-bit addressing on v3.0 */
        return;
    if (std_off == STD_SDHCI_HOST_CTRL2) /* SRS15: mask unsupported HV4E/A64 */
        val &= ~(SDHCI_SRS15_HV4E | SDHCI_SRS15_A64);
    *((volatile uint32_t *)(base + std_off)) = val;
}

void sdhci_platform_init(void)
{
    /* The GPU firmware already brought up the EMMC2 clock/pinmux; just issue a
     * controller soft reset and wait for it to clear. */
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;
    volatile int i;

    *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) = STD_SDHCI_SRA;
    for (i = 0; i < 100000; i++) {
        if ((*((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) & STD_SDHCI_SRA) == 0)
            break;
    }
}

void sdhci_platform_irq_init(void)
{
    /* Polled mode; no IRQ wiring needed for the boot path. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc; /* handled by the generic driver via Host Control 1 */
}

/* Microseconds from the ARMv8 generic timer (SDHCI udelay + disk updater).
 * Falls back to the BCM2711 system counter frequency if CNTFRQ_EL0 is 0. */
uint64_t hal_get_timer_us(void)
{
    uint64_t count, freq;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(count));
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
    if (freq == 0)
        freq = BCM2711_TIMER_CLK_FREQ;
    return (uint64_t)(((__uint128_t)count * 1000000ULL) / freq);
}
#endif /* DISK_SDCARD || DISK_EMMC */
