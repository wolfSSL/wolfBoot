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
 * IMX95_BL33_BASE in NS-EL2. RAM-resident, so the flash HAL below is a no-op
 * surface the core references unconditionally. */

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "image.h"
#include "loader.h"
#include "printf.h"
#include "hal/imx95_a55.h"
#ifdef IMX95_EMMC_PROBE
#include "hal/imx95_ahab.h"
#endif

#ifdef IMX95_INIT_M7
/* SCMI M7 power-domain on + TCM ECC scrub, in the SCMI section below. */
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

#ifndef BUILD_LOADER_STAGE1
/* To the drivers further down, everything is the wolfBoot HAL API this image
 * presents as BL33. Stage 1 links this file for those drivers but is not a
 * wolfBoot instance: it has no partitions, FIT or device tree, and its target.h
 * does not define the load addresses here. The PPC targets gate the same way. */

/* Handoff recon: the entry EL, SCTLR MMU/cache bits and x0 BL31 passed, to
 * confirm the BL33 entry contract. On by default; it is cheap and diagnostic. */

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

/* Span covering every address a verified FIT sub-image is copied to, cleaned as
 * one range by hal_prepare_boot() because the exit's set/way clean misses the
 * DSU/L3. imx95_check_load_ranges() rejects loads outside it; keep them in step. */
#ifndef IMX95_PAYLOAD_FLUSH_BASE
#define IMX95_PAYLOAD_FLUSH_BASE   0xA0000000UL
#endif
#ifndef IMX95_PAYLOAD_FLUSH_TOP
/* Must stay above the highest address a FIT sub-image reaches: ramdisk dest plus
 * WOLFBOOT_FIT_MAX_RAMDISK lands at 0xCA000000 here, so a full-size initramfs
 * cannot leave its tail dirty. Still inside DRAM bank 0. */
#define IMX95_PAYLOAD_FLUSH_TOP    0xD0000000UL
#endif

/* Staging-window sanity: overlap with the BL31/OP-TEE/M7 carveouts corrupts
 * them silently; fail loudly at boot instead. */

/* Same contract as its twin in hal/imx95_ahab.c: an empty range overlaps
 * nothing, and callers hand in wrap-free extents (imx95_check_dest() rejects a
 * wrapping addr+len before it gets here). */
