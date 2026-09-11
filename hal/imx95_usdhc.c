/* imx95_usdhc.c
 *
 * Minimal uSDHC (SD card) driver for wolfBoot on the NXP i.MX95.
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

/* Minimal SD-only driver for the i.MX uSDHC (i.MX 95 RM; same IP as i.MX
 * 6/7/8). NOT SDHCI-register-compatible, so src/sdhci.c does not apply.
 * PIO reads at 25 MHz default speed, no DMA/tuning/1.8V; disk_write() is
 * not implemented (updates are staged by the OS). Module clock and pinmux
 * must already be up (SPL/prior stage); only SYS_CTRL dividers are set. */

#include <stdint.h>
#include <stddef.h>
#include "printf.h"
#include "hal/imx95_a55.h"

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

#ifndef USDHC_BASE
#define USDHC_BASE          IMX95_USDHC2_BASE
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
#define USDHC_VEND_SPEC         0xC0

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
#define SYS_CTRL_RSTA           (1UL << 24)   /* reset all */
#define SYS_CTRL_RSTC           (1UL << 25)   /* reset cmd */
#define SYS_CTRL_RSTD           (1UL << 26)   /* reset data */

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

static int card_rca;        /* relative card address, from CMD3 */
static int card_high_cap;   /* SDHC/SDXC: block addressing */
static int card_ready;

static inline uint32_t rd(uint32_t off)
{
    return *(volatile uint32_t*)(USDHC_BASE + off);
}

static inline void wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t*)(USDHC_BASE + off) = v;
}

/* --- Low level ----------------------------------------------------------- */

static int usdhc_wait_clear(uint32_t off, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        if ((rd(off) & mask) == 0)
            return 0;
    }
    return -1;
}

static int usdhc_wait_set(uint32_t off, uint32_t mask)
{
    uint32_t n;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        if ((rd(off) & mask) != 0)
            return 0;
    }
    return -1;
}

static void usdhc_reset(uint32_t bits)
{
    wr(USDHC_SYS_CTRL, rd(USDHC_SYS_CTRL) | bits);
    (void)usdhc_wait_clear(USDHC_SYS_CTRL, bits);
}

/* Divider = prescaler (2^n, field 0x01..0x80) x divisor (1..16). At the
 * 400 MHz module clock default: /256/4 = ~390 kHz id, /2/8 = 25 MHz xfer. */
