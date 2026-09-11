/* imx8qm.c
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

/* NXP i.MX 8QuadMax (MCIMX8QM-MEK): bare-metal wolfBoot as BL33, replacing
 * U-Boot in the NXP boot container. DDR is already trained by SCFW when this
 * runs. See docs/Targets.md; registers in hal/imx8qm.h. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <target.h>

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif

#include "image.h"
#include "printf.h"
#include "hal/imx8qm.h"
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#include "sdhci.h"
#endif

#ifndef ARCH_AARCH64
#   error "wolfBoot imx8qm HAL: wrong architecture. Compile with ARCH=AARCH64."
#endif

/* Handoff x0 captured at reset by the .boot stub (src/boot_aarch64_start.S):
 * the DTB/params pointer the prior stage passed per the arm64 boot protocol.
 * The nonzero initializer keeps it in .data, out of reach of the BSS clear. */
volatile uint64_t boot_handoff_x0 = 0xFFFFFFFFFFFFFFFFULL;

/* Flattened Device Tree magic 0xd00dfeed, stored big-endian -> reads back as
 * 0xedfe0dd0 on this little-endian core. */
#define FDT_MAGIC_LE  0xedfe0dd0u

/* MMIO is Device-nGnRnE (MMU off), so accesses are strongly ordered by the
 * memory type and need no barriers. Mapping these Normal would break that. */
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

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
#if defined(DISK_EMMC)
#define IMX8QM_USDHC_BASE   IMX8QM_USDHC1_BASE  /* eMMC, 8-bit */
#else
#define IMX8QM_USDHC_BASE   IMX8QM_USDHC2_BASE  /* SD card, 4-bit */
#endif
#endif

/* Set once imx8qm_scu_init() has brought the console's clock up, so
 * uart_init() knows the LPUART root clock rate and can program the divisor. */
static int imx8qm_scu_ready;

/* Console: LPUART0. With the SCU client wolfBoot brings the port up itself and
 * so knows the root clock rate and programs BAUD; without it the rate is not
 * readable, so BAUD is left alone and only TE/RE are enabled. */

#define LPUART_TX_TIMEOUT_US   10000

#if defined(DEBUG_UART)
void uart_init(void)
{
    uintptr_t b = IMX8QM_LPUART0_BASE;
    uint32_t ctrl = rd32(b + LPUART_CTRL);
    uint32_t baud;

    /* Only program BAUD when wolfBoot brought the port up, so the root clock
     * rate is known. Otherwise a prior stage set it and it is not knowable. */
    if (imx8qm_scu_ready) {
        wr32(b + LPUART_CTRL, 0);           /* TE/RE off while BAUD changes */
        baud = rd32(b + LPUART_BAUD);
        baud &= ~(uint32_t)(LPUART_BAUD_OSR_MASK | LPUART_BAUD_SBR_MASK |
                            LPUART_BAUD_M10 | LPUART_BAUD_SBNS);
        baud |= IMX8QM_LPUART_BAUD_115200;  /* 8N1 at 80 MHz */
        wr32(b + LPUART_BAUD, baud);
        wr32(b + LPUART_CTRL, LPUART_CTRL_TE | LPUART_CTRL_RE);
        return;
    }

    if ((ctrl & LPUART_CTRL_TE) == 0) {
        wr32(b + LPUART_CTRL, ctrl | LPUART_CTRL_TE | LPUART_CTRL_RE);
    }
}

static void lpuart_putc(char c)
{
    uintptr_t b = IMX8QM_LPUART0_BASE;
    uint64_t deadline = timer_deadline_us(LPUART_TX_TIMEOUT_US);

    while ((rd32(b + LPUART_STAT) & LPUART_STAT_TDRE) == 0) {
        if (timer_expired(deadline))
            return; /* drop the character; do not stall the boot */
    }
    wr32(b + LPUART_DATA, (uint32_t)(uint8_t)c);
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n')
            lpuart_putc('\r');
        lpuart_putc(buf[i]);
    }
}
#endif /* DEBUG_UART */

/* --------------------------------------------------------------------------
 * System Controller (SCU) client, over MU1_A.
 *
 * The SCU owns power, clocks and pad mux. MU1_A is the channel free for BL33:
 * Linux and U-Boot bind lsio_mu1, TF-A uses MU0. RPC framing follows U-Boot
 * drivers/misc/imx8/scu_api.c. See docs/Targets.md.
 * -------------------------------------------------------------------------- */

#ifdef IMX8QM_SCU

/* SCU resources this HAL may drive, and the config that needs each one. */
struct imx8qm_resource {
    uint16_t rsrc;
    uint8_t  has_clock;   /* 1 if a peripheral clock must be set + enabled */
    uint32_t clock_hz;
};

static const struct imx8qm_resource imx8qm_resources[] = {
    { SC_R_UART_0, 1, IMX8QM_UART_CLK_HZ },
#if defined(DISK_EMMC)
    { SC_R_SDHC_0, 1, IMX8QM_USDHC_PERCLK_HZ },
#endif
#if defined(DISK_SDCARD)
    { SC_R_SDHC_1, 1, IMX8QM_USDHC_PERCLK_HZ },
#endif
#if defined(EXT_FLASH)
    { SC_R_FSPI_0, 1, 100000000 },
#endif
};

#define IMX8QM_NUM_RESOURCES \
    (int)(sizeof(imx8qm_resources) / sizeof(imx8qm_resources[0]))


/* SCFW RPC framing. A message is a header word followed by up to 7 payload
 * words, moved through the MU's four transmit/receive registers. */
#define SC_RPC_VERSION          1U
#define SC_RPC_SVC_PM           2U
#define SC_PM_FUNC_SET_RESOURCE_POWER_MODE  3U
#define SC_PM_FUNC_SET_CLOCK_RATE           5U
#define SC_PM_FUNC_GET_CLOCK_RATE           6U
#define SC_PM_FUNC_CLOCK_ENABLE             7U
#define SC_RPC_SVC_RM                       3U
#define SC_RM_FUNC_SET_MASTER_SID          11U

#define SC_MU_TIMEOUT_US        1000000

#define SC_RPC_HEADER(size, svc, func) \
    ((uint32_t)SC_RPC_VERSION | ((uint32_t)(size) << 8) | \
     ((uint32_t)(svc) << 16) | ((uint32_t)(func) << 24))

/* Move one 32-bit word into MU transmit register n, waiting for it to drain.
 * Returns 0 on success, -1 if the SCU stopped consuming. */
static int mu_send_word(int n, uint32_t val)
{
    uintptr_t b = IMX8QM_LSIO_MU1A_BASE;
    uint64_t deadline = timer_deadline_us(SC_MU_TIMEOUT_US);

    while ((rd32(b + MU_SR) & (MU_SR_TE0 >> n)) == 0) {
        if (timer_expired(deadline))
            return -1;
    }
    wr32(b + MU_TR0 + (uintptr_t)n * 4, val);
    return 0;
}

static int mu_recv_word(int n, uint32_t* val)
{
    uintptr_t b = IMX8QM_LSIO_MU1A_BASE;
    uint64_t deadline = timer_deadline_us(SC_MU_TIMEOUT_US);

    while ((rd32(b + MU_SR) & (MU_SR_RF0 >> n)) == 0) {
        if (timer_expired(deadline))
            return -1;
    }
    *val = rd32(b + MU_RR0 + (uintptr_t)n * 4);
    return 0;
}

/* One SCFW RPC. "size" counts header + payload in words. The reply carries its
 * own size and is not always one word; every word must be drained or the next
 * call reads a leftover as its reply header. */
/* Drop any pending reply words after a failed transfer, or every later RPC is
 * misaligned. Bounded so a mailbox the SCU keeps refilling cannot spin. */
static void mu_drain_rx(void)
{
    uintptr_t b = IMX8QM_LSIO_MU1A_BASE;
    uint32_t discard;
    int pass, n;

    for (pass = 0; pass < 8; pass++) {
        int drained = 0;
        for (n = 0; n < 4; n++) {
            if ((rd32(b + MU_SR) & (MU_SR_RF0 >> n)) != 0) {
                discard = rd32(b + MU_RR0 + (uintptr_t)n * 4);
                (void)discard;
                drained = 1;
            }
        }
        if (!drained)
            return;
    }
}

static int sc_rpc_call(uint32_t* msg, int size)
{
    int i, reply_size;
    uint32_t reply = 0, discard;

    for (i = 0; i < size; i++) {
        if (mu_send_word(i % 4, msg[i]) != 0) {
            mu_drain_rx();
            return -1;
        }
    }
    if (mu_recv_word(0, &reply) != 0) {
        mu_drain_rx();
        return -1;
    }

    reply_size = (int)((reply >> 8) & 0xFF);
    for (i = 1; i < reply_size; i++) {
        if (mu_recv_word(i % 4, &discard) != 0) {
            mu_drain_rx();
            return -1;
        }
    }
    /* Result is a signed 8-bit status in the function byte of the reply. */
    return (int8_t)((reply >> 24) & 0xFF);
}

static int sc_pm_set_resource_power_mode(uint16_t rsrc, uint8_t mode)
{
    uint32_t msg[2];

    /* Data layout: resource at byte 0, power mode at byte 2. */
    msg[0] = SC_RPC_HEADER(2, SC_RPC_SVC_PM,
                           SC_PM_FUNC_SET_RESOURCE_POWER_MODE);
    msg[1] = (uint32_t)rsrc | ((uint32_t)mode << 16);
    return sc_rpc_call(msg, 2);
}

static int sc_pm_set_clock_rate(uint16_t rsrc, uint8_t clk, uint32_t rate)
{
    uint32_t msg[3];

    /* Data layout: rate at byte 0 (a whole word), resource at byte 4, clock
     * type at byte 6. The SCU replies with the rate it actually programmed,
     * which sc_rpc_call drains; this HAL does not need it back. */
    msg[0] = SC_RPC_HEADER(3, SC_RPC_SVC_PM, SC_PM_FUNC_SET_CLOCK_RATE);
    msg[1] = rate;
    msg[2] = (uint32_t)rsrc | ((uint32_t)clk << 16);
    return sc_rpc_call(msg, 3);
}

#if defined(DEBUG_UART) && defined(EXT_FLASH) && defined(IMX8QM_FLEXSPI_PROBE)
/* As sc_rpc_call(), but hands back the first reply data word. Separate from
 * sc_rpc_call() rather than a refactor of it so that enabling the FlexSPI
 * probe cannot change the code generated for any shipped build. */
static int sc_rpc_call_ret(uint32_t* msg, int size, uint32_t* r1)
{
    int i, reply_size;
    uint32_t reply = 0, word;

    for (i = 0; i < size; i++) {
        if (mu_send_word(i % 4, msg[i]) != 0) {
            mu_drain_rx();
            return -1;
        }
    }
    if (mu_recv_word(0, &reply) != 0) {
        mu_drain_rx();
        return -1;
    }

    reply_size = (int)((reply >> 8) & 0xFF);
    for (i = 1; i < reply_size; i++) {
        if (mu_recv_word(i % 4, &word) != 0) {
            mu_drain_rx();
            return -1;
        }
        if (i == 1 && r1 != NULL)
            *r1 = word;
    }
    return (int8_t)((reply >> 24) & 0xFF);
}

/* The rate the SCU actually programmed. sc_pm_set_clock_rate() is a request:
 * the SCU rounds and clamps it to what the clock tree can produce, so a
 * driver that only checks the status byte cannot tell how fast it is running. */
