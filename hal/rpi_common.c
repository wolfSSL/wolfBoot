/* rpi_common.c
 *
 * Code shared by the Raspberry Pi Compute Module targets (hal/cm4.c,
 * hal/cm5.c): the firmware device tree handoff, RAUC A/B slot selection, the
 * FIPS heap and RNG200 entropy seed, the SDHCI glue and the generic timer.
 * The target header supplies the RPI_* base addresses and names.
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "printf.h"
#if defined(TARGET_cm5)
#include "hal/cm5.h"
#else
#include "hal/cm4.h"
#endif

#if defined(MMU)
#include "fdt.h"

/* Firmware-provided DTB pointer captured at _cm4_entry (boot_aarch64_start.S).
 * The RPi firmware hands this dtb (fully patched: RAM size, UART clock,
 * serial no.) to the kernel it loads - which is wolfBoot. */
extern void* rpi_fw_dtb;

/* Size of the DTS landing zone, and so the capacity handed to fdt_open()
 * for the firmware DTB: a valid DTB is well under the arm64 boot-protocol
 * 2MB limit, and this keeps a corrupt header from overrunning the zone.
 * Override with -DRPI_FDT_MAX_SIZE=<bytes>. */
#ifndef RPI_FDT_MAX_SIZE
#define RPI_FDT_MAX_SIZE 0x200000
#endif

/* Kernel command line for the kernel-only FIT boot path (the raw dtb carries
 * neither root= nor console=). Override LINUX_BOOTARGS or just LINUX_BOOTARGS_ROOT
 * in the .config. LINUX_BOOTARGS_BASE (console/earlycon, no root=) comes from
 * the target header and is shared by the static and RAUC A/B cmdlines. */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p3"
#endif
#define LINUX_BOOTARGS \
    LINUX_BOOTARGS_BASE " root=" LINUX_BOOTARGS_ROOT " rootfstype=ext4 rootwait"
#endif

#if defined(RPI_RAUC_AB)
#include "disk.h"
#include "ubootenv.h"

/* 0-based GPT index of the raw U-Boot-env partition (RAUC fw_env.config target). */
#ifndef RPI_UBOOT_ENV_PART
#define RPI_UBOOT_ENV_PART 1
#endif
#ifndef BOOT_DISK
#define BOOT_DISK 0
#endif
/* RAUC bootname -> rootfs device. Override in the .config for your layout. */
#ifndef RPI_ROOT_A
#define RPI_ROOT_A "/dev/mmcblk0p4"
#endif
#ifndef RPI_ROOT_B
#define RPI_ROOT_B "/dev/mmcblk0p5"
#endif
/* RAUC bootnames (BOOT_ORDER tokens) that map to each rootfs. RAUC's own
 * default is A/B; override to match a system.conf that uses other names
 * (e.g. system0/system1). An unrecognised name fails loudly (static cmdline). */
#ifndef RPI_SLOT_A_NAME
#define RPI_SLOT_A_NAME "A"
#endif
#ifndef RPI_SLOT_B_NAME
#define RPI_SLOT_B_NAME "B"
#endif

static uint8_t rpi_uboot_env[UBOOT_ENV_SIZE];
/* Slot chosen by hal_boot_slot_select() (the A/B state machine), consumed by
 * rpi_rauc_build_bootargs() when building the kernel-only-FIT command line. */
static char rpi_rauc_slot_name[UBOOT_ENV_VAL_MAX];
static const char *rpi_rauc_root;
static int rpi_rauc_selected;

/* Bounded append of src to dst starting at index at; returns the new index. */
static size_t rpi_strcat(char *dst, size_t dstsz, size_t at, const char *src)
{
    while (*src != '\0' && at + 1 < dstsz)
        dst[at++] = *src++;
    dst[at] = '\0';
    return at;
}

/* Build the kernel command line "<base> root=<slot dev> rauc.slot=<name>
 * rootfstype=ext4 rootwait" into out, from the slot hal_boot_slot_select()
 * already chose. Returns 0 on success, -1 if no slot was selected (caller falls
 * back to the static LINUX_BOOTARGS). Pure string assembly - no disk I/O. */
