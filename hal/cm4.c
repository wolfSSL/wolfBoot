/* cm4.c
 *
 * HAL for the Raspberry Pi Compute Module 4 (CM4): Broadcom BCM2711,
 * quad-core Cortex-A72 (ARMv8-A).
 *
 * The VideoCore GPU firmware loads wolfBoot (an ARM64 kernel8.img carrying the
 * Linux image header) to 0x200000 and enters it at EL2; wolfBoot verifies the
 * signed payload and boots it from RAM, or from eMMC/SD A/B via the generic
 * SDHCI driver (at the end of this file). hal_flash_* are no-ops (no in-place
 * flash in this mode).
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
#include "hal/cm4.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"   /* sdhci_shutdown(), called from hal_prepare_boot() */
#endif
#ifndef ARCH_AARCH64
#   error "wolfBoot cm4 HAL: wrong architecture selected. Please compile with ARCH=AARCH64."
#endif

/* Hardware register map (bases, UART, EMMC2/SDHCI) is in hal/cm4.h */

/* Fixed addresses (provided by the linker script) */
extern void *kernel_addr, *update_addr, *dts_addr;

/* Enable the identity MMU + caches when the build does more than the trivial
 * RAM-boot: FIPS (unaligned/SIMD in the module), or the disk path (optimized
 * code + SDHCI block-buffer memcpy fault on MMU-off Device memory). Normal
 * cacheable memory permits those accesses and speeds up crypto/disk reads. */
#if (defined(HAVE_FIPS) || defined(DISK_SDCARD) || defined(DISK_EMMC)) \
    && defined(__aarch64__)
#define CM4_USE_MMU
#endif

#if defined(CM4_USE_MMU)
void cm4_mmu_enable(void);  /* defined below; called from hal_init */
void cm4_mmu_disable(void); /* defined below; called from hal_prepare_boot */
#endif

#if defined(DEBUG_UART)
/* Console UART select. On this bench CM4 the debug cable on GPIO14/15 is the
 * BCM2711 mini-UART (AUX, Linux ttyS0), so that is the default. Boards where
 * dtoverlay=disable-bt actually routes the PL011 onto GPIO14/15 can build with
 * CM4_UART_PL011 to use the PL011 (0xFE201000) instead. */
#if defined(CM4_UART_PL011)
static void uart_tx(char c)
{
    while (*UART0_FR & 0x20) /* TXFF: wait while FIFO full */
        ;
    *UART0_DR = (unsigned int)(unsigned char)c;
}

void uart_init(void)
{
    /* Route GPIO14 (TXD0) and GPIO15 (RXD0) to the PL011 via ALT0, so wolfBoot's
     * console reaches the 40-pin debug header even when the firmware has NOT
     * applied dtoverlay=disable-bt (which needs overlays/disable-bt.dtbo on the
     * boot FAT). Without this the PL011 stays wired to the Bluetooth pins and
     * wolfBoot is silent on the header. GPFSEL1 (GPIO_BASE+0x04) holds FSEL10-19:
     * FSEL14 = bits [14:12], FSEL15 = bits [17:15]; ALT0 = 0b100. */
    volatile unsigned int *gpfsel1 =
        (volatile unsigned int *)(BCM2711_GPIO_BASE + 0x04);
    unsigned int fsel;

    fsel = *gpfsel1;
    fsel &= ~((7u << 12) | (7u << 15));
    fsel |=  ((4u << 12) | (4u << 15));
    *gpfsel1 = fsel;

    /* Program the PL011 for 115200 8N1 assuming a 48MHz UARTCLK
     * (init_uart_clock=48000000): BAUDDIV = 48e6/(16*115200) -> IBRD 26 FBRD 3. */
    *UART0_CR = 0;
    *UART0_ICR = 0x7FF;
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6); /* FIFO, 8-bit */
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);   /* enable UART, TX, RX */
}
#else /* mini-UART (default) */
static void uart_tx(char c)
{
    while ((*MU_LSR & MU_LSR_TXFF_EMPTY) == 0) /* wait until TX can accept */
        ;
    *MU_IO = (unsigned int)(unsigned char)c;
}

