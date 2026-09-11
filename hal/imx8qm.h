/* imx8qm.h
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

/* NXP i.MX 8QuadMax (MIMX8QM) hardware definitions for the MCIMX8QM-MEK.
 * Register bases from the upstream device tree.
 *
 * src/boot_aarch64_start.S includes this, so everything here must be a plain
 * integer expression - no C-only syntax. */

#ifndef _IMX8QM_H_
#define _IMX8QM_H_

/* Simple AArch64 startup: SCFW trains DDR and ATF BL31 has already run, so no
 * builtin EL3/MMU init is needed here (same model as tegra234). */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* --- BL33 image layout ---------------------------------------------------
 * ATF enters BL33 at 0x80020000, where u-boot.bin links on this SoC, so the
 * stock imx-mkimage recipe places wolfBoot unchanged. The payload and DTB are
 * bundled at fixed offsets for no-storage builds. Keep in sync with
 * hal/imx8qm.ld and tools/scripts/imx8qm/imx8qm-mkflashbin.sh. */
#define IMX8QM_BL33_BASE            0x80020000
/* Low DRAM window, used to sanity-check handoff pointers */
#define IMX8QM_DRAM_BASE            0x80000000
#define IMX8QM_DRAM_END             0x100000000
#define IMX8QM_BUNDLE_OFFSET        0x200000    /* 2 MB: signed payload */
#define IMX8QM_DTB_OFFSET           0x300000    /* 3 MB: device tree    */
#define IMX8QM_BL33_MAX_SIZE        0x400000    /* container image cap  */

/* Update partition, staged in low DRAM above the BL33 image. A constant
 * rather than a linker symbol: an ADRP against an absolute symbol this far
 * from the image base does not fit its +/-4GB reach. */
#define IMX8QM_UPDATE_ADDR          0x88000000

/* --- Generic timer -------------------------------------------------------
 * Fallback if firmware left CNTFRQ_EL0 unprogrammed. The i.MX8QM system
 * counter runs at 8 MHz. */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ              8000000
#endif

/* --- Console: LPUART0 ----------------------------------------------------
 * stdout-path in imx8qm-mek.dts, on the J11 FTDI interface 0 at 115200 8N1.
 * 32-bit little-endian map (UPIO_MEM32, not the Layerscape UPIO_MEM32BE) with
 * reg_off 0x10, which the offsets below account for. */
#define IMX8QM_LPUART0_BASE         0x5A060000
#define LPUART_VERID                0x00
#define LPUART_PARAM                0x04
#define LPUART_GLOBAL               0x08
#define LPUART_PINCFG               0x0C
#define LPUART_BAUD                 0x10
#define LPUART_STAT                 0x14
#define LPUART_CTRL                 0x18
#define LPUART_DATA                 0x1C
#define LPUART_MATCH                0x20
#define LPUART_MODIR                0x24
#define LPUART_FIFO                 0x28
#define LPUART_WATER                0x2C

#define LPUART_STAT_TDRE            (1 << 23)   /* TX data register empty */
#define LPUART_STAT_TC              (1 << 22)   /* transmit complete */
#define LPUART_STAT_RDRF            (1 << 21)   /* RX data register full */
#define LPUART_CTRL_TE              (1 << 19)   /* transmitter enable */
#define LPUART_CTRL_RE              (1 << 18)   /* receiver enable */

/* --- uSDHC ---------------------------------------------------------------
 * usdhc1 is the 8-bit eMMC, usdhc2 the 4-bit SD socket (card detect on
 * lsio_gpio5[22]). One is compiled in at a time via DISK_EMMC / DISK_SDCARD. */
#define IMX8QM_USDHC1_BASE          0x5B010000  /* eMMC, 8-bit */
#define IMX8QM_USDHC2_BASE          0x5B020000  /* SD card, 4-bit */
#define IMX8QM_USDHC3_BASE          0x5B030000  /* unused on the MEK */

