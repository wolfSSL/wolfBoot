/* imx95_a55_stage1.c
 *
 * wolfBoot stage 1 for the NXP i.MX95 Cortex-A55, in place of U-Boot SPL.
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

/* The boot ROM loads this image from AHAB container 0 into OCRAM and enters it
 * at EL3. Everything before it - the ELE firmware, the System Manager on the
 * M33 and the OEI that trains DDR - is still the SoC's own, because the ROM
 * authenticates and starts them directly. What this replaces is the last image
 * in that container, U-Boot SPL, whose whole job is to find the next container
 * and load BL31, OP-TEE and BL33 out of it.
 *
 * DDR is already up by the time this runs: the OEI did it, and SPL only checks
 * the power domain. So a stage 1 needs the boot device, the container parser,
 * and the handful of platform pokes below that SPL also does. */

#include <stdint.h>
#include <stddef.h>
#include "printf.h"
#include "hal/imx95_a55.h"
#include "hal/imx95_ahab.h"

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* Watchdogs 3 and 4 are left running by a warm reset out of Linux. */
#define WDG3_BASE           0x42490000UL
#define WDG4_BASE           0x424A0000UL
#define WDOG_CS             0x00
#define WDOG_CNT            0x04
#define WDOG_TOVAL          0x08
#define WDOG_WIN            0x0C
#define WDOG_CS_EN          (1UL << 7)
#define WDOG_CS_ULK         (1UL << 11)
#define WDOG_CS_RCS         (1UL << 10)
#define WDOG_REFRESH_WORD   0xB480A602UL
#define WDOG_UNLOCK_WORD    0xD928C520UL
#define WDOG_POLL_LOOPS     1000000UL

/* GPIO2..5 are in the domains this core owns; a warm reset can leave pins
 * driven, so the interrupt and data registers are cleared. */
#define GPIO2_BASE          0x43810000UL
#define GPIO3_BASE          0x43820000UL
#define GPIO4_BASE          0x43840000UL
#define GPIO5_BASE          0x43850000UL

#define SMMU_BASE           0x490D0000UL
#define SMMU_CR0            0x20
#define SMMU_CR0_ACK        0x24
#define SMMU_GBPA           0x44
#define SMMU_GBPA_UPDATE    (1UL << 31)
#define SMMU_GBPA_SHCFG_IN  (1UL << 12)
#define SMMU_POLL_LOOPS     1000000UL

/* EdgeLock Enclave message unit. Stage 1 owns MU1; the later stages use MU3. */
#define ELE_MU_BASE         0x47530000UL
#define ELE_MU_PAR          (ELE_MU_BASE + 0x004)
#define ELE_MU_TCR          (ELE_MU_BASE + 0x120)
#define ELE_MU_TSR          (ELE_MU_BASE + 0x124)
#define ELE_MU_RCR          (ELE_MU_BASE + 0x128)
#define ELE_MU_RSR          (ELE_MU_BASE + 0x12C)
#define ELE_MU_TR(n)        (ELE_MU_BASE + 0x200 + ((n) * 4))
#define ELE_MU_RR(n)        (ELE_MU_BASE + 0x280 + ((n) * 4))
#define ELE_MU_SR           (ELE_MU_BASE + 0x00C)
#define ELE_MU_SR_RDR       (1UL << 6)      /* receive data ready */
#define ELE_MU_POLL_LOOPS   1000000UL

#define ELE_VERSION         0x06U
#define ELE_CMD_TAG         0x17U
#define ELE_START_RNG       0xA3U
#define ELE_OK              0xD6U
#define ELE_MAX_MSG         8U

extern int imx95_scmi_arm_max_clk(void);
extern int imx95_scmi_ddr_powered(void);
extern int imx95_scmi_uart_clk_init(void);
extern int imx95_usdhc1_cold_init(void);
extern int imx95_usdhc2_cold_init(void);
extern void uart_init(void);
extern int disk_init(int drv);
extern int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf);

/* Every wait in this stage is bounded: a bootloader that spins on a status bit
 * that never arrives is harder to diagnose than one that reports and moves on,
 * and there is no watchdog left to rescue it. */
