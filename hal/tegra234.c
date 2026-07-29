/* tegra234.c
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

/*
 * Bare-metal HAL for the NVIDIA Jetson Orin (Tegra234, Cortex-A78AE): wolfBoot
 * as the BL33 firmware stage, replacing the edk2 UEFI payload. BL33 is the
 * normal-world bootloader that ARM Trusted Firmware (BL31) hands off to at EL2
 * non-secure. This HAL owns the console (Tegra Combined UART), the generic
 * timer, and BPMP IPC, and drives the BL33 -> EL1 handoff.
 *
 * Validated on hardware: verifies a signed payload with wolfCrypt and boots it
 * EL2 -> EL1 with a device tree in x0 (the arm64 Linux boot contract), straight
 * out of DRAM with no storage driver. Storage (SDHCI, DISK_SDCARD) is in
 * progress; see the SDHCI section. The board-agnostic Orin path that runs under
 * UEFI is the aarch64_efi target (hal/aarch64_efi.c). Register definitions are
 * in hal/tegra234.h.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "image.h"
#include "printf.h"
#include "hal/tegra234.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"
#endif

#ifndef ARCH_AARCH64
#   error "wolfBoot tegra234 HAL: wrong architecture. Compile with ARCH=AARCH64."
#endif

/* Handoff x0 captured at reset by the .boot stub (src/boot_aarch64_start.S):
 * the DTB/params pointer the prior stage passed per the arm64 boot protocol.
 * The nonzero initializer keeps it in .data, out of reach of the BSS clear. */
volatile uint64_t boot_handoff_x0 = 0xFFFFFFFFFFFFFFFFULL;

/* Flattened Device Tree magic 0xd00dfeed, stored big-endian -> reads back as
 * 0xedfe0dd0 on this little-endian core. */
#define FDT_MAGIC_LE  0xedfe0dd0u

/* MMU is off throughout, so all MMIO below is Device/non-cacheable and
 * strongly ordered: no cache maintenance is needed, and "dmb sy" covers the
 * ordering the BPMP IVC protocol requires. */
static inline void dmb_sy(void) { __asm__ volatile("dmb sy" ::: "memory"); }
static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/* --------------------------------------------------------------------------
 * ARMv8 generic timer (architectural; no SoC register needed)
 * -------------------------------------------------------------------------- */

static inline uint64_t timer_get_count(void)
{
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r" (cntpct));
    return cntpct;
}

static inline uint64_t timer_get_freq(void)
{
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (cntfrq));
    return cntfrq ? cntfrq : TIMER_CLK_FREQ;
}

uint64_t hal_get_timer_us(void)
{
    return (timer_get_count() * 1000000ULL) / timer_get_freq();
}

/* Deadline helpers for the polling loops below: one division at setup, none
 * in the loop itself. */
static uint64_t timer_deadline_us(uint32_t us)
{
    return timer_get_count() + (((uint64_t)us * timer_get_freq()) / 1000000ULL);
}

static int timer_expired(uint64_t deadline)
{
    return timer_get_count() > deadline;
}

void hal_delay_us(uint32_t us)
{
    uint64_t deadline = timer_deadline_us(us);

    while (!timer_expired(deadline))
        ;
}

/* --------------------------------------------------------------------------
 * Console: Tegra Combined UART (TCU) via the AON HSP shared mailbox.
 * See hal/tegra234.h for the mailbox address derivation and the protocol.
 * -------------------------------------------------------------------------- */

/* Give up on a mailbox write if the SPE stops draining, rather than hanging
 * the boot inside printf. */
#define TCU_TX_TIMEOUT_US   10000

#if defined(DEBUG_UART)
void uart_init(void)
{
    /* The TCU/SPE mailbox is already live from the earlier boot stages; no
     * clock, pinmux, or baud setup is needed (or possible) here. */
}

