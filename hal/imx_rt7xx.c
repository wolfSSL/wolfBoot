/* imx_rt7xx — wolfBoot HAL for the NXP i.MX RT700 (MIMXRT798S, Cortex-M33).
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include "image.h"
#include "hal.h"
#include "loader.h"
#include "printf.h"
#include "imx_rt7xx.h"

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_xspi.h"

/* The BootROM configures the compute-domain clocks and the XSPI0 XIP window
 * from the flash config block before handing control to this image. The XSPI
 * controller is re-initialized here with the NXP SDK driver so the IP command
 * path (program/erase/status) gets the full device configuration the ROM only
 * applies to the AHB read engine. Everything on that path executes from RAM:
 * XIP is unavailable while the controller is reconfigured and while the NOR
 * is busy (single die). */

#define RT7XX_REG(a) (*(volatile uint32_t *)(a))

/* Every hardware poll has a deadline so a stalled peripheral or a damaged
 * NOR can fail the operation instead of hanging the boot. Register polls
 * spin for microseconds; the NOR busy poll issues an IP read status
 * transaction per iteration, so its budget covers a worst-case sector
 * erase many times over. */
#define RT7XX_REG_POLL_LIMIT     100000u
#define RT7XX_UART_TX_POLL_LIMIT 100000u
#define RT7XX_NOR_BUSY_LIMIT     1000000u

#ifdef DEBUG_UART

#define CLKCTL0_PSCCTL1_SET  RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x44u)
#define CLKCTL0_PSCCTL5_SET  RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x54u)
#define CLKCTL0_FCCLK0SEL    RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x800u)
#define CLKCTL0_FCCLK0DIV    RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x804u)
#define CLKCTL0_FC0FCLKSEL   RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x808u)
#define RSTCTL0_PRSTCTL0_CLR RT7XX_REG(IMX_RT7XX_RSTCTL0_NS + 0x70u)
#define RSTCTL0_PRSTCTL2_CLR RT7XX_REG(IMX_RT7XX_RSTCTL0_NS + 0x78u)
#define IOPCTL0_PIO0_31      RT7XX_REG(IMX_RT7XX_IOPCTL0_NS + 0x7Cu)
#define IOPCTL0_PIO1_0       RT7XX_REG(IMX_RT7XX_IOPCTL0_NS + 0x80u)
#define LPFC0_PSELID         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0xFF8u)
#define LPUART0_BAUD         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x10u)
#define LPUART0_STAT         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x14u)
#define LPUART0_CTRL         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x18u)
#define LPUART0_DATA         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x1Cu)

#define FCCLK0_DIV_REQFLAG   0x80000000u
#define FCCLK0_DIV_RESET     0x20000000u
#define LPFC0_CLK_RST_BIT    (1u << 30)
#define UART0_STAT_TDRE     0x00800000u
#define UART0_CTRL_TE_RE    0x000C0000u

/* FCCLK0 source is the compute base clock = FRO1 div1 after the BootROM. */
#define UART_FCCLK_HZ        192000000u
#ifndef UART_BAUD
#define UART_BAUD            115200u
#endif

void uart_init(void)
{
    uint32_t sbr;
    uint32_t t;

    /* The BootROM leaves FCCLK0 halted and LP_FLEXCOMM0 gated + in reset */
    CLKCTL0_FCCLK0SEL = 0x4u;
    CLKCTL0_FCCLK0DIV |= FCCLK0_DIV_RESET;
    CLKCTL0_FCCLK0DIV = 0u;
    t = RT7XX_REG_POLL_LIMIT;
    while (((CLKCTL0_FCCLK0DIV & FCCLK0_DIV_REQFLAG) != 0u) && (t > 0u)) {
        t--;
    }
    CLKCTL0_FC0FCLKSEL = 0x4u; /* FLEXCOMM0 functional clock = FCCLK0 */
    CLKCTL0_PSCCTL1_SET = LPFC0_CLK_RST_BIT;
    RSTCTL0_PRSTCTL2_CLR = LPFC0_CLK_RST_BIT;

    /* The BootROM also leaves IOPCTL0 in reset: release it before muxing */
    CLKCTL0_PSCCTL5_SET = (1u << 3);
    RSTCTL0_PRSTCTL0_CLR = (1u << 6);

    /* EVK MCU-LINK console: PIO1_0 = FC0 TX at 33 ohm drive (the VCOM net
     * drops 115200 edges at the default 100 ohm), PIO0_31 = FC0 RX */
    IOPCTL0_PIO1_0 = 0x3001u;
    IOPCTL0_PIO0_31 = 0x41u;

    LPFC0_PSELID = 0x1u; /* LP_FLEXCOMM0 function select: LPUART */

    sbr = UART_FCCLK_HZ / (16u * UART_BAUD);
    LPUART0_CTRL = 0u;
    LPUART0_BAUD = (15u << 24) | (sbr & 0x1FFFu);
    LPUART0_CTRL = UART0_CTRL_TE_RE;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    uint32_t t;

    for (i = 0; i < sz; i++) {
        t = RT7XX_UART_TX_POLL_LIMIT;
        while (((LPUART0_STAT & UART0_STAT_TDRE) == 0u) && (t > 0u)) {
            t--;
        }
        if (t == 0u) {
            return;
        }
        LPUART0_DATA = (uint32_t)(uint8_t)buf[i];
    }
}
#endif /* DEBUG_UART */

/* XSPI0 octal NOR (Macronix MX25UM51345G, OPI DDR) via the NXP SDK driver */

#define NOR_BASE       IMX_RT7XX_XSPI0_NS_BASE
#define NOR_SIZE       IMX_RT7XX_XSPI0_SIZE
#define NOR_SIZE_KB    0x10000u
#define NOR_PAGE       256u
#define NOR_SECTOR     0x1000u