static int sc_pm_get_clock_rate(uint16_t rsrc, uint8_t clk, uint32_t* rate)
{
    uint32_t msg[2];

    /* Data layout: resource at byte 0, clock type at byte 2. */
    msg[0] = SC_RPC_HEADER(2, SC_RPC_SVC_PM, SC_PM_FUNC_GET_CLOCK_RATE);
    msg[1] = (uint32_t)rsrc | ((uint32_t)clk << 16);
    return sc_rpc_call_ret(msg, 2, rate);
}
#endif /* DEBUG_UART && EXT_FLASH && IMX8QM_FLEXSPI_PROBE */

static int sc_pm_clock_enable(uint16_t rsrc, uint8_t clk, uint8_t enable)
{
    uint32_t msg[3];

    /* Data layout: resource at byte 0, clock type at byte 2, enable at byte 3,
     * autogate at byte 4 (i.e. the start of the second data word). */
    msg[0] = SC_RPC_HEADER(3, SC_RPC_SVC_PM, SC_PM_FUNC_CLOCK_ENABLE);
    msg[1] = (uint32_t)rsrc | ((uint32_t)clk << 16) |
             ((uint32_t)enable << 24);
    msg[2] = 0;     /* autogate off */
    return sc_rpc_call(msg, 3);
}

#define SC_RPC_SVC_PAD          6U
#define PAD_FUNC_SET            15U

/* The ID a master emits is owned by the SCU's resource manager, not the SMMU or
 * the device tree, so without this the OS programs the SMMU for the "iommus" ID
 * and that master's DMA faults. Cf. U-Boot config_smmu_fdt_device_sid(). */
static int sc_rm_set_master_sid(uint16_t rsrc, uint16_t sid)
{
    uint32_t msg[2];

    /* Data layout: resource at byte 0, stream ID at byte 2. */
    msg[0] = SC_RPC_HEADER(2, SC_RPC_SVC_RM, SC_RM_FUNC_SET_MASTER_SID);
    msg[1] = (uint32_t)rsrc | ((uint32_t)sid << 16);
    return sc_rpc_call(msg, 2);
}

/* The IFMUX/GP enables must be ORed into cfg by the caller, or the mux is
 * configured but not driven (U-Boot does the same in
 * imx8_iomux_setup_pad()). */
static int sc_pad_set(uint16_t pad, uint32_t val)
{
    uint32_t msg[3];

    /* Data layout: the control word at byte 0, the pad id at byte 4. */
    msg[0] = SC_RPC_HEADER(3, SC_RPC_SVC_PAD, PAD_FUNC_SET);
    msg[1] = val;
    msg[2] = (uint32_t)pad;
    return sc_rpc_call(msg, 3);
}

/* The clock-gate cell in front of the peripheral, separate from the SCU ungate.
 * Written twice with a delay, per the documented LPCG erratum. */
static void lpcg_all_clock_on(uintptr_t lpcg)
{
    uint64_t deadline;

    wr32(lpcg, LPCG_ALL_CLOCK_ON);
    hal_delay_us(10);
    wr32(lpcg, LPCG_ALL_CLOCK_ON);

    deadline = timer_deadline_us(10000);
    while ((rd32(lpcg) & LPCG_ALL_CLOCK_STOP) != 0) {
        if (timer_expired(deadline))
            return;
    }
}

/* Failures are buffered, not printed: this runs before the console is up. */
#define SCU_MAX_FAILURES 8
static struct { uint16_t rsrc; const char* what; int err; }
    scu_failures[SCU_MAX_FAILURES];
static int scu_nfailures;

static void scu_note_failure(uint16_t rsrc, const char* what, int err)
{
    if (scu_nfailures < SCU_MAX_FAILURES) {
        scu_failures[scu_nfailures].rsrc = rsrc;
        scu_failures[scu_nfailures].what = what;
        scu_failures[scu_nfailures].err = err;
        scu_nfailures++;
    }
}

/* Failures are reported but not fatal: the ROM may already have brought a
 * resource up. */
static void imx8qm_scu_init(void)
{
    int i, ret;

    for (i = 0; i < IMX8QM_NUM_RESOURCES; i++) {
        const struct imx8qm_resource* r = &imx8qm_resources[i];

        ret = sc_pm_set_resource_power_mode(r->rsrc, SC_PM_PW_MODE_ON);
        if (ret != 0) {
            scu_note_failure(r->rsrc, "power on", ret);
            continue;
        }
        if (r->has_clock) {
            ret = sc_pm_set_clock_rate(r->rsrc, SC_PM_CLK_PER, r->clock_hz);
            if (ret != 0)
                scu_note_failure(r->rsrc, "set rate", ret);
            ret = sc_pm_clock_enable(r->rsrc, SC_PM_CLK_PER, 1);
            if (ret != 0)
                scu_note_failure(r->rsrc, "clk enable", ret);
        }
    }

    /* Pads and LPCG: without both, LPUART0 accepts writes and drives nothing. */
    ret = sc_pad_set(SC_P_UART0_RX,
                     IMX8QM_UART_PAD_CTRL | PADRING_IFMUX_EN | PADRING_GP_EN);
    if (ret != 0)
        scu_note_failure(SC_R_UART_0, "pad set RX", ret);
    ret = sc_pad_set(SC_P_UART0_TX,
                     IMX8QM_UART_PAD_CTRL | PADRING_IFMUX_EN | PADRING_GP_EN);
    if (ret != 0)
        scu_note_failure(SC_R_UART_0, "pad set TX", ret);

    lpcg_all_clock_on(IMX8QM_LPUART0_LPCG);

#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Same for the storage controller, or it reads back plausible register
     * values and never talks to the card. */
    {
        /* Both pad sets, not just the boot medium's: the OS brings up every
         * uSDHC its device tree enables, and an unrouted controller finds no
         * card. Routing only the boot medium left an eMMC-booted kernel with
         * no SD slot, so root=/dev/mmcblk1p3 did not exist. Matches the
         * stream-ID publication below, which has always covered both. */
        static const struct { uint16_t pad; uint32_t cfg; } sd_pads[] = {
            /* uSDHC1: soldered 8-bit eMMC */
            { SC_P_EMMC0_CLK,      IMX8QM_SD_PAD_CLK_CTRL },
            { SC_P_EMMC0_CMD,      IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA0,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA1,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA2,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA3,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA4,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA5,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA6,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_DATA7,    IMX8QM_SD_PAD_CTRL },
            { SC_P_EMMC0_STROBE,   0x00000041 },
            { SC_P_EMMC0_RESET_B,  IMX8QM_SD_PAD_CTRL },
            /* uSDHC2: 4-bit SD socket */
            { SC_P_USDHC1_CLK,     IMX8QM_SD_PAD_CLK_CTRL },
            { SC_P_USDHC1_CMD,     IMX8QM_SD_PAD_CTRL },
            { SC_P_USDHC1_DATA0,   IMX8QM_SD_PAD_CTRL },
            { SC_P_USDHC1_DATA1,   IMX8QM_SD_PAD_CTRL },
            { SC_P_USDHC1_DATA2,   IMX8QM_SD_PAD_CTRL },
            { SC_P_USDHC1_DATA3,   IMX8QM_SD_PAD_CTRL },
            { SC_P_USDHC1_VSELECT, IMX8QM_SD_PAD_CTRL },
        };
        unsigned int pad_i;

        for (pad_i = 0; pad_i < sizeof(sd_pads)/sizeof(sd_pads[0]); pad_i++) {
            ret = sc_pad_set(sd_pads[pad_i].pad,
                sd_pads[pad_i].cfg | PADRING_IFMUX_EN | PADRING_GP_EN);
            if (ret != 0)
                scu_note_failure(sd_pads[pad_i].pad, "sd pad set", ret);
        }
        lpcg_all_clock_on(IMX8QM_USDHC_LPCG(IMX8QM_USDHC_BASE));

        /* Both controllers: the OS brings up every uSDHC the device tree
         * enables, and one with no published ID is unusable by it. */
        ret = sc_rm_set_master_sid(SC_R_SDHC_0, IMX8QM_USDHC_SMMU_SID);
        if (ret != 0)
            scu_note_failure(SC_R_SDHC_0, "set master sid", ret);
        ret = sc_rm_set_master_sid(SC_R_SDHC_1, IMX8QM_USDHC_SMMU_SID);
        if (ret != 0)
            scu_note_failure(SC_R_SDHC_1, "set master sid", ret);
    }
#endif

    /* BL33 owns SMMU bring-up: the OS aborts reading SMMU_IDR0 off an
     * unpowered block. Write guarded on the power call succeeding. */
    ret = sc_pm_set_resource_power_mode(SC_R_SMMU, SC_PM_PW_MODE_ON);
    if (ret != 0) {
        scu_note_failure(SC_R_SMMU, "power on", ret);
    }
    else {
        /* Read back: a dropped write leaves translation on for every client. */
        int tries;

        for (tries = 0; tries < 10; tries++) {
            wr32(IMX8QM_SMMU_BASE + IMX8QM_SMMU_sCR0,
                IMX8QM_SMMU_sCR0_CLIENTPD);
            if ((rd32(IMX8QM_SMMU_BASE + IMX8QM_SMMU_sCR0)
                    & IMX8QM_SMMU_sCR0_CLIENTPD) != 0) {
                break;
            }
            hal_delay_us(1000);
        }
        if (tries == 10) {
            scu_note_failure(SC_R_SMMU, "sCR0 CLIENTPD did not stick", 0);
        }
    }

#if defined(EXT_FLASH)
    /* flexspi0grp is one contiguous pad run, all at mux 0. */
    {
        uint16_t pad;

        for (pad = SC_P_QSPI0A_DATA0; pad <= SC_P_QSPI0B_SS1_B; pad++) {
            ret = sc_pad_set(pad, IMX8QM_FLEXSPI_PAD_CTRL |
                PADRING_IFMUX_EN | PADRING_GP_EN);
            if (ret != 0)
                scu_note_failure(pad, "fspi pad set", ret);
        }
        lpcg_all_clock_on(IMX8QM_FLEXSPI0_LPCG);
    }
#endif

    imx8qm_scu_ready = 1;
}
/* Print anything imx8qm_scu_init() could not do, now that the console is up. */
static void imx8qm_scu_report(void)
{
    int i;

    for (i = 0; i < scu_nfailures; i++) {
        wolfBoot_printf("imx8qm scu: resource %u %s failed (%d)\n",
            (unsigned)scu_failures[i].rsrc, scu_failures[i].what,
            scu_failures[i].err);
    }
}
#else /* !IMX8QM_SCU */
static void imx8qm_scu_init(void)
{
    (void)imx8qm_scu_ready;
    /* The prior stage (boot ROM / SCFW / ATF) left the console and the boot
     * device powered and clocked. Build with IMX8QM_SCU=1 to have wolfBoot
     * bring them up itself instead. */
}

static void imx8qm_scu_report(void)
{
}
#endif /* IMX8QM_SCU */

/* Partition / load addresses. In the no-storage configs the payload and DTB are
 * bundled into the BL33 image at the hal/imx8qm.h offsets. */

void* hal_get_primary_address(void)
{
    return (void*)(uintptr_t)(IMX8QM_BL33_BASE + IMX8QM_BUNDLE_OFFSET);
}

/* No update partition is populated by the packaging script, which writes only
 * the boot payload. This returns the reserved DRAM staging address so the
 * update API has a valid target. */
