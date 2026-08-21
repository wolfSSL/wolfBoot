/* imx95_m7.c
 *
 * HAL for the Cortex-M7 on the NXP i.MX95.
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

/* The M7 on this part has no dedicated flash. wolfBoot is loaded into TCM by
 * the Linux remoteproc driver on the A55 cluster, and the images it verifies
 * live in the DDR region reserved for the M7 (memory@80000000, 16 MiB). The
 * "flash" the partitions sit in is therefore ordinary DDR: writes are copies
 * and erases are fills. Updates are staged by Linux writing the update
 * partition before restarting the core.
 *
 * Addresses come from the i.MX 95 Reference Manual rev 5 ch. 123 and from the
 * board's own device tree; see BOARD-FINDINGS.md in the demo tree.
 *
 *   ITCM   core view 0x00000000   system view 0x203C0000   256 KiB
 *   DTCM   core view 0x20000000   system view 0x20400000   256 KiB
 *   DDR    0x80000000                                       16 MiB
 *
 * The core/system distinction matters: wolfBoot is linked for the core view,
 * while remoteproc loads through the system view and the imx_rproc driver
 * translates between them.
 *
 * Chain of trust
 *
 * The M7 has no flash of its own: wolfBoot and the images it verifies are
 * placed in TCM and DDR by the A55 cluster, so trust in what runs here is
 * anchored on the A55 side. Three mechanisms provide it:
 *
 *   - AHAB, anchored in the SRK fuses, authenticates the boot containers.
 *   - wolfBoot on the A55, replacing U-Boot, verifying and staging this image
 *     under its own partition id before Linux starts. Planned; this is the
 *     piece we intend to support.
 *   - The TRDC, configured by the System Manager on the M33, restricting the
 *     M7 TCM and DDR carveout to the M7 domain.
 *
 * Until those are in place the image is selected by Linux, so verification
 * here protects against corruption and mis-staged updates rather than against
 * a compromised A55. See docs/Targets.md.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "hal/imx95_m7.h"

/* ---------------------------------------------------------------------------
 * remoteproc resource table
 *
 * Linux finds the resource table by looking for a .resource_table section in
 * the ELF *it* loads. On this target that ELF is wolfBoot, not the payload, so
 * the vdev/vring declaration has to live here even though the RPMsg endpoint is
 * serviced later by whatever wolfBoot boots. Without it the kernel logs
 * "No resource table in elf" and never creates the virtio device.
 *
 * The addresses are the ones the board's device tree already reserves, so the
 * two sides agree by construction rather than by convention:
 *
 *   vdev0vring0   0x88000000   32 KiB
 *   vdev0vring1   0x88008000   32 KiB
 *   vdevbuffer    0x88020000    1 MiB
 *
 * A vring of 256 buffers at 4 KiB alignment needs about 10 KiB, so each fits
 * its 32 KiB reservation with room to spare.
 * ------------------------------------------------------------------------- */

#define RSC_VDEV            3U
#define VIRTIO_ID_RPMSG     7U
#define VIRTIO_RPMSG_F_NS   (1U << 0) /* name service announcements */

#define RSC_VRING0_DA       0x88000000U
#define RSC_VRING1_DA       0x88008000U
#define RSC_VRING_ALIGN     0x1000U
#define RSC_VRING_NUM       256U

struct fw_rsc_vdev_vring {
    uint32_t da;
    uint32_t align;
    uint32_t num;
    uint32_t notifyid;
    uint32_t reserved;
};

struct fw_rsc_vdev {
    uint32_t type;
    uint32_t id;
    uint32_t notifyid;
    uint32_t dfeatures;
    uint32_t gfeatures;
    uint32_t config_len;
    uint8_t  status;
    uint8_t  num_of_vrings;
    uint8_t  reserved[2];
    struct fw_rsc_vdev_vring vring[2];
};

struct imx95_rsc_table {
    uint32_t ver;
    uint32_t num;
    uint32_t reserved[2];
    uint32_t offset[1];
    struct fw_rsc_vdev vdev;
};

