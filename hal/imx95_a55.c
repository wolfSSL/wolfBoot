/* imx95_a55.c
 *
 * HAL for the Cortex-A55 cluster on the NXP i.MX95, running as BL33.
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

/* wolfBoot as BL33: third image in AHAB container 2, entered by BL31 at
 * IMX95_BL33_BASE in NS-EL2. SPL/ELE authenticate the container (anchored
 * once SRK fuses are programmed). RAM-resident, no flash: the flash HAL is a
 * no-op surface the core references unconditionally. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "image.h"
#include "loader.h"
#include "printf.h"
#include "hal/imx95_a55.h"

#ifdef IMX95_INIT_M7
/* SCMI M7 power-domain on + TCM ECC scrub (hal/imx95_scmi.c). */
extern int imx95_m7_tcm_init(void);
#endif

/* x0 at entry (captured by the .boot stub); .data so BSS clear spares it. */
volatile uint64_t boot_handoff_x0 = 0xFFFFFFFFFFFFFFFFULL;

static inline uint32_t rd32(uintptr_t a)
{
    return *(volatile uint32_t*)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
    *(volatile uint32_t*)a = v;
}

/* --------------------------------------------------------------------------
 * Handoff recon: print the machine state BL31 handed us - entry EL, SCTLR
 * MMU/cache bits, the x0 pointer - to confirm the BL33 entry contract. This is
 * the whole point of the first bring-up image, so it is on by default.
 * -------------------------------------------------------------------------- */

static inline uint64_t read_current_el(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (v >> 2) & 0x3;
}

/* Read the SCTLR of the current EL (a higher EL's would trap). */
static uint64_t read_current_sctlr(uint64_t el)
{
    uint64_t v = 0;

    switch (el) {
        case 3: __asm__ volatile("mrs %0, sctlr_el3" : "=r"(v)); break;
        case 2: __asm__ volatile("mrs %0, sctlr_el2" : "=r"(v)); break;
        default: __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v)); break;
    }
    return v;
}

#if defined(DEBUG_UART) && defined(IMX95_HANDOFF_DUMP)

static inline uint64_t read_mpidr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

/* Two 32-bit halves: the small printf may lack long long. */
static void dump64(const char* name, uint64_t v)
{
    wolfBoot_printf("%s0x%08x%08x\n", name,
        (uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFFUL));
}

static void imx95_handoff_dump(void)
{
    uint64_t el = read_current_el();
    uint64_t sctlr = read_current_sctlr(el);
    uint64_t x0 = boot_handoff_x0;
    uint64_t cntfrq;
    const uint8_t* p;
    int i;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    wolfBoot_printf("i.MX95 A55 handoff recon:\n");
    wolfBoot_printf("  CurrentEL:   EL%d\n", (int)el);
    dump64("  SCTLR_ELx:   ", sctlr);
    wolfBoot_printf("    MMU=%d I$=%d D$=%d\n",
        (int)(sctlr & 0x1), (int)((sctlr >> 12) & 0x1),
        (int)((sctlr >> 2) & 0x1));
    dump64("  MPIDR_EL1:   ", read_mpidr());
    dump64("  CNTFRQ_EL0:  ", cntfrq);
    dump64("  handoff x0:  ", x0);

    /* Only deref x0 if it lands in DRAM (a DTB starts d00dfeed BE). */
    if (x0 >= IMX95_DRAM_BASE && x0 <= (IMX95_DRAM_END - 16)) {
        p = (const uint8_t*)(uintptr_t)x0;
        wolfBoot_printf("  [x0] first 16 bytes:\n    ");
        for (i = 0; i < 16; i++)
            wolfBoot_printf("%02x ", p[i]);
        wolfBoot_printf("\n");
    }
    else {
        wolfBoot_printf("  [x0] not a plausible DRAM pointer; skipping dump\n");
    }
}
#endif /* DEBUG_UART && IMX95_HANDOFF_DUMP */

/* Span covering every address a verified FIT sub-image is copied to on this
 * target: the DTS relocation target, the staging window and the kernel/ramdisk
 * loads. hal_prepare_boot() cleans it as one range because the exit's set/way
 * clean does not reach the DSU/L3. imx95_check_load_ranges() rejects a
 * configuration whose load addresses fall outside it; a payload whose tail
 * would run past the top needs the top raised, so keep the two in step. */