/* uSDHC keeps eSDHC's register layout and adds MIX_CTRL, which holds the
 * transfer-mode half standard SDHCI keeps in the command register. */
#define USDHC_DS_ADDR               0x00
#define USDHC_BLK_ATT               0x04
#define USDHC_CMD_ARG               0x08
#define USDHC_CMD_XFR_TYP           0x0C
#define USDHC_CMD_RSP0              0x10
#define USDHC_CMD_RSP1              0x14
#define USDHC_CMD_RSP2              0x18
#define USDHC_CMD_RSP3              0x1C
#define USDHC_DATA_BUFF_ACC_PORT    0x20
#define USDHC_PRES_STATE            0x24
#define USDHC_PROT_CTRL             0x28
#define USDHC_SYS_CTRL              0x2C
#define USDHC_INT_STATUS            0x30
#define USDHC_INT_STATUS_EN         0x34
#define USDHC_INT_SIGNAL_EN         0x38
#define USDHC_AUTOCMD12_ERR_STATUS  0x3C
#define USDHC_HOST_CTRL_CAP         0x40
#define USDHC_WTMK_LVL              0x44
#define USDHC_MIX_CTRL              0x48
#define USDHC_FORCE_EVENT           0x50
#define USDHC_ADMA_ERR_STATUS       0x54
#define USDHC_ADMA_SYS_ADDR         0x58
#define USDHC_VEND_SPEC             0xC0
#define USDHC_MMC_BOOT              0xC4
#define USDHC_VEND_SPEC2            0xC8
#define USDHC_TUNING_CTRL           0xCC

/* PRES_STATE */
#define USDHC_PRES_CIHB             (1 << 0)    /* command inhibit (CMD) */
#define USDHC_PRES_CDIHB            (1 << 1)    /* command inhibit (DAT) */
#define USDHC_PRES_DLA              (1 << 2)    /* data line active */
#define USDHC_PRES_SDSTB            (1 << 3)    /* SD clock stable */
#define USDHC_PRES_BWEN             (1 << 10)   /* buffer write enable */
#define USDHC_PRES_BREN             (1 << 11)   /* buffer read enable */
#define USDHC_PRES_CINST            (1 << 16)   /* card inserted */
#define USDHC_PRES_DLSL_DAT0        (1 << 24)   /* DAT0 line signal level */

/* PROT_CTRL */
#define USDHC_PROT_DTW_SHIFT        1
#define USDHC_PROT_DTW_MASK         (0x3 << 1)
#define USDHC_PROT_DTW_1BIT         (0x0 << 1)
#define USDHC_PROT_DTW_4BIT         (0x1 << 1)
#define USDHC_PROT_DTW_8BIT         (0x2 << 1)
#define USDHC_PROT_D3CD             (1 << 3)
#define USDHC_PROT_EMODE_MASK       (0x3 << 4)
#define USDHC_PROT_EMODE_LE         (0x2 << 4)  /* little-endian data port */
#define USDHC_PROT_DMASEL_MASK      (0x3 << 8)
#define USDHC_PROT_DMASEL_SIMPLE    (0x0 << 8)

/* SYS_CTRL. The reset and data-timeout fields sit at the same bit positions
 * as standard SDHCI's SRS11, so those pass through the shim unchanged; the
 * clock divider (DVS/SDCLKFS) does not and is programmed directly. */
#define USDHC_SYS_DVS_SHIFT         4
#define USDHC_SYS_DVS_MASK          (0xF << 4)
#define USDHC_SYS_SDCLKFS_SHIFT     8
#define USDHC_SYS_SDCLKFS_MASK      (0xFF << 8)
#define USDHC_SYS_DTOCV_SHIFT       16
#define USDHC_SYS_DTOCV_MASK        (0xF << 16)
#define USDHC_SYS_RSTA              (1 << 24)   /* reset all */
#define USDHC_SYS_RSTC              (1 << 25)   /* reset command line */
#define USDHC_SYS_RSTD              (1 << 26)   /* reset data line */
#define USDHC_SYS_INITA             (1 << 27)   /* send 80 init clocks */

