/* imx95_a55.h
 *
 * Hardware definitions for the Cortex-A55 cluster on the NXP i.MX95.
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

/* wolfBoot as BL33 (third image in AHAB container 2, entered by BL31).
 * Included from assembly: integer expressions only, no C-only syntax.
 * Addresses measured on a Toradex SMARC iMX95. */

#ifndef _IMX95_A55_H_
#define _IMX95_A55_H_

/* Simple startup stub: DDR and EL3 setup already done by OEI/BL31 (same
 * model as tegra234). */
#define USE_BUILTIN_STARTUP
#define USE_SIMPLE_STARTUP

/* Boot chain: ROM -> ELE -> SM -> OEI -> SPL [c1] -> BL31 -> OP-TEE ->
 * wolfBoot [c2]. Load addresses fixed by the vendor imx-mkimage layout. */
#define IMX95_BL31_BASE        0x8A200000UL
#define IMX95_OPTEE_BASE       0x8C000000UL
#define IMX95_OPTEE_SIZE       0x02000000UL   /* 32 MiB */
#define IMX95_OPTEE_SHM_BASE   0x8E000000UL
#define IMX95_OPTEE_SHM_SIZE   0x00200000UL   /* 2 MiB */
#define IMX95_BL33_BASE        0x90200000UL

/* DRAM bank 0; below 0x90000000 is carved out for M7/BL31/OP-TEE. */
#define IMX95_DRAM_BASE        0x90000000UL
#define IMX95_DRAM_SIZE        0x70000000UL   /* 1.75 GiB, bank 0 */
#define IMX95_DRAM_END         (IMX95_DRAM_BASE + IMX95_DRAM_SIZE)

/* Reserved windows inside bank 0 that staging must not overlap. */
#define IMX95_M7_DDR_BASE      0x80000000UL
#define IMX95_M7_DDR_SIZE      0x01000000UL   /* 16 MiB */
#define IMX95_ELE_SHM_BASE     0x9C300000UL
#define IMX95_ELE_SHM_SIZE     0x00100000UL   /* 1 MiB */
#define IMX95_VPU_BOOT_BASE    0xA0000000UL
#define IMX95_VPU_BOOT_SIZE    0x00100000UL   /* 1 MiB */

/* Console: LPUART1 (ttyLP1), already clocked/pinmuxed at 115200 by the
 * prior stages; BAUD is not reprogrammed (its ref clock is SM-owned).
 * NXP LPUART v2 register block (same layout as hal/s32k1xx.h). */
#define IMX95_LPUART1_BASE     0x44380000UL

#define LPUART_STAT_OFF        0x14UL
#define LPUART_CTRL_OFF        0x18UL
#define LPUART_DATA_OFF        0x1CUL

#define LPUART_STAT_TDRE       (1UL << 23)  /* Transmit Data Register Empty */
#define LPUART_STAT_TC         (1UL << 22)  /* Transmission Complete */
#define LPUART_CTRL_TE         (1UL << 19)  /* Transmitter Enable */

/* Storage: uSDHC1 = module eMMC, uSDHC2 = carrier SD slot. */
#define IMX95_USDHC1_BASE      0x42850000UL   /* eMMC */
#define IMX95_USDHC2_BASE      0x42860000UL   /* carrier SD */

/* M7 TCMs through the system aperture (M7 links for its core view). */
#define IMX95_M7_ITCM_SYS      0x203C0000UL   /* M7 core view 0x00000000 */
#define IMX95_M7_DTCM_SYS      0x20400000UL   /* M7 core view 0x20000000 */
#define IMX95_M7_TCM_SIZE      0x00040000UL   /* 256 KiB each */

#endif /* _IMX95_A55_H_ */