void* hal_get_update_address(void)
{
    return (void*)(uintptr_t)IMX8QM_UPDATE_ADDR;
}

void* hal_get_dts_address(void)
{
    uintptr_t dtb = (uintptr_t)(IMX8QM_BL33_BASE + IMX8QM_DTB_OFFSET);
    uint32_t totalsize;

    /* Only claim a DTB if one was bundled; otherwise this offset is empty and
     * x0 would be garbage. A bundled DTB is covered by AHAB, not by wolfBoot's
     * payload signature. */
    if (rd32(dtb) != FDT_MAGIC_LE)
        return NULL;
    /* totalsize (big-endian, at offset 4) must fit in what is left of the
     * BL33 image: update_ram copies that many bytes. */
    totalsize = __builtin_bswap32(rd32(dtb + 4));
    if (totalsize < 8 ||
            totalsize > (IMX8QM_BL33_MAX_SIZE - IMX8QM_DTB_OFFSET))
        return NULL;
    return (void*)dtb;
}

void* hal_get_dts_update_address(void)
{
    return NULL; /* Not yet supported */
}

/* --------------------------------------------------------------------------
 * FlexSPI0 serial NOR (MT35XU512ABA, 64 MB). Reads via the AHB window, erase
 * and program via the LUT-driven IP command path; same IP as the Layerscape
 * XSPI block in hal/nxp_ls1028a.c. All LUT sequences are single-pad (1-1-1)
 * with 4-byte addressing, since the part powers up in extended SPI mode and
 * 64 MB is past the 3-byte limit.
 * -------------------------------------------------------------------------- */

#ifdef EXT_FLASH

#define FSPI_REG(off)   (IMX8QM_FLEXSPI0_BASE + (off))
#define FSPI_LUT(n)     FSPI_REG(FLEXSPI_LUT0 + ((n) * 4))

/* One LUT instruction: opcode[15:10] | pad[9:8] | operand[7:0]. Two per word. */
#define LUT_INSTR(op, pad, operand) \
    ((uint16_t)(((op) << 10) | ((pad) << 8) | ((operand) & 0xFF)))
#define LUT_WORD(i0, i1)  ((uint32_t)(i0) | ((uint32_t)(i1) << 16))

/* Command-done / error bits are write-1-to-clear in INTR. */
#define FSPI_IP_ERR (FLEXSPI_INTR_IPCMDGE | FLEXSPI_INTR_IPCMDERR)

/* Bound every IP command so a mis-configured controller fails the boot with an
 * error instead of hanging inside the flash driver. */
#define FSPI_IPCMD_TIMEOUT_US   1000000
/* A full-chip erase is never issued here; the longest single operation is a
 * 128 KB sector erase, spec'd in the hundreds of milliseconds. */
#define FSPI_WIP_TIMEOUT_US     5000000

static int flexspi_ready;

/* IPCR0 takes the offset within the connected device, not the AHB address
 * (this matches Linux spi-nxp-fspi.c, which writes op->addr.val). Accept
 * either form so callers can pass a wolfBoot partition address. */
static uint32_t flexspi_dev_off(uintptr_t address)
{
    if (address >= IMX8QM_FLEXSPI0_AHB_BASE)
        address -= IMX8QM_FLEXSPI0_AHB_BASE;
    return (uint32_t)address;
}

static void flexspi_lut_unlock(void)
{
    wr32(FSPI_REG(FLEXSPI_LUTKEY), FLEXSPI_LUTKEY_VALUE);
    wr32(FSPI_REG(FLEXSPI_LUTCR), FLEXSPI_LUTCR_UNLOCK);
}

static void flexspi_lut_lock(void)
{
    wr32(FSPI_REG(FLEXSPI_LUTKEY), FLEXSPI_LUTKEY_VALUE);
    wr32(FSPI_REG(FLEXSPI_LUTCR), FLEXSPI_LUTCR_LOCK);
}

static void flexspi_lut_setup(void)
{
    int i;

    flexspi_lut_unlock();

    /* Zero every sequence slot this driver owns, so a stale LUT left by the
     * boot ROM cannot be executed by a sequence index we did not program. */
    for (i = 0; i < 5 * 4; i++)
        wr32(FSPI_LUT(i), 0);

    /* READ (0x13): CMD, 32-bit address, then read. Used by the AHB window. */
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_READ * 4 + 0),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_CMD, FLEXSPI_LUT_PAD1,
                           FLEXSPI_NOR_CMD_READ_4B),
                 LUT_INSTR(FLEXSPI_LUT_RADDR, FLEXSPI_LUT_PAD1, 32)));
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_READ * 4 + 1),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_READ, FLEXSPI_LUT_PAD1, 4),
                 LUT_INSTR(FLEXSPI_LUT_STOP, FLEXSPI_LUT_PAD1, 0)));

    /* WREN (0x06) */
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_WREN * 4 + 0),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_CMD, FLEXSPI_LUT_PAD1,
                           FLEXSPI_NOR_CMD_WREN),
                 LUT_INSTR(FLEXSPI_LUT_STOP, FLEXSPI_LUT_PAD1, 0)));

    /* RDSR (0x05): one status byte */
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_RDSR * 4 + 0),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_CMD, FLEXSPI_LUT_PAD1,
                           FLEXSPI_NOR_CMD_RDSR),
                 LUT_INSTR(FLEXSPI_LUT_READ, FLEXSPI_LUT_PAD1, 1)));

    /* Sector erase (0xDC), 128 KB - see FLEXSPI_NOR_SECTOR_SIZE */
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_SE * 4 + 0),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_CMD, FLEXSPI_LUT_PAD1,
                           FLEXSPI_NOR_CMD_SE_4B),
                 LUT_INSTR(FLEXSPI_LUT_RADDR, FLEXSPI_LUT_PAD1, 32)));

    /* Page program (0x12) */
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_PP * 4 + 0),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_CMD, FLEXSPI_LUT_PAD1,
                           FLEXSPI_NOR_CMD_PP_4B),
                 LUT_INSTR(FLEXSPI_LUT_RADDR, FLEXSPI_LUT_PAD1, 32)));
    wr32(FSPI_LUT(FLEXSPI_LUT_SEQ_PP * 4 + 1),
        LUT_WORD(LUT_INSTR(FLEXSPI_LUT_WRITE, FLEXSPI_LUT_PAD1, 4),
                 LUT_INSTR(FLEXSPI_LUT_STOP, FLEXSPI_LUT_PAD1, 0)));

    flexspi_lut_lock();
}

static void flexspi_init(void)
{
    uint64_t deadline;
    int i;

    /* Software reset, then hold the module disabled while it is configured. */
    wr32(FSPI_REG(FLEXSPI_MCR0), rd32(FSPI_REG(FLEXSPI_MCR0)) |
        FLEXSPI_MCR0_SWRESET);
    deadline = timer_deadline_us(FSPI_IPCMD_TIMEOUT_US);
    while ((rd32(FSPI_REG(FLEXSPI_MCR0)) & FLEXSPI_MCR0_SWRESET) != 0) {
        if (timer_expired(deadline))
            break;
    }
    wr32(FSPI_REG(FLEXSPI_MCR0), FLEXSPI_MCR0_MDIS);

    /* Bypass the DLL: at the conservative single-pad SDR rates used here the
     * delay line is not needed, and overriding it removes a lock step that can
     * fail before the flash has been probed. */
    wr32(FSPI_REG(FLEXSPI_DLLACR), FLEXSPI_DLLCR_OVRDEN);
    wr32(FSPI_REG(FLEXSPI_DLLBCR), FLEXSPI_DLLCR_OVRDEN);

    /* Give each chip select its own configuration rather than mirroring A1. */
    wr32(FSPI_REG(FLEXSPI_MCR2), rd32(FSPI_REG(FLEXSPI_MCR2)) &
        ~(uint32_t)FLEXSPI_MCR2_SAMEDEVEN);

    /* Flash A1 size, in KB. Only A1 is populated on the MEK; leaving the
     * other three at 0 keeps their windows closed. */
    wr32(FSPI_REG(FLEXSPI_FLSHA1CR0), IMX8QM_FLEXSPI0_SIZE / 1024);

    /* Point AHB reads at the READ sequence. */
    wr32(FSPI_REG(FLEXSPI_FLSHA1CR2),
        (uint32_t)FLEXSPI_LUT_SEQ_READ << FLEXSPI_FLSHCR2_ARDSEQID_S);

    /* Give all of the AHB RX buffer space to the last buffer, which is the
     * one used by ordinary AHB reads. */
    for (i = 0; i < 7; i++)
        wr32(FSPI_REG(FLEXSPI_AHBRXBUF0CR0 + i * 4), 0);

    wr32(FSPI_REG(FLEXSPI_AHBCR), FLEXSPI_AHBCR_PREFETCH);

    /* Enable the module with generous arbitration timeouts. */
    wr32(FSPI_REG(FLEXSPI_MCR0),
        FLEXSPI_MCR0_AHBGRANTWAIT | FLEXSPI_MCR0_IPGRANTWAIT);

    flexspi_lut_setup();
    flexspi_ready = 1;
}

/* Run one IP command sequence. Returns 0 on success, -1 on error/timeout. */
static int flexspi_ip_cmd(uint32_t seq, uint32_t dev_off, uint32_t data_size)
{
    uint64_t deadline;
    uint32_t intr;

    wr32(FSPI_REG(FLEXSPI_INTR), FLEXSPI_INTR_IPCMDDONE | FSPI_IP_ERR);
    wr32(FSPI_REG(FLEXSPI_IPCR0), dev_off);
    wr32(FSPI_REG(FLEXSPI_IPCR1), (seq << 16) | data_size);
    wr32(FSPI_REG(FLEXSPI_IPCMD), FLEXSPI_IPCMD_TRG);

    deadline = timer_deadline_us(FSPI_IPCMD_TIMEOUT_US);
    for (;;) {
        intr = rd32(FSPI_REG(FLEXSPI_INTR));
        if ((intr & (FLEXSPI_INTR_IPCMDDONE | FSPI_IP_ERR)) != 0)
            break;
        if (timer_expired(deadline)) {
            wolfBoot_printf("imx8qm fspi: IP command %u timed out\n",
                (unsigned)seq);
            return -1;
        }
    }
    wr32(FSPI_REG(FLEXSPI_INTR), intr & (FLEXSPI_INTR_IPCMDDONE | FSPI_IP_ERR));
    if ((intr & FSPI_IP_ERR) != 0) {
        wolfBoot_printf("imx8qm fspi: IP command %u error (INTR 0x%08x)\n",
            (unsigned)seq, (unsigned)intr);
        return -1;
    }
    return 0;
}

static int flexspi_write_en(uint32_t dev_off)
{
    wr32(FSPI_REG(FLEXSPI_IPTXFCR), FLEXSPI_IPTXFCR_CLR);
    return flexspi_ip_cmd(FLEXSPI_LUT_SEQ_WREN, dev_off, 0);
}

static int flexspi_read_sr(uint32_t dev_off, uint8_t* sr)
{
    uint32_t data;

    wr32(FSPI_REG(FLEXSPI_IPRXFCR), FLEXSPI_IPRXFCR_CLR);
    if (flexspi_ip_cmd(FLEXSPI_LUT_SEQ_RDSR, dev_off, 1) != 0)
        return -1;
    data = rd32(FSPI_REG(FLEXSPI_RFDR0));
    *sr = (uint8_t)(data & 0xFF);
    wr32(FSPI_REG(FLEXSPI_IPRXFCR), FLEXSPI_IPRXFCR_CLR);
    wr32(FSPI_REG(FLEXSPI_INTR), FLEXSPI_INTR_IPRXWA);
    return 0;
}

