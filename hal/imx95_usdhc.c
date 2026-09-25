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
#ifdef IMX95_EMMC_PROBE
#include "hal/imx95_ahab.h"
#endif

#if defined(IMX95_SCMI_COLD_INIT) && defined(DISK_SDCARD)
extern int imx95_usdhc2_cold_init(void);
#endif

#if defined(DISK_SDCARD) || defined(DISK_EMMC)

/* uSDHC1 carries the eMMC on this module, uSDHC2 the carrier SD slot. A build
 * selects one: the two are separate controllers and the driver keeps a single
 * card's state. */
#ifndef USDHC_BASE
#ifdef DISK_EMMC
#define USDHC_BASE          IMX95_USDHC1_BASE   /* eMMC (uSDHC1) */
#else
#define USDHC_BASE          IMX95_USDHC2_BASE   /* carrier SD (uSDHC2) */
#endif
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
#define USDHC_DLL_CTRL          0x60
#define USDHC_CLK_TUNE_CTRL     0x68    /* CLK_TUNE_CTRL_STATUS */
#define USDHC_VEND_SPEC         0xC0
#define USDHC_MMC_BOOT          0xC4
/* VEND_SPEC reset value: clock gates on, 3.3V signaling. */
#define VEND_SPEC_INIT          0x20007809UL
#define VEND_SPEC_FRC_SDCLK_ON  (1UL << 8)
#define VEND_SPEC_IPGEN         (1UL << 11)   /* IPG clock always on */
#define VEND_SPEC_HCKEN         (1UL << 12)   /* AHB clock always on */
#define VEND_SPEC_PEREN         (1UL << 13)   /* peripheral clock on */
#define VEND_SPEC_CKEN          (1UL << 14)   /* SD clock on */

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
#define SYS_CTRL_INITA          (1UL << 27)   /* send 80 init clocks */
#define SYS_CTRL_RSTA           (1UL << 24)   /* reset all */
#define SYS_CTRL_RSTC           (1UL << 25)   /* reset cmd */
#define SYS_CTRL_RSTD           (1UL << 26)   /* reset data */
#define SYS_CTRL_RSTT           (1UL << 28)   /* reset tuning */

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
#define SD_CMD6_SWITCH          6
#define MMC_CMD1_SEND_OP_COND   1
#define MMC_CMD8_SEND_EXT_CSD   8
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

/* Build with -DIMX95_SCMI_DEBUG for a per-step trace of the SD bring-up. */
#ifdef IMX95_SCMI_DEBUG
#define DISK_DBG(...) wolfBoot_printf(__VA_ARGS__)
#else
#define DISK_DBG(...) do { } while (0)
#endif

static int card_rca;        /* relative card address, from CMD3 */
static int card_high_cap;   /* SDHC/SDXC: block addressing */
static int card_ready;

/* Which controller the calls below talk to. A variable rather than the macro
 * so one image can reach both instances - the eMMC on uSDHC1 and the carrier
 * SD on uSDHC2 are the same IP at different addresses. */
static uintptr_t usdhc_base = USDHC_BASE;

static inline uint32_t rd(uint32_t off)
{
    return *(volatile uint32_t*)(usdhc_base + off);
}

static inline void wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t*)(usdhc_base + off) = v;
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
 * 400 MHz module clock default: /256/4 = ~390 kHz identification, /2/8 =
 * 25 MHz default speed, /2/4 = 50 MHz once the card is in high-speed mode. */
#define USDHC_CLK_ID    0
#define USDHC_CLK_25MHZ 1
#define USDHC_CLK_50MHZ 2

/* RSTA does not touch the vendor registers, so a controller the boot ROM has
 * already driven comes back still in its fast-boot mode, with the HS400 DLL
 * and tuning it selected. The first command then never completes. U-Boot's
 * esdhc_init() puts the same five registers back by hand for this reason; a
 * stage that runs after U-Boot inherits them already clean, which is why this
 * only shows up when nothing ran first. */
