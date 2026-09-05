/* nxp_esdhc.c
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

/* Freescale/NXP eSDHC block driver for QorIQ (T1040, T1024, T2080).
 *
 * Provides the four entry points src/disk.c expects (disk_init, disk_read,
 * disk_write, disk_close) so the disk boot path, and therefore DISK_FS, can
 * read a signed image from an SD card.
 *
 * This is NOT the Cadence controller driven by src/sdhci.c. The register map
 * is entirely different: eSDHC keeps the command index and transfer setup in
 * one XFERTYP register, combines block size and count into BLKATTR, and has
 * a watermark register with no standard-SDHCI equivalent.
 *
 * Included from hal/nxp_t10xx.c rather than compiled separately, because the
 * clock helpers it needs (hal_get_bus_clk) are static to that translation
 * unit.
 *
 * Transfers use PIO through DATPORT rather than DMA. On e5500 with the MMU
 * enabled a DMA descriptor would need cache maintenance on the destination,
 * and boot-time throughput is dominated by media latency rather than by the
 * copy, so PIO is the simpler and safer choice.
 */

#ifndef _WOLFBOOT_NXP_ESDHC_C_
#define _WOLFBOOT_NXP_ESDHC_C_

#ifdef DISK_SDCARD

#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "printf.h"
#include "nxp_ppc.h"

#ifdef DEBUG_ESDHC
#define ESDHC_DBG(_f_, ...) wolfBoot_printf(_f_, ##__VA_ARGS__)
#else
#define ESDHC_DBG(_f_, ...) do{}while(0)
#endif

/* ---------------------------------------------------------------------
 * Register map. CCSRBAR comes from nxp_ppc.h for the selected target.
 *
 * The block presents its registers big-endian and the e5500 is big-endian,
 * so a native 32-bit access reads them correctly with no swapping. The one
 * exception is DATPORT, whose byte order is selected by PROCTL[EMODE] and is
 * configured below.
 * --------------------------------------------------------------------- */
#ifndef ESDHC_BASE
#define ESDHC_BASE          (CCSRBAR + 0x114000)
#endif
#define ESDHC_REG(off)      ((volatile uint32_t*)(ESDHC_BASE + (off)))

#define ESDHC_DSADDR        0x00
#define ESDHC_BLKATTR       0x04
#define ESDHC_CMDARG        0x08
#define ESDHC_XFERTYP       0x0C
#define ESDHC_CMDRSP0       0x10
#define ESDHC_CMDRSP1       0x14
#define ESDHC_CMDRSP2       0x18
#define ESDHC_CMDRSP3       0x1C
#define ESDHC_DATPORT       0x20
#define ESDHC_PRSSTAT       0x24
#define ESDHC_PROCTL        0x28
#define ESDHC_SYSCTL        0x2C
#define ESDHC_IRQSTAT       0x30
#define ESDHC_IRQSTATEN     0x34
#define ESDHC_IRQSIGEN      0x38
#define ESDHC_HOSTCAPBLT    0x40
#define ESDHC_WML           0x44
#define ESDHC_HOSTVER       0xFC

/* BLKATTR: block count in the high half, block size in the low 13 bits */
#define ESDHC_BLKATTR_CNT(x)   (((uint32_t)(x)) << 16)
#define ESDHC_BLKATTR_SIZE(x)  ((uint32_t)(x) & 0x1FFFU)

/* XFERTYP */
#define ESDHC_XFERTYP_CMDINX(x) (((uint32_t)(x)) << 24)
#define ESDHC_XFERTYP_DPSEL     (1U << 21)  /* data present */
#define ESDHC_XFERTYP_CICEN     (1U << 20)  /* check command index */
#define ESDHC_XFERTYP_CCCEN     (1U << 19)  /* check command CRC */
#define ESDHC_XFERTYP_RSPTYP_NONE (0U << 16)
#define ESDHC_XFERTYP_RSPTYP_136  (1U << 16)
#define ESDHC_XFERTYP_RSPTYP_48   (2U << 16)
#define ESDHC_XFERTYP_RSPTYP_48B  (3U << 16)
#define ESDHC_XFERTYP_MSBSEL    (1U << 5)   /* multi-block */
#define ESDHC_XFERTYP_DTDSEL    (1U << 4)   /* 1 = read */
#define ESDHC_XFERTYP_AC12EN    (1U << 2)   /* auto CMD12 */
#define ESDHC_XFERTYP_BCEN      (1U << 1)   /* block count enable */

