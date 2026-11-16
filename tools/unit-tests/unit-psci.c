/* unit-psci.c
 *
 * Unit tests for the EL3 PSCI runtime (src/psci.c).
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

#include <check.h>
#include <stdint.h>

#include "psci.c"

#define RAM_BASE    0x80000000ULL
#define RAM_SIZE    0x80000000ULL
#define ENTRY       (RAM_BASE + 0x80000)
#define ERR(e)      ((uint64_t)(int64_t)(e))

/* Platform side: two clusters of two cores, as on the S32G2. */
static unsigned int this_cpu;
static int woken_cpu;
static int reset_calls;
static int off_calls;
static uint64_t state_in_cpu_off;

int hal_psci_mpidr_to_cpu(uint64_t mpidr)
{
    if ((mpidr & ~0x101ULL) != 0)
        return -1;
    return (int)(((mpidr >> 8) & 1) * 2 + (mpidr & 1));
}

unsigned int hal_psci_this_cpu(void)
{
    return this_cpu;
}

int hal_psci_entry_ok(uint64_t entry)
{
    return ((entry & 3) == 0 && entry >= RAM_BASE &&
        entry - RAM_BASE < RAM_SIZE);
}

void hal_psci_cpu_wake(unsigned int cpu)
{
    woken_cpu = (int)cpu;
}

/* The real hook does not return; this one records what the other cores
 * would see while the core parks. */
void hal_psci_cpu_off(void)
{
    uint64_t mpidr = ((uint64_t)(this_cpu >> 1) << 8) | (this_cpu & 1);

    state_in_cpu_off = wolfBoot_psci_smc(PSCI_AFFINITY_INFO | SMCCC_SMC64,
        mpidr, 0, 0);
}

void hal_psci_system_reset(void)
{
    reset_calls++;
}

void hal_psci_system_off(void)
{
    off_calls++;
}

static void setup(void)
{
    this_cpu = 0;
    woken_cpu = -1;
    reset_calls = 0;
    off_calls = 0;
    state_in_cpu_off = ~0ULL;
    wolfBoot_psci_init(0);
}

/* Park a core as its startup code does, so that CPU_ON can reach it. */
static void park(unsigned int cpu)
{
    uint64_t entry, context;

    ck_assert_int_eq(wolfBoot_psci_cpu_take(cpu, &entry, &context), 0);
}

START_TEST(test_version_and_features)
{
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_VERSION, 0, 0, 0),
        PSCI_VERSION_1_1);
    ck_assert_uint_eq(wolfBoot_psci_smc(SMCCC_VERSION, 0, 0, 0),
        SMCCC_VERSION_1_1);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_MIGRATE_INFO_TYPE, 0, 0, 0),
        PSCI_TOS_NOT_PRESENT);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_FEATURES,
        PSCI_CPU_ON | SMCCC_SMC64, 0, 0), PSCI_E_SUCCESS);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_FEATURES, PSCI_SYSTEM_RESET,
        0, 0), PSCI_E_SUCCESS);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_FEATURES, SMCCC_VERSION, 0, 0),
        PSCI_E_SUCCESS);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_FEATURES, PSCI_CPU_SUSPEND,
        0, 0), ERR(PSCI_E_NOT_SUPPORTED));
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_FEATURES, SMCCC_ARCH_FEATURES,
        0, 0), ERR(PSCI_E_NOT_SUPPORTED));
}
END_TEST

START_TEST(test_unknown_calls)
{
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_CPU_SUSPEND, 0, ENTRY, 0),
        ERR(PSCI_E_NOT_SUPPORTED));
    ck_assert_uint_eq(wolfBoot_psci_smc(SMCCC_ARCH_FEATURES, 0x80008000UL,
        0, 0), ERR(PSCI_E_NOT_SUPPORTED));
    /* a yielding call, and another owner's range */
    ck_assert_uint_eq(wolfBoot_psci_smc(0x04000003UL, 1, ENTRY, 0),
        ERR(PSCI_E_NOT_SUPPORTED));
    ck_assert_uint_eq(wolfBoot_psci_smc(0xC2000001UL, 0, 0, 0),
        ERR(PSCI_E_NOT_SUPPORTED));
    /* only w0 is the function id */
    ck_assert_uint_eq(wolfBoot_psci_smc(0xFFFFFFFF00000000ULL | PSCI_VERSION,
        0, 0, 0), PSCI_VERSION_1_1);
    ck_assert_int_eq(woken_cpu, -1);
}
END_TEST

START_TEST(test_cpu_on_arguments)
{
    uint64_t on = PSCI_CPU_ON | SMCCC_SMC64;

    park(1);
    /* not a core: a third core in a cluster, a third cluster, aff2, aff3 */
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x2, ENTRY, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x200, ENTRY, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x10001, ENTRY, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x100000001ULL, ENTRY, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
    /* entry points outside RAM, in the firmware's own memory, unaligned */
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 1, 0, 0),
        ERR(PSCI_E_INVALID_ADDRESS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 1, 0x340A0000, 0),
        ERR(PSCI_E_INVALID_ADDRESS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 1, RAM_BASE + RAM_SIZE, 0),
        ERR(PSCI_E_INVALID_ADDRESS));
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 1, ENTRY + 2, 0),
        ERR(PSCI_E_INVALID_ADDRESS));
    ck_assert_int_eq(woken_cpu, -1);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 1, 0, 0),
        PSCI_STATE_OFF);
}
END_TEST