/* Send one mailbox message carrying 1..3 packed bytes. */
static void tcu_tx_word(uint32_t bytes, unsigned int count)
{
    uint64_t deadline = timer_deadline_us(TCU_TX_TIMEOUT_US);

    while ((rd32(TEGRA_TCU_TX_MBOX) & TEGRA_TCU_MBOX_FULL) != 0) {
        if (timer_expired(deadline))
            return; /* drop the characters; do not stall the boot */
    }
    wr32(TEGRA_TCU_TX_MBOX, TEGRA_TCU_MBOX_FULL |
        ((uint32_t)count << TEGRA_TCU_MBOX_NBYTES_SHIFT) | bytes);
}

void uart_write(const char* buf, unsigned int sz)
{
    uint32_t bytes = 0;
    unsigned int count = 0;
    unsigned int i = 0;
    int insert_nl = 0;
    char c;

    while (i < sz) {
        if (insert_nl) {
            c = '\n';
            insert_nl = 0;
            i++;
        }
        else if (buf[i] == '\n') {
            c = '\r';
            insert_nl = 1;
        }
        else {
            c = buf[i++];
        }
        bytes |= (uint32_t)(uint8_t)c << (count * 8);
        if (++count == TEGRA_TCU_MBOX_MAX_BYTES) {
            tcu_tx_word(bytes, count);
            bytes = 0;
            count = 0;
        }
    }
    if (count > 0)
        tcu_tx_word(bytes, count);
}
#endif /* DEBUG_UART */

/* --------------------------------------------------------------------------
 * Partition / load addresses. The signed payload and, for a Linux boot, the
 * DTB are bundled into the BL33 image at the offsets in hal/tegra234.h, so
 * update_ram boots them straight out of DRAM with no storage driver.
 * -------------------------------------------------------------------------- */

void* hal_get_primary_address(void)
{
    return (void*)(uintptr_t)(TEGRA234_BL33_BASE + TEGRA234_BUNDLE_OFFSET);
}

/* No update partition is populated on this target yet: there is no storage
 * driver, and the bundling scripts write only the boot payload. This returns
 * the reserved DRAM staging address so the update API has a valid target once
 * a storage path lands. */
void* hal_get_update_address(void)
{
    return (void*)(uintptr_t)TEGRA234_UPDATE_ADDR;
}

void* hal_get_dts_address(void)
{
    uintptr_t dtb = (uintptr_t)(TEGRA234_BL33_BASE + TEGRA234_DTB_OFFSET);
    uint32_t totalsize;

    /* Only claim a DTB if one was actually bundled here: a plain
     * "make wolfboot.bin" (no bundling script) leaves this offset empty, and
     * update_ram would otherwise hand the payload a garbage pointer in x0.
     * NOTE: the bundled DTB is covered by whatever signs the BL33 image, not
     * by wolfBoot's payload signature. */
    if (rd32(dtb) != FDT_MAGIC_LE)
        return NULL;
    /* totalsize (big-endian, at offset 4) must fit in what is left of the
     * BL33 image: update_ram copies that many bytes, and the bundled DTB is
     * not covered by wolfBoot's payload signature. */
    totalsize = __builtin_bswap32(rd32(dtb + 4));
    if (totalsize < 8 ||
            totalsize > (TEGRA234_CPUBL_MAX_SIZE - TEGRA234_DTB_OFFSET))
        return NULL;
    /* update_ram relocates this to WOLFBOOT_LOAD_DTS_ADDRESS and hands the
     * pointer to the payload in x0. */
    return (void*)dtb;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}

/* --------------------------------------------------------------------------
 * External-flash hooks. Not reached by either shipped config (the payload is
 * bundled in the BL33 image and read directly); provided as flat memory
 * accessors for a build that turns EXT_FLASH on.
 * -------------------------------------------------------------------------- */

#ifdef EXT_FLASH
int ext_flash_read(unsigned long address, uint8_t *data, int len)
{
    memcpy(data, (void *)address, len);
    return len;
}

int ext_flash_erase(unsigned long address, int len)
{
    memset((void *)address, 0xFF, len);
    return len;
}

int ext_flash_write(unsigned long address, const uint8_t *data, int len)
{
    memcpy((void *)address, data, len);
    return len;
}

void ext_flash_lock(void)
{
}

void ext_flash_unlock(void)
{
}
#endif /* EXT_FLASH */