/* PRSSTAT */
#define ESDHC_PRSSTAT_CIHB      (1U << 0)   /* command inhibit (CMD) */
#define ESDHC_PRSSTAT_CDIHB     (1U << 1)   /* command inhibit (DAT) */
#define ESDHC_PRSSTAT_DLA       (1U << 2)   /* data line active */
#define ESDHC_PRSSTAT_SDSTB     (1U << 3)   /* SD clock stable */
#define ESDHC_PRSSTAT_BREN      (1U << 11)  /* buffer read enable */
#define ESDHC_PRSSTAT_CINS      (1U << 16)  /* card inserted */

/* PROCTL */
#define ESDHC_PROCTL_DTW_1BIT   (0U << 1)
#define ESDHC_PROCTL_DTW_4BIT   (1U << 1)
#define ESDHC_PROCTL_DTW_MASK   (3U << 1)
/* EMODE selects the byte order of DATPORT. Big-endian mode delivers bytes
 * in media order when the word is stored natively by this big-endian core.
 * Verified on T1040D4RDB silicon: little-endian mode read every aligned
 * 4-byte group byte-reversed (MBR signature came back AA55). */
#define ESDHC_PROCTL_EMODE_BE   (0U << 4)
#define ESDHC_PROCTL_EMODE_MASK (3U << 4)

/* SYSCTL */
#define ESDHC_SYSCTL_IPGEN      (1U << 0)
#define ESDHC_SYSCTL_HCKEN      (1U << 1)
#define ESDHC_SYSCTL_PEREN      (1U << 2)
#define ESDHC_SYSCTL_SDCLKEN    (1U << 3)
#define ESDHC_SYSCTL_DTOCV(x)   (((uint32_t)(x) & 0xFU) << 16)
#define ESDHC_SYSCTL_SDCLKFS(x) (((uint32_t)(x) & 0xFFU) << 8)
#define ESDHC_SYSCTL_DVS(x)     (((uint32_t)(x) & 0xFU) << 4)
#define ESDHC_SYSCTL_RSTA       (1U << 24)  /* reset all */
#define ESDHC_SYSCTL_RSTC       (1U << 25)  /* reset command line */
#define ESDHC_SYSCTL_RSTD       (1U << 26)  /* reset data line */
#define ESDHC_SYSCTL_INITA      (1U << 27)  /* send 80 init clocks */

/* IRQSTAT */
#define ESDHC_IRQSTAT_CC        (1U << 0)   /* command complete */
#define ESDHC_IRQSTAT_TC        (1U << 1)   /* transfer complete */
#define ESDHC_IRQSTAT_BRR       (1U << 5)   /* buffer read ready */
#define ESDHC_IRQSTAT_CTOE      (1U << 16)  /* command timeout */
#define ESDHC_IRQSTAT_CCE       (1U << 17)  /* command CRC error */
#define ESDHC_IRQSTAT_CEBE      (1U << 18)
#define ESDHC_IRQSTAT_CIE       (1U << 19)
#define ESDHC_IRQSTAT_DTOE      (1U << 20)  /* data timeout */
#define ESDHC_IRQSTAT_DCE       (1U << 21)  /* data CRC error */
#define ESDHC_IRQSTAT_DEBE      (1U << 22)
#define ESDHC_IRQSTAT_ALL       0xFFFFFFFFU