static int range_overlaps(uint64_t a_start, uint64_t a_len,
                          uint64_t b_start, uint64_t b_len)
{
    if (a_len == 0U || b_len == 0U)
        return 0;
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

/* A FIT sub-image destination must be in DRAM bank 0, inside the span
 * hal_prepare_boot() cleans, and clear of the carveouts - vpu_boot starts at the
 * bottom of that span, so being inside it is not proof of being safe. */
static int imx95_check_dest(const char* name, uint64_t addr, uint64_t len)
{
    uint64_t end;
    int ret = 0;

    if (len == 0UL || addr > (UINT64_MAX - len)) {
        wolfBoot_printf("imx95: %s load range is empty or wraps\n", name);
        return -1;
    }
    end = addr + len;
    if (addr < IMX95_DRAM_BASE || end > IMX95_DRAM_END) {
        wolfBoot_printf("imx95: %s load range outside DRAM bank 0\n", name);
        return -1;
    }
    /* The end matters as much as the start: hal_prepare_boot() cleans only this
     * span by VA, so a tail outside it reaches Linux un-cleaned at MMU-off. */
    if (addr < IMX95_PAYLOAD_FLUSH_BASE || end > IMX95_PAYLOAD_FLUSH_TOP) {
        wolfBoot_printf("imx95: %s load range outside the flushed span\n",
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

    /* The destinations known from the build. The kernel, DTB and ramdisk come
     * from the FIT's own load properties instead, so wolfBoot_fit_memcpy()
     * bounds those at the point of copy. */
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

    /* Clean payload DRAM by VA: set/way misses the A55's DSU cache, so copies
     * made with D-cache on can be stale at MMU-off. Load-bearing, not an
     * optimization. The SCTLR.C test only skips it on an MMU-less build. */
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

/* WOLFBOOT_NO_PARTITIONS: boot/update addresses come from here, not linker
 * symbols. The disk updater picks A/B by partition index and ignores these, so
 * they serve libwolfboot.c's version helpers and both name one staging window. */
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

/* Primary ethernet (end0) MAC as a 48-bit value, e.g.
 * -DIMX95_ETH0_MAC=0x00142d888d20 (see config/examples/imx95-a55.config).
 * Undefined means skip the fixup: shipping one module's address as every
 * board's default would put duplicate MACs on a network. */

/* Fix up /chosen and /memory on the (already verified) FIT DTB. */
/* Every FIT sub-image destination comes from that image's own `load` property,
 * so it is bounded here rather than assumed from the build: this hook is the
 * only place the kernel and DTB destinations are known. The caller fails closed
 * on a negative return. */
/* Shared by both FIT write paths. The FIT is still live in the staging window
 * and is the copy's source, so a destination inside it would overwrite the
 * bytes being read. That is checked here and not in imx95_check_dest(), which
 * is also asked to validate the staging window itself and would then reject its
 * own configuration. */
static int imx95_fit_dest_ok(uint64_t dst, uint64_t len)
{
    if (imx95_check_dest("FIT sub-image", dst, len) != 0) {
        return -1;
    }
    if (range_overlaps(dst, len, (uint64_t)WOLFBOOT_LOAD_ADDRESS,
                       (uint64_t)WOLFBOOT_RAMBOOT_MAX_SIZE)) {
        wolfBoot_printf("imx95: FIT sub-image would land in the staging "
                        "window\n");
        return -1;
    }
    return 0;
}

int wolfBoot_fit_memcpy(void *dst, const void *src, uint32_t len)
{
    if (imx95_fit_dest_ok((uint64_t)(uintptr_t)dst, (uint64_t)len) != 0) {
        return -1;
    }
    memcpy(dst, src, len);
    return 0;
}

/* The gzip path writes straight to the FIT-declared address, so it needs the
 * same bound as the copy. len is the decompressor's output ceiling. */
int wolfBoot_fit_check_dest(void *dst, uint32_t len)
{
    return imx95_fit_dest_ok((uint64_t)(uintptr_t)dst, (uint64_t)len);
}

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
    ret = fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);
    if (ret != 0) {
        wolfBoot_printf("FDT: no headroom for fixups (%d)\n", ret);
        return ret;
    }

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
            /* Without device_type Linux does not read the node as memory, so
             * falling through to write reg would cause the silent pre-earlycon
             * hang this fixup exists to avoid. */
            ret = fdt_setprop(&ctx, off, "device_type", "memory", 7);
            if (ret != 0) {
                wolfBoot_printf("FDT: /memory device_type failed (%d)\n", ret);
                return ret;
            }
        }
    }
    if (off >= 0) {
        uint64_t memreg[4];
        /* Override per module: DRAM population varies by SKU. */
        memreg[0] = cpu_to_fdt64(IMX95_DRAM_BANK0_BASE);
        memreg[1] = cpu_to_fdt64(IMX95_DRAM_BANK0_SIZE);
        memreg[2] = cpu_to_fdt64(IMX95_DRAM_BANK1_BASE);
        memreg[3] = cpu_to_fdt64(IMX95_DRAM_BANK1_SIZE);
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

    /* The ENETC ports are PCIe-enumerated with no local-mac-address in the OS
     * DTB, so Linux would pick a random MAC each boot and the DHCP address would
     * float. Sets ethernet0 only, and is non-fatal. */
#ifdef IMX95_ETH0_MAC
    {
        static const uint8_t eth0_mac[6] = {
            (uint8_t)((IMX95_ETH0_MAC >> 40) & 0xFFU),
            (uint8_t)((IMX95_ETH0_MAC >> 32) & 0xFFU),
            (uint8_t)((IMX95_ETH0_MAC >> 24) & 0xFFU),
            (uint8_t)((IMX95_ETH0_MAC >> 16) & 0xFFU),
            (uint8_t)((IMX95_ETH0_MAC >>  8) & 0xFFU),
            (uint8_t)(IMX95_ETH0_MAC & 0xFFU)
        };
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
#endif /* IMX95_ETH0_MAC */

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
#endif /* !BUILD_LOADER_STAGE1 */


/* LPUART1 console. A55-only, shared with the stage 1 loader. */

#if defined(DEBUG_UART)

/* Stage 1 runs before anything has touched the port, so it programs it; as
 * BL33 the earlier stages left it clocked and at 115200, and rewriting BAUD
 * would need the reference rate the System Manager owns. */
void uart_init(void)
{
#ifdef IMX95_STAGE1
    /* 24 MHz / (16 * 13) = 115385, inside what an 8N1 receiver tolerates. */
    wr32(IMX95_LPUART1_BASE + LPUART_CTRL_OFF, 0);
    wr32(IMX95_LPUART1_BASE + LPUART_BAUD_OFF,
         ((LPUART_BAUD_OSR - 1UL) << 24) | LPUART_BAUD_SBR);
    wr32(IMX95_LPUART1_BASE + LPUART_CTRL_OFF, LPUART_CTRL_TE);
#endif
}

static void uart_tx(char c)
{
    while ((rd32(IMX95_LPUART1_BASE + LPUART_STAT_OFF) & LPUART_STAT_TDRE) == 0)
        ;
    wr32(IMX95_LPUART1_BASE + LPUART_DATA_OFF, (uint32_t)(uint8_t)c);
}

#ifdef IMX95_LOG_RING
/* The ring is in the M7 carveout, which the EL2 map marks Normal
 * Non-Cacheable so readers outside this cluster's coherency see it without
 * maintenance. */
static void log_ring_putc(char c)
{
    static int ring_ready;
    volatile uint32_t *hdr = (volatile uint32_t *)(uintptr_t)IMX95_LOG_RING_BASE;
    volatile uint8_t *data =
        (volatile uint8_t *)(uintptr_t)(IMX95_LOG_RING_BASE + IMX95_LOG_RING_HDR);
    uint32_t wr;
    int inherit = 0;

    if (!ring_ready) {
#ifndef IMX95_STAGE1
        /* Stage 1 starts the ring clean and later images continue it, or the
         * second writer resets the counter and the first image's output is
         * lost. A cold boot leaves garbage, so magic and size must match. */
        if (hdr[0] == (uint32_t)IMX95_LOG_RING_MAGIC &&
                hdr[2] == (uint32_t)IMX95_LOG_RING_SIZE)
            inherit = 1;
#endif
        if (!inherit) {
            /* Publish the magic last: until it is set a reader treats the
             * region as absent rather than reading a stale count. */
            hdr[1] = 0;
            hdr[2] = (uint32_t)IMX95_LOG_RING_SIZE;
            hdr[3] = 0;
            hdr[0] = (uint32_t)IMX95_LOG_RING_MAGIC;
        }
        ring_ready = 1;
    }
    wr = hdr[1];
    data[wr % (uint32_t)IMX95_LOG_RING_SIZE] = (uint8_t)c;
    hdr[1] = wr + 1;   /* monotonic: the reader derives wrap from it */
}
#endif /* IMX95_LOG_RING */

/* printf does not expand newlines. */
void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n')
            uart_tx('\r');
        uart_tx(buf[i]);
#ifdef IMX95_LOG_RING
        log_ring_putc(buf[i]);
#endif
    }
    while ((rd32(IMX95_LPUART1_BASE + LPUART_STAT_OFF) & LPUART_STAT_TC) == 0)
        ;
}

#endif /* DEBUG_UART */


/* SCMI client over MU2: clocks, pinmux, power. A55-only, shared with the stage 1 loader. */

/* Without U-Boot ahead of wolfBoot, clocks, pinmux and power belong to the M33
 * System Manager and must be requested over SCMI - the pad registers data-abort
 * on direct access from BL33. SMT transport over MU2, then clock/pinctrl/power. */


#if defined(IMX95_SCMI_COLD_INIT) || defined(IMX95_INIT_M7) || \
    defined(IMX95_STAGE1)

#define MU2_GCR         (IMX95_MU2_BASE + 0x114)   /* set BIT0: ring A2P doorbell */
#define MU2_GSR         (IMX95_MU2_BASE + 0x118)   /* BIT0: GIR0 ack (write 1 to clr) */
#define SCMI_SHMEM      0x445B1000UL         /* scmi_buf0, 1 KiB */

/* SMT header offsets within the shared buffer */
#define SMT_CHAN_STATUS 0x04
#define SMT_FLAGS       0x10
#define SMT_LENGTH      0x14
#define SMT_MSG_HEADER  0x18
#define SMT_PAYLOAD     0x1C
#define CHAN_FREE       0x1UL

#define SCMI_PROTO_PERF     0x13
#define SCMI_PROTO_CLOCK    0x14
#define SCMI_PROTO_PINCTRL  0x19
#define SCMI_PROTO_POWER    0x11
#define CLOCK_RATE_SET      0x5
#define CLOCK_RATE_GET      0x6
#define CLOCK_CONFIG_SET    0x7
#define CLOCK_PARENT_SET    0xD
#define CLOCK_RATE_ROUND_CLOSEST (1UL << 3)
#define PINCTRL_CONFIG_SET  0x6
#define PWD_STATE_SET       0x4
#define PWD_STATE_GET       0x5
#define PERF_LEVEL_SET      0x7
#define PINCTRL_TYPE_MUX    192
#define PINCTRL_TYPE_CONFIG 193

/* Power domain id for the M7 mix (dt-bindings/power/fsl,imx95-power.h). */
#define IMX95_PD_DDR        12
#define IMX95_PD_M7         17

/* Performance domain 8 is the A55 cluster; level 3 is its top operating
 * point. U-Boot's set_arm_core_max_clk() uses the same pair. */
#define IMX95_PERF_DOM_ARM  8
#define IMX95_PERF_LVL_MAX  3

/* Pad ALT mode passed as the SCMI MUX value: 0 = uSDHC2 function, 5 = GPIO3. */
#define PAD_ALT_LPUART1     0
#define PAD_ALT_USDHC1      0
#define PAD_ALT_USDHC2      0
#define PAD_ALT_GPIO        5

#define IMX95_CLK_24M       2
#define IMX95_CLK_SYSPLL1_PFD1 9
#define IMX95_CLK_LPUART1   52    /* IMX95_CCM_NUM_CLK_SRC(41) + 11 */
#define IMX95_CLK_USDHC1    158   /* IMX95_CCM_NUM_CLK_SRC(41) + 117 */
#define IMX95_CLK_USDHC2    159   /* IMX95_CCM_NUM_CLK_SRC(41) + 118 */

#define SCMI_TIMEOUT        2000000

/* Build with -DIMX95_SCMI_DEBUG for a per-step trace of the cold-init. */
#ifdef IMX95_SCMI_DEBUG
#define SCMI_DBG(...) wolfBoot_printf(__VA_ARGS__)
#else
#define SCMI_DBG(...) do { } while (0)
#endif


/* One synchronous SCMI command. payload[] holds n_in request words in and n_out
 * response words out, word 0 the status. The counts differ per message, and a
 * wrong one makes the System Manager read a field from the wrong offset. */
static int scmi_cmd(uint32_t proto, uint32_t msg_id,
                    uint32_t* payload, uint32_t n_in, uint32_t n_out)
{
    uintptr_t sh = SCMI_SHMEM;
    uint32_t n, i;

    /* Wait for the channel to be free. */
    for (n = 0; n < SCMI_TIMEOUT; n++) {
        if (rd32(sh + SMT_CHAN_STATUS) & CHAN_FREE)
            break;
    }
    if (n == SCMI_TIMEOUT)
        return -1;

    wr32(sh + SMT_FLAGS, 0);                       /* poll, no interrupt */
    wr32(sh + SMT_LENGTH, 4 + n_in * 4);           /* header word + payload */
    wr32(sh + SMT_MSG_HEADER, (proto << 10) | (msg_id & 0xFF));
    for (i = 0; i < n_in; i++)
        wr32(sh + SMT_PAYLOAD + i * 4, payload[i]);

    /* Hand the channel to the SM and ring the doorbell. */
    wr32(sh + SMT_CHAN_STATUS, rd32(sh + SMT_CHAN_STATUS) & ~CHAN_FREE);
    wr32(MU2_GCR, rd32(MU2_GCR) | 0x1);

    /* The SM sets FREE again when the response is in place. */
    for (n = 0; n < SCMI_TIMEOUT; n++) {
        if (rd32(sh + SMT_CHAN_STATUS) & CHAN_FREE)
            break;
    }
    if (n == SCMI_TIMEOUT)
        return -2;
    wr32(MU2_GSR, 0x1);                            /* clear GIR0 ack */

    for (i = 0; i < n_out; i++)
        payload[i] = rd32(sh + SMT_PAYLOAD + i * 4);
    return (int)payload[0];                      /* SCMI status, 0 = OK */
}

#if (defined(DISK_SDCARD) && defined(IMX95_SCMI_COLD_INIT)) || \
    defined(IMX95_STAGE1)
static int scmi_clock_enable(uint32_t clock_id)
{
    uint32_t p[3];
    p[0] = clock_id;
    p[1] = 1;    /* attributes: enable */
    p[2] = 0;    /* oem_config_val */
    return scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_CONFIG_SET, p, 3, 1);
}

/* One pad: MUX(=mux) + CONFIG(=conf). identifier is the mux register offset/4
 * (the SM's pin numbering); function_id 0xFFFFFFFF, attributes = ncfgs<<2. */
static int scmi_pin_config(uint32_t mux_ofs, uint32_t mux, uint32_t conf)
{
    uint32_t p[7];
    p[0] = mux_ofs / 4;      /* identifier */
    p[1] = 0xFFFFFFFF;       /* function_id */
    p[2] = (uint32_t)(2 << 2); /* attributes: 2 configs */
    p[3] = PINCTRL_TYPE_MUX;    p[4] = mux;
    p[5] = PINCTRL_TYPE_CONFIG; p[6] = conf;
    return scmi_cmd(SCMI_PROTO_PINCTRL, PINCTRL_CONFIG_SET, p, 7, 1);
}

