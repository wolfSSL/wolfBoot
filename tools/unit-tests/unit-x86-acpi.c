/* unit-x86-acpi.c
 *
 * Unit tests for the x86 FSP ACPI table builders (src/x86/acpi.c): the MADT
 * CPU list, the TPM2, HPET and DMAR tables, their lengths and checksums,
 * with the PCI, MMIO and CPU topology reads mocked.
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
#include <string.h>

#include "../../src/x86/acpi.c"

static int g_cpus;
static int g_igd;
static uint32_t g_bdsm, g_tolud;
static uint8_t g_buf[4096] __attribute__((aligned(16)));

int x86_cpu_apic_ids(uint8_t *ids, int max)
{
    const uint8_t order[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
    int i;

    for (i = 0; i < g_cpus && i < max; i++)
        ids[i] = order[i];
    return i;
}

uint32_t mmio_read32(uintptr_t address)
{
    if (address == ACPI_HPET_BASE)
        return 0x8086a701U;
    if (address == ACPI_LAPIC_ADDR + 0x20)
        return 0x05000000U;
    return 0;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus;
    if (dev == 2 && fun == 0 && off == 0)
        return g_igd ? 0x9a498086U : 0xffffffffU;
    if (dev == 2 && fun == 0 && off == 0x5c)
        return g_bdsm | 0x1U;
    if (dev == 0 && fun == 0 && off == 0xbc)
        return g_tolud | 0x1U;
    return 0xffffffffU;
}

static void reset_mocks(void)
{
    memset(g_buf, 0xa5, sizeof(g_buf));
    g_cpus = 4;
    g_igd = 1;
    g_bdsm = 0x4c000000U;
    g_tolud = 0x50400000U;
}

static int table_ok(const uint8_t *t, const char *sig, uint32_t len)
{
    const struct acpi_header *h = (const struct acpi_header *)t;
    uint8_t sum = 0;
    uint32_t i;

    if (memcmp(h->signature, sig, 4) != 0 || h->length != len)
        return 0;
    for (i = 0; i < len; i++)
        sum = (uint8_t)(sum + t[i]);
    return sum == 0;
}

START_TEST(test_madt_lists_present_cpus)
{
    uint8_t *end;
    const struct madt_lapic *l;
    int i, n;

    for (n = 4; n <= 8; n += 4) {
        reset_mocks();
        g_cpus = n;
        end = acpi_build_madt(g_buf);
        ck_assert(table_ok(g_buf, "APIC", (uint32_t)(end - g_buf)));
        l = (const struct madt_lapic *)(g_buf + sizeof(struct acpi_madt));
        for (i = 0; i < n; i++) {
            ck_assert_uint_eq(l[i].type, MADT_TYPE_LAPIC);
            ck_assert_uint_eq(l[i].acpi_processor_id, i);
            ck_assert_uint_eq(l[i].flags, MADT_LAPIC_ENABLED);
        }
        ck_assert_uint_eq(l[3].apic_id, 6);
        /* the entry after the last CPU is the IOAPIC */
        ck_assert_uint_eq(l[n].type, MADT_TYPE_IOAPIC);
    }
    ck_assert_uint_eq(l[4].apic_id, 1);
}
END_TEST

START_TEST(test_madt_falls_back_to_the_bsp)
{
    const struct madt_lapic *l;

    reset_mocks();
    g_cpus = 0;
    acpi_build_madt(g_buf);
    l = (const struct madt_lapic *)(g_buf + sizeof(struct acpi_madt));
    ck_assert_uint_eq(l[0].apic_id, 5);
    ck_assert_uint_eq(l[1].type, MADT_TYPE_IOAPIC);
}
END_TEST

