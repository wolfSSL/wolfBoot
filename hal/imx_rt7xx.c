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
#include "printf.h"
#include "imx_rt7xx.h"

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_xspi.h"
#ifdef TZEN
#include "armv8m_tz.h"
#endif

/* The BootROM configures the compute-domain clocks and the XSPI0 XIP window
 * from the flash config block before handing control to this image. The XSPI
 * controller is re-initialized here with the NXP SDK driver so the IP command
 * path (program/erase/status) gets the full device configuration the ROM only
 * applies to the AHB read engine. Everything on that path executes from RAM:
 * XIP is unavailable while the controller is reconfigured and while the NOR
 * is busy (single die). */

#define RT7XX_REG(a) (*(volatile uint32_t *)(a))

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

    /* The BootROM leaves FCCLK0 halted and LP_FLEXCOMM0 gated + in reset */
    CLKCTL0_FCCLK0SEL = 0x4u;
    CLKCTL0_FCCLK0DIV |= FCCLK0_DIV_RESET;
    CLKCTL0_FCCLK0DIV = 0u;
    while ((CLKCTL0_FCCLK0DIV & FCCLK0_DIV_REQFLAG) != 0u) {
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

    for (i = 0; i < sz; i++) {
        while ((LPUART0_STAT & UART0_STAT_TDRE) == 0u) {
        }
        LPUART0_DATA = (uint32_t)(uint8_t)buf[i];
    }
}
#endif /* DEBUG_UART */

/* XSPI0 octal NOR (Macronix MX25UM51345G, OPI DDR) via the NXP SDK driver */

#define NOR_BASE       IMX_RT7XX_XSPI0_NS_BASE
#define NOR_SIZE       0x04000000u
#define NOR_SIZE_KB    0x10000u
#define NOR_PAGE       256u
#define NOR_SECTOR     0x1000u

/* Both apertures map the same NOR, bit 28 only carries the security
 * attribute, so the device offset is the low 26 bits either way. */
#define NOR_OFFSET(a)   ((a) & (NOR_SIZE - 1u))
#define NOR_APERTURE(a) ((a) & ~(NOR_SIZE - 1u))

#define XSPI_NOR       ((XSPI_Type *)IMX_RT7XX_XSPI0_REGS_NS)

#define LUT_SEQ_IDX_READ            0
#define LUT_SEQ_IDX_READ_STATUS     1
#define LUT_SEQ_IDX_READ_STATUS_OPI 2
#define LUT_SEQ_IDX_WRITE_ENABLE    3
#define LUT_SEQ_IDX_WRITE_ENABLE_OPI 4
#define LUT_SEQ_IDX_PAGEPROGRAM_OCTAL 7
#define LUT_SEQ_IDX_ERASE_SECTOR    8
#define LUT_SEQ_IDX_READ_ID_OPI     10
#define LUT_SEQ_IDX_ERASE_CHIP      11
#define LUT_SEQ_IDX_ENTER_OPI       12
#define CUSTOM_LUT_LENGTH           80

#define NOR_WIP_BIT    0x01u

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

    [5 * LUT_SEQ_IDX_ERASE_CHIP] =
        XSPI_LUT_SEQ(kXSPI_Command_DDR, kXSPI_8PAD, 0x60,
                     kXSPI_Command_DDR, kXSPI_8PAD, 0x9F),

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
static void RAMFUNCTION xspi_ahb_flush(void)
{
    volatile uint32_t *ccr;
    uint32_t t;

    XSPI_NOR->SPTRCLR |= XSPI_SPTRCLR_ABRT_CLR_MASK;
    t = 1000000u;
    while (((XSPI_NOR->SPTRCLR & XSPI_SPTRCLR_ABRT_CLR_MASK) != 0u) &&
           (t > 0u)) {
        t--;
    }
    ccr = (volatile uint32_t *)0x40033000u; /* XCACHE0 */
    if ((*ccr & 0x1u) != 0u) {
        *ccr |= 0x85000000u;
        while ((*ccr & 0x80000000u) != 0u) {
        }
    }
    ccr = (volatile uint32_t *)0x40034000u; /* XCACHE1 */
    if ((*ccr & 0x1u) != 0u) {
        *ccr |= 0x85000000u;
        while ((*ccr & 0x80000000u) != 0u) {
        }
    }
    ccr = (volatile uint32_t *)0x40035800u; /* CACHE64_CTRL0 CCR */
    if ((*ccr & 0x1u) != 0u) {
        *ccr |= 0x85000000u;
        while ((*ccr & 0x80000000u) != 0u) {
        }
    }
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    xspi_ahb_flush();
}

static status_t RAMFUNCTION xspi_nor_write_enable(uint32_t addr)
{
    xspi_transfer_t flashXfer;

    flashXfer.deviceAddress = addr;
    flashXfer.cmdType = kXSPI_Command;
    flashXfer.seqIndex = LUT_SEQ_IDX_WRITE_ENABLE_OPI;
    flashXfer.targetGroup = kXSPI_TargetGroup0;
    flashXfer.data = NULL;
    flashXfer.dataSize = 0UL;
    flashXfer.lockArbitration = false;
    return XSPI_TransferBlocking(XSPI_NOR, &flashXfer);
}