/* Three uSDHC2 signals are carrier GPIOs, not controller pins: SCMI-mux the pads
 * to GPIO3 (ALT5), then drive RGPIO3 directly. From the SMARC DTS:
 *   GPIO3.0  SD2_CD_B   card detect     (input, active low)
 *   GPIO3.7  SD2_RESET_B SDIO_PWR_EN    (output high = card power on)
 *   GPIO3.19 SD2_VSELECT PMIC_SD2_VSEL  (output low = 3.3V, high = 1.8V) */
#define RGPIO3_PDOR  (IMX95_GPIO3_BASE + 0x40)
#define RGPIO3_PDDR  (IMX95_GPIO3_BASE + 0x54)
#define GPIO3_CD     0U
#define GPIO3_PWR    7U
#define GPIO3_VSEL   19U

static void delay_loops(uint32_t loops)
{
    volatile uint32_t d = loops;
    while (d-- > 0U) { }
}

int imx95_usdhc2_cold_init(void)
{
    /* All nine uSDHC2 pads: {mux offset, ALT mode, pad conf}. Data/clk/cmd take
     * ALT0, the three carrier GPIOs ALT5. From the SMARC pinctrl groups. */
    static const uint32_t pads[9][3] = {
        { 0x1A4, PAD_ALT_USDHC2, 0x158e }, /* SD2_CLK   */
        { 0x1A8, PAD_ALT_USDHC2, 0x138e }, /* SD2_CMD   */
        { 0x1AC, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA0 */
        { 0x1B0, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA1 */
        { 0x1B4, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA2 */
        { 0x1B8, PAD_ALT_USDHC2, 0x138e }, /* SD2_DATA3 */
        { 0x1A0, PAD_ALT_GPIO,   0x1100 }, /* SD2_CD_B    -> GPIO3.0  */
        { 0x1BC, PAD_ALT_GPIO,   0x011e }, /* SD2_RESET_B -> GPIO3.7  */
        { 0x154, PAD_ALT_GPIO,   0x0004 }, /* SD2_VSELECT -> GPIO3.19 */
    };
    int i, ret;
    uint32_t pddr, pdor;

    ret = scmi_clock_enable(IMX95_CLK_USDHC2);
    SCMI_DBG("scmi: clock enable(%d) -> %d\n", IMX95_CLK_USDHC2, ret);
    if (ret != 0) {
        wolfBoot_printf("scmi: uSDHC2 clock enable failed (%d)\n", ret);
        return ret;
    }

    for (i = 0; i < 9; i++) {
        ret = scmi_pin_config(pads[i][0], pads[i][1], pads[i][2]);
        SCMI_DBG("scmi: pad 0x%x mux %d -> %d\n",
            (unsigned)pads[i][0], (int)pads[i][1], ret);
        if (ret != 0) {
            wolfBoot_printf("scmi: pad 0x%x config failed (%d)\n",
                (unsigned)pads[i][0], ret);
            return ret;
        }
    }

    /* Card detect as input; voltage-select and power-enable as outputs. */
    pddr = rd32(RGPIO3_PDDR);
    pddr &= ~(1U << GPIO3_CD);
    pddr |= (1U << GPIO3_VSEL) | (1U << GPIO3_PWR);
    wr32(RGPIO3_PDDR, pddr);

    /* Select 3.3V I/O (VSEL low) before powering the card. */
    pdor = rd32(RGPIO3_PDOR);
    pdor &= ~(1U << GPIO3_VSEL);
    wr32(RGPIO3_PDOR, pdor);

    /* Power on, then wait past the regulator startup delay (DTS
     * startup-delay-us 20000). The count is generous enough to clear 20 ms
     * either way: stage 1 runs this with caches off, BL33 with them on. */
    pdor |= (1U << GPIO3_PWR);
    wr32(RGPIO3_PDOR, pdor);
    delay_loops(40000000U);

    SCMI_DBG("scmi: GPIO3 PDDR=0x%x PDOR=0x%x\n",
        (unsigned)rd32(RGPIO3_PDDR), (unsigned)rd32(RGPIO3_PDOR));
    wolfBoot_printf("scmi: uSDHC2 clock+pinmux+power up\n");
    return 0;
}
#endif /* DISK_SDCARD && IMX95_SCMI_COLD_INIT */

#ifdef IMX95_INIT_M7
/* Cortex-M7 TCM system-view bases and size (256 KiB each at TCM_SIZE=000b). */

/* Match U-Boot's power_on_m7(): power up the M7 mix over SCMI, then scrub its TCM
 * so never-written words carry valid ECC. Needed because wolfBoot replaces the
 * U-Boot board init on a board whose M7 Linux remoteproc launches later. */
int imx95_m7_tcm_init(void)
{
    uint32_t p[3];
    volatile uint32_t *w;
    uint32_t i, words;
    int ret;

    /* SCMI power domain: STATE_SET {flags=0, domain_id=M7, pstate=0=on}. */
    p[0] = 0;
    p[1] = IMX95_PD_M7;
    p[2] = 0;
    ret = scmi_cmd(SCMI_PROTO_POWER, PWD_STATE_SET, p, 3, 1);
    SCMI_DBG("scmi: M7 power-on -> %d\n", ret);
    if (ret != 0) {
        wolfBoot_printf("scmi: M7 power domain on failed (%d)\n", ret);
        return ret;
    }

    /* Scrub ITCM + DTCM to initialize ECC. Word writes: the TCM system view is
     * mapped Device, where an unaligned or wider access would fault. */
    words = (uint32_t)(IMX95_M7_TCM_SIZE / 4U);
    w = (volatile uint32_t *)IMX95_M7_ITCM_SYS;
    for (i = 0; i < words; i++)
        w[i] = 0U;
    w = (volatile uint32_t *)IMX95_M7_DTCM_SYS;
    for (i = 0; i < words; i++)
        w[i] = 0U;

    wolfBoot_printf("scmi: M7 powered, TCM ECC initialized\n");
    return 0;
}
#endif /* IMX95_INIT_M7 */

#ifdef IMX95_STAGE1
/* Report what the System Manager currently has a clock running at. */
static uint32_t scmi_clock_rate(uint32_t clock_id)
{
    uint32_t p[3];

    p[0] = clock_id;
    if (scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_RATE_GET, p, 1, 3) != 0)
        return 0;
    return p[1];
}

/* Parent a peripheral clock and give it a rate, then enable it. Enabling alone
 * leaves whatever the previous owner set, which is invisible when a stage runs
 * after U-Boot SPL and fatal when it runs instead of it. */
static int scmi_clock_setup(uint32_t clock_id, uint32_t parent_id,
                            uint32_t rate)
{
    uint32_t p[4];
    int ret;

    p[0] = clock_id;
    p[1] = 0;                  /* attributes: off while reparenting */
    p[2] = 0;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_CONFIG_SET, p, 3, 1);
    if (ret != 0)
        return ret;

    p[0] = clock_id;
    p[1] = parent_id;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_PARENT_SET, p, 2, 1);
    if (ret != 0)
        return ret;

    p[0] = CLOCK_RATE_ROUND_CLOSEST;
    p[1] = clock_id;
    p[2] = rate;
    p[3] = 0;
    ret = scmi_cmd(SCMI_PROTO_CLOCK, CLOCK_RATE_SET, p, 4, 1);
    if (ret != 0)
        return ret;

    return scmi_clock_enable(clock_id);
}

/* uSDHC1 is the on-module eMMC, already used by the ROM, so this only makes the
 * state explicit instead of inheriting it. Eight data lines, soldered down, so
 * no card detect and no card power. */
int imx95_usdhc1_cold_init(void)
{
    static const uint32_t pads[11][2] = {
        { 0x128, 0x158e }, /* SD1_CLK    */
        { 0x12C, 0x138e }, /* SD1_CMD    */
        { 0x130, 0x138e }, /* SD1_DATA0  */
        { 0x134, 0x138e }, /* SD1_DATA1  */
        { 0x138, 0x138e }, /* SD1_DATA2  */
        { 0x13C, 0x138e }, /* SD1_DATA3  */
        { 0x140, 0x138e }, /* SD1_DATA4  */
        { 0x144, 0x138e }, /* SD1_DATA5  */
        { 0x148, 0x138e }, /* SD1_DATA6  */
        { 0x14C, 0x138e }, /* SD1_DATA7  */
        { 0x150, 0x158e }  /* SD1_STROBE */
    };
    uint32_t was;
    int i, ret;

    /* The divider in the uSDHC section below is written against a 400 MHz module
     * clock, which is what U-Boot's init_clk_usdhc() sets. */
    was = scmi_clock_rate(IMX95_CLK_USDHC1);
    ret = scmi_clock_setup(IMX95_CLK_USDHC1, IMX95_CLK_SYSPLL1_PFD1,
                           400000000UL);
    if (ret != 0) {
        wolfBoot_printf("scmi: uSDHC1 clock setup failed (%d)\n", ret);
        return ret;
    }
    wolfBoot_printf("scmi: uSDHC1 clock %u -> %u Hz\n",
        (unsigned)was, (unsigned)scmi_clock_rate(IMX95_CLK_USDHC1));

    for (i = 0; i < 11; i++) {
        ret = scmi_pin_config(pads[i][0], PAD_ALT_USDHC1, pads[i][1]);
        if (ret != 0) {
            wolfBoot_printf("scmi: pad 0x%x config failed (%d)\n",
                (unsigned)pads[i][0], ret);
            return ret;
        }
    }

    wolfBoot_printf("scmi: uSDHC1 clock+pinmux up\n");
    return 0;
}