const struct imx95_rsc_table resource_table
    __attribute__((section(".resource_table"), used)) = {
    1,                                    /* ver */
    1,                                    /* num: one entry */
    { 0, 0 },                             /* reserved */
    { offsetof(struct imx95_rsc_table, vdev) },
    {
        RSC_VDEV,
        VIRTIO_ID_RPMSG,
        0,                                /* notifyid, assigned by the host */
        VIRTIO_RPMSG_F_NS,                /* dfeatures */
        0,                                /* gfeatures, filled by the host */
        0,                                /* config_len */
        0,                                /* status, driven by the host */
        2,                                /* num_of_vrings */
        { 0, 0 },
        {
            { RSC_VRING0_DA, RSC_VRING_ALIGN, RSC_VRING_NUM, 0, 0 },
            { RSC_VRING1_DA, RSC_VRING_ALIGN, RSC_VRING_NUM, 1, 0 }
        }
    }
};

/* Erased state of the DDR-backed pseudo-flash. */
#define FLASH_ERASED_BYTE 0xFFU

/* The M7 has no confirmed UART route on this carrier, so early bring-up
 * reports progress through the shared status block that Linux can read with
 * devmem instead of over a serial port. Its address, and the console ring
 * immediately below it, live in hal/imx95_m7.h.
 *
 * Both caches are OFF out of reset. wolfBoot's own text runs from ITCM (see
 * hal/imx95_m7.ld), so the I-cache buys it little; the D-cache is what matters,
 * because verifying an image means hashing multiple megabytes that live in
 * DDR - measured at ~664 cycles per byte for SHA-256 with the cache off,
 * roughly fifty times slower than the core is capable of.
 *
 * The ARMv7-M default memory map already marks 0x80000000-0x9FFFFFFF as normal
 * write-through cacheable memory, so no MPU region is needed to make this take
 * effect, and stores to the shared window still reach DDR. */

/* Fill sets/ways/shift from CCSIDR for the L1 data cache. The shifts are
 * geometry-dependent (ARMv7-M DDI 0403: way field starts at 32-log2(assoc),
 * set field at log2(line size)), so derive both rather than assuming the
 * Cortex-M7's fixed 4-way, 32-byte-line configuration. */
static void hal_dcache_geometry(uint32_t *sets, uint32_t *ways,
    uint32_t *way_shift, uint32_t *set_shift)
{
    uint32_t ccsidr, tmp, shift;

    SCB_CSSELR = 0UL; /* select L1 data cache */
    DSB();
    ccsidr = SCB_CCSIDR;

    *sets = ((ccsidr >> 13) & 0x7FFFUL) + 1UL;
    *ways = ((ccsidr >> 3) & 0x3FFUL) + 1UL;
    *set_shift = (ccsidr & 0x7UL) + 4UL;

    shift = 32UL;
    tmp = *ways - 1UL;
    while (tmp != 0UL) {
        shift--;
        tmp >>= 1;
    }
    *way_shift = shift;
}

static void hal_cache_enable(void)
{
    uint32_t sets, ways, way_shift, set_shift, s, w;

    /* I-cache: invalidate, then enable. */
    DSB(); ISB();
    SCB_ICIALLU = 0UL;
    DSB(); ISB();
    SCB_CCR |= CCR_IC;
    DSB(); ISB();

    /* D-cache: invalidate every set/way before enabling, otherwise stale
     * lines from reset can be written back over live DDR. */
    hal_dcache_geometry(&sets, &ways, &way_shift, &set_shift);
    for (s = 0; s < sets; s++) {
        for (w = 0; w < ways; w++) {
            SCB_DCISW = ((w << way_shift) | (s << set_shift));
        }
    }
    DSB();
    SCB_CCR |= CCR_DC;
    DSB(); ISB();
}

/* Clean every dirty line back to DDR and drop the I-cache. Used before handing
 * off to an image wolfBoot may have just written through the D-cache: the
 * Cortex-M7 instruction side does not snoop the data side, so without this the
 * jump would rely on the default map happening to be write-through. */
