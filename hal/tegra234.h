/* tegra234.h
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

/* NVIDIA Jetson Orin (Tegra234) hardware definitions. Register bases are from
 * the public Tegra234 memory map / upstream device tree (tegra234.dtsi).
 *
 * src/boot_aarch64_start.S includes this header for the USE_*_STARTUP
 * selection below, so nothing here may expand to C-only syntax on the path the
 * assembler sees. Everything is a plain integer expression; the pointer
 * casting lives in the HAL (hal/tegra234.c).
 */

#ifndef _TEGRA234_H_
#define _TEGRA234_H_

/* Simple AArch64 startup (src/boot_aarch64_start.S): a .boot reset stub that
 * detects the EL, sets sp, and calls boot_entry_C. MB1/MB2 already brought up
 * DRAM/clocks, so no builtin EL3/MMU init is needed (same model as raspi3). */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* --- BL33 image layout ---------------------------------------------------
 * MB2 loads the cpu-bootloader (cpubl) slot at BL33_BASE and BL31 enters it
 * there; confirmed on hardware. The signed payload and the DTB are bundled
 * into the same image at fixed offsets so update_ram boots straight out of
 * DRAM with no storage driver. Offsets must clear wolfBoot's runtime
 * footprint (~600 KB incl. the 256 KB .stack) and the total must stay under
 * MB2's 4 MB cpubl cap. Keep in sync with hal/tegra234.ld and
 * tools/scripts/tegra234-mk*.sh. */
#define TEGRA234_BL33_BASE          0x272000000
/* Non-ECC DRAM window, used to sanity-check handoff pointers */
#define TEGRA234_DRAM_BASE          0x80000000
#define TEGRA234_DRAM_END           0x280000000
#define TEGRA234_BUNDLE_OFFSET      0x200000    /* 2 MB: signed payload */
#define TEGRA234_DTB_OFFSET         0x300000    /* 3 MB: device tree    */
#define TEGRA234_CPUBL_MAX_SIZE     0x400000    /* MB2 cap on the image */

/* Update partition, staged in low DRAM. A constant rather than a linker
 * symbol: an ADRP against an absolute symbol this far from the image base does
 * not fit its +/-4GB reach. */
#define TEGRA234_UPDATE_ADDR        0x81100000

/* --- Generic timer -------------------------------------------------------
 * Fallback if firmware left CNTFRQ_EL0 unprogrammed. Tegra234's timestamp
 * counter runs at 31.25 MHz (CNTFRQ_EL0 = 0x01DCD650, read on silicon). */
#ifndef TIMER_CLK_FREQ
#define TIMER_CLK_FREQ              31250000
#endif

/* --- Console: Tegra Combined UART (TCU) ----------------------------------
 * At BL33 the console is the TCU (Linux ttyTCU0), not a raw NS16550: a
 * physical UART such as UARTA (0x03100000) is neither clocked nor routed
 * here, and touching it faults with a CBB slave error (RAS uncorrectable)
 * that powers the core off. Confirmed on hardware 2026-07-27.
 *
 * TX is an AON HSP shared mailbox the SPE drains and forwards to the debug
 * UART. Per the platform device tree the TCU "tx" mailbox is SM1 of the AON
 * HSP (hsp@c150000); Tegra HSP lays shared mailboxes out at
 * 0x10000 + (n/2)*0x10000 + (n%2)*0x8000, so SM1 = 0x0c150000 + 0x18000
 * (this equals ARM Trusted Firmware's TEGRA_CONSOLE_SPE_BASE).
 *
 * Protocol (matches tegra-tcu / ATF console_spe): wait for FULL to clear,
 * then write FULL | count<<24 | up to 3 bytes packed little-endian. */
#define TEGRA_TCU_TX_MBOX           0x0c168000
#define TEGRA_TCU_MBOX_FULL         (1u << 31)
#define TEGRA_TCU_MBOX_NBYTES_SHIFT 24
#define TEGRA_TCU_MBOX_MAX_BYTES    3

/* --- BPMP IPC: IVC channel + HSP doorbell --------------------------------
 * Clocks and resets are owned by the BPMP. T234 uses the tegra186 BPMP layout
 * (cpu_tx channel at queue index 3, 256-byte queues), signalled by the top0
 * HSP doorbell for master BPMP (19 -> doorbell register index 3). */
#define TEGRA_BPMP_TXCH             0x40070300  /* TX SysRAM + cpu_tx(3)*256 */
#define TEGRA_BPMP_RXCH             0x40071300  /* RX SysRAM + 0x300         */
#define TEGRA_IVC_HDR_SZ            128         /* tx{count,state} + rx{count} */
#define TEGRA_IVC_TX_COUNT          0x00
#define TEGRA_IVC_TX_STATE          0x04
#define TEGRA_IVC_RX_COUNT          0x40
#define TEGRA_IVC_SYNC              0
#define TEGRA_IVC_ACK               1
#define TEGRA_IVC_ESTABLISHED       2

#define TEGRA_HSP_TOP0              0x03c00000
#define TEGRA_HSP_INT_DIMENSIONING  0x380
#define TEGRA_HSP_DB_BPMP           3

/* BPMP mb_data frame: u32 code, u32 flags, u8 data[120] */
#define TEGRA_BPMP_MB_CODE          0x00
#define TEGRA_BPMP_MB_FLAGS         0x04
#define TEGRA_BPMP_MB_DATA          0x08
#define TEGRA_BPMP_MB_DATA_SZ       120
#define TEGRA_BPMP_MSG_ACK          1