static int wdog_wait(uintptr_t base, uint32_t bit)
{
    uint32_t n;

    for (n = 0; n < WDOG_POLL_LOOPS; n++) {
        if ((rd(base + WDOG_CS) & bit) != 0UL)
            return 0;
    }
    return -1;
}

static void wdog_disable(uintptr_t base)
{
    uint32_t cs = rd(base + WDOG_CS);

    if ((cs & WDOG_CS_EN) == 0UL)
        return;

    wr(base + WDOG_CNT, WDOG_REFRESH_WORD);
    if ((cs & WDOG_CS_ULK) == 0UL) {
        wr(base + WDOG_CNT, WDOG_UNLOCK_WORD);
        if (wdog_wait(base, WDOG_CS_ULK) != 0) {
            wolfBoot_printf("stage1: watchdog 0x%x did not unlock\n",
                (unsigned)base);
            return;
        }
    }
    wr(base + WDOG_WIN, 0);
    wr(base + WDOG_TOVAL, 0x400);
    wr(base + WDOG_CS, 0x2120);
    if (wdog_wait(base, WDOG_CS_RCS) != 0)
        wolfBoot_printf("stage1: watchdog 0x%x did not acknowledge\n",
            (unsigned)base);
}

static void gpio_reset(uintptr_t base)
{
    wr(base + 0x10, 0);
    wr(base + 0x14, 0);
    wr(base + 0x18, 0);
    wr(base + 0x1C, 0);
}

/* Linux can be reset while it is in the middle of disabling the SMMU, so this
 * is done unconditionally rather than after a state check. */