static status_t RAMFUNCTION xspi_nor_wait_bus_busy(void)
{
    xspi_transfer_t flashXfer;
    status_t status;
    uint32_t readValue = 0u;
    bool isBusy = true;

    flashXfer.deviceAddress = NOR_BASE;
    flashXfer.cmdType = kXSPI_Read;
    flashXfer.seqIndex = LUT_SEQ_IDX_READ_STATUS_OPI;
    flashXfer.targetGroup = kXSPI_TargetGroup0;
    flashXfer.data = &readValue;
    flashXfer.dataSize = 2;
    flashXfer.lockArbitration = false;
    do {
        status = XSPI_TransferBlocking(XSPI_NOR, &flashXfer);
        if (status != kStatus_Success) {
            return status;
        }
        isBusy = ((readValue & NOR_WIP_BIT) != 0u);
    } while (isBusy);
    return kStatus_Success;
}

static status_t RAMFUNCTION xspi_nor_erase_sector(uint32_t addr)
{
    xspi_transfer_t flashXfer;
    status_t status;

    status = xspi_nor_wait_bus_busy();
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_nor_write_enable(addr);
    if (status != kStatus_Success) {
        return status;
    }
    flashXfer.deviceAddress = addr;
    flashXfer.cmdType = kXSPI_Command;
    flashXfer.seqIndex = LUT_SEQ_IDX_ERASE_SECTOR;
    flashXfer.targetGroup = kXSPI_TargetGroup0;
    flashXfer.data = NULL;
    flashXfer.dataSize = 0UL;
    flashXfer.lockArbitration = false;
    status = XSPI_TransferBlocking(XSPI_NOR, &flashXfer);
    if (status != kStatus_Success) {
        return status;
    }
    return xspi_nor_wait_bus_busy();
}

static status_t RAMFUNCTION xspi_nor_page_program(uint32_t addr,
    const uint32_t *src)
{
    xspi_transfer_t flashXfer;
    status_t status;

    status = xspi_nor_wait_bus_busy();
    if (status != kStatus_Success) {
        return status;
    }
    status = xspi_nor_write_enable(addr);
    if (status != kStatus_Success) {
        return status;
    }
    flashXfer.deviceAddress = addr;
    flashXfer.cmdType = kXSPI_Write;
    flashXfer.seqIndex = LUT_SEQ_IDX_PAGEPROGRAM_OCTAL;
    flashXfer.targetGroup = kXSPI_TargetGroup0;
    flashXfer.data = (uint32_t *)(uintptr_t)src;
    flashXfer.dataSize = NOR_PAGE;
    flashXfer.lockArbitration = false;
    status = XSPI_TransferBlocking(XSPI_NOR, &flashXfer);
    if (status != kStatus_Success) {
        return status;
    }
    return xspi_nor_wait_bus_busy();
}

/* Full SDK re-init of XSPI0: device config (DLL, sample clock, strobes) and
 * the LUT. Runs entirely from RAM: XIP is down until it returns. */
static void RAMFUNCTION xspi_nor_init(void)
{
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
    config.ptrIpAccessConfig->ipAccessTimeoutValue = 0xFFFFFFFFUL;
    config.ptrIpAccessConfig->sfpArbitrationLockTimeoutValue = 0xFFFFFFUL;

    XSPI_Init(XSPI_NOR, &config);
    XSPI_SetDeviceConfig(XSPI_NOR, &deviceConfig);
    XSPI_UpdateLUT(XSPI_NOR, 0, tempLUT, CUSTOM_LUT_LENGTH);
    xspi_ahb_flush();
    g_xspi_ready = 1;
}

static int RAMFUNCTION xspi_addr_ok(uint32_t address, int len)
{
    uint32_t aperture = NOR_APERTURE(address);

    if ((len <= 0) ||
        ((aperture != IMX_RT7XX_XSPI0_NS_BASE) &&
         (aperture != IMX_RT7XX_XSPI0_S_BASE)) ||
        (NOR_OFFSET(address) + (uint32_t)len > NOR_SIZE)) {
        return -1;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    if (g_xspi_ready == 0) {
        xspi_nor_init();
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
    int ret = 0;

    if ((xspi_addr_ok(address, len) != 0) || (data == NULL)) {
        return -1;
    }
    if (g_xspi_ready == 0) {
        xspi_nor_init();
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
            xspi_ahb_flush();
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
    xspi_ahb_flush();
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t sector;
    uint32_t end;
    int ret = 0;

    if (xspi_addr_ok(address, len) != 0) {
        return -1;
    }
    if (g_xspi_ready == 0) {
        xspi_nor_init();
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
    xspi_ahb_flush();
    return ret;
}

int hal_flash_protect(uint32_t address, int len)
{
    (void)address;
    (void)len;
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
    ST(7) = ((ret == 0) && (bad == 0)) ? 0x40AAu : 0x40EEu;
    wolfBoot_printf("xspi selftest %s (ret %d, mismatch %d, clk %u)\n",
        (ST(7) == 0x40AAu) ? "PASS" : "FAIL", ret, bad,
        (unsigned)deviceConfig.xspiRootClk);
}
#endif

#ifdef TZEN
/* Hand off fully Secure: no Non-secure regions here, the secure application
 * carves out the Non-secure world for its own guests. */
static void hal_sau_init(void)
{
    SAU_CTRL = SAU_INIT_CTRL_ENABLE;
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;
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

void hal_prepare_boot(void)
{
}