/* MRQ requests and the clock/reset sub-commands we issue */
#define TEGRA_BPMP_MRQ_PING         0
#define TEGRA_BPMP_MRQ_RESET        20
#define TEGRA_BPMP_MRQ_CLK          22
#define TEGRA_BPMP_CMD_CLK_GET_RATE 1
#define TEGRA_BPMP_CMD_CLK_SET_RATE 2
#define TEGRA_BPMP_CMD_CLK_ENABLE   7
#define TEGRA_BPMP_CMD_RESET_DEASSERT 2

#define TEGRA234_CLK_SDMMC1         120
#define TEGRA234_RESET_SDMMC1       82
/* SDMMC legacy timeout clock (tmclk). t234 SDHCI (NVQUIRK_HAS_TMCLK) keeps
 * this 12 MHz clock on at all times for data-timeout, and advertises it in
 * CAPS; without it the SD clock path never completes. */
#define TEGRA234_CLK_SDMMC_LEGACY_TM 219
#define TEGRA234_SDMMC_TMCLK_RATE    12000000
/* SDMMC1 base clock: the controller advertises 208 MHz in CAPS */
#define TEGRA234_SDMMC1_BASE_RATE    208000000

/* --- SDMMC / SDHCI -------------------------------------------------------
 * SDMMC1 (external microSD on the Orin Nano dev kit) is sdhci@3400000 in
 * tegra234.dtsi; SDMMC4 (eMMC) is at 0x03460000. Tegra234 SDMMC1 is a v4+
 * standard-SDHCI controller (HOSTVER 0x0505, A64S set). */
#define TEGRA_SDMMC1_BASE           0x03400000
#define TEGRA_SDMMC4_BASE           0x03460000
#ifndef TEGRA_SDHCI_BASE
#define TEGRA_SDHCI_BASE            TEGRA_SDMMC1_BASE
#endif

/* Offset at which the generic (Cadence) driver in src/sdhci.c places the
 * standard SD Host Controller "SRS" block; subtract it to reach Tegra's
 * standard-SDHCI registers, which begin at offset 0. */
#define TEGRA_SDHCI_SRS_OFFSET      0x200

/* Standard SDHCI registers the shim reaches directly (the Cadence HRS
 * definitions come from include/sdhci.h) */
#define TEGRA_STD_SW_RESET          0x2F  /* 8-bit  */
#define TEGRA_STD_SW_RESET_ALL      0x01
#define TEGRA_STD_CAPS1             0x40
#define TEGRA_STD_CAPS2             0x44
#define TEGRA_STD_PRESENT_STATE     0x24
#define TEGRA_STD_HOST_VERSION      0xFC

/* Tegra SDMMC vendor registers (outside the standard SDHCI range, so they
 * survive the driver's Software-Reset-for-All) */
#define TEGRA_VENDOR_CLOCK_CTRL     0x100
#define TEGRA_VCC_TAP_MASK          0x00FF0000
#define TEGRA_VCC_TAP_SHIFT         16
#define TEGRA_VCC_TRIM_MASK         0x1F000000
#define TEGRA_VCC_TRIM_SHIFT        24
#define TEGRA_VCC_PADPIPE_CLKEN_OVERRIDE (1u << 3)
#define TEGRA_VCC_SDR50_TUNING_OVERRIDE  (1u << 5)
#define TEGRA_VENDOR_MISC_CTRL      0x120
#define TEGRA_VMC_ENABLE_SDR104     0x08
#define TEGRA_VMC_ENABLE_SDR50      0x10
#define TEGRA_VMC_ENABLE_SPEC_300   0x20
#define TEGRA_SDMEM_COMP_PADCTRL    0x1e0
#define TEGRA_PADCTRL_VREF_SEL_MASK 0x0000000F
#define TEGRA_PADCTRL_VREF_SEL_VAL  0x7
#define TEGRA_PADCTRL_E_INPUT_E_PWRD (1u << 31)

/* SDMMC1 (microSD) default pad tap/trim from the platform device tree */
#define TEGRA_SDMMC1_DEFAULT_TAP    0x0E
#define TEGRA_SDMMC1_DEFAULT_TRIM   0x08

/* --- Main GPIO: microSD 3.3V rail ----------------------------------------
 * VDD_3V3_SD is a GPIO-controlled fixed regulator that MB1 leaves off (it
 * boots from QSPI). Per the platform device tree the enable is main-GPIO port
 * A pin 0 (regulator-vdd-3v3-sd, enable-active-high). Per-pin registers are
 * at base + bank*0x1000 + port*0x200 + pin*0x20, and port A is bank 0 /
 * port 0 / pin 0. */
#define TEGRA_MAIN_GPIO_A0          0x02210000
#define TEGRA_GPIO_ENABLE_CONFIG    0x00   /* bit0 ENABLE, bit1 OUT      */
#define TEGRA_GPIO_ENABLE_OUT       0x03   /* ENABLE | OUT               */
#define TEGRA_GPIO_OUTPUT_CONTROL   0x0C   /* bit0 FLOATED (0 = driven)  */
#define TEGRA_GPIO_OUTPUT_VALUE     0x10   /* bit0 HIGH                  */

#endif /* _TEGRA234_H_ */
