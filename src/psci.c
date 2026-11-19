/* psci.c
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

/* Minimal PSCI 1.1 runtime for a wolfBoot that stays resident at EL3 after
 * the handoff: core power on and off, affinity state, system reset and off.
 * The platform HAL owns the SMC vector and calls wolfBoot_psci_smc(). It
 * runs on the caller's EL3 stack with the caller's FP/SIMD state live, so
 * this file must be built without FP/SIMD register use. */

#include <stddef.h>
#include <stdint.h>
#include "psci.h"

struct psci_cpu {
    uint64_t entry;
    uint64_t context;
    volatile uint8_t state;
    volatile uint8_t alive;
};

static struct psci_cpu psci_cpus[WOLFBOOT_PSCI_NUM_CPUS];
static volatile unsigned char psci_lock;

static void psci_acquire(void)
{
    while (__atomic_test_and_set(&psci_lock, __ATOMIC_ACQUIRE)) {
        /* wait on a plain read, so that waiters do not take the line from
         * the holder with a store per attempt */
        while (psci_lock) {
        }
    }
}

static void psci_release(void)
{
    __atomic_clear(&psci_lock, __ATOMIC_RELEASE);
}

void wolfBoot_psci_init(unsigned int boot_cpu)
{
    unsigned int i;

    for (i = 0; i < WOLFBOOT_PSCI_NUM_CPUS; i++) {
        psci_cpus[i].entry = 0;
        psci_cpus[i].context = 0;
        psci_cpus[i].state = PSCI_STATE_OFF;
        psci_cpus[i].alive = 0;
    }
    if (boot_cpu < WOLFBOOT_PSCI_NUM_CPUS) {
        psci_cpus[boot_cpu].state = PSCI_STATE_ON;
        psci_cpus[boot_cpu].alive = 1;
    }
    psci_lock = 0;
}

int wolfBoot_psci_cpu_take(unsigned int cpu, uint64_t* entry,
    uint64_t* context)
{
    int ret = 0;

    if (cpu >= WOLFBOOT_PSCI_NUM_CPUS || entry == NULL || context == NULL)
        return 0;
    psci_cpus[cpu].alive = 1;
    /* Unlocked first look: the lock's exclusive accesses would wake this
     * core out of its own wait-for-event. */
    if (psci_cpus[cpu].state != PSCI_STATE_ON_PENDING)
        return 0;
    psci_acquire();
    if (psci_cpus[cpu].state == PSCI_STATE_ON_PENDING) {
        psci_cpus[cpu].state = PSCI_STATE_ON;
        *entry = psci_cpus[cpu].entry;
        *context = psci_cpus[cpu].context;
        ret = 1;
    }
    psci_release();
    return ret;
}

int wolfBoot_psci_cpu_alive(unsigned int cpu)
{
    if (cpu >= WOLFBOOT_PSCI_NUM_CPUS)
        return 0;
    return psci_cpus[cpu].alive;
}

static int psci_cpu_on(uint64_t mpidr, uint64_t entry, uint64_t context)
{
    int cpu = hal_psci_mpidr_to_cpu(mpidr);
    int ret;

    if (cpu < 0 || cpu >= WOLFBOOT_PSCI_NUM_CPUS)
        return PSCI_E_INVALID_PARAMETERS;
    if (!hal_psci_entry_ok(entry))
        return PSCI_E_INVALID_ADDRESS;
    psci_acquire();
    if (psci_cpus[cpu].state == PSCI_STATE_ON) {
        ret = PSCI_E_ALREADY_ON;
    }
    else if (psci_cpus[cpu].state == PSCI_STATE_ON_PENDING) {
        ret = PSCI_E_ON_PENDING;
    }
    else if (!psci_cpus[cpu].alive) {
        /* never reached its parking loop: nothing would answer the wake */
        ret = PSCI_E_INTERNAL_FAILURE;
    }
    else {
        psci_cpus[cpu].entry = entry;
        psci_cpus[cpu].context = context;
        psci_cpus[cpu].state = PSCI_STATE_ON_PENDING;
        ret = PSCI_E_SUCCESS;
    }
    psci_release();
    if (ret == PSCI_E_SUCCESS)
        hal_psci_cpu_wake((unsigned int)cpu);
    return ret;
}

