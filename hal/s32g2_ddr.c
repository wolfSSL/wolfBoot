/* s32g2_ddr.c
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

/* LPDDR4 bring-up for the NXP S32G2: a Synopsys uMCTL2 controller with a
 * DWC LPDDR4 multiPHY. The sequence is the controller's and PHY's standard
 * one: program the controller, release it from reset, load and run the PHY
 * training firmware (1D then 2D), load the PHY init engine, then hand the
 * DFI over and enter normal operation.
 *
 * The board's register tables and the PHY training firmware are not part of
 * wolfBoot. They come from a data blob built by
 * tools/scripts/s32g2/s32g2-ddr-blob.py and linked in with S32G2_DDR_BLOB.
 * Inline ECC is not supported: the blob is generated without it. */

#include <stdint.h>
#include <stddef.h>
#include "hal/s32g2.h"
#include "hal.h"

#if defined(DEBUG_UART)
    #define PRINTF_ENABLED
#endif
#include "printf.h"

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t*)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/* uMCTL2 controller */
#define DDRC                    0x403C0000UL
#define DDRC_STAT               (DDRC + 0x004)
#define DDRC_STAT_OPMODE        0x7U
#define DDRC_MRCTRL0            (DDRC + 0x010)
#define DDRC_MRCTRL0_READ       (1U << 0)
#define DDRC_MRCTRL0_RANK0      (1U << 4)
#define DDRC_MRCTRL0_RANK_MASK  (0xFU << 4)
#define DDRC_MRCTRL0_START      (1U << 31)
#define DDRC_MRCTRL1            (DDRC + 0x014)
#define DDRC_MRSTAT             (DDRC + 0x018)
#define DDRC_MRSTAT_BUSY        (1U << 0)
#define DDRC_DERATEEN           (DDRC + 0x020)
#define DDRC_DERATEEN_EN        (1U << 0)
#define DDRC_PWRCTL             (DDRC + 0x030)
#define DDRC_PWRCTL_SELFREF_EN  (1U << 0)
#define DDRC_PWRCTL_PDN_EN      (1U << 1)
#define DDRC_PWRCTL_CLKDIS_EN   (1U << 3)
#define DDRC_PWRCTL_SELFREF_SW  (1U << 5)
#define DDRC_RFSHCTL3           (DDRC + 0x060)
#define DDRC_RFSHCTL3_DIS_AUTO  (1U << 0)
#define DDRC_RFSHCTL3_UPDATE    (1U << 1)
#define DDRC_RFSHTMG            (DDRC + 0x064)
#define DDRC_ECCCFG0            (DDRC + 0x070)
#define DDRC_DRAMTMG0           (DDRC + 0x100)
#define DDRC_DRAMTMG1           (DDRC + 0x104)
#define DDRC_DRAMTMG4           (DDRC + 0x110)
#define DDRC_DFIMISC            (DDRC + 0x1B0)
#define DDRC_DFIMISC_COMPLETE_EN (1U << 0)
#define DDRC_DFIMISC_INIT_START (1U << 5)
#define DDRC_DFISTAT            (DDRC + 0x1BC)
#define DDRC_DFISTAT_COMPLETE   (1U << 0)
#define DDRC_DBG1               (DDRC + 0x304)
#define DDRC_SWCTL              (DDRC + 0x320)
#define DDRC_SWSTAT             (DDRC + 0x324)
#define DDRC_PCTRL(n)           (DDRC + 0x490 + (n) * 0xB0)
#define DDRC_NUM_AXI_PORTS      3

/* Performance block: mode register read data, and the LPDDR4 select */
#define DDR_PERF_MRR0           0x403E0040UL
#define DDR_PERF_MRR1           0x403E0044UL

/* Subsystem glue: AXI parity and DFI1 enable; controller reset; DDR clock */
#define DDR_SS_REG              0x403D0000UL
#define DDR_SS_AXI_PARITY_EN    0x00001FF0U
#define DDR_SS_AXI_PARITY_ODD   0x01FF0000U
#define DDR_SS_DFI1_EN          0x00000001U
#define DDR_RGM_PRST0           0x40078040UL
#define DDR_RGM_PRST0_DDR       (1U << 3)
#define DDR_CGM_MUX_FIRC        0
#define DDR_CGM_MUX_PLL_PHI0    36

