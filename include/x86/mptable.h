/* mptable.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
 *
 */
#ifndef MPTABLE_H
#define MPTABLE_H

#include <stdint.h>

/* Intel MP Floating Pointer Structure */
struct mp_float {
    char signature[4];          /* _MP_ */
    uint32_t phy_addr;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature1;
    uint8_t feature2;
    uint8_t feature3;
    uint8_t feature4;
    uint8_t feature5;
} __attribute__((packed));
#define MPC_SIGNATURE "PCMP"

#include <stdint.h>

struct mp_conf_table_header {
    char signature[4];
    uint16_t base_table_len;
    char spec;
    char checksum;
    char oem_id_string[8];
    char product_id_string[12];
    uint32_t oem_table_ptr;
    uint16_t oem_table_size;
    uint16_t oem_entry_count;
    uint32_t lapic;
    uint32_t _res;
}__attribute__((packed));

/* Followed by entries */
#define     MP_PROCESSOR        0
#define     MP_BUS          1
#define     MP_IOAPIC       2
#define     MP_INTSRC       3
#define     MP_LINTSRC      4
/* Used by IBM NUMA-Q to describe node locality */
#define     MP_TRANSLATION      192

#define CPU_ENABLED         1   /* Processor is available */
#define CPU_BOOTPROCESSOR   2   /* Processor is the boot CPU */

#define CPU_STEPPING_MASK   0x000F
#define CPU_MODEL_MASK      0x00F0
#define CPU_FAMILY_MASK         0x0F00

#define MPC_APIC_USABLE         0x01

#include <stdint.h>

struct mp_conf_entry_processor {
    uint8_t entry_type;
    uint8_t apic_id;
    uint8_t apic_ver;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t _res[2];
} __attribute__((packed));

struct mp_conf_entry_ioapic {
    uint8_t entry_type;
    uint8_t apic_id;
    uint8_t apic_ver;
    uint8_t flags;
    uint32_t apic_addr;
} __attribute__((packed));

struct mp_conf_entry_bus {
    uint8_t entry_type;
    uint8_t bus_id;
    char bus_type_string[6];
} __attribute__((packed));

struct mp_conf_entry_interrupt {
    uint8_t entry_type;
    uint8_t int_type;
    uint16_t int_flag;
    uint8_t src_bus_id;
    uint8_t src_bus_irq;
    uint8_t dst_apic_id;
    uint8_t dst_apic_irq;
} __attribute__((packed));

struct mp_conf_entry_local_interrupt {
    uint8_t entry_type;
    uint8_t int_type;
    uint16_t int_flag;
    uint8_t src_bus_id;
    uint8_t src_bus_irq;
    uint8_t dst_apic_id;
    uint8_t dst_apic_lintin;
} __attribute__((packed));

enum mp_irq_source_types {
    mp_INT = 0,
    mp_NMI = 1,
    mp_SMI = 2,
    mp_ExtINT = 3
} __attribute__((packed));

#define MP_IRQPOL_DEFAULT   0x0
#define MP_IRQPOL_ACTIVE_HIGH   0x1
#define MP_IRQPOL_RESERVED  0x2
#define MP_IRQPOL_ACTIVE_LOW    0x3
#define MP_IRQPOL_MASK      0x3

#define MP_IRQTRIG_DEFAULT  0x0
#define MP_IRQTRIG_EDGE         0x4
#define MP_IRQTRIG_RESERVED     0x8
#define MP_IRQTRIG_LEVEL    0xc
#define MP_IRQTRIG_MASK         0xc

#define MP_APIC_ALL     0xFF

#define MPTABLE_LOAD_BASE 0x100

#ifdef TARGET_x86_fsp_qemu
    #define MP_IOAPIC_NUM_ENTRY 1
    #define MP_INTSRC_NUM_ENTRY 13
    #define MP_LINTSRC_NUM_ENTRY 2
    #define MP_BUS_NUM_ENTRY 2
    #define MP_CPU_NUM_ENTRY 1
#endif

#ifdef TARGET_kontron_vx3060_s2
    #define MP_IOAPIC_NUM_ENTRY 1
    #define MP_INTSRC_NUM_ENTRY 17
    #define MP_LINTSRC_NUM_ENTRY 0
    #define MP_BUS_NUM_ENTRY 2
    #define MP_CPU_NUM_ENTRY 4
#endif

struct mptable {
    struct mp_float mpf;
    struct mp_conf_table_header mpc_table;
    struct mp_conf_entry_processor mpce_processor[MP_CPU_NUM_ENTRY];
    struct mp_conf_entry_bus bus[MP_BUS_NUM_ENTRY];
    struct mp_conf_entry_ioapic ioapic[MP_IOAPIC_NUM_ENTRY];
    struct mp_conf_entry_interrupt intsrc[MP_INTSRC_NUM_ENTRY];
    struct mp_conf_entry_local_interrupt lintsrc[MP_LINTSRC_NUM_ENTRY];
} __attribute__((packed));


void mptable_setup(void);

#endif /* MPTABLE_H */