#define ESDHC_IRQSTAT_CMD_ERR   (ESDHC_IRQSTAT_CTOE | ESDHC_IRQSTAT_CCE | \
                                 ESDHC_IRQSTAT_CEBE | ESDHC_IRQSTAT_CIE)
#define ESDHC_IRQSTAT_DAT_ERR   (ESDHC_IRQSTAT_DTOE | ESDHC_IRQSTAT_DCE | \
                                 ESDHC_IRQSTAT_DEBE)

/* SD commands used here */
#define SD_CMD_GO_IDLE           0
#define SD_CMD_ALL_SEND_CID      2
#define SD_CMD_SEND_REL_ADDR     3
#define SD_CMD_SELECT_CARD       7
#define SD_CMD_SEND_IF_COND      8
#define SD_CMD_SEND_CSD          9
#define SD_CMD_SET_BLOCKLEN     16
#define SD_CMD_READ_SINGLE      17
#define SD_CMD_READ_MULTI       18
#define SD_CMD_APP_CMD          55
#define SD_ACMD_SET_BUS_WIDTH    6
#define SD_ACMD_SEND_OP_COND    41

#define SD_BLOCK_SIZE          512U
#define SD_OCR_BUSY     (1UL << 31)
#define SD_OCR_HCS      (1UL << 30)
#define SD_IF_COND_ARG  0x000001AAU  /* 2.7-3.6V, check pattern 0xAA */

/* Card state discovered during init */
static uint32_t g_esdhc_rca;      /* relative card address, in the high half */
static int      g_esdhc_hc;       /* 1 when the card is high capacity (SDHC) */
static int      g_esdhc_ready;

/* ---------------------------------------------------------------------
 * Timebase. The e5500 time base increments at the platform (CCB) clock
 * divided by 16; TIMEBASE_HZ comes from nxp_ppc.c in this translation
 * unit. Verified on T1040D4RDB silicon: CCB 600 MHz, timebase reads
 * 37500000 Hz (600 MHz / 16).
 * --------------------------------------------------------------------- */