/* Both apertures map the same NOR, bit 28 only carries the security
 * attribute, so the device offset is the low 26 bits either way. */
#define NOR_OFFSET(a)   IMX_RT7XX_XSPI0_OFFSET(a)
#define NOR_APERTURE(a) IMX_RT7XX_XSPI0_APERTURE(a)

#define XSPI_NOR       ((XSPI_Type *)IMX_RT7XX_XSPI0_REGS_NS)
/* The SFP policy words only take Secure writes; wolfBoot runs Secure. */
#define XSPI_NOR_S     ((XSPI_Type *)IMX_RT7XX_XSPI0_REGS_S)

#define LUT_SEQ_IDX_READ            0
#define LUT_SEQ_IDX_READ_STATUS     1
#define LUT_SEQ_IDX_READ_STATUS_OPI 2
#define LUT_SEQ_IDX_WRITE_ENABLE    3
#define LUT_SEQ_IDX_WRITE_ENABLE_OPI 4
#define LUT_SEQ_IDX_READ_SECURITY_OPI 6
#define LUT_SEQ_IDX_PAGEPROGRAM_OCTAL 7
#define LUT_SEQ_IDX_ERASE_SECTOR    8
#define LUT_SEQ_IDX_READ_ID_OPI     10
#define LUT_SEQ_IDX_ENTER_OPI       12
#define CUSTOM_LUT_LENGTH           80

#define NOR_WIP_BIT    0x01u
#define NOR_WEL_BIT    0x02u
/* Security register: the die reports a rejected program or erase here
 * after WIP clears, so WIP alone is not a success indication. */
#define NOR_PFAIL_BIT  0x20u
#define NOR_EFAIL_BIT  0x40u

static xspi_device_ddr_config_t flashDdrConfig = {
    .ddrDataAlignedClk = kXSPI_DDRDataAlignedWith2xInternalRefClk,
    .enableDdr = true,
    .enableByteSwapInOctalMode = false,
};

static xspi_device_config_t deviceConfig = {
    .xspiRootClk = 0u, /* filled from the live clock tree at init */
    .enableCknPad = false,
    .deviceInterface = kXSPI_StrandardExtendedSPI,
    .interfaceSettings.strandardExtendedSPISettings.pageSize = NOR_PAGE,
    .CSHoldTime = 3,
    .CSSetupTime = 3,
    .sampleClkConfig.sampleClkSource = kXSPI_SampleClkFromExternalDQS,
    .sampleClkConfig.enableDQSLatency = false,
    .sampleClkConfig.dllConfig.dllMode = kXSPI_AutoUpdateMode,
    .sampleClkConfig.dllConfig.useRefValue = true,
    .sampleClkConfig.dllConfig.enableCdl8 = true,
    .ptrDeviceDdrConfig = &flashDdrConfig,
    .addrMode = kXSPI_DeviceByteAddressable,
    .columnAddrWidth = 0U,
    .enableCASInterleaving = false,
    .deviceSize[0] = NOR_SIZE_KB,
    .deviceSize[1] = NOR_SIZE_KB,
    .ptrDeviceRegInfo = NULL,
};

/* LUT data sizes of at least 8 bytes on every read: ERR052528 */
static const uint32_t customLUT[CUSTOM_LUT_LENGTH] = {
    [5 * LUT_SEQ_IDX_READ] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0xEE,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0x11),
    [5 * LUT_SEQ_IDX_READ + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_DUMMY_SDR, kXSPI_8PAD, 0x12),
    [5 * LUT_SEQ_IDX_READ + 2] =
        XSPI_LUT_SEQ(kXSPI_Command_DUMMY_SDR, kXSPI_8PAD, 0x2,
                     kXSPI_Command_READ_DDR, kXSPI_8PAD, 0x8),
    [5 * LUT_SEQ_IDX_READ + 3] =
        XSPI_LUT_SEQ(kXSPI_Command_STOP, kXSPI_8PAD, 0x0, 0, 0, 0),

    [5 * LUT_SEQ_IDX_READ_STATUS] =
        XSPI_LUT_SEQ(kXSPI_Command_SDR, kXSPI_1PAD, 0x05,
                     kXSPI_Command_READ_SDR, kXSPI_1PAD, 0x08),

    [5 * LUT_SEQ_IDX_READ_STATUS_OPI] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x05,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0xFA),
    [5 * LUT_SEQ_IDX_READ_STATUS_OPI + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_DUMMY_SDR, kXSPI_8PAD, 0x4),
    [5 * LUT_SEQ_IDX_READ_STATUS_OPI + 2] =
        XSPI_LUT_SEQ(kXSPI_Command_READ_DDR, kXSPI_8PAD, 0x8,
                     kXSPI_Command_STOP, kXSPI_8PAD, 0x0),

    [5 * LUT_SEQ_IDX_READ_SECURITY_OPI] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x2B,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0xD4),
    [5 * LUT_SEQ_IDX_READ_SECURITY_OPI + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_DUMMY_SDR, kXSPI_8PAD, 0x4),
    [5 * LUT_SEQ_IDX_READ_SECURITY_OPI + 2] =
        XSPI_LUT_SEQ(kXSPI_Command_READ_DDR, kXSPI_8PAD, 0x8,
                     kXSPI_Command_STOP, kXSPI_8PAD, 0x0),

    [5 * LUT_SEQ_IDX_WRITE_ENABLE] =
        XSPI_LUT_SEQ(kXSPI_Command_SDR, kXSPI_1PAD, 0x06,
                     kXSPI_Command_STOP, kXSPI_1PAD, 0x04),

    [5 * LUT_SEQ_IDX_WRITE_ENABLE_OPI] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x06,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0xF9),

    [5 * LUT_SEQ_IDX_PAGEPROGRAM_OCTAL] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x12,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0xED),
    [5 * LUT_SEQ_IDX_PAGEPROGRAM_OCTAL + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_WRITE_DDR, kXSPI_8PAD, 0x8),

    [5 * LUT_SEQ_IDX_ERASE_SECTOR] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x21,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0xDE),
    [5 * LUT_SEQ_IDX_ERASE_SECTOR + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_STOP, kXSPI_8PAD, 0x0),

    [5 * LUT_SEQ_IDX_READ_ID_OPI] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x9F,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0x60),
    [5 * LUT_SEQ_IDX_READ_ID_OPI + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_RADDR_DDR, kXSPI_8PAD, 0x20,
                     kXSPI_Command_DUMMY_SDR, kXSPI_8PAD, 0x04),
    [5 * LUT_SEQ_IDX_READ_ID_OPI + 2] =
        XSPI_LUT_SEQ(kXSPI_Command_READ_DDR, kXSPI_8PAD, 0x08,
                     kXSPI_Command_STOP, kXSPI_1PAD, 0x0),

    /* No chip-erase sequence: it is addressless and would erase the boot
     * region straight through the address-based FRAD fence. */
    [5 * LUT_SEQ_IDX_ENTER_OPI] =
        XSPI_LUT_SEQ(kXSPI_Command_SDR, kXSPI_1PAD, 0x72,
                     kXSPI_Command_SDR, kXSPI_1PAD, 0x00),
    [5 * LUT_SEQ_IDX_ENTER_OPI + 1] =
        XSPI_LUT_SEQ(kXSPI_Command_SDR, kXSPI_1PAD, 0x00,
                     kXSPI_Command_SDR, kXSPI_1PAD, 0x00),
    [5 * LUT_SEQ_IDX_ENTER_OPI + 2] =
        XSPI_LUT_SEQ(kXSPI_Command_SDR, kXSPI_1PAD, 0x00,
                     kXSPI_Command_WRITE_SDR, kXSPI_1PAD, 0x01),
};