static void usdhc_set_clock(int identification)
{
    uint32_t v;

    v = rd(USDHC_SYS_CTRL);
    v &= ~((0xFFUL << SYS_CTRL_SDCLKFS_SHIFT) | (0xFUL << SYS_CTRL_DVS_SHIFT)
           | (0xFUL << SYS_CTRL_DTOCV_SHIFT));
    if (identification) {
        v |= (0x80UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /256 */
        v |= (0x3UL << SYS_CTRL_DVS_SHIFT);       /* /4  -> ~390 kHz */
    }
    else {
        v |= (0x01UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /2 */
        v |= (0x7UL << SYS_CTRL_DVS_SHIFT);       /* /8  -> 25 MHz */
    }
    v |= (0xEUL << SYS_CTRL_DTOCV_SHIFT);         /* max data timeout */
    wr(USDHC_SYS_CTRL, v);
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
    wr(USDHC_INT_STATUS, 0xFFFFFFFFUL);

    /* MIX_CTRL first: the CMD_XFR_TYP write launches the command. */
    if (data) {
        uint32_t mix = MIX_CTRL_DTDSEL_READ;
        if (multi)
            mix |= MIX_CTRL_MSBSEL | MIX_CTRL_BCEN | MIX_CTRL_AC12EN;
        wr(USDHC_MIX_CTRL, mix);
    }
    else {
        wr(USDHC_MIX_CTRL, 0);
    }

    xfr = CMD_XFR_CMDINX(idx);
    if (rsp136)
        xfr |= CMD_XFR_RSPTYP_136;
    else if (rsp_busy)
        xfr |= CMD_XFR_RSPTYP_48B;
    else
        xfr |= CMD_XFR_RSPTYP_48;
    if (check_crc_idx)
        xfr |= CMD_XFR_CICEN | CMD_XFR_CCCEN;
    if (data)
        xfr |= CMD_XFR_DPSEL;

    wr(USDHC_CMD_ARG, arg);
    wr(USDHC_CMD_XFR_TYP, xfr);

    /* Wait for command complete or error */
    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        st = rd(USDHC_INT_STATUS);
        if (st & INT_CMD_ERRS) {
            wr(USDHC_INT_STATUS, INT_CMD_ERRS | INT_CC);
            usdhc_reset(SYS_CTRL_RSTC);
            return (st & INT_CTOE) ? -2 : -3;
        }
        if (st & INT_CC) {
            wr(USDHC_INT_STATUS, INT_CC);
            return 0;
        }
    }
    usdhc_reset(SYS_CTRL_RSTC);
    return -1;
}

/* --- Card bring-up ------------------------------------------------------- */

static int sd_card_init(void)
{
    uint32_t rsp;
    uint32_t n;
    int ret;

    card_rca = 0;
    card_high_cap = 0;

    usdhc_reset(SYS_CTRL_RSTA);

    /* CDTL+CDSS force card-present: boards routing CD to a GPIO leave the
     * dedicated CD pad floating, so CINST never sets. The card answering
     * CMD8/ACMD41 is the real presence check. */
    wr(USDHC_PROT_CTRL, PROT_CTRL_EMODE_LE | PROT_CTRL_DTW_1BIT |
                        PROT_CTRL_CDTL | PROT_CTRL_CDSS);
    /* PIO watermark: one 512-byte block = 128 words on both sides */
    wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    /* Enable status bits (polled; signals stay off) */
    wr(USDHC_INT_STATUS_EN, 0xFFFFFFFFUL);
    wr(USDHC_INT_SIGNAL_EN, 0);

    usdhc_set_clock(1);

    /* CMD0: idle */
    (void)usdhc_cmd(SD_CMD0_GO_IDLE, 0, 0, 0, 0, 0, 0);

    /* CMD8 voltage check (2.7-3.6V, pattern 0xAA). SD v1 not supported. */
    ret = usdhc_cmd(SD_CMD8_SEND_IF_COND, 0x1AA, 0, 0, 0, 0, 1);
    if (ret != 0) {
        wolfBoot_printf("usdhc: CMD8 failed (%d) - SD v1 card?\n", ret);
        return -1;
    }
    if ((rd(USDHC_CMD_RSP0) & 0xFF) != 0xAA) {
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
        rsp = rd(USDHC_CMD_RSP0);
        if (rsp & 0x80000000UL)
            break;
    }
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
    card_rca = (int)(rd(USDHC_CMD_RSP0) >> 16);

    /* CMD9 (CSD): unparsed, but some cards require it before select */
    (void)usdhc_cmd(SD_CMD9_SEND_CSD, (uint32_t)card_rca << 16, 1, 0, 0, 0, 0);

    /* CMD7: select the card (R1b) */
    if (usdhc_cmd(SD_CMD7_SELECT, (uint32_t)card_rca << 16, 0, 1, 0, 0, 1)
        != 0)
        return -1;

    /* Transfer clock, then 4-bit bus (ACMD6 arg 2) */
    usdhc_set_clock(0);
    if (usdhc_cmd(SD_CMD55_APP_CMD, (uint32_t)card_rca << 16, 0, 0, 0, 0, 1)
        != 0)
        return -1;
    if (usdhc_cmd(SD_ACMD6_SET_BUS_WIDTH, 2, 0, 0, 0, 0, 1) != 0)
        return -1;
    wr(USDHC_PROT_CTRL,
       (rd(USDHC_PROT_CTRL) & ~PROT_CTRL_DTW_MASK) | PROT_CTRL_DTW_4BIT);
    /* keep CDTL/CDSS asserted; RSTA is the only thing that clears them */

    /* CMD16: 512-byte blocks (no-op for high capacity, harmless) */
    (void)usdhc_cmd(SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, 0, 0, 0, 0, 1);

    wolfBoot_printf("usdhc: SD card ready, rca=0x%x %s\n",
        (unsigned)card_rca, card_high_cap ? "(high capacity)" : "");
    return 0;
}

/* Read 'blocks' full blocks starting at 'lba' into buf via PIO. */
/* Destination must be 4-byte aligned: PIO drains the FIFO as words and this
 * stage runs with the MMU off, where unaligned stores fault. disk_read()
 * routes unaligned callers through the bounce block. */
static int sd_read_blocks(uint32_t lba, uint32_t blocks, uint8_t *buf)
{
    uint32_t arg = card_high_cap ? lba : lba * SD_BLOCK_SIZE;
    uint32_t *out = (uint32_t*)(void*)buf;
    uint32_t b, w, st, n;
    int multi = (blocks > 1);
    int cmd = multi ? SD_CMD18_READ_MULTIPLE : SD_CMD17_READ_SINGLE;

    wr(USDHC_BLK_ATT, ((uint32_t)blocks << 16) | SD_BLOCK_SIZE);

    if (usdhc_cmd((uint32_t)cmd, arg, 0, 0, 1, multi, 1) != 0)
        return -1;

    for (b = 0; b < blocks; b++) {
        /* Wait for one block in the FIFO */
        for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
            st = rd(USDHC_INT_STATUS);
            if (st & INT_DATA_ERRS) {
                wr(USDHC_INT_STATUS, INT_DATA_ERRS);
                usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
                return -1;
            }
            if (rd(USDHC_PRES_STATE) & PRES_BREN)
                break;
        }
        if (n == USDHC_TIMEOUT_LOOPS) {
            usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
            return -1;
        }
        wr(USDHC_INT_STATUS, INT_BRR);
        for (w = 0; w < SD_BLOCK_SIZE / 4; w++)
            *out++ = rd(USDHC_DATA_BUFF_ACC);
    }

    /* Transfer complete (auto CMD12 covers the multi-block stop) */
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0) {
        usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
        return -1;
    }
    wr(USDHC_INT_STATUS, INT_TC);
    return 0;
}

/* --- wolfBoot disk interface --------------------------------------------- */

int disk_init(int drv)
{
    (void)drv;
    if (card_ready)
        return 0;
    if (sd_card_init() != 0)
        return -1;
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

    (void)drv;
    if (!card_ready)
        return -1;

    while (done < count) {
        lba = (uint32_t)((start + done) / SD_BLOCK_SIZE);
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
