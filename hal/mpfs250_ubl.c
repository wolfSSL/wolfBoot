/* mpfs250_ubl.c
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

/* PolarFire SoC boot mode 2 User Boot Loader (UBL) stub.
 *
 * Authenticated by the System Controller out of sNVM and run from the LIM,
 * this verifies the signed wolfBoot manifest in eNVM and enters wolfBoot at
 * its reset vector on every hart, as a boot mode 1 boot would.  It touches
 * no clock, cache way or peripheral the eNVM wolfBoot does not expect to
 * find in its reset state. */

#include <stdint.h>
#include <string.h>

#include "image.h"
#include "loader.h"
#include "hal.h"
#include "printf.h"
#include "hal/mpfs250.h"
#include "hal/mpfs250_ubl.h"
#include "wolfboot/version.h"

#define UBL_MAGIC     (*(volatile uint32_t *)(uintptr_t)MPFS_UBL_MAGIC_ADDR)
#define UBL_ENTRY     (*(volatile uint64_t *)(uintptr_t)MPFS_UBL_ENTRY_ADDR)
#define UBL_ENTRY_INV (*(volatile uint64_t *)(uintptr_t)MPFS_UBL_ENTRY_INV_ADDR)
#define UBL_ACK(h)    (*(volatile uint32_t *)(uintptr_t)(MPFS_UBL_ACK_BASE + \
                          (h) * 4))

/* Wait for the parked harts to acknowledge the published entry. */
#define UBL_ACK_WAIT_US  50000U

static const unsigned long wdt_bases[MPFS_NUM_HARTS] = {
    MSS_WDT_E51_BASE, MSS_WDT_U54_1_BASE, MSS_WDT_U54_2_BASE,
    MSS_WDT_U54_3_BASE, MSS_WDT_U54_4_BASE
};

static uint64_t rdcycle(void)
{
    uint64_t c;
    __asm__ volatile("rdcycle %0" : "=r"(c));
    return c;
}

void hal_init(void)
{
    unsigned int w;

    /* Start every window from full, so the signature check cannot be cut
     * short.  The window itself is a design-time property and already the
     * widest the counter allows; see the watchdog policy in hal/mpfs250.c. */
    for (w = 0; w < MPFS_NUM_HARTS; w++) {
        MSS_WDT_REFRESH(wdt_bases[w]) = 0xDEADC0DEU;
    }

#ifdef DEBUG_UART
    SYSREG_SUBBLK_CLOCK_CR |= (MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    SYSREG_SOFT_RESET_CR &= ~(MSS_PERIPH_MMUART0 << DEBUG_UART_PORT);
    uart_init();
#endif
}

void hal_prepare_boot(void)
{
    /* Hand the eNVM wolfBoot a full window. */
    MSS_WDT_REFRESH(MSS_WDT_E51_BASE) = 0xDEADC0DEU;
}

/* The manifest is verified in place; there is no flash backend. */
void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

#ifdef DEBUG_UART
/* The MMUART sequence of hal/mpfs250.c, at the compile-time reset clock:
 * a fix there belongs here too. */
static void uart_config_baud(unsigned long base, uint32_t baudrate)
{
    const uint64_t pclk = MSS_APB_AHB_CLK;
    uint32_t div_x128 = (uint32_t)((8UL * pclk) / baudrate);
    uint32_t div_x64  = div_x128 / 2u;
    uint32_t div_int  = div_x64 / 64u;
    uint32_t div_frac = div_x64 - (div_int * 64u);
    div_frac += (div_x128 - (div_int * 128u)) - (div_frac * 2u);
    if (div_frac > 63u)
        div_frac = 63u;
    if (div_int > (uint32_t)UINT16_MAX)
        return;
    MMUART_LCR(base) |= DLAB_MASK;
    MMUART_DMR(base) = (uint8_t)(div_int >> 8);
    MMUART_DLR(base) = (uint8_t)div_int;
    MMUART_LCR(base) &= ~DLAB_MASK;
    if (div_int > 1u) {
        MMUART_MM0(base) |= EFBR_MASK;
        MMUART_DFR(base) = (uint8_t)div_frac;
    } else {
        MMUART_MM0(base) &= ~EFBR_MASK;
    }
}

void uart_init(void)
{
    const unsigned long base = DEBUG_UART_BASE;

    MMUART_MM0(base) &= ~ELIN_MASK;
    MMUART_MM1(base) &= ~EIRD_MASK;
    MMUART_MM2(base) &= ~EERR_MASK;
    MMUART_IER(base)  = 0u;
    MMUART_FCR(base)  = CLEAR_RX_FIFO_MASK | CLEAR_TX_FIFO_MASK | RXRDY_TXRDYN_EN_MASK;
    MMUART_MCR(base) &= ~(LOOP_MASK | RLOOP_MASK);
    MMUART_MCR(base) |= RTS_MASK;
    MMUART_MM1(base) &= ~(E_MSB_TX_MASK | E_MSB_RX_MASK);
    MMUART_MM2(base) &= ~(EAFM_MASK | ESWM_MASK);
    MMUART_MM0(base) &= ~(ETTG_MASK | ERTO_MASK | EFBR_MASK);
    MMUART_GFR(base)  = 0u;
    MMUART_TTG(base)  = 0u;
    MMUART_RTO(base)  = 0u;
    uart_config_baud(base, 115200);
    MMUART_LCR(base)  = MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT;
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
            MMUART_THR(DEBUG_UART_BASE) = '\r';
        }
        while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_THRE) == 0);
        MMUART_THR(DEBUG_UART_BASE) = c;
    }
}