START_TEST(test_tpm2_and_hpet)
{
    const struct acpi_tpm2 *tpm2 = (const struct acpi_tpm2 *)g_buf;
    const struct acpi_hpet *hpet = (const struct acpi_hpet *)g_buf;

    reset_mocks();
    ck_assert_ptr_eq(acpi_build_tpm2(g_buf), g_buf + sizeof(*tpm2));
    ck_assert(table_ok(g_buf, "TPM2", sizeof(*tpm2)));
    ck_assert_uint_eq(tpm2->start_method, ACPI_TPM2_START_TIS);
    ck_assert_uint_eq(tpm2->control_address, 0);

    reset_mocks();
    ck_assert_ptr_eq(acpi_build_hpet(g_buf), g_buf + sizeof(*hpet));
    ck_assert(table_ok(g_buf, "HPET", sizeof(*hpet)));
    ck_assert_uint_eq(hpet->block_id, 0x8086a701U);
    ck_assert_uint_eq(hpet->base.address, ACPI_HPET_BASE);
}
END_TEST

START_TEST(test_dmar_with_igd)
{
    const struct dmar_drhd *gfx, *def;
    const struct dmar_rmrr *rmrr;
    const struct dmar_scope *sc;
    uint8_t *end;

    reset_mocks();
    end = acpi_build_dmar(g_buf);
    ck_assert(table_ok(g_buf, "DMAR", (uint32_t)(end - g_buf)));

    gfx = (const struct dmar_drhd *)(g_buf + sizeof(struct acpi_dmar));
    ck_assert_uint_eq(gfx->base, DMAR_GFX_UNIT);
    ck_assert_uint_eq(gfx->flags, 0);
    sc = (const struct dmar_scope *)(gfx + 1);
    ck_assert_uint_eq(sc->type, DMAR_SCOPE_ENDPOINT);
    ck_assert_uint_eq(sc->dev, 2);

    def = (const struct dmar_drhd *)((const uint8_t *)gfx + gfx->length);
    ck_assert_uint_eq(def->base, DMAR_DEFAULT_UNIT);
    ck_assert_uint_eq(def->flags, DMAR_INCLUDE_PCI_ALL);
    sc = (const struct dmar_scope *)(def + 1);
    ck_assert_uint_eq(sc[0].type, DMAR_SCOPE_IOAPIC);
    ck_assert_uint_eq(sc[0].enumeration_id, ACPI_IOAPIC_ID);
    ck_assert_uint_eq(sc[1].type, DMAR_SCOPE_HPET);

    rmrr = (const struct dmar_rmrr *)((const uint8_t *)def + def->length);
    ck_assert_uint_eq(rmrr->type, DMAR_RMRR);
    ck_assert_uint_eq(rmrr->base, 0x4c000000U);
    ck_assert_uint_eq(rmrr->limit, 0x503fffffU);
    ck_assert_ptr_eq((const uint8_t *)rmrr + rmrr->length, end);
}
END_TEST

START_TEST(test_dmar_without_igd)
{
    const struct dmar_drhd *def;
    uint8_t *end;

    reset_mocks();
    g_igd = 0;
    end = acpi_build_dmar(g_buf);
    ck_assert(table_ok(g_buf, "DMAR", (uint32_t)(end - g_buf)));
    def = (const struct dmar_drhd *)(g_buf + sizeof(struct acpi_dmar));
    ck_assert_uint_eq(def->base, DMAR_DEFAULT_UNIT);
    /* no graphics unit and no reserved region after the default unit */
    ck_assert_ptr_eq((const uint8_t *)def + def->length, end);
}
END_TEST

static Suite *acpi_suite(void)
{
    Suite *s = suite_create("x86-acpi");
    TCase *tc = tcase_create("x86-acpi");

    tcase_add_test(tc, test_madt_lists_present_cpus);
    tcase_add_test(tc, test_madt_falls_back_to_the_bsp);
    tcase_add_test(tc, test_tpm2_and_hpet);
    tcase_add_test(tc, test_dmar_with_igd);
    tcase_add_test(tc, test_dmar_without_igd);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = acpi_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