/* --------------------------------------------------------------------------
 * Handoff recon (bring-up, TEGRA234_HANDOFF_DUMP): print the machine state the
 * prior stage handed wolfBoot - entry EL, SCTLR MMU/cache bits, the x0
 * DTB/params pointer - to confirm the BL33 entry contract on new BSP/silicon.
 * -------------------------------------------------------------------------- */

#if defined(DEBUG_UART) && defined(TEGRA234_HANDOFF_DUMP)

static inline uint64_t read_current_el(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (v >> 2) & 0x3;
}

static inline uint64_t read_mpidr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

/* The sysreg name is encoded in the instruction, so read the SCTLR that
 * matches the EL we are actually at (a higher EL's would trap). */
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

/* Print as two 32-bit halves: correct whether or not the small UART printf
 * was built with PRINTF_LONG_LONG. */
static void dump64(const char* name, uint64_t v)
{
    wolfBoot_printf("%s0x%08x%08x\n", name,
        (uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFFUL));
}

static void tegra234_handoff_dump(void)
{
    uint64_t el = read_current_el();
    uint64_t sctlr = read_current_sctlr(el);
    uint64_t x0 = boot_handoff_x0;
    uint64_t cntfrq;
    const uint8_t* p;
    int i;

    /* Raw register, not timer_get_freq(): a zero here is exactly what the
     * dump is meant to reveal, and the fallback would hide it. */
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    wolfBoot_printf("TEGRA234 handoff recon:\n");
    wolfBoot_printf("  CurrentEL:   EL%d\n", (int)el);
    dump64("  SCTLR_ELx:   ", sctlr);
    wolfBoot_printf("    MMU=%d I$=%d D$=%d\n",
        (int)(sctlr & 0x1), (int)((sctlr >> 12) & 0x1),
        (int)((sctlr >> 2) & 0x1));
    dump64("  MPIDR_EL1:   ", read_mpidr());
    dump64("  CNTFRQ_EL0:  ", cntfrq);
    dump64("  handoff x0:  ", x0);

    /* Dump the head of the handoff pointer only if it lands in DRAM (avoid
     * faulting on a stray value). A DTB starts with 0xd00dfeed big-endian. */
    if (x0 >= TEGRA234_DRAM_BASE && x0 <= (TEGRA234_DRAM_END - 16)) {
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
#endif /* DEBUG_UART && TEGRA234_HANDOFF_DUMP */

/* --------------------------------------------------------------------------
 * BPMP IPC: clocks and resets are BPMP-owned, so the SDMMC1 registers are
 * inaccessible at BL33 (a bare read of the controller base faults with a CBB
 * slave error) until the clock is enabled over the BPMP's CPU-NS IVC channel.
 * Ported from Linux (drivers/firmware/tegra/{ivc.c,bpmp*.c}); channel layout
 * and MRQ definitions are in hal/tegra234.h.
 * -------------------------------------------------------------------------- */

/* Only the storage path needs the BPMP. Deliberately NOT built for the default
 * (RAM boot) configs: enabling the SDMMC1 clock and releasing its reset is a
 * lasting change to SoC state that the OS would inherit, so wolfBoot does not
 * touch the BPMP unless it actually has to drive the controller. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC) || defined(TEGRA234_SDMMC_PROBE)

#define BPMP_IVC_RESET_TIMEOUT_US   100000     /* handshake with the BPMP  */
#define BPMP_XFER_TIMEOUT_US        1000000    /* one MRQ round-trip       */
#define BPMP_ERR_TIMEOUT            (-1000)
#define BPMP_ERR_BADARG             (-1001)

static uint32_t bpmp_rx_count;  /* frames consumed from the BPMP */
static int bpmp_ready;          /* IVC channel established */

/* Ring the BPMP doorbell. The top0 HSP doorbell block base is computed from
 * INT_DIMENSIONING; the trigger register is base + index*0x100, where the
 * index for master BPMP (19) is TEGRA_HSP_DB_BPMP (3). */
static void bpmp_ring_doorbell(void)
{
    uint32_t dim = rd32(TEGRA_HSP_TOP0 + TEGRA_HSP_INT_DIMENSIONING);
    uint32_t nsm = dim & 0xf, nss = (dim >> 4) & 0xf, nas = (dim >> 8) & 0xf;
    uintptr_t db = TEGRA_HSP_TOP0
                   + (uintptr_t)(1u + nsm / 2u + nss + nas) * 0x10000UL
                   + (uintptr_t)TEGRA_HSP_DB_BPMP * 0x100UL;
    dmb_sy();
    wr32(db, 1); /* HSP_DB_TRIGGER */
}

/* Drive the IVC SYNC->ACK->ESTABLISHED handshake with the BPMP. */
static int bpmp_ivc_reset(void)
{
    uint64_t deadline;
    uint32_t peer, mine;

    wr32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_STATE, TEGRA_IVC_SYNC);
    dmb_sy();
    bpmp_ring_doorbell();

    deadline = timer_deadline_us(BPMP_IVC_RESET_TIMEOUT_US);
    while (!timer_expired(deadline)) {
        dmb_sy();
        peer = rd32(TEGRA_BPMP_RXCH + TEGRA_IVC_TX_STATE);
        mine = rd32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_STATE);
        if (mine == TEGRA_IVC_ESTABLISHED)
            return 0;
        if (peer == TEGRA_IVC_SYNC || (mine == TEGRA_IVC_SYNC &&
                                       peer == TEGRA_IVC_ACK)) {
            wr32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_COUNT, 0);
            wr32(TEGRA_BPMP_RXCH + TEGRA_IVC_RX_COUNT, 0);
            bpmp_rx_count = 0;
            dmb_sy();
            wr32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_STATE,
                (peer == TEGRA_IVC_SYNC) ? TEGRA_IVC_ACK :
                                           TEGRA_IVC_ESTABLISHED);
            bpmp_ring_doorbell();
        }
        else if (mine == TEGRA_IVC_ACK) {
            wr32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_STATE, TEGRA_IVC_ESTABLISHED);
            bpmp_ring_doorbell();
        }
    }
    return -1;
}