START_TEST(test_cpu_on_states)
{
    uint64_t on = PSCI_CPU_ON | SMCCC_SMC64;
    uint64_t entry = 0, context = 0;

    /* the calling core */
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0, ENTRY, 0),
        ERR(PSCI_E_ALREADY_ON));
    /* a core that never reached its parking loop */
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x100, ENTRY, 0),
        ERR(PSCI_E_INTERNAL_FAILURE));
    ck_assert_int_eq(wolfBoot_psci_cpu_alive(2), 0);
    ck_assert_int_eq(woken_cpu, -1);

    park(2);
    ck_assert_int_eq(wolfBoot_psci_cpu_alive(2), 1);
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x100, ENTRY, 0x1234),
        PSCI_E_SUCCESS);
    ck_assert_int_eq(woken_cpu, 2);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO | SMCCC_SMC64,
        0x100, 0, 0), PSCI_STATE_ON_PENDING);
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x100, ENTRY + 4, 0),
        ERR(PSCI_E_ON_PENDING));
    /* the other cores are not affected */
    ck_assert_int_eq(wolfBoot_psci_cpu_take(3, &entry, &context), 0);

    ck_assert_int_eq(wolfBoot_psci_cpu_take(2, &entry, &context), 1);
    ck_assert_uint_eq(entry, ENTRY);
    ck_assert_uint_eq(context, 0x1234);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0x100, 0, 0),
        PSCI_STATE_ON);
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x100, ENTRY, 0),
        ERR(PSCI_E_ALREADY_ON));
    /* taken once */
    ck_assert_int_eq(wolfBoot_psci_cpu_take(2, &entry, &context), 0);
}
END_TEST

START_TEST(test_cpu_on_smc32)
{
    uint64_t entry = 0, context = 0;

    park(1);
    /* an SMC32 call passes 32-bit arguments: the upper halves are not part
     * of them */
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_CPU_ON,
        0xAAAAAAAA00000001ULL, 0xBBBBBBBB00000000ULL | ENTRY,
        0xCCCCCCCC00000042ULL), PSCI_E_SUCCESS);
    ck_assert_int_eq(wolfBoot_psci_cpu_take(1, &entry, &context), 1);
    ck_assert_uint_eq(entry, ENTRY);
    ck_assert_uint_eq(context, 0x42);
    /* the SMC64 form takes them whole */
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_CPU_ON | SMCCC_SMC64,
        0xAAAAAAAA00000001ULL, ENTRY, 0), ERR(PSCI_E_INVALID_PARAMETERS));
}
END_TEST

START_TEST(test_affinity_info)
{
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0, 0, 0),
        PSCI_STATE_ON);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0x101, 0, 0),
        PSCI_STATE_OFF);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0, 1, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0x102, 0, 0),
        ERR(PSCI_E_INVALID_PARAMETERS));
}
END_TEST

START_TEST(test_cpu_off_and_on_again)
{
    uint64_t on = PSCI_CPU_ON | SMCCC_SMC64;
    uint64_t entry = 0, context = 0;

    park(3);
    ck_assert_uint_eq(wolfBoot_psci_smc(on, 0x101, ENTRY, 7), PSCI_E_SUCCESS);
    ck_assert_int_eq(wolfBoot_psci_cpu_take(3, &entry, &context), 1);

    /* The core is reported off from the moment it starts to park. The mock
     * hook returns, which the runtime treats as a failed power down. */
    this_cpu = 3;
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_CPU_OFF, 0, 0, 0),
        ERR(PSCI_E_DENIED));
    ck_assert_uint_eq(state_in_cpu_off, PSCI_STATE_OFF);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_AFFINITY_INFO, 0x101, 0, 0),
        PSCI_STATE_ON);

    /* a core id the platform should never report */
    this_cpu = WOLFBOOT_PSCI_NUM_CPUS;
    state_in_cpu_off = ~0ULL;
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_CPU_OFF, 0, 0, 0),
        ERR(PSCI_E_DENIED));
    ck_assert_uint_eq(state_in_cpu_off, ~0ULL);
}
END_TEST

START_TEST(test_system_calls)
{
    /* neither hook returns on hardware */
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_SYSTEM_RESET, 0, 0, 0),
        ERR(PSCI_E_INTERNAL_FAILURE));
    ck_assert_int_eq(reset_calls, 1);
    ck_assert_int_eq(off_calls, 0);
    ck_assert_uint_eq(wolfBoot_psci_smc(PSCI_SYSTEM_OFF, 0, 0, 0),
        ERR(PSCI_E_INTERNAL_FAILURE));
    ck_assert_int_eq(off_calls, 1);
}
END_TEST

START_TEST(test_take_arguments)
{
    uint64_t entry = 0, context = 0;

    ck_assert_int_eq(wolfBoot_psci_cpu_take(WOLFBOOT_PSCI_NUM_CPUS, &entry,
        &context), 0);
    ck_assert_int_eq(wolfBoot_psci_cpu_take(1, NULL, &context), 0);
    ck_assert_int_eq(wolfBoot_psci_cpu_take(1, &entry, NULL), 0);
    ck_assert_int_eq(wolfBoot_psci_cpu_alive(1), 0);
    ck_assert_int_eq(wolfBoot_psci_cpu_alive(WOLFBOOT_PSCI_NUM_CPUS), 0);
    ck_assert_int_eq(wolfBoot_psci_cpu_alive(0), 1);
}
END_TEST

static Suite* psci_suite(void)
{
    Suite* s = suite_create("psci");
    TCase* tc = tcase_create("psci");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_version_and_features);
    tcase_add_test(tc, test_unknown_calls);
    tcase_add_test(tc, test_cpu_on_arguments);
    tcase_add_test(tc, test_cpu_on_states);
    tcase_add_test(tc, test_cpu_on_smc32);
    tcase_add_test(tc, test_affinity_info);
    tcase_add_test(tc, test_cpu_off_and_on_again);
    tcase_add_test(tc, test_system_calls);
    tcase_add_test(tc, test_take_arguments);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite* s = psci_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
