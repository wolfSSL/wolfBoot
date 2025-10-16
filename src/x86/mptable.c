/* mptable.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include <x86/mptable.h>
#include <x86/common.h>
#include <stdint.h>
#include <string.h>

#define LOCAL_APIC_ID 0xfee00020
#define LOCAL_APIC_VER 0xfee00030

#ifdef TARGET_kontron_vx3060_s2
#define ISA_BUS 0x4

/* TGL mptable */
static struct mptable _mptable = {
    .mpf = {
        .signature = "_MP_",
        .phy_addr = (int)MPTABLE_LOAD_BASE + sizeof(struct mp_float),
        .length = 1,
        .spec_rev = 0x4,
        .checksum = 0x12,
        .feature1 = 0,
        .feature2 = 1 << 7,
        .feature3 = 0,
        .feature4 = 0,
        .feature5 = 0
    },
    .mpc_table = {
        .signature = MPC_SIGNATURE,
        .base_table_len = sizeof(struct mp_conf_table_header) +
            sizeof(struct mp_conf_entry_processor) * MP_CPU_NUM_ENTRY +
            sizeof(struct mp_conf_entry_bus) * MP_BUS_NUM_ENTRY +
            sizeof(struct mp_conf_entry_ioapic) * MP_IOAPIC_NUM_ENTRY +
            sizeof(struct mp_conf_entry_interrupt) * MP_INTSRC_NUM_ENTRY +
            sizeof(struct mp_conf_entry_local_interrupt) * MP_LINTSRC_NUM_ENTRY,
        .spec = 0x01,
        .checksum = 0x55,
        .oem_id_string = { 0 },
        .product_id_string = { 0 },
        .oem_table_ptr = 0,
        .oem_table_size = 0,
        .oem_entry_count = 0,
        .lapic = 0xfee00000,
        ._res= 0
    },
    .mpce_processor = {
        /* loaded by bios */
    },
    .bus = {
        {
            .entry_type = MP_BUS,
            .bus_id = 0,
            .bus_type_string = "PCI"
        },
        {
            .entry_type = MP_BUS,
            .bus_id = 1,
            .bus_type_string = "ISA"
        },
    },
    .ioapic = {
        {
            .entry_type = MP_IOAPIC,
            .apic_id = 0x2,
            .apic_ver = 0x20,
            .flags = MPC_APIC_USABLE,
            .apic_addr = 0xfec00000,
        }
    },
    .intsrc = {
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x0,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x2,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_ACTIVE_HIGH | MP_IRQTRIG_LEVEL,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x9,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x9,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x1,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x1,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x3,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x3,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x4,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x4,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x5,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x5,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x6,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x6,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x7,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x7,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x8,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x8,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0x9,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0x9,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xa,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xa,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xb,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xb,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xc,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xc,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xd,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xd,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xe,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xe,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_DEFAULT | MP_IRQTRIG_DEFAULT,
            .src_bus_id= 0x1,
            .src_bus_irq= 0xf,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 0xf,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_ACTIVE_LOW | MP_IRQTRIG_LEVEL,
            .src_bus_id= 0x0,
            .src_bus_irq= (30 << 2) | 0x0,
            .dst_apic_id = 0x2,
            .dst_apic_irq = 16,
        },
    },
};
#endif

#ifdef TARGET_x86_fsp_qemu
/* MPtables for qemu */
struct mptable _mptable = {
    .mpf = {
        .signature = "_MP_",
        .phy_addr = (int)MPTABLE_LOAD_BASE + sizeof(struct mp_float),
        .length = 1,
        .spec_rev = 1,
        .checksum = 0x12,
        .feature1 = 0,
        .feature2 = 1 << 7,
        .feature3 = 0,
        .feature4 = 0,
        .feature5 = 0
    },
    .mpc_table = {
        .signature = MPC_SIGNATURE,
        .base_table_len = sizeof(struct mp_conf_table_header) +
            sizeof(struct mp_conf_entry_processor) * MP_CPU_NUM_ENTRY +
            sizeof(struct mp_conf_entry_bus) * MP_BUS_NUM_ENTRY +
            sizeof(struct mp_conf_entry_ioapic) * MP_IOAPIC_NUM_ENTRY +
            sizeof(struct mp_conf_entry_interrupt) * MP_INTSRC_NUM_ENTRY +
            sizeof(struct mp_conf_entry_local_interrupt) * MP_LINTSRC_NUM_ENTRY,
        .spec = 0x01,
        .checksum = 0x55,
        .oem_id_string = { "wolfSSL" },
        .product_id_string = { "wolfBoot" },
        .oem_table_ptr = 0,
        .oem_table_size = 0,
        .oem_entry_count = 0,
        .lapic = 0xfee00000,
        ._res = 0
    },
    .mpce_processor = {
        {
            /* filled by bios */
        }
    },
    .bus = {
        {
            .entry_type = MP_BUS,
            .bus_id = 0,
            .bus_type_string = "PCI"
        },
        {
            .entry_type = MP_BUS,
            .bus_id = 1,
            .bus_type_string = "ISA"
        }
    },
    .ioapic = {
        {
            .entry_type = MP_IOAPIC,
            .apic_id = 0x0,
            .apic_ver = 0x20,
            .flags = MPC_APIC_USABLE,
            .apic_addr = 0xfec00000,
        }
    },
    .intsrc = {
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_ACTIVE_HIGH,
            .src_bus_id = 0x0,
            .src_bus_irq = (0x2 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x0b,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = MP_IRQPOL_ACTIVE_HIGH,
            .src_bus_id = 0x0,
            .src_bus_irq = (0x1f << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x10,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x00 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x02,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x00 << 2) | 0x1,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x01,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x00 << 2) | 0x3,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x03,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x01 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x04,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x01 << 2) | 0x2,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x06,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x01 << 2) | 0x3,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x07,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x02 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x08,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x03 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x0c,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x03 << 2) | 0x1,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x0d,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x03 << 2) | 0x2,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x0e,
        },
        {
            .entry_type = MP_INTSRC,
            .int_type = mp_INT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x03 << 2) | 0x3,
            .dst_apic_id = 0x0,
            .dst_apic_irq = 0x0f,
        },
    },
    .lintsrc = {
        {
            .entry_type = MP_LINTSRC,
            .int_type = mp_ExtINT,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x00 << 2) | 0x0,
            .dst_apic_id = 0x0,
            .dst_apic_lintin = 0x00,
        },
        {
            .entry_type = MP_LINTSRC,
            .int_type = mp_NMI,
            .int_flag = 0,
            .src_bus_id = 0x1,
            .src_bus_irq = (0x00 << 2) | 0x0,
            .dst_apic_id = 0xFF,
            .dst_apic_lintin = 0x01,
        }
    }
};

