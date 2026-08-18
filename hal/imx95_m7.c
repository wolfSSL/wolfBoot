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
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <target.h>
#include "image.h"

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

/* Bounds of the M7's reserved DDR window. Anything outside it is rejected
 * rather than silently corrupting memory that belongs to Linux. */
#define M7_DDR_BASE 0x80000000UL
#define M7_DDR_SIZE 0x01000000UL

/* Liveness marker. The M7 has no confirmed UART route on this carrier, so
 * early bring-up reports progress through shared memory that Linux can read
 * with devmem instead of over a serial port.
 *
 * This lives inside the M7's own DDR window, immediately above the console
 * ring at 0x80F00000, and deliberately NOT in the 0x88000000 block: the device
 * tree reserves that for RPMsg (vdev0/vdev1 vrings, and a 1 MiB vdevbuffer at
 * 0x88020000), so a status word there would be scribbling on buffers Linux
 * owns as soon as RPMsg is brought up. */
#define WOLFBOOT_STATUS_ADDR 0x80F10000UL
#define WOLFBOOT_STATUS_MAGIC 0x57424F54UL /* "WBOT" */

/* Cortex-M7 DWT cycle counter. With no usable console on this carrier the
 * cycle counter is how the signature-verification cost gets measured: it is
 * exact, free to read, and needs no peripheral. Timestamps are published
 * alongside the status word for the A55 to read. */
#define CORE_DEMCR   (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA (1UL << 24)
#define DWT_CTRL     (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNTENA (1UL << 0)
#define DWT_CYCCNT   (*(volatile uint32_t *)0xE0001004UL)

/* Cortex-M7 cache maintenance. Both caches are OFF out of reset, and this
 * image runs its text from DDR, so leaving them off means every instruction
 * fetch and data access goes to external memory - measured at ~664 cycles per
 * byte for SHA-256, roughly fifty times slower than the core is capable of.
 *
 * The ARMv7-M default memory map already marks 0x80000000-0x9FFFFFFF as normal
 * cacheable memory, so no MPU region is needed to make this take effect. */
#define SCB_CCR     (*(volatile uint32_t *)0xE000ED14UL)
#define SCB_CCSIDR  (*(volatile uint32_t *)0xE000ED80UL)
#define SCB_CSSELR  (*(volatile uint32_t *)0xE000ED84UL)
#define SCB_ICIALLU (*(volatile uint32_t *)0xE000EF50UL)
#define SCB_DCISW   (*(volatile uint32_t *)0xE000EF60UL)
#define CCR_IC      (1UL << 17)
#define CCR_DC      (1UL << 16)

#define DSB() __asm__ volatile ("dsb 0xF" ::: "memory")
#define ISB() __asm__ volatile ("isb 0xF" ::: "memory")

static void hal_cache_enable(void)
{
    uint32_t ccsidr, sets, ways, s, w;

    /* I-cache: invalidate, then enable. */
    DSB(); ISB();
    SCB_ICIALLU = 0UL;
    DSB(); ISB();
    SCB_CCR |= CCR_IC;
    DSB(); ISB();

    /* D-cache: invalidate every set/way before enabling, otherwise stale
     * lines from reset can be written back over live DDR. */
    SCB_CSSELR = 0UL; /* select L1 data cache */
    DSB();
    ccsidr = SCB_CCSIDR;
    sets = (ccsidr >> 13) & 0x7FFFUL;
    ways = (ccsidr >> 3) & 0x3FFUL;
    for (s = 0; s <= sets; s++) {
        for (w = 0; w <= ways; w++) {
            /* DCISW: way in [31:30], set in [13:5] for a 32-byte line. */
            SCB_DCISW = ((w << 30) | (s << 5));
        }
    }
    DSB();
    SCB_CCR |= CCR_DC;
    DSB(); ISB();
}

static void hal_cycle_counter_start(void)
{
    CORE_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CYCCNTENA;
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

#ifdef __WOLFBOOT

/* Record a progress code where the A55 side can see it. Kept deliberately
 * trivial: no locking, no ordering guarantees beyond the write itself. */
void hal_status(uint32_t code)
{
    volatile uint32_t *status = (volatile uint32_t *)WOLFBOOT_STATUS_ADDR;

    status[0] = WOLFBOOT_STATUS_MAGIC;
    status[1] = code;
    /* status[2] is stamped at hal_init, status[3] at hal_prepare_boot; the
     * difference is the cost of everything wolfBoot does in between, which is
     * dominated by the signature verification. */
    status[code + 1] = DWT_CYCCNT;
}

void hal_init(void)
{
    /* Clocks, DDR and the TCM split are configured before this core is
     * released: DDR by the OEI/SPL stage, the TCM split by the System Manager
     * on the M33 via M7_CFG[TCM_SIZE] in BLK_CTRL_Secure_AON. There is
     * nothing for wolfBoot to bring up here except the caches, which are off
     * out of reset and which this image needs because it runs from DDR. */
    hal_cache_enable();
    hal_cycle_counter_start();
    hal_status(1);
}

void hal_prepare_boot(void)
{
    hal_status(2);
}

#endif /* __WOLFBOOT */

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