static int g_xspi_ready;

/* AHB prefetch abort plus data/code cache invalidation after NOR contents
 * change; the caches sit between the core and the XSPI read path. */
static status_t RAMFUNCTION cache_invalidate(volatile uint32_t *ccr,
    uint32_t inv_mask, uint32_t go_mask)
{
    uint32_t t = RT7XX_REG_POLL_LIMIT;

    *ccr |= inv_mask | go_mask;
    while (((*ccr & go_mask) != 0u) && (t > 0u)) {
        t--;
    }
    *ccr &= ~inv_mask;
    return (t == 0u) ? kStatus_Timeout : kStatus_Success;
}

static status_t RAMFUNCTION xspi_ahb_flush(void)
{
    status_t status;
    uint32_t t = RT7XX_REG_POLL_LIMIT;

    XSPI_NOR->SPTRCLR |= XSPI_SPTRCLR_ABRT_CLR_MASK;
    while (((XSPI_NOR->SPTRCLR & XSPI_SPTRCLR_ABRT_CLR_MASK) != 0u) &&
           (t > 0u)) {
        t--;
    }
    if (t == 0u) {
        return kStatus_Timeout;
    }
    /* CACHE64_CTRL0 backs the XSPI0 aperture; XCACHE0/1 are the core caches.
     * Invalidate all three so an AHB read after a program or erase sees the
     * new NOR contents. */
    status = cache_invalidate(&CACHE64_CTRL0->CCR,
        CACHE64_CTRL_CCR_INVW0_MASK | CACHE64_CTRL_CCR_INVW1_MASK,
        CACHE64_CTRL_CCR_GO_MASK);
    if (status == kStatus_Success) {
        status = cache_invalidate(&XCACHE0->CCR,
            XCACHE_CCR_INVW0_MASK | XCACHE_CCR_INVW1_MASK, XCACHE_CCR_GO_MASK);
    }
    if (status == kStatus_Success) {
        status = cache_invalidate(&XCACHE1->CCR,
            XCACHE_CCR_INVW0_MASK | XCACHE_CCR_INVW1_MASK, XCACHE_CCR_GO_MASK);
    }
    return status;
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    (void)xspi_ahb_flush();
}

/* Bounded IP transaction layer. XSPI_TransferBlocking mirrors this exact
 * sequence but every wait in it is open ended, and the SFP never grants a
 * request it refuses; here each wait has a deadline, a refusal is reported
 * instead of spun on, and a timeout resets the queue and the flash domain
 * before returning. */
/* LUT unlock key, private to the SDK driver source */
#define XSPI_LUT_KEY_VAL 0x5AF05AF0u
#define XSPI_IP_ERRORS \
    ((uint32_t)kXSPI_ErrorAllFlags & ~(uint32_t)XSPI_ERRSTAT_ARB_WIN_MASK)

static status_t RAMFUNCTION xspi_ip_recover(void)
{
    XSPI_ResetTgQueue(XSPI_NOR);
    XSPI_ResetSfmAndAhbDomain(XSPI_NOR);
    XSPI_NOR->ERRSTAT = XSPI_NOR->ERRSTAT;
    return kStatus_Timeout;
}