/* The 80 startup clocks a card needs before its first command. The SD clock
 * is normally gated when idle, so it is forced on across the sequence the way
 * U-Boot's esdhc_init() does - INITA on its own is not enough on a controller
 * the boot ROM has already used. */
static void usdhc_init_clocks(void)
{
    volatile uint32_t d;

    wr(USDHC_VEND_SPEC, rd(USDHC_VEND_SPEC) | VEND_SPEC_FRC_SDCLK_ON);
    wr(USDHC_SYS_CTRL, rd(USDHC_SYS_CTRL) | SYS_CTRL_INITA);
    (void)usdhc_wait_clear(USDHC_SYS_CTRL, SYS_CTRL_INITA);
    /* Caches are off in this stage, so a plain loop is well over the 1 ms
     * U-Boot waits here. */
    for (d = 0; d < 200000U; d++) { }
    wr(USDHC_VEND_SPEC, rd(USDHC_VEND_SPEC) & ~VEND_SPEC_FRC_SDCLK_ON);
}

static void usdhc_vendor_reset(void)
{
    wr(USDHC_MMC_BOOT, 0);
    wr(USDHC_MIX_CTRL, 0);
    wr(USDHC_CLK_TUNE_CTRL, 0);
    wr(USDHC_DLL_CTRL, 0);
    wr(USDHC_VEND_SPEC, VEND_SPEC_INIT);
}

