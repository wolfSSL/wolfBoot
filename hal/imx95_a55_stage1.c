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

/* The boot ROM loads this from AHAB container 0 into OCRAM and enters at EL3,
 * replacing U-Boot SPL: find the next container and load BL31, OP-TEE and BL33.
 * DDR is already up from the OEI. */

#include <stdint.h>
#include <stddef.h>
#include "printf.h"
#include "hal/imx95_a55.h"
#include "hal/imx95_ahab.h"

/* Both bring-up switches below defeat authentication outright, so neither may be
 * combined with it: PASSTHROUGH jumps to BL31 after a reported failure, turning
 * "refuse to boot" into "boot the thing that just failed". */
#if defined(IMX95_AHAB_AUTH) && defined(IMX95_STAGE1_PASSTHROUGH)
#error "IMX95_STAGE1_PASSTHROUGH enters BL31 after a failed load; it cannot be used with IMX95_AHAB_AUTH"
#endif
#if defined(IMX95_AHAB_AUTH) && defined(IMX95_STAGE1_ALLOW_SELF_OVERLAP)
#error "IMX95_STAGE1_ALLOW_SELF_OVERLAP skips an image the container asked to load; it cannot be used with IMX95_AHAB_AUTH"
#endif

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* Watchdogs 3 and 4 are left running by a warm reset out of Linux. */
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
#define SMMU_CR0            0x20
#define SMMU_CR0_ACK        0x24
#define SMMU_GBPA           0x44
#define SMMU_GBPA_UPDATE    (1UL << 31)
#define SMMU_GBPA_SHCFG_IN  (1UL << 12)
#define SMMU_POLL_LOOPS     1000000UL

/* EdgeLock Enclave message unit. Stage 1 owns MU1; the later stages use MU3. */
#define ELE_MU_PAR          (IMX95_ELE_MU_BASE + 0x004)
#define ELE_MU_TCR          (IMX95_ELE_MU_BASE + 0x120)
#define ELE_MU_TSR          (IMX95_ELE_MU_BASE + 0x124)
#define ELE_MU_RCR          (IMX95_ELE_MU_BASE + 0x128)
#define ELE_MU_RSR          (IMX95_ELE_MU_BASE + 0x12C)
#define ELE_MU_TR(n)        (IMX95_ELE_MU_BASE + 0x200 + ((n) * 4))
#define ELE_MU_RR(n)        (IMX95_ELE_MU_BASE + 0x280 + ((n) * 4))
#define ELE_MU_SR           (IMX95_ELE_MU_BASE + 0x00C)
#define ELE_MU_SR_RDR       (1UL << 6)      /* receive data ready */

/* A loop count is the wrong unit here: AHAB authentication hashes the whole
 * image inside the enclave, so the wait is bounded in seconds, not iterations. */
#define ELE_TIMEOUT_MS      5000UL

/* Enough iterations to see a 24 MHz counter tick, and - for the no-clock
 * fallback - a spin long enough to cover an enclave hashing an image. */
#define ELE_TIMER_PROBE_LOOPS   1000UL
#define ELE_MU_POLL_LOOPS       100000000UL

#define ELE_VERSION         0x06U
#define ELE_CMD_TAG         0x17U
#define ELE_RESP_TAG        0xE1U
#define ELE_START_RNG       0xA3U
#define ELE_OK              0xD6U
#define ELE_MAX_MSG         8U

/* AHAB container authentication, exactly as U-Boot's SPL issues it. */
#define ELE_CNTR_AUTH       0x87U
#define ELE_VERIFY_IMAGE    0x88U
#define ELE_RELEASE_CNTR    0x89U

/* Indications worth naming: the rest are reported as a raw byte. */
#define ELE_IND_NO_AUTH     0xEEU   /* container carries no signature */
#define ELE_IND_BAD_SIG     0xF0U
#define ELE_IND_LIFECYCLE   0xF2U
#define ELE_IND_BAD_CNTR    0xF7U
#define ELE_IND_RNG_STOPPED 0xB8U
#define ELE_IND_BAD_OP      0xC0U   /* e.g. release with no context held */