static int rpi_rauc_build_bootargs(char *out, size_t outsz)
{
    size_t at;

    if (!rpi_rauc_selected)
        return -1;
    at = 0;
    at = rpi_strcat(out, outsz, at, LINUX_BOOTARGS_BASE);
    at = rpi_strcat(out, outsz, at, " root=");
    at = rpi_strcat(out, outsz, at, rpi_rauc_root);
    at = rpi_strcat(out, outsz, at, " rauc.slot=");
    at = rpi_strcat(out, outsz, at, rpi_rauc_slot_name);
    at = rpi_strcat(out, outsz, at, " rootfstype=ext4 rootwait");
    if (at >= outsz - 1) {
        /* Buffer filled: the command line was truncated (a cut root= or
         * rauc.slot= would mis-boot silently). Fail so the caller falls back to
         * the static LINUX_BOOTARGS. */
        wolfBoot_printf(RPI_TAG ": RAUC bootargs truncated (need > %u bytes)\n",
            (unsigned)outsz);
        return -1;
    }
    return 0;
}

/* Weak-hook override (see include/hal.h): read the raw U-Boot env, run the RAUC
 * A/B state machine, and persist the decremented try counter - BEFORE any DTB
 * handling and UNCONDITIONALLY (not gated on whether the FIT embedded an fdt),
 * so adding an fdt sub-image cannot silently disable failover. Selects the slot
 * into rpi_rauc_* for rpi_rauc_build_bootargs(). Runs with the MMU on and the
 * boot disk still open (update_disk.c defers disk_close until after this).
 * Returns 0. */
int hal_boot_slot_select(void)
{
    struct uboot_slot slot;
    int r;
    int env_ok;

    rpi_rauc_selected = 0;

    /* Require the FULL env: disk_part_read() clamps sz to the partition length,
     * so a short read (partition smaller than UBOOT_ENV_SIZE) returns a positive
     * count with the tail left zero - which would fail CRC. Treat anything but a
     * complete read as "no env". */
    r = disk_part_read(BOOT_DISK, RPI_UBOOT_ENV_PART, 0, UBOOT_ENV_SIZE,
        rpi_uboot_env);
    env_ok = (r == (int)UBOOT_ENV_SIZE);
    if (!env_ok) {
        /* Boot a default slot but do NOT persist a guess over RAUC state we did
         * not successfully read (a transient read error must not roll back a
         * "slot B good" system to "boot A"). */
        wolfBoot_printf(RPI_TAG ": uboot-env read failed (%d); default slot, no writeback\n",
            r);
        memset(rpi_uboot_env, 0, sizeof(rpi_uboot_env));
    }
    if (uboot_env_select_slot(rpi_uboot_env, UBOOT_ENV_SIZE, &slot) != 0)
        return 0; /* malformed BOOT_ORDER / redundant env -> static cmdline */
    if (!slot.selected) {
        /* All slots were exhausted; counters were re-armed - pick slot A now. */
        wolfBoot_printf(RPI_TAG ": all RAUC slots exhausted; re-armed, retrying\n");
        if (uboot_env_select_slot(rpi_uboot_env, UBOOT_ENV_SIZE, &slot) != 0)
            return 0;
        if (!slot.selected)
            return 0;
    }
    /* Persist the decremented counter only when the env read a CRC-valid image.
     * A bad-CRC full read leaves slot.reinitialized set: booting the default is
     * fine, but writing the wiped buffer back would destroy the last-good RAUC
     * state, so skip the writeback (see src/ubootenv.c). A short/failed write
     * leaves the counter un-decremented, so fall back to the static cmdline. */
    if (env_ok && !slot.reinitialized) {
        r = disk_part_write(BOOT_DISK, RPI_UBOOT_ENV_PART, 0, UBOOT_ENV_SIZE,
            rpi_uboot_env);
        if (r != (int)UBOOT_ENV_SIZE) {
            /* Loud on purpose: this is the failure mode the whole A/B feature
             * exists to prevent. The counter did not reach storage, so nothing
             * decrements and the SAME slot is chosen again on every boot -
             * redundancy is silently gone until the env partition is writable
             * again (worn flash, read-only media, wrong partition index). This
             * printf is the only evidence, so it names the consequence rather
             * than just the errno. */
            wolfBoot_printf(RPI_TAG ": WARNING uboot-env write failed (%d): RAUC try "
                "counter NOT persisted; A/B failover is DISABLED until the env "
                "partition is writable. Falling back to the static cmdline\n",
                r);
            return 0;
        }
    }
    else if (slot.reinitialized) {
        wolfBoot_printf(RPI_TAG ": uboot-env invalid CRC; default slot, not persisting reset\n");
    }

    if (strcmp(slot.name, RPI_SLOT_B_NAME) == 0)
        rpi_rauc_root = RPI_ROOT_B;
    else if (strcmp(slot.name, RPI_SLOT_A_NAME) == 0)
        rpi_rauc_root = RPI_ROOT_A;
    else {
        wolfBoot_printf(RPI_TAG ": unknown RAUC slot '%s'; static cmdline\n",
            slot.name);
        return 0;
    }
    memcpy(rpi_rauc_slot_name, slot.name, sizeof(rpi_rauc_slot_name));
    rpi_rauc_slot_name[sizeof(rpi_rauc_slot_name) - 1] = '\0';
    rpi_rauc_selected = 1;
    wolfBoot_printf(RPI_TAG ": RAUC slot %s -> root %s\n", slot.name, rpi_rauc_root);
    return 0;
}
#endif /* RPI_RAUC_AB */

