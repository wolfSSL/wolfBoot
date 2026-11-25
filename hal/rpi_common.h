/* rpi_common.h
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

/* Register definitions shared by the Raspberry Pi Compute Module targets.
 * Included at the end of hal/cm4.h and hal/cm5.h, which define the RPI_*
 * base addresses first. Also reaches boot_aarch64_start.S, so pointer-cast
 * accessors are guarded with __ASSEMBLER__. */

#ifndef _RPI_COMMON_H_
#define _RPI_COMMON_H_

/* RNG200 (iproc-rng200) register offsets */
#define RNG200_CTRL             0x00  /* bit0 RBGEN = enable */
#define RNG200_SOFT_RESET       0x04
#define RNG200_RBG_SOFT_RESET   0x08
#define RNG200_INT_STATUS       0x18
#define RNG200_FIFO_DATA        0x20
#define RNG200_FIFO_COUNT       0x24  /* [7:0] = words available */
#define RNG200_CTRL_RBGEN       0x00000001

/* SP800-90B health-test status bits in RNG200_INT_STATUS (iproc-rng200).
 * MASTER_FAIL_LOCKOUT latches when the continuous test trips repeatedly;
 * NIST_FAIL flags a startup/continuous health-test failure. Either means the
 * entropy source is degraded and must not seed the FIPS DRBG. */
#define RNG200_INT_MASTER_FAIL_LOCKOUT     0x80000000
#define RNG200_INT_STARTUP_TRANSITIONS_MET 0x00020000
#define RNG200_INT_NIST_FAIL               0x00000020
#define RNG200_INT_TOTAL_BITS_COUNT        0x00000001
#define RNG200_INT_HEALTH_FAIL \
    (RNG200_INT_MASTER_FAIL_LOCKOUT | RNG200_INT_NIST_FAIL)

/* SDHCI register offsets used by the glue in hal/rpi_common.c. The generic
 * SDHCI driver addresses registers with Cadence-style SRS offsets (0x200 +
 * std); the glue translates to the standard layout below. Integers,
 * assembler-safe. */
#define CADENCE_SRS_OFFSET      0x200
#define STD_SDHCI_SDMA_ADDR     0x00  /* SDMA System Address (32-bit) */
#define STD_SDHCI_HOST_CTRL1    0x28
#define STD_SDHCI_POWER_CTRL    0x29
#define STD_SDHCI_BLKGAP_CTRL   0x2A
#define STD_SDHCI_WAKEUP_CTRL   0x2B
#define STD_SDHCI_CLK_CTRL      0x2C
#define STD_SDHCI_TIMEOUT_CTRL  0x2E
#define STD_SDHCI_SW_RESET      0x2F
#define STD_SDHCI_HOST_CTRL2    0x3C
#define STD_SDHCI_SRA           0x01  /* Software Reset for All */

#ifndef __ASSEMBLER__
/* RNG200 hardware TRNG register accessors */
#define RNG_CTRL        ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_CTRL))
#define RNG_SOFT_RESET  ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_SOFT_RESET))
#define RNG_RBG_SOFT_RESET ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_RBG_SOFT_RESET))
#define RNG_INT_STATUS  ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_INT_STATUS))
#define RNG_FIFO_DATA   ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_FIFO_DATA))
#define RNG_FIFO_COUNT  ((volatile unsigned int*)(RPI_RNG_BASE+RNG200_FIFO_COUNT))
#endif /* !__ASSEMBLER__ */

#endif /* _RPI_COMMON_H_ */