/* Establish the channel once; subsequent callers reuse it. */
static int bpmp_init(void)
{
    if (bpmp_ready)
        return 0;
    if (bpmp_ivc_reset() != 0)
        return -1;
    bpmp_ready = 1;
    return 0;
}

/* One synchronous MRQ round-trip. Returns the BPMP response code, or
 * BPMP_ERR_TIMEOUT. num_frames==1, so the frame is always at channel+HDR. */
static int32_t bpmp_transfer(uint32_t mrq, const void* tx, unsigned txsz,
                             void* rx, unsigned rxsz)
{
    uintptr_t ob = TEGRA_BPMP_TXCH + TEGRA_IVC_HDR_SZ;
    uintptr_t ib = TEGRA_BPMP_RXCH + TEGRA_IVC_HDR_SZ;
    uint64_t deadline;
    int32_t ret;

    /* Fail fast on a channel that was never established, rather than paying
     * the full round-trip timeout per call. */
    if (!bpmp_ready)
        return BPMP_ERR_TIMEOUT;
    if (txsz > TEGRA_BPMP_MB_DATA_SZ || rxsz > TEGRA_BPMP_MB_DATA_SZ)
        return BPMP_ERR_BADARG;

    wr32(ob + TEGRA_BPMP_MB_CODE, mrq);
    wr32(ob + TEGRA_BPMP_MB_FLAGS, TEGRA_BPMP_MSG_ACK);
    /* Clear the whole payload first: the frame is reused across MRQs, so a
     * short request would otherwise inherit the tail of a longer one. */
    memset((void*)(ob + TEGRA_BPMP_MB_DATA), 0, TEGRA_BPMP_MB_DATA_SZ);
    if (tx != NULL && txsz > 0)
        memcpy((void*)(ob + TEGRA_BPMP_MB_DATA), tx, txsz);
    dmb_sy();
    wr32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_COUNT,
         rd32(TEGRA_BPMP_TXCH + TEGRA_IVC_TX_COUNT) + 1);
    bpmp_ring_doorbell();

    deadline = timer_deadline_us(BPMP_XFER_TIMEOUT_US);
    do {
        dmb_sy();
        if (rd32(TEGRA_BPMP_RXCH + TEGRA_IVC_TX_COUNT) != bpmp_rx_count)
            break;
        if (timer_expired(deadline)) {
            /* We incremented the TX count but never consumed a response, so
             * the channel is now out of step with the BPMP. Drop it: every
             * later call fails fast instead of misreading a stale frame. */
            bpmp_ready = 0;
            return BPMP_ERR_TIMEOUT;
        }
    } while (1);

    ret = (int32_t)rd32(ib + TEGRA_BPMP_MB_CODE);
    if (rx != NULL && rxsz > 0)
        memcpy(rx, (void*)(ib + TEGRA_BPMP_MB_DATA), rxsz);
    bpmp_rx_count++;
    wr32(TEGRA_BPMP_RXCH + TEGRA_IVC_RX_COUNT, bpmp_rx_count);
    dmb_sy();
    return ret;
}