/* Supply Linux a bootable DTB when the FIT is kernel-only: relocate the
 * firmware dtb to WOLFBOOT_LOAD_DTS_ADDRESS (headroom to grow /chosen without
 * disturbing whatever the firmware placed after its copy) and inject the kernel
 * command line. Called from update_disk.c BEFORE hal_prepare_boot(), so this
 * runs with the MMU/caches on (FDT edits do unaligned accesses that would fault
 * on the MMU-off Device memory wolfBoot hands off with); the HAL MMU teardown then
 * cleans it to DRAM for the MMU-off kernel. Returns NULL if no valid dtb, in
 * which case do_boot() is handed a NULL dtb. */
void* hal_get_boot_dts(void)
{
#if !defined(RPI_FIRMWARE_DTB)
    /* Only the Linux (kernel-only FIT) config opts into using the firmware DTB.
     * The stub/FIPS disk boots (cm4_emmc.config, cm5_emmc.config) leave this a no-op so their
     * validated NULL-DTB handoff is unchanged. */
    return NULL;
#else
    fdt_ctx ctx;
    void *fdt = rpi_fw_dtb;
    uint32_t sz;
    int off;

    if (fdt == NULL) {
        wolfBoot_printf(RPI_TAG ": no firmware DTB\n");
        return NULL;
    }
    /* Bound the parse by the largest DTB this path can ever accept: the
     * DTS landing zone less the fixup headroom. A corrupt header can then
     * neither be walked out of bounds here nor make the copy below clobber
     * memory past that zone. */
    if (fdt_open(&ctx, fdt,
            RPI_FDT_MAX_SIZE - WOLFBOOT_FDT_FIXUP_HEADROOM) != 0) {
        wolfBoot_printf(RPI_TAG ": no valid firmware DTB (%p)\n", fdt);
        return NULL;
    }
    sz = fdt_size(&ctx);
    /* SECURITY: the firmware DTB lives on the unsigned FAT partition (unless
     * the RPi EEPROM secure-boot is enabled), so it is NOT covered by wolfBoot's
     * signature. Only /chosen/bootargs is overwritten below, and the
     * /chosen/linux,initrd-{start,end} pointers are zeroed: this path never
     * loads an authenticated ramdisk, so a non-zero firmware value would
     * direct the signed kernel to an unauthenticated initramfs in RAM.
     * /memory, /reserved-memory and per-device reg windows remain firmware /
     * attacker controlled.
     * This is NOT effectively optional: RPI_FIRMWARE_DTB is enabled by default
     * in the shipped Linux configurations (the *_emmc_linux.config and
     * *_emmc_rauc.config examples), so anyone following the documented Linux recipe is
     * on this path. It is a compile-time switch, not a default-off feature.
     * Why the signed-DTB mechanism is not used here: master carries
     * HDR_DEVICE_TREE_DIGEST plus "sign --dts" to bind a raw DTB to the signed
     * image, but it does not apply to this path for two independent reasons.
     * (1) The digest is only checked by the RAM-boot path (src/update_ram.c);
     * these targets boot via src/update_disk.c, which has no such check. (2) Even
     * with the plumbing, the hash is not predictable at signing time: the
     * Raspberry Pi firmware patches the DTB at runtime (the shipped
     * bcm2711-rpi-cm4.dtb ships /memory@0 reg = <0 0 0> and gets the real RAM
     * size, UART clock and board serial injected before handoff), so the
     * blob wolfBoot receives is never the blob that was signed. That runtime
     * patching is exactly why the firmware DTB is used instead of one carried
     * inside the FIT - a FIT-carried DTB boots with no RAM size.
     * The only thing that actually closes this boundary on these boards is the
     * Raspberry Pi EEPROM secure-boot, which authenticates the boot partition
     * itself. See "Device tree trust boundary" in docs/Targets.md. */
    wolfBoot_printf(RPI_TAG ": using UNVERIFIED firmware DTB (RPI_FIRMWARE_DTB)\n");
    memcpy((void*)WOLFBOOT_LOAD_DTS_ADDRESS, fdt, sz);
    fdt = (void*)WOLFBOOT_LOAD_DTS_ADDRESS;

    /* Zero the appended headroom so the grown blob holds no uninitialized
     * bytes, then re-open on the landing zone and record the new size. */
    memset((uint8_t*)fdt + sz, 0, WOLFBOOT_FDT_FIXUP_HEADROOM);
    if (fdt_open(&ctx, fdt, RPI_FDT_MAX_SIZE) != 0 ||
            fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM) != 0) {
        wolfBoot_printf(RPI_TAG ": relocated DTB rejected; refusing firmware DTB\n");
        return NULL;
    }
    off = fdt_subnode_offset(&ctx, 0, "chosen");
    if (off == -FDT_ERR_NOTFOUND) {
        off = fdt_add_subnode(&ctx, 0, "chosen");
    }
    if (off < 0) {
        /* Fail closed: without /chosen we cannot inject our known-good bootargs,
         * so refuse to hand Linux the unverified firmware DTB (with its own,
         * attacker-influenceable command line) rather than boot open. */
        wolfBoot_printf(RPI_TAG ": DTB /chosen error (%d); refusing firmware DTB\n",
            off);
        return NULL;
    }