/* Parent the console UART to the 24 MHz oscillator and enable it. Stage 1 runs
 * before anything else has touched the clock tree, so the console is silent
 * until this has been done. Mirrors U-Boot's init_uart_clk(). */
int imx95_scmi_uart_clk_init(void)
{
    /* SMARC SER1 on the AONMIX LPUART1 pads, both at ALT0. */
    static const uint32_t pads[2][2] = {
        { 0x1D0, 0x31e },  /* UART1_RXD */
        { 0x1D4, 0x31e }   /* UART1_TXD */
    };
    int i, ret;

    for (i = 0; i < 2; i++) {
        ret = scmi_pin_config(pads[i][0], PAD_ALT_LPUART1, pads[i][1]);
        if (ret != 0)
            return ret;
    }

    return scmi_clock_setup(IMX95_CLK_LPUART1, IMX95_CLK_24M, 24000000UL);
}

/* Raise the A55 cluster to its maximum operating point. Nothing before stage 1
 * does this, so without it the whole boot runs at the reset rate. */
int imx95_scmi_arm_max_clk(void)
{
    uint32_t p[2];

    p[0] = IMX95_PERF_DOM_ARM;
    p[1] = IMX95_PERF_LVL_MAX;
    return scmi_cmd(SCMI_PROTO_PERF, PERF_LEVEL_SET, p, 2, 1);
}

/* 1 when the DDR mix is powered, 0 when it is off, negative if the System
 * Manager would not answer. The OEI is what brings DDR up, so an off domain
 * means it did not run and nothing loaded into DRAM would survive. */
int imx95_scmi_ddr_powered(void)
{
    uint32_t p[2];
    int ret;

    p[0] = IMX95_PD_DDR;
    ret = scmi_cmd(SCMI_PROTO_POWER, PWD_STATE_GET, p, 1, 2);
    if (ret != 0)
        return ret;
    /* Reply word 1 is the power state; bit 30 set means off. */
    return ((p[1] & (1UL << 30)) != 0UL) ? 0 : 1;
}
#endif /* IMX95_STAGE1 */

#endif /* IMX95_SCMI_COLD_INIT || IMX95_INIT_M7 || IMX95_STAGE1 */


/* uSDHC: eMMC and carrier SD. A55-only, shared with the stage 1 loader. */

/* Minimal i.MX uSDHC driver (same IP as i.MX 6/7/8), NOT SDHCI-register
 * compatible so src/sdhci.c does not apply. PIO, no DMA/tuning/1.8V, no
 * disk_write(). Module clock and pinmux must already be up. */


#if defined(IMX95_SCMI_COLD_INIT) && defined(DISK_SDCARD)
extern int imx95_usdhc2_cold_init(void);
#endif

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

/* uSDHC1 carries the eMMC on this module, uSDHC2 the carrier SD slot. A build
 * selects one: the two are separate controllers and the driver keeps a single
 * card's state. */
#ifndef USDHC_BASE
#ifdef DISK_EMMC
#define USDHC_BASE          IMX95_USDHC1_BASE   /* eMMC (uSDHC1) */
#else
#define USDHC_BASE          IMX95_USDHC2_BASE   /* carrier SD (uSDHC2) */
#endif
#endif

/* --- uSDHC registers (offsets from the instance base) -------------------- */
#define USDHC_DS_ADDR           0x00    /* DMA system address */
#define USDHC_BLK_ATT           0x04    /* block size / count */
#define USDHC_CMD_ARG           0x08
#define USDHC_CMD_XFR_TYP       0x0C
#define USDHC_CMD_RSP0          0x10
#define USDHC_CMD_RSP1          0x14
#define USDHC_CMD_RSP2          0x18
#define USDHC_CMD_RSP3          0x1C
#define USDHC_DATA_BUFF_ACC     0x20    /* PIO data port */
#define USDHC_PRES_STATE        0x24
#define USDHC_PROT_CTRL         0x28
#define USDHC_SYS_CTRL          0x2C
#define USDHC_INT_STATUS        0x30
#define USDHC_INT_STATUS_EN     0x34
#define USDHC_INT_SIGNAL_EN     0x38
#define USDHC_AUTOCMD12_ERR     0x3C
#define USDHC_HOST_CTRL_CAP     0x40
#define USDHC_WTMK_LVL          0x44
#define USDHC_MIX_CTRL          0x48
#define USDHC_DLL_CTRL          0x60
#define USDHC_CLK_TUNE_CTRL     0x68    /* CLK_TUNE_CTRL_STATUS */
#define USDHC_VEND_SPEC         0xC0
#define USDHC_MMC_BOOT          0xC4
/* VEND_SPEC reset value: clock gates on, 3.3V signaling. */
#define VEND_SPEC_INIT          0x20007809UL
#define VEND_SPEC_FRC_SDCLK_ON  (1UL << 8)
#define VEND_SPEC_IPGEN         (1UL << 11)   /* IPG clock always on */
#define VEND_SPEC_HCKEN         (1UL << 12)   /* AHB clock always on */
#define VEND_SPEC_PEREN         (1UL << 13)   /* peripheral clock on */
#define VEND_SPEC_CKEN          (1UL << 14)   /* SD clock on */

/* CMD_XFR_TYP fields */
#define CMD_XFR_CMDINX(c)       (((uint32_t)(c) & 0x3F) << 24)
#define CMD_XFR_CMDTYP_ABORT    (3UL << 22)
#define CMD_XFR_DPSEL           (1UL << 21)   /* data present */
#define CMD_XFR_CICEN           (1UL << 20)   /* check index */
#define CMD_XFR_CCCEN           (1UL << 19)   /* check CRC */
#define CMD_XFR_RSPTYP_NONE     (0UL << 16)
#define CMD_XFR_RSPTYP_136      (1UL << 16)
#define CMD_XFR_RSPTYP_48       (2UL << 16)
#define CMD_XFR_RSPTYP_48B      (3UL << 16)   /* 48 with busy */

/* MIX_CTRL fields */
#define MIX_CTRL_DMAEN          (1UL << 0)
#define MIX_CTRL_BCEN           (1UL << 1)    /* block count enable */
#define MIX_CTRL_AC12EN         (1UL << 2)    /* auto CMD12 */
#define MIX_CTRL_DTDSEL_READ    (1UL << 4)    /* data direction: read */
#define MIX_CTRL_MSBSEL         (1UL << 5)    /* multi block */

/* PRES_STATE fields */
#define PRES_CIHB               (1UL << 0)    /* command inhibit (cmd line) */
#define PRES_CDIHB              (1UL << 1)    /* command inhibit (data line) */
#define PRES_DLA                (1UL << 2)    /* data line active */
#define PRES_SDSTB              (1UL << 3)    /* SD clock stable */
#define PRES_BREN               (1UL << 11)   /* buffer read enable */
#define PRES_CINST              (1UL << 16)   /* card inserted */

/* PROT_CTRL fields */
#define PROT_CTRL_DTW_MASK      (3UL << 1)
#define PROT_CTRL_DTW_1BIT      (0UL << 1)
#define PROT_CTRL_DTW_4BIT      (1UL << 1)
#define PROT_CTRL_EMODE_LE      (2UL << 4)    /* little-endian mode */
#define PROT_CTRL_CDTL          (1UL << 6)    /* card detect test level */
#define PROT_CTRL_CDSS          (1UL << 7)    /* card detect source = test */

/* SYS_CTRL fields */
#define SYS_CTRL_DVS_SHIFT      4             /* divisor: 1..16 */
#define SYS_CTRL_SDCLKFS_SHIFT  8             /* prescaler: 2^n */
#define SYS_CTRL_DTOCV_SHIFT    16            /* data timeout counter */
#define SYS_CTRL_INITA          (1UL << 27)   /* send 80 init clocks */
#define SYS_CTRL_RSTA           (1UL << 24)   /* reset all */
#define SYS_CTRL_RSTC           (1UL << 25)   /* reset cmd */
#define SYS_CTRL_RSTD           (1UL << 26)   /* reset data */
#define SYS_CTRL_RSTT           (1UL << 28)   /* reset tuning */

/* INT_STATUS fields */
#define INT_CC                  (1UL << 0)    /* command complete */
#define INT_TC                  (1UL << 1)    /* transfer complete */
#define INT_BRR                 (1UL << 5)    /* buffer read ready */
#define INT_CTOE                (1UL << 16)   /* command timeout */
#define INT_CCE                 (1UL << 17)   /* command CRC error */
#define INT_CEBE                (1UL << 18)   /* command end bit error */
#define INT_CIE                 (1UL << 19)   /* command index error */
#define INT_DTOE                (1UL << 20)   /* data timeout */
#define INT_DCE                 (1UL << 21)   /* data CRC error */
#define INT_DEBE                (1UL << 22)   /* data end bit error */
#define INT_CMD_ERRS            (INT_CTOE | INT_CCE | INT_CEBE | INT_CIE)
#define INT_DATA_ERRS           (INT_DTOE | INT_DCE | INT_DEBE)

