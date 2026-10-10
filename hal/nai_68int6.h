/* nai_68int6.h
 *
 * Flash map and PCH SPI controller registers for the NAI 68INT6.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#ifndef NAI_68INT6_H
#define NAI_68INT6_H

/* Flash layout, from the Intel Flash Descriptor of a 32 MB 68INT6 image:
 *   Descriptor 0x00000000  ME/CSME 0x00001000  DevExp2 0x01001000  BIOS 0x01400000
 * FLASH_SIZE and FLASH_BIOS_REGION_ADDR are overridable at build time for a
 * board revision with a different device size or region map. */
#ifndef FLASH_SIZE
#define FLASH_SIZE                   0x02000000
#endif
#ifndef FLASH_BIOS_REGION_ADDR
#define FLASH_BIOS_REGION_ADDR       0x01400000
#endif
#define FLASH_BIOS_REGION_SIZE       (FLASH_SIZE - FLASH_BIOS_REGION_ADDR)
/* Descriptor, CSME and DevExp2 below the BIOS region, preserved verbatim. */
#define FLASH_INTEL_BOOT_REGION_SIZE (FLASH_BIOS_REGION_ADDR)
/* FLASH_RECOVERY_REGION_ADDR is intentionally left undefined. */

/* The SPI flash is aliased into the top of the 32-bit address space, so the
 * wolfBoot partition addresses (WOLFBOOT_PARTITION_BOOT_ADDRESS and friends)
 * are CPU aliases. Hardware sequencing (SPI_FADDR) wants a linear offset from
 * the start of the device, so translate before programming or erasing. */
#define FLASH_MEM_WINDOW_BASE        ((uint32_t)(0U - (uint32_t)FLASH_SIZE))
#define FLASH_CPU_TO_OFFSET(a)       ((uint32_t)(uintptr_t)(a) \
                                      - FLASH_MEM_WINDOW_BASE)

/* PCH SPI flash controller (00:1f.5), same location on PCH-LP as the 500
 * series. Register offsets are from the memory-mapped BAR0, per the Intel PCH
 * SPI register map (FREG0-7 at 0x54-0x74, FPR0-4 at 0x84-0x9C). */
#define SPI_PCI_DEV          31
#define SPI_PCI_FUN          5
#define SPI_FREG1            0x58
#define SPI_FREG_BASE_MASK   (0x7fffU << 0)
#define SPI_FREG_LIMIT_MASK  (0x7fffU << 16)
#define SPI_FREG_LIMIT_SHIFT 16
#define SPI_FREG_ADDR_SHIFT  12
#define SPI_FPR0             0x84
#define SPI_FPR_WPE          (1U << 31)
#define SPI_FPR_RPE          (1U << 15)
#define SPI_FLOCKDN          (1U << 15)

/* Hardware-sequencing registers, used to read/write/erase flash by linear
 * offset against the descriptor's region permissions. */
#define SPI_HSFSTS_CTL       0x04
#define SPI_FADDR            0x08
#define SPI_FDATA0           0x10
#define SPI_HSFSTS_FDONE     (1U << 0)
#define SPI_HSFSTS_FCERR     (1U << 1)
#define SPI_HSFSTS_HAEL      (1U << 2)
#define SPI_HSFSTS_SCIP      (1U << 5)
#define SPI_HSFCTL_FGO       (1U << 16)
#define SPI_HSFCTL_FCYCLE_SH 17
#define SPI_HSFCTL_FCYCLE_MK (0x7U << 17)
#define SPI_HSFCTL_FDBC_SH   24
#define SPI_HSFCTL_FDBC_MK   (0x3FU << 24)
#define SPI_FCYCLE_READ      0x0
#define SPI_FCYCLE_WRITE     0x2
#define SPI_FCYCLE_ERASE4K   0x3
#define SPI_HWSEQ_MAX        64
#define SPI_SECTOR_SIZE      0x1000
#define SPI_WAIT_MAX         2000000

#endif /* NAI_68INT6_H */