/* Block until the NOR device finishes its program/erase cycle. IPCMDDONE only
 * means the controller finished driving the bus; the device keeps WIP set for
 * the duration of the cycle and ignores Write Enable until it clears. */
static int flexspi_wait_ready(uint32_t dev_off)
{
    uint64_t deadline = timer_deadline_us(FSPI_WIP_TIMEOUT_US);
    uint8_t sr = 0;

    do {
        if (flexspi_read_sr(dev_off, &sr) != 0)
            return -1;
        if ((sr & FLEXSPI_NOR_SR_WIP) == 0)
            return 0;
    } while (!timer_expired(deadline));

    wolfBoot_printf("imx8qm fspi: WIP never cleared at 0x%08x\n",
        (unsigned)dev_off);
    return -1;
}

/* Reads through the AHB window are served from prefetch buffers that an IP
 * command does not invalidate, so after an erase or a program the window can
 * still return the previous contents. Hardware testing found exactly that: a
 * freshly programmed pattern read back as the pre-program data, and a read
 * after an erase still showed the old pattern, while the device itself was
 * correct both times (the pattern survived a power cycle and read back
 * correctly on the next boot). A software reset flushes the buffers and
 * leaves the configuration registers and the LUT in place. */
static void flexspi_ahb_flush(void)
{
    uint64_t deadline;

    wr32(FSPI_REG(FLEXSPI_MCR0), rd32(FSPI_REG(FLEXSPI_MCR0)) |
        FLEXSPI_MCR0_SWRESET);
    deadline = timer_deadline_us(FSPI_IPCMD_TIMEOUT_US);
    while ((rd32(FSPI_REG(FLEXSPI_MCR0)) & FLEXSPI_MCR0_SWRESET) != 0) {
        if (timer_expired(deadline))
            break;
    }
}

void ext_flash_unlock(void)
{
    if (!flexspi_ready)
        flexspi_init();
}

void ext_flash_lock(void)
{
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uintptr_t src;

    if (len < 0)
        return -1;
    if (!flexspi_ready)
        flexspi_init();

    /* Reads come from the memory-mapped AHB window. Accept either a device
     * offset or an AHB address. */
    src = address;
    if (src < IMX8QM_FLEXSPI0_AHB_BASE)
        src += IMX8QM_FLEXSPI0_AHB_BASE;
    if ((src + (uint32_t)len) >
            (IMX8QM_FLEXSPI0_AHB_BASE + IMX8QM_FLEXSPI0_SIZE))
        return -1;

    memcpy(data, (const void*)src, (size_t)len);
    return len;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    uint32_t off = flexspi_dev_off(address);
    uint32_t remaining = (uint32_t)len;
    uint32_t i, chunk, page_room, filled;
    uint64_t deadline;

    if (len < 0)
        return -1;
    if (!flexspi_ready)
        flexspi_init();
    if ((off + remaining) > IMX8QM_FLEXSPI0_SIZE)
        return -1;

    while (remaining > 0) {
        /* A NOR page program must not cross a physical page boundary: the
         * device's write pointer wraps to the start of the page and the excess
         * bytes clobber preceding data. */
        page_room = FLEXSPI_NOR_PAGE_SIZE - (off % FLEXSPI_NOR_PAGE_SIZE);
        chunk = remaining > page_room ? page_room : remaining;

        /* The device clears its write-enable latch after every program, so
         * this is per page rather than once up front. */
        wr32(FSPI_REG(FLEXSPI_IPTXFCR), FLEXSPI_IPTXFCR_CLR);
        if (flexspi_write_en(off) != 0)
            return -1;

        /* Fill the IP TX FIFO a watermark at a time before triggering. */
        wr32(FSPI_REG(FLEXSPI_IPCR0), off);
        filled = 0;
        while (filled < chunk) {
            uint32_t burst = chunk - filled;

            if (burst > FLEXSPI_IP_WM_BYTES)
                burst = FLEXSPI_IP_WM_BYTES;

            deadline = timer_deadline_us(FSPI_IPCMD_TIMEOUT_US);
            while ((rd32(FSPI_REG(FLEXSPI_INTR)) & FLEXSPI_INTR_IPTXWE) == 0) {
                if (timer_expired(deadline)) {
                    wolfBoot_printf("imx8qm fspi: TX FIFO never drained\n");
                    return -1;
                }
            }
            for (i = 0; i < burst; i += 4) {
                uint32_t word = 0;
                uint32_t n = (burst - i) < 4 ? (burst - i) : 4;

                memcpy(&word, data + filled + i, n);
                wr32(FSPI_REG(FLEXSPI_TFDR0 + i), word);
            }
            wr32(FSPI_REG(FLEXSPI_INTR), FLEXSPI_INTR_IPTXWE);
            filled += burst;
        }

        if (flexspi_ip_cmd(FLEXSPI_LUT_SEQ_PP, off, chunk) != 0)
            return -1;
        wr32(FSPI_REG(FLEXSPI_IPTXFCR), FLEXSPI_IPTXFCR_CLR);
        if (flexspi_wait_ready(off) != 0)
            return -1;

        data += chunk;
        off += chunk;
        remaining -= chunk;
    }
    flexspi_ahb_flush();
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    uint32_t off = flexspi_dev_off(address);
    uint32_t end;

    if (len <= 0)
        return -1;
    if (!flexspi_ready)
        flexspi_init();

    end = off + (uint32_t)len;
    if (end > IMX8QM_FLEXSPI0_SIZE)
        return -1;

    /* Erase every sector the range touches, including a partial head or tail:
     * the caller is entitled to pass an unaligned address and length. */
    off -= off % FLEXSPI_NOR_SECTOR_SIZE;
    while (off < end) {
        if (flexspi_write_en(off) != 0)
            return -1;
        if (flexspi_ip_cmd(FLEXSPI_LUT_SEQ_SE, off, 0) != 0)
            return -1;
        if (flexspi_wait_ready(off) != 0)
            return -1;
        off += FLEXSPI_NOR_SECTOR_SIZE;
    }
    flexspi_ahb_flush();
    return len;
}
#endif /* EXT_FLASH */

/* --------------------------------------------------------------------------
 * uSDHC -> SDHCI shim. src/sdhci.c reaches the controller only through
 * sdhci_reg_read/write, so uSDHC is supported by translating here:
 *
 *  1. Transfer-mode half of the command register is in MIX_CTRL (0x48).
 *  2. PROT_CTRL holds bus width as a 2-bit field, no bus power/voltage.
 *  3. SYS_CTRL uses a DVS/SDCLKFS divider pair, not a 10-bit divisor.
 *  4. No error-interrupt summary bit; DMA error moves from bit 25 to 28.
 *
 * Software shadows cover fields uSDHC has no home for, so the driver's
 * read-modify-write cycles round-trip.
 * -------------------------------------------------------------------------- */

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

/* Not exported by include/sdhci.h; every in-tree SDHCI platform defines it
 * locally (hal/versal.c, hal/zynq7000.c, hal/cm4.h). */
#ifndef CADENCE_SRS_OFFSET
#define CADENCE_SRS_OFFSET  0x200
#endif

/* Standard-SDHCI offsets, as seen after the Cadence SRS base is removed. */
#define STD_BLK             0x04
#define STD_CMD             0x0C
#define STD_PRES_STATE      0x24
#define STD_HOST_CTRL1      0x28
#define STD_CLOCK_CTRL      0x2C
#define STD_INT_STATUS      0x30
#define STD_INT_STATUS_EN   0x34
#define STD_INT_SIGNAL_EN   0x38
#define STD_HOST_CTRL2      0x3C
#define STD_CAPS1           0x40
#define STD_CAPS2           0x44
#define STD_ADMA_ADDR_LO    0x58
#define STD_ADMA_ADDR_HI    0x5C

/* uSDHC-only error bits with no standard-SDHCI position. */
#define USDHC_INT_DMAE      (1U << 28)
#define USDHC_INT_TNE       (1U << 26)

/* Fields the driver reads back that uSDHC does not implement. */
static uint32_t srs10_shadow;   /* bus power / bus voltage / high speed */
static uint32_t srs15_shadow;   /* host control 2 */
/* The rate sdhci_platform_set_clock() actually achieved, reported back as the
 * base so the generic driver's divider comes out as 1. Deliberately NOT the
 * CAPS1 base-clock field below, which must stay the constant source rate. */
static uint32_t usdhc_achieved_clk_khz = IMX8QM_USDHC_PERCLK_HZ / 1000;

uint32_t sdhci_reg_read(uint32_t offset)
{
    uintptr_t b = IMX8QM_USDHC_BASE;
    uint32_t std, v, raw;

    if (offset < CADENCE_SRS_OFFSET) {
        /* Cadence HRS range: uSDHC has no equivalent. Report the PHY
         * handshake as acknowledged so the driver's wait loops still exit. */
        return (offset == SDHCI_HRS04) ? SDHCI_HRS04_UIS_ACK : 0;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_PRES_STATE:
            v = rd32(b + USDHC_PRES_STATE);
            /* Card state stable has no uSDHC bit; the card-inserted bit is
             * already debounced, so report it as always stable. */
            v |= SDHCI_SRS09_CSS;
            /* DAT0 level moves from the DLSL field (bit 24) to bit 20. */
            if ((v & USDHC_PRES_DLSL_DAT0) != 0)
                v |= SDHCI_SRS09_DAT0_LVL;
            else
                v &= ~SDHCI_SRS09_DAT0_LVL;
            return v;

        case STD_HOST_CTRL1:
            v = srs10_shadow & ~(uint32_t)(SDHCI_SRS10_DTW | SDHCI_SRS10_EDTW);
            switch (rd32(b + USDHC_PROT_CTRL) & USDHC_PROT_DTW_MASK) {
                case USDHC_PROT_DTW_4BIT: v |= SDHCI_SRS10_DTW;  break;
                case USDHC_PROT_DTW_8BIT: v |= SDHCI_SRS10_EDTW; break;
                default: break;
            }
            return v;

        case STD_CLOCK_CTRL:
            /* No internal clock-enable or stable bit; report both satisfied. */
            v = rd32(b + USDHC_SYS_CTRL) &
                (uint32_t)(USDHC_SYS_DTOCV_MASK | USDHC_SYS_RSTA |
                           USDHC_SYS_RSTC | USDHC_SYS_RSTD);
            return v | SDHCI_SRS11_ICE | SDHCI_SRS11_ICS | SDHCI_SRS11_SDCE;

        case STD_INT_STATUS:
        case STD_INT_STATUS_EN:
        case STD_INT_SIGNAL_EN:
            raw = rd32(b + std);
            v = raw;
            if ((raw & USDHC_INT_DMAE) != 0)
                v |= SDHCI_SRS12_EADMA;
            /* Present the uSDHC-only bits as nothing, and synthesize the
             * error-interrupt summary the driver polls on. TNE has no
             * standard equivalent to map onto, so test the raw value: left
             * out of the summary a latched tuning error is invisible to the
             * driver, which then waits on a transfer that will never
             * complete. */
            v &= ~(uint32_t)(USDHC_INT_DMAE | USDHC_INT_TNE);
            if (std == STD_INT_STATUS &&
                    (((v & SDHCI_SRS12_ERR_STAT) != 0) ||
                     ((raw & USDHC_INT_TNE) != 0))) {
                v |= SDHCI_SRS12_EINT;
            }
            return v;

        case STD_HOST_CTRL2:
            return srs15_shadow;

        case STD_CAPS1:
            /* uSDHC zeroes these; synthesize a 50 MHz timeout clock and the
             * real peripheral clock. 64-bit addressing masked off (PIO). */
            v = rd32(b + USDHC_HOST_CTRL_CAP) &
                (uint32_t)(USDHC_CAP_VS33 | USDHC_CAP_VS30 | USDHC_CAP_VS18);
            v |= (50U << SDHCI_SRS16_TCF_SHIFT) & SDHCI_SRS16_TCF_MASK;
            v |= SDHCI_SRS16_TCU; /* timeout clock is in MHz */
            /* Constant source clock in MHz. Using the achieved rate here
             * reports 0 MHz once the 400 kHz identification clock has been
             * programmed, since it truncates. */
            v |= ((IMX8QM_USDHC_PERCLK_HZ / 1000000U)
                    << SDHCI_SRS16_BCSDCLK_SHIFT) &
                 SDHCI_SRS16_BCSDCLK_MASK;
            return v;

        case STD_CAPS2:
            return 0;

        case STD_ADMA_ADDR_LO:
            /* SDMA, not ADMA2: PROT_CTRL selects DMASEL=simple below, so the
             * generic driver's SRS22 is uSDHC's DS_ADDR (the running system
             * address) rather than ADMA_SYS_ADDR, which would be a descriptor
             * table pointer. The boundary-interrupt handler reads this back
             * and rewrites it to resume, which DS_ADDR supports. */
            return rd32(b + USDHC_DS_ADDR);

        case STD_ADMA_ADDR_HI:
            return 0; /* 32-bit ADMA only */

        default:
            return rd32(b + std);
    }
}