#endif

static uint32_t get_cpuid(uint32_t eax)
{
    uint32_t ret;

    asm volatile(
                 "mov %1, %%eax\r\n"
                 "cpuid\r\n"
                 "mov %%eax, %0 \r\n"
                 : "=d"(ret)
                 : "r" (eax));

    return ret;
}

static void calc_checksum(struct mptable *mp)
{
    uint8_t checksum = 0;
    unsigned int i;
    uint8_t *ptr;

    mp->mpc_table.checksum = 0;
    ptr = (uint8_t*)&mp->mpc_table;
    for (i = 0; i < sizeof(struct mptable) - sizeof(struct mp_float); i++, ptr++) {
        checksum += *ptr;
    }
    mp->mpc_table.checksum = (uint8_t)(-((int8_t)checksum));

    checksum = 0;
    mp->mpf.checksum = 0;
    ptr = (uint8_t*)&mp->mpf;
    for (i = 0; i < sizeof(struct mp_float); i++, ptr++) {
        checksum += *ptr;
    }
    mp->mpf.checksum  = (uint8_t)(-((int8_t)checksum));

    ptr = (uint8_t*)&mp->mpc_table;
    checksum = 0;
    for (i = 0; i < sizeof(struct mptable) - sizeof(struct mp_float); i++) {
        checksum += *ptr;
        ptr++;
    }
}

void mptable_setup(void)
{
    uint32_t apic_id, apic_ver;
    uint32_t cpupid_sign;
    struct mptable *_mp;

    _mp = (struct mptable *)MPTABLE_LOAD_BASE;
    x86_log_memory_load((uint32_t)MPTABLE_LOAD_BASE,
                        (uint32_t)MPTABLE_LOAD_BASE + sizeof(struct mptable), 
                        "MPTABLE");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
    memcpy(_mp, (void*)&_mptable, sizeof(struct mptable));
#pragma GCC diagnostic pop
    apic_id = mmio_read32(LOCAL_APIC_ID);
    apic_ver = mmio_read32(LOCAL_APIC_VER);

    apic_id >>= 24;

    cpupid_sign = get_cpuid(0x1);

    _mp->mpce_processor[0].apic_id = apic_id;
    _mp->mpce_processor[0].apic_ver = (uint8_t)(apic_ver & 0xff);
    _mp->mpce_processor[0].cpu_flags = 0x3; /* bp | enabled */
    _mp->mpce_processor[0].cpu_signature = cpupid_sign;
    _mp->mpce_processor[0].feature_flags = 0;
    _mp->mpce_processor[0]._res[0] = _mp->mpce_processor[0]._res[1] = 0;


#ifdef TARGET_kontron_vx3060_s2
    {
        int i;
        for (i = 1; i < 4; ++i) {
            _mp->mpce_processor[i].apic_id = i * 2;
            _mp->mpce_processor[i].apic_ver = (uint8_t)(apic_ver & 0xff);
            _mp->mpce_processor[i].cpu_flags = 0x1; /* enabled */
            _mp->mpce_processor[i].cpu_signature = cpupid_sign;
            _mp->mpce_processor[i].feature_flags = 0;
            _mp->mpce_processor[i]._res[0] = _mp->mpce_processor[i]._res[1] = 0;
        }
    }
#endif /* TARGET_kontron_vx3060_s2 */

    calc_checksum(_mp);
}