#if defined(RPI_RAUC_AB)
    {
        static char rpi_bootargs[256];
        const char *args = LINUX_BOOTARGS;
        if (rpi_rauc_build_bootargs(rpi_bootargs, sizeof(rpi_bootargs)) == 0)
            args = rpi_bootargs;
        if (fdt_fixup_str(&ctx, off, "chosen", "bootargs", args) != 0) {
            wolfBoot_printf(RPI_TAG ": DTB bootargs fixup failed; refusing firmware DTB\n");
            return NULL;
        }
    }
#else
    if (fdt_fixup_str(&ctx, off, "chosen", "bootargs", LINUX_BOOTARGS) != 0) {
        wolfBoot_printf(RPI_TAG ": DTB bootargs fixup failed; refusing firmware DTB\n");
        return NULL;
    }
#endif
    /* Zero the initrd pointers the firmware DTB may carry. This path never
     * attaches an authenticated ramdisk (a FIT ramdisk subimage is only
     * fixup'd into an FIT-embedded DTB, which bypasses this fallback), so
     * any non-zero value here would point the signed kernel at an
     * unauthenticated initramfs in RAM. The kernel loads no initrd from a
     * zero range, and the properties are zeroed, not deleted, so the
     * existing fdt_fixup_val64() covers both the present and absent case.
     * Fail closed, like the bootargs fixup above. */
    if (fdt_fixup_val64(&ctx, off, "chosen", "linux,initrd-start", 0) != 0 ||
            fdt_fixup_val64(&ctx, off, "chosen", "linux,initrd-end", 0) != 0) {
        wolfBoot_printf(RPI_TAG ": DTB initrd fixup failed; refusing firmware DTB\n");
        return NULL;
    }
    wolfBoot_printf(RPI_TAG ": DTB relocated to %p, bootargs set\n", fdt);
    return fdt;