static uint64_t esdhc_timebase(void)
{
    uint32_t hi, lo, hi2;

    /* Re-read the upper half to guard against a carry between the two
     * reads; the pair is not atomic. */
    do {
        __asm__ __volatile__("mfspr %0, 269" : "=r"(hi));
        __asm__ __volatile__("mfspr %0, 268" : "=r"(lo));
        __asm__ __volatile__("mfspr %0, 269" : "=r"(hi2));
    } while (hi != hi2);

    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static uint64_t esdhc_timer_us(void)
{
    uint32_t tb_hz = TIMEBASE_HZ;

    if (tb_hz == 0U) {
        return 0;
    }
    return (esdhc_timebase() * 1000000ULL) / (uint64_t)tb_hz;
}

static void esdhc_udelay(uint32_t us)
{
    uint64_t end = esdhc_timer_us() + (uint64_t)us;

    while (esdhc_timer_us() < end) {
        /* spin */
    }
}

/* ---------------------------------------------------------------------
 * Low level helpers
 * --------------------------------------------------------------------- */

/* Wait for a set of IRQSTAT bits, or for any error bit. Returns 0 on the
 * expected completion, or a negative value on error or timeout. */
static int esdhc_wait_irq(uint32_t want, uint32_t err_mask, uint32_t timeout_us)
{
    uint64_t end = esdhc_timer_us() + (uint64_t)timeout_us;
    uint32_t stat;

    for (;;) {
        stat = *ESDHC_REG(ESDHC_IRQSTAT);
        if ((stat & err_mask) != 0U) {
            ESDHC_DBG("esdhc: irq error %x\r\n", stat);
            return -1;
        }
        if ((stat & want) == want) {
            return 0;
        }
        if (esdhc_timer_us() > end) {
            ESDHC_DBG("esdhc: irq timeout, stat %x\r\n", stat);
            return -1;
        }
    }
}

/* Wait until the controller will accept a new command. */
static int esdhc_wait_ready(int need_dat, uint32_t timeout_us)
{
    uint64_t end = esdhc_timer_us() + (uint64_t)timeout_us;
    uint32_t mask = ESDHC_PRSSTAT_CIHB;

    if (need_dat != 0) {
        mask |= ESDHC_PRSSTAT_CDIHB | ESDHC_PRSSTAT_DLA;
    }
    while ((*ESDHC_REG(ESDHC_PRSSTAT) & mask) != 0U) {
        if (esdhc_timer_us() > end) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Issue one command and collect its response.
 *
 * @param resp  Receives up to four response words when non-NULL. A 136-bit
 *              response is returned as the controller presents it, which is
 *              shifted left by 8 bits relative to the card's CID/CSD.
 */
static int esdhc_send_cmd(uint32_t idx, uint32_t arg, uint32_t xfertyp,
                          uint32_t *resp)
{
    uint32_t cmd;
    int ret;

    if (esdhc_wait_ready((xfertyp & ESDHC_XFERTYP_DPSEL) != 0U,
            1000000U) != 0) {
        ESDHC_DBG("esdhc: controller busy before CMD%u\r\n", idx);
        return -1;
    }

    /* Clear any stale status before starting. */
    *ESDHC_REG(ESDHC_IRQSTAT) = ESDHC_IRQSTAT_ALL;

    *ESDHC_REG(ESDHC_CMDARG) = arg;
    cmd = ESDHC_XFERTYP_CMDINX(idx) | xfertyp;
    *ESDHC_REG(ESDHC_XFERTYP) = cmd;

    ret = esdhc_wait_irq(ESDHC_IRQSTAT_CC, ESDHC_IRQSTAT_CMD_ERR, 1000000U);
    if (ret != 0) {
        return ret;
    }

    if (resp != NULL) {
        resp[0] = *ESDHC_REG(ESDHC_CMDRSP0);
        resp[1] = *ESDHC_REG(ESDHC_CMDRSP1);
        resp[2] = *ESDHC_REG(ESDHC_CMDRSP2);
        resp[3] = *ESDHC_REG(ESDHC_CMDRSP3);
    }
    return 0;
}

/* Set the SD clock. The divider is SDCLKFS (base 2 prescaler) times DVS. */
static void esdhc_set_clock(uint32_t target_hz)
{
    uint32_t base = hal_get_bus_clk();
    uint32_t pre = 2, div = 1, sysctl;

    if (target_hz == 0U) {
        return;
    }
    /* Stop the clock while the divider changes. */
    sysctl = *ESDHC_REG(ESDHC_SYSCTL);
    *ESDHC_REG(ESDHC_SYSCTL) = sysctl & ~ESDHC_SYSCTL_SDCLKEN;

    while ((pre < 256U) && ((base / pre) > target_hz)) {
        pre <<= 1;
    }
    while ((div < 16U) && (((base / pre) / div) > target_hz)) {
        div++;
    }

    sysctl = *ESDHC_REG(ESDHC_SYSCTL);
    sysctl &= ~(ESDHC_SYSCTL_SDCLKFS(0xFF) | ESDHC_SYSCTL_DVS(0xF));
    sysctl |= ESDHC_SYSCTL_SDCLKFS(pre >> 1) | ESDHC_SYSCTL_DVS(div - 1U);
    sysctl |= ESDHC_SYSCTL_DTOCV(0xE);
    sysctl |= ESDHC_SYSCTL_IPGEN | ESDHC_SYSCTL_HCKEN | ESDHC_SYSCTL_PEREN;
    *ESDHC_REG(ESDHC_SYSCTL) = sysctl;

    /* Wait for the divider to take effect, then re-enable the card clock. */
    esdhc_udelay(100);
    *ESDHC_REG(ESDHC_SYSCTL) = *ESDHC_REG(ESDHC_SYSCTL) |
        ESDHC_SYSCTL_SDCLKEN;
    esdhc_udelay(100);

    ESDHC_DBG("esdhc: clock %u Hz (pre %u, div %u)\r\n",
        (base / pre) / div, pre, div);
}


/* ---------------------------------------------------------------------
 * Card initialisation
 * --------------------------------------------------------------------- */

/* Reset the controller and bring the bus up at the 400 kHz identification
 * clock, 1-bit wide. */
static int esdhc_host_init(void)
{
    uint32_t proctl;
    uint64_t end;

    /* Reset all. The bit self-clears when the reset completes. */
    *ESDHC_REG(ESDHC_SYSCTL) = *ESDHC_REG(ESDHC_SYSCTL) | ESDHC_SYSCTL_RSTA;
    end = esdhc_timer_us() + 1000000ULL;
    while ((*ESDHC_REG(ESDHC_SYSCTL) & ESDHC_SYSCTL_RSTA) != 0U) {
        if (esdhc_timer_us() > end) {
            ESDHC_DBG("esdhc: controller reset timeout\r\n");
            return -1;
        }
    }

    /* Mask interrupt delivery but enable status reporting: this driver
     * polls IRQSTAT rather than taking interrupts. */
    *ESDHC_REG(ESDHC_IRQSTATEN) = ESDHC_IRQSTAT_ALL;
    *ESDHC_REG(ESDHC_IRQSIGEN) = 0;
    *ESDHC_REG(ESDHC_IRQSTAT) = ESDHC_IRQSTAT_ALL;

    /* 1-bit bus for identification, and set the data-port byte order. */
    proctl = *ESDHC_REG(ESDHC_PROCTL);
    proctl &= ~(ESDHC_PROCTL_DTW_MASK | ESDHC_PROCTL_EMODE_MASK);
    proctl |= ESDHC_PROCTL_DTW_1BIT | ESDHC_PROCTL_EMODE_BE;
    *ESDHC_REG(ESDHC_PROCTL) = proctl;

    esdhc_set_clock(400000U);

    /* Drive the 80 initialisation clocks the card needs before CMD0. */
    *ESDHC_REG(ESDHC_SYSCTL) = *ESDHC_REG(ESDHC_SYSCTL) | ESDHC_SYSCTL_INITA;
    end = esdhc_timer_us() + 1000000ULL;
    while ((*ESDHC_REG(ESDHC_SYSCTL) & ESDHC_SYSCTL_INITA) != 0U) {
        if (esdhc_timer_us() > end) {
            ESDHC_DBG("esdhc: INITA timeout\r\n");
            return -1;
        }
    }
    return 0;
}

/* CMD55 + the given application command. */
static int esdhc_send_acmd(uint32_t idx, uint32_t arg, uint32_t xfertyp,
                           uint32_t *resp)
{
    int ret;

    ret = esdhc_send_cmd(SD_CMD_APP_CMD, g_esdhc_rca,
        ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN | ESDHC_XFERTYP_CCCEN,
        NULL);
    if (ret != 0) {
        return ret;
    }
    return esdhc_send_cmd(idx, arg, xfertyp, resp);
}

/**
 * @brief Take the card from idle to transfer state.
 *
 * CMD0 -> CMD8 -> ACMD41 -> CMD2 -> CMD3 -> CMD7 -> ACMD6 -> CMD16.
 * CMD8 is what distinguishes an SD v2 card, and only a card that answered
 * it may be told HCS in ACMD41; a v1 card must not see that bit set.
 */
static int esdhc_card_init(void)
{
    uint32_t resp[4];
    uint32_t arg;
    int v2 = 0;
    int ret;
    uint64_t end;

    g_esdhc_rca = 0;
    g_esdhc_hc = 0;

    ret = esdhc_send_cmd(SD_CMD_GO_IDLE, 0, ESDHC_XFERTYP_RSPTYP_NONE, NULL);
    if (ret != 0) {
        ESDHC_DBG("esdhc: CMD0 failed\r\n");
        return ret;
    }
    esdhc_udelay(2000);

    /* CMD8. A card that does not respond is pre-v2; that is not an error. */
    if (esdhc_send_cmd(SD_CMD_SEND_IF_COND, SD_IF_COND_ARG,
            ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN |
            ESDHC_XFERTYP_CCCEN, resp) == 0) {
        if ((resp[0] & 0xFFU) != 0xAAU) {
            ESDHC_DBG("esdhc: CMD8 check pattern %x\r\n", resp[0]);
            return -1;
        }
        v2 = 1;
    }
    else {
        /* CMD8 leaves the command line in error state on a v1 card. */
        *ESDHC_REG(ESDHC_SYSCTL) = *ESDHC_REG(ESDHC_SYSCTL) |
            ESDHC_SYSCTL_RSTC;
        *ESDHC_REG(ESDHC_IRQSTAT) = ESDHC_IRQSTAT_ALL;
    }

    /* ACMD41 until the card leaves busy. Only a v2 card may be offered
     * HCS; setting it for a v1 card is out of spec. */
    arg = 0x00FF8000U;              /* 2.7-3.6V window */
    if (v2 != 0) {
        arg |= SD_OCR_HCS;
    }
    end = esdhc_timer_us() + 2000000ULL;   /* spec allows up to 1 s */
    for (;;) {
        ret = esdhc_send_acmd(SD_ACMD_SEND_OP_COND, arg,
            ESDHC_XFERTYP_RSPTYP_48, resp);
        if (ret != 0) {
            ESDHC_DBG("esdhc: ACMD41 failed\r\n");
            return ret;
        }
        if ((resp[0] & SD_OCR_BUSY) != 0U) {
            break;
        }
        if (esdhc_timer_us() > end) {
            ESDHC_DBG("esdhc: ACMD41 busy timeout\r\n");
            return -1;
        }
        esdhc_udelay(1000);
    }
    /* CCS in the OCR says block addressing rather than byte addressing. */
    g_esdhc_hc = ((resp[0] & SD_OCR_HCS) != 0U) ? 1 : 0;

    ret = esdhc_send_cmd(SD_CMD_ALL_SEND_CID, 0,
        ESDHC_XFERTYP_RSPTYP_136 | ESDHC_XFERTYP_CCCEN, resp);
    if (ret != 0) {
        ESDHC_DBG("esdhc: CMD2 failed\r\n");
        return ret;
    }

    ret = esdhc_send_cmd(SD_CMD_SEND_REL_ADDR, 0,
        ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN | ESDHC_XFERTYP_CCCEN,
        resp);
    if (ret != 0) {
        ESDHC_DBG("esdhc: CMD3 failed\r\n");
        return ret;
    }
    g_esdhc_rca = resp[0] & 0xFFFF0000U;
    ESDHC_DBG("esdhc: RCA %x, %s capacity\r\n", g_esdhc_rca,
        (g_esdhc_hc != 0) ? "high" : "standard");

    ret = esdhc_send_cmd(SD_CMD_SELECT_CARD, g_esdhc_rca,
        ESDHC_XFERTYP_RSPTYP_48B | ESDHC_XFERTYP_CICEN | ESDHC_XFERTYP_CCCEN,
        NULL);
    if (ret != 0) {
        ESDHC_DBG("esdhc: CMD7 failed\r\n");
        return ret;
    }

    /* 4-bit bus. Card first, then the controller, so the two never
     * disagree about the width mid-transfer. */
    if (esdhc_send_acmd(SD_ACMD_SET_BUS_WIDTH, 2U,
            ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN |
            ESDHC_XFERTYP_CCCEN, NULL) == 0) {
        uint32_t proctl = *ESDHC_REG(ESDHC_PROCTL);
        proctl &= ~ESDHC_PROCTL_DTW_MASK;
        proctl |= ESDHC_PROCTL_DTW_4BIT;
        *ESDHC_REG(ESDHC_PROCTL) = proctl;
    }

    /* Harmless on a high-capacity card, which is fixed at 512. */
    (void)esdhc_send_cmd(SD_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE,
        ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN | ESDHC_XFERTYP_CCCEN,
        NULL);

    /* Identification is done; run at full speed. */
    esdhc_set_clock(25000000U);
    return 0;
}

/* ---------------------------------------------------------------------
 * Block read
 * --------------------------------------------------------------------- */

/* Drain one block from the data port.
 *
 * PROCTL[EMODE] is set to big-endian above, so a native 32-bit read of
 * DATPORT returns the four media bytes already in order and they can be
 * stored as-is. Silicon-verified: little-endian mode returned every
 * aligned 4-byte group byte-reversed. */
static int esdhc_read_block(uint8_t *buf)
{
    uint32_t i, word;
    int ret;

    ret = esdhc_wait_irq(ESDHC_IRQSTAT_BRR, ESDHC_IRQSTAT_DAT_ERR, 1000000U);
    if (ret != 0) {
        return ret;
    }
    for (i = 0; i < (SD_BLOCK_SIZE / 4U); i++) {
        word = *ESDHC_REG(ESDHC_DATPORT);
        memcpy(buf + (i * 4U), &word, 4U);
    }
    *ESDHC_REG(ESDHC_IRQSTAT) = ESDHC_IRQSTAT_BRR;
    return 0;
}

/* One transfer: count is bounded by the caller to the 16-bit block count
 * field of BLKATTR. */
static int esdhc_read_blocks_chunk(uint64_t lba, uint32_t count, uint8_t *buf)
{
    uint32_t xfertyp, i;
    uint32_t arg;
    int ret;

    if ((count == 0U) || (buf == NULL)) {
        return -1;
    }

    /* Standard-capacity cards are addressed in bytes, high-capacity in
     * blocks. Getting this backwards reads from a wildly wrong offset. */
    arg = (g_esdhc_hc != 0) ? (uint32_t)lba
                            : (uint32_t)(lba * SD_BLOCK_SIZE);

    /* Read watermark in words. */
    *ESDHC_REG(ESDHC_WML) = (SD_BLOCK_SIZE / 4U);
    *ESDHC_REG(ESDHC_BLKATTR) = ESDHC_BLKATTR_CNT(count) |
        ESDHC_BLKATTR_SIZE(SD_BLOCK_SIZE);

    xfertyp = ESDHC_XFERTYP_DPSEL | ESDHC_XFERTYP_DTDSEL |
        ESDHC_XFERTYP_RSPTYP_48 | ESDHC_XFERTYP_CICEN | ESDHC_XFERTYP_CCCEN;
    if (count > 1U) {
        xfertyp |= ESDHC_XFERTYP_MSBSEL | ESDHC_XFERTYP_BCEN |
            ESDHC_XFERTYP_AC12EN;
        ret = esdhc_send_cmd(SD_CMD_READ_MULTI, arg, xfertyp, NULL);
    }
    else {
        ret = esdhc_send_cmd(SD_CMD_READ_SINGLE, arg, xfertyp, NULL);
    }
    if (ret != 0) {
        ESDHC_DBG("esdhc: read cmd failed at lba %u\r\n", (uint32_t)lba);
        return ret;
    }

    for (i = 0; i < count; i++) {
        ret = esdhc_read_block(buf + ((size_t)i * SD_BLOCK_SIZE));
        if (ret != 0) {
            return ret;
        }
    }

    return esdhc_wait_irq(ESDHC_IRQSTAT_TC, ESDHC_IRQSTAT_DAT_ERR, 5000000U);
}

/* BLKATTR encodes the block count in a 16-bit field, so split larger
 * requests into multiple transfers. */
#define ESDHC_MAX_BLK_CNT 0xFFFFU

static int esdhc_read_blocks(uint64_t lba, uint32_t count, uint8_t *buf)
{
    uint32_t chunk;
    int ret;

    if ((count == 0U) || (buf == NULL)) {
        return -1;
    }
    while (count > 0U) {
        chunk = count;
        if (chunk > ESDHC_MAX_BLK_CNT) {
            chunk = ESDHC_MAX_BLK_CNT;
        }
        ret = esdhc_read_blocks_chunk(lba, chunk, buf);
        if (ret != 0) {
            return ret;
        }
        lba += chunk;
        buf += (size_t)chunk * SD_BLOCK_SIZE;
        count -= chunk;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * disk.c interface
 * --------------------------------------------------------------------- */

int disk_init(int drv)
{
    if (drv != 0) {
        return -1;
    }
    if (g_esdhc_ready != 0) {
        return 0;
    }
#ifdef DEBUG_ESDHC
    /* Bring-up probes, in dependency order (see docs). The two timebase
     * markers must land ~1 s apart on a timestamped console; if not,
     * ESDHC_TB_DIV is wrong and every timeout below is wrong with it.
     * HOSTVER/HOSTCAPBLT prove the base address and register byte order
     * before any card interaction; CINS proves the socket sees a card. */
    ESDHC_DBG("esdhc: timebase check begin (expect 1s gap)\r\n");
    esdhc_udelay(1000000U);
    ESDHC_DBG("esdhc: timebase check end\r\n");
    ESDHC_DBG("esdhc: HOSTVER %x HOSTCAPBLT %x PRSSTAT %x (CINS %u)\r\n",
        *ESDHC_REG(ESDHC_HOSTVER), *ESDHC_REG(ESDHC_HOSTCAPBLT),
        *ESDHC_REG(ESDHC_PRSSTAT),
        (*ESDHC_REG(ESDHC_PRSSTAT) & ESDHC_PRSSTAT_CINS) != 0U ? 1U : 0U);
#endif
    if (esdhc_host_init() != 0) {
        return -1;
    }
    if (esdhc_card_init() != 0) {
        return -1;
    }
    g_esdhc_ready = 1;
    return 0;
}

/* Byte-granular read on top of a block device. start and count need not be
 * sector aligned, so the head and tail are staged through a bounce block. */
int disk_read(int drv, uint64_t start, uint32_t count, uint8_t *buf)
{
    uint8_t block[SD_BLOCK_SIZE];
    uint64_t lba;
    uint32_t done = 0, chunk, off, whole;

    if ((drv != 0) || (buf == NULL)) {
        return -1;
    }
    if (g_esdhc_ready == 0) {
        return -1;
    }

    while (done < count) {
        lba = (start + done) / SD_BLOCK_SIZE;
        off = (uint32_t)((start + done) % SD_BLOCK_SIZE);

        if ((off == 0U) && ((count - done) >= SD_BLOCK_SIZE)) {
            /* Aligned run: read whole blocks straight into the caller's
             * buffer, no bounce. */
            whole = (count - done) / SD_BLOCK_SIZE;
            if (esdhc_read_blocks(lba, whole, buf + done) != 0) {
                return -1;
            }
            done += whole * SD_BLOCK_SIZE;
            continue;
        }

        if (esdhc_read_blocks(lba, 1U, block) != 0) {
            return -1;
        }
        chunk = SD_BLOCK_SIZE - off;
        if (chunk > (count - done)) {
            chunk = count - done;
        }
        memcpy(buf + done, block + off, chunk);
        done += chunk;
    }
    return (int)done;
}

int disk_write(int drv, uint64_t start, uint32_t count, const uint8_t *buf)
{
    /* wolfBoot only reads from the boot media on this target. Returning an
     * error is deliberate: a silent success would let a caller believe an
     * update had been written. */
    (void)drv; (void)start; (void)count; (void)buf;
    return -1;
}

void disk_close(int drv)
{
    (void)drv;
    g_esdhc_ready = 0;
}

#endif /* DISK_SDCARD */

#endif /* _WOLFBOOT_NXP_ESDHC_C_ */