/* SD commands used */
#define SD_CMD0_GO_IDLE         0
#define SD_CMD2_ALL_SEND_CID    2
#define SD_CMD3_SEND_REL_ADDR   3
#define SD_CMD6_SWITCH          6
#define MMC_CMD1_SEND_OP_COND   1
#define MMC_CMD8_SEND_EXT_CSD   8
#define SD_CMD7_SELECT          7
#define SD_CMD8_SEND_IF_COND    8
#define SD_CMD9_SEND_CSD        9
#define SD_CMD12_STOP           12
#define SD_CMD16_SET_BLOCKLEN   16
#define SD_CMD17_READ_SINGLE    17
#define SD_CMD18_READ_MULTIPLE  18
#define SD_CMD55_APP_CMD        55
#define SD_ACMD6_SET_BUS_WIDTH  6
#define SD_ACMD41_OP_COND       41

#define SD_BLOCK_SIZE           512
/* Multi-block cap: BLK_ATT count is 16-bit; stay well under it. */
#define SD_MAX_BLOCKS           1024

#define USDHC_TIMEOUT_LOOPS     1000000

/* Build with -DIMX95_SCMI_DEBUG for a per-step trace of the SD bring-up. */
#ifdef IMX95_SCMI_DEBUG
#define DISK_DBG(...) wolfBoot_printf(__VA_ARGS__)
#else
#define DISK_DBG(...) do { } while (0)
#endif

static int card_rca;        /* relative card address, from CMD3 */
static int card_high_cap;   /* SDHC/SDXC: block addressing */
static int card_ready;

/* Which controller the calls below talk to. A variable rather than the macro
 * so one image can reach both instances - the eMMC on uSDHC1 and the carrier
 * SD on uSDHC2 are the same IP at different addresses. */
static uintptr_t usdhc_base = USDHC_BASE;

static inline uint32_t usdhc_rd(uint32_t off)
{
    return *(volatile uint32_t*)(usdhc_base + off);
}

static inline void usdhc_wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t*)(usdhc_base + off) = v;
}

/* --- Low level ----------------------------------------------------------- */

static int usdhc_wait_clear(uint32_t off, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        if ((usdhc_rd(off) & mask) == 0)
            return 0;
    }
    return -1;
}

static int usdhc_wait_set(uint32_t off, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        if ((usdhc_rd(off) & mask) != 0)
            return 0;
    }
    return -1;
}

static void usdhc_reset(uint32_t bits)
{
    usdhc_wr(USDHC_SYS_CTRL, usdhc_rd(USDHC_SYS_CTRL) | bits);
    (void)usdhc_wait_clear(USDHC_SYS_CTRL, bits);
}

/* Divider = prescaler (2^n, field 0x01..0x80) x divisor (1..16). At the
 * 400 MHz module clock default: /256/4 = ~390 kHz identification, /2/8 =
 * 25 MHz default speed, /2/4 = 50 MHz once the card is in high-speed mode. */
#define USDHC_CLK_ID    0
#define USDHC_CLK_25MHZ 1
#define USDHC_CLK_50MHZ 2

/* RSTA leaves the vendor registers alone, so a controller the boot ROM already
 * drove stays in fast-boot mode with its HS400 tuning and the first command
 * never completes. U-Boot's esdhc_init() restores the same five by hand. */
/* The 80 startup clocks a card needs before its first command. The SD clock is
 * forced on across it: INITA alone is not enough after the boot ROM. */
static void usdhc_init_clocks(void)
{
    volatile uint32_t d;

    usdhc_wr(USDHC_VEND_SPEC, usdhc_rd(USDHC_VEND_SPEC) | VEND_SPEC_FRC_SDCLK_ON);
    usdhc_wr(USDHC_SYS_CTRL, usdhc_rd(USDHC_SYS_CTRL) | SYS_CTRL_INITA);
    (void)usdhc_wait_clear(USDHC_SYS_CTRL, SYS_CTRL_INITA);
    /* Belt to the INITA self-clear above, which is the real 80-clock wait.
     * Generous either way: stage 1 runs uncached, BL33 cached. */
    for (d = 0; d < 200000U; d++) { }
    usdhc_wr(USDHC_VEND_SPEC, usdhc_rd(USDHC_VEND_SPEC) & ~VEND_SPEC_FRC_SDCLK_ON);
}

static void usdhc_vendor_reset(void)
{
    usdhc_wr(USDHC_MMC_BOOT, 0);
    usdhc_wr(USDHC_MIX_CTRL, 0);
    usdhc_wr(USDHC_CLK_TUNE_CTRL, 0);
    usdhc_wr(USDHC_DLL_CTRL, 0);
    usdhc_wr(USDHC_VEND_SPEC, VEND_SPEC_INIT);
}

static void usdhc_set_clock(int speed)
{
    uint32_t v;

    v = usdhc_rd(USDHC_SYS_CTRL);
    v &= ~((0xFFUL << SYS_CTRL_SDCLKFS_SHIFT) | (0xFUL << SYS_CTRL_DVS_SHIFT)
           | (0xFUL << SYS_CTRL_DTOCV_SHIFT));
    if (speed == USDHC_CLK_ID) {
        v |= (0x80UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /256 */
        v |= (0x3UL << SYS_CTRL_DVS_SHIFT);       /* /4  -> ~390 kHz */
    }
    else if (speed == USDHC_CLK_50MHZ) {
        v |= (0x01UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /2 */
        v |= (0x3UL << SYS_CTRL_DVS_SHIFT);       /* /4  -> 50 MHz */
    }
    else {
        v |= (0x01UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /2 */
        v |= (0x7UL << SYS_CTRL_DVS_SHIFT);       /* /8  -> 25 MHz */
    }
    v |= (0xEUL << SYS_CTRL_DTOCV_SHIFT);         /* max data timeout */
    usdhc_wr(USDHC_SYS_CTRL, v);
    /* Enable the IPG/AHB/peripheral/SD clock gates: after a cold reset these
     * are off, so SDSTB never sets and no command can run. (A warm handoff
     * from a prior stage leaves them on; setting them again is harmless.) */
    usdhc_wr(USDHC_VEND_SPEC, usdhc_rd(USDHC_VEND_SPEC) |
        VEND_SPEC_IPGEN | VEND_SPEC_HCKEN | VEND_SPEC_PEREN | VEND_SPEC_CKEN);
    (void)usdhc_wait_set(USDHC_PRES_STATE, PRES_SDSTB);
}

/* Send a command; response left in CMD_RSP0..3. Returns 0 on success. */
static int usdhc_cmd(uint32_t idx, uint32_t arg, int rsp136, int rsp_busy,
                     int data, int multi, int check_crc_idx)
{
    uint32_t xfr;
    uint32_t st;
    uint32_t n;

    if (usdhc_wait_clear(USDHC_PRES_STATE, PRES_CIHB) != 0)
        return -1;
    if ((data || rsp_busy) &&
        usdhc_wait_clear(USDHC_PRES_STATE, PRES_CDIHB) != 0)
        return -1;

    /* Clear stale status */
    usdhc_wr(USDHC_INT_STATUS, 0xFFFFFFFFUL);

    /* MIX_CTRL first: the CMD_XFR_TYP write launches the command. */
    if (data) {
        uint32_t mix = MIX_CTRL_DTDSEL_READ;
        if (multi)
            mix |= MIX_CTRL_MSBSEL | MIX_CTRL_BCEN | MIX_CTRL_AC12EN;
        usdhc_wr(USDHC_MIX_CTRL, mix);
    }
    else {
        usdhc_wr(USDHC_MIX_CTRL, 0);
    }

    xfr = CMD_XFR_CMDINX(idx);
    if (rsp136)
        xfr |= CMD_XFR_RSPTYP_136;
    else if (rsp_busy)
        xfr |= CMD_XFR_RSPTYP_48B;
    else if (idx == SD_CMD0_GO_IDLE)
        xfr |= CMD_XFR_RSPTYP_NONE;   /* CMD0 has no response */
    else
        xfr |= CMD_XFR_RSPTYP_48;
    if (check_crc_idx)
        xfr |= CMD_XFR_CICEN | CMD_XFR_CCCEN;
    if (data)
        xfr |= CMD_XFR_DPSEL;

    usdhc_wr(USDHC_CMD_ARG, arg);
    usdhc_wr(USDHC_CMD_XFR_TYP, xfr);

    /* Wait for command complete or error */
    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        st = usdhc_rd(USDHC_INT_STATUS);
        if (st & INT_CMD_ERRS) {
            usdhc_wr(USDHC_INT_STATUS, INT_CMD_ERRS | INT_CC);
            usdhc_reset(SYS_CTRL_RSTC);
            return (st & INT_CTOE) ? -2 : -3;
        }
        if (st & INT_CC) {
            usdhc_wr(USDHC_INT_STATUS, INT_CC);
            return 0;
        }
    }
    usdhc_reset(SYS_CTRL_RSTC);
    return -1;
}

/* --- Card bring-up ------------------------------------------------------- */

/* CMD6 SWITCH_FUNC, mode 1, group 1 = high speed. The 512-bit status answers with
 * the function actually selected in bits 379:376 - the low nibble of the 17th
 * byte. Returns 0 only when the card confirms it. */
static int sd_switch_high_speed(void)
{
    uint32_t sw[16];
    const uint8_t *b = (const uint8_t *)sw;
    uint32_t n, st, i;

    usdhc_wr(USDHC_BLK_ATT, (1UL << 16) | 64UL);
    usdhc_wr(USDHC_WTMK_LVL, (16UL << 16) | 16UL);

    if (usdhc_cmd(SD_CMD6_SWITCH, 0x80FFFFF1UL, 0, 0, 1, 0, 1) != 0)
        goto restore;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        st = usdhc_rd(USDHC_INT_STATUS);
        if (st & INT_DATA_ERRS) {
            usdhc_wr(USDHC_INT_STATUS, INT_DATA_ERRS);
            goto restore;
        }
        if (usdhc_rd(USDHC_PRES_STATE) & PRES_BREN)
            break;
    }
    if (n == USDHC_TIMEOUT_LOOPS)
        goto restore;
    usdhc_wr(USDHC_INT_STATUS, INT_BRR);
    for (i = 0; i < 16; i++)
        sw[i] = usdhc_rd(USDHC_DATA_BUFF_ACC);
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0)
        goto restore;
    usdhc_wr(USDHC_INT_STATUS, INT_TC);

    /* Restore the block geometry the read path expects. */
    usdhc_wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    usdhc_wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    return ((b[16] & 0x0FU) == 1U) ? 0 : -1;

restore:
    usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
    usdhc_wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    usdhc_wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    return -1;
}