#ifndef IMX95_PAYLOAD_FLUSH_BASE
#define IMX95_PAYLOAD_FLUSH_BASE   0xA0000000UL
#endif
#ifndef IMX95_PAYLOAD_FLUSH_TOP
/* Must stay above the highest address a FIT sub-image can reach: the ramdisk
 * destination plus WOLFBOOT_FIT_MAX_RAMDISK (src/fdt.c, 256 MiB by default)
 * lands at 0xCA000000 with this config. Sized past that so a full-size
 * initramfs cannot leave its tail dirty, and still inside DRAM bank 0. */
#define IMX95_PAYLOAD_FLUSH_TOP    0xD0000000UL
#endif

/* Staging-window sanity: overlap with the BL31/OP-TEE/M7 carveouts corrupts
 * them silently; fail loudly at boot instead. */

static int range_overlaps(uint64_t a_start, uint64_t a_len,
                          uint64_t b_start, uint64_t b_len)
{
    return (a_start < (b_start + b_len)) && (b_start < (a_start + a_len));
}

static int imx95_check_one(const char* name, uint64_t load, uint64_t len,
                           uint64_t res, uint64_t res_len)
{
    if (range_overlaps(load, len, res, res_len)) {
        wolfBoot_printf("imx95: staging window overlaps %s\n", name);
        return -1;
    }
    return 0;
}

/* A destination wolfBoot copies a FIT sub-image to must sit in DRAM bank 0,
 * inside the span hal_prepare_boot() cleans (or the copy is left dirty in the
 * DSU/L3 and the kernel reads it with caches off), and clear of the reserved
 * carveouts - vpu_boot starts exactly at the bottom of the flushed span, so
 * being inside that span is not by itself proof of being safe. */
static int imx95_check_dest(const char* name, uint64_t addr, uint64_t len)
{
    int ret = 0;

    if (addr < IMX95_DRAM_BASE || addr >= IMX95_DRAM_END) {
        wolfBoot_printf("imx95: %s load address outside DRAM bank 0\n", name);
        return -1;
    }
    if (addr < IMX95_PAYLOAD_FLUSH_BASE || addr >= IMX95_PAYLOAD_FLUSH_TOP) {
        wolfBoot_printf("imx95: %s load address outside the flushed span\n",
                        name);
        return -1;
    }
    ret |= imx95_check_one(name, addr, len, IMX95_BL31_BASE, 0x200000UL);
    ret |= imx95_check_one(name, addr, len,
                           IMX95_OPTEE_BASE, IMX95_OPTEE_SIZE);
    ret |= imx95_check_one(name, addr, len,
                           IMX95_OPTEE_SHM_BASE, IMX95_OPTEE_SHM_SIZE);
    ret |= imx95_check_one(name, addr, len,
                           IMX95_M7_DDR_BASE, IMX95_M7_DDR_SIZE);
    ret |= imx95_check_one(name, addr, len,
                           IMX95_ELE_SHM_BASE, IMX95_ELE_SHM_SIZE);
    ret |= imx95_check_one(name, addr, len,
                           IMX95_VPU_BOOT_BASE, IMX95_VPU_BOOT_SIZE);
    return ret;
}

int imx95_check_load_ranges(void)
{
    uint64_t load = (uint64_t)WOLFBOOT_LOAD_ADDRESS;
    uint64_t len = (uint64_t)WOLFBOOT_RAMBOOT_MAX_SIZE;
    extern uint8_t _end[];
    uint64_t self = (uint64_t)IMX95_BL33_BASE;
    uint64_t self_len = (uint64_t)(uintptr_t)_end - self;
    int ret = 0;

    ret |= imx95_check_one("BL31", load, len, IMX95_BL31_BASE, 0x200000UL);
    ret |= imx95_check_one("OP-TEE", load, len,
                           IMX95_OPTEE_BASE, IMX95_OPTEE_SIZE);
    ret |= imx95_check_one("OP-TEE shm", load, len,
                           IMX95_OPTEE_SHM_BASE, IMX95_OPTEE_SHM_SIZE);
    ret |= imx95_check_one("M7 carveout", load, len,
                           IMX95_M7_DDR_BASE, IMX95_M7_DDR_SIZE);
    ret |= imx95_check_one("ELE shared buffer", load, len,
                           IMX95_ELE_SHM_BASE, IMX95_ELE_SHM_SIZE);
    ret |= imx95_check_one("vpu_boot", load, len,
                           IMX95_VPU_BOOT_BASE, IMX95_VPU_BOOT_SIZE);
    ret |= imx95_check_one("wolfBoot itself", load, len, self, self_len);

    if (load < IMX95_DRAM_BASE || (load + len) > IMX95_DRAM_END) {
        wolfBoot_printf("imx95: staging window outside DRAM bank 0\n");
        ret = -1;
    }

    /* The other destinations this stage writes to. The kernel's own load
     * address comes from the FIT rather than the build, so it is not checked
     * here; keep it inside the same span. */
    ret |= imx95_check_dest("staging", (uint64_t)WOLFBOOT_LOAD_ADDRESS,
                            (uint64_t)WOLFBOOT_RAMBOOT_MAX_SIZE);
    ret |= imx95_check_dest("DTS", (uint64_t)WOLFBOOT_LOAD_DTS_ADDRESS,
                            (uint64_t)WOLFBOOT_DTS_MAX_SIZE);
#ifdef WOLFBOOT_LOAD_RAMDISK_ADDRESS
    /* The ramdisk extent is not known until the FIT is parsed; check the
     * destination itself so a configuration that aims it at a carveout fails
     * at boot rather than corrupting one. */
    ret |= imx95_check_dest("ramdisk",
                            (uint64_t)WOLFBOOT_LOAD_RAMDISK_ADDRESS, 1UL);
#endif
    return ret;
}