static status_t RAMFUNCTION xspi_ip_start(uint32_t addr, uint8_t seq,
    uint32_t size)
{
    uint32_t t = RT7XX_REG_POLL_LIMIT;
    uint32_t err;
    status_t status;

    XSPI_NOR->ERRSTAT = XSPI_NOR->ERRSTAT;
    while (((XSPI_NOR->TGSFARS & XSPI_TGSFARS_VLD_MASK) != 0u) && (t > 0u)) {
        t--;
    }
    if (t == 0u) {
        return xspi_ip_recover();
    }
    XSPI_NOR->SFP_TG_SFAR = addr;
    if (((XSPI_NOR->MGC & XSPI_MGC_GVLDMDAD_MASK) != 0u) &&
        ((XSPI_NOR->TG0MDAD & XSPI_TG0MDAD_VLD_MASK) != 0u)) {
        t = RT7XX_REG_POLL_LIMIT;
        do {
            err = XSPI_NOR->TGSFARS &
                  (XSPI_TGSFARS_VLD_MASK | XSPI_TGSFARS_ERR_MASK);
            if (err == XSPI_TGSFARS_ERR_MASK) {
                return kStatus_XSPI_IpAccessAddrSettingInvalid;
            }
            t--;
        } while ((err != XSPI_TGSFARS_VLD_MASK) && (t > 0u));
        if (t == 0u) {
            return xspi_ip_recover();
        }
    }
    XSPI_NOR->SFP_TG_IPCR = XSPI_SFP_TG_IPCR_IDATSZ(size) |
                            XSPI_SFP_TG_IPCR_SEQID(seq);
    t = RT7XX_REG_POLL_LIMIT;
    while (t > 0u) {
        err = XSPI_NOR->ERRSTAT;
        if ((err & XSPI_ERRSTAT_ARB_WIN_MASK) != 0u) {
            break;
        }
        if ((err & XSPI_IP_ERRORS) != 0u) {
            status = XSPI_CheckAndClearError(XSPI_NOR, err);
            XSPI_NOR->TGIPCRS |= XSPI_TGIPCRS_CLR_MASK;
            XSPI_NOR->TGSFARS |= XSPI_TGSFARS_CLR_MASK;
            return status;
        }
        t--;
    }
    if (t == 0u) {
        return xspi_ip_recover();
    }
    XSPI_ClearErrorStatusFlags(XSPI_NOR, kXSPI_ArbWinEventFlag);
    return kStatus_Success;
}

static status_t RAMFUNCTION xspi_ip_idle(void)
{
    uint32_t t = RT7XX_REG_POLL_LIMIT;

    while ((XSPI_GetBusIdleStatus(XSPI_NOR) == false) && (t > 0u)) {
        t--;
    }
    return (t == 0u) ? xspi_ip_recover() : kStatus_Success;
}

static status_t RAMFUNCTION xspi_ip_released(void)
{
    uint32_t t = RT7XX_REG_POLL_LIMIT;

    while ((XSPI_CheckIPAccessAsserted(XSPI_NOR) == true) && (t > 0u)) {
        t--;
    }
    return (t == 0u) ? xspi_ip_recover() : kStatus_Success;
}

static status_t RAMFUNCTION xspi_ip_command(uint32_t addr, uint8_t seq)
{
    status_t status;

    status = xspi_ip_start(addr, seq, 0u);
    if (status == kStatus_Success) {
        status = xspi_ip_idle();
    }
    if (status == kStatus_Success) {
        status = XSPI_CheckAndClearError(XSPI_NOR, XSPI_NOR->ERRSTAT);
    }
    if (status == kStatus_Success) {
        status = xspi_ip_released();
    }
    return status;
}

/* One whole-word chunk no larger than the program page: the only write this
 * port issues is a 256 byte page program. */
static status_t RAMFUNCTION xspi_ip_write(uint32_t addr, uint8_t seq,
    const uint32_t *words, uint32_t size)
{
    uint32_t t;
    uint32_t i;
    status_t status;

    status = xspi_ip_start(addr, seq, size);
    if (status != kStatus_Success) {
        return status;
    }
    XSPI_ClearTxBuffer(XSPI_NOR);
    t = RT7XX_REG_POLL_LIMIT;
    while ((XSPI_CheckTxBuffLockOpen(XSPI_NOR) == false) && (t > 0u)) {
        t--;
    }
    if (t == 0u) {
        return xspi_ip_recover();
    }
    XSPI_NOR->TBCT = (256u + 1u) - (size / 4u);
    status = XSPI_CheckAndClearError(XSPI_NOR, XSPI_NOR->ERRSTAT);
    if (status != kStatus_Success) {
        return status;
    }
    for (i = 0u; i < (size / 4u); i++) {
        t = RT7XX_REG_POLL_LIMIT;
        while (((XSPI_NOR->SR & XSPI_SR_TXFULL_MASK) != 0u) && (t > 0u)) {
            t--;
        }
        if (t == 0u) {
            return xspi_ip_recover();
        }
        XSPI_NOR->TBDR = words[i];
    }
    XSPI_NOR->FR = XSPI_FR_TBFF_MASK;
    status = xspi_ip_released();
    if (status == kStatus_Success) {
        status = xspi_ip_idle();
    }
    if (status == kStatus_Success) {
        status = XSPI_CheckAndClearError(XSPI_NOR, XSPI_NOR->ERRSTAT);
    }
    return status;
}

/* Reads shorter than one word, as the status register read is. The SDK sets
 * a one word watermark for these and drains the single entry. */