static void uart_drain(void)
{
    while ((MMUART_LSR(DEBUG_UART_BASE) & MSS_UART_TEMT) == 0);
}
#endif /* DEBUG_UART */

/* Hand the verified entry to the parked harts, then take it last. */
static void __attribute__((noreturn)) ubl_enter(uintptr_t entry)
{
    uint64_t start;
    unsigned int h, acked;

    hal_prepare_boot();
#ifdef DEBUG_UART
    uart_drain();
#endif

    for (h = 1; h < MPFS_NUM_HARTS; h++) {
        UBL_ACK(h) = 0;
    }
    UBL_ENTRY = entry;
    UBL_ENTRY_INV = ~(uint64_t)entry;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    UBL_MAGIC = MPFS_UBL_MAGIC;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    start = rdcycle();
    do {
        acked = 0;
        for (h = 1; h < MPFS_NUM_HARTS; h++) {
            acked += (UBL_ACK(h) != 0);
        }
    } while ((acked < MPFS_NUM_HARTS - 1) &&
             ((rdcycle() - start) <
              (uint64_t)UBL_ACK_WAIT_US * MPFS_CPU_FREQ_RESET_MHZ));
#ifdef DEBUG_UART
    wolfBoot_printf("UBL: %u of %u harts released\n", acked + 1,
                    (unsigned int)MPFS_NUM_HARTS);
    uart_drain();
#endif

    __asm__ volatile("fence.i" ::: "memory");
    ((void (*)(void))entry)();
    while (1) {
    }
}

void main(void)
{
    struct wolfBoot_image img;
    uint8_t *manifest = (uint8_t *)(uintptr_t)MPFS_ENVM_MANIFEST_ADDR;
    uint8_t *image = (uint8_t *)(uintptr_t)MPFS_ENVM_IMAGE_ADDR;
#ifdef DEBUG_UART
    uint64_t start = rdcycle();
#endif
    int ret;

    /* Invalidate whatever the mailbox held before the U54s can act on it */
    UBL_MAGIC = 0;
    UBL_ENTRY = 0;
    UBL_ENTRY_INV = 0;
    __asm__ volatile("fence iorw, iorw" ::: "memory");

    hal_init();

    wolfBoot_printf("wolfBoot UBL %s: eNVM image 0x%08lx, manifest 0x%08lx\n",
                    LIBWOLFBOOT_VERSION_STRING,
                    (unsigned long)(uintptr_t)image,
                    (unsigned long)(uintptr_t)manifest);

    /* The manifest sits in the last eNVM page, the image it signs from the
     * reset vector up to it. */
    ret = wolfBoot_open_self_address(&img, manifest, image);
    if (ret != 0) {
        wolfBoot_printf("UBL: no manifest at 0x%08lx\n",
                        (unsigned long)(uintptr_t)manifest);
    }
    else if (img.fw_size > (uint32_t)(manifest - image)) {
        wolfBoot_printf("UBL: manifest size %u exceeds eNVM\n",
                        (unsigned int)img.fw_size);
        ret = -1;
    }
    if (ret == 0) {
        ret = wolfBoot_verify_integrity(&img);
        if (ret != 0) {
            wolfBoot_printf("UBL: eNVM wolfBoot digest mismatch\n");
        }
    }
    if (ret == 0) {
        ret = wolfBoot_verify_authenticity(&img);
        if (ret != 0) {
            wolfBoot_printf("UBL: eNVM wolfBoot signature invalid\n");
        }
    }
#ifdef MPFS_UBL_MIN_VERSION
    if ((ret == 0) &&
        (wolfBoot_get_blob_version(manifest) < MPFS_UBL_MIN_VERSION)) {
        wolfBoot_printf("UBL: eNVM wolfBoot version 0x%x below floor 0x%x\n",
                        (unsigned int)wolfBoot_get_blob_version(manifest),
                        (unsigned int)MPFS_UBL_MIN_VERSION);
        ret = -1;
    }
#endif
    if ((ret != 0) || (img.signature_ok != 1) || (img.sha_ok != 1)) {
        wolfBoot_printf("UBL: eNVM wolfBoot verify FAILED (%d)\n", ret);
        wolfBoot_panic();
    }

    wolfBoot_printf("UBL: eNVM wolfBoot signature OK (%u ms), entering 0x%08lx\n",
                    (unsigned int)((rdcycle() - start) /
                                   (MPFS_CPU_FREQ_RESET_MHZ * 1000U)),
                    (unsigned long)(uintptr_t)img.fw_base);
    ubl_enter((uintptr_t)img.fw_base);
}
