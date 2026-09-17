/* agilex5.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#ifndef WOLFSSL_AGILEX5_H
#define WOLFSSL_AGILEX5_H

#ifndef EL3_SECURE
#define EL3_SECURE      0
#endif
#ifndef EL2_HYPERVISOR
#define EL2_HYPERVISOR  1
#endif
#ifndef EL1_NONSECURE
#define EL1_NONSECURE   1
#endif
#ifndef HYP_GUEST
#define HYP_GUEST       0
#endif
#ifndef FPU_TRAP
#define FPU_TRAP        0
#endif
#ifndef SKIP_GIC_INIT
#define SKIP_GIC_INIT   1
#endif

/* Preserve EL2 for Linux/KVM, matching the normal TF-A -> U-Boot handoff. */
#if !defined(BOOT_EL1) && !defined(BOOT_EL2)
#define BOOT_EL2 1
#endif
#if defined(BOOT_EL1) && defined(BOOT_EL2)
#error "BOOT_EL1 and BOOT_EL2 are mutually exclusive"
#endif

#define AGILEX5_DDR_BASE       0x80000000UL
/* The DK-A5E013BM16AEA is fitted with 1792 MiB of LPDDR4.  The SPL/U-Boot
 * device tree normally fills this in at runtime; wolfBoot hands the DTB
 * directly to Linux, so the fixed board size must be supplied here. */
#define AGILEX5_DDR_SIZE       0x70000000UL
#define AGILEX5_UART0_BASE     0x10C02000UL
#define AGILEX5_UART1_BASE     0x10C02100UL
#define AGILEX5_SDHCI_BASE     0x10808000UL
#define CACHE_LINE_SIZE        64UL

#ifndef __ASSEMBLER__
unsigned int current_el(void);
#endif

#ifndef WOLFBOOT_LOAD_ADDRESS
#define WOLFBOOT_LOAD_ADDRESS  0x90000000UL
#endif
#ifndef WOLFBOOT_LOAD_DTS_ADDRESS
#define WOLFBOOT_LOAD_DTS_ADDRESS 0x8F000000UL
#endif
#ifndef WOLFBOOT_RAMBOOT_MAX_SIZE
#define WOLFBOOT_RAMBOOT_MAX_SIZE 0x10000000UL
#endif

#endif /* WOLFSSL_AGILEX5_H */