/* An ELE call fails two ways. A clean negative reply leaves the mailbox usable,
 * so a release can follow. A protocol failure - timeout, or a reply not matching
 * what was sent - does not: a later message could consume the abandoned reply. */
#define ELE_CALL_OK         0
#define ELE_CALL_REJECTED   (-1)
#define ELE_CALL_BROKEN     (-2)

/* Lifecycle, so every boot says which one it is rather than leaving an "AHAB
 * OK" line to imply more than it means. */
#define FSB_LC_OFFSET       0x414U
#define FSB_LC_MASK         0x3FFU
#define LC_OEM_OPEN         0x10U
#define LC_OEM_SWC          0x20U   /* secure world closed */
#define LC_OEM_CLOSED       0x40U

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
        if ((rd(IMX95_SMMU_BASE + SMMU_GBPA) & SMMU_GBPA_UPDATE) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    /* Use incoming SHCFG attributes */
    wr(IMX95_SMMU_BASE + SMMU_GBPA, SMMU_GBPA_SHCFG_IN | SMMU_GBPA_UPDATE);
    for (n = 0; n < SMMU_POLL_LOOPS; n++) {
        if ((rd(IMX95_SMMU_BASE + SMMU_GBPA) & SMMU_GBPA_UPDATE) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    wr(IMX95_SMMU_BASE + SMMU_CR0, 0);
    for (n = 0; n < SMMU_POLL_LOOPS; n++) {
        if (rd(IMX95_SMMU_BASE + SMMU_CR0_ACK) == 0UL)
            break;
    }
    if (n == SMMU_POLL_LOOPS)
        return -1;

    return 0;
}

/* The generic timer is readable at EL3 regardless of MMU state, so it is the
 * only clock available to bound a wait in this stage - but see timer_deadline()
 * for why being readable does not mean it is counting. */
static uint64_t timer_ticks(void)
{
    uint64_t v;

    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

/* Returns 0 when the system counter is not advancing, which mu_wait() reads as
 * "no clock". Nothing below BL31 guarantees the counter is enabled, and a
 * deadline a stalled CNTPCT_EL0 never reaches is not a bound at all. */
static uint64_t timer_deadline(uint32_t ms)
{
    uint64_t hz, t0;
    uint32_t n;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    if (hz == 0ULL)
        hz = 24000000ULL;   /* the SoC's fixed rate, if CNTFRQ is unset */

    t0 = timer_ticks();
    for (n = 0; n < ELE_TIMER_PROBE_LOOPS; n++) {
        if (timer_ticks() != t0)
            return t0 + ((hz / 1000ULL) * (uint64_t)ms);
    }
    return 0;
}

/* Wait for a status bit, bounded by wall time where there is a clock to read
 * and by iterations where there is not. */
static int mu_wait(uintptr_t reg, uint32_t bit, uint64_t deadline)
{
    uint32_t n;

    if (deadline == 0ULL) {
        for (n = 0; n < ELE_MU_POLL_LOOPS; n++) {
            if ((rd(reg) & bit) != 0UL)
                return 0;
        }
        return -1;
    }
    do {
        if ((rd(reg) & bit) != 0UL)
            return 0;
    } while (timer_ticks() < deadline);
    return -1;
}

/* One ELE message, a word per transmit register. msg[0] is version, word count,
 * command and tag; the next reply word is the status. The reply's command is
 * checked so an abandoned response is not misread; resp's [15:8] says why. */
static int ele_call(uint32_t cmd, uint32_t *msg, uint32_t nwords,
                    uint32_t *resp)
{
    uint32_t tr_num, rr_num, i, n, reply_words;
    uint64_t deadline;

    if (resp != NULL)
        *resp = 0;
    if (nwords == 0U || nwords > ELE_MAX_MSG)
        return ELE_CALL_BROKEN;

    tr_num = rd(ELE_MU_PAR) & 0xFFU;
    rr_num = (rd(ELE_MU_PAR) >> 8) & 0xFFU;
    /* PAR is a hardware constant, but an unclocked MU reads back as all-ones
     * and these index TR/RR, so a bad count must not reach a register offset. */
    if (tr_num == 0U || rr_num == 0U ||
            tr_num > ELE_MAX_MSG || rr_num > ELE_MAX_MSG)
        return ELE_CALL_BROKEN;

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

    /* The enclave reads the staged container over its own master port, so the
     * stores that placed it must be observable before the doorbell. */
    __asm__ volatile("dsb sy" ::: "memory");

    deadline = timer_deadline(ELE_TIMEOUT_MS);

    for (i = 0; i < nwords; i++) {
        if (mu_wait(ELE_MU_TSR, 1UL << (i % tr_num), deadline) != 0)
            return ELE_CALL_BROKEN;
        wr(ELE_MU_TR(i % tr_num), msg[i]);
    }

    reply_words = nwords;
    for (i = 0; i < reply_words; i++) {
        if (mu_wait(ELE_MU_RSR, 1UL << (i % rr_num), deadline) != 0)
            return ELE_CALL_BROKEN;
        msg[i] = rd(ELE_MU_RR(i % rr_num));
        /* The reply header says how many words the ELE is actually sending. */
        if (i == 0U) {
            reply_words = (msg[0] >> 8) & 0xFFU;
            if (reply_words < 2U || reply_words > ELE_MAX_MSG)
                return ELE_CALL_BROKEN;
            /* A reply that is not this command's reply means the mailbox is
             * out of step; reading further would compound the confusion. */
            if (((msg[0] >> 24) & 0xFFU) != ELE_RESP_TAG)
                return ELE_CALL_BROKEN;
            if (((msg[0] >> 16) & 0xFFU) != cmd)
                return ELE_CALL_BROKEN;
        }
    }

    if (resp != NULL)
        *resp = msg[1];
    if ((msg[1] & 0xFFU) != ELE_OK)
        return ELE_CALL_REJECTED;
    return ELE_CALL_OK;
}

static void ele_report(const char *what, uint32_t resp)
{
    wolfBoot_printf("stage1: ELE %s failed, status 0x%x (indication 0x%x)\n",
        what, (unsigned)resp, (unsigned)((resp >> 8) & 0xFFU));
}

/* Starts the ELE's random generator. Later stages - OP-TEE and Linux - expect
 * it to be running, and only the first caller after reset may start it. */
static int ele_start_rng(void)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = (uint32_t)ELE_VERSION | ((uint32_t)1 << 8) |
             ((uint32_t)ELE_START_RNG << 16) | ((uint32_t)ELE_CMD_TAG << 24);
    return ele_call(ELE_START_RNG, msg, 1, NULL);
}

#ifdef IMX95_AHAB_LIFECYCLE
/* Which lifecycle the part is in: on an open part AHAB reports success without
 * enforcing a signature, so the distinction is the difference between
 * integrity and authenticity. */
static uint32_t ele_lifecycle(void)
{
    return rd(IMX95_FSB_BASE + FSB_LC_OFFSET) & FSB_LC_MASK;
}

static void ele_report_lifecycle(uint32_t lc)
{
    const char *name = "unknown";

    if (lc == LC_OEM_OPEN)
        name = "OEM open - AHAB does not enforce signatures";
    else if (lc == LC_OEM_SWC)
        name = "OEM secure world closed";
    else if (lc == LC_OEM_CLOSED)
        name = "OEM closed";
    wolfBoot_printf("stage1: lifecycle 0x%x (%s)\n", (unsigned)lc, name);
}
#endif /* IMX95_AHAB_LIFECYCLE */

#ifdef IMX95_AHAB_AUTH
/* Hand the ELE the staged container. Everything it goes on to verify is taken
 * from those bytes, not from the medium they came off. */
static int ele_auth_container(uint64_t addr, uint32_t *resp)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = (uint32_t)ELE_VERSION | ((uint32_t)3 << 8) |
             ((uint32_t)ELE_CNTR_AUTH << 16) | ((uint32_t)ELE_CMD_TAG << 24);
    msg[1] = (uint32_t)(addr >> 32);
    msg[2] = (uint32_t)addr;
    return ele_call(ELE_CNTR_AUTH, msg, 3, resp);
}

/* Verify one image where it now sits. The payload is a bitmask, not an index,
 * but it is sent one bit at a time so a failure names the image. */
static int ele_verify_image(uint32_t index, uint32_t *resp)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = (uint32_t)ELE_VERSION | ((uint32_t)2 << 8) |
             ((uint32_t)ELE_VERIFY_IMAGE << 16) | ((uint32_t)ELE_CMD_TAG << 24);
    msg[1] = 1UL << index;
    return ele_call(ELE_VERIFY_IMAGE, msg, 2, resp);
}