static status_t RAMFUNCTION xspi_ip_read(uint32_t addr, uint8_t seq,
    uint8_t *out, uint32_t size)
{
    uint32_t t;
    uint32_t i;
    uint32_t word;
    status_t status;

    XSPI_ClearRxBuffer(XSPI_NOR);
    (void)XSPI_UpdateRxBufferWaterMark(XSPI_NOR, 4u);
    status = xspi_ip_start(addr, seq, size);
    if (status != kStatus_Success) {
        return status;
    }
    if (XSPI_CheckFSMValid(XSPI_NOR) == false) {
        XSPI_ClearRxBuffer(XSPI_NOR);
        return kStatus_NoTransferInProgress;
    }
    t = RT7XX_REG_POLL_LIMIT;
    while (((XSPI_NOR->SR & XSPI_SR_BUSY_MASK) == 0u) &&
           ((XSPI_NOR->SR & XSPI_SR_IP_ACC_MASK) != 0u) && (t > 0u)) {
        t--;
    }
    if (t == 0u) {
        return xspi_ip_recover();
    }
    t = RT7XX_REG_POLL_LIMIT;
    while (((XSPI_NOR->SR & XSPI_SR_RXWE_MASK) == 0u) && (t > 0u)) {
        if ((XSPI_NOR->ERRSTAT & XSPI_ERRSTAT_TO_ERR_MASK) != 0u) {
            XSPI_ClearRxBuffer(XSPI_NOR);
            XSPI_NOR->ERRSTAT = XSPI_ERRSTAT_TO_ERR_MASK;
            return xspi_ip_recover();
        }
        t--;
    }
    if (t == 0u) {
        return xspi_ip_recover();
    }
    if (XSPI_GetRxBufferAvailableBytesCount(XSPI_NOR) != 4u) {
        XSPI_ClearRxBuffer(XSPI_NOR);
        return kStatus_XSPI_RxBufferEntriesCountError;
    }
    status = XSPI_CheckAndClearError(XSPI_NOR, XSPI_NOR->ERRSTAT);
    if (status != kStatus_Success) {
        XSPI_ClearRxBuffer(XSPI_NOR);
        return status;
    }
    word = XSPI_NOR->RBDR[0];
    for (i = 0u; i < size; i++) {
        out[i] = (uint8_t)((word >> (8u * i)) & 0xFFu);
    }
    XSPI_ClearRxBuffer(XSPI_NOR);
    return xspi_ip_idle();
}

static status_t RAMFUNCTION xspi_lut_update(const uint32_t *lut, uint32_t count)
{
    volatile uint32_t *dst;
    uint32_t i;
    status_t status;

    status = xspi_ip_idle();
    if (status != kStatus_Success) {
        return status;
    }
    XSPI_NOR->LUTKEY = XSPI_LUT_KEY_VAL;
    XSPI_NOR->LCKCR = 0x02u;
    dst = &XSPI_NOR->LUT[0];
    for (i = 0u; i < count; i++) {
        dst[i] = lut[i];
    }
    XSPI_NOR->LUTKEY = XSPI_LUT_KEY_VAL;
    XSPI_NOR->LCKCR = 0x01u;
    return kStatus_Success;
}

static status_t RAMFUNCTION xspi_nor_read_reg(uint32_t addr, uint8_t seq,
    uint8_t *value)
{
    uint8_t reg[2];
    status_t status;

    status = xspi_ip_read(addr, seq, reg, (uint32_t)sizeof(reg));
    if (status == kStatus_Success) {
        *value = reg[0];
    }
    return status;
}

static status_t RAMFUNCTION xspi_nor_write_enable(uint32_t addr)
{
    uint8_t sr = 0u;
    status_t status;

    status = xspi_ip_command(addr, LUT_SEQ_IDX_WRITE_ENABLE_OPI);
    if (status == kStatus_Success) {
        status = xspi_nor_read_reg(NOR_BASE, LUT_SEQ_IDX_READ_STATUS_OPI, &sr);
    }
    if ((status == kStatus_Success) && ((sr & NOR_WEL_BIT) == 0u)) {
        status = kStatus_Fail;
    }
    return status;
}

static status_t RAMFUNCTION xspi_nor_check_fail(uint8_t mask)
{
    uint8_t scur = 0u;
    status_t status;

    /* RDSR and RDSCUR require a zero address in DTR-OPI, so read at the
     * device base regardless of the operation that is being checked. */
    status = xspi_nor_read_reg(NOR_BASE, LUT_SEQ_IDX_READ_SECURITY_OPI, &scur);
    if ((status == kStatus_Success) && ((scur & mask) != 0u)) {
        status = kStatus_Fail;
    }
    return status;
}

static status_t RAMFUNCTION xspi_nor_wait_bus_busy(void)
{
    uint8_t sr[2];
    status_t status;
    uint32_t t = RT7XX_NOR_BUSY_LIMIT;
    bool isBusy = true;

    do {
        status = xspi_ip_read(NOR_BASE, LUT_SEQ_IDX_READ_STATUS_OPI, sr,
                              (uint32_t)sizeof(sr));
        if (status != kStatus_Success) {
            return status;
        }
        isBusy = ((sr[0] & NOR_WIP_BIT) != 0u);
        t--;
    } while (isBusy && (t > 0u));
    return isBusy ? kStatus_Timeout : kStatus_Success;
}

static status_t RAMFUNCTION xspi_nor_erase_sector(uint32_t addr)
{
    status_t status;

    status = xspi_nor_wait_bus_busy();
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_nor_write_enable(addr);
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_ip_command(addr, LUT_SEQ_IDX_ERASE_SECTOR);
    if (status == kStatus_Success) {
        status = xspi_nor_wait_bus_busy();
    }
    if (status == kStatus_Success) {
        status = xspi_nor_check_fail(NOR_EFAIL_BIT);
    }
    return status;
}

static status_t RAMFUNCTION xspi_nor_page_program(uint32_t addr,
    const uint32_t *src)
{
    status_t status;

    status = xspi_nor_wait_bus_busy();
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_nor_write_enable(addr);
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_ip_write(addr, LUT_SEQ_IDX_PAGEPROGRAM_OCTAL, src, NOR_PAGE);
    if (status == kStatus_Success) {
        status = xspi_nor_wait_bus_busy();
    }
    if (status == kStatus_Success) {
        status = xspi_nor_check_fail(NOR_PFAIL_BIT);
    }
    return status;
}

/* Full SDK re-init of XSPI0: device config (DLL, sample clock, strobes) and
 * the LUT. Runs entirely from RAM: XIP is down until it returns. */