/* PHY: APB-only block (firmware mailbox, reset), master block, memories */
#define PHY_MUX_SEL             0x40380400UL    /* 0 = CSRs writable */
#define PHY_UCT_SHADOW          0x40380404UL
#define PHY_UCT_SHADOW_MSG      (1U << 0)       /* 0 = a message is waiting */
#define PHY_DCT_WRITE_PROT      0x4038040CUL
#define PHY_UCT_MSG             0x40380410UL
#define PHY_MICRO_RESET         0x40380420UL
#define PHY_CAL_BUSY            0x4038165CUL
#define PHY_PLL_CTRL2           0x403816DCUL
#define PHY_PLL_CTRL1           0x403816F0UL
#define PHY_PLL_TESTMODE        0x40381708UL
#define PHY_PLL_CTRL4           0x4038171CUL
#define PHY_IMEM                0x403A0000UL
#define PHY_DMEM                0x403B0000UL
#define PHY_MSG_DONE            0x07U
#define PHY_MSG_FAIL            0xFFU

#define DDR_POLL_US             1000000U
#define DDR_MAX_MESSAGES        10000U

/* Blob written by tools/scripts/s32g2/s32g2-ddr-blob.py */
#define BLOB_MAGIC              0x44323353U     /* "S32D" */
#define BLOB_VERSION            1U
#define BLOB_HDR_WORDS          4
enum {
    BLOB_DDRC, BLOB_DQ_SWAP, BLOB_PHY, BLOB_PIE,
    BLOB_IMEM_1D, BLOB_DMEM_1D, BLOB_IMEM_2D, BLOB_DMEM_2D,
    BLOB_SECTIONS
};

#ifdef S32G2_DDR_BLOB
__asm__(
    ".section .rodata.s32g2_ddr_blob, \"a\"\n"
    ".balign 8\n"
    ".global s32g2_ddr_blob\n"
    "s32g2_ddr_blob:\n"
    ".incbin \"" S32G2_DDR_BLOB "\"\n"
    ".global s32g2_ddr_blob_end\n"
    "s32g2_ddr_blob_end:\n"
    ".previous\n");
extern const uint8_t s32g2_ddr_blob[];
extern const uint8_t s32g2_ddr_blob_end[];
#endif

static const uint32_t* blob;
static int errata_polling = -1;

static int wait_bits(uintptr_t reg, uint32_t mask, uint32_t want)
{
    uint32_t us;

    for (us = 0; us < DDR_POLL_US; us++) {
        if ((rd32(reg) & mask) == want)
            return 0;
        hal_delay_us(1);
    }
    return -1;
}

static void set_bits(uintptr_t reg, uint32_t bits)
{
    wr32(reg, rd32(reg) | bits);
}

static void clr_bits(uintptr_t reg, uint32_t bits)
{
    wr32(reg, rd32(reg) & ~bits);
}