static void usdhc_set_clock(int speed)
{
    uint32_t v;

    v = rd(USDHC_SYS_CTRL);
    v &= ~((0xFFUL << SYS_CTRL_SDCLKFS_SHIFT) | (0xFUL << SYS_CTRL_DVS_SHIFT)
           | (0xFUL << SYS_CTRL_DTOCV_SHIFT));
    if (speed == USDHC_CLK_ID) {
        v |= (0x80UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /256 */
        v |= (0x3UL << SYS_CTRL_DVS_SHIFT);       /* /4  -> ~390 kHz */
    }
    else if (speed == USDHC_CLK_50MHZ) {
        v |= (0x01UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /2 */
        v |= (0x3UL << SYS_CTRL_DVS_SHIFT);       /* /4  -> 50 MHz */
    }
    else {
        v |= (0x01UL << SYS_CTRL_SDCLKFS_SHIFT);  /* /2 */
        v |= (0x7UL << SYS_CTRL_DVS_SHIFT);       /* /8  -> 25 MHz */
    }
    v |= (0xEUL << SYS_CTRL_DTOCV_SHIFT);         /* max data timeout */
    wr(USDHC_SYS_CTRL, v);
    /* Enable the IPG/AHB/peripheral/SD clock gates: after a cold reset these
     * are off, so SDSTB never sets and no command can run. (A warm handoff
     * from a prior stage leaves them on; setting them again is harmless.) */
    wr(USDHC_VEND_SPEC, rd(USDHC_VEND_SPEC) |
        VEND_SPEC_IPGEN | VEND_SPEC_HCKEN | VEND_SPEC_PEREN | VEND_SPEC_CKEN);
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
    else if (idx == SD_CMD0_GO_IDLE)
        xfr |= CMD_XFR_RSPTYP_NONE;   /* CMD0 has no response */
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

/* CMD6 SWITCH_FUNC, mode 1 (set), function group 1 = 1 (high speed). The card
 * answers with a 512-bit status; bits 379:376 hold the function it actually
 * selected for group 1, which is the low nibble of the 17th byte received.
 * Returns 0 only when the card confirms high speed. */
static int sd_switch_high_speed(void)
{
    uint32_t sw[16];
    const uint8_t *b = (const uint8_t *)sw;
    uint32_t n, st, i;

    wr(USDHC_BLK_ATT, (1UL << 16) | 64UL);
    wr(USDHC_WTMK_LVL, (16UL << 16) | 16UL);

    if (usdhc_cmd(SD_CMD6_SWITCH, 0x80FFFFF1UL, 0, 0, 1, 0, 1) != 0)
        goto restore;

    for (n = 0; n < USDHC_TIMEOUT_LOOPS; n++) {
        st = rd(USDHC_INT_STATUS);
        if (st & INT_DATA_ERRS) {
            wr(USDHC_INT_STATUS, INT_DATA_ERRS);
            goto restore;
        }
        if (rd(USDHC_PRES_STATE) & PRES_BREN)
            break;
    }
    if (n == USDHC_TIMEOUT_LOOPS)
        goto restore;
    wr(USDHC_INT_STATUS, INT_BRR);
    for (i = 0; i < 16; i++)
        sw[i] = rd(USDHC_DATA_BUFF_ACC);
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0)
        goto restore;
    wr(USDHC_INT_STATUS, INT_TC);

    /* Restore the block geometry the read path expects. */
    wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    return ((b[16] & 0x0FU) == 1U) ? 0 : -1;

restore:
    usdhc_reset(SYS_CTRL_RSTC | SYS_CTRL_RSTD);
    wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    return -1;
}

#if defined(DISK_EMMC) || defined(IMX95_EMMC_PROBE)

/* EXT_CSD byte 179. Bits [2:0] select which partition ordinary read commands
 * address; bits [5:3] select which one the boot ROM loads from. Only the
 * access bits may be touched here - rewriting the enable bits would change
 * where the SoC boots from next reset. */
#define EXT_CSD_PARTITION_CONFIG    179
#define EXT_CSD_BUS_WIDTH           183
#define EXT_CSD_PART_ACCESS_MASK    0x07U
#define EXT_CSD_BUS_WIDTH_4BIT      1
#define MMC_SWITCH_WRITE_BYTE       (3UL << 24)

static uint8_t emmc_part_config;    /* EXT_CSD[179] as read at init */

/* CMD6 in the "write byte" form: index in [23:16], value in [15:8]. R1b, so
 * the card holds DAT0 low while it applies the change. */
static int emmc_switch(uint32_t index, uint32_t value)
{
    uint32_t arg = MMC_SWITCH_WRITE_BYTE | (index << 16) | (value << 8);

    return usdhc_cmd(SD_CMD6_SWITCH, arg, 0, 1, 0, 0, 1);
}

/* CMD8 on eMMC returns the 512-byte EXT_CSD as a data block, unlike the SD
 * CMD8 which is an interface-condition check with no data. */
static int emmc_read_ext_csd(uint8_t *buf)
{
    uint32_t n, st, i;
    uint32_t *out = (uint32_t *)(void *)buf;

    wr(USDHC_BLK_ATT, (1UL << 16) | SD_BLOCK_SIZE);
    if (usdhc_cmd(MMC_CMD8_SEND_EXT_CSD, 0, 0, 0, 1, 0, 1) != 0)
        return -1;

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
    if (n == USDHC_TIMEOUT_LOOPS)
        return -1;
    wr(USDHC_INT_STATUS, INT_BRR);
    for (i = 0; i < SD_BLOCK_SIZE / 4; i++)
        out[i] = rd(USDHC_DATA_BUFF_ACC);
    if (usdhc_wait_set(USDHC_INT_STATUS, INT_TC) != 0)
        return -1;
    wr(USDHC_INT_STATUS, INT_TC);
    return 0;
}

/* Point ordinary reads at the user area (0), boot0 (1) or boot1 (2). The boot
 * partitions are where the SoC's own boot containers live, so this is what
 * lets wolfBoot read them. */
int imx95_emmc_select_partition(int part)
{
    uint8_t cfg;

    if (!card_ready)
        return -1;
    if (part < 0 || part > 2)
        return -1;

    cfg = (uint8_t)((emmc_part_config & (uint8_t)~EXT_CSD_PART_ACCESS_MASK) |
                    (uint8_t)part);
    if (emmc_switch(EXT_CSD_PARTITION_CONFIG, cfg) != 0) {
        wolfBoot_printf("emmc: partition switch to %d failed\n", part);
        return -1;
    }
    emmc_part_config = cfg;
    return 0;
}

/* Which boot partition the SoC loads from, 1 for boot0 or 2 for boot1, taken
 * from the enable field the ROM itself reads. 0 means none is enabled. */
int imx95_emmc_boot_partition(void)
{
    if (!card_ready)
        return -1;
    return (int)((emmc_part_config >> 3) & 0x07U);
}

static int emmc_card_init(void)
{
    static uint8_t ext_csd[SD_BLOCK_SIZE] __attribute__((aligned(4)));
    uint32_t rsp;
    uint32_t n;
    int ret;

    card_rca = 1;           /* the host assigns it on eMMC, unlike SD */
    card_high_cap = 0;

    usdhc_reset(SYS_CTRL_RSTA | SYS_CTRL_RSTT);
    usdhc_vendor_reset();
    wr(USDHC_PROT_CTRL, PROT_CTRL_EMODE_LE | PROT_CTRL_DTW_1BIT |
                        PROT_CTRL_CDTL | PROT_CTRL_CDSS);
    wr(USDHC_WTMK_LVL, (128UL << 16) | 128UL);
    wr(USDHC_INT_STATUS_EN, 0xFFFFFFFFUL);
    wr(USDHC_INT_SIGNAL_EN, 0);

    usdhc_set_clock(USDHC_CLK_ID);
    usdhc_init_clocks();

    DISK_DBG("emmc: SYS_CTRL=%08x PRES=%08x VEND=%08x\n",
        (unsigned)rd(USDHC_SYS_CTRL), (unsigned)rd(USDHC_PRES_STATE),
        (unsigned)rd(USDHC_VEND_SPEC));

    (void)usdhc_cmd(SD_CMD0_GO_IDLE, 0, 0, 0, 0, 0, 0);

    /* CMD1 rather than ACMD41, and with the sector-address bit set: parts this
     * size are always block addressed, and asking for byte addressing would be
     * refused. R3 carries no CRC. */
    rsp = 0;
    for (n = 0; n < 1000; n++) {
        ret = usdhc_cmd(MMC_CMD1_SEND_OP_COND, 0x40FF8080UL, 0, 0, 0, 0, 0);
        if (ret != 0) {
            wolfBoot_printf("emmc: CMD1 failed (%d) PRES=%08x INT=%08x\n",
                ret, (unsigned)rd(USDHC_PRES_STATE),
                (unsigned)rd(USDHC_INT_STATUS));
            return -1;
        }
        rsp = rd(USDHC_CMD_RSP0);
        if (rsp & 0x80000000UL)
            break;
    }
    if (!(rsp & 0x80000000UL)) {
        wolfBoot_printf("emmc: card stuck busy in CMD1 (OCR 0x%08x)\n",
            (unsigned)rsp);
        return -1;
    }
    card_high_cap = (rsp & 0x40000000UL) ? 1 : 0;

    ret = usdhc_cmd(SD_CMD2_ALL_SEND_CID, 0, 1, 0, 0, 0, 0);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD2 failed (%d)\n", ret);
        return -1;
    }
    /* The host chooses the address on eMMC and tells the card. */
    ret = usdhc_cmd(SD_CMD3_SEND_REL_ADDR, (uint32_t)card_rca << 16,
                    0, 0, 0, 0, 1);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD3 failed (%d)\n", ret);
        return -1;
    }
    (void)usdhc_cmd(SD_CMD9_SEND_CSD, (uint32_t)card_rca << 16, 1, 0, 0, 0, 0);
    ret = usdhc_cmd(SD_CMD7_SELECT, (uint32_t)card_rca << 16, 0, 1, 0, 0, 1);
    if (ret != 0) {
        wolfBoot_printf("emmc: CMD7 failed (%d)\n", ret);
        return -1;
    }

    usdhc_set_clock(USDHC_CLK_25MHZ);

    /* 4-bit: the SMARC carrier routes four eMMC data lines, and the wider bus
     * is a device-side setting the card has to be told about. */
    if (emmc_switch(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4BIT) == 0) {
        wr(USDHC_PROT_CTRL,
           (rd(USDHC_PROT_CTRL) & ~PROT_CTRL_DTW_MASK) | PROT_CTRL_DTW_4BIT);
    }

    (void)usdhc_cmd(SD_CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, 0, 0, 0, 0, 1);

    if (emmc_read_ext_csd(ext_csd) != 0) {
        wolfBoot_printf("emmc: EXT_CSD read failed\n");
        return -1;
    }
    emmc_part_config = ext_csd[EXT_CSD_PARTITION_CONFIG];

    wolfBoot_printf("emmc: ready, rca=0x%x part_config=0x%02x\n",
        (unsigned)card_rca, (unsigned)emmc_part_config);
    return 0;
}

