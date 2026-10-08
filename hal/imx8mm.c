/* imx8mm.c
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
#include "hal/imx8mm.h"
#include "aarch64_arch.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "hal.h"
#include "sdhci.h"
#include "disk.h"
#endif
#ifndef ARCH_AARCH64
#   error "wolfBoot imx8mm HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

/* Fixed addresses */
extern void *kernel_addr, *update_addr, *dts_addr;

void* hal_get_primary_address(void)
{
#ifdef IMX8MM_KERNEL_ADDR
    /* Signed kernel placed by the previous stage */
    return (void*)IMX8MM_KERNEL_ADDR;
#else
    return (void*)&kernel_addr;
#endif
}

void* hal_get_update_address(void)
{
  return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
  return (void*)&dts_addr;
}

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    memcpy(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    memset((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    memcpy((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}

#endif


#ifdef DEBUG_UART
/* UART TX only; clocks, pins and baud rate are set by the earlier stages */
#define IMX_UART_REG(off)   (*(volatile uint32_t *)(IMX8MM_UART_BASE + (off)))
#define IMX_UART_UTXD       IMX_UART_REG(IMX8MM_UART_UTXD)
#define IMX_UART_UCR1       IMX_UART_REG(IMX8MM_UART_UCR1)
#define IMX_UART_USR2       IMX_UART_REG(IMX8MM_UART_USR2)
#define IMX_UART_UTS        IMX_UART_REG(IMX8MM_UART_UTS)

void uart_init(void)
{
    IMX_UART_UCR1 |= UCR1_UARTEN;
}

static void uart_putc(char c)
{
    while (IMX_UART_UTS & UTS_TXFULL)
        ;
    IMX_UART_UTXD = (uint32_t)(uint8_t)c;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n')
            uart_putc('\r');
        uart_putc(buf[i]);
    }
    /* Drain the FIFO before handing over */
    while (!(IMX_UART_USR2 & USR2_TXDC))
        ;
}
#endif /* DEBUG_UART */

void* hal_get_dts_update_address(void)
{
  return NULL; /* Not yet supported */
}

/* public HAL functions */
#ifdef IMX8MM_BL33
/* Enable the I-cache if the MMU is off (BL33 entry). do_boot() disables it. */
static void imx8mm_icache_enable(void)
{
    uint64_t sctlr;

    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    if ((sctlr & (1UL << 0)) == 0) {            /* SCTLR_EL2.M: MMU off */
        __asm__ volatile("ic iallu\n"
                         "dsb sy\n"
                         "isb\n" ::: "memory");
        sctlr |= (1UL << 12);                   /* SCTLR_EL2.I */
        __asm__ volatile("msr sctlr_el2, %0\n"
                         "isb\n" :: "r"(sctlr) : "memory");
    }
}

#ifdef IMX8MM_MMU
/* Identity MMU, 1 GB blocks: DRAM Normal cacheable, the rest Device (XN).
 * do_boot() turns it off again before Linux. As hal/imx8qm.c. */
#define MMU_BLOCK_NORMAL  0x0000000000000701ULL
#define MMU_BLOCK_DEVICE  (0x0000000000000405ULL | (1ULL << 54) | (1ULL << 53))

static volatile uint64_t imx8mm_l1_table[512] __attribute__((aligned(4096)));

static void imx8mm_mmu_enable(void)
{
    uint64_t sctlr, el;
    int i;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    if (((el >> 2) & 0x3) != 2 || (sctlr & (1UL << 0)) != 0) {
        imx8mm_icache_enable();
        return;
    }

    for (i = 0; i < 4; i++) {
        uint64_t base = (uint64_t)i << 30;
        imx8mm_l1_table[i] = base |
            ((base >= IMX8MM_DRAM_BASE && base < IMX8MM_DRAM_END) ?
                MMU_BLOCK_NORMAL : MMU_BLOCK_DEVICE);
    }

    /* Attr0: Normal WB, Attr1: Device */
    __asm__ volatile("msr mair_el2, %0" :: "r"(0x00000000000000FFUL));
    __asm__ volatile("msr ttbr0_el2, %0"
        :: "r"((uint64_t)(uintptr_t)imx8mm_l1_table));
    /* T0SZ=32, 4 KB granule, cacheable walks; bits 31 and 23 are RES1 */
    __asm__ volatile("msr tcr_el2, %0"
        :: "r"(0x0000000000013520UL | (1UL << 31) | (1UL << 23)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");

    /* Drop stale lines from earlier stages */
    aarch64_dcache_maint(0);
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);   /* M | C | I */
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

#if defined(DISK_EMMC) && !defined(SDHCI_SDMA_DISABLED)
/* Cache maintenance by address range for SDMA */
static void imx8mm_dcache_range(uintptr_t start, uint32_t sz, int invalidate)
{
    uintptr_t line, end;
    uint64_t ctr;

    if (sz == 0)
        return;
    /* CTR_EL0.DminLine: log2 of the line size in words */
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line = (uintptr_t)4 << ((ctr >> 16) & 0xF);

    if (((start | sz) & (line - 1)) != 0)
        wolfBoot_printf("imx8mm: DMA buffer %p+%u not cache-line aligned\n",
            (void*)start, (unsigned)sz);

    end = (start + sz + line - 1) & ~(line - 1);
    start &= ~(line - 1);
    __asm__ volatile("dsb sy");
    for (; start < end; start += line) {
        if (invalidate)
            __asm__ volatile("dc ivac, %0" :: "r"(start) : "memory");
        else
            __asm__ volatile("dc civac, %0" :: "r"(start) : "memory");
    }
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

void sdhci_platform_dma_prepare(void *buf, uint32_t sz, int is_write)
{
    /* Clean in both directions: a dirty line could evict onto DMA data */
    imx8mm_dcache_range((uintptr_t)buf, sz, 0);
    (void)is_write;
}

void sdhci_platform_dma_complete(void *buf, uint32_t sz, int is_write)
{
    if (!is_write)
        imx8mm_dcache_range((uintptr_t)buf, sz, 1);
}
#endif /* DISK_EMMC && !SDHCI_SDMA_DISABLED */
#endif /* IMX8MM_MMU */
#endif /* IMX8MM_BL33 */

#if defined(IMX8MM_BL33) && defined(DEBUG) && defined(DEBUG_UART)
/* Image bounds (hal/imx8mm.ld) */
extern uint8_t _start_text[];
extern uint8_t _end[];

/* Called by the BL33 EL2 exception vectors (src/boot_aarch64_start.S) */
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector);
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector)
{
    const char* from;
    uintptr_t pc;

    from = (vector >= 8) ? "lower EL (payload)" : "current EL (wolfBoot)";
    pc = (uintptr_t)elr;

    wolfBoot_printf("\n*** i.MX8MM EL2 EXCEPTION ***\n");
    wolfBoot_printf("vector=%d from %s\n", (int)vector, from);
    wolfBoot_printf("ESR=0x%08x EC=0x%02x ISS=0x%06x\n",
        (uint32_t)esr, (uint32_t)((esr >> 26) & 0x3F),
        (uint32_t)(esr & 0x1FFFFFUL));
    wolfBoot_printf("ELR=0x%08x%08x\n",
        (uint32_t)(elr >> 32), (uint32_t)(elr & 0xFFFFFFFFUL));
    wolfBoot_printf("FAR=0x%08x%08x\n",
        (uint32_t)(far >> 32), (uint32_t)(far & 0xFFFFFFFFUL));
    if (pc >= (uintptr_t)_start_text && pc < (uintptr_t)_end) {
        wolfBoot_printf("ELR is inside wolfBoot (+0x%x from 0x%08x)\n",
            (uint32_t)(pc - (uintptr_t)_start_text),
            (uint32_t)(uintptr_t)_start_text);
    }
    else {
        wolfBoot_printf("ELR is outside wolfBoot [0x%08x-0x%08x)\n",
            (uint32_t)(uintptr_t)_start_text, (uint32_t)(uintptr_t)_end);
    }
}
#endif /* IMX8MM_BL33 && DEBUG && DEBUG_UART */


#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* eMMC (uSDHC3) for src/sdhci.c, BL33 mode only */
#ifdef DISK_SDCARD
#error "imx8mm: only DISK_EMMC (uSDHC3) is supported"
#endif

#define IMX8MM_USDHC_BASE       IMX8MM_USDHC3_BASE

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

uint64_t hal_get_timer_us(void)
{
    return (timer_get_count() * 1000000ULL) / timer_get_freq();
}

void hal_delay_us(uint32_t us)
{
    uint64_t deadline = timer_deadline_us(us);
    while (!timer_expired(deadline))
        ;
}

/* uSDHC3 pins (PICO-IMX8MM eMMC), mux mode 2, as in U-Boot SPL */
static const struct {
    uint16_t mux;   /* IOMUXC SW_MUX_CTL_PAD offset */
    uint16_t pad;   /* IOMUXC SW_PAD_CTL_PAD offset */
    uint32_t mode;
} usdhc3_pads[] = {
    { 0x138, 0x3A0, 2 | IOMUXC_MUX_SION },  /* NAND_WE_B   -> USDHC3_CLK   */
    { 0x13C, 0x3A4, 2 },                    /* NAND_WP_B   -> USDHC3_CMD   */
    { 0x11C, 0x384, 2 },                    /* NAND_DATA04 -> USDHC3_DATA0 */
    { 0x120, 0x388, 2 },                    /* NAND_DATA05 -> USDHC3_DATA1 */
    { 0x124, 0x38C, 2 },                    /* NAND_DATA06 -> USDHC3_DATA2 */
    { 0x128, 0x390, 2 },                    /* NAND_DATA07 -> USDHC3_DATA3 */
    { 0x130, 0x398, 2 },                    /* NAND_RE_B   -> USDHC3_DATA4 */
    { 0x100, 0x368, 2 },                    /* NAND_CE2_B  -> USDHC3_DATA5 */
    { 0x104, 0x36C, 2 },                    /* NAND_CE3_B  -> USDHC3_DATA6 */
    { 0x108, 0x370, 2 },                    /* NAND_CLE    -> USDHC3_DATA7 */
};

/* Clock root SYS_PLL1_400M, gate on, pins (SPL skips this on USB boot) */
static void imx8mm_usdhc3_setup(void)
{
    unsigned int i;

    wr32(CCM_CCGR_CLR(CCM_CCGR_USDHC3), CCM_CCGR_CLK_ON);
    wr32(CCM_TARGET_ROOT(CCM_ROOT_USDHC3),
        CCM_TARGET_ROOT_ENABLE | CCM_TARGET_ROOT_MUX(1));
    wr32(CCM_CCGR_SET(CCM_CCGR_USDHC3), CCM_CCGR_CLK_ON);

    for (i = 0; i < sizeof(usdhc3_pads) / sizeof(usdhc3_pads[0]); i++) {
        wr32(IMX8MM_IOMUXC_BASE + usdhc3_pads[i].mux, usdhc3_pads[i].mode);
        wr32(IMX8MM_IOMUXC_BASE + usdhc3_pads[i].pad, IOMUXC_PAD_USDHC);
    }
}

/* uSDHC -> SDHCI register shim, taken from hal/imx8qm.c */

#ifndef CADENCE_SRS_OFFSET
#define CADENCE_SRS_OFFSET  0x200
#endif

#ifndef USDHC_RESET_TIMEOUT_US
#define USDHC_RESET_TIMEOUT_US   1000000
#endif
#ifndef USDHC_INIT_TIMEOUT_US
#define USDHC_INIT_TIMEOUT_US     100000
#endif
#ifndef USDHC_CLK_STABLE_TIMEOUT_US
#define USDHC_CLK_STABLE_TIMEOUT_US 100000
#endif

/* Standard SDHCI offsets (after the Cadence SRS base) */
#define STD_BLK             0x04
#define STD_CMD             0x0C
#define STD_PRES_STATE      0x24
#define STD_HOST_CTRL1      0x28
#define STD_CLOCK_CTRL      0x2C
#define STD_INT_STATUS      0x30
#define STD_INT_STATUS_EN   0x34
#define STD_INT_SIGNAL_EN   0x38
#define STD_HOST_CTRL2      0x3C
#define STD_CAPS1           0x40
#define STD_CAPS2           0x44
#define STD_MAX_CURRENT     0x48
#define STD_ADMA_ADDR_LO    0x58
#define STD_ADMA_ADDR_HI    0x5C

/* uSDHC-only error bits */
#define USDHC_INT_DMAE      (1U << 28)
#define USDHC_INT_TNE       (1U << 26)

/* Registers uSDHC does not implement */
static uint32_t srs10_shadow;   /* bus power / bus voltage / high speed */
static uint32_t srs15_shadow;   /* host control 2 */
/* Rate set by sdhci_platform_set_clock() */
static uint32_t usdhc_achieved_clk_khz = IMX8MM_USDHC_PERCLK_HZ / 1000;

uint32_t sdhci_reg_read(uint32_t offset)
{
    uintptr_t b = IMX8MM_USDHC_BASE;
    uint32_t std, v, raw;

    if (offset < CADENCE_SRS_OFFSET) {
        /* Cadence HRS: none on uSDHC; report the PHY handshake as done */
        return (offset == SDHCI_HRS04) ? SDHCI_HRS04_UIS_ACK : 0;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_PRES_STATE:
            v = rd32(b + USDHC_PRES_STATE);
            v |= SDHCI_SRS09_CSS;
            /* DAT0 level: bit 24 -> bit 20 */
            if ((v & USDHC_PRES_DLSL_DAT0) != 0)
                v |= SDHCI_SRS09_DAT0_LVL;
            else
                v &= ~SDHCI_SRS09_DAT0_LVL;
            return v;

        case STD_HOST_CTRL1:
            v = srs10_shadow & ~(uint32_t)(SDHCI_SRS10_DTW | SDHCI_SRS10_EDTW);
            switch (rd32(b + USDHC_PROT_CTRL) & USDHC_PROT_DTW_MASK) {
                case USDHC_PROT_DTW_4BIT: v |= SDHCI_SRS10_DTW;  break;
                case USDHC_PROT_DTW_8BIT: v |= SDHCI_SRS10_EDTW; break;
                default: break;
            }
            return v;

        case STD_CLOCK_CTRL:
            /* No clock enable/stable bits on uSDHC */
            v = rd32(b + USDHC_SYS_CTRL) &
                (uint32_t)(USDHC_SYS_DTOCV_MASK | USDHC_SYS_RSTA |
                           USDHC_SYS_RSTC | USDHC_SYS_RSTD);
            return v | SDHCI_SRS11_ICE | SDHCI_SRS11_ICS | SDHCI_SRS11_SDCE;

        case STD_INT_STATUS:
        case STD_INT_STATUS_EN:
        case STD_INT_SIGNAL_EN:
            raw = rd32(b + std);
            v = raw;
            if ((raw & USDHC_INT_DMAE) != 0)
                v |= SDHCI_SRS12_EADMA;
            /* Hide uSDHC-only bits; synthesize the error summary */
            v &= ~(uint32_t)(USDHC_INT_DMAE | USDHC_INT_TNE);
            if (std == STD_INT_STATUS &&
                    (((v & SDHCI_SRS12_ERR_STAT) != 0) ||
                     ((raw & USDHC_INT_TNE) != 0))) {
                v |= SDHCI_SRS12_EINT;
            }
            return v;

        case STD_HOST_CTRL2:
            return srs15_shadow;

        case STD_CAPS1:
            /* Synthesize the timeout clock (50 MHz) and base clock */
            v = rd32(b + USDHC_HOST_CTRL_CAP) &
                (uint32_t)(USDHC_CAP_VS33 | USDHC_CAP_VS30 | USDHC_CAP_VS18);
            v |= (50U << SDHCI_SRS16_TCF_SHIFT) & SDHCI_SRS16_TCF_MASK;
            v |= SDHCI_SRS16_TCU; /* timeout clock is in MHz */
            v |= ((IMX8MM_USDHC_PERCLK_HZ / 1000000U)
                    << SDHCI_SRS16_BCSDCLK_SHIFT) &
                 SDHCI_SRS16_BCSDCLK_MASK;
            return v;

        case STD_CAPS2:
            return 0;

        case STD_ADMA_ADDR_LO:
            /* SDMA address (DS_ADDR) */
            return rd32(b + USDHC_DS_ADDR);

        case STD_ADMA_ADDR_HI:
            return 0; /* 32-bit ADMA only */

        case STD_MAX_CURRENT:
            /* MIX_CTRL on uSDHC; no max-current register */
            return 0;

        default:
            return rd32(b + std);
    }
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uintptr_t b = IMX8MM_USDHC_BASE;
    uint32_t std, v, mix;

    if (offset < CADENCE_SRS_OFFSET) {
        /* Cadence software reset -> uSDHC reset-all */
        if (offset == SDHCI_HRS00 && (val & SDHCI_HRS00_SWR) != 0)
            wr32(b + USDHC_SYS_CTRL,
                rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
        return;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_BLK:
            /* Drop the SDMA boundary bits */
            wr32(b + USDHC_BLK_ATT,
                (val & 0xFFFF0000U) | (val & 0x0FFFU));
            return;

        case STD_CMD:
            /* MIX_CTRL first: writing CMD_XFR_TYP starts the command */
            mix = rd32(b + USDHC_MIX_CTRL) & ~(uint32_t)USDHC_MIX_CTRL_XFER_MASK;
            mix |= val & USDHC_MIX_CTRL_XFER_MASK;
            wr32(b + USDHC_MIX_CTRL, mix);

            v = val & 0xFFFF0000U;
            /* Data commands are R1, not R1b */
            if ((v & USDHC_XFR_DPSEL) != 0 &&
                    (v & USDHC_XFR_RSPTYP_MASK) == USDHC_XFR_RSPTYP_48B) {
                v = (v & ~(uint32_t)USDHC_XFR_RSPTYP_MASK) |
                    USDHC_XFR_RSPTYP_48;
            }
            wr32(b + USDHC_CMD_XFR_TYP, v);
            return;

        case STD_HOST_CTRL1:
            srs10_shadow = val;
            v = rd32(b + USDHC_PROT_CTRL) &
                ~(uint32_t)(USDHC_PROT_DTW_MASK | USDHC_PROT_DMASEL_MASK);
            if ((val & SDHCI_SRS10_EDTW) != 0)
                v |= USDHC_PROT_DTW_8BIT;
            else if ((val & SDHCI_SRS10_DTW) != 0)
                v |= USDHC_PROT_DTW_4BIT;
            else
                v |= USDHC_PROT_DTW_1BIT;
            /* SDMA only */
            v |= USDHC_PROT_DMASEL_SIMPLE;
            wr32(b + USDHC_PROT_CTRL, v);
            return;

        case STD_CLOCK_CTRL:
            /* Divider is set by sdhci_platform_set_clock() */
            v = rd32(b + USDHC_SYS_CTRL) & ~(uint32_t)USDHC_SYS_DTOCV_MASK;
            v |= val & USDHC_SYS_DTOCV_MASK;
            v |= val & (uint32_t)(USDHC_SYS_RSTA | USDHC_SYS_RSTC |
                                  USDHC_SYS_RSTD);
            wr32(b + USDHC_SYS_CTRL, v);
            return;

        case STD_INT_STATUS:
            /* W1C; also clear the uSDHC-only error bits */
            v = val;
            if ((val & SDHCI_SRS12_EADMA) != 0)
                v |= USDHC_INT_DMAE;
            if ((val & (SDHCI_SRS12_ERR_STAT | SDHCI_SRS12_EINT)) != 0)
                v |= USDHC_INT_TNE;
            wr32(b + USDHC_INT_STATUS, v & ~(uint32_t)SDHCI_SRS12_EINT);
            return;

        case STD_INT_STATUS_EN:
        case STD_INT_SIGNAL_EN:
            /* Also enable the uSDHC-only error bits */
            v = val & ~(uint32_t)SDHCI_SRS12_EINT;
            if ((val & SDHCI_SRS12_ERR_STAT) != 0)
                v |= USDHC_INT_DMAE | USDHC_INT_TNE;
            wr32(b + std, v);
            return;

        case STD_HOST_CTRL2:
            srs15_shadow = val;
            /* 1.8V signaling is in VEND_SPEC */
            v = rd32(b + USDHC_VEND_SPEC);
            if ((val & SDHCI_SRS15_V18SE) != 0)
                v |= USDHC_VEND_SPEC_VSELECT;
            else
                v &= ~(uint32_t)USDHC_VEND_SPEC_VSELECT;
            wr32(b + USDHC_VEND_SPEC, v);
            return;

        case STD_CAPS1:
        case STD_CAPS2:
            return; /* read-only */

        case STD_ADMA_ADDR_LO:
            wr32(b + USDHC_DS_ADDR, val);
            return;

        case STD_ADMA_ADDR_HI:
            return; /* 32-bit ADMA only */

        default:
            wr32(b + std, val);
            return;
    }
}

void sdhci_platform_init(void)
{
    uintptr_t b = IMX8MM_USDHC_BASE;
    uint64_t deadline;
    uint32_t v;

    imx8mm_usdhc3_setup();

    /* Reset all */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
    deadline = timer_deadline_us(USDHC_RESET_TIMEOUT_US);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_RSTA) != 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("imx8mm usdhc: reset-all did not clear\n");
            break;
        }
    }

    /* Little-endian data port, no DAT3 card detect */
    v = rd32(b + USDHC_PROT_CTRL);
    v = (v & ~(uint32_t)USDHC_PROT_EMODE_MASK) | USDHC_PROT_EMODE_LE;
    v &= ~(uint32_t)USDHC_PROT_D3CD;
    wr32(b + USDHC_PROT_CTRL, v);

    /* One 512-byte block per watermark event */
    wr32(b + USDHC_WTMK_LVL,
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_RD_SHIFT) |
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_WR_SHIFT));

    /* 80 init clocks before CMD0 */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_INITA);
    deadline = timer_deadline_us(USDHC_INIT_TIMEOUT_US);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_INITA) != 0) {
        if (timer_expired(deadline))
            break;
    }
}