void uart_init(void)
{
    /* The firmware has already enabled the mini-UART at a stable baud
     * (enable_uart=1 fixes core_freq), so - like the Linux 8250 console with
     * "skip-init" - wolfBoot inherits that setup and just writes AUX_MU_IO.
     * Reprogramming the baud here is unnecessary (and error-prone: the mini-UART
     * clock is core_freq-derived, not a fixed rate). */
}
#endif /* CM4_UART_PL011 */

void uart_write(const char* buf, uint32_t sz)
{
    while (sz-- > 0 && *buf)
        uart_tx(*buf++);
}
#endif /* DEBUG_UART */

void* hal_get_primary_address(void)
{
    return (void*)&kernel_addr;
}

void* hal_get_update_address(void)
{
    return (void*)&update_addr;
}

void* hal_get_dts_address(void)
{
    return (void*)&dts_addr;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}

#if defined(MMU)
#include "fdt.h"

/* Firmware-provided DTB pointer captured at _cm4_entry (boot_aarch64_start.S).
 * The RPi VideoCore firmware hands this dtb (fully patched: RAM size, mini-UART
 * clock, serial no.) to the kernel it loads - which is wolfBoot. */
extern void* cm4_fw_dtb;

/* Upper bound for the firmware DTB copy. fdt_check_header() does not validate
 * totalsize, so cap it: a valid DTB is well under the arm64 boot-protocol 2MB
 * limit, and this keeps a corrupt header from overrunning the DTS landing zone.
 * Override with -DCM4_FDT_MAX_SIZE=<bytes>. */
#ifndef CM4_FDT_MAX_SIZE
#define CM4_FDT_MAX_SIZE 0x200000
#endif

/* Kernel command line for the kernel-only FIT boot path (the raw dtb carries
 * neither root= nor console=). Override LINUX_BOOTARGS or just LINUX_BOOTARGS_ROOT
 * in the .config. The console must match the wolfBoot UART build:
 *   CM4_UART_PL011 (+ dtoverlay=disable-bt) -> PL011 on GPIO14/15 = ttyAMA0. The
 *     PL011 registers cleanly in Linux; the mini-UART (bcm2835-aux) does not on
 *     this DTB ("unable to register 8250 port").
 *   default (mini-UART) -> serial0/ttyS0. */
/* Console/earlycon base (no root=), shared by the static and RAUC A/B cmdlines. */
#if defined(CM4_UART_PL011)
#define LINUX_BOOTARGS_BASE \
    "earlycon=pl011,mmio32,0xfe201000 console=ttyAMA0,115200"
#else
/* earlycon=uart8250,mmio32,0xfe215040: the mini-UART is 8250-driven with a 4-byte
 * register stride, so LSR lands at 0xfe215054 - prints from MMIO before the dtb
 * console driver is up. serial0 aliases the mini-UART (ttyS0) at runtime. */
#define LINUX_BOOTARGS_BASE \
    "earlycon=uart8250,mmio32,0xfe215040 console=serial0,115200"
#endif
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk0p3"
#endif
#define LINUX_BOOTARGS \
    LINUX_BOOTARGS_BASE " root=" LINUX_BOOTARGS_ROOT " rootfstype=ext4 rootwait"
#endif

#if defined(CM4_RAUC_AB)
#include "disk.h"
#include "ubootenv.h"

/* 0-based GPT index of the raw U-Boot-env partition (RAUC fw_env.config target). */
#ifndef CM4_UBOOT_ENV_PART
#define CM4_UBOOT_ENV_PART 1
#endif
#ifndef BOOT_DISK
#define BOOT_DISK 0
#endif
/* RAUC bootname -> rootfs device. Override in the .config for your layout. */
#ifndef CM4_ROOT_A
#define CM4_ROOT_A "/dev/mmcblk0p4"
#endif
#ifndef CM4_ROOT_B
#define CM4_ROOT_B "/dev/mmcblk0p5"
#endif
/* RAUC bootnames (BOOT_ORDER tokens) that map to each rootfs. RAUC's own
 * default is A/B; override to match a system.conf that uses other names
 * (e.g. system0/system1). An unrecognised name fails loudly (static cmdline). */