#endif /* RPI_FIRMWARE_DTB */
}
#endif /* MMU */

#if defined(HAVE_FIPS)
/* Bounded heap for the FIPS module's malloc. wolfBoot builds the FIPS target
 * with --specs=nosys.specs, whose newlib _sbrk grows unbounded from the linker
 * 'end' symbol - toward the unverified image staged at kernel_addr (0x2C0000).
 * Provide our own _sbrk over a fixed static buffer (in .bss, well below the
 * image) so heap growth is bounded and can never reach kernel_addr. */
#ifndef RPI_FIPS_HEAP_SIZE
#define RPI_FIPS_HEAP_SIZE (128 * 1024)
#endif
static unsigned char rpi_fips_heap[RPI_FIPS_HEAP_SIZE];
void* _sbrk(int incr);
void* _sbrk(int incr)
{
    static unsigned char* brk = rpi_fips_heap;
    unsigned char* prev = brk;

    if (incr < 0) {
        /* Heap trim: newlib's malloc returns memory on free() with a negative
         * increment. Clamp to the heap base so brk cannot underflow. */
        if ((size_t)(-incr) > (size_t)(brk - rpi_fips_heap))
            brk = rpi_fips_heap;
        else
            brk += incr;
        return (void*)prev;
    }
    if ((size_t)(brk - rpi_fips_heap) + (size_t)incr > sizeof(rpi_fips_heap))
        return (void*)-1; /* out of heap */
    brk += incr;
    return (void*)prev;
}
#endif /* HAVE_FIPS */

#if defined(DEBUG) && defined(DEBUG_UART)
/* Bring-up diagnostic: exception handler invoked from simple_el2_vectors in
 * src/boot_aarch64_start.S. Dumps the fault syndrome so a data/instruction
 * abort shows up over UART instead of hanging silently. Built only with
 * DEBUG + DEBUG_UART. ESR_EL2[31:26] = exception class; vector is the 0-15 slot
 * in the table, so >= 8 means the fault came from a lower EL. */
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector);
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector)
{
    wolfBoot_printf("\n*** " RPI_BOARD " EXCEPTION *** vector=%d\n", (int)vector);
    wolfBoot_printf("ESR_EL2=0x%08x EC=0x%02x\n",
        (unsigned)esr, (unsigned)((esr >> 26) & 0x3F));
    wolfBoot_printf("ELR_EL2=0x%08x%08x\n",
        (unsigned)(elr >> 32), (unsigned)elr);
    wolfBoot_printf("FAR_EL2=0x%08x%08x\n",
        (unsigned)(far >> 32), (unsigned)far);
}
#endif /* DEBUG && DEBUG_UART */

#if defined(HAVE_FIPS)
#include <wolfssl/wolfcrypt/memory.h> /* wc_ForceZero */

/* Upper bound on the busy-wait for the RNG200 FIFO to fill. This is a coarse,
 * CPU-clock-dependent spin count (not a wall-clock timeout); it only guards
 * against a wedged RNG so the seed read cannot hang forever. Tune if needed. */
#ifndef RNG200_FIFO_WAIT_ITERS
#define RNG200_FIFO_WAIT_ITERS 200000000U
#endif

/* FIPS DRBG entropy seed from the RNG200 hardware TRNG. Registered via
 * CUSTOM_RAND_GENERATE_SEED in include/user_settings.h. The RNG200 has NIST
 * SP800-90B startup/continuous health tests in hardware. */
