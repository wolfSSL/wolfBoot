/* zynqmp_atf.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

/* ARM Trusted Firmware (BL31) handoff for wolfBoot as the ZynqMP FSBL.
 *
 * Matches the Xilinx FSBL handoff format (embeddedsw zynqmp_fsbl): a block of
 * ASCII magic "XLNX", an entry count, and up to 8 entries {u64 entry_point,
 * u64 flags}. Its address is published in PMU_GLOBAL.GEN_STORAGE6, which stock
 * BL31 reads during early setup. wolfBoot writes a single entry for the BL33
 * image at its requested EL; BL31 owns starting BL33. */

#include "zynqmp_atf.h"
#include "hal.h"
#include "fdt.h"
#include "loader.h"
#include "printf.h"

/* PMU_GLOBAL base 0xFFD80000. GEN_STORAGE6 (0x48) carries the handoff-block
 * address (read by stock BL31); GEN_STORAGE5 (0x44) carries the BL33 device
 * tree address (consumed by a small TF-A patch). Both are device memory. */
#define PMU_GLOBAL_GLOB_GEN_STORAGE5  0xFFD80044UL
#define PMU_GLOBAL_GLOB_GEN_STORAGE6  0xFFD80048UL

/* PartitionFlags: bit0 exec-AA32, bit1 big-endian, bit2 secure (all 0 for
 * non-secure AArch64 LE), bits[4:3] target EL, bits[6:5] A53 core (0). */
#define ZYNQMP_ATF_FLAG_EL_SHIFT    3U
#define ZYNQMP_ATF_FLAG_EL_MASK     (3U << ZYNQMP_ATF_FLAG_EL_SHIFT)

#define ZYNQMP_ATF_MAX_ENTRIES      8

struct zynqmp_atf_entry {
    uint64_t entry_point;
    uint64_t flags;
};

struct zynqmp_atf_handoff_params {
    char     magic[4];   /* "XLNX" */
    uint32_t num_entries;
    struct zynqmp_atf_entry entry[ZYNQMP_ATF_MAX_ENTRIES];
};

/* Lives in OCM (wolfBoot's .bss). BL31 is loaded to DDR, so this block is
 * untouched until BL31 reads it. */
static struct zynqmp_atf_handoff_params atf_handoff;

/* Generic AArch64 EL3 exit (cache/MMU teardown + branch), defined in
 * src/boot_aarch64_start.S (built under WOLFBOOT_ZYNQMP_FSBL). */
extern void el3_jump_to_entry(uintptr_t entry);
extern void flush_dcache_range(uintptr_t start, uintptr_t end);

void zynqmp_atf_handoff(uintptr_t bl31_entry, uintptr_t bl33_entry,
    uintptr_t dts_addr, uint32_t bl33_el)
{
    volatile uint32_t* storage5 =
        (volatile uint32_t*)PMU_GLOBAL_GLOB_GEN_STORAGE5;
    volatile uint32_t* storage6 =
        (volatile uint32_t*)PMU_GLOBAL_GLOB_GEN_STORAGE6;
    uint64_t flags;

#ifdef MMU
    /* BL31 owns starting BL33, so this handoff never reaches do_boot() and
     * the DTB fixup pass that runs there. Do it here instead, ahead of the
     * whole-D-cache clean in el3_jump_to_entry(). */
    if (dts_addr != 0 &&
            hal_dts_fixup((void*)dts_addr, WOLFBOOT_DTS_MAX_SIZE) != 0) {
        wolfBoot_printf("FDT: fixup failed before BL31 handoff\n");
#ifdef WOLFBOOT_UPDATE_DISK
        /* A pass that gave up before the /chosen/wolfboot,boot-part write
         * leaves the OS with no way to tell which slot it must confirm. */
        if (wolfBoot_disk_boot_part() >= 0) {
            wolfBoot_panic();
        }
#endif
    }
#endif

    /* Non-secure, AArch64, little-endian, A53-0, at the requested EL. */
    flags = ((uint64_t)(bl33_el << ZYNQMP_ATF_FLAG_EL_SHIFT))
            & ZYNQMP_ATF_FLAG_EL_MASK;

    atf_handoff.magic[0] = 'X';
    atf_handoff.magic[1] = 'L';
    atf_handoff.magic[2] = 'N';
    atf_handoff.magic[3] = 'X';
    atf_handoff.num_entries = 1;
    atf_handoff.entry[0].entry_point = (uint64_t)bl33_entry;
    atf_handoff.entry[0].flags = flags;

    /* Publish the parameter-block address where BL31 reads it, and the BL33
     * device tree address for the TF-A patch that forwards it to x0. */
    *storage6 = (uint32_t)(uintptr_t)&atf_handoff;
    *storage5 = (uint32_t)dts_addr;

    /* Clean the parameter block to the PoC so BL31, entered with the MMU and
     * caches off, observes it. */
    flush_dcache_range((uintptr_t)&atf_handoff,
        (uintptr_t)&atf_handoff + sizeof(atf_handoff));

    /* Tear down EL3 MMU/caches and branch to BL31. Does not return. */
    el3_jump_to_entry(bl31_entry);
}