/* CMD_XFR_TYP (upper half is bit-identical to standard SDHCI's command half) */
#define USDHC_XFR_RSPTYP_MASK       (0x3 << 16)
#define USDHC_XFR_RSPTYP_48         (0x2 << 16)
#define USDHC_XFR_RSPTYP_48B        (0x3 << 16)
#define USDHC_XFR_DPSEL             (1 << 21)   /* data present */
#define USDHC_XFR_DTDSEL_READ       (1 << 4)    /* in MIX_CTRL, not here */

/* MIX_CTRL: the transfer-mode half. Bits 0,1,2,4,5 line up with standard
 * SDHCI (DMAEN/BCEN/AC12EN/DTDSEL/MSBSEL); the tuning and DDR bits above
 * them have no standard-SDHCI equivalent and must be preserved. */
#define USDHC_MIX_CTRL_XFER_MASK    0x3F

/* HOST_CTRL_CAP: the voltage-support bits are at the standard positions. */
#define USDHC_CAP_VS33              (1 << 24)
#define USDHC_CAP_VS30              (1 << 25)
#define USDHC_CAP_VS18              (1 << 26)

/* WTMK_LVL: read/write burst watermarks, in 4-byte words. Set to a full
 * 512-byte block so the generic driver's PIO loop sees one Buffer Read Ready
 * per block rather than one per 32 bytes. */
#define USDHC_WTMK_RD_SHIFT         0
#define USDHC_WTMK_WR_SHIFT         16
#define USDHC_WTMK_BLOCK_WORDS      0x80

/* VEND_SPEC */
#define USDHC_VEND_SPEC_VSELECT     (1 << 1)    /* 1.8V signaling */
#define USDHC_VEND_SPEC_FRC_SDCLK   (1 << 8)    /* force SD clock on */

/* uSDHC peripheral (per) clock. The SCU owns this clock tree; 198 MHz is the
 * rate the NXP BSP programs for uSDHC on this SoC. With IMX8QM_SCU=1 the real
 * rate is read back from the SCU and this is only the fallback. */
#ifndef IMX8QM_USDHC_PERCLK_HZ
#define IMX8QM_USDHC_PERCLK_HZ      198000000
#endif

/* --- FlexSPI0 ------------------------------------------------------------
 * MT35XU512ABA, 64 MB octal NOR. Memory-mapped AHB read window plus the
 * LUT-driven IP command path for erase/program; same IP as the Layerscape
 * XSPI block (hal/nxp_ls1028a.c). */
#define IMX8QM_FLEXSPI0_BASE        0x5D120000  /* controller registers */
#define IMX8QM_FLEXSPI0_AHB_BASE    0x08000000  /* memory-mapped read window */
#define IMX8QM_FLEXSPI0_SIZE        0x4000000   /* 64 MB */

#define FLEXSPI_MCR0                0x00
#define FLEXSPI_MCR1                0x04
#define FLEXSPI_MCR2                0x08
#define FLEXSPI_AHBCR               0x0C
#define FLEXSPI_INTEN               0x10
#define FLEXSPI_INTR                0x14
#define FLEXSPI_LUTKEY              0x18
#define FLEXSPI_LUTCR               0x1C
#define FLEXSPI_AHBRXBUF0CR0        0x20
#define FLEXSPI_FLSHA1CR0           0x60
#define FLEXSPI_FLSHA1CR1           0x70
#define FLEXSPI_FLSHA1CR2           0x80
#define FLEXSPI_IPCR0               0xA0
#define FLEXSPI_IPCR1               0xA4
#define FLEXSPI_IPCMD               0xB0
#define FLEXSPI_IPRXFCR             0xB8
#define FLEXSPI_IPTXFCR             0xBC
#define FLEXSPI_STS0                0xE0
#define FLEXSPI_STS1                0xE4
#define FLEXSPI_STS2                0xE8
#define FLEXSPI_AHBSPNDSTS          0xEC
#define FLEXSPI_IPRXFSTS            0xF0
#define FLEXSPI_IPTXFSTS            0xF4
#define FLEXSPI_RFDR0               0x100
#define FLEXSPI_TFDR0               0x180
#define FLEXSPI_LUT0                0x200