#ifndef CM4_SLOT_A_NAME
#define CM4_SLOT_A_NAME "A"
#endif
#ifndef CM4_SLOT_B_NAME
#define CM4_SLOT_B_NAME "B"
#endif

static uint8_t cm4_uboot_env[UBOOT_ENV_SIZE];
/* Slot chosen by hal_boot_slot_select() (the A/B state machine), consumed by
 * cm4_rauc_build_bootargs() when building the kernel-only-FIT command line. */
static char cm4_rauc_slot_name[UBOOT_ENV_VAL_MAX];
static const char *cm4_rauc_root;
static int cm4_rauc_selected;

/* Bounded append of src to dst starting at index at; returns the new index. */
static size_t cm4_strcat(char *dst, size_t dstsz, size_t at, const char *src)
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
static int cm4_rauc_build_bootargs(char *out, size_t outsz)
{
    size_t at;

    if (!cm4_rauc_selected)
        return -1;
    at = 0;
    at = cm4_strcat(out, outsz, at, LINUX_BOOTARGS_BASE);
    at = cm4_strcat(out, outsz, at, " root=");
    at = cm4_strcat(out, outsz, at, cm4_rauc_root);
    at = cm4_strcat(out, outsz, at, " rauc.slot=");
    at = cm4_strcat(out, outsz, at, cm4_rauc_slot_name);
    at = cm4_strcat(out, outsz, at, " rootfstype=ext4 rootwait");
    if (at >= outsz - 1) {
        /* Buffer filled: the command line was truncated (a cut root= or
         * rauc.slot= would mis-boot silently). Fail so the caller falls back to
         * the static LINUX_BOOTARGS. */
        wolfBoot_printf("cm4: RAUC bootargs truncated (need > %u bytes)\n",
            (unsigned)outsz);
        return -1;
    }
    return 0;
}

/* Weak-hook override (see include/hal.h): read the raw U-Boot env, run the RAUC
 * A/B state machine, and persist the decremented try counter - BEFORE any DTB
 * handling and UNCONDITIONALLY (not gated on whether the FIT embedded an fdt),
 * so adding an fdt sub-image cannot silently disable failover. Selects the slot
 * into cm4_rauc_* for cm4_rauc_build_bootargs(). Runs with the MMU on and the
 * boot disk still open (update_disk.c defers disk_close until after this).
 * Returns 0. */