/* The enclave holds one authentication context at a time, so this has to run
 * before the jump as well as on every failure that left the mailbox usable. */
static int ele_release_container(uint32_t *resp)
{
    uint32_t msg[ELE_MAX_MSG];

    msg[0] = (uint32_t)ELE_VERSION | ((uint32_t)1 << 8) |
             ((uint32_t)ELE_RELEASE_CNTR << 16) | ((uint32_t)ELE_CMD_TAG << 24);
    return ele_call(ELE_RELEASE_CNTR, msg, 1, resp);
}
#endif /* IMX95_AHAB_AUTH */

/* Everything SPL does between the console coming up and the container load. */
int imx95_stage1_platform_init(void)
{
    int ret;

    wdog_disable(IMX95_WDG3_BASE);
    wdog_disable(IMX95_WDG4_BASE);

    gpio_reset(IMX95_GPIO2_BASE);
    gpio_reset(IMX95_GPIO3_BASE);
    gpio_reset(IMX95_GPIO4_BASE);
    gpio_reset(IMX95_GPIO5_BASE);

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

    if (ele_start_rng() != 0) {
        wolfBoot_printf("stage1: ELE RNG start failed\n");
#ifdef IMX95_AHAB_AUTH
        /* Without it the enclave answers ELE_IND_RNG_STOPPED to the services
         * that need it, authentication among them, so this stops being a
         * warning once the boot depends on the ELE answering. */
        return -1;
#endif
    }

    return 0;
}