int wolfBoot_fips_seed(unsigned char* output, unsigned int sz)
{
    static int rng_inited = 0;
    unsigned int pos = 0;
    unsigned int guard;

    if (!rng_inited) {
        /* iproc-rng200 bring-up: disable RBG, soft-reset the RBG then RNG
         * cores, clear pending interrupt status, then re-enable the RBG. */
        *RNG_CTRL = 0;
        *RNG_RBG_SOFT_RESET = 1;
        *RNG_RBG_SOFT_RESET = 0;
        *RNG_SOFT_RESET = 1;
        *RNG_SOFT_RESET = 0;
        *RNG_INT_STATUS = 0xFFFFFFFF; /* write-1-to-clear all pending status */
        *RNG_CTRL = RNG200_CTRL_RBGEN;
        /* Do NOT latch rng_inited here: only treat the core as initialized
         * once it has actually produced a word (see below), so a wedged core
         * re-runs bring-up on the next call rather than being trusted. */
    }

    while (pos < sz) {
        unsigned int word, n, i, st;
        /* SP800-90B health status: fail closed on a degraded-but-clocking TRNG.
         * A continuous-test lockout or NIST health-test failure must not seed
         * the FIPS Hash-DRBG, even when the FIFO still has data. */
        st = *RNG_INT_STATUS;
        if ((st & RNG200_INT_HEALTH_FAIL) != 0) {
#if defined(DEBUG_UART)
            wolfBoot_printf("RNG200 health fail int=0x%08x; refusing seed\n",
                (unsigned)st);
#endif
            /* Soft-reset and clear the latch so a later boot can re-init a
             * healthy core; refuse this seed request. */
            *RNG_CTRL = 0;
            rng_inited = 0;
            return -1;
        }
        /* wait for at least one 32-bit word in the FIFO (bounded) */
        guard = 0;
        while ((*RNG_FIFO_COUNT & 0xFF) == 0) {
            if (++guard > RNG200_FIFO_WAIT_ITERS) {
#if defined(DEBUG_UART)
                wolfBoot_printf("RNG200 FIFO timeout int=0x%08x ctrl=0x%08x\n",
                    (unsigned)*RNG_INT_STATUS, (unsigned)*RNG_CTRL);
#endif
                return -1;
            }
        }
        word = *RNG_FIFO_DATA;
        rng_inited = 1; /* first successful word: safe to skip bring-up later */
        n = (sz - pos) < 4 ? (sz - pos) : 4;
        for (i = 0; i < n; i++)
            output[pos++] = (unsigned char)(word >> (i * 8));
        /* Raw TRNG output seeds the FIPS Hash-DRBG: do not leave the last word
         * on the stack for a later frame to observe. wc_ForceZero (not memset)
         * so the compiler cannot elide the dead store. */
        wc_ForceZero(&word, sizeof(word));
    }
    return 0;
}
#endif /* HAVE_FIPS */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* Platform glue for the generic SDHCI driver (src/sdhci.c). The BCM2711 EMMC2
 * and BCM2712 SDIO1 controllers are standard SDHCI v3.0. The driver uses
 * Cadence-style SRS offsets (0x200 + std); translate them to the standard
 * layout, mirroring the ZynqMP path in hal/zynq.c. The firmware has already
 * configured the controller clock/pinmux, so only a controller soft reset is
 * needed here. */
#include "sdhci.h"

uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)RPI_SDHCI_BASE;

    if (offset >= CADENCE_SRS_OFFSET) {
        uint32_t std_off = offset - CADENCE_SRS_OFFSET;
        uint32_t val;

        if (std_off == 0x58) /* SRS22 -> legacy SDMA address (SRS00) */
            return *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR));
        if (std_off == 0x5C) /* SRS23: no 64-bit addressing on v3.0 */
            return 0;
        val = *((volatile uint32_t *)(base + std_off));
        if (std_off == 0x40) /* SRS16 Capabilities: mask A64S (no HV4E) */
            val &= ~SDHCI_SRS16_A64S;
        return val;
    }
    return 0; /* HRS region not present on these controllers */
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)RPI_SDHCI_BASE;
    uint32_t std_off;

    if (offset < CADENCE_SRS_OFFSET)
        return; /* HRS region not present */
    std_off = offset - CADENCE_SRS_OFFSET;

    /* SRS10 (0x28): Host Control 1 / Power / Block Gap / Wakeup (8-bit each) */
    if (std_off == 0x28) {
        *((volatile uint8_t *)(base + STD_SDHCI_HOST_CTRL1)) = (uint8_t)val;
        *((volatile uint8_t *)(base + STD_SDHCI_POWER_CTRL))  = (uint8_t)(val >> 8);
        *((volatile uint8_t *)(base + STD_SDHCI_BLKGAP_CTRL)) = (uint8_t)(val >> 16);
        *((volatile uint8_t *)(base + STD_SDHCI_WAKEUP_CTRL)) = (uint8_t)(val >> 24);
        return;
    }
    /* SRS11 (0x2C): Clock Control (16-bit) / Timeout / Software Reset */
    if (std_off == 0x2C) {
        *((volatile uint16_t *)(base + STD_SDHCI_CLK_CTRL))   = (uint16_t)val;
        *((volatile uint8_t *)(base + STD_SDHCI_TIMEOUT_CTRL)) = (uint8_t)(val >> 16);
        *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET))    = (uint8_t)(val >> 24);
        return;
    }
    if (std_off == 0x58) { /* SRS22 -> legacy SDMA address; write restarts DMA */
        *((volatile uint32_t *)(base + STD_SDHCI_SDMA_ADDR)) = val;
        return;
    }
    if (std_off == 0x5C) /* SRS23: no 64-bit addressing on v3.0 */
        return;
    if (std_off == STD_SDHCI_HOST_CTRL2) /* SRS15: mask unsupported HV4E/A64 */
        val &= ~(SDHCI_SRS15_HV4E | SDHCI_SRS15_A64);
    *((volatile uint32_t *)(base + std_off)) = val;
}