static int32_t bpmp_clk_enable(uint32_t clk_id)
{
    uint32_t req = (TEGRA_BPMP_CMD_CLK_ENABLE << 24) | (clk_id & 0xFFFFFF);
    return bpmp_transfer(TEGRA_BPMP_MRQ_CLK, &req, sizeof(req), NULL, 0);
}

static int32_t bpmp_clk_set_rate(uint32_t clk_id, int64_t rate)
{
    uint8_t req[16];
    uint32_t cmd = (TEGRA_BPMP_CMD_CLK_SET_RATE << 24) | (clk_id & 0xFFFFFF);

    memset(req, 0, sizeof(req));
    memcpy(&req[0], &cmd, sizeof(cmd));  /* cmd_and_id           */
    memcpy(&req[8], &rate, sizeof(rate));/* [4]=unused, [8]=rate */
    return bpmp_transfer(TEGRA_BPMP_MRQ_CLK, req, sizeof(req), NULL, 0);
}

static int64_t bpmp_clk_get_rate(uint32_t clk_id)
{
    uint32_t req = (TEGRA_BPMP_CMD_CLK_GET_RATE << 24) | (clk_id & 0xFFFFFF);
    int64_t rate = -1;

    if (bpmp_transfer(TEGRA_BPMP_MRQ_CLK, &req, sizeof(req),
                      &rate, sizeof(rate)) != 0)
        return -1;
    return rate;
}

static int32_t bpmp_reset_deassert(uint32_t reset_id)
{
    uint32_t req[2];

    req[0] = TEGRA_BPMP_CMD_RESET_DEASSERT;
    req[1] = reset_id;
    return bpmp_transfer(TEGRA_BPMP_MRQ_RESET, req, sizeof(req), NULL, 0);
}

/* Keep the first failure while still running the whole sequence. */
static int32_t bpmp_first_err(int32_t err, int32_t ret)
{
    return (err != 0) ? err : ret;
}

/* Enable + un-reset the SDMMC1 controller so its SDHCI registers become
 * accessible. Idempotent. Order: rate (BPMP picks a parent), enable, then
 * release the reset. The controller advertises a 208 MHz base in CAPS, so the
 * module clock matches it and the SDHCI internal divider produces the 400 kHz
 * init clock. tmclk is separate and must also run (see hal/tegra234.h). */
static int tegra234_sdmmc1_clock_on(void)
{
    int32_t err = 0;

    if (bpmp_init() != 0)
        return -1;
    err = bpmp_first_err(err, bpmp_clk_set_rate(TEGRA234_CLK_SDMMC1,
                                                TEGRA234_SDMMC1_BASE_RATE));
    err = bpmp_first_err(err, bpmp_clk_enable(TEGRA234_CLK_SDMMC1));
    err = bpmp_first_err(err,
              bpmp_clk_set_rate(TEGRA234_CLK_SDMMC_LEGACY_TM,
                                TEGRA234_SDMMC_TMCLK_RATE));
    err = bpmp_first_err(err, bpmp_clk_enable(TEGRA234_CLK_SDMMC_LEGACY_TM));
    err = bpmp_first_err(err, bpmp_reset_deassert(TEGRA234_RESET_SDMMC1));
    return (err == 0) ? 0 : -1;
}
#endif /* BPMP transport */