#if defined(DISK_EMMC) || defined(IMX95_EMMC_PROBE)

/* EXT_CSD byte 179: [2:0] picks the partition ordinary reads address, [5:3] the
 * one the boot ROM loads from. Only the access bits are touched here - the
 * enable bits would change where the SoC boots from next reset. */
#define EXT_CSD_PARTITION_CONFIG    179
#define EXT_CSD_BUS_WIDTH           183
#define EXT_CSD_PART_ACCESS_MASK    0x07U
#define EXT_CSD_BUS_WIDTH_4BIT      1
#define MMC_SWITCH_WRITE_BYTE       (3UL << 24)

static uint8_t emmc_part_config;    /* EXT_CSD[179] as read at init */

/* CMD6 in the "write byte" form: index in [23:16], value in [15:8]. R1b, so
 * the card holds DAT0 low while it applies the change. */
static int emmc_switch(uint32_t index, uint32_t value)
{
    uint32_t arg = MMC_SWITCH_WRITE_BYTE | (index << 16) | (value << 8);

    return usdhc_cmd(SD_CMD6_SWITCH, arg, 0, 1, 0, 0, 1);
}

/* CMD8 on eMMC returns the 512-byte EXT_CSD as a data block, unlike the SD
 * CMD8 which is an interface-condition check with no data. */
static int emmc_read_ext_csd(uint8_t *buf)
{
    uint32_t n, st, i;
    uint32_t *out = (uint32_t *)(void *)buf;

    usdhc_wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    if (usdhc_cmd(MMC_CMD8_SEND_EXT_CSD, 0, 0, 0, 1, 0, 1) != 0)
        return -1;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        st = usdhc_rd(USDHC_INT_STATUS);
        if (st & INT_DATA_ERRS) {
            usdhc_wr(USDHC_INT_STATUS, INT_DATA_ERRS);
            usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
            return -1;
        }
        if (usdhc_rd(USDHC_PRES_STATE) & PRES_BREN)
            break;
    }
    if (n == USDHC_TIMEOUT_LOOPS)
        return -1;
    usdhc_wr(USDHC_INT_STATUS, INT_BRR);
    for (i = 0; i < SD_BLOCK_SIZE / 4; i++)
        out[i] = usdhc_rd(USDHC_DATA_BUFF_ACC);
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0)
        return -1;
    usdhc_wr(USDHC_INT_STATUS, INT_TC);
    return 0;
}

/* Point ordinary reads at the user area (0), boot0 (1) or boot1 (2). The boot
 * partitions are where the SoC's own boot containers live, so this is what
 * lets wolfBoot read them. */
int imx95_emmc_select_partition(int part)
{
    uint8_t cfg;

    if (!card_ready)
        return -1;
    if (part < 0 || part > 2)
        return -1;

    cfg = (uint8_t)((emmc_part_config & (uint8_t)~EXT_CSD_PART_ACCESS_MASK) |
                    (uint8_t)part);
    if (emmc_switch(EXT_CSD_PARTITION_CONFIG, cfg) != 0) {
        wolfBoot_printf("emmc: partition switch to %d failed\n", part);
        return -1;
    }
    emmc_part_config = cfg;
    return 0;
}

/* Which boot partition the SoC loads from, 1 for boot0 or 2 for boot1, taken
 * from the enable field the ROM itself reads. 0 means none is enabled. */
int imx95_emmc_boot_partition(void)
{
    if (!card_ready)
        return -1;
    return (int)((emmc_part_config >> 3) & 0x07U);
}

static int emmc_card_init(void)
{
    static uint8_t ext_csd[SD_BLOCK_SIZE] __attribute__((aligned(4)));
    uint32_t rsp;
    uint32_t n;
    int ret;

    card_rca = 1;           /* the host assigns it on eMMC, unlike SD */
    card_high_cap = 0;

    usdhc_reset(SYS_CTRL_RSTA | SYS_CTRL_RSTT);
    usdhc_vendor_reset();
    usdhc_wr(USDHC_PROT_CTRL, PROT_CTRL_EMODE_LE | PROT_CTRL_DTW_1BIT |
                        PROT_CTRL_CDTL | PROT_CTRL_CDSS);
    usdhc_wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    usdhc_wr(USDHC_INT_STATUS_EN, 0xFFFFFFFFUL);
    usdhc_wr(USDHC_INT_SIGNAL_EN, 0);

    usdhc_set_clock(USDHC_CLK_ID);
    usdhc_init_clocks();

    DISK_DBG("emmc: SYS_CTRL=%08x PRES=%08x VEND=%08x\n",
        (unsigned)usdhc_rd(USDHC_SYS_CTRL), (unsigned)usdhc_rd(USDHC_PRES_STATE),
        (unsigned)usdhc_rd(USDHC_VEND_SPEC));

    (void)usdhc_cmd(SD_CMD0_GO_IDLE, 0, 0, 0, 0, 0, 0);

    /* CMD1 rather than ACMD41, and with the sector-address bit set: parts this
     * size are always block addressed, and asking for byte addressing would be
     * refused. R3 carries no CRC. */
    rsp = 0;
    for (n = 0; n < 1000; n++) {
        ret = usdhc_cmd(MMC_CMD1_SEND_OP_COND, 0x40FF8080UL, 0, 0, 0, 0, 0);
        if (ret != 0) {
            wolfBoot_printf("emmc: CMD1 failed (%d) PRES=%08x INT=%08x\n",
                ret, (unsigned)usdhc_rd(USDHC_PRES_STATE),
                (unsigned)usdhc_rd(USDHC_INT_STATUS));
            return -1;
        }
        rsp = usdhc_rd(USDHC_CMD_RSP0);
        if (rsp & 0x80000000UL)
            break;
    }
    if (!(rsp & 0x80000000UL)) {
        wolfBoot_printf("emmc: card stuck busy in CMD1 (OCR 0x%08x)\n",
            (unsigned)rsp);
        return -1;
    }
    card_high_cap = (rsp & 0x40000000UL) ? 1 : 0;

    ret = usdhc_cmd(SD_CMD2_ALL_SEND_CID, 0, 1, 0, 0, 0, 0);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD2 failed (%d)\n", ret);
        return -1;
    }
    /* The host chooses the address on eMMC and tells the card. */
    ret = usdhc_cmd(SD_CMD3_SEND_REL_ADDR, (uint32_t)card_rca << 16,
                    0, 0, 0, 0, 1);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD3 failed (%d)\n", ret);
        return -1;
    }
    (void)usdhc_cmd(SD_CMD9_SEND_CSD, (uint32_t)card_rca << 16, 1, 0, 0, 0, 0);
    ret = usdhc_cmd(SD_CMD7_SELECT, (uint32_t)card_rca << 16, 0, 1, 0, 0, 1);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD7 failed (%d)\n", ret);
        return -1;
    }

    usdhc_set_clock(USDHC_CLK_25MHZ);

    /* 4-bit: the SMARC carrier routes four eMMC data lines, and the wider bus
     * is a device-side setting the card has to be told about. */
    if (emmc_switch(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4BIT) == 0) {
        usdhc_wr(USDHC_PROT_CTRL,
           (usdhc_rd(USDHC_PROT_CTRL) & ~PROT_CTRL_DTW_MASK) | PROT_CTRL_DTW_4BIT);
    }

    (void)usdhc_cmd(SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, 0, 0, 0, 0, 1);

    if (emmc_read_ext_csd(ext_csd) != 0) {
        wolfBoot_printf("emmc: EXT_CSD read failed\n");
        return -1;
    }
    emmc_part_config = ext_csd[EXT_CSD_PARTITION_CONFIG];

    wolfBoot_printf("emmc: ready, rca=0x%x part_config=0x%02x\n",
        (unsigned)card_rca, (unsigned)emmc_part_config);
    return 0;
}