int hal_boot_slot_select(void)
{
    struct uboot_slot slot;
    int r;
    int env_ok;

    cm4_rauc_selected = 0;

    /* Require the FULL env: disk_part_read() clamps sz to the partition length,
     * so a short read (partition smaller than UBOOT_ENV_SIZE) returns a positive
     * count with the tail left zero - which would fail CRC. Treat anything but a
     * complete read as "no env". */
    r = disk_part_read(BOOT_DISK, CM4_UBOOT_ENV_PART, 0, UBOOT_ENV_SIZE,
        cm4_uboot_env);
    env_ok = (r == (int)UBOOT_ENV_SIZE);
    if (!env_ok) {
        /* Boot a default slot but do NOT persist a guess over RAUC state we did
         * not successfully read (a transient read error must not roll back a
         * "slot B good" system to "boot A"). */
        wolfBoot_printf("cm4: uboot-env read failed (%d); default slot, no writeback\n",
            r);
        memset(cm4_uboot_env, 0, sizeof(cm4_uboot_env));
    }
    if (uboot_env_select_slot(cm4_uboot_env, UBOOT_ENV_SIZE, &slot) != 0)
        return 0; /* malformed BOOT_ORDER / redundant env -> static cmdline */
    if (!slot.selected) {
        /* All slots were exhausted; counters were re-armed - pick slot A now. */
        wolfBoot_printf("cm4: all RAUC slots exhausted; re-armed, retrying\n");
        if (uboot_env_select_slot(cm4_uboot_env, UBOOT_ENV_SIZE, &slot) != 0)
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
        r = disk_part_write(BOOT_DISK, CM4_UBOOT_ENV_PART, 0, UBOOT_ENV_SIZE,
            cm4_uboot_env);
        if (r != (int)UBOOT_ENV_SIZE) {
            /* Loud on purpose: this is the failure mode the whole A/B feature
             * exists to prevent. The counter did not reach storage, so nothing
             * decrements and the SAME slot is chosen again on every boot -
             * redundancy is silently gone until the env partition is writable
             * again (worn flash, read-only media, wrong partition index). This
             * printf is the only evidence, so it names the consequence rather
             * than just the errno. */
            wolfBoot_printf("cm4: WARNING uboot-env write failed (%d): RAUC try "
                "counter NOT persisted; A/B failover is DISABLED until the env "
                "partition is writable. Falling back to the static cmdline\n",
                r);
            return 0;
        }
    }
    else if (slot.reinitialized) {
        wolfBoot_printf("cm4: uboot-env invalid CRC; default slot, not persisting reset\n");
    }

    if (strcmp(slot.name, CM4_SLOT_B_NAME) == 0)
        cm4_rauc_root = CM4_ROOT_B;
    else if (strcmp(slot.name, CM4_SLOT_A_NAME) == 0)
        cm4_rauc_root = CM4_ROOT_A;
    else {
        wolfBoot_printf("cm4: unknown RAUC slot '%s'; static cmdline\n",
            slot.name);
        return 0;
    }
    memcpy(cm4_rauc_slot_name, slot.name, sizeof(cm4_rauc_slot_name));
    cm4_rauc_slot_name[sizeof(cm4_rauc_slot_name) - 1] = '\0';
    cm4_rauc_selected = 1;
    wolfBoot_printf("cm4: RAUC slot %s -> root %s\n", slot.name, cm4_rauc_root);
    return 0;
}
#endif /* CM4_RAUC_AB */

/* Supply Linux a bootable DTB when the FIT is kernel-only: relocate the
 * firmware dtb to WOLFBOOT_LOAD_DTS_ADDRESS (headroom to grow /chosen without
 * disturbing whatever the firmware placed after its copy) and inject the kernel
 * command line. Called from update_disk.c BEFORE hal_prepare_boot(), so this
 * runs with the MMU/caches on (FDT edits do unaligned accesses that would fault
 * on the MMU-off Device memory wolfBoot hands off with); cm4_mmu_disable() then
 * cleans it to DRAM for the MMU-off kernel. Returns NULL if no valid dtb, in
 * which case do_boot() is handed a NULL dtb. */