/* Bring up the controller the ROM booted from and point ordinary reads where it
 * read the containers: on eMMC a boot partition, from the same EXT_CSD field the
 * ROM used; on SD the raw area ahead of the partitions. */
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
    /* disk_init() runs the uSDHC2 cold-init itself when IMX95_SCMI_COLD_INIT is
     * set, and repeating it would repeat the regulator settle delay. */
#if !(defined(IMX95_SCMI_COLD_INIT) && defined(DISK_SDCARD))
    if (imx95_usdhc2_cold_init() != 0)
        return -1;
#endif
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
    /* Two halves, because the small printf may lack long long. The low word
     * needs the width or its leading zeros are dropped and the halves run
     * together, which misreports the register exactly when it matters. */
    wolfBoot_printf("stage1: exception ESR=0x%08x%08x ELR=0x%08x%08x "
        "FAR=0x%08x%08x\n",
        (unsigned)(esr >> 32), (unsigned)esr,
        (unsigned)(elr >> 32), (unsigned)elr,
        (unsigned)(far >> 32), (unsigned)far);
}

extern uint8_t _start_text[];
extern uint8_t END_STACK[];

/* True when an image would land on the code loading it. Nothing does in the real
 * chain (OCRAM loader, DRAM images), but a stage 1 linked into DRAM for bring-up
 * is itself an image of the container it walks. */
/* True when loading this image would land on the code doing the loading. The
 * padded length is what matters, so the arithmetic lives in imx95_ahab.c where
 * the host tests can reach it. */
static int stage1_overlaps_self(uint64_t dst, uint32_t size)
{
    return imx95_ahab_overlaps(dst, size, (uint64_t)(uintptr_t)_start_text,
                               (uint64_t)(uintptr_t)END_STACK);
}

/* Windows no container image may load over. Staging is first because it is the
 * least obvious: the ELE keeps reading it while the authentication is
 * outstanding, so an image there would replace the blessed bytes. */
static const struct imx95_ahab_region stage1_excl[] = {
    { IMX95_AHAB_STAGE_BASE, IMX95_AHAB_STAGE_SIZE },
    { IMX95_OPTEE_SHM_BASE,  IMX95_OPTEE_SHM_SIZE  },
    { IMX95_M7_DDR_BASE,     IMX95_M7_DDR_SIZE     },
    { IMX95_ELE_SHM_BASE,    IMX95_ELE_SHM_SIZE    },
    { IMX95_VPU_BOOT_BASE,   IMX95_VPU_BOOT_SIZE   },
};