#if defined(DEBUG_UART) && defined(TEGRA234_SDMMC_PROBE)
/* Bring-up: establish the BPMP channel, PING it, then clock + un-reset SDMMC1
 * and re-probe its SDHCI registers (which faulted before the clock was on). */
static void tegra234_bpmp_bringup(void)
{
    uint32_t chal = 0x5a5a5a5a, reply = 0;
    uintptr_t sd = TEGRA_SDMMC1_BASE;
    int64_t rate;
    int32_t r;

    wolfBoot_printf("BPMP: ivc reset...\n");
    if (bpmp_init() != 0) {
        wolfBoot_printf("BPMP: ivc reset TIMEOUT (channel not established)\n");
        return;
    }
    wolfBoot_printf("BPMP: established\n");

    r = bpmp_transfer(TEGRA_BPMP_MRQ_PING, &chal, sizeof(chal),
                      &reply, sizeof(reply));
    wolfBoot_printf("BPMP: ping ret=%d reply=0x%08x\n", (int)r, (uint32_t)reply);
    if (r == BPMP_ERR_TIMEOUT)
        return;

    /* Must not fall through to the register probe on failure: reading the
     * controller with its clock off faults with a CBB slave error that powers
     * the core down - the very failure this dump exists to diagnose. */
    if (tegra234_sdmmc1_clock_on() != 0) {
        wolfBoot_printf("BPMP: SDMMC1 clock enable FAILED (skipping probe)\n");
        return;
    }
    rate = bpmp_clk_get_rate(TEGRA234_CLK_SDMMC1);
    if (rate < 0)
        wolfBoot_printf("BPMP: SDMMC1 rate query FAILED\n");
    else
        wolfBoot_printf("BPMP: SDMMC1 actual rate = 0x%08x%08x Hz\n",
            (uint32_t)((uint64_t)rate >> 32), (uint32_t)rate);

    wolfBoot_printf("SDMMC1 probe @0x%08x (should NOT fault now):\n",
        (uint32_t)sd);
    wolfBoot_printf("  CAPS1=0x%08x CAPS2=0x%08x\n",
        rd32(sd + TEGRA_STD_CAPS1), rd32(sd + TEGRA_STD_CAPS2));
    wolfBoot_printf("  HOSTVER=0x%08x PRESENT=0x%08x\n",
        rd32(sd + TEGRA_STD_HOST_VERSION), rd32(sd + TEGRA_STD_PRESENT_STATE));
    wolfBoot_printf("SDMMC1 probe survived -- clock is ON\n");

    /* Card I/O is deliberately not exercised here: the disk boot path runs
     * sdhci_init() itself, and calling it twice leaves the driver's cached
     * clock stale across its software reset. */
}
#endif /* DEBUG_UART && TEGRA234_SDMMC_PROBE */

/* --------------------------------------------------------------------------
 * microSD via SDHCI/MMC (DISK_SDCARD) - WIP, off by default.
 *
 * DISK_SDCARD=1 compiles the generic Cadence-model driver (src/sdhci.c); the
 * hooks below translate its SRS register model to Tegra234 SDMMC1, enable
 * clocks via the BPMP, and drive the SD-power GPIO. STATUS: blocked - the
 * controller will not latch SD Clock Enable at BL33; the SDMMC1 functional
 * (axicif) clock bring-up happens inside closed MB2/BPMP firmware only for the
 * boot device (eMMC), not microSD. Do not expect card I/O yet.
 * -------------------------------------------------------------------------- */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

static inline uint8_t rd8(uintptr_t a) { return *(volatile uint8_t*)a; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t*)a = v; }

/* Set once the BPMP has clocked and un-reset SDMMC1. Until then the
 * controller's registers MUST NOT be touched: a bare access faults with a CBB
 * slave error (RAS uncorrectable) that powers the core off. The generic driver
 * has no way to learn that platform init failed (sdhci_platform_init is void),
 * so the accessors below go quiet instead. sdhci_init() then reads 0 from the
 * card-detect register and returns -1 through its normal error path. */
static int sdmmc_ready;

