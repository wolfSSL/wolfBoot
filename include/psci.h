/* psci.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Interface between the EL3 PSCI runtime (src/psci.c) and the platform HAL
 * that owns the SMC vector and the cores. */

#ifndef PSCI_H
#define PSCI_H

#include <stdint.h>

#ifndef WOLFBOOT_PSCI_NUM_CPUS
#define WOLFBOOT_PSCI_NUM_CPUS  4
#endif

/* SMCCC fast calls: bit 31 fast, bit 30 SMC64, bits 29:24 owner */
#define SMCCC_SMC64                 0x40000000UL
#define SMCCC_VERSION               0x80000000UL
#define SMCCC_ARCH_FEATURES         0x80000001UL
#define SMCCC_VERSION_1_1           0x00010001UL

#define PSCI_VERSION                0x84000000UL
#define PSCI_CPU_SUSPEND            0x84000001UL
#define PSCI_CPU_OFF                0x84000002UL
#define PSCI_CPU_ON                 0x84000003UL
#define PSCI_AFFINITY_INFO          0x84000004UL
#define PSCI_MIGRATE_INFO_TYPE      0x84000006UL
#define PSCI_SYSTEM_OFF             0x84000008UL
#define PSCI_SYSTEM_RESET           0x84000009UL
#define PSCI_FEATURES               0x8400000AUL
#define PSCI_VERSION_1_1            0x00010001UL

/* MIGRATE_INFO_TYPE: no Trusted OS, migration not required */
#define PSCI_TOS_NOT_PRESENT        2

#define PSCI_E_SUCCESS              0
#define PSCI_E_NOT_SUPPORTED        (-1)
#define PSCI_E_INVALID_PARAMETERS   (-2)
#define PSCI_E_DENIED               (-3)
#define PSCI_E_ALREADY_ON           (-4)
#define PSCI_E_ON_PENDING           (-5)
#define PSCI_E_INTERNAL_FAILURE     (-6)
#define PSCI_E_INVALID_ADDRESS      (-9)

/* AFFINITY_INFO results */
#define PSCI_STATE_ON               0
#define PSCI_STATE_OFF              1
#define PSCI_STATE_ON_PENDING       2

/* Before any secondary core runs: the boot core is on, the others off. */
void wolfBoot_psci_init(unsigned int boot_cpu);

/* One SMC from a lower exception level, x0-x3 as received; the result goes
 * back in x0. */
uint64_t wolfBoot_psci_smc(uint64_t fid, uint64_t a1, uint64_t a2,
    uint64_t a3);

/* Called by a parked core. Returns 1 with the entry point and context id
 * once CPU_ON has been issued for it, 0 while it must keep waiting. */
int wolfBoot_psci_cpu_take(unsigned int cpu, uint64_t* entry,
    uint64_t* context);

/* Non-zero once the core has parked for the first time. */
int wolfBoot_psci_cpu_alive(unsigned int cpu);

/* Platform hooks. All are called at EL3 on the core that issued the SMC. */

/* Linear core index for an MPIDR as passed to CPU_ON, negative if it does
 * not name a core. */
int hal_psci_mpidr_to_cpu(uint64_t mpidr);
unsigned int hal_psci_this_cpu(void);
/* Non-zero if a lower exception level may be entered at this address. */
int hal_psci_entry_ok(uint64_t entry);
/* Wake a parked core so that it calls wolfBoot_psci_cpu_take() again. */
void hal_psci_cpu_wake(unsigned int cpu);
/* Park the calling core. Does not return on success. */
void hal_psci_cpu_off(void);
/* Neither returns on success. */
void hal_psci_system_reset(void);
void hal_psci_system_off(void);

#endif /* PSCI_H */