static int smmu_disable(void)
{
    uint32_t n;

    for (n = 0; n < SMMU_POLL_LOOPS; n++) {
        if ((rd(SMMU_BASE + SMMU_GBPA) & SMMU_GBPA_UPDATE) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    /* Use incoming SHCFG attributes */
    wr(SMMU_BASE + SMMU_GBPA, SMMU_GBPA_SHCFG_IN | SMMU_GBPA_UPDATE);
    for (n = 0; n < SMMU_POLL_LOOPS; n++) {
        if ((rd(SMMU_BASE + SMMU_GBPA) & SMMU_GBPA_UPDATE) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    wr(SMMU_BASE + SMMU_CR0, 0);
    for (n = 0; n < SMMU_POLL_LOOPS; n++) {
        if (rd(SMMU_BASE + SMMU_CR0_ACK) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    return 0;
}

/* One ELE message: words are handed over one transmit register at a time and
 * the reply comes back the same way. msg[0] carries version, word count,
 * command and tag; the first reply word after it is the status. */
static int ele_call(uint32_t *msg, uint32_t nwords)
{
    uint32_t tr_num, rr_num, i, n, sr;

    if (nwords == 0U || nwords > ELE_MAX_MSG)
        return -1;

    tr_num = rd(ELE_MU_PAR) & 0xFFU;
    rr_num = (rd(ELE_MU_PAR) >> 8) & 0xFFU;
    if (tr_num == 0U || rr_num == 0U)
        return -1;

    /* Drain anything a previous owner left behind, bounded in case the mailbox
     * keeps re-asserting. */
    wr(ELE_MU_TCR, 0);
    wr(ELE_MU_RCR, 0);
    for (n = 0; n < ELE_MAX_MSG; n++) {
        if ((rd(ELE_MU_SR) & ELE_MU_SR_RDR) == 0UL)
            break;
        for (i = 0; i < rr_num; i++)
            (void)rd(ELE_MU_RR(i));
    }

    for (i = 0; i < nwords; i++) {
        for (n = 0; n < ELE_MU_POLL_LOOPS; n++) {
            sr = rd(ELE_MU_TSR);
            if ((sr & (1UL << (i % tr_num))) != 0UL)
                break;
        }
        if (n == ELE_MU_POLL_LOOPS)
            return -1;
        wr(ELE_MU_TR(i % tr_num), msg[i]);
    }

    for (i = 0; i < nwords; i++) {
        for (n = 0; n < ELE_MU_POLL_LOOPS; n++) {
            sr = rd(ELE_MU_RSR);
            if ((sr & (1UL << (i % rr_num))) != 0UL)
                break;
        }
        if (n == ELE_MU_POLL_LOOPS)
            return -1;
        msg[i] = rd(ELE_MU_RR(i % rr_num));
        /* The reply header says how many words the ELE is actually sending. */
        if (i == 0U) {
            nwords = (msg[0] >> 8) & 0xFFU;
            if (nwords == 0U || nwords > ELE_MAX_MSG)
                return -1;
        }
    }

    if (nwords < 2U)
        return -1;
    if ((msg[1] & 0xFFU) != ELE_OK)
        return -1;
    return 0;
}

/* Starts the ELE's random generator. Later stages - OP-TEE and Linux - expect
 * it to be running, and only the first caller after reset may start it. */
static int ele_start_rng(void)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = (uint32_t)ELE_VERSION | ((uint32_t)1 << 8) |
             ((uint32_t)ELE_START_RNG << 16) | ((uint32_t)ELE_CMD_TAG << 24);
    return ele_call(msg, 1);
}

/* Everything SPL does between the console coming up and the container load. */
int imx95_stage1_platform_init(void)
{
    int ret;

    wdog_disable(WDG3_BASE);
    wdog_disable(WDG4_BASE);

    gpio_reset(GPIO2_BASE);
    gpio_reset(GPIO3_BASE);
    gpio_reset(GPIO4_BASE);
    gpio_reset(GPIO5_BASE);

    if (smmu_disable() != 0)
        wolfBoot_printf("stage1: SMMU disable timed out\n");

    /* Without this the A55 cluster stays at its reset rate for the whole of
     * the boot, which is the single biggest thing this stage controls. */
    if (imx95_scmi_arm_max_clk() != 0)
        wolfBoot_printf("stage1: ARM clock set failed\n");

    /* Only a definitive "off" is fatal. A query the System Manager refuses says
     * nothing about DDR, and stopping the boot over it would be worse than the
     * problem it is meant to catch. */
    ret = imx95_scmi_ddr_powered();
    if (ret == 0) {
        wolfBoot_printf("stage1: DDR is powered off - OEI did not run\n");
        return -1;
    }
    if (ret < 0)
        wolfBoot_printf("stage1: DDR power state unavailable (%d)\n", ret);

    if (ele_start_rng() != 0)
        wolfBoot_printf("stage1: ELE RNG start failed\n");

    return 0;
}

/* Bring up the controller the ROM booted from and point ordinary reads at the
 * same place the ROM read the containers from. On eMMC that is a boot
 * partition, selected by the same EXT_CSD field that told the ROM which one to
 * use; on SD the containers sit in the raw area ahead of the partitions. */
static int imx95_stage1_disk_init(void)
{
#ifdef DISK_EMMC
    int part;

    if (imx95_usdhc1_cold_init() != 0)
        return -1;
    if (disk_init(0) != 0)
        return -1;
    part = imx95_emmc_boot_partition();
    if (part < 1) {
        wolfBoot_printf("stage1: no eMMC boot partition enabled\n");
        return -1;
    }
    wolfBoot_printf("stage1: eMMC boot%d\n", part - 1);
    return imx95_emmc_select_partition(part);
#else
    if (imx95_usdhc2_cold_init() != 0)
        return -1;
    return disk_init(0);
#endif
}

/* Reads len bytes at byte offset off from the boot device. Both are multiples
 * of the block size; disk_read() takes byte units and answers with how many it
 * read, so a short read is a failure here. */
static int stage1_read(void *ctx, uint32_t off, uint32_t len, void *buf)
{
    (void)ctx;
    if (disk_read(0, (uint64_t)off, len, (uint8_t *)buf) != (int)len)
        return -1;
    return 0;
}

/* Called from the exception vectors with the syndrome, the faulting
 * instruction and the faulting address. */
void imx95_stage1_fault(uint64_t esr, uint64_t elr, uint64_t far)
{
    wolfBoot_printf("stage1: exception ESR=0x%x%x ELR=0x%x%x FAR=0x%x%x\n",
        (unsigned)(esr >> 32), (unsigned)esr,
        (unsigned)(elr >> 32), (unsigned)elr,
        (unsigned)(far >> 32), (unsigned)far);
}

extern uint8_t _start_text[];
extern uint8_t END_STACK[];

/* True when loading this image would land on top of the code doing the
 * loading. In the boot chain nothing does - the stage runs from OCRAM and the
 * images it loads go to DRAM - but a stage 1 linked into DRAM for bring-up is
 * reached by being an image of the very container it then walks. */
static int stage1_overlaps_self(uint64_t dst, uint32_t size)
{
    uint64_t lo = (uint64_t)(uintptr_t)_start_text;
    uint64_t hi = (uint64_t)(uintptr_t)END_STACK;

    return (dst < hi) && ((dst + size) > lo);
}

/* Load every image of the container that follows the one the ROM booted from,
 * then enter the first one loaded. That is BL31, which knows where OP-TEE and
 * BL33 were placed; this is the same handoff U-Boot SPL makes. */
static int stage1_boot(void)
{
    static struct imx95_ahab_container ctnr;
    void (*entry)(void);
    uint64_t bl31_entry = 0;
    uint32_t next, i;

    if (imx95_scmi_uart_clk_init() != 0) {
        /* Nothing can be reported: this is what the console runs on. */
        return -1;
    }
    uart_init();
    wolfBoot_printf("\nwolfBoot stage 1: NXP i.MX95 Cortex-A55\n");

    if (imx95_stage1_platform_init() != 0)
        return -1;

    if (imx95_stage1_disk_init() != 0) {
        wolfBoot_printf("stage1: boot device init failed\n");
        return -1;
    }

    /* Container 0 is the one the ROM read; the next one starts where it ends. */
    if (imx95_ahab_parse(&ctnr, IMX95_AHAB_MMC_OFFSET, stage1_read, NULL) != 0) {
        wolfBoot_printf("stage1: no container at 0x%x\n",
            (unsigned)IMX95_AHAB_MMC_OFFSET);
        return -1;
    }
    next = imx95_ahab_next(&ctnr);

    if (imx95_ahab_parse(&ctnr, next, stage1_read, NULL) != 0) {
        wolfBoot_printf("stage1: no container at 0x%x\n", (unsigned)next);
        return -1;
    }

    for (i = 0; i < ctnr.count; i++) {
        if (ctnr.img[i].size == 0U)
            continue;
        if (stage1_overlaps_self(ctnr.img[i].dst, ctnr.img[i].size)) {
            wolfBoot_printf("stage1: skipping image %u at 0x%x (that is us)\n",
                (unsigned)i, (unsigned)ctnr.img[i].dst);
            continue;
        }
        wolfBoot_printf("stage1: image %u -> 0x%x, %u bytes\n",
            (unsigned)i, (unsigned)ctnr.img[i].dst,
            (unsigned)ctnr.img[i].size);
        if (imx95_ahab_load(&ctnr, i, (void *)(uintptr_t)ctnr.img[i].dst,
                            stage1_read, NULL) != 0) {
            wolfBoot_printf("stage1: image %u load failed\n", (unsigned)i);
            return -1;
        }
        if (bl31_entry == 0U)
            bl31_entry = ctnr.img[i].entry;
    }

    if (bl31_entry == 0U) {
        wolfBoot_printf("stage1: container has no entry point\n");
        return -1;
    }

    wolfBoot_printf("stage1: entering BL31 at 0x%x\n", (unsigned)bl31_entry);

    /* Caches are off, so the loads above are already in memory. */
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    entry = (void (*)(void))(uintptr_t)bl31_entry;
    entry();
    return -1;
}

int imx95_stage1_main(void)
{
    int ret = stage1_boot();

#ifdef IMX95_STAGE1_PASSTHROUGH
    /* Bring-up build: this stage is an extra image in a container that some
     * other loader already staged completely, so its job is to be invisible
     * when it fails. Production does not define this - there a failure means
     * nothing has been loaded, and stopping is the only honest answer. */
    void (*bl31)(void) = (void (*)(void))(uintptr_t)IMX95_BL31_BASE;

    wolfBoot_printf("stage1: passing through to BL31\n");
    bl31();
#endif
    return ret;
}