#define STAGE1_EXCL_COUNT \
    ((uint32_t)(sizeof(stage1_excl) / sizeof(stage1_excl[0])))

/* Stage the container header, table and signature block in DDR and parse from
 * there: the ELE requires DDR, and every value acted on must come from the same
 * bytes it saw, since a medium can answer differently the second time. */
static int stage1_stage_container(uint32_t base,
                                 struct imx95_ahab_container *ctnr)
{
    uint8_t *stage = (uint8_t *)(uintptr_t)IMX95_AHAB_STAGE_BASE;
    uint32_t span, want;

    if (stage1_read(NULL, base, (uint32_t)IMX95_AHAB_HDR_BYTES, stage) != 0)
        return -1;
    if (imx95_ahab_parse_buf(ctnr, base, stage,
                             (uint32_t)IMX95_AHAB_HDR_BYTES) != 0)
        return -1;
    if (imx95_ahab_span(ctnr, &span) != 0)
        return -1;

    if (span > (uint32_t)IMX95_AHAB_HDR_BYTES) {
        /* Re-read the whole span in one go. Splicing the first read to a tail
         * would give two reads of one structure, which is a hole on a medium
         * that need not answer consistently. */
        want = (span + (IMX95_AHAB_BLOCK - 1U)) & ~(IMX95_AHAB_BLOCK - 1U);
        if (want > (uint32_t)IMX95_AHAB_STAGE_SIZE)
            return -1;
        if (stage1_read(NULL, base, want, stage) != 0)
            return -1;
        if (imx95_ahab_parse_buf(ctnr, base, stage, span) != 0)
            return -1;
    }

    return 0;
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
#ifdef IMX95_AHAB_AUTH
    uint32_t resp = 0;
    int ret, held = 0;
#endif

    if (imx95_scmi_uart_clk_init() != 0) {
        /* Nothing can be reported: this is what the console runs on. */
        return -1;
    }
#if defined(DEBUG_UART)
    uart_init();
    wolfBoot_printf("\nwolfBoot stage 1: NXP i.MX95 Cortex-A55\n");
#endif

    if (imx95_stage1_platform_init() != 0)
        return -1;

    if (imx95_stage1_disk_init() != 0) {
        wolfBoot_printf("stage1: boot device init failed\n");
        return -1;
    }

#ifdef IMX95_AHAB_LIFECYCLE
    /* Opt-in: U-Boot reads the lifecycle only from U-Boot proper and only under
     * CONFIG_AHAB_BOOT, so whether the FSB answers the A55 in SPL's slot is
     * untested per part, and a diagnostic must not cost a working boot. */
    ele_report_lifecycle(ele_lifecycle());
#endif

    /* Container 0 is the one the ROM read; the next one starts where it ends. */
    if (imx95_ahab_parse(&ctnr, IMX95_AHAB_MMC_OFFSET, stage1_read, NULL) != 0) {
        wolfBoot_printf("stage1: no container at 0x%x\n",
            (unsigned)IMX95_AHAB_MMC_OFFSET);
        return -1;
    }
    if (imx95_ahab_next(&ctnr, &next) != 0) {
        wolfBoot_printf("stage1: container 0 does not lead anywhere\n");
        return -1;
    }

    if (stage1_stage_container(next, &ctnr) != 0) {
        wolfBoot_printf("stage1: no container at 0x%x\n", (unsigned)next);
        return -1;
    }

#ifdef IMX95_AHAB_AUTH
    ret = ele_auth_container((uint64_t)IMX95_AHAB_STAGE_BASE, &resp);
    if (ret == ELE_CALL_REJECTED &&
            ((resp >> 8) & 0xFFU) == (uint32_t)ELE_IND_BAD_OP) {
        /* A context an interrupted boot left held. Release once, retry once. */
        (void)ele_release_container(NULL);
        ret = ele_auth_container((uint64_t)IMX95_AHAB_STAGE_BASE, &resp);
    }
    if (ret != ELE_CALL_OK) {
        ele_report("container authenticate", resp);
        /* Only a clean refusal leaves the mailbox usable enough to release on. */
        if (ret == ELE_CALL_REJECTED)
            (void)ele_release_container(NULL);
        return -1;
    }
    held = 1;
    wolfBoot_printf("stage1: container authenticated by ELE\n");
#endif