void* hal_get_boot_dts(void)
{
#if !defined(CM4_FIRMWARE_DTB)
    /* Only the Linux (kernel-only FIT) config opts into using the firmware DTB.
     * The stub/FIPS disk boots (cm4_emmc.config) leave this a no-op so their
     * validated NULL-DTB handoff is unchanged. */
    return NULL;
#else
    void *fdt = cm4_fw_dtb;
    uint32_t sz;
    int off;

    if (fdt == NULL || fdt_check_header(fdt) != 0) {
        wolfBoot_printf("cm4: no valid firmware DTB (%p)\n", fdt);
        return NULL;
    }
    sz = (uint32_t)fdt_totalsize(fdt);
    /* fdt_check_header() does not validate totalsize; bound it so a corrupt DTB
     * header cannot make the copy/fixup clobber memory past the DTS landing
     * zone (leaving room for the fixup headroom too). */
    if (sz < sizeof(struct fdt_header) ||
            sz > CM4_FDT_MAX_SIZE - WOLFBOOT_FDT_FIXUP_HEADROOM) {
        wolfBoot_printf("cm4: firmware DTB size %u out of range\n",
            (unsigned)sz);
        return NULL;
    }
    /* SECURITY: the firmware DTB lives on the unsigned FAT partition (unless
     * the RPi EEPROM secure-boot is enabled), so it is NOT covered by wolfBoot's
     * signature. Only /chosen/bootargs is overwritten below; /memory,
     * /reserved-memory, per-device reg windows and initrd remain firmware /
     * attacker controlled.
     * This is NOT effectively optional: CM4_FIRMWARE_DTB is enabled by default
     * in both shipped Linux configurations (cm4_emmc_linux.config and
     * cm4_emmc_rauc.config), so anyone following the documented Linux recipe is
     * on this path. It is a compile-time switch, not a default-off feature.
     * Why the signed-DTB mechanism is not used here: master carries
     * HDR_DEVICE_TREE_DIGEST plus "sign --dts" to bind a raw DTB to the signed
     * image, but it does not apply to this path for two independent reasons.
     * (1) The digest is only checked by the RAM-boot path (src/update_ram.c);
     * the CM4 boots via src/update_disk.c, which has no such check. (2) Even
     * with the plumbing, the hash is not predictable at signing time: the
     * VideoCore firmware patches the DTB at runtime (the shipped
     * bcm2711-rpi-cm4.dtb ships /memory@0 reg = <0 0 0> and gets the real RAM
     * size, mini-UART clock and board serial injected before handoff), so the
     * blob wolfBoot receives is never the blob that was signed. That runtime
     * patching is exactly why the firmware DTB is used instead of one carried
     * inside the FIT - a FIT-carried DTB boots with no RAM size.
     * The only thing that actually closes this boundary on a CM4 is the
     * Raspberry Pi EEPROM secure-boot, which authenticates the boot partition
     * itself. See "Device tree trust boundary" in docs/Targets.md. */
    wolfBoot_printf("cm4: using UNVERIFIED firmware DTB (CM4_FIRMWARE_DTB)\n");
    memcpy((void*)WOLFBOOT_LOAD_DTS_ADDRESS, fdt, sz);
    fdt = (void*)WOLFBOOT_LOAD_DTS_ADDRESS;

    /* Zero the appended headroom so the grown blob holds no uninitialized
     * bytes, then record the new total size. */
    memset((uint8_t*)fdt + sz, 0, WOLFBOOT_FDT_FIXUP_HEADROOM);
    fdt_set_totalsize(fdt, sz + WOLFBOOT_FDT_FIXUP_HEADROOM);
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off == -FDT_ERR_NOTFOUND) {
        off = fdt_add_subnode(fdt, 0, "chosen");
    }
    if (off < 0) {
        /* Fail closed: without /chosen we cannot inject our known-good bootargs,
         * so refuse to hand Linux the unverified firmware DTB (with its own,
         * attacker-influenceable command line) rather than boot open. */
        wolfBoot_printf("cm4: DTB /chosen error (%d); refusing firmware DTB\n",
            off);
        return NULL;
    }
#if defined(CM4_RAUC_AB)
    {
        static char cm4_bootargs[256];
        const char *args = LINUX_BOOTARGS;
        if (cm4_rauc_build_bootargs(cm4_bootargs, sizeof(cm4_bootargs)) == 0)
            args = cm4_bootargs;
        if (fdt_fixup_str(fdt, off, "chosen", "bootargs", args) != 0) {
            wolfBoot_printf("cm4: DTB bootargs fixup failed; refusing firmware DTB\n");
            return NULL;
        }
    }
#else
    if (fdt_fixup_str(fdt, off, "chosen", "bootargs", LINUX_BOOTARGS) != 0) {
        wolfBoot_printf("cm4: DTB bootargs fixup failed; refusing firmware DTB\n");
        return NULL;
    }
#endif
    wolfBoot_printf("cm4: DTB relocated to %p, bootargs set\n", fdt);
    return fdt;
