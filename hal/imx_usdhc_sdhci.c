/* imx_usdhc_sdhci.c
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

/* i.MX uSDHC -> SDHCI shim, shared by the i.MX 8QuadMax and i.MX 8M Mini HALs.
 * src/sdhci.c uses only sdhci_reg_read/write: transfer mode in MIX_CTRL, width
 * in PROT_CTRL, DVS/SDCLKFS divider, no error summary, DMA err 25->28. */

#include <stdint.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "printf.h"
#include "wolfboot/wolfboot.h"

#if defined(TARGET_imx8qm)
#include "hal/imx8qm.h"
#elif defined(TARGET_imx8mm)
#include "hal/imx8mm.h"
#elif defined(TARGET_s32g2)
#include "hal/s32g2.h"
#else
#error "hal/imx_usdhc_sdhci.c: unsupported target"
#endif

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"
#include "aarch64_arch.h"

#if !defined(IMX_USDHC_BASE) || !defined(IMX_USDHC_PERCLK_HZ)
#error "IMX_USDHC_BASE and IMX_USDHC_PERCLK_HZ must be set by the SoC header"
#endif

/* MMIO is Device-nGnRnE (MMU off), so accesses are strongly ordered by the
 * memory type and need no barriers. Kept as functions, not open-coded casts:
 * the uSDHC unit test extracts the shim from this file and substitutes its own. */
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/* Fields the driver reads back that uSDHC does not implement. */
static uint32_t srs10_shadow;   /* bus power / bus voltage / high speed */
static uint32_t srs15_shadow;   /* host control 2 */
/* The rate sdhci_platform_set_clock() achieved, reported as the base so the generic
 * driver's divider is 1. NOT the CAPS1 base field, which stays the source rate. */
static uint32_t usdhc_achieved_clk_khz = IMX_USDHC_PERCLK_HZ / 1000;