#endif /* DISK_EMMC || IMX95_EMMC_PROBE */

static int sd_card_init(void)
{
    uint32_t rsp;
    uint32_t n;
    int ret;

    card_rca = 0;
    card_high_cap = 0;

    usdhc_reset(SYS_CTRL_RSTA | SYS_CTRL_RSTT);
    usdhc_vendor_reset();

    /* CDTL+CDSS force card-present: boards routing CD to a GPIO leave the
     * dedicated CD pad floating, so CINST never sets. The card answering
     * CMD8/ACMD41 is the real presence check. */
    usdhc_wr(USDHC_PROT_CTRL, PROT_CTRL_EMODE_LE | PROT_CTRL_DTW_1BIT |
                        PROT_CTRL_CDTL | PROT_CTRL_CDSS);
    /* PIO watermark: one 512-byte block = 128 words on both sides */
    usdhc_wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    /* Enable status bits (polled; signals stay off) */
    usdhc_wr(USDHC_INT_STATUS_EN, 0xFFFFFFFFUL);
    usdhc_wr(USDHC_INT_SIGNAL_EN, 0);

    usdhc_set_clock(USDHC_CLK_ID);

    DISK_DBG("usdhc: after clk SYS_CTRL=%08x PRES=%08x (SDSTB=%d CINST=%d) CAP=%08x\n",
        (unsigned)usdhc_rd(USDHC_SYS_CTRL), (unsigned)usdhc_rd(USDHC_PRES_STATE),
        (int)((usdhc_rd(USDHC_PRES_STATE) >> 3) & 1U),
        (int)((usdhc_rd(USDHC_PRES_STATE) >> 16) & 1U),
        (unsigned)usdhc_rd(USDHC_HOST_CTRL_CAP));

    /* Emit the 80 startup clocks a cold card needs before CMD0/CMD8. */
    usdhc_init_clocks();

    /* CMD0: idle */
    (void)usdhc_cmd(SD_CMD0_GO_IDLE, 0, 0, 0, 0, 0, 0);

    /* CMD8 voltage check (2.7-3.6V, pattern 0xAA). SD v1 not supported. */
    ret = usdhc_cmd(SD_CMD8_SEND_IF_COND, 0x1AA, 0, 0, 0, 0, 1);
    DISK_DBG("usdhc: CMD8 -> %d RSP0=%08x\n", ret, (unsigned)usdhc_rd(USDHC_CMD_RSP0));
    if (ret != 0) {
        wolfBoot_printf("usdhc: CMD8 failed (%d) - SD v1 card?\n", ret);
        return -1;
    }
    if ((usdhc_rd(USDHC_CMD_RSP0) & 0xFF) != 0xAA) {
        wolfBoot_printf("usdhc: CMD8 pattern mismatch\n");
        return -1;
    }

    /* ACMD41 with HCS until the card leaves busy (bit 31 set). */
    for (n = 0; n < 1000; n++) {
        ret = usdhc_cmd(SD_CMD55_APP_CMD, 0, 0, 0, 0, 0, 1);
        if (ret != 0)
            return -1;
        /* ACMD41 response (R3) has no CRC; disable the checks */
        ret = usdhc_cmd(SD_ACMD41_OP_COND, 0x40300000UL, 0, 0, 0, 0, 0);
        if (ret != 0)
            return -1;
        rsp = usdhc_rd(USDHC_CMD_RSP0);
        if (rsp & 0x80000000UL)
            break;
    }
    DISK_DBG("usdhc: ACMD41 done after %u loops, OCR=%08x\n",
        (unsigned)n, (unsigned)rsp);
    if (!(rsp & 0x80000000UL)) {
        wolfBoot_printf("usdhc: card stuck busy in ACMD41\n");
        return -1;
    }
    card_high_cap = (rsp & 0x40000000UL) ? 1 : 0;

    /* CMD2 (CID, R2/136) then CMD3 (RCA) */
    if (usdhc_cmd(SD_CMD2_ALL_SEND_CID, 0, 1, 0, 0, 0, 0) != 0)
        return -1;
    if (usdhc_cmd(SD_CMD3_SEND_REL_ADDR, 0, 0, 0, 0, 0, 1) != 0)
        return -1;
    card_rca = (int)(usdhc_rd(USDHC_CMD_RSP0) >> 16);

    /* CMD9 (CSD): unparsed, but some cards require it before select */
    (void)usdhc_cmd(SD_CMD9_SEND_CSD, (uint32_t)card_rca << 16, 1, 0, 0, 0, 0);

    /* CMD7: select the card (R1b) */
    if (usdhc_cmd(SD_CMD7_SELECT, (uint32_t)card_rca << 16, 0, 1, 0, 0, 1)
        != 0)
        return -1;

    /* Transfer clock, then 4-bit bus (ACMD6 arg 2) */
    usdhc_set_clock(USDHC_CLK_25MHZ);
    if (usdhc_cmd(SD_CMD55_APP_CMD, (uint32_t)card_rca << 16, 0, 0, 0, 0, 1)
        != 0)
        return -1;
    if (usdhc_cmd(SD_ACMD6_SET_BUS_WIDTH, 2, 0, 0, 0, 0, 1) != 0)
        return -1;
    usdhc_wr(USDHC_PROT_CTRL,
       (usdhc_rd(USDHC_PROT_CTRL) & ~PROT_CTRL_DTW_MASK) | PROT_CTRL_DTW_4BIT);
    /* keep CDTL/CDSS asserted; RSTA is the only thing that clears them */

    /* CMD16: 512-byte blocks (no-op for high capacity, harmless) */
    (void)usdhc_cmd(SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, 0, 0, 0, 0, 1);

    /* High speed doubles the ceiling to 50 MHz. Switch the controller only
     * after the card confirms CMD6, because driving 50 MHz at a card still in
     * default speed corrupts data silently; one that declines stays at 25. */
    if (sd_switch_high_speed() == 0) {
        usdhc_set_clock(USDHC_CLK_50MHZ);
        wolfBoot_printf("usdhc: high speed (50 MHz)\n");
    }

    wolfBoot_printf("usdhc: SD card ready, rca=0x%x %s\n",
        (unsigned)card_rca, card_high_cap ? "(high capacity)" : "");
    return 0;
}

/* Read 'blocks' full blocks starting at 'lba' into buf via PIO. */
/* Destination must be 4-byte aligned: PIO drains the FIFO as words and this
 * stage is built -mstrict-align, where unaligned stores fault. disk_read()
 * routes unaligned callers through the bounce block. */
static int sd_read_blocks(uint32_t lba, uint32_t blocks, uint8_t *buf)
{
    uint32_t arg;
    uint32_t *out = (uint32_t*)(void*)buf;
    uint32_t b, w, st, n;
    int multi = (blocks > 1);
    int cmd = multi ? SD_CMD18_READ_MULTIPLE : SD_CMD17_READ_SINGLE;

    /* A standard-capacity card takes a byte address, so its last addressable
     * block is the one whose byte offset still fits the 32-bit argument.
     * Refuse rather than wrap into an unrelated sector. */
    if (!card_high_cap) {
        if (lba > (0xFFFFFFFFUL / SD_BLOCK_SIZE))
            return -1;
        arg = lba * SD_BLOCK_SIZE;
    }
    else {
        arg = lba;
    }

    usdhc_wr(USDHC_BLK_ATT, ((uint32_t)blocks << 16) | SD_BLOCK_SIZE);

    if (usdhc_cmd((uint32_t)cmd, arg, 0, 0, 1, multi, 1) != 0)
        return -1;

    for (b = 0; b < blocks; b++) {
        /* Wait for one block in the FIFO */
        for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
            st = usdhc_rd(USDHC_INT_STATUS);
            if (st & INT_DATA_ERRS) {
                usdhc_wr(USDHC_INT_STATUS, INT_DATA_ERRS);
                usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
                return -1;
            }
            if (usdhc_rd(USDHC_PRES_STATE) & PRES_BREN)
                break;
        }
        if (n == USDHC_TIMEOUT_LOOPS) {
            usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
            return -1;
        }
        usdhc_wr(USDHC_INT_STATUS, INT_BRR);
        for (w = 0; w < SD_BLOCK_SIZE / 4; w++)
            *out++ = usdhc_rd(USDHC_DATA_BUFF_ACC);
    }

    /* Transfer complete (auto CMD12 covers the multi-block stop) */
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0) {
        usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
        return -1;
    }
    usdhc_wr(USDHC_INT_STATUS, INT_TC);
    return 0;
}

/* --- wolfBoot disk interface --------------------------------------------- */

#ifdef IMX95_COLD_PROBE
/* Cold-state prober: report exactly what a cold uSDHC2 needs, one marker
 * line before each access so a TRDC abort (caught by the debug vectors)
 * names the blocked register. Debug builds only. */
static void probe_delay(uint32_t loops)
{
    volatile uint32_t n = loops;
    while (n-- > 0U) { __asm__ volatile("nop"); }
}