void sdhci_reg_write(uint32_t offset, uint32_t val)
{
    uintptr_t b = IMX8QM_USDHC_BASE;
    uint32_t std, v, mix;

    if (offset < CADENCE_SRS_OFFSET) {
        /* The only Cadence host register the driver writes is the software
         * reset; route it to the uSDHC reset-all bit. */
        if (offset == SDHCI_HRS00 && (val & SDHCI_HRS00_SWR) != 0)
            wr32(b + USDHC_SYS_CTRL,
                rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
        return;
    }
    std = offset - CADENCE_SRS_OFFSET;

    switch (std) {
        case STD_BLK:
            /* Bit 12 is block size here, SDMA boundary in standard SDHCI. */
            wr32(b + USDHC_BLK_ATT,
                (val & 0xFFFF0000U) | (val & 0x0FFFU));
            return;

        case STD_CMD:
            /* MIX_CTRL first: writing CMD_XFR_TYP starts the command. Bits
             * above 5 of MIX_CTRL have no standard equivalent; preserve. */
            mix = rd32(b + USDHC_MIX_CTRL) & ~(uint32_t)USDHC_MIX_CTRL_XFER_MASK;
            mix |= val & USDHC_MIX_CTRL_XFER_MASK;
            wr32(b + USDHC_MIX_CTRL, mix);

            v = val & 0xFFFF0000U;
            /* The Cadence response-check bits alias the response type and
             * arrive as "48-bit with busy". SD data commands are R1, not R1b,
             * so a busy check would wait on DAT0 forever; downgrade. */
            if ((v & USDHC_XFR_DPSEL) != 0 &&
                    (v & USDHC_XFR_RSPTYP_MASK) == USDHC_XFR_RSPTYP_48B) {
                v = (v & ~(uint32_t)USDHC_XFR_RSPTYP_MASK) |
                    USDHC_XFR_RSPTYP_48;
            }
            wr32(b + USDHC_CMD_XFR_TYP, v);
            return;

        case STD_HOST_CTRL1:
            srs10_shadow = val;
            v = rd32(b + USDHC_PROT_CTRL) &
                ~(uint32_t)(USDHC_PROT_DTW_MASK | USDHC_PROT_DMASEL_MASK);
            if ((val & SDHCI_SRS10_EDTW) != 0)
                v |= USDHC_PROT_DTW_8BIT;
            else if ((val & SDHCI_SRS10_DTW) != 0)
                v |= USDHC_PROT_DTW_4BIT;
            else
                v |= USDHC_PROT_DTW_1BIT;
            /* Standard SDHCI selects the DMA engine in bits 3:4; uSDHC uses
             * bits 8:9. Only simple (SDMA) is ever requested here. */
            v |= USDHC_PROT_DMASEL_SIMPLE;
            wr32(b + USDHC_PROT_CTRL, v);
            return;

        case STD_CLOCK_CTRL:
            /* Only the shared fields; the divider is owned by
             * sdhci_platform_set_clock, so drop the standard divisor bits. */
            v = rd32(b + USDHC_SYS_CTRL) & ~(uint32_t)USDHC_SYS_DTOCV_MASK;
            v |= val & USDHC_SYS_DTOCV_MASK;
            v |= val & (uint32_t)(USDHC_SYS_RSTA | USDHC_SYS_RSTC |
                                  USDHC_SYS_RSTD);
            wr32(b + USDHC_SYS_CTRL, v);
            return;

        case STD_INT_STATUS:
            /* Write-1-to-clear. Map the standard ADMA error bit onto the
             * uSDHC DMA error, and clear the uSDHC-only tuning error whenever
             * the driver clears any error, including through the synthesized
             * summary bit. The driver never sees TNE itself, so this is its
             * only route to being cleared; left latched it holds the summary
             * asserted for good. */
            v = val;
            if ((val & SDHCI_SRS12_EADMA) != 0)
                v |= USDHC_INT_DMAE;
            if ((val & (SDHCI_SRS12_ERR_STAT | SDHCI_SRS12_EINT)) != 0)
                v |= USDHC_INT_TNE;
            wr32(b + USDHC_INT_STATUS, v & ~(uint32_t)SDHCI_SRS12_EINT);
            return;

        case STD_INT_STATUS_EN:
        case STD_INT_SIGNAL_EN:
            /* On uSDHC a status bit does not latch unless its enable is set,
             * so the uSDHC-only error enables have to be added to the mask
             * the generic driver builds. */
            v = val & ~(uint32_t)SDHCI_SRS12_EINT;
            if ((val & SDHCI_SRS12_ERR_STAT) != 0)
                v |= USDHC_INT_DMAE | USDHC_INT_TNE;
            wr32(b + std, v);
            return;

        case STD_HOST_CTRL2:
            srs15_shadow = val;
            /* 1.8V signaling lives in VEND_SPEC on uSDHC. */
            v = rd32(b + USDHC_VEND_SPEC);
            if ((val & SDHCI_SRS15_V18SE) != 0)
                v |= USDHC_VEND_SPEC_VSELECT;
            else
                v &= ~(uint32_t)USDHC_VEND_SPEC_VSELECT;
            wr32(b + USDHC_VEND_SPEC, v);
            return;

        case STD_CAPS1:
        case STD_CAPS2:
            return; /* read-only */

        case STD_ADMA_ADDR_LO:
            wr32(b + USDHC_DS_ADDR, val);   /* see the read side */
            return;

        case STD_ADMA_ADDR_HI:
            return; /* 32-bit ADMA only */

        default:
            wr32(b + std, val);
            return;
    }
}

void sdhci_platform_init(void)
{
    uintptr_t b = IMX8QM_USDHC_BASE;
    uint64_t deadline;
    uint32_t v;

    /* Reset the controller, then apply the settings that survive it. */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_RSTA);
    deadline = timer_deadline_us(1000000);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_RSTA) != 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("imx8qm usdhc: reset-all did not clear\n");
            break;
        }
    }

    /* Little-endian data port, card-detect from DAT3 disabled (the MEK wires
     * a dedicated card-detect GPIO, and on eMMC there is nothing to detect). */
    v = rd32(b + USDHC_PROT_CTRL);
    v = (v & ~(uint32_t)USDHC_PROT_EMODE_MASK) | USDHC_PROT_EMODE_LE;
    v &= ~(uint32_t)USDHC_PROT_D3CD;
    wr32(b + USDHC_PROT_CTRL, v);

    /* Move a whole 512-byte block per watermark event, so the generic
     * driver's PIO loop sees one Buffer Read Ready per block rather than one
     * every 32 bytes. */
    wr32(b + USDHC_WTMK_LVL,
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_RD_SHIFT) |
        ((uint32_t)USDHC_WTMK_BLOCK_WORDS << USDHC_WTMK_WR_SHIFT));

    /* Send the 80 initialization clocks the card needs before CMD0. */
    wr32(b + USDHC_SYS_CTRL, rd32(b + USDHC_SYS_CTRL) | USDHC_SYS_INITA);
    deadline = timer_deadline_us(100000);
    while ((rd32(b + USDHC_SYS_CTRL) & USDHC_SYS_INITA) != 0) {
        if (timer_expired(deadline))
            break;
    }
}

/* uSDHC divides by (SDCLKFS prescaler) * (DVS + 1). Reporting the requested
 * rate back as the base clock makes the generic driver's divider settle on 1. */
uint32_t sdhci_platform_set_clock(uint32_t clock_khz, uint32_t base_clk_khz)
{
    uintptr_t b = IMX8QM_USDHC_BASE;
    uint32_t target_hz, pre, dvs, v;
    uint64_t deadline;

    (void)base_clk_khz;

    if (clock_khz == 0)
        return 0;

    /* Optional ceiling: sustained reads fail CRC at the 50 MHz the generic
     * driver steps to. Raising it needs uSDHC delay-line tuning. */
#ifdef IMX8QM_USDHC_MAX_CLK_KHZ
    if (clock_khz > IMX8QM_USDHC_MAX_CLK_KHZ)
        clock_khz = IMX8QM_USDHC_MAX_CLK_KHZ;
#endif

    target_hz = clock_khz * 1000U;

    /* Find the smallest prescaler/divisor pair that does not exceed the
     * requested rate. The prescaler is a power of two from 1 to 256, and the
     * divisor runs 1..16. */
    for (pre = 1; pre <= 256; pre <<= 1) {
        for (dvs = 1; dvs <= 16; dvs++) {
            if ((IMX8QM_USDHC_PERCLK_HZ / (pre * dvs)) <= target_hz)
                goto found;
        }
    }
    pre = 256;
    dvs = 16;
found:
    v = rd32(b + USDHC_SYS_CTRL) &
        ~(uint32_t)(USDHC_SYS_DVS_MASK | USDHC_SYS_SDCLKFS_MASK);
    /* SDCLKFS is a one-hot prescaler code: 0x01 = divide by 2, and each
     * further bit doubles it. A prescaler of 1 is encoded as 0. */
    v |= ((pre >> 1) << USDHC_SYS_SDCLKFS_SHIFT) & USDHC_SYS_SDCLKFS_MASK;
    v |= ((dvs - 1) << USDHC_SYS_DVS_SHIFT) & USDHC_SYS_DVS_MASK;
    wr32(b + USDHC_SYS_CTRL, v);

    /* Wait for the divided clock to settle before any command is issued. */
    deadline = timer_deadline_us(100000);
    while ((rd32(b + USDHC_PRES_STATE) & USDHC_PRES_SDSTB) == 0) {
        if (timer_expired(deadline)) {
            wolfBoot_printf("imx8qm usdhc: SD clock never stabilized\n");
            return 0;
        }
    }

    usdhc_achieved_clk_khz = (IMX8QM_USDHC_PERCLK_HZ / (pre * dvs)) / 1000U;
    return usdhc_achieved_clk_khz;
}