#endif /* DISK_EMMC || IMX95_EMMC_PROBE */

static int sd_card_init(void)
{
    uint32_t rsp;
    uint32_t n;
    int ret;

    card_rca = 0;
    card_high_cap = 0;

    usdhc_reset(SYS_CTRL_RSTA | SYS_CTRL_RSTT);
    usdhc_vendor_reset();

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

    usdhc_set_clock(USDHC_CLK_ID);

    DISK_DBG("usdhc: after clk SYS_CTRL=%08x PRES=%08x (SDSTB=%d CINST=%d) CAP=%08x\n",
        (unsigned)rd(USDHC_SYS_CTRL), (unsigned)rd(USDHC_PRES_STATE),
        (int)((rd(USDHC_PRES_STATE) >> 3) & 1U),
        (int)((rd(USDHC_PRES_STATE) >> 16) & 1U),
        (unsigned)rd(USDHC_HOST_CTRL_CAP));

    /* Emit the 80 startup clocks a cold card needs before CMD0/CMD8. */
    usdhc_init_clocks();

    /* CMD0: idle */
    (void)usdhc_cmd(SD_CMD0_GO_IDLE, 0, 0, 0, 0, 0, 0);

    /* CMD8 voltage check (2.7-3.6V, pattern 0xAA). SD v1 not supported. */
    ret = usdhc_cmd(SD_CMD8_SEND_IF_COND, 0x1AA, 0, 0, 0, 0, 1);
    DISK_DBG("usdhc: CMD8 -> %d RSP0=%08x\n", ret, (unsigned)rd(USDHC_CMD_RSP0));
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
    DISK_DBG("usdhc: ACMD41 done after %u loops, OCR=%08x\n",
        (unsigned)n, (unsigned)rsp);
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
    usdhc_set_clock(USDHC_CLK_25MHZ);
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

    /* High speed doubles the bus ceiling from 25 to 50 MHz, which is most of
     * the read time on an image of any size. CMD6 returns a 64-byte status;
     * only switch the controller after the card confirms it took the mode,
     * because driving 50 MHz at a card still in default speed corrupts data
     * silently. A card that declines simply stays at 25 MHz. */
    if (sd_switch_high_speed() == 0) {
        usdhc_set_clock(USDHC_CLK_50MHZ);
        wolfBoot_printf("usdhc: high speed (50 MHz)\n");
    }

    wolfBoot_printf("usdhc: SD card ready, rca=0x%x %s\n",
        (unsigned)card_rca, card_high_cap ? "(high capacity)" : "");
    return 0;
}