static status_t RAMFUNCTION xspi_nor_init(void)
{
    status_t status;
    xspi_config_t config;
    xspi_ahb_access_config_t ahbConfig;
    xspi_ip_access_config_t ipConfig;
    uint32_t tempLUT[CUSTOM_LUT_LENGTH];
    uint32_t i;

    for (i = 0u; i < CUSTOM_LUT_LENGTH; i++) {
        tempLUT[i] = customLUT[i];
    }

    deviceConfig.xspiRootClk = CLOCK_GetXspiClkFreq(0u);

    config.ptrAhbAccessConfig = &ahbConfig;
    config.ptrIpAccessConfig = &ipConfig;
    XSPI_GetDefaultConfig(&config);
    config.ptrAhbAccessConfig->ahbErrorPayload.highPayload = 0x5A5A5A5AUL;
    config.ptrAhbAccessConfig->ahbErrorPayload.lowPayload = 0x5A5A5A5AUL;
    config.ptrAhbAccessConfig->ptrAhbWriteConfig = NULL;
    config.ptrAhbAccessConfig->ARDSeqIndex = LUT_SEQ_IDX_READ;
    config.ptrAhbAccessConfig->enableAHBBufferWriteFlush = true;
    config.ptrAhbAccessConfig->enableAHBPrefetch = true;
    config.ptrIpAccessConfig->ptrSfpFradConfig = NULL;
    config.ptrIpAccessConfig->ptrSfpMdadConfig = NULL;
    config.ptrIpAccessConfig->ipAccessTimeoutValue = 0x00FFFFFFUL;
    config.ptrIpAccessConfig->sfpArbitrationLockTimeoutValue = 0xFFFFFFUL;

    XSPI_Init(XSPI_NOR, &config);
    status = XSPI_SetDeviceConfig(XSPI_NOR, &deviceConfig);
    if (status == kStatus_Success) {
        status = xspi_lut_update(tempLUT, CUSTOM_LUT_LENGTH);
    }
    if (status == kStatus_Success) {
        status = xspi_ahb_flush();
    }
    if (status == kStatus_Success) {
        g_xspi_ready = 1;
    }
    return status;
}

static int RAMFUNCTION xspi_addr_ok(uint32_t address, int len)
{
    return imx_rt7xx_xspi0_addr_ok(address, len);
}