#define FLEXSPI_MCR0_SWRESET        (1 << 0)
#define FLEXSPI_MCR0_MDIS           (1 << 1)
#define FLEXSPI_LUTKEY_VALUE        0x5AF05AF0
#define FLEXSPI_LUTCR_LOCK          0x01
#define FLEXSPI_LUTCR_UNLOCK        0x02
#define FLEXSPI_IPCMD_TRG           (1 << 0)
#define FLEXSPI_INTR_IPCMDDONE      (1 << 0)
#define FLEXSPI_INTR_IPCMDGE        (1 << 1)
#define FLEXSPI_INTR_IPCMDERR       (1 << 3)
#define FLEXSPI_STS0_ARBIDLE        (1 << 0)
#define FLEXSPI_STS0_SEQIDLE        (1 << 1)
#define FLEXSPI_IPRXFCR_CLR         (1 << 0)
#define FLEXSPI_IPTXFCR_CLR         (1 << 0)
#define FLEXSPI_DLLACR              0xC0
#define FLEXSPI_DLLBCR              0xC4
#define FLEXSPI_DLLCR_OVRDEN        (1 << 8)
#define FLEXSPI_MCR0_AHBGRANTWAIT   (0xFF << 24)
#define FLEXSPI_MCR0_IPGRANTWAIT    (0xFF << 16)
#define FLEXSPI_MCR2_SAMEDEVEN      (1 << 15)
#define FLEXSPI_AHBCR_CACHABLE      (1 << 3)
#define FLEXSPI_AHBCR_BUFFERABLE    (1 << 4)
#define FLEXSPI_AHBCR_PREFETCH      (1 << 5)
#define FLEXSPI_FLSHCR2_ARDSEQID_S  0
#define FLEXSPI_INTR_IPRXWA         (1 << 5)
#define FLEXSPI_INTR_IPTXWE         (1 << 6)
/* IP TX/RX FIFO watermark, in 8-byte units of (value + 1). Left at the reset
 * value of 0, so the IP fill loop below moves 8 bytes per watermark event. */
#define FLEXSPI_IP_WM_BYTES         8

/* LUT sequence slots used by the HAL. Each sequence is 4 LUT words. */
#define FLEXSPI_LUT_SEQ_READ        0
#define FLEXSPI_LUT_SEQ_WREN        1
#define FLEXSPI_LUT_SEQ_RDSR        2
#define FLEXSPI_LUT_SEQ_SE          3   /* 128 KB sector erase (0xDC) */
#define FLEXSPI_LUT_SEQ_PP          4   /* page program */

/* LUT instruction opcodes (FlexSPI "INSTR" field) */
#define FLEXSPI_LUT_CMD             0x01
#define FLEXSPI_LUT_RADDR           0x02
#define FLEXSPI_LUT_DUMMY           0x03
#define FLEXSPI_LUT_READ            0x09
#define FLEXSPI_LUT_WRITE           0x08
#define FLEXSPI_LUT_STOP            0x00

/* LUT pad counts */
#define FLEXSPI_LUT_PAD1            0
#define FLEXSPI_LUT_PAD4            2

/* MT35XU512ABA / generic SPI NOR opcodes. 4-byte addressing is used
 * throughout: the part is 64 MB, past the 16 MB 3-byte limit. */
#define FLEXSPI_NOR_CMD_READ_4B     0x13    /* READ, 4-byte address */
#define FLEXSPI_NOR_CMD_PP_4B       0x12    /* PAGE PROGRAM, 4-byte address */
#define FLEXSPI_NOR_CMD_SE_4B       0xDC    /* SECTOR ERASE 128K, 4-byte addr */
#define FLEXSPI_NOR_CMD_WREN        0x06
#define FLEXSPI_NOR_CMD_RDSR        0x05
#define FLEXSPI_NOR_SR_WIP          0x01