/* Read 'blocks' full blocks starting at 'lba' into buf via PIO. */
/* Destination must be 4-byte aligned: PIO drains the FIFO as words and this
 * stage is built -mstrict-align, where unaligned stores fault. disk_read()
 * routes unaligned callers through the bounce block. */
static int sd_read_blocks(uint32_t lba, uint32_t blocks, uint8_t *buf)
{
    uint32_t arg;
    uint32_t *out = (uint32_t*)(void*)buf;
    uint32_t b, w, st, n;
    int multi = (blocks > 1);
    int cmd = multi ? SD_CMD18_READ_MULTIPLE : SD_CMD17_READ_SINGLE;

    /* A standard-capacity card takes a byte address, so its last addressable
     * block is the one whose byte offset still fits the 32-bit argument.
     * Refuse rather than wrap into an unrelated sector. */
    if (!card_high_cap) {
        if (lba > (0xFFFFFFFFUL / SD_BLOCK_SIZE))
            return -1;
        arg = lba * SD_BLOCK_SIZE;
    }
    else {
        arg = lba;
    }

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

#ifdef IMX95_COLD_PROBE
/* Cold-state prober: report exactly what a cold uSDHC2 needs, one marker
 * line before each access so a TRDC abort (caught by the debug vectors)
 * names the blocked register. Debug builds only. */
#define IOMUXC_BASE   0x443C0000UL
#define RGPIO3_BASE   0x43820000UL   /* PDOR 0x40 PSOR 0x44 PDDR 0x54 */

static void probe_delay(uint32_t loops)
{
    volatile uint32_t n = loops;
    while (n-- > 0U) { __asm__ volatile("nop"); }
}

void imx95_usdhc_cold_probe(void)
{
    static const uint32_t mux_off[6] =
        { 0x1a4, 0x1a8, 0x1ac, 0x1b0, 0x1b4, 0x1b8 };
    static const uint32_t cfg_off[6] =
        { 0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc };
    uint32_t i, v;

    wolfBoot_printf("probe: SYS_CTRL=%08x PRES=%08x CAP=%08x\n",
        (unsigned)rd(USDHC_SYS_CTRL), (unsigned)rd(USDHC_PRES_STATE),
        (unsigned)rd(USDHC_HOST_CTRL_CAP));
    usdhc_reset(SYS_CTRL_RSTA);
    usdhc_set_clock(USDHC_CLK_ID);
    wolfBoot_printf("probe: after clk PRES=%08x (SDSTB=%d)\n",
        (unsigned)rd(USDHC_PRES_STATE),
        (int)((rd(USDHC_PRES_STATE) >> 3) & 1U));

    wolfBoot_printf("probe: reading IOMUXC\n");
    for (i = 0; i < 6U; i++) {
        v = *(volatile uint32_t*)(IOMUXC_BASE + mux_off[i]);
        wolfBoot_printf("probe: mux[%08x]=%08x cfg=%08x\n",
            (unsigned)mux_off[i], (unsigned)v,
            (unsigned)*(volatile uint32_t*)(IOMUXC_BASE + cfg_off[i]));
    }

    wolfBoot_printf("probe: writing IOMUXC (mode0, conf 0x138e/0x158e)\n");
    for (i = 0; i < 6U; i++) {
        *(volatile uint32_t*)(IOMUXC_BASE + mux_off[i]) = 0U;
        *(volatile uint32_t*)(IOMUXC_BASE + cfg_off[i]) =
            (i == 0U) ? 0x158eU : 0x138eU;   /* CLK gets the stronger conf */
    }
    wolfBoot_printf("probe: mux write ok, readback mux[0]=%08x\n",
        (unsigned)*(volatile uint32_t*)(IOMUXC_BASE + 0x1a4));

    wolfBoot_printf("probe: reading GPIO3\n");
    wolfBoot_printf("probe: PDOR=%08x PDDR=%08x\n",
        (unsigned)*(volatile uint32_t*)(RGPIO3_BASE + 0x40),
        (unsigned)*(volatile uint32_t*)(RGPIO3_BASE + 0x54));
    wolfBoot_printf("probe: card power on (GPIO3.7)\n");
    *(volatile uint32_t*)(RGPIO3_BASE + 0x54) |= (1U << 7);  /* PDDR out */
    *(volatile uint32_t*)(RGPIO3_BASE + 0x44)  = (1U << 7);  /* PSOR high */
    probe_delay(20000000U);  /* > startup-delay-us at ~1.8 GHz */

    wolfBoot_printf("probe: retry enumeration\n");
    card_ready = 0;
    if (sd_card_init() == 0) {
        wolfBoot_printf("probe: SD ENUMERATED after cold init\n");
        card_ready = 1;
    }
    else {
        wolfBoot_printf("probe: still failing after pinmux+power\n");
    }
}
#endif /* IMX95_COLD_PROBE */

#ifdef IMX95_EMMC_PROBE
/* Block-granular read of the currently selected eMMC partition, in the shape
 * the container parser asks for. */
static int emmc_ahab_read(void *ctx, uint32_t off, uint32_t len, void *buf)
{
    (void)ctx;
    if ((off % SD_BLOCK_SIZE) != 0U || (len % SD_BLOCK_SIZE) != 0U)
        return -1;
    return sd_read_blocks(off / SD_BLOCK_SIZE, len / SD_BLOCK_SIZE,
                          (uint8_t *)buf);
}

/* Read the SoC's own boot containers out of an eMMC boot partition and report
 * what is there. This is how the eMMC path is proven before anything depends
 * on it: it runs inside an SD-booting image, against the other controller, and
 * restores the SD selection afterwards, so a failure here cannot disturb the
 * boot in progress. Nothing is loaded - only the offsets are walked. */
void imx95_emmc_probe(void)
{
    static struct imx95_ahab_container ctnr;
    uintptr_t saved_base = usdhc_base;
    int saved_ready = card_ready;
    int saved_rca = card_rca;
    int saved_cap = card_high_cap;
    uint32_t off, i;
    int part, n;

    usdhc_base = IMX95_USDHC1_BASE;
    card_ready = 0;

    if (emmc_card_init() != 0) {
        wolfBoot_printf("emmc probe: init failed\n");
        goto restore;
    }
    card_ready = 1;

    for (part = 1; part <= 2; part++) {
        if (imx95_emmc_select_partition(part) != 0)
            continue;
        off = IMX95_AHAB_MMC_OFFSET;
        for (n = 0; n < 2; n++) {
            if (imx95_ahab_parse(&ctnr, off, emmc_ahab_read, NULL) != 0) {
                wolfBoot_printf("emmc probe: boot%d no container at 0x%x\n",
                    part - 1, (unsigned)off);
                break;
            }
            wolfBoot_printf("emmc probe: boot%d ctnr%d @0x%x size=0x%x images=%u\n",
                part - 1, n, (unsigned)ctnr.base, (unsigned)ctnr.size,
                (unsigned)ctnr.count);
            for (i = 0; i < ctnr.count; i++) {
                wolfBoot_printf("  img%u +0x%x size=0x%x dst=0x%x core=%u\n",
                    (unsigned)i, (unsigned)ctnr.img[i].offset,
                    (unsigned)ctnr.img[i].size,
                    (unsigned)ctnr.img[i].dst,
                    (unsigned)IMX95_AHAB_CORE(ctnr.img[i].flags));
            }
            off = imx95_ahab_next(&ctnr);
        }
    }
    (void)imx95_emmc_select_partition(0);

restore:
    usdhc_base = saved_base;
    card_ready = saved_ready;
    card_rca = saved_rca;
    card_high_cap = saved_cap;
}
#endif /* IMX95_EMMC_PROBE */

int disk_init(int drv)
{
    (void)drv;
    if (card_ready)
        return 0;
#if defined(IMX95_SCMI_COLD_INIT) && defined(DISK_SDCARD)
    /* Brings up uSDHC2's clock, pads and card power. uSDHC1 (eMMC) would need
     * its own equivalent to run from a cold power-on; today an eMMC build
     * relies on the stage ahead of wolfBoot having initialized it. */
    if (imx95_usdhc2_cold_init() != 0)
        return -1;
#endif
#ifdef IMX95_COLD_PROBE
    imx95_usdhc_cold_probe();
    if (card_ready)
        return 0;
#endif
#ifdef IMX95_EMMC_PROBE
    imx95_emmc_probe();
#endif
#ifdef DISK_EMMC
    if (emmc_card_init() != 0)
        return -1;
#else
    if (sd_card_init() != 0)
        return -1;
#endif
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
    uint64_t lba64;

    (void)drv;
    if (!card_ready)
        return -1;

    while (done < count) {
        /* The card command argument is 32-bit. A partition table that puts an
         * image past that limit must fail the read, not silently wrap round to
         * an in-range sector and hand back the wrong bytes. */
        lba64 = (start + done) / SD_BLOCK_SIZE;
        if (lba64 > 0xFFFFFFFFULL)
            return -1;
        lba = (uint32_t)lba64;
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