void RAMFUNCTION hal_flash_unlock(void)
{
    if (g_xspi_ready == 0) {
        (void)xspi_nor_init();
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    static uint32_t page_buf[NOR_PAGE / 4u];
    uint8_t *pb = (uint8_t *)page_buf;
    uint32_t page;
    uint32_t off;
    uint32_t n;
    uint32_t i;
    uint32_t primask;
    int ret = 0;

    if ((xspi_addr_ok(address, len) != 0) || (data == NULL)) {
        return -1;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if ((g_xspi_ready == 0) && (xspi_nor_init() != kStatus_Success)) {
        __set_PRIMASK(primask);
        return -1;
    }
    address = NOR_BASE + NOR_OFFSET(address);
    while ((len > 0) && (ret == 0)) {
        page = address & ~(NOR_PAGE - 1u);
        off = address & (NOR_PAGE - 1u);
        n = NOR_PAGE - off;
        if ((uint32_t)len < n) {
            n = (uint32_t)len;
        }
        if (n < NOR_PAGE) {
            if (xspi_ahb_flush() != kStatus_Success) {
                ret = -1;
                break;
            }
            for (i = 0u; i < NOR_PAGE; i++) {
                pb[i] = *(volatile uint8_t *)(page + i);
            }
        }
        for (i = 0u; i < n; i++) {
            pb[off + i] = data[i];
        }
        if (xspi_nor_page_program(page, page_buf) != kStatus_Success) {
            ret = -1;
        }
        address += n;
        data += n;
        len -= (int)n;
    }
    if (xspi_ahb_flush() != kStatus_Success) {
        ret = -1;
    }
    __set_PRIMASK(primask);
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t sector;
    uint32_t end;
    uint32_t primask;
    int ret = 0;

    if (xspi_addr_ok(address, len) != 0) {
        return -1;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if ((g_xspi_ready == 0) && (xspi_nor_init() != kStatus_Success)) {
        __set_PRIMASK(primask);
        return -1;
    }
    address = NOR_BASE + NOR_OFFSET(address);
    sector = address & ~(NOR_SECTOR - 1u);
    end = address + (uint32_t)len;
    while ((sector < end) && (ret == 0)) {
        if (xspi_nor_erase_sector(sector) != kStatus_Success) {
            ret = -1;
        }
        sector += NOR_SECTOR;
    }
    if (xspi_ahb_flush() != kStatus_Success) {
        ret = -1;
    }
    __set_PRIMASK(primask);
    return ret;
}

#define FRAD_GRANULE        0x10000u
#define FRAD_ACCESS_NONE    0x0u
#define FRAD_ACCESS_ALL     0x7u
#define FRAD_WORD2_ACP_MASK XSPI_FRAD0_WORD2_MD0ACP_MASK
#define FRAD_WORD2_ACP_ALL  XSPI_FRAD0_WORD2_MD0ACP(FRAD_ACCESS_ALL)
#define FRAD_WORD3_LOCKED   XSPI_FRAD0_WORD3_LOCK(kXSPI_DescriptorLockEnabledTillHardReset)
#define FRAD_WORD3_LOCK_VLD (XSPI_FRAD0_WORD3_LOCK_MASK | XSPI_FRAD0_WORD3_VLD_MASK | \
                             XSPI_FRAD0_WORD3_EAL_MASK)
#define FRAD_WORD(n, w) \
    (*(volatile uint32_t *)((uint32_t)XSPI_NOR_S + 0x800u + ((n) * 0x20u) + ((w) * 4u)))

#ifdef XSPI_FLASH_PROTECT_SELFTEST
#define PST(n) (*(volatile uint32_t *)(0x20180080u + ((n) * 4u)))
/* Behind the armed fence: a status read at a partition address must still
 * succeed (the IP path and its domain handshake work), and a sector erase of
 * the last, blank 64 KB block of the boot root must come back refused with
 * kStatus_XSPI_FradCheckError, which leaves the block untouched either way. */
static void RAMFUNCTION xspi_flash_protect_selftest(uint32_t end)
{
    uint8_t sr[2];

    PST(0) = 0x50510001u;
    PST(1) = end - FRAD_GRANULE;
    PST(2) = (uint32_t)xspi_ip_read(NOR_BASE, LUT_SEQ_IDX_READ_STATUS_OPI, sr,
                                    (uint32_t)sizeof(sr));
    PST(3) = (uint32_t)xspi_ip_command(end - FRAD_GRANULE,
                                       LUT_SEQ_IDX_ERASE_SECTOR);
    PST(4) = (uint32_t)kStatus_XSPI_FradCheckError;
    (void)xspi_ahb_flush();
    PST(5) = *(volatile uint32_t *)(end - FRAD_GRANULE);
    /* Pass only if the read behind the fence worked, the boot-root erase
     * was refused with a FRAD check error, and the block stayed erased. */
    PST(6) = ((PST(2) == (uint32_t)kStatus_Success) &&
              (PST(3) == (uint32_t)kStatus_XSPI_FradCheckError) &&
              (PST(5) == 0xFFFFFFFFu)) ? 0x505150AAu : 0x505150EEu;
    wolfBoot_printf("xspi protect selftest %s\n",
        (PST(6) == 0x505150AAu) ? "PASS" : "FAIL");
    PST(0) = 0x50510002u;
}
#endif

/* Bootloader write protection via the XSPI SFP flash region descriptors.
 * FRAD0 covers the boot root from the start of the NOR (so the FCB the
 * BootROM reads is fenced too) through the end of the requested region and
 * grants no write access to any domain, which refuses program and erase while
 * leaving reads, and so XIP, unrestricted; FRAD1 keeps the partitions
 * writable. Every descriptor, used or not, and the global configuration are
 * locked until the next reset so nothing can be added, widened or switched
 * off from the application. FRAD boundaries are 64 KB, end inclusive. */
int RAMFUNCTION hal_flash_protect(uint32_t address, int len)
{
    static xspi_sfp_frad_config_t frad;
    static xspi_sfp_mdad_config_t mdad;
    uint32_t end;
    uint32_t i;

    if ((len <= 0) || (xspi_addr_ok(address, len) != 0)) {
        return -1;
    }
    end = NOR_BASE + NOR_OFFSET(address) + (uint32_t)len;
    if ((end & (FRAD_GRANULE - 1u)) != 0u) {
        return -1;
    }
    if ((g_xspi_ready == 0) && (xspi_nor_init() != kStatus_Success)) {
        return -1;
    }

    for (i = 0u; i < XSPI_SFP_FRAD_COUNT; i++) {
        frad.fradConfig[i].startAddress = 0u;
        frad.fradConfig[i].endAddress = 0u;
        frad.fradConfig[i].tg0MasterAccess = FRAD_ACCESS_NONE;
        frad.fradConfig[i].tg1MasterAccess = FRAD_ACCESS_NONE;
        frad.fradConfig[i].assignIsValid = false;
        frad.fradConfig[i].descriptorLock = kXSPI_DescriptorLockEnabledTillHardReset;
        frad.fradConfig[i].exclusiveAccessLock = kXSPI_ExclusiveAccessLockDisabled;
    }
    frad.fradConfig[0].startAddress = NOR_BASE;
    frad.fradConfig[0].endAddress = end - 1u;
    frad.fradConfig[0].assignIsValid = true;
    frad.fradConfig[1].startAddress = end;
    frad.fradConfig[1].endAddress = NOR_BASE + NOR_SIZE - 1u;
    frad.fradConfig[1].tg0MasterAccess = FRAD_ACCESS_ALL;
    frad.fradConfig[1].tg1MasterAccess = FRAD_ACCESS_NONE;
    frad.fradConfig[1].assignIsValid = true;
    frad.fradConfig[1].descriptorLock = kXSPI_DescriptorLockDisabled;

    /* Region policy is held per initiator domain, so the fence needs a
     * domain to bind to: one locked domain that every initiator matches
     * (AND mask 0 against reference 0) makes the fence uniform for the
     * whole SoC. */
    for (i = 0u; i < XSPI_TARGET_GROUP_COUNT; i++) {
        mdad.tgMdad[i].assignIsValid = false;
        mdad.tgMdad[i].enableDescriptorLock = true;
        mdad.tgMdad[i].maskType = (uint8_t)kXSPI_MdadMaskTypeAnd;
        mdad.tgMdad[i].mask = 0u;
        mdad.tgMdad[i].masterIdReference = 0u;
        mdad.tgMdad[i].secureAttribute =
            kXSPI_AttributeMasterNonsecureSecureBoth;
    }
    mdad.tgMdad[0].assignIsValid = true;

    XSPI_UpdateSFPConfig(XSPI_NOR_S, &mdad, &frad);
    FRAD_WORD(1, 2) = FRAD_WORD2_ACP_ALL;
    FRAD_WORD(1, 3) |= FRAD_WORD3_LOCKED;
    XSPI_NOR_S->MGC |= XSPI_MGC_GCLCK(1u);

    /* The driver cannot report a locked or ignored write, so trust only
     * what the registers read back. */
    if ((XSPI_NOR_S->MGC & (XSPI_MGC_GVLDFRAD_MASK | XSPI_MGC_GVLD_MASK)) !=
        (XSPI_MGC_GVLDFRAD_MASK | XSPI_MGC_GVLD_MASK)) {
        return -1;
    }
    if ((XSPI_NOR_S->MGC & XSPI_MGC_GCLCK_MASK) == 0u) {
        return -1;
    }
    if (((XSPI_NOR_S->MGC & XSPI_MGC_GVLDMDAD_MASK) == 0u) ||
        ((XSPI_NOR_S->TG0MDAD & (XSPI_TG0MDAD_VLD_MASK | XSPI_TG0MDAD_LCK_MASK)) !=
         (XSPI_TG0MDAD_VLD_MASK | XSPI_TG0MDAD_LCK_MASK))) {
        return -1;
    }
    if (((FRAD_WORD(0, 0) & 0xFFFF0000u) != (NOR_BASE & 0xFFFF0000u)) ||
        ((FRAD_WORD(0, 1) & 0xFFFF0000u) != ((end - 1u) & 0xFFFF0000u)) ||
        ((FRAD_WORD(0, 2) & FRAD_WORD2_ACP_MASK) != 0u) ||
        ((FRAD_WORD(0, 3) & FRAD_WORD3_LOCK_VLD) !=
         (FRAD_WORD3_LOCKED | XSPI_FRAD0_WORD3_VLD_MASK))) {
        return -1;
    }
    if (((FRAD_WORD(1, 0) & 0xFFFF0000u) != (end & 0xFFFF0000u)) ||
        ((FRAD_WORD(1, 1) & 0xFFFF0000u) !=
         ((NOR_BASE + NOR_SIZE - 1u) & 0xFFFF0000u)) ||
        ((FRAD_WORD(1, 2) & FRAD_WORD2_ACP_MASK) != FRAD_WORD2_ACP_ALL) ||
        ((FRAD_WORD(1, 3) & FRAD_WORD3_LOCK_VLD) !=
         (FRAD_WORD3_LOCKED | XSPI_FRAD0_WORD3_VLD_MASK))) {
        return -1;
    }
    for (i = 2u; i < XSPI_SFP_FRAD_COUNT; i++) {
        if ((FRAD_WORD(i, 3) & FRAD_WORD3_LOCK_VLD) != FRAD_WORD3_LOCKED) {
            return -1;
        }
    }
#ifdef XSPI_FLASH_PROTECT_SELFTEST
    xspi_flash_protect_selftest(end);
#endif
    return 0;
}

#ifdef XSPI_FLASH_SELFTEST
#define SELFTEST_ADDR (NOR_BASE + NOR_SIZE - 0x10000u)
#define ST(n) (*(volatile uint32_t *)(0x20180040u + ((n) * 4u)))
static void xspi_flash_selftest(void)
{
    static uint8_t buf[NOR_PAGE + 100u];
    uint32_t i;
    uint8_t b;
    int bad = 0;
    int ret;

    ST(0) = 0x4001u;
    hal_flash_unlock();
    ST(1) = deviceConfig.xspiRootClk;
    ret = hal_flash_erase(SELFTEST_ADDR, NOR_SECTOR);
    ST(2) = (uint32_t)ret;
    for (i = 0u; (ret == 0) && (i < NOR_SECTOR); i++) {
        b = *(volatile uint8_t *)(SELFTEST_ADDR + i);
        if (b != 0xFFu) {
            ret = -2;
        }
    }
    ST(3) = (uint32_t)ret;
    for (i = 0u; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)(i ^ 0x5Au);
    }
    if (ret == 0) {
        /* full page at +0, then a sub-page RMW spanning a page boundary */
        ret = hal_flash_write(SELFTEST_ADDR, buf, (int)NOR_PAGE);
        if (ret == 0) {
            ret = hal_flash_write(SELFTEST_ADDR + NOR_PAGE + 60u,
                                  buf + NOR_PAGE, 100);
        }
    }
    ST(4) = (uint32_t)ret;
    for (i = 0u; (ret == 0) && (i < NOR_PAGE); i++) {
        b = *(volatile uint8_t *)(SELFTEST_ADDR + i);
        if (b != buf[i]) {
            bad++;
        }
    }
    for (i = 0u; (ret == 0) && (i < 100u); i++) {
        b = *(volatile uint8_t *)(SELFTEST_ADDR + NOR_PAGE + 60u + i);
        if (b != buf[NOR_PAGE + i]) {
            bad++;
        }
    }
    ST(5) = (uint32_t)bad;
    ST(6) = 0xEEu;
    (void)xspi_nor_read_reg(NOR_BASE, LUT_SEQ_IDX_READ_SECURITY_OPI,
                            (uint8_t *)&ST(6));
    ST(7) = ((ret == 0) && (bad == 0)) ? 0x40AAu : 0x40EEu;
    wolfBoot_printf("xspi selftest %s (ret %d, mismatch %d, clk %u)\n",
        (ST(7) == 0x40AAu) ? "PASS" : "FAIL", ret, bad,
        (unsigned)deviceConfig.xspiRootClk);
}
#endif

void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    wolfBoot_printf("wolfBoot HAL init: MIMXRT798S\n");
#endif
#ifdef TZEN
    hal_sau_init();
#endif
#ifdef XSPI_FLASH_SELFTEST
    xspi_flash_selftest();
#endif
}

/* The shared boot path arms the flash fence only when TrustZone is off; a
 * secure application keeps Secure state, so arm it here for that case. */
void hal_prepare_boot(void)
{
#if defined(TZEN) && defined(__WOLFBOOT)
    if (hal_flash_protect(WOLFBOOT_ORIGIN, BOOTLOADER_PARTITION_SIZE) < 0) {
        wolfBoot_printf("Error protecting bootloader flash region\n");
        wolfBoot_panic();
    }
#endif
}