/* SD clock = PERCLK / (prescaler * divisor). Returns the rate in kHz. */
uint32_t sdhci_platform_set_clock(uint32_t clock_khz, uint32_t base_clk_khz)
{
    uintptr_t b = IMX8MM_USDHC_BASE;
    uint32_t target_hz, pre, dvs, v;
    uint64_t deadline;

    (void)base_clk_khz;

    if (clock_khz == 0)
        return 0;

#ifdef IMX8MM_USDHC_MAX_CLK_KHZ
    if (clock_khz > IMX8MM_USDHC_MAX_CLK_KHZ)
        clock_khz = IMX8MM_USDHC_MAX_CLK_KHZ;
#endif

    target_hz = clock_khz * 1000U;

    /* Prescaler 1..256 (power of 2), divisor 1..16 */
    for (pre = 1; pre <= 256; pre <<= 1) {
        for (dvs = 1; dvs <= 16; dvs++) {
            if ((IMX8MM_USDHC_PERCLK_HZ / (pre * dvs)) <= target_hz)
                goto found;
        }
    }
    pre = 256;
    dvs = 16;
found:
    v = rd32(b + USDHC_SYS_CTRL) &
        ~(uint32_t)(USDHC_SYS_DVS_MASK | USDHC_SYS_SDCLKFS_MASK);
    /* SDCLKFS = prescaler / 2 */
    v |= ((pre >> 1) << USDHC_SYS_SDCLKFS_SHIFT) & USDHC_SYS_SDCLKFS_MASK;
    v |= ((dvs - 1) << USDHC_SYS_DVS_SHIFT) & USDHC_SYS_DVS_MASK;
    wr32(b + USDHC_SYS_CTRL, v);

    deadline = timer_deadline_us(USDHC_CLK_STABLE_TIMEOUT_US);
    while ((rd32(b + USDHC_PRES_STATE) & USDHC_PRES_SDSTB) == 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("imx8mm usdhc: SD clock never stabilized\n");
            return 0;
        }
    }

    usdhc_achieved_clk_khz = (IMX8MM_USDHC_PERCLK_HZ / (pre * dvs)) / 1000U;
    return usdhc_achieved_clk_khz;
}