void sdhci_platform_irq_init(void)
{
    /* Polled mode: no GIC wiring needed for the boot path. */
}

void sdhci_platform_set_bus_mode(int is_emmc)
{
    /* The bus width is driven through Host Control 1, which the shim
     * translates; there is no separate eMMC/SD mode select on uSDHC. */
    (void)is_emmc;
}

#endif /* DISK_SDCARD || DISK_EMMC */

/* Handoff recon (IMX8QM_HANDOFF_DUMP): read-only dump of the state ATF handed
 * wolfBoot, to confirm the BL33 entry contract on new BSP or silicon. */

#if defined(DEBUG_UART) && defined(IMX8QM_HANDOFF_DUMP)

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

static void imx8qm_handoff_dump(void)
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

    wolfBoot_printf("i.MX8QM handoff recon:\n");
    wolfBoot_printf("  CurrentEL:   EL%d\n", (int)el);
    dump64("  SCTLR_ELx:   ", sctlr);
    wolfBoot_printf("    MMU=%d I$=%d D$=%d\n",
        (int)(sctlr & 0x1), (int)((sctlr >> 12) & 0x1),
        (int)((sctlr >> 2) & 0x1));
    dump64("  MPIDR_EL1:   ", read_mpidr());
    dump64("  CNTFRQ_EL0:  ", cntfrq);
    {
        /* FP/SIMD trapping at EL2: the wolfcrypt ARMv8 assembly is NEON-based,
         * so a set TFP or TZ bit here would trap it. Read-only. */
        uint64_t cptr;
        __asm__ volatile("mrs %0, cptr_el2" : "=r"(cptr));
        dump64("  CPTR_EL2:    ", cptr);
        wolfBoot_printf("    TFP=%d TTA=%d TZ=%d\n",
            (int)((cptr >> 10) & 1), (int)((cptr >> 20) & 1),
            (int)((cptr >> 8) & 1));
    }
    dump64("  handoff x0:  ", x0);

    /* Dump the head of the handoff pointer only if it lands in DRAM (avoid
     * faulting on a stray value). A DTB starts with 0xd00dfeed big-endian. */
    if (x0 >= IMX8QM_DRAM_BASE && x0 <= (IMX8QM_DRAM_END - 16)) {
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
#endif /* DEBUG_UART && IMX8QM_HANDOFF_DUMP */

/* --------------------------------------------------------------------------
 * Identity MMU (IMX8QM_MMU). MMU-off DRAM is uncached, which makes hashing a
 * large payload impractical. Maps the low 4 GB Normal cacheable in four 1 GB
 * level-1 blocks for load-and-verify, then tears it down in hal_prepare_boot()
 * so the payload is entered MMU-off and 1:1. Structure follows hal/cm4.c.
 * -------------------------------------------------------------------------- */

#ifdef IMX8QM_MMU

/* Normal = AttrIdx0 + AF + inner-shareable; Device = AttrIdx1 + AF + XN, since
 * an instruction fetch from Device memory is CONSTRAINED UNPREDICTABLE. */
#define MMU_BLOCK_NORMAL  0x0000000000000701ULL
#define MMU_BLOCK_DEVICE  (0x0000000000000405ULL | (1ULL << 54) | (1ULL << 53))

static volatile uint64_t imx8qm_l1_table[512] __attribute__((aligned(4096)));

/* Data-cache maintenance by set/way over every level to the point of
 * coherency. clean != 0 -> clean+invalidate, else invalidate only. */
#if (defined(DISK_SDCARD) || defined(DISK_EMMC)) && !defined(SDHCI_SDMA_DISABLED)
/* DMA cache maintenance for the uSDHC SDMA path.
 *
 * By virtual-address range, not the set/way walk used at handoff: that walks
 * the whole cache and would cost far more per transfer than the DMA saves.
 * Only meaningful with IMX8QM_MMU, where DRAM is cacheable; with the MMU off
 * the D-cache is disabled and these are harmless no-ops. */
static void imx8qm_dcache_range(uintptr_t start, uint32_t sz, int invalidate)
{
    uintptr_t line, end;
    uint64_t ctr;
    unsigned int dminline;

    if (sz == 0)
        return;
    /* CTR_EL0.DminLine is log2 of the smallest data cache line, in words. */
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    dminline = (unsigned int)((ctr >> 16) & 0xF);
    line = (uintptr_t)4 << dminline;

    end = (start + sz + line - 1) & ~(line - 1);
    start &= ~(line - 1);
    __asm__ volatile("dsb sy");
    for (; start < end; start += line) {
        if (invalidate)
            __asm__ volatile("dc ivac, %0" :: "r"(start) : "memory");
        else
            __asm__ volatile("dc civac, %0" :: "r"(start) : "memory");
    }
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}

void sdhci_platform_dma_prepare(void* buf, uint32_t sz, int is_write)
{
    /* Outbound data must be in memory before the controller reads it. For an
     * inbound transfer, clean too: a dirty line covering the buffer could be
     * evicted over data the controller has already written. */
    imx8qm_dcache_range((uintptr_t)buf, sz, 0);
    (void)is_write;
}

void sdhci_platform_dma_complete(void* buf, uint32_t sz, int is_write)
{
    /* Inbound data landed in memory behind the cache's back; drop any stale
     * lines so the CPU reads what the controller wrote. */
    if (!is_write)
        imx8qm_dcache_range((uintptr_t)buf, sz, 1);
}
#endif /* (DISK_SDCARD || DISK_EMMC) && !SDHCI_SDMA_DISABLED */

static void imx8qm_dcache_maint(int clean)
{
    uint64_t clidr, ccsidr;
    unsigned int level, loc, ctype, linesize, ways, sets, way, set, wayshift;

    __asm__ volatile("dsb sy");
    __asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
    loc = (unsigned int)((clidr >> 24) & 0x7);
    for (level = 0; level < loc; level++) {
        ctype = (unsigned int)((clidr >> (level * 3)) & 0x7);
        if (ctype < 2)          /* no data or unified cache at this level */
            continue;
        __asm__ volatile("msr csselr_el1, %0" :: "r"((uint64_t)(level << 1)));
        __asm__ volatile("isb");
        __asm__ volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));
        linesize = (unsigned int)(ccsidr & 0x7) + 4;
        ways     = (unsigned int)((ccsidr >> 3) & 0x3FF);
        sets     = (unsigned int)((ccsidr >> 13) & 0x7FFF);
        /* __builtin_clz(0) is undefined; a direct-mapped cache never uses the
         * way field, so the shift amount does not matter there. */
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

/* The registers below are EL2 ones. ATF enters BL33 at EL2 on this SoC, but
 * check rather than trap silently if that ever changes. */
static int imx8qm_mmu_at_el2(void)
{
    unsigned long el;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    if (((el >> 2) & 0x3) != 2) {
        wolfBoot_printf("imx8qm: MMU needs EL2, running at EL%d; leaving it off\n",
            (int)((el >> 2) & 0x3));
        return 0;
    }
    return 1;
}

static int imx8qm_mmu_on;

void imx8qm_mmu_enable(void)
{
    unsigned long sctlr;
    int i;

    if (!imx8qm_mmu_at_el2())
        return;

    /* Low 4 GB in 1 GB blocks: DRAM (>= 0x80000000) Normal, the rest Device. */
    for (i = 0; i < 4; i++) {
        uint64_t base = (uint64_t)i << 30;
        imx8qm_l1_table[i] = base |
            ((base >= IMX8QM_DRAM_BASE) ? MMU_BLOCK_NORMAL : MMU_BLOCK_DEVICE);
    }

    /* Attr0 = 0xFF Normal write-back write-allocate, Attr1 = 0x00 Device. */
    __asm__ volatile("msr mair_el2, %0" :: "r"(0x00000000000000FFUL));
    __asm__ volatile("msr ttbr0_el2, %0"
        :: "r"((uint64_t)(uintptr_t)imx8qm_l1_table));
    /* T0SZ=32 (32-bit VA), 4 KB granule, cacheable inner-shareable table
     * walks. Bits 31 and 23 are RES1 for TCR_EL2 when E2H is 0. */
    __asm__ volatile("msr tcr_el2, %0"
        :: "r"(0x0000000000013520UL | (1UL << 31) | (1UL << 23)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");

    /* Drop anything an earlier stage left in the caches before turning them
     * on, so no stale line surfaces once caching is live. */
    imx8qm_dcache_maint(0);
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);   /* M | C | I */
    __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    imx8qm_mmu_on = 1;
}

/* In src/boot_aarch64_start.S, and deliberately in assembly: it touches no
 * stack, which a C equivalent cannot promise. A C set/way walk keeps its loop
 * counters in stack slots and so re-dirties lines behind itself; clearing
 * SCTLR.C then strands them while later stack reads go to DRAM, and any frame
 * sharing a cache line with the walk - the caller's included - reads stale
 * values. See docs/Targets.md. */
extern void el2_flush_and_disable_mmu(void);

void imx8qm_mmu_disable(void)
{
    if (!imx8qm_mmu_on)
        return;

    /* Cleared before the teardown so the store is written back with everything
     * else; afterwards the D-cache is off and this would go straight to DRAM
     * either way. */
    imx8qm_mmu_on = 0;
    el2_flush_and_disable_mmu();
    /* With M clear the stale EL2 mappings can no longer be used, but drop them
     * anyway so nothing inherits them. Register-only, so no stack hazard. */
    __asm__ volatile("tlbi alle2");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
#endif /* IMX8QM_MMU */

/* --------------------------------------------------------------------------
 * FlexSPI bring-up probe (IMX8QM_FLEXSPI_PROBE). Opt-in and read-only: proves
 * the IP command path and the AHB window independently.
 * -------------------------------------------------------------------------- */

#if defined(DEBUG_UART) && defined(EXT_FLASH) && defined(IMX8QM_FLEXSPI_PROBE)
/* Controller state, so a failed command says *why*. Printed before and after
 * the command: a stuck arbiter looks identical to a device that never answered
 * unless STS0/INTR are compared across the attempt. */
static void flexspi_dump_regs(const char* when)
{
    wolfBoot_printf("  [%s] MCR0=%08x MCR2=%08x AHBCR=%08x INTR=%08x\n", when,
        (unsigned)rd32(FSPI_REG(FLEXSPI_MCR0)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_MCR2)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_AHBCR)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_INTR)));
    wolfBoot_printf("  [%s] STS0=%08x STS1=%08x FLSHA1CR0=%08x CR1=%08x "
        "CR2=%08x\n", when,
        (unsigned)rd32(FSPI_REG(FLEXSPI_STS0)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_STS1)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_FLSHA1CR0)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_FLSHA1CR1)),
        (unsigned)rd32(FSPI_REG(FLEXSPI_FLSHA1CR2)));
}