/* The part also documents a 4 KB subsector erase (0x21), but 128 KB is what it
 * advertises and what U-Boot and Linux use. Being wrong in the unsafe direction
 * is unrecoverable, so match the advertised size. */
#define FLEXSPI_NOR_SECTOR_SIZE     0x20000
#define FLEXSPI_NOR_PAGE_SIZE       0x100    /* confirmed by "sf probe" */

/* --- Messaging Unit / System Controller ----------------------------------
 * A-core software reaches the SCU over MU1_A, the channel free for BL33 (TF-A
 * uses MU0). Only compiled with IMX8QM_SCU=1. */
#define IMX8QM_LSIO_MU1A_BASE       0x5D1C0000

#define MU_TR0                      0x00    /* transmit registers */
#define MU_RR0                      0x10    /* receive registers */
#define MU_SR                       0x20    /* status */
#define MU_CR                       0x24    /* control */

#define MU_SR_TE0                   (1 << 23)   /* TR0 empty */
#define MU_SR_RF0                   (1 << 27)   /* RR0 full */

/* SCU resources this HAL may touch (subset of the SCFW sc_rsrc_t enum).
 * Values match include/dt-bindings/firmware/imx/rsrc.h upstream. */
/* System MMU (MMU-500). BL33 must power it and set sCR0.CLIENTPD, or the OS
 * aborts on its first ID-register read. */
#define SC_R_SMMU                   17
/* SMMU stream ID both uSDHC controllers present, from the "iommus" property on
 * mmc@5b010000 and mmc@5b020000 in imx8qm-mek.dtb (<&smmu 0x11 0x7f80>). It is
 * the same for both, which is why Linux puts them in one iommu group. */
#define IMX8QM_USDHC_SMMU_SID       0x11
#define IMX8QM_SMMU_BASE            0x51400000
/* sCR0 is at offset 0; CLIENTPD bypasses translation for all clients. */
#define IMX8QM_SMMU_sCR0            0x00
#define IMX8QM_SMMU_sCR0_CLIENTPD   (1U << 0)

#define SC_R_SDHC_0                 248     /* uSDHC1 - eMMC */
#define SC_R_SDHC_1                 249     /* uSDHC2 - SD card */
#define SC_R_SDHC_2                 250
#define SC_R_FSPI_0                 237
#define SC_R_UART_0                 57

/* SCU clock types (sc_pm_clk_t) */
#define SC_PM_CLK_PER               2

/* Pad mux: powering and clocking LPUART0 is not enough, its pads must be
 * routed too. Values follow U-Boot's imx8qm_mek UART_PAD_CTRL. */
#define SC_P_UART0_RX               21
#define SC_P_UART0_TX               22
#define SC_PAD_CONFIG_OUT_IN        3
#define SC_PAD_ISO_OFF              0
#define SC_PAD_28FDSOI_DSE_DV_HIGH  0
#define SC_PAD_28FDSOI_PS_PU        1
#define PADRING_CONFIG_SHIFT        25
#define PADRING_LPCONFIG_SHIFT      23
#define PADRING_PULL_SHIFT          5
#define PADRING_DSE_SHIFT           0
#define PADRING_IFMUX_EN            (1U << 31)
#define PADRING_GP_EN               (1U << 30)
#define IMX8QM_UART_PAD_CTRL \
    (((uint32_t)SC_PAD_CONFIG_OUT_IN << PADRING_CONFIG_SHIFT) | \
     ((uint32_t)SC_PAD_ISO_OFF << PADRING_LPCONFIG_SHIFT) | \
     ((uint32_t)SC_PAD_28FDSOI_DSE_DV_HIGH << PADRING_DSE_SHIFT) | \
     ((uint32_t)SC_PAD_28FDSOI_PS_PU << PADRING_PULL_SHIFT))