uint32_t sdhci_reg_read(uint32_t offset)
{
    uintptr_t b = IMX_USDHC_BASE;
    uint32_t std, v, raw;

    if (offset < CADENCE_SRS_OFFSET) {
        /* Cadence HRS range: uSDHC has no equivalent. Report the PHY
         * handshake as acknowledged so the driver's wait loops still exit. */
        return (offset == SDHCI_HRS04) ? SDHCI_HRS04_UIS_ACK : 0;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_PRES_STATE:
            v = rd32(b + USDHC_PRES_STATE);
            /* Card state stable has no uSDHC bit; the card-inserted bit is
             * already debounced, so report it as always stable. */
            v |= SDHCI_SRS09_CSS;
            /* DAT0 level moves from the DLSL field (bit 24) to bit 20. */
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
            /* No internal clock-enable or stable bit; report both satisfied. */
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
            /* Hide uSDHC-only bits and synthesize the summary the driver polls. TNE has no standard
             * bit, so test the raw value: otherwise a latched tuning error stalls the transfer. */
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
            /* uSDHC zeroes these; synthesize a 50 MHz timeout clock and the
             * real peripheral clock. 64-bit addressing masked off (PIO). */
            v = rd32(b + USDHC_HOST_CTRL_CAP) &
                (uint32_t)(USDHC_CAP_VS33 | USDHC_CAP_VS30 | USDHC_CAP_VS18);
            v |= (50U << SDHCI_SRS16_TCF_SHIFT) & SDHCI_SRS16_TCF_MASK;
            v |= SDHCI_SRS16_TCU; /* timeout clock is in MHz */
            /* Constant source clock in MHz; the achieved rate would truncate to 0 MHz once the
             * 400 kHz identification clock is programmed. */
            v |= ((IMX_USDHC_PERCLK_HZ / 1000000U)
                    << SDHCI_SRS16_BCSDCLK_SHIFT) &
                 SDHCI_SRS16_BCSDCLK_MASK;
            return v;

        case STD_CAPS2:
            return 0;

        case STD_ADMA_ADDR_LO:
            /* SDMA, not ADMA2: PROT_CTRL selects DMASEL=simple, so SRS22 is uSDHC's DS_ADDR (the
             * running address the boundary handler rewrites to resume), not ADMA_SYS_ADDR. */
            return rd32(b + USDHC_DS_ADDR);

        case STD_ADMA_ADDR_HI:
            return 0; /* 32-bit ADMA only */

        case STD_MAX_CURRENT:
            /* uSDHC has no max-current register, and this offset is its live
             * MIX_CTRL. Passing that through would feed transfer-mode bits to
             * the driver's XPC decision as if they were milliamps. Report no
             * capability, as STD_CAPS2 does, which leaves XPC off. */
            return 0;

        default:
            return rd32(b + std);
    }
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uintptr_t b = IMX_USDHC_BASE;
    uint32_t std, v, mix;

    if (offset < CADENCE_SRS_OFFSET) {
        /* The only Cadence host register the driver writes is the software
         * reset; route it to the uSDHC reset-all bit. */
        if (offset == SDHCI_HRS00 && (val & SDHCI_HRS00_SWR) != 0)
            wr32(b + USDHC_SYS_CTRL,
                rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
        return;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_BLK:
            /* Bit 12 is block size here, SDMA boundary in standard SDHCI. */
            wr32(b + USDHC_BLK_ATT,
                (val & 0xFFFF0000U) | (val & 0x0FFFU));
            return;

        case STD_CMD:
            /* MIX_CTRL first: writing CMD_XFR_TYP starts the command. Bits
             * above 5 of MIX_CTRL have no standard equivalent; preserve. */
            mix = rd32(b + USDHC_MIX_CTRL) & ~(uint32_t)USDHC_MIX_CTRL_XFER_MASK;
            mix |= val & USDHC_MIX_CTRL_XFER_MASK;
            wr32(b + USDHC_MIX_CTRL, mix);

            v = val & 0xFFFF0000U;
            /* The Cadence response-check bits alias to "48-bit with busy". SD data commands are
             * R1, not R1b, so a busy check would wait on DAT0 forever; downgrade. */
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
            /* Standard SDHCI selects the DMA engine in bits 3:4; uSDHC uses
             * bits 8:9. Only simple (SDMA) is ever requested here. */
            v |= USDHC_PROT_DMASEL_SIMPLE;
            wr32(b + USDHC_PROT_CTRL, v);
            return;

        case STD_CLOCK_CTRL:
            /* Only the shared fields; the divider is owned by
             * sdhci_platform_set_clock, so drop the standard divisor bits. */
            v = rd32(b + USDHC_SYS_CTRL) & ~(uint32_t)USDHC_SYS_DTOCV_MASK;
            v |= val & USDHC_SYS_DTOCV_MASK;
            v |= val & (uint32_t)(USDHC_SYS_RSTA | USDHC_SYS_RSTC |
                                  USDHC_SYS_RSTD);
            wr32(b + USDHC_SYS_CTRL, v);
            return;

        case STD_INT_STATUS:
            /* Write-1-to-clear. Map the standard ADMA error bit onto the uSDHC one and clear TNE
             * with any error or the summary: the driver never sees TNE, so this is its only exit. */
            v = val;
            if ((val & SDHCI_SRS12_EADMA) != 0)
                v |= USDHC_INT_DMAE;
            if ((val & (SDHCI_SRS12_ERR_STAT | SDHCI_SRS12_EINT)) != 0)
                v |= USDHC_INT_TNE;
            wr32(b + USDHC_INT_STATUS, v & ~(uint32_t)SDHCI_SRS12_EINT);
            return;

        case STD_INT_STATUS_EN:
        case STD_INT_SIGNAL_EN:
            /* On uSDHC a status bit does not latch unless its enable is set, so the uSDHC-only
             * error enables must be added to the mask the generic driver builds. */
            v = val & ~(uint32_t)SDHCI_SRS12_EINT;
            if ((val & SDHCI_SRS12_ERR_STAT) != 0)
                v |= USDHC_INT_DMAE | USDHC_INT_TNE;
            wr32(b + std, v);
            return;

        case STD_HOST_CTRL2:
            srs15_shadow = val;
            /* 1.8V signaling lives in VEND_SPEC on uSDHC. */
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
            wr32(b + USDHC_DS_ADDR, val);   /* see the read side */
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
    uintptr_t b = IMX_USDHC_BASE;
    uint64_t deadline;
    uint32_t v;

    imx_usdhc_board_init();

    /* Reset the controller, then apply the settings that survive it. */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
    deadline = timer_deadline_us(USDHC_RESET_TIMEOUT_US);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_RSTA) != 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("usdhc: reset-all did not clear\n");
            break;
        }
    }

    /* Little-endian data port, card-detect from DAT3 disabled (the boards wire
     * a dedicated card-detect GPIO, and on eMMC there is nothing to detect). */
    v = rd32(b + USDHC_PROT_CTRL);
    v = (v & ~(uint32_t)USDHC_PROT_EMODE_MASK) | USDHC_PROT_EMODE_LE;
    v &= ~(uint32_t)USDHC_PROT_D3CD;
    wr32(b + USDHC_PROT_CTRL, v);

    /* Move a whole 512-byte block per watermark event, so the PIO loop sees one Buffer
     * Read Ready per block rather than one every 32 bytes. */
    wr32(b + USDHC_WTMK_LVL,
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_RD_SHIFT) |
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_WR_SHIFT));

    /* Send the 80 initialization clocks the card needs before CMD0. */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_INITA);
    deadline = timer_deadline_us(USDHC_INIT_TIMEOUT_US);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_INITA) != 0) {
        if (timer_expired(deadline))
            break;
    }
}