void sdhci_platform_init(void)
{
    /* The firmware already brought up the controller clock/pinmux; just issue a
     * controller soft reset and wait for it to clear. */
    volatile uint8_t *base = (volatile uint8_t *)RPI_SDHCI_BASE;
    volatile int i;
#ifdef RPI_SDHCI_CFG_BASE
    /* BCM2712: force card presence (the eMMC has no card-detect line) and
     * select the eMMC pin mode. */
    volatile uint32_t *cfg = (volatile uint32_t *)RPI_SDHCI_CFG_BASE;
    uint32_t reg;

    reg = cfg[SDIO_CFG_CTRL / 4];
    reg &= ~SDIO_CFG_CTRL_SDCD_N_TEST_LEV;
    reg |= SDIO_CFG_CTRL_SDCD_N_TEST_EN;
    cfg[SDIO_CFG_CTRL / 4] = reg;
    reg = cfg[SDIO_CFG_SD_PIN_SEL / 4];
    reg &= ~SDIO_CFG_SD_PIN_SEL_MASK;
    reg |= SDIO_CFG_SD_PIN_SEL_MMC;
    cfg[SDIO_CFG_SD_PIN_SEL / 4] = reg;
#endif

    *((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) = STD_SDHCI_SRA;
    for (i = 0; i < 100000; i++) {
        if ((*((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) & STD_SDHCI_SRA) == 0)
            break;
    }
    if ((*((volatile uint8_t *)(base + STD_SDHCI_SW_RESET)) & STD_SDHCI_SRA) != 0) {
        wolfBoot_printf("sdhci_platform_init: SRA soft reset did not clear; "
            "controller may be unresponsive\n");
    }
}

void sdhci_platform_irq_init(void)
{
    /* Polled mode; no IRQ wiring needed for the boot path. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc; /* handled by the generic driver via Host Control 1 */
}

#endif /* DISK_SDCARD || DISK_EMMC */

/* Microseconds from the ARMv8 generic timer (SDHCI udelay + disk updater +
 * boot benchmark). Falls back to the SoC system counter frequency if
 * CNTFRQ_EL0 is 0. Guard matches include/hal.h (WOLFBOOT_UPDATE_DISK ||
 * BOOT_BENCHMARK) plus the disk backends, so a BOOT_BENCHMARK=1 build without a
 * disk backend still links. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC) || \
    defined(WOLFBOOT_UPDATE_DISK) || defined(BOOT_BENCHMARK)
uint64_t hal_get_timer_us(void)
{
#if defined(__aarch64__)
    uint64_t count, freq;
    __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(count));
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
    if (freq == 0)
        freq = RPI_TIMER_CLK_FREQ;
    return (uint64_t)(((__uint128_t)count * 1000000ULL) / freq);
#else
    /* Non-AArch64 host build (unit tests): the generic timer is unavailable. */
    return 0;
#endif
}
#endif /* timer guard */
