/* acpi.c
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
 *
 *
 * Minimal ACPI table generation for the x86 FSP (Tiger Lake) target. The FSP
 * does silicon init only and returns no usable ACPI table set, so wolfBoot
 * builds RSDP -> XSDT -> { FADT, MADT, MCFG, DSDT } in low reserved memory and
 * points boot_params.acpi_rsdp_addr at the RSDP. The MADT interrupt model and
 * the DSDT _PRT routing are the values measured on this board; see docs and
 * acpi_dsdt.asl. Only the DSDT needs AML, embedded here as a precompiled blob.
 */

#include <stdint.h>
#include <string.h>

#include <x86/acpi.h>
#include <x86/acpi_dsdt.h>

/* Local APIC and IOAPIC as the MP table already encodes them (mptable.c). */
#define ACPI_LAPIC_ADDR   0xFEE00000UL
#define ACPI_IOAPIC_ADDR  0xFEC00000UL
#define ACPI_IOAPIC_ID    0x02
#define ACPI_CPU_COUNT    4

/* ECAM base and bus range, matching MCFG to config PCI_ECAM_BASE. */
#ifndef PCI_ECAM_BASE
#define PCI_ECAM_BASE 0xC0000000UL
#endif
#define ACPI_PCI_END_BUS  0x17

/* MADT subtable types. */
#define MADT_TYPE_LAPIC     0
#define MADT_TYPE_IOAPIC    1
#define MADT_TYPE_ISO       2
#define MADT_TYPE_LAPIC_NMI 4

#define MADT_LAPIC_ENABLED  1u
#define MADT_FLAG_PCAT_COMPAT 1u

/* ACPI Generic Address Structure space ids. */
#define ACPI_GAS_SYSTEM_MEMORY 0
#define ACPI_GAS_SYSTEM_IO     1

/* FADT: HW-reduced ACPI (no PM/SCI block; see acpi_build_fadt), a reset
 * register for reboot, and the legacy-devices boot-architecture flag.
 * RESET_REG_SUP advertises the reset register as usable; without it the OS
 * ignores it. */
#define FADT_HW_REDUCED_ACPI    (1u << 20)
#define FADT_RESET_REG_SUP      (1u << 10)
#define ACPI_RESET_PORT     0xCF9
/* CF9 cold reset: SYS_RST (bit 2) with full/CPU reset, as src/x86/common.c
 * writes. A value with bit 2 clear does not trigger a reset. */
#define ACPI_RESET_VAL      0x0E
#define ACPI_FADT_IAPC_BOOT     0x0001

struct acpi_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     creator_id[4];
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdp {
    char     signature[8];      /* "RSD PTR " */
    uint8_t  checksum;          /* over the first 20 bytes */
    char     oem_id[6];
    uint8_t  revision;          /* 2 */
    uint32_t rsdt_address;      /* 0 - XSDT is used */
    uint32_t length;            /* full table length */
    uint64_t xsdt_address;
    uint8_t  ext_checksum;      /* over the full length */
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_gas {               /* Generic Address Structure */
    uint8_t  space_id;
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_header header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed));

struct madt_lapic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed));

struct madt_ioapic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    uint8_t  type;
    uint8_t  length;
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct madt_lapic_nmi {
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_processor_id;
    uint16_t flags;
    uint8_t  lint;
} __attribute__((packed));

struct acpi_mcfg_alloc {
    uint64_t base_address;
    uint16_t segment;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} __attribute__((packed));

struct acpi_mcfg {
    struct acpi_header header;
    uint64_t reserved;
    struct acpi_mcfg_alloc alloc;
} __attribute__((packed));

/* Firmware ACPI Control Structure. No checksum field; mostly zero for a
 * platform that does not implement S3 firmware resume. */
/* HW-reduced ACPI has no FACS: there is no firmware sleep-control block, so
 * the FADT leaves firmware_ctrl / x_firmware_ctrl zero and none is built. */

/* ACPI 6.x FADT (fixed layout to the hypervisor id, 276 bytes). */
struct acpi_fadt {
    struct acpi_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved1;
    uint32_t flags;
    struct acpi_gas reset_reg;
    uint8_t  reset_value;
    uint16_t arm_boot_arch;
    uint8_t  fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk;
    struct acpi_gas x_pm1b_cnt_blk;
    struct acpi_gas x_pm2_cnt_blk;
    struct acpi_gas x_pm_tmr_blk;
    struct acpi_gas x_gpe0_blk;
    struct acpi_gas x_gpe1_blk;
    struct acpi_gas sleep_control_reg;
    struct acpi_gas sleep_status_reg;
    uint64_t hypervisor_vendor_id;
} __attribute__((packed));

/* acpi_processor_id -> Local APIC id. wolfBoot FSP-S brings up 4 cores with
 * HT off (apic ids 0/2/4/6); listing the golden bank's 8 HT threads strands
 * the kernel for 10s per absent AP. */