/* uSDHC divides by (SDCLKFS prescaler) * (DVS + 1). Reporting the requested
 * rate back as the base clock makes the generic driver's divider settle on 1. */
uint32_t sdhci_platform_set_clock(uint32_t clock_khz, uint32_t base_clk_khz)
{
    uintptr_t b = IMX_USDHC_BASE;
    uint32_t target_hz, pre, dvs, v;
    uint64_t deadline;

    (void)base_clk_khz;

    if (clock_khz == 0)
        return 0;

    /* Optional ceiling: sustained reads fail CRC at the 50 MHz the generic
     * driver steps to. Raising it needs uSDHC delay-line tuning. */
#ifdef IMX_USDHC_MAX_CLK_KHZ
    if (clock_khz > IMX_USDHC_MAX_CLK_KHZ)
        clock_khz = IMX_USDHC_MAX_CLK_KHZ;
#endif

    target_hz = clock_khz * 1000U;

    /* Smallest prescaler/divisor pair not exceeding the requested rate; the prescaler is
     * a power of two from 1 to 256 and the divisor runs 1..16. */
    for (pre = 1; pre <= 256; pre <<= 1) {
        for (dvs = 1; dvs <= 16; dvs++) {
            if ((IMX_USDHC_PERCLK_HZ / (pre * dvs)) <= target_hz)
                goto found;
        }
    }
    pre = 256;
    dvs = 16;
found:
    v = rd32(b + USDHC_SYS_CTRL) &
        ~(uint32_t)(USDHC_SYS_DVS_MASK | USDHC_SYS_SDCLKFS_MASK);
    /* SDCLKFS is a one-hot prescaler code: 0x01 = divide by 2, and each
     * further bit doubles it. A prescaler of 1 is encoded as 0. */
    v |= ((pre >> 1) << USDHC_SYS_SDCLKFS_SHIFT) & USDHC_SYS_SDCLKFS_MASK;
    v |= ((dvs - 1) << USDHC_SYS_DVS_SHIFT) & USDHC_SYS_DVS_MASK;
    wr32(b + USDHC_SYS_CTRL, v);

    /* Wait for the divided clock to settle before any command is issued. */
    deadline = timer_deadline_us(USDHC_CLK_STABLE_TIMEOUT_US);
    while ((rd32(b + USDHC_PRES_STATE) & USDHC_PRES_SDSTB) == 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("usdhc: SD clock never stabilized\n");
            return 0;
        }
    }

    usdhc_achieved_clk_khz = (IMX_USDHC_PERCLK_HZ / (pre * dvs)) / 1000U;
    return usdhc_achieved_clk_khz;
}

void sdhci_platform_irq_init(void)
{
    /* Polled mode: no GIC wiring needed for the boot path. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    /* The bus width is driven through Host Control 1, which the shim
     * translates; there is no separate eMMC/SD mode select on uSDHC. */
    (void)is_emmc;
}

#endif /* DISK_SDCARD || DISK_EMMC */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
void WEAKFUNCTION imx_usdhc_board_init(void)
{
}

uint32_t imx_usdhc_get_clock_khz(void)
{
    return usdhc_achieved_clk_khz;
}

#if (defined(WOLFBOOT_AARCH64_EL2_IDMAP) || \
     defined(WOLFBOOT_AARCH64_DCACHE_RANGE)) && !defined(SDHCI_SDMA_DISABLED)
/* SDMA cache maintenance with the identity MMU on, by address range rather than
 * the set/way walk used at handoff, which would cost more per transfer than the
 * DMA saves. The range routines are in src/boot_aarch64_start.S. */
void sdhci_platform_dma_prepare(void* buf, uint32_t sz, int is_write)
{
    uintptr_t start = (uintptr_t)buf;

    if (sz == 0)
        return;
    /* An unaligned buffer shares lines with other data, which the invalidate
     * after the transfer would discard */
    if (((start | sz) & (IMX_USDHC_DMA_ALIGN - 1)) != 0)
        wolfBoot_printf("usdhc: DMA buffer %p+%u not cache-line aligned\n",
            buf, (unsigned)sz);
    /* Outbound data must reach memory before the controller reads it. Clean on inbound
     * too: a dirty line over the buffer could evict onto data the controller wrote. */
    flush_dcache_range(start, start + sz);
    (void)is_write;
}

void sdhci_platform_dma_complete(void* buf, uint32_t sz, int is_write)
{
    uintptr_t start = (uintptr_t)buf;

    /* Inbound data landed in memory behind the cache's back; drop any stale
     * lines so the CPU reads what the controller wrote. */
    if (!is_write && sz != 0)
        invalidate_dcache_range(start, start + sz);
}
#endif /* (EL2_IDMAP || DCACHE_RANGE) && !SDHCI_SDMA_DISABLED */
#endif /* DISK_SDCARD || DISK_EMMC */