static void hal_cache_flush(void)
{
    uint32_t sets, ways, way_shift, set_shift, s, w;

    hal_dcache_geometry(&sets, &ways, &way_shift, &set_shift);
    for (s = 0; s < sets; s++) {
        for (w = 0; w < ways; w++) {
            SCB_DCCSW = ((w << way_shift) | (s << set_shift));
        }
    }
    DSB();
    SCB_ICIALLU = 0UL;
    DSB(); ISB();
}

static int hal_addr_is_valid(uint32_t address, int len)
{
    uint64_t start = (uint64_t)address;
    uint64_t end;

    if (len < 0)
        return 0;
    end = start + (uint64_t)len;
    if (start < (uint64_t)M7_DDR_BASE)
        return 0;
    if (end > (uint64_t)M7_DDR_BASE + (uint64_t)M7_DDR_SIZE)
        return 0;
    return 1;
}

/* hal_init()/hal_prepare_boot() are deliberately NOT wrapped in
 * #ifdef __WOLFBOOT: test-app/Makefile pulls ../hal/imx95_m7.o into APP_OBJS
 * and rebuilds it in place with the application's flags, so an object that
 * only defines them for the bootloader build would be clobbered and break the
 * next wolfboot.elf link. The application simply never calls them, and
 * --gc-sections drops them from its image.
 *
 * Progress codes stamped into the shared status block. Kept deliberately
 * trivial: no locking, no ordering guarantees beyond the store itself. */
#define HAL_STATUS_INIT    1U
#define HAL_STATUS_PREBOOT 2U

/* Record a progress code where the A55 side can see it. The block is only
 * WOLFBOOT_STATUS_WORDS words wide and the word above it is the test app's
 * liveness magic, so the timestamp slot is selected explicitly rather than
 * indexed by the caller's code. */
static void hal_status(uint32_t code)
{
    volatile uint32_t *status = (volatile uint32_t *)WOLFBOOT_STATUS_ADDR;

    status[0] = WOLFBOOT_STATUS_MAGIC;
    status[1] = code;
    /* status[2] is stamped at hal_init, status[3] at hal_prepare_boot; the
     * difference is the cost of everything wolfBoot does in between, which is
     * dominated by the signature verification. */
    switch (code) {
        case HAL_STATUS_INIT:
            status[2] = DWT_CYCCNT;
            break;
        case HAL_STATUS_PREBOOT:
            status[3] = DWT_CYCCNT;
            break;
        default:
            break;
    }
    imx95_dcache_clean((const void *)status,
        WOLFBOOT_STATUS_WORDS * sizeof(uint32_t));
}

void hal_init(void)
{
    /* Clocks, DDR and the TCM split are configured before this core is
     * released: DDR by the OEI/SPL stage, the TCM split by the System Manager
     * on the M33 via M7_CFG[TCM_SIZE] in BLK_CTRL_Secure_AON. There is
     * nothing for wolfBoot to bring up here except the caches, which are off
     * out of reset and which this image needs because the partitions it hashes
     * live in DDR.
     *
     * .bss and .data are handled by src/boot_arm.c:isr_reset(): remoteproc
     * loads only PT_LOAD segments and .bss is NOBITS, so nothing external
     * zeroes it before entry. */
    hal_cache_enable();
    imx95_dwt_init();
    hal_status(HAL_STATUS_INIT);
}

void hal_prepare_boot(void)
{
    hal_status(HAL_STATUS_PREBOOT);
    /* wolfBoot may have just written the image being booted into DDR through
     * the D-cache; make that visible to the instruction side before jumping. */
    hal_cache_flush();
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    if (data == NULL)
        return -1;
    if (!hal_addr_is_valid(address, len))
        return -1;

    memcpy((void *)(uintptr_t)address, data, (size_t)len);
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    if (!hal_addr_is_valid(address, len))
        return -1;

    memset((void *)(uintptr_t)address, FLASH_ERASED_BYTE, (size_t)len);
    return 0;
}