/* uSDHC pads, from the MEK device tree pin groups; all mux function 0. Note the
 * numbering gap: USDHC1_DATA2 is 227, not 226.
 *
 * Naming is a trap: the pad ring calls the SD slot USDHC1_* and the eMMC
 * EMMC0_*, while the controllers are uSDHC2 and uSDHC1. Follow the base
 * address, not the pad name. */
#define SC_P_USDHC1_VSELECT         159
#define SC_P_USDHC1_CLK             222
#define SC_P_USDHC1_CMD             223
#define SC_P_USDHC1_DATA0           224
#define SC_P_USDHC1_DATA1           225
#define SC_P_USDHC1_DATA2           227
#define SC_P_USDHC1_DATA3           228

#define SC_P_EMMC0_CLK              209
#define SC_P_EMMC0_CMD              210
#define SC_P_EMMC0_DATA0            211
#define SC_P_EMMC0_DATA1            212
#define SC_P_EMMC0_DATA2            213
#define SC_P_EMMC0_DATA3            214
#define SC_P_EMMC0_DATA4            215
#define SC_P_EMMC0_DATA5            216
#define SC_P_EMMC0_DATA6            217
#define SC_P_EMMC0_DATA7            218
#define SC_P_EMMC0_STROBE           219
#define SC_P_EMMC0_RESET_B          220

#define IMX8QM_SD_PAD_CLK_CTRL      0x06000041  /* clock/strobe pads */
#define IMX8QM_SD_PAD_CTRL          0x00000021  /* cmd/data/vselect pads */

/* uSDHC low-power clock gating cells, one per controller, laid out with the
 * same stride as the controllers themselves. */
#define IMX8QM_USDHC0_LPCG          0x5B200000
#define IMX8QM_USDHC_LPCG_STRIDE    0x10000
#define IMX8QM_USDHC_LPCG(base) \
    (IMX8QM_USDHC0_LPCG + \
     ((((uintptr_t)(base)) - IMX8QM_USDHC1_BASE) / IMX8QM_USDHC_LPCG_STRIDE) \
       * IMX8QM_USDHC_LPCG_STRIDE)

/* Low Power Clock Gating. sc_pm_clock_enable() ungates at the SCU level; the
 * peripheral's own LPCG cell still has to be opened, and it needs the
 * double-write the reference driver documents. */
#define IMX8QM_LPUART0_LPCG         0x5A460000
/* FlexSPI0. Pads and LPCG from imx8qm-mek.dtb (flexspi0grp: pads 181-196, mux 0,
 * pad control 0x6000021) and U-Boot's imx8qm_lpcg.h (FSPI_0_LPCG). */
#define IMX8QM_FLEXSPI0_LPCG        0x5D520000
#define SC_P_QSPI0A_DATA0           181
#define SC_P_QSPI0B_SS1_B           196
#define IMX8QM_FLEXSPI_PAD_CTRL     0x06000021
#define LPCG_ALL_CLOCK_ON           0x22222222
#define LPCG_ALL_CLOCK_STOP         0x88888888

/* LPUART0 root clock the BSP programs, and the resulting 115200 divisor.
 * OSR 8 / SBR 87 lands within 0.25% of 115200 from 80 MHz. */
#define IMX8QM_UART_CLK_HZ          80000000
#define IMX8QM_LPUART_BAUD_115200   ((7U << 24) | 87U)
#define LPUART_BAUD_OSR_MASK        (0x1FU << 24)
#define LPUART_BAUD_SBR_MASK        0x1FFFU
#define LPUART_BAUD_M10             (1U << 29)
#define LPUART_BAUD_SBNS            (1U << 13)

/* SCU power modes (sc_pm_power_mode_t) */
#define SC_PM_PW_MODE_OFF           0
#define SC_PM_PW_MODE_ON            3

#endif /* _IMX8QM_H_ */