uint32_t sdhci_reg_read(uint32_t offset)
{
    if (!sdmmc_ready) {
        /* Answer the PHY handshake so the driver's wait loops still exit. */
        return (offset == SDHCI_HRS04) ? SDHCI_HRS04_UIS_ACK : 0;
    }
    if (offset >= TEGRA_SDHCI_SRS_OFFSET) {
        /* Cadence SRS (0x200+) -> Tegra standard-SDHCI (regs at 0). */
        return rd32(TEGRA_SDHCI_BASE + offset - TEGRA_SDHCI_SRS_OFFSET);
    }
    /* Cadence HRS range: emulate what the generic driver reads. */
    switch (offset) {
        case SDHCI_HRS00: /* -> standard SW-reset-for-all bit */
            return (rd8(TEGRA_SDHCI_BASE + TEGRA_STD_SW_RESET)
                    & TEGRA_STD_SW_RESET_ALL) ? 1u : 0u;
        case SDHCI_HRS04: /* report ACK so the wait loops exit */
            return SDHCI_HRS04_UIS_ACK;
        default:
            return 0;
    }
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    if (!sdmmc_ready)
        return;
    if (offset >= TEGRA_SDHCI_SRS_OFFSET) {
        /* Tegra234 accepts 32-bit access to the whole standard block and
         * (unlike the Arasan v3.0 on ZynqMP) requires the SRS11 clock/reset
         * word to be written 32-bit for SD-clock-enable to latch, so there is
         * no byte/half-word decomposition here. */
        wr32(TEGRA_SDHCI_BASE + offset - TEGRA_SDHCI_SRS_OFFSET, val);
        return;
    }
    if (offset == SDHCI_HRS00 && (val & SDHCI_HRS00_SWR) != 0)
        wr8(TEGRA_SDHCI_BASE + TEGRA_STD_SW_RESET, TEGRA_STD_SW_RESET_ALL);
}

/* MB1 boots from QSPI and leaves the microSD 3.3V rail (VDD_3V3_SD) off, so
 * the card is unpowered and SDMMC1 refuses to enable its SD clock. Drive the
 * enable GPIO high (output, not floated); see hal/tegra234.h. */
static void tegra234_sd_power_on(void)
{
    wr32(TEGRA_MAIN_GPIO_A0 + TEGRA_GPIO_OUTPUT_VALUE, 1u);
    wr32(TEGRA_MAIN_GPIO_A0 + TEGRA_GPIO_OUTPUT_CONTROL,
         rd32(TEGRA_MAIN_GPIO_A0 + TEGRA_GPIO_OUTPUT_CONTROL) & ~1u);
    wr32(TEGRA_MAIN_GPIO_A0 + TEGRA_GPIO_ENABLE_CONFIG,
         rd32(TEGRA_MAIN_GPIO_A0 + TEGRA_GPIO_ENABLE_CONFIG) |
         TEGRA_GPIO_ENABLE_OUT);
    hal_delay_us(2000); /* let the rail ramp before the card is clocked */
}

void sdhci_platform_init(void)
{
    tegra234_sd_power_on();

    /* MB1 has already applied the SDMMC1 pinmux and pad voltage (3.3V). The
     * vendor clock/pad setup is (re)applied in sdhci_platform_set_clock below,
     * because it must land after the driver's software-reset-for-all. */
    if (tegra234_sdmmc1_clock_on() == 0) {
        sdmmc_ready = 1;
    }
    else {
#if defined(DEBUG_UART)
        wolfBoot_printf("tegra234 sdhci: BPMP SDMMC1 clock enable FAILED; "
                        "leaving the controller untouched\n");
#endif
    }
}

/* Tegra owns the SD clock in its BPMP/CAR tree: set the SDMMC1 module clock
 * equal to the requested card clock so the SDHCI internal divider stays ~1
 * (with a large divider the controller will not latch SD Clock Enable), and
 * report the achieved module rate as the base. */