    /* Everything below acts on the staged copy, and destinations are checked in
     * full before any byte is written: an image streams straight into img->dst,
     * so by the time the ELE could object the bytes have landed. */
    if (imx95_ahab_check_dst(&ctnr, (uint64_t)IMX95_DDR_BASE,
                             (uint64_t)IMX95_DDR_SIZE,
                             stage1_excl, STAGE1_EXCL_COUNT) != 0) {
        wolfBoot_printf("stage1: container has an unusable load address\n");
        goto release;
    }
    if (imx95_ahab_check_exec(&ctnr, 0) != 0) {
        wolfBoot_printf("stage1: image 0 is not an A55 executable\n");
        goto release;
    }

    for (i = 0; i < ctnr.count; i++) {
        if (stage1_overlaps_self(ctnr.img[i].dst, ctnr.img[i].size)) {
#ifdef IMX95_STAGE1_ALLOW_SELF_OVERLAP
            /* Bring-up only: a stage 1 linked into DRAM is itself an image of
             * the container it walks, so it has to skip its own entry. */
            wolfBoot_printf("stage1: skipping image %u at 0x%x (that is us)\n",
                (unsigned)i, (unsigned)ctnr.img[i].dst);
            if (i == 0U) {
                wolfBoot_printf("stage1: image 0 skipped, nothing to enter\n");
                goto release;
            }
            continue;
#else
            /* A container asking to be written over the running loader is a
             * hostile-container signal, not something to work around. */
            wolfBoot_printf("stage1: image %u would overwrite this loader\n",
                (unsigned)i);
            goto release;
#endif
        }
        wolfBoot_printf("stage1: image %u -> 0x%x, %u bytes\n",
            (unsigned)i, (unsigned)ctnr.img[i].dst,
            (unsigned)ctnr.img[i].size);
        if (imx95_ahab_load(&ctnr, i, (void *)(uintptr_t)ctnr.img[i].dst,
                            stage1_read, NULL) != 0) {
            wolfBoot_printf("stage1: image %u load failed\n", (unsigned)i);
            goto release;
        }
#ifdef IMX95_AHAB_AUTH
        /* Verify where it now sits, one image at a time so a failure names it. */
        ret = ele_verify_image(i, &resp);
        if (ret != ELE_CALL_OK) {
            wolfBoot_printf("stage1: image %u failed verification\n",
                (unsigned)i);
            ele_report("image verify", resp);
            if (ret == ELE_CALL_BROKEN)
                held = 0;   /* mailbox is out of step; send nothing more */
            goto release;
        }
#endif
    }

    bl31_entry = ctnr.img[0].entry;

#ifdef IMX95_AHAB_AUTH
    /* Release before the jump, not on the way out of an error path: the success
     * path never returns, and the enclave holds one context at a time. */
    if (held) {
        if (ele_release_container(&resp) != ELE_CALL_OK)
            ele_report("container release", resp);
        held = 0;
    }
#endif

    wolfBoot_printf("stage1: entering BL31 at 0x%x\n", (unsigned)bl31_entry);

    /* Caches are off, so the loads above are already in memory. */
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    entry = (void (*)(void))(uintptr_t)bl31_entry;
    entry();
    return -1;

release:
#ifdef IMX95_AHAB_AUTH
    if (held)
        (void)ele_release_container(NULL);
#endif
    return -1;
}

int imx95_stage1_main(void)
{
    int ret = stage1_boot();

#ifdef IMX95_STAGE1_PASSTHROUGH
    /* Bring-up only: here this stage is an extra image in a container another
     * loader already staged, so it should be invisible when it fails. In
     * production a failure means nothing loaded, and stopping is the answer. */
    void (*bl31)(void) = (void (*)(void))(uintptr_t)IMX95_BL31_BASE;

    wolfBoot_printf("stage1: passing through to BL31\n");
    bl31();
#endif
    return ret;
}