/* --------------------------------------------------------------------------
 * HAL surface
 * -------------------------------------------------------------------------- */

void hal_init(void)
{
#if defined(DEBUG_UART)
    uart_init();
    wolfBoot_printf("\nwolfBoot: NXP i.MX95 Cortex-A55 (BL33)\n");
#endif
#if defined(DEBUG_UART) && defined(IMX95_HANDOFF_DUMP)
    imx95_handoff_dump();
#endif
    if (imx95_check_load_ranges() != 0) {
        wolfBoot_printf("imx95: refusing to boot with an unsafe load window\n");
        wolfBoot_panic();
    }
#ifdef IMX95_INIT_M7
    /* U-Boot powers up the M7 mix and scrubs its TCM (ECC) at board init; as
     * its BL33 replacement wolfBoot must too. Non-fatal: the A55 boot proceeds
     * even if the M7 bring-up fails. */
    (void)imx95_m7_tcm_init();
#endif
}

/* src/boot_aarch64_start.S: clean+invalidate by VA. */
extern void flush_dcache_range(uintptr_t start, uintptr_t end);

#if defined(WOLFBOOT_UPDATE_DISK) || defined(BOOT_BENCHMARK)
/* Microseconds from the ARM generic timer. CNTFRQ_EL0 is set up by the stages
 * ahead of wolfBoot (24 MHz on this part); fall back to that rather than
 * dividing by zero if a platform ever leaves it clear. */
uint64_t hal_get_timer_us(void)
{
    uint64_t cnt, frq;

    __asm__ volatile("isb" : : : "memory");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    if (frq == 0)
        frq = 24000000ULL;
    return (cnt * 1000000ULL) / frq;
}
#endif

void hal_prepare_boot(void)
{
    uint64_t sctlr;

    /* Clean payload DRAM by VA: set/way cleaning misses the A55's DSU system
     * cache, so copies made with D-cache on can be stale at MMU-off. With the
     * EL2 identity map in place this is load-bearing - the FIT sub-images were
     * copied through the D-cache and Linux enters with caches off - so it is
     * not an optimization to remove. The SCTLR.C test only skips the work for
     * a configuration built without the MMU, where no line can be dirty. */
    sctlr = read_current_sctlr(read_current_el());
    if ((sctlr & SCTLR_C) == 0)
        return;

    flush_dcache_range((uintptr_t)IMX95_PAYLOAD_FLUSH_BASE,
                       (uintptr_t)IMX95_PAYLOAD_FLUSH_TOP);
}

/* No flash on this stage. wolfBoot is RAM-resident and its images arrive from
 * a storage driver, so these exist only to satisfy the core's unconditional
 * references. */
int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* WOLFBOOT_NO_PARTITIONS: the boot and update addresses come from here rather
 * than from linker-script partition symbols. The disk updater selects A/B by
 * partition index and does not consult these, so they exist for the version
 * helpers in libwolfboot.c and both name the one RAM staging window this
 * target loads into. A RAM-update path would need two distinct windows. */
void* hal_get_primary_address(void)
{
    return (void*)(uintptr_t)WOLFBOOT_LOAD_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)(uintptr_t)WOLFBOOT_LOAD_ADDRESS;
}

#if defined(__WOLFBOOT) && defined(LINUX_BOOTARGS)
#include "fdt.h"