static const uint8_t acpi_lapic_ids[ACPI_CPU_COUNT] = {
    0, 2, 4, 6
};

static void acpi_set_checksum(struct acpi_header *h)
{
    uint8_t *bytes = (uint8_t *)h;
    uint8_t sum = 0;
    uint32_t i;

    h->checksum = 0;
    for (i = 0; i < h->length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    h->checksum = (uint8_t)(0 - sum);
}

static void acpi_fill_header(struct acpi_header *h, const char *sig,
                             uint32_t length, uint8_t revision)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->signature, sig, 4);
    h->length = length;
    h->revision = revision;
    memcpy(h->oem_id, "WOLFBT", 6);
    memcpy(h->oem_table_id, "NAITGL  ", 8);
    h->oem_revision = 1;
    memcpy(h->creator_id, "WBT ", 4);
    h->creator_revision = 1;
}

/* Round p up to a 16-byte boundary so each table starts aligned. */
static uint8_t *acpi_align(uint8_t *p)
{
    uintptr_t v = (uintptr_t)p;
    v = (v + 0xF) & ~(uintptr_t)0xF;
    return (uint8_t *)v;
}

static uint8_t *acpi_build_madt(uint8_t *p)
{
    struct acpi_madt *madt = (struct acpi_madt *)p;
    uint8_t *e = p + sizeof(*madt);
    struct madt_lapic *lapic;
    struct madt_ioapic *ioapic;
    struct madt_iso *iso;
    struct madt_lapic_nmi *nmi;
    uint32_t i;

    madt->lapic_address = (uint32_t)ACPI_LAPIC_ADDR;
    madt->flags = MADT_FLAG_PCAT_COMPAT;

    for (i = 0; i < ACPI_CPU_COUNT; i++) {
        lapic = (struct madt_lapic *)e;
        memset(lapic, 0, sizeof(*lapic));
        lapic->type = MADT_TYPE_LAPIC;
        lapic->length = sizeof(*lapic);
        lapic->acpi_processor_id = (uint8_t)i;
        lapic->apic_id = acpi_lapic_ids[i];
        lapic->flags = MADT_LAPIC_ENABLED;
        e += sizeof(*lapic);
    }

    ioapic = (struct madt_ioapic *)e;
    memset(ioapic, 0, sizeof(*ioapic));
    ioapic->type = MADT_TYPE_IOAPIC;
    ioapic->length = sizeof(*ioapic);
    ioapic->ioapic_id = ACPI_IOAPIC_ID;
    ioapic->ioapic_address = (uint32_t)ACPI_IOAPIC_ADDR;
    ioapic->gsi_base = 0;
    e += sizeof(*ioapic);

    /* ISA IRQ0 -> GSI2, polarity/trigger conform. */
    iso = (struct madt_iso *)e;
    memset(iso, 0, sizeof(*iso));
    iso->type = MADT_TYPE_ISO;
    iso->length = sizeof(*iso);
    iso->bus = 0;
    iso->source = 0;
    iso->gsi = 2;
    iso->flags = 0;
    e += sizeof(*iso);

    /* SCI IRQ9 -> GSI9, active-high level. */
    iso = (struct madt_iso *)e;
    memset(iso, 0, sizeof(*iso));
    iso->type = MADT_TYPE_ISO;
    iso->length = sizeof(*iso);
    iso->bus = 0;
    iso->source = 9;
    iso->gsi = 9;
    iso->flags = 0x000D; /* active high (01) | level (11 << 2) */
    e += sizeof(*iso);

    /* Local APIC NMI on LINT1 for all processors, active-high edge. */
    nmi = (struct madt_lapic_nmi *)e;
    memset(nmi, 0, sizeof(*nmi));
    nmi->type = MADT_TYPE_LAPIC_NMI;
    nmi->length = sizeof(*nmi);
    nmi->acpi_processor_id = 0xFF;
    nmi->flags = 0x0005; /* active high (01) | edge (01 << 2) */
    nmi->lint = 1;
    e += sizeof(*nmi);

    acpi_fill_header(&madt->header, "APIC", (uint32_t)(e - p), 4);
    acpi_set_checksum(&madt->header);
    return e;
}

static uint8_t *acpi_build_mcfg(uint8_t *p)
{
    struct acpi_mcfg *mcfg = (struct acpi_mcfg *)p;

    memset(mcfg, 0, sizeof(*mcfg));
    mcfg->alloc.base_address = (uint64_t)PCI_ECAM_BASE;
    mcfg->alloc.segment = 0;
    mcfg->alloc.start_bus = 0;
    mcfg->alloc.end_bus = ACPI_PCI_END_BUS;
    acpi_fill_header(&mcfg->header, "MCFG", sizeof(*mcfg), 1);
    acpi_set_checksum(&mcfg->header);
    return p + sizeof(*mcfg);
}