void sdhci_platform_irq_init(void)
{
    /* Polled mode */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc;
}

#ifdef IMX8MM_EMMC_PROBE
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Print the eMMC bus, the MBR partitions and the type of partition 1 */
static void imx8mm_emmc_probe(void)
{
    static uint8_t sec[512] XALIGNED(4);
    uint64_t t0, t1;
    uint32_t lba1 = 0;
    char name[9];
    int i, ret;

    t0 = hal_get_timer_us();
    ret = disk_init(0);
    t1 = hal_get_timer_us();
    wolfBoot_printf("eMMC: init %s (%d), %d ms\n", (ret == 0) ? "OK" : "FAILED",
        ret, (int)((t1 - t0) / 1000));
    if (ret != 0)
        return;
    switch (rd32(IMX8MM_USDHC_BASE + USDHC_PROT_CTRL) & USDHC_PROT_DTW_MASK) {
        case USDHC_PROT_DTW_8BIT: i = 8; break;
        case USDHC_PROT_DTW_4BIT: i = 4; break;
        default:                  i = 1; break;
    }
    wolfBoot_printf("eMMC: %d-bit, %u kHz\n", i, usdhc_achieved_clk_khz);

    if (disk_read(0, 0, sizeof(sec), sec) < 0) {
        wolfBoot_printf("eMMC: MBR read failed\n");
        return;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        wolfBoot_printf("eMMC: no MBR signature (0x%x 0x%x)\n",
            sec[510], sec[511]);
        return;
    }
    wolfBoot_printf("eMMC: MBR partitions\n");
    for (i = 0; i < 4; i++) {
        const uint8_t *e = sec + 446 + (16 * i);
        uint32_t start = le32(e + 8);
        uint32_t num = le32(e + 12);
        if (e[4] == 0)
            continue;
        if (i == 0)
            lba1 = start;
        wolfBoot_printf("  %d: type 0x%x start %u size %u sectors (%u MB)\n",
            i + 1, e[4], start, num, num / 2048);
    }

    if (lba1 != 0 &&
            disk_read(0, (uint64_t)lba1 * 512, sizeof(sec), sec) == 0) {
        wolfBoot_printf("eMMC: partition 1 boot sector: sig 0x%x%x",
            sec[510], sec[511]);
        memcpy(name, sec + 0x03, 8);    /* BS_OEMName */
        name[8] = '\0';
        wolfBoot_printf(", OEM \"%s\"", name);
        memcpy(name, sec + 0x36, 8);    /* FAT12/FAT16 BS_FilSysType */
        if (memcmp(name, "FAT", 3) != 0)
            memcpy(name, sec + 0x52, 8);/* FAT32 BS_FilSysType */
        if (memcmp(name, "FAT", 3) != 0)
            memcpy(name, "unknown ", 8);
        wolfBoot_printf(", type \"%s\"\n", name);
    }
}
#endif /* IMX8MM_EMMC_PROBE */

#endif /* DISK_SDCARD || DISK_EMMC */

void hal_init(void)
{
#if defined(IMX8MM_BL33) && defined(IMX8MM_MMU)
    imx8mm_mmu_enable();
#elif defined(IMX8MM_BL33)
    imx8mm_icache_enable();
#endif
#ifdef DEBUG_UART
    uart_init();
#endif
#if defined(DISK_EMMC) && defined(IMX8MM_EMMC_PROBE)
    imx8mm_emmc_probe();
#endif
    #if defined(TEST_ENCRYPT) && defined (EXT_ENCRYPTED)
    char enc_key[] = "0123456789abcdef0123456789abcdef"
        "0123456789abcdef";
    wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
    #endif
}

/* do_boot() turns off the MMU and caches (EL2_HYPERVISOR) */
void hal_prepare_boot(void)
{
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Reset the controller for Linux */
    sdhci_shutdown();
#endif
}


int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
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
    return 0;
}