#endif /* CM4_FIRMWARE_DTB */
}
#endif /* MMU */

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    XMEMCPY(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    XMEMSET((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    XMEMCPY((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */

void hal_init(void)
{
#if defined(DEBUG_UART)
    unsigned long el;
    /* The banner is emitted before cm4_mmu_enable() on purpose - it is the
     * earliest bring-up signal. This is safe because wolfBoot's uart_printf
     * (src/string.c) is built -mstrict-align and performs no unaligned / SIMD
     * access; do NOT route the banner through a libc printf, which would fault
     * on the MMU-off Device memory this runs on. */
    uart_init();
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    wolfBoot_printf("wolfBoot CM4 (BCM2711 Cortex-A72) hal_init, EL%d\n",
        (int)((el >> 2) & 0x3));
#endif
#if defined(CM4_USE_MMU)
    /* Bring up Normal cacheable memory before any code that uses unaligned /
     * SIMD accesses (FIPS module, optimized disk path) which the MMU-off
     * Device memory rejects. */
    cm4_mmu_enable();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Hand the OS a clean SDHCI controller: wolfBoot just used it (PIO, clocked)
     * for the image + env I/O, and the leftover state makes the OS driver's
     * re-init / tuning intermittently flake. Must run BEFORE the MMU is torn
     * down (the shutdown touches the controller through Normal-cacheable MMIO).
     * disk_close() on this target is a no-op, so the reset happens here. */
    sdhci_shutdown();
#endif
#if defined(CM4_USE_MMU)
    /* Undo cm4_mmu_enable() before handoff: flush the loaded image out of the
     * D-cache and return to the MMU-off state the application expects. */
    cm4_mmu_disable();
#endif
}

#if defined(HAVE_FIPS)
/* Bounded heap for the FIPS module's malloc. wolfBoot builds the FIPS target
 * with --specs=nosys.specs, whose newlib _sbrk grows unbounded from the linker
 * 'end' symbol - toward the unverified image staged at kernel_addr (0x2C0000).
 * Provide our own _sbrk over a fixed static buffer (in .bss, well below the
 * image) so heap growth is bounded and can never reach kernel_addr. */
#ifndef CM4_FIPS_HEAP_SIZE
#define CM4_FIPS_HEAP_SIZE (128 * 1024)
#endif
static unsigned char cm4_fips_heap[CM4_FIPS_HEAP_SIZE];
void* _sbrk(int incr);
void* _sbrk(int incr)
{
    static unsigned char* brk = cm4_fips_heap;
    unsigned char* prev = brk;

    if (incr < 0) {
        /* Heap trim: newlib's malloc returns memory on free() with a negative
         * increment. Clamp to the heap base so brk cannot underflow. */
        if ((size_t)(-incr) > (size_t)(brk - cm4_fips_heap))
            brk = cm4_fips_heap;
        else
            brk += incr;
        return (void*)prev;
    }
    if ((size_t)(brk - cm4_fips_heap) + (size_t)incr > sizeof(cm4_fips_heap))
        return (void*)-1; /* out of heap */
    brk += incr;
    return (void*)prev;
}
#endif /* HAVE_FIPS */

#if defined(CM4_USE_MMU)
/* Minimal identity-mapped MMU + caches for the CM4. wolfBoot's simple startup
 * runs with the MMU off, so all memory is Device-nGnRnE, which faults on the
 * unaligned / 128-bit SIMD accesses that the FIPS module, newlib printf, and
 * the optimized disk/SDHCI code paths perform. Mapping DDR as Normal
 * (cacheable) permits those accesses and speeds up crypto/disk reads; the
 * peripheral region (incl. 0xFE000000) stays Device. Four 1GB block
 * descriptors cover the 32-bit VA space at translation level 1. */
#define MMU_BLOCK_NORMAL  0x0000000000000701ULL /* block, AttrIdx0, AF, SH inner */
/* Device MMIO: mark execute-never (UXN bit 54 | PXN bit 53). Instruction fetch
 * from Device memory is CONSTRAINED UNPREDICTABLE and several BCM2711 registers
 * are read-destructive, so a speculative fetch there is a real hazard. Matches
 * the non-executable Device attributes in boot_aarch64_start.S. */
#define MMU_BLOCK_DEVICE  (0x0000000000000405ULL | (1ULL << 54) | (1ULL << 53))

static volatile uint64_t cm4_l1_table[512] __attribute__((aligned(4096)));

/* Data-cache maintenance by set/way over all levels to the point of coherency.
 * clean != 0 -> clean+invalidate (dc cisw); else invalidate-only (dc isw). */
static void cm4_dcache_maint(int clean)
{
    uint64_t clidr, ccsidr;
    unsigned int level, loc, ctype, linesize, ways, sets, way, set, wayshift;

    __asm__ volatile("dsb sy");
    __asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
    loc = (unsigned int)((clidr >> 24) & 0x7); /* Level of Coherency */
    for (level = 0; level < loc; level++) {
        ctype = (unsigned int)((clidr >> (level * 3)) & 0x7);
        if (ctype < 2) /* no data/unified cache at this level */
            continue;
        __asm__ volatile("msr csselr_el1, %0" :: "r"((uint64_t)(level << 1)));
        __asm__ volatile("isb");
        __asm__ volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));
        linesize = (unsigned int)(ccsidr & 0x7) + 4;          /* log2(bytes) */
        ways     = (unsigned int)((ccsidr >> 3) & 0x3FF);     /* assoc - 1 */
        sets     = (unsigned int)((ccsidr >> 13) & 0x7FFF);   /* sets - 1 */
        /* __builtin_clz(0) is UB; a direct-mapped cache (ways==0) never uses
         * the way field (way stays 0), so the shift amount is irrelevant. */
        wayshift = (ways == 0) ? 32u : (unsigned int)__builtin_clz(ways);
        for (set = 0; set <= sets; set++) {
            for (way = 0; way <= ways; way++) {
                uint64_t val = ((uint64_t)(level << 1))
                    | ((uint64_t)way << wayshift)
                    | ((uint64_t)set << linesize);
                if (clean)
                    __asm__ volatile("dc cisw, %0" :: "r"(val));
                else
                    __asm__ volatile("dc isw, %0" :: "r"(val));
            }
        }
    }
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

/* MMU/cache setup uses EL2 system registers; wolfBoot enters at EL2 on the CM4.
 * Guard against an EL1 entry (a custom armstub) so the msr *_el2 below do not
 * trap silently before anything can be reported. */
static void cm4_require_el2(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (((el >> 2) & 0x3) != 2) {
#if defined(DEBUG_UART)
        wolfBoot_printf("cm4: MMU setup requires EL2 (running at EL%d); halting\n",
            (int)((el >> 2) & 0x3));
#endif
        while (1)
            __asm__ volatile("wfi");
    }
}

void cm4_mmu_enable(void)
{
    unsigned long sctlr;
    int i;

    cm4_require_el2();
    /* 0-3GB DDR -> Normal; 3-4GB peripherals (0xFE000000) -> Device. */
    for (i = 0; i < 4; i++) {
        uint64_t base = (uint64_t)i << 30;
        cm4_l1_table[i] = base | ((i == 3) ? MMU_BLOCK_DEVICE : MMU_BLOCK_NORMAL);
    }
    /* MAIR: Attr0 = 0xFF Normal WB write-alloc, Attr1 = 0x00 Device-nGnRnE. */
    __asm__ volatile("msr mair_el2, %0" :: "r"(0x00000000000000FFUL));
    __asm__ volatile("msr ttbr0_el2, %0"
        :: "r"((uint64_t)(uintptr_t)cm4_l1_table));
    /* TCR_EL2: T0SZ=32 (32-bit VA), 4KB granule, WB cacheable inner-shareable
     * table walks, 36-bit PA. Bits 31 and 23 are RES1 for TCR_EL2 (E2H==0) and
     * must be written as 1. */
    __asm__ volatile("msr tcr_el2, %0"
        :: "r"(0x0000000000013520UL | (1UL << 31) | (1UL << 23)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    /* Invalidate the D-cache (and I-cache) before enabling them, so no stale
     * lines left by an earlier boot stage surface once caching is on. */
    cm4_dcache_maint(0);
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
    /* SCTLR_EL2: enable MMU (M), data cache (C), instruction cache (I). */
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}

/* Tear down the MMU/caches before boot handoff: clean the freshly-copied app
 * out of the D-cache to memory, disable the MMU and caches, and invalidate the
 * I-cache/TLB. Returns the CPU to the MMU-off state the application (and the
 * ARM64 Linux boot protocol) expects. */
void cm4_mmu_disable(void)
{
    unsigned long sctlr;

    cm4_require_el2();
    /* Flush the loaded app to DRAM WHILE the D-cache is still enabled, then
     * disable M/C/I together. The "textbook" order (clear SCTLR.C first, then
     * flush) is UNSAFE here: cm4_dcache_maint() and this function use the stack,
     * and once C is cleared, stack reads bypass the cache and return stale DRAM
     * (the dirty lines - including this function's spilled return address - are
     * not yet written back), so the function would return to garbage. That
     * order is only safe in a pure-asm flush with no stack use (U-Boot). What
     * must be coherent for the application is the loaded image, and it is fully
     * flushed here with caches on. */
    cm4_dcache_maint(1); /* clean+invalidate: flush the loaded app to memory */
    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr &= ~((1UL << 0) | (1UL << 2) | (1UL << 12)); /* clear M, C, I */
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    __asm__ volatile("ic iallu");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
#endif /* CM4_USE_MMU */

#if defined(DEBUG) && defined(DEBUG_UART)
/* CM4 bring-up diagnostic: exception handler invoked from cm4_vectors in
 * src/boot_aarch64_start.S. Dumps the fault syndrome so a data/instruction
 * abort shows up over UART instead of hanging silently. Built only with
 * DEBUG + DEBUG_UART. ESR_EL2[31:26] = exception class. */
void cm4_fault_handler(unsigned long esr, unsigned long elr, unsigned long far);
void cm4_fault_handler(unsigned long esr, unsigned long elr, unsigned long far)
{
    wolfBoot_printf("\n*** CM4 EXCEPTION ***\n");
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
 * A72-clock-dependent spin count (not a wall-clock timeout); it only guards
 * against a wedged RNG so the seed read cannot hang forever. Tune if needed. */
#ifndef RNG200_FIFO_WAIT_ITERS
#define RNG200_FIFO_WAIT_ITERS 200000000U
#endif

/* FIPS DRBG entropy seed from the BCM2711 RNG200 hardware TRNG. Registered via
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

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
/* BCM2711 EMMC2 platform glue for the generic SDHCI driver (src/sdhci.c).
 * EMMC2 is a standard SDHCI v3.0 Arasan block at 0xFE340000. The driver uses
 * Cadence-style SRS offsets (0x200 + std); translate them to the standard
 * Arasan layout, mirroring the ZynqMP path in hal/zynq.c. The GPU firmware has
 * already configured the EMMC2 clock/pinmux, so only a controller soft reset is
 * needed here (hardware-validated on CM4 eMMC). */
#include "sdhci.h"

uint32_t sdhci_reg_read(uint32_t offset)
{
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;

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
    return 0; /* HRS region not present on this Arasan block */
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;
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
    /* The GPU firmware already brought up the EMMC2 clock/pinmux; just issue a
     * controller soft reset and wait for it to clear. */
    volatile uint8_t *base = (volatile uint8_t *)BCM2711_EMMC2_BASE;
    volatile int i;

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
 * boot benchmark). Falls back to the BCM2711 system counter frequency if
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
        freq = BCM2711_TIMER_CLK_FREQ;
    return (uint64_t)(((__uint128_t)count * 1000000ULL) / freq);
#else
    /* Non-AArch64 host build (unit tests): the generic timer is unavailable. */
    return 0;
#endif
}
#endif /* timer guard */