uint32_t sdhci_platform_set_clock(uint32_t clock_khz, uint32_t base_clk_khz)
{
    uintptr_t b = TEGRA_SDHCI_BASE;
    int64_t actual;
    uint32_t v;

    if (clock_khz == 0 || !sdmmc_ready)
        return 0;

    /* Tegra prerequisites for SD Clock Enable to latch. This hook runs inside
     * sdhci_set_clock, i.e. after the software-reset-for-all and before SDCE
     * is set - where the Linux driver also (re)applies them. */
    v = rd32(b + TEGRA_VENDOR_CLOCK_CTRL);
    v &= ~(TEGRA_VCC_TAP_MASK | TEGRA_VCC_TRIM_MASK |
           TEGRA_VCC_SDR50_TUNING_OVERRIDE | TEGRA_VCC_PADPIPE_CLKEN_OVERRIDE);
    v |= (TEGRA_SDMMC1_DEFAULT_TRIM << TEGRA_VCC_TRIM_SHIFT) |
         (TEGRA_SDMMC1_DEFAULT_TAP << TEGRA_VCC_TAP_SHIFT);
    wr32(b + TEGRA_VENDOR_CLOCK_CTRL, v);

    /* default speed */
    v = rd32(b + TEGRA_VENDOR_MISC_CTRL);
    v &= ~(TEGRA_VMC_ENABLE_SPEC_300 | TEGRA_VMC_ENABLE_SDR50 |
           TEGRA_VMC_ENABLE_SDR104);
    wr32(b + TEGRA_VENDOR_MISC_CTRL, v);

    /* power the pads */
    v = rd32(b + TEGRA_SDMEM_COMP_PADCTRL);
    v &= ~TEGRA_PADCTRL_VREF_SEL_MASK;
    v |= TEGRA_PADCTRL_VREF_SEL_VAL | TEGRA_PADCTRL_E_INPUT_E_PWRD;
    wr32(b + TEGRA_SDMEM_COMP_PADCTRL, v);

    if (bpmp_clk_set_rate(TEGRA234_CLK_SDMMC1,
                          (int64_t)clock_khz * 1000) != 0)
        return base_clk_khz; /* fall back to the CAPS base */
    actual = bpmp_clk_get_rate(TEGRA234_CLK_SDMMC1);
    if (actual <= 0)
        return base_clk_khz;
    return (uint32_t)((uint64_t)actual / 1000);
}

void sdhci_platform_irq_init(void)
{
    /* Polled-mode: no GIC wiring needed. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    (void)is_emmc; /* SDMMC1 is the external microSD (SD, not eMMC). */
}

#endif /* DISK_SDCARD || DISK_EMMC */

/* --------------------------------------------------------------------------
 * Public HAL entry points
 * -------------------------------------------------------------------------- */

void hal_init(void)
{
#if defined(DEBUG_UART)
    uart_init();
    wolfBoot_printf("wolfBoot Tegra234 (bare-metal BL33)\n");
#if defined(TEGRA234_HANDOFF_DUMP)
    tegra234_handoff_dump();    /* read-only: prints state, changes none */
#endif
#if defined(TEGRA234_SDMMC_PROBE)
    tegra234_bpmp_bringup();    /* MUTATES SoC state: clocks/resets/GPIO */
#endif
#endif
}

void hal_prepare_boot(void)
{
    /* The payload was just copied into DRAM as data. The MMU is off, so no
     * D-cache maintenance is needed (with the MMU off, data accesses are
     * Device-nGnRnE and never allocate in the D-cache), but
     * SCTLR_ELx.I is IMPLEMENTATION DEFINED at BL33 entry: with the I-cache
     * on, instruction fetches can still hit stale lines covering the load
     * address (e.g. after a warm reset or a repeated RCM load). Invalidate
     * before branching, as TF-A and U-Boot do at their handoffs. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Quiesce SDMMC1 so the OS does not inherit a controller this firmware
     * left clocked and running. */
    if (sdmmc_ready)
        sdhci_shutdown();
#endif
    __asm__ volatile(
        "dsb sy      \n"
        "ic iallu    \n"
        "dsb sy      \n"
        "isb         \n"
        ::: "memory");
}

/* No internal flash on this target: wolfBoot is loaded into DRAM by an earlier
 * stage. Provide the required no-op HAL flash surface. */
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