static uint32_t crc32(const uint8_t* p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU, i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int blob_open(void)
{
#ifdef S32G2_DDR_BLOB
    const uint32_t* b = (const uint32_t*)(uintptr_t)s32g2_ddr_blob;
    uint32_t avail = (uint32_t)((uintptr_t)s32g2_ddr_blob_end -
        (uintptr_t)s32g2_ddr_blob);
    uint32_t total, i, off, count, bytes;

    if (avail < (BLOB_HDR_WORDS + 2 * BLOB_SECTIONS) * 4 ||
            b[0] != BLOB_MAGIC || b[1] != BLOB_VERSION)
        return -1;
    total = b[2];
    if (total > avail || total < (BLOB_HDR_WORDS + 2 * BLOB_SECTIONS) * 4)
        return -1;
    if (crc32((const uint8_t*)b + 16, total - 16) != b[3])
        return -2;
    for (i = 0; i < BLOB_SECTIONS; i++) {
        off = b[BLOB_HDR_WORDS + 2 * i];
        count = b[BLOB_HDR_WORDS + 2 * i + 1];
        bytes = (i < BLOB_IMEM_1D) ? 8 : 2;
        if ((off & 3) != 0 || off > total || count > (total - off) / bytes)
            return -3;
    }
    blob = b;
    return 0;
#else
    return -4;
#endif
}

static const void* blob_section(int id, uint32_t* count)
{
    *count = blob[BLOB_HDR_WORDS + 2 * id + 1];
    return (const uint8_t*)blob + blob[BLOB_HDR_WORDS + 2 * id];
}

/* A table of {register address, value} writes */
static void write_table(int id)
{
    uint32_t count, i;
    const uint32_t* t = (const uint32_t*)blob_section(id, &count);

    for (i = 0; i < count; i++)
        wr32((uintptr_t)t[2 * i], t[2 * i + 1]);
}

/* 16-bit words into consecutive 32-bit PHY memory cells */
static void load_phy_mem(uintptr_t dst, int id)
{
    uint32_t count, i;
    const uint16_t* w = (const uint16_t*)blob_section(id, &count);

    wr32(PHY_MUX_SEL, 0);
    for (i = 0; i < count; i++)
        wr32(dst + 4 * i, w[i]);
    wr32(PHY_MUX_SEL, 1);
}

/* Quasi-dynamic register group: open with sw_done = 0, close with 1 */
static int swctl(uint32_t done)
{
    wr32(DDRC_SWCTL, done);
    return wait_bits(DDRC_SWSTAT, 1, done);
}

/* Bring the controller out of reset with the DDR clock parked on FIRC, as
 * the reset release needs, then quiesce it for PHY training. */
static int controller_release(void)
{
    int moved = 0;

    set_bits(DDR_SS_REG, DDR_SS_AXI_PARITY_EN);
    set_bits(DDR_SS_REG, DDR_SS_AXI_PARITY_ODD);
    set_bits(DDR_SS_REG, DDR_SS_DFI1_EN);

    if (s32g2_cgm_mux_get(S32G2_MC_CGM5_BASE, 0) != DDR_CGM_MUX_FIRC) {
        if (s32g2_cgm_mux_set(S32G2_MC_CGM5_BASE, 0, DDR_CGM_MUX_FIRC) != 0)
            return -10;
        moved = 1;
    }
    clr_bits(DDR_RGM_PRST0, DDR_RGM_PRST0_DDR);
    if (moved &&
            s32g2_cgm_mux_set(S32G2_MC_CGM5_BASE, 0, DDR_CGM_MUX_PLL_PHI0) != 0)
        return -11;

    wr32(DDRC_DBG1, 0);
    set_bits(DDRC_RFSHCTL3, DDRC_RFSHCTL3_DIS_AUTO);
    clr_bits(DDRC_PWRCTL, DDRC_PWRCTL_PDN_EN);
    clr_bits(DDRC_PWRCTL, DDRC_PWRCTL_SELFREF_EN);
    clr_bits(DDRC_PWRCTL, DDRC_PWRCTL_CLKDIS_EN);
    if (swctl(0) != 0)
        return -12;
    clr_bits(DDRC_DFIMISC, DDRC_DFIMISC_COMPLETE_EN);
    if (swctl(1) != 0)
        return -13;
    return 0;
}

/* Start the PHY microcontroller on the loaded firmware and follow its
 * mailbox until it reports the training stage done or failed. */
static int run_training(void)
{
    uint32_t n, msg;

    /* PHY PLL settings for this data rate, then reset and release */
    wr32(PHY_PLL_CTRL1, 0x21);
    wr32(PHY_PLL_TESTMODE, 0x24);
    wr32(PHY_PLL_CTRL4, 0x17F);
    wr32(PHY_PLL_CTRL2, 0x19);
    wr32(PHY_MUX_SEL, 1);
    wr32(PHY_MICRO_RESET, 0x9);
    wr32(PHY_MICRO_RESET, 0x1);
    wr32(PHY_MICRO_RESET, 0x0);

    for (n = 0; n < DDR_MAX_MESSAGES; n++) {
        if (wait_bits(PHY_UCT_SHADOW, PHY_UCT_SHADOW_MSG, 0) != 0)
            return -20;
        msg = rd32(PHY_UCT_MSG);
        /* Acknowledge, wait for the firmware to take it, re-arm */
        wr32(PHY_DCT_WRITE_PROT, 0);
        if (wait_bits(PHY_UCT_SHADOW, PHY_UCT_SHADOW_MSG,
                PHY_UCT_SHADOW_MSG) != 0)
            return -21;
        wr32(PHY_DCT_WRITE_PROT, 1);
        if (msg == PHY_MSG_DONE)
            return 0;
        if (msg == PHY_MSG_FAIL)
            return -22;
    }
    return -23;
}

static int phy_train(void)
{
    uint32_t count;
    int ret;

    write_table(BLOB_DQ_SWAP);
    write_table(BLOB_PHY);

    load_phy_mem(PHY_IMEM, BLOB_IMEM_1D);
    load_phy_mem(PHY_DMEM, BLOB_DMEM_1D);
    wolfBoot_printf("DDR: 1D training\n");
    ret = run_training();
    if (ret != 0)
        return ret;

    (void)blob_section(BLOB_IMEM_2D, &count);
    if (count != 0) {
        load_phy_mem(PHY_IMEM, BLOB_IMEM_2D);
        load_phy_mem(PHY_DMEM, BLOB_DMEM_2D);
        wolfBoot_printf("DDR: 2D training\n");
        ret = run_training();
        if (ret != 0)
            return ret - 10;
    }

    /* PHY init engine sequences, with the CSRs writable */
    wr32(PHY_MUX_SEL, 0);
    write_table(BLOB_PIE);
    wr32(PHY_MUX_SEL, 1);
    return 0;
}

/* Read an LPDDR4 mode register of rank 0 through the controller */
static int mode_reg_read(uint32_t mr, uint32_t* val)
{
    uint32_t v;

    set_bits(DDR_PERF_MRR0, 1);
    if (wait_bits(DDRC_MRSTAT, DDRC_MRSTAT_BUSY, 0) != 0)
        return -1;
    v = rd32(DDRC_MRCTRL0) & ~DDRC_MRCTRL0_RANK_MASK;
    wr32(DDRC_MRCTRL0, v | DDRC_MRCTRL0_READ | DDRC_MRCTRL0_RANK0);
    wr32(DDRC_MRCTRL1, (rd32(DDRC_MRCTRL1) & ~0xFF00U) | (mr << 8));
    __asm__ volatile("dsb sy");
    set_bits(DDRC_MRCTRL0, DDRC_MRCTRL0_START);
    if (wait_bits(DDRC_MRSTAT, DDRC_MRSTAT_BUSY, 0) != 0)
        return -1;
    *val = rd32(DDR_PERF_MRR1);
    return 0;
}

/* Add delta to a timing field, failing instead of overflowing it */
static int field_add(uintptr_t reg, uint32_t shift, uint32_t mask,
    uint32_t delta)
{
    uint32_t v = rd32(reg);
    uint32_t f = ((v >> shift) & mask) + delta;

    if (f > mask)
        return -1;
    wr32(reg, (v & ~(mask << shift)) | (f << shift));
    return 0;
}

/* NXP erratum ERR050543: the controller's temperature derating can use a
 * stale MR4 refresh-rate code. Read MR4; when the device asks for derated
 * timing (code above 3), switch automatic derating off and apply the
 * derated refresh interval and AC timings by hand; otherwise automatic
 * derating is safe to enable. An OS then has to keep polling MR4. */
static int errata_050543(void)
{
    uint32_t mr4, code, v;

    if (mode_reg_read(4, &mr4) != 0)
        return -30;
    code = mr4 & 0x7;
    if (((mr4 >> 16) & 0x7) > code)
        code = (mr4 >> 16) & 0x7;

    if (code <= 3) {
        set_bits(DDRC_DERATEEN, DDRC_DERATEEN_EN);
        errata_polling = 0;
        return 0;
    }
    clr_bits(DDRC_DERATEEN, DDRC_DERATEEN_EN);
    /* t_rfc_nom_x1_x32 to a quarter, flagged through refresh_update_level */
    v = rd32(DDRC_RFSHTMG);
    wr32(DDRC_RFSHTMG, (v & ~(0xFFFU << 16)) |
        ((((v >> 16) & 0xFFFU) >> 2) << 16));
    wr32(DDRC_RFSHCTL3, rd32(DDRC_RFSHCTL3) ^ DDRC_RFSHCTL3_UPDATE);
    if (swctl(0) != 0)
        return -31;
    if (field_add(DDRC_DRAMTMG4, 24, 0x1F, 2) != 0 ||   /* t_rcd */
            field_add(DDRC_DRAMTMG4, 8, 0xF, 2) != 0 || /* t_rrd */
            field_add(DDRC_DRAMTMG0, 0, 0x3F, 2) != 0 ||/* t_ras_min */
            field_add(DDRC_DRAMTMG4, 0, 0x1F, 2) != 0 ||/* t_rp */
            field_add(DDRC_DRAMTMG1, 0, 0x7F, 3) != 0)  /* t_rc */
        return -32;
    if (swctl(1) != 0)
        return -33;
    errata_polling = 1;
    return 0;
}

/* DFI initialisation, normal operating mode, low-power features, AXI ports */
static int controller_start(void)
{
    uint32_t n;
    int ret;

    if (wait_bits(PHY_CAL_BUSY, 1, 0) != 0)
        return -40;
    if (swctl(0) != 0)
        return -41;
    set_bits(DDRC_DFIMISC, DDRC_DFIMISC_INIT_START);
    if (swctl(1) != 0)
        return -42;
    if (wait_bits(DDRC_DFISTAT, DDRC_DFISTAT_COMPLETE,
            DDRC_DFISTAT_COMPLETE) != 0)
        return -43;
    if (swctl(0) != 0)
        return -44;
    clr_bits(DDRC_DFIMISC, DDRC_DFIMISC_INIT_START);
    set_bits(DDRC_DFIMISC, DDRC_DFIMISC_COMPLETE_EN);
    clr_bits(DDRC_PWRCTL, DDRC_PWRCTL_SELFREF_SW);
    if (swctl(1) != 0)
        return -45;

    for (n = 0; n < DDR_POLL_US; n++) {
        if ((rd32(DDRC_STAT) & DDRC_STAT_OPMODE) != 0)
            break;
        hal_delay_us(1);
    }
    if (n == DDR_POLL_US)
        return -46;
    clr_bits(DDRC_RFSHCTL3, DDRC_RFSHCTL3_DIS_AUTO);
    if ((rd32(DDRC_ECCCFG0) & 0x7) != 0)
        return -47;     /* inline ECC tables: not supported */
    set_bits(DDRC_PWRCTL, DDRC_PWRCTL_PDN_EN);
    set_bits(DDRC_PWRCTL, DDRC_PWRCTL_SELFREF_EN);
    set_bits(DDRC_PWRCTL, DDRC_PWRCTL_CLKDIS_EN);

    ret = errata_050543();
    if (ret != 0)
        return ret;
    for (n = 0; n < DDRC_NUM_AXI_PORTS; n++)
        wr32(DDRC_PCTRL(n), 1);
    return 0;
}

int s32g2_ddr_init(void)
{
    int ret;

    ret = blob_open();
    if (ret != 0) {
        wolfBoot_printf("DDR: no valid data blob (%d), build with "
            "S32G2_DDR_BLOB\n", ret);
        return ret;
    }
    write_table(BLOB_DDRC);
    ret = controller_release();
    if (ret == 0)
        ret = phy_train();
    if (ret == 0)
        ret = controller_start();
    return ret;
}

/* 1 when ERR050543 left derating to be handled by polling MR4, 0 when
 * automatic derating is on, -1 before s32g2_ddr_init() succeeded. */
int s32g2_ddr_polling_needed(void)
{
    return errata_polling;
}