void imx95_usdhc_cold_probe(void)
{
    static const uint32_t mux_off[6] =
        { 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8 };
    static const uint32_t cfg_off[6] =
        { 0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc };
    uint32_t i, v;

    wolfBoot_printf("probe: SYS_CTRL=%08x PRES=%08x CAP=%08x\n",
        (unsigned)usdhc_rd(USDHC_SYS_CTRL), (unsigned)usdhc_rd(USDHC_PRES_STATE),
        (unsigned)usdhc_rd(USDHC_HOST_CTRL_CAP));
    usdhc_reset(SYS_CTRL_RSTA);
    usdhc_set_clock(USDHC_CLK_ID);
    wolfBoot_printf("probe: after clk PRES=%08x (SDSTB=%d)\n",
        (unsigned)usdhc_rd(USDHC_PRES_STATE),
        (int)((usdhc_rd(USDHC_PRES_STATE) >> 3) & 1U));

    wolfBoot_printf("probe: reading IOMUXC\n");
    for (i = 0; i < 6U; i++) {
        v = *(volatile uint32_t*)(IMX95_IOMUXC_BASE + mux_off[i]);
        wolfBoot_printf("probe: mux[%08x]=%08x cfg=%08x\n",
            (unsigned)mux_off[i], (unsigned)v,
            (unsigned)*(volatile uint32_t*)(IMX95_IOMUXC_BASE + cfg_off[i]));
    }

    wolfBoot_printf("probe: writing IOMUXC (mode0, conf 0x138e/0x158e)\n");
    for (i = 0; i < 6U; i++) {
        *(volatile uint32_t*)(IMX95_IOMUXC_BASE + mux_off[i]) = 0U;
        *(volatile uint32_t*)(IMX95_IOMUXC_BASE + cfg_off[i]) =
            (i == 0U) ? 0x158eU : 0x138eU;   /* CLK gets the stronger conf */
    }
    wolfBoot_printf("probe: mux write ok, readback mux[0]=%08x\n",
        (unsigned)*(volatile uint32_t*)(IMX95_IOMUXC_BASE + 0x1a4));

    wolfBoot_printf("probe: reading GPIO3\n");
    wolfBoot_printf("probe: PDOR=%08x PDDR=%08x\n",
        (unsigned)*(volatile uint32_t*)(IMX95_GPIO3_BASE + 0x40),
        (unsigned)*(volatile uint32_t*)(IMX95_GPIO3_BASE + 0x54));
    wolfBoot_printf("probe: card power on (GPIO3.7)\n");
    *(volatile uint32_t*)(IMX95_GPIO3_BASE + 0x54) |= (1U << 7);  /* PDDR out */
    *(volatile uint32_t*)(IMX95_GPIO3_BASE + 0x44)  = (1U << 7);  /* PSOR high */
    probe_delay(20000000U);  /* > startup-delay-us at ~1.8 GHz */

    wolfBoot_printf("probe: retry enumeration\n");
    card_ready = 0;
    if (sd_card_init() == 0) {
        wolfBoot_printf("probe: SD ENUMERATED after cold init\n");
        card_ready = 1;
    }
    else {
        wolfBoot_printf("probe: still failing after pinmux+power\n");
    }
}
#endif /* IMX95_COLD_PROBE */

#ifdef IMX95_EMMC_PROBE
/* Block-granular read of the currently selected eMMC partition, in the shape
 * the container parser asks for. */
static int emmc_ahab_read(void *ctx, uint32_t off, uint32_t len, void *buf)
{
    (void)ctx;
    if ((off % SD_BLOCK_SIZE) != 0U || (len % SD_BLOCK_SIZE) != 0U)
        return -1;
    return sd_read_blocks(off / SD_BLOCK_SIZE, len / SD_BLOCK_SIZE,
                          (uint8_t *)buf);
}

/* Walk the SoC's boot containers in an eMMC boot partition and report them. Runs
 * inside an SD-booting image and restores the SD selection after, so a failure
 * cannot disturb the boot in progress. Nothing is loaded. */
void imx95_emmc_probe(void)
{
    static struct imx95_ahab_container ctnr;
    uintptr_t saved_base = usdhc_base;
    int saved_ready = card_ready;
    int saved_rca = card_rca;
    int saved_cap = card_high_cap;
    /* emmc_card_init() and imx95_emmc_select_partition() below both rewrite
     * this, so it belongs in the same save/restore as the rest. */
    uint8_t saved_part_cfg = emmc_part_config;
    uint32_t off, i;
    int part, n;

    usdhc_base = IMX95_USDHC1_BASE;
    card_ready = 0;

    if (emmc_card_init() != 0) {
        wolfBoot_printf("emmc probe: init failed\n");
        goto restore;
    }
    card_ready = 1;

    for (part = 1; part <= 2; part++) {
        if (imx95_emmc_select_partition(part) != 0)
            continue;
        off = IMX95_AHAB_MMC_OFFSET;
        for (n = 0; n < 2; n++) {
            if (imx95_ahab_parse(&ctnr, off, emmc_ahab_read, NULL) != 0) {
                wolfBoot_printf("emmc probe: boot%d no container at 0x%x\n",
                    part - 1, (unsigned)off);
                break;
            }
            wolfBoot_printf("emmc probe: boot%d ctnr%d @0x%x size=0x%x images=%u\n",
                part - 1, n, (unsigned)ctnr.base, (unsigned)ctnr.size,
                (unsigned)ctnr.count);
            for (i = 0; i < ctnr.count; i++) {
                wolfBoot_printf("  img%u +0x%x size=0x%x dst=0x%x core=%u\n",
                    (unsigned)i, (unsigned)ctnr.img[i].offset,
                    (unsigned)ctnr.img[i].size,
                    (unsigned)ctnr.img[i].dst,
                    (unsigned)IMX95_AHAB_CORE(ctnr.img[i].flags));
            }
            if (imx95_ahab_next(&ctnr, &off) != 0) {
                wolfBoot_printf("  no container follows this one\n");
                break;
            }
        }
    }
    (void)imx95_emmc_select_partition(0);

restore:
    usdhc_base = saved_base;
    card_ready = saved_ready;
    card_rca = saved_rca;
    card_high_cap = saved_cap;
    emmc_part_config = saved_part_cfg;
}
#endif /* IMX95_EMMC_PROBE */

int disk_init(int drv)
{
    (void)drv;
    if (card_ready)
        return 0;
#if defined(IMX95_SCMI_COLD_INIT) && defined(DISK_SDCARD)
    /* Brings up uSDHC2's clock, pads and card power. uSDHC1 (eMMC) would need
     * its own equivalent to run from a cold power-on; today an eMMC build
     * relies on the stage ahead of wolfBoot having initialized it. */
    if (imx95_usdhc2_cold_init() != 0)
        return -1;
#endif
#ifdef IMX95_COLD_PROBE
    imx95_usdhc_cold_probe();
    if (card_ready)
        return 0;
#endif
#ifdef IMX95_EMMC_PROBE
    imx95_emmc_probe();
#endif
#ifdef DISK_EMMC
    if (emmc_card_init() != 0)
        return -1;
#else
    if (sd_card_init() != 0)
        return -1;
#endif
    card_ready = 1;
    return 0;
}

/* Byte-addressed (src/disk.c passes byte offsets); unaligned head/tail go
 * through a bounce block. */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    static uint8_t bounce[SD_BLOCK_SIZE] __attribute__((aligned(4)));
    uint32_t lba, off, chunk, blocks;
    uint32_t done = 0;
    uint32_t i;
    uint64_t lba64;

    (void)drv;
    if (!card_ready)
        return -1;
    /* The byte count comes back through an int, so a request too large to
     * report must be refused rather than answered with a negative length. */
    if (count > (uint32_t)INT_MAX)
        return -1;

    while (done < count) {
        /* The card command argument is 32-bit. A partition table that puts an
         * image past that limit must fail the read, not silently wrap round to
         * an in-range sector and hand back the wrong bytes. */
        lba64 = (start + done) / SD_BLOCK_SIZE;
        if (lba64 > 0xFFFFFFFFULL)
            return -1;
        lba = (uint32_t)lba64;
        off = (uint32_t)((start + done) % SD_BLOCK_SIZE);

        if (off != 0 || (count - done) < SD_BLOCK_SIZE ||
            (((uintptr_t)buf + done) & 3U) != 0U) {
            /* Partial block through the bounce buffer */
            chunk = SD_BLOCK_SIZE - off;
            if (chunk > (count - done))
                chunk = count - done;
            if (sd_read_blocks(lba, 1, bounce) != 0)
                return -1;
            for (i = 0; i < chunk; i++)
                buf[done + i] = bounce[off + i];
            done += chunk;
        }
        else {
            /* Whole blocks straight into the destination */
            blocks = (count - done) / SD_BLOCK_SIZE;
            if (blocks > SD_MAX_BLOCKS)
                blocks = SD_MAX_BLOCKS;
            if (sd_read_blocks(lba, blocks, buf + done) != 0)
                return -1;
            done += blocks * SD_BLOCK_SIZE;
        }
    }
    return (int)done;
}

/* Not implemented: nothing in the boot path writes. */
int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

/* Quiesce so the OS driver starts from reset state. */
void disk_close(int drv)
{
    (void)drv;
    if (card_ready) {
        usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
        card_ready = 0;
    }
}

#endif /* DISK_SDCARD || DISK_EMMC */