/* Primary ethernet (end0) MAC written into the DTB. Default is the Toradex
 * SMARC iMX95 module's address (OUI 00:14:2d; low bytes are the module serial),
 * read from its config block. Override with -DIMX95_ETH0_MAC={...} per board. */
#ifndef IMX95_ETH0_MAC
#define IMX95_ETH0_MAC { 0x00, 0x14, 0x2d, 0x88, 0x8d, 0x20 }
#endif

/* Fix up /chosen and /memory on the (already verified) FIT DTB. */
int hal_dts_fixup(void* dts_addr, uint32_t capacity)
{
    fdt_ctx ctx;
    int off, ret;

    ret = fdt_open(&ctx, dts_addr, capacity);
    if (ret != 0) {
        wolfBoot_printf("FDT: invalid header (%d)\n", ret);
        return ret;
    }
    /* Headroom so the next stage sees room to grow. */
    (void)fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);

    /* Root child only: a nested node named "chosen" must not be matched. */
    off = fdt_subnode_offset(&ctx, 0, "chosen");
    if (off == -FDT_ERR_NOTFOUND)
        off = fdt_add_subnode(&ctx, 0, "chosen");
    if (off < 0) {
        wolfBoot_printf("FDT: no /chosen (%d)\n", off);
        return off;
    }
    ret = fdt_fixup_str(&ctx, off, "chosen", "bootargs", LINUX_BOOTARGS);
    if (ret != 0) {
        wolfBoot_printf("FDT: bootargs fixup failed (%d)\n", ret);
        return ret;
    }

    /* /memory: the OS deployment DTB ships without one (the bootloader
     * normally adds it); a kernel with no memory node hangs before earlycon.
     * Banks match U-Boot's fixup for this board. */
    off = fdt_find_devtype(&ctx, -1, "memory");
    if (off < 0) {
        off = fdt_add_subnode(&ctx, 0, "memory");
        if (off >= 0) {
            fdt_setprop(&ctx, off, "device_type", "memory", 7);
        }
    }
    if (off >= 0) {
        uint64_t memreg[4];
        memreg[0] = cpu_to_fdt64(0x90000000ULL);   /* bank0 start */
        memreg[1] = cpu_to_fdt64(0x70000000ULL);   /* bank0 size  */
        memreg[2] = cpu_to_fdt64(0x100000000ULL);  /* bank1 start */
        memreg[3] = cpu_to_fdt64(0x180000000ULL);  /* bank1 size  */
        ret = fdt_setprop(&ctx, off, "reg", memreg, sizeof(memreg));
        if (ret != 0) {
            wolfBoot_printf("FDT: /memory reg failed (%d)\n", ret);
            return ret;
        }
    }
    else {
        /* Fail closed: a kernel with no RAM description hangs silently. */
        wolfBoot_printf("FDT: cannot create /memory (%d)\n", off);
        return off;
    }

    /* Ethernet MAC: the ENETC ports are PCIe-enumerated and carry no
     * local-mac-address in the OS DTB (U-Boot normally programs it from the
     * Toradex config block), so Linux would pick a random MAC each boot and the
     * DHCP address would float. Set the primary port (ethernet0 = end0) to the
     * module's address. Non-fatal: networking is not boot-critical. */
    {
        static const uint8_t eth0_mac[6] = IMX95_ETH0_MAC;
        off = fdt_path_offset(&ctx, "/soc/pcie@4ca00000/ethernet@0,0");
        if (off >= 0) {
            ret = fdt_setprop(&ctx, off, "local-mac-address", eth0_mac,
                              sizeof(eth0_mac));
            if (ret != 0)
                wolfBoot_printf("FDT: eth0 MAC fixup failed (%d)\n", ret);
        }
        else {
            wolfBoot_printf("FDT: eth0 node not found (%d)\n", off);
        }
    }

    /* Last DTB write before the jump, after hal_prepare_boot()'s flush;
     * set/way cleaning in do_boot misses the DSU system cache, so clean
     * the blob by VA here or the kernel can read stale /chosen edits. */
    flush_dcache_range((uintptr_t)dts_addr,
                       (uintptr_t)dts_addr + fdt_size(&ctx));
    return 0;
}
#endif /* __WOLFBOOT && LINUX_BOOTARGS */

/* The DTB travels inside the verified FIT rather than as a separate image, so
 * there is no standalone DTS partition. Returning NULL is supported - the
 * updater treats it as "no external device tree". */
void* hal_get_dts_address(void)
{
    return NULL;
}

void* hal_get_dts_update_address(void)
{
    return NULL;
}
