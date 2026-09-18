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

#ifndef IMX_RT7XX_H
#define IMX_RT7XX_H

#include <stdint.h>

/* XSPI0 octal NOR memory-mapped apertures (bit 28 selects the Secure alias). */
#define IMX_RT7XX_XSPI0_NS_BASE   0x28000000u
#define IMX_RT7XX_XSPI0_S_BASE    0x38000000u

/* XSPI0 controller register block (distinct from the flash-content aperture). */
#define IMX_RT7XX_XSPI0_REGS_S    0x50184000u
#define IMX_RT7XX_XSPI0_REGS_NS   0x40184000u

/* Compute-domain register blocks, Non-secure aliases (TZEN=0 boot). */
#define IMX_RT7XX_RSTCTL0_NS      0x40000000u
#define IMX_RT7XX_CLKCTL0_NS      0x40001000u
#define IMX_RT7XX_IOPCTL0_NS      0x40004000u
#define IMX_RT7XX_LPFC0_NS        0x40110000u /* LP_FLEXCOMM0 / LPUART0 */

#endif /* IMX_RT7XX_H */