static void imx8qm_flexspi_probe(void)
{
    uint8_t buf[16];
    uint8_t sr = 0;
    int i, ret;
#if defined(IMX8QM_SCU)
    uint32_t rate = 0;

    /* Requested 100 MHz in the resource table; this is what was granted. */
    ret = sc_pm_get_clock_rate(SC_R_FSPI_0, SC_PM_CLK_PER, &rate);
    if (ret != 0)
        wolfBoot_printf("FlexSPI: get_clock_rate failed (%d)\n", ret);
    else
        wolfBoot_printf("FlexSPI: SCU granted PER clock %u Hz\n",
            (unsigned)rate);
#endif

    wolfBoot_printf("FlexSPI probe:\n");
    /* The ext_flash_* entry points init lazily; flexspi_read_sr() below is
     * called directly and does not, so without this the probe drives a
     * controller still in reset (MCR0[MDIS] set, module disabled) and every
     * IP command times out. That is what the earlier "IP command 2 timed
     * out" report was measuring. */
    if (!flexspi_ready) {
        flexspi_dump_regs("reset");
        flexspi_init();
    }
    flexspi_dump_regs("pre");
    /* LUT entry for the sequence that times out, to rule out a bad LUT. */
    wolfBoot_printf("  RDSR LUT: %08x %08x\n",
        (unsigned)rd32(FSPI_LUT(FLEXSPI_LUT_SEQ_RDSR * 4 + 0)),
        (unsigned)rd32(FSPI_LUT(FLEXSPI_LUT_SEQ_RDSR * 4 + 1)));

    ret = flexspi_read_sr(0, &sr);
    flexspi_dump_regs("post-rdsr");
    if (ret != 0) {
        wolfBoot_printf("  status read FAILED (%d) - no IP command path\n",
            ret);
    }
    else {
        /* WIP (bit 0) clear means the device answered and is idle. All-ones
         * or all-zeros usually means nothing is driving the bus at all. */
        wolfBoot_printf("  status register: 0x%02x (WIP=%d, WEL=%d)\n",
            (unsigned)sr, (sr & 0x01) ? 1 : 0, (sr & 0x02) ? 1 : 0);
    }

    ret = ext_flash_read(0, buf, (int)sizeof(buf));
    if (ret != (int)sizeof(buf)) {
        wolfBoot_printf("  AHB read FAILED (%d)\n", ret);
        return;
    }
    wolfBoot_printf("  first 16 bytes @ AHB 0x%08x:",
        (unsigned)IMX8QM_FLEXSPI0_AHB_BASE);
    for (i = 0; i < (int)sizeof(buf); i++)
        wolfBoot_printf(" %02x", (unsigned)buf[i]);
    wolfBoot_printf("\n");

#if defined(IMX8QM_FLEXSPI_WRITE_TEST)
    /* DESTRUCTIVE: erases and reprograms the first sector. Opt-in only, and
     * the one thing host unit tests cannot cover, since they stub the
     * controller. Exercises the shipped ext_flash_* entry points rather than
     * the internal helpers, so a pass means the driver a caller would use
     * works end to end. */
    {
        static const uint8_t pattern[16] = {
            0x77, 0x6F, 0x6C, 0x66, 0x42, 0x6F, 0x6F, 0x74,
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
        };
        uint8_t back[16];
        int bad;

        wolfBoot_printf("  write test: erasing sector 0 (%d KB)\n",
            (int)(FLEXSPI_NOR_SECTOR_SIZE / 1024));
        if (ext_flash_erase(IMX8QM_FLEXSPI0_AHB_BASE,
                FLEXSPI_NOR_SECTOR_SIZE) < 0) {
            wolfBoot_printf("  write test: ERASE FAILED\n");
        }
        else if (ext_flash_read(IMX8QM_FLEXSPI0_AHB_BASE, back,
                    (int)sizeof(back)) != (int)sizeof(back)) {
            wolfBoot_printf("  write test: read-after-erase FAILED\n");
        }
        else {
            for (i = 0, bad = 0; i < (int)sizeof(back); i++) {
                if (back[i] != 0xFF)
                    bad++;
            }
            if (bad != 0) {
                wolfBoot_printf("  write test: erase left %d non-FF bytes\n",
                    bad);
            }
            else if (ext_flash_write(IMX8QM_FLEXSPI0_AHB_BASE, pattern,
                        (int)sizeof(pattern)) < 0) {
                wolfBoot_printf("  write test: PROGRAM FAILED\n");
            }
            else if (ext_flash_read(IMX8QM_FLEXSPI0_AHB_BASE, back,
                        (int)sizeof(back)) != (int)sizeof(back)) {
                wolfBoot_printf("  write test: read-back FAILED\n");
            }
            else {
                wolfBoot_printf("  write test: read back:");
                for (i = 0; i < (int)sizeof(back); i++)
                    wolfBoot_printf(" %02x", (unsigned)back[i]);
                for (i = 0, bad = 0; i < (int)sizeof(back); i++) {
                    if (back[i] != pattern[i])
                        bad++;
                }
                wolfBoot_printf(bad == 0 ? "\n  write test: PASS\n"
                                         : "\n  write test: MISMATCH\n");
                /* A mismatch here is ambiguous: either the program never
                 * reached the device, or it did and the AHB window served a
                 * stale prefetched copy. Separate the two. The status
                 * register comes back over the IP path, which does not go
                 * through the AHB buffers, and re-running init reconfigures
                 * those buffers, so a second read that now matches means the
                 * data was on the device all along. */
                if (bad != 0) {
                    sr = 0xEE;
                    if (flexspi_read_sr(0, &sr) == 0) {
                        wolfBoot_printf("  write test: SR after program "
                            "0x%02x (WIP=%d, WEL=%d)\n", (unsigned)sr,
                            (sr & 0x01) ? 1 : 0, (sr & 0x02) ? 1 : 0);
                    }
                    flexspi_ready = 0;
                    flexspi_init();
                    if (ext_flash_read(IMX8QM_FLEXSPI0_AHB_BASE, back,
                            (int)sizeof(back)) == (int)sizeof(back)) {
                        wolfBoot_printf("  write test: after re-init:");
                        for (i = 0; i < (int)sizeof(back); i++)
                            wolfBoot_printf(" %02x", (unsigned)back[i]);
                        for (i = 0, bad = 0; i < (int)sizeof(back); i++) {
                            if (back[i] != pattern[i])
                                bad++;
                        }
                        wolfBoot_printf(bad == 0
                            ? "\n  write test: data IS on the device; the AHB"
                              " window was stale\n"
                            : "\n  write test: still FF; the program did not"
                              " reach the device\n");
                    }
                }
            }
        }
    }
#endif /* IMX8QM_FLEXSPI_WRITE_TEST */
}
#endif /* DEBUG_UART && EXT_FLASH && IMX8QM_FLEXSPI_PROBE */

/* --------------------------------------------------------------------------
 * EL2 exception report
 * -------------------------------------------------------------------------- */

#if defined(DEBUG) && defined(DEBUG_UART)

/* Image bounds from hal/imx8qm.ld, used to say whether the faulting PC is
 * inside wolfBoot itself. */
extern uint8_t _start_text[];
extern uint8_t _end[];

/* From simple_el2_fault_common (src/boot_aarch64_start.S); the vector halts in
 * wfi after this. Slots 0-7 are current EL (wolfBoot), 8-15 lower EL. */
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector);
void simple_el2_fault_handler(unsigned long esr, unsigned long elr,
    unsigned long far, unsigned long vector)
{
    const char* from;
    uintptr_t pc;

    from = (vector >= 8) ? "lower EL (payload)" : "current EL (wolfBoot)";
    pc = (uintptr_t)elr;

    wolfBoot_printf("\n*** i.MX8QM EL2 EXCEPTION ***\n");
    wolfBoot_printf("vector=%d from %s\n", (int)vector, from);
    wolfBoot_printf("ESR=0x%08x EC=0x%02x ISS=0x%06x\n",
        (uint32_t)esr, (uint32_t)((esr >> 26) & 0x3F),
        (uint32_t)(esr & 0x1FFFFFUL));
    wolfBoot_printf("ELR=0x%08x%08x\n",
        (uint32_t)(elr >> 32), (uint32_t)(elr & 0xFFFFFFFFUL));
    wolfBoot_printf("FAR=0x%08x%08x\n",
        (uint32_t)(far >> 32), (uint32_t)(far & 0xFFFFFFFFUL));
    if (pc >= (uintptr_t)_start_text && pc < (uintptr_t)_end) {
        wolfBoot_printf("ELR is inside wolfBoot (+0x%x from 0x%08x)\n",
            (uint32_t)(pc - (uintptr_t)_start_text),
            (uint32_t)(uintptr_t)_start_text);
    }
    else {
        wolfBoot_printf("ELR is outside wolfBoot [0x%08x-0x%08x)\n",
            (uint32_t)(uintptr_t)_start_text, (uint32_t)(uintptr_t)_end);
    }
}
#endif /* DEBUG && DEBUG_UART */

/* --------------------------------------------------------------------------
 * Device tree fixups
 *
 * The stock imx8qm-mek.dtb depends on U-Boot patching it at runtime: it ships a
 * 1 GB placeholder /memory node and no /chosen/bootargs. wolfBoot replaces
 * U-Boot, so it does the same fixups. See docs/Targets.md.
 * -------------------------------------------------------------------------- */

#if defined(MMU) && defined(WOLFBOOT_FDT)

#include "fdt.h"

/* Console is LPUART0 (ttyLP0). Root defaults to the SD card's third partition
 * (uSDHC2 is mmcblk1; uSDHC1 is the eMMC). Override LINUX_BOOTARGS, or just
 * LINUX_BOOTARGS_ROOT, from the config. */
#ifndef LINUX_BOOTARGS
#ifndef LINUX_BOOTARGS_ROOT
#define LINUX_BOOTARGS_ROOT "/dev/mmcblk1p3"
#endif
#define LINUX_BOOTARGS \
    "console=ttyLP0,115200 earlycon root=" LINUX_BOOTARGS_ROOT \
    " rootfstype=ext4 rootwait rw"
#endif

/* Matching U-Boot's PHYS_SDRAM_1 / PHYS_SDRAM_2. Constants because the MEK is a
 * fixed 6 GB board; override from the config for a different fit. */
#ifndef IMX8QM_DRAM_BANK0_BASE
#define IMX8QM_DRAM_BANK0_BASE 0x0000000080000000ULL
#endif
#ifndef IMX8QM_DRAM_BANK0_SIZE
#define IMX8QM_DRAM_BANK0_SIZE 0x0000000080000000ULL   /* 2 GB */
#endif
#ifndef IMX8QM_DRAM_BANK1_BASE
#define IMX8QM_DRAM_BANK1_BASE 0x0000000880000000ULL
#endif
#ifndef IMX8QM_DRAM_BANK1_SIZE
#define IMX8QM_DRAM_BANK1_SIZE 0x0000000100000000ULL   /* 4 GB */
#endif

/* Full path of the SD slot's controller node in the stock imx8qm-mek.dtb
 * (uSDHC2; uSDHC1 at 5b010000 is the eMMC and must keep 1.8V for HS200/HS400). */
#ifndef IMX8QM_SD_DT_PATH
#define IMX8QM_SD_DT_PATH "/bus@5b000000/mmc@5b020000"
#endif

/* Bus clock ceiling published to the OS for the SD slot. Matches the cap
 * wolfBoot's own driver already applies (IMX8QM_USDHC_MAX_CLK_KHZ). */
#ifndef IMX8QM_SD_DT_MAX_FREQ_HZ
#define IMX8QM_SD_DT_MAX_FREQ_HZ 25000000U
#endif