static void acpi_gas_io(struct acpi_gas *g, uint8_t width, uint64_t addr)
{
    g->space_id = ACPI_GAS_SYSTEM_IO;
    g->bit_width = width;
    g->bit_offset = 0;
    g->access_size = 0;
    g->address = addr;
}

static uint8_t *acpi_build_fadt(uint8_t *p, uint64_t facs_addr,
                                uint64_t dsdt_addr)
{
    struct acpi_fadt *fadt = (struct acpi_fadt *)p;

    (void)facs_addr; /* no FACS: HW-reduced ACPI has no firmware sleep control */

    /* HW-reduced ACPI. wolfBoot has no SMM handler to service the legacy
     * enable sequence (writing acpi_enable to smi_cmd and waiting for SCI_EN),
     * so advertising the PM/SCI block would make the OS spin ~40s on
     * "hardware did not enter ACPI mode" and then disable the AML interpreter,
     * losing the DSDT _PRT. HW-reduced tells the OS ACPI is always enabled;
     * the interrupt model comes from the MADT and _PRT from the DSDT either
     * way. Only the DSDT pointer and a reset register are needed. The
     * SLEEP_CONTROL/STATUS registers are left zero, so only reset/reboot is
     * provided; ACPI S5 soft-off is intentionally not supported. */
    memset(fadt, 0, sizeof(*fadt));
    fadt->dsdt = (uint32_t)dsdt_addr;
    fadt->x_dsdt = dsdt_addr;
    fadt->preferred_pm_profile = 2; /* Mobile */
    fadt->iapc_boot_arch = ACPI_FADT_IAPC_BOOT;
    fadt->flags = FADT_HW_REDUCED_ACPI | FADT_RESET_REG_SUP;
    acpi_gas_io(&fadt->reset_reg, 8, ACPI_RESET_PORT);
    fadt->reset_value = ACPI_RESET_VAL;
    fadt->fadt_minor_version = 3;   /* ACPI 6.3 */
    acpi_fill_header(&fadt->header, "FACP", sizeof(*fadt), 6);
    acpi_set_checksum(&fadt->header);
    return p + sizeof(*fadt);
}

uint64_t acpi_setup(void)
{
    uint8_t *base = (uint8_t *)(uintptr_t)ACPI_TABLE_BASE;
    uint8_t *p = base;
    uint8_t *dsdt;
    uint8_t *fadt;
    uint8_t *madt;
    uint8_t *mcfg;
    struct acpi_header *xsdt;
    struct acpi_rsdp *rsdp;
    uint64_t *xsdt_entries;
    uint8_t *bytes;
    uint8_t sum;
    uint32_t i;

    /* DSDT first, so the FADT can point at its address. */
    dsdt = p;
    memcpy(dsdt, acpi_dsdt_aml, acpi_dsdt_aml_len);
    p = acpi_align(dsdt + acpi_dsdt_aml_len);

    fadt = p;
    p = acpi_align(acpi_build_fadt(p, 0, (uint64_t)(uintptr_t)dsdt));

    madt = p;
    p = acpi_align(acpi_build_madt(p));

    mcfg = p;
    p = acpi_align(acpi_build_mcfg(p));

    /* XSDT lists the three top-level tables (not the DSDT, reached via FADT). */
    xsdt = (struct acpi_header *)p;
    xsdt_entries = (uint64_t *)((uint8_t *)xsdt + sizeof(*xsdt));
    xsdt_entries[0] = (uint64_t)(uintptr_t)fadt;
    xsdt_entries[1] = (uint64_t)(uintptr_t)madt;
    xsdt_entries[2] = (uint64_t)(uintptr_t)mcfg;
    acpi_fill_header(xsdt, "XSDT",
                     (uint32_t)(sizeof(*xsdt) + 3 * sizeof(uint64_t)), 1);
    acpi_set_checksum(xsdt);
    p = acpi_align((uint8_t *)xsdt + xsdt->length);

    rsdp = (struct acpi_rsdp *)p;
    memset(rsdp, 0, sizeof(*rsdp));
    memcpy(rsdp->signature, "RSD PTR ", 8);
    memcpy(rsdp->oem_id, "WOLFBT", 6);
    rsdp->revision = 2;
    rsdp->rsdt_address = 0;
    rsdp->length = sizeof(*rsdp);
    rsdp->xsdt_address = (uint64_t)(uintptr_t)xsdt;

    /* v1 checksum over the first 20 bytes. */
    bytes = (uint8_t *)rsdp;
    sum = 0;
    for (i = 0; i < 20; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    rsdp->checksum = (uint8_t)(0 - sum);

    /* Extended checksum over the whole structure. */
    sum = 0;
    for (i = 0; i < sizeof(*rsdp); i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    rsdp->ext_checksum = (uint8_t)(0 - sum);

    return (uint64_t)(uintptr_t)rsdp;
}