static int psci_cpu_off(void)
{
    unsigned int cpu = hal_psci_this_cpu();

    if (cpu >= WOLFBOOT_PSCI_NUM_CPUS)
        return PSCI_E_DENIED;
    psci_acquire();
    psci_cpus[cpu].state = PSCI_STATE_OFF;
    psci_release();
    hal_psci_cpu_off();
    /* still running: the core could not be parked */
    psci_acquire();
    psci_cpus[cpu].state = PSCI_STATE_ON;
    psci_release();
    return PSCI_E_DENIED;
}

static int psci_affinity_info(uint64_t mpidr, uint64_t level)
{
    int cpu = hal_psci_mpidr_to_cpu(mpidr);
    int ret;

    if (level != 0 || cpu < 0 || cpu >= WOLFBOOT_PSCI_NUM_CPUS)
        return PSCI_E_INVALID_PARAMETERS;
    psci_acquire();
    ret = psci_cpus[cpu].state;
    psci_release();
    return ret;
}

static int psci_features(uint64_t fid)
{
    switch (fid) {
        case PSCI_VERSION:
        case PSCI_CPU_OFF:
        case PSCI_CPU_ON:
        case PSCI_CPU_ON | SMCCC_SMC64:
        case PSCI_AFFINITY_INFO:
        case PSCI_AFFINITY_INFO | SMCCC_SMC64:
        case PSCI_MIGRATE_INFO_TYPE:
        case PSCI_SYSTEM_OFF:
        case PSCI_SYSTEM_RESET:
        case PSCI_FEATURES:
        case SMCCC_VERSION:
            return PSCI_E_SUCCESS;
        default:
            return PSCI_E_NOT_SUPPORTED;
    }
}

uint64_t wolfBoot_psci_smc(uint64_t fid, uint64_t a1, uint64_t a2,
    uint64_t a3)
{
    int ret;

    /* w0 carries the function id; an SMC32 call passes 32-bit arguments */
    fid &= 0xFFFFFFFFUL;
    if ((fid & SMCCC_SMC64) == 0) {
        a1 &= 0xFFFFFFFFUL;
        a2 &= 0xFFFFFFFFUL;
        a3 &= 0xFFFFFFFFUL;
    }
    switch (fid) {
        case SMCCC_VERSION:
            return SMCCC_VERSION_1_1;
        case PSCI_VERSION:
            return PSCI_VERSION_1_1;
        case PSCI_FEATURES:
            ret = psci_features(a1);
            break;
        case PSCI_CPU_ON:
        case PSCI_CPU_ON | SMCCC_SMC64:
            ret = psci_cpu_on(a1, a2, a3);
            break;
        case PSCI_CPU_OFF:
            ret = psci_cpu_off();
            break;
        case PSCI_AFFINITY_INFO:
        case PSCI_AFFINITY_INFO | SMCCC_SMC64:
            ret = psci_affinity_info(a1, a2);
            break;
        case PSCI_MIGRATE_INFO_TYPE:
            ret = PSCI_TOS_NOT_PRESENT;
            break;
        case PSCI_SYSTEM_RESET:
            hal_psci_system_reset();
            ret = PSCI_E_INTERNAL_FAILURE;
            break;
        case PSCI_SYSTEM_OFF:
            hal_psci_system_off();
            ret = PSCI_E_INTERNAL_FAILURE;
            break;
        default:
            /* also the SMCCC "unknown function" value */
            ret = PSCI_E_NOT_SUPPORTED;
            break;
    }
    return (uint64_t)(int64_t)ret;
}