int hal_dts_fixup(void* dts_addr, uint32_t capacity)
{
    fdt_ctx ctx;
    uint64_t reg[4];
    int off;
    int ret;

    ret = fdt_open(&ctx, dts_addr, capacity);
    if (ret != 0) {
        wolfBoot_printf("FDT: invalid header (%d)\n", ret);
        return ret;
    }

    /* Both fixups add or grow properties, so make room first. */
    ret = fdt_grow(&ctx, WOLFBOOT_FDT_FIXUP_HEADROOM);
    if (ret != 0) {
        wolfBoot_printf("FDT: no headroom for fixups (%d)\n", ret);
        return ret;
    }

    /* /chosen/bootargs. Create /chosen only if it is genuinely absent; any
     * other negative return is a malformed FDT and is surfaced as-is rather
     * than masked by a follow-on fdt_add_subnode() failure. */
    off = fdt_subnode_offset(&ctx, 0, "chosen");
    if (off == -FDT_ERR_NOTFOUND) {
        off = fdt_add_subnode(&ctx, 0, "chosen");
    }
    if (off < 0) {
        wolfBoot_printf("FDT: /chosen error (%d)\n", off);
        return off;
    }
    ret = fdt_fixup_str(&ctx, off, "chosen", "bootargs", LINUX_BOOTARGS);
    if (ret != 0) {
        wolfBoot_printf("FDT: bootargs fixup failed (%d)\n", ret);
        return ret;
    }

    /* "no-1-8-v" plus a clock cap, opt-in via IMX8QM_SD_NO_UHS, for a signal
     * path that cannot carry UHS-I. The MEK itself can: with the card in the
     * socket the OS runs it at DDR50, 1.8V, 50 MHz. An SD multiplexer in the
     * path could not, and produced exactly the failure this avoids, so the
     * option stays available but off by default. The eMMC node is never
     * touched; it needs 1.8V for HS200/HS400. Applies to an eMMC-booting
     * build too, since both pad sets are routed above and the OS therefore
     * sees the SD socket whichever medium wolfBoot booted from. */
#if (defined(DISK_SDCARD) || defined(DISK_EMMC)) && defined(IMX8QM_SD_NO_UHS)
    off = fdt_path_offset(&ctx, IMX8QM_SD_DT_PATH);
    if (off < 0) {
        wolfBoot_printf("FDT: %s not found (%d); SD left UHS-capable\n",
            IMX8QM_SD_DT_PATH, off);
    }
    else {
        /* Empty property: presence is the flag. */
        ret = fdt_setprop(&ctx, off, "no-1-8-v", NULL, 0);
        if (ret != 0) {
            wolfBoot_printf("FDT: no-1-8-v fixup failed (%d)\n", ret);
            return ret;
        }
        /* Cap the clock too: "no-1-8-v" only rules out UHS, leaving high-speed
         * 50 MHz, which this slot is also unreliable at. Uncapped, root mounts
         * and then falls apart under load. */
        {
            uint32_t hz = cpu_to_fdt32(IMX8QM_SD_DT_MAX_FREQ_HZ);

            ret = fdt_setprop(&ctx, off, "max-frequency", &hz,
                (int)sizeof(hz));
            if (ret != 0) {
                wolfBoot_printf("FDT: max-frequency fixup failed (%d)\n", ret);
                return ret;
            }
        }
        wolfBoot_printf("FDT: SD pinned to 3.3V, max %u Hz\n",
            (unsigned)IMX8QM_SD_DT_MAX_FREQ_HZ);
    }
#endif

    /* Located by device_type, not unit address. Root is #address/#size-cells
     * = 2, so each bank is a pair of big-endian 64-bit words. */
    off = fdt_find_devtype(&ctx, 0, "memory");
    if (off < 0) {
        /* Not fatal on its own: a DTB that already describes memory correctly
         * (or a payload that does not need it) still boots. Say so loudly. */
        wolfBoot_printf("FDT: no /memory node (%d); leaving memory as-is\n",
            off);
        return 0;
    }
    reg[0] = cpu_to_fdt64(IMX8QM_DRAM_BANK0_BASE);
    reg[1] = cpu_to_fdt64(IMX8QM_DRAM_BANK0_SIZE);
    reg[2] = cpu_to_fdt64(IMX8QM_DRAM_BANK1_BASE);
    reg[3] = cpu_to_fdt64(IMX8QM_DRAM_BANK1_SIZE);
    ret = fdt_setprop(&ctx, off, "reg", reg, (int)sizeof(reg));
    if (ret != 0) {
        wolfBoot_printf("FDT: memory fixup failed (%d)\n", ret);
        return ret;
    }
    wolfBoot_printf("FDT: memory 0x%08x+0x%08x, 0x%x_%08x+0x%x_%08x\n",
        (uint32_t)IMX8QM_DRAM_BANK0_BASE, (uint32_t)IMX8QM_DRAM_BANK0_SIZE,
        (uint32_t)(IMX8QM_DRAM_BANK1_BASE >> 32),
        (uint32_t)IMX8QM_DRAM_BANK1_BASE,
        (uint32_t)(IMX8QM_DRAM_BANK1_SIZE >> 32),
        (uint32_t)IMX8QM_DRAM_BANK1_SIZE);
    return 0;
}
#endif /* MMU && WOLFBOOT_FDT */

/* --------------------------------------------------------------------------
 * Public HAL entry points
 * -------------------------------------------------------------------------- */

#if defined(DEBUG_UART) && defined(IMX8QM_STACK_PROBE)
/* Stack high-water measurement, to answer whether the wolfcrypt ARMv8 assembly
 * (NO_ARM_ASM=0) overruns the 256 KB stack from hal/imx8qm.ld. Paint on the way
 * in, scan on the way out: the peak then covers everything in between,
 * including the signature verify. */
extern uint32_t _stack_bottom[];
extern uint32_t END_STACK[];

#define STACK_PAINT 0xB7B7B7B7u
/* Leave the live frame alone: paint only well below the current SP. */
#define STACK_PAINT_GUARD 4096u

static void imx8qm_stack_paint(void)
{
    uintptr_t sp;
    uint32_t* p = (uint32_t*)_stack_bottom;
    uint32_t* end;

    __asm__ volatile("mov %0, sp" : "=r"(sp));
    end = (uint32_t*)((sp - STACK_PAINT_GUARD) & ~(uintptr_t)3);
    while (p < end)
        *p++ = STACK_PAINT;
}

static void imx8qm_stack_report(const char* when)
{
    const uint32_t* p = (const uint32_t*)_stack_bottom;
    const uint32_t* top = (const uint32_t*)END_STACK;
    uintptr_t used;

    while (p < top && *p == STACK_PAINT)
        p++;
    used = (uintptr_t)top - (uintptr_t)p;
    wolfBoot_printf("stack[%s]: %lu of %lu bytes used, %lu free\n", when,
        (unsigned long)used,
        (unsigned long)((uintptr_t)top - (uintptr_t)_stack_bottom),
        (unsigned long)((uintptr_t)p - (uintptr_t)_stack_bottom));
}
/* Reported on the panic path too, via wolfBoot's generic WOLFBOOT_HOOK_PANIC.
 * A build whose signature verify fails never reaches hal_prepare_boot(), and
 * the peak matters most in exactly that case. */
void wolfBoot_hook_panic(void)
{
    imx8qm_stack_report("at panic");
}
#endif /* DEBUG_UART && IMX8QM_STACK_PROBE */

#if defined(DEBUG_UART) && defined(IMX8QM_CRYPTO_SELFTEST)
#include <wolfssl/wolfcrypt/ecc.h>
#include "imx8qm_ecc_kat.h"

/* On-target ECDSA P-384 known-answer test.
 *
 * Exists to split one question in two. Enabling the wolfcrypt ARMv8 assembly
 * (NO_ARM_ASM=0) makes the SHA-384 7x faster and correct, but the signature
 * verify of the real image then fails. The same verify passes under QEMU with
 * this target's own define set, so either the board environment breaks the
 * crypto, or wolfBoot feeds it something wrong. This runs the crypto on the
 * board against a fixed vector, before any image is touched:
 *
 *   PASS here -> the crypto is fine on the board; look at wolfBoot's data path.
 *   FAIL here -> the environment (EL2, MMU, caches) breaks it; look there. */
static void imx8qm_crypto_selftest(void)
{
    ecc_key ecc;
    mp_int r, s;
    int ret, verify = 0;

    wolfBoot_printf("ECDSA P-384 self-test: ");
    ret = wc_ecc_init(&ecc);
    if (ret != 0) {
        wolfBoot_printf("init failed %d\n", ret);
        return;
    }
    ret = wc_ecc_import_unsigned(&ecc, (byte*)wb_pubkey, (byte*)(wb_pubkey + 48),
        NULL, ECC_SECP384R1);
    if (ret != 0) {
        wolfBoot_printf("import failed %d\n", ret);
        wc_ecc_free(&ecc);
        return;
    }
    mp_init(&r);
    mp_init(&s);
    mp_read_unsigned_bin(&r, wb_sig, 48);
    mp_read_unsigned_bin(&s, wb_sig + 48, 48);
    ret = wc_ecc_verify_hash_ex(&r, &s, (byte*)wb_digest, 48, &verify, &ecc);
    wolfBoot_printf("ret=%d verify=%d %s\n", ret, verify,
        (ret == 0 && verify == 1) ? "PASS" : "FAIL");
    wc_ecc_free(&ecc);
}
#endif /* DEBUG_UART && IMX8QM_CRYPTO_SELFTEST */

void hal_init(void)
{
    /* Must run first: the SCU powers the console too, so a banner printed
     * before this goes nowhere. Failures are buffered and printed below. */
    imx8qm_scu_init();
#if defined(DEBUG_UART)
    uart_init();
    wolfBoot_printf("wolfBoot i.MX 8QuadMax (bare-metal BL33)\n");
    imx8qm_scu_report();
#if defined(IMX8QM_STACK_PROBE)
    imx8qm_stack_paint();
    imx8qm_stack_report("after hal_init");
#endif
#if defined(IMX8QM_CRYPTO_SELFTEST)
    imx8qm_crypto_selftest();
#endif
#if defined(IMX8QM_HANDOFF_DUMP)
    imx8qm_handoff_dump();      /* read-only: prints state, changes none */
#endif
#if defined(EXT_FLASH) && defined(IMX8QM_FLEXSPI_PROBE)
    imx8qm_flexspi_probe();     /* read-only: proves the NOR responds */
#endif
#endif
#ifdef IMX8QM_MMU
    /* After the console, so a refusal can be reported. Verification of a large
     * image is impractically slow without this; see the MMU section above. */
    imx8qm_mmu_enable();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DEBUG_UART) && defined(IMX8QM_STACK_PROBE)
    imx8qm_stack_report("at handoff");
#endif
    /* No D-cache maintenance needed (MMU off), but SCTLR.I is IMPLEMENTATION
     * DEFINED at BL33 entry, so invalidate the I-cache before branching. */
#if defined(DISK_SDCARD) || defined(DISK_EMMC)
    /* Quiesce the controller so the OS does not inherit a uSDHC this
     * firmware left clocked and running. */
    sdhci_shutdown();
#endif
#ifdef IMX8QM_MMU
    /* Writes the verified payload back to DRAM and returns the core to the
     * MMU-off, 1:1 state the arm64 boot protocol expects. */
    imx8qm_mmu_disable();
#endif
    __asm__ volatile(
        "dsb sy      \n"
        "ic iallu    \n"
        "dsb sy      \n"
        "isb         \n"
        ::: "memory");
}

/* No internal flash on this target: wolfBoot is loaded into DRAM by an earlier
 * stage, and the serial NOR is reached through ext_flash_* above. Provide the
 * required no-op HAL flash surface. */
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
