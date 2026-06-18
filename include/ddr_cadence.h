/* ddr_cadence.h
 *
 * Generic Cadence DDR controller driver interface (controller register
 * programming, the Memory Test Controller engine, and the LPDDR4 mode-
 * register write protocol).  The PHY, PLL, clock and training are SoC
 * specific and stay in the platform HAL, which composes these calls.
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
#ifndef DDR_CADENCE_H
#define DDR_CADENCE_H

#include <stdint.h>

/* Controller CSR window.  Default is the PolarFire SoC DDRC APB base; a
 * different Cadence-based SoC can override it at build time. */
#ifndef DDR_CADENCE_CTRL_BASE
#define DDR_CADENCE_CTRL_BASE       0x20080000UL
#endif
#define DDRC_REG(off)   (*(volatile uint32_t*)(DDR_CADENCE_CTRL_BASE + (off)))

static inline void ddrc_mb(void)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* MTC address mode and timeout return code (the data/address pattern
 * selectors are passed through opaquely). */
#define DDR_CADENCE_MTC_ADD_RANDOM      0x01U
#define DDR_CADENCE_MTC_TIMEOUT_ERROR   0x02U

/* DDR Controller Register Offsets
 *
 * From HSS mss_ddr_sgmii_regs.h:
 *   MC_BASE2  @ DDRCFG_BASE + 0x4000  = 0x20084000 (controller registers)
 *   DFI_BASE  @ DDRCFG_BASE + 0x10000 = 0x20090000 (DFI interface)
 */

/* MC_BASE2 registers (DDRCFG_BASE + 0x4000)
 *
 * BUG FIX (Phase 3.6): the previous offsets in this block were taken from
 * an incomplete/incorrect mapping and many were WRONG vs the actual IP
 * layout, so setup_controller has been writing values to scrambled
 * register addresses for some time.  Examples of the wrong offsets:
 *   MC_CFG_CL was 0x74 (actually CFG_XP)
 *   MC_CFG_STARTUP_DELAY was 0x80 (actually CFG_CL)
 *   MC_INIT_ZQ_CAL_START was 0xDC (actually CFG_MEM_BANKBITS)
 *   MC_CFG_AUTO_ZQ_CAL_EN was 0xE0 (actually CFG_ODT_RD_MAP_CS0)
 *
 * The offsets below are verified against the HSS register struct
 * `DDR_CSR_APB_MC_BASE2_TypeDef` in mss_ddr_sgmii_regs.h.
 */
#define MC_BASE2                    0x4000

/* MC_BASE2 controller register offsets (verified against HSS
 * mss_ddr_sgmii_regs.h DDR_CSR_APB_MC_BASE2_TypeDef). */
#define MC_CTRLR_SOFT_RESET_N                      (MC_BASE2 + 0x0)
#define MC_CFG_LOOKAHEAD_PCH                       (MC_BASE2 + 0x8)
#define MC_CFG_LOOKAHEAD_ACT                       (MC_BASE2 + 0xc)
#define MC_INIT_AUTOINIT_DISABLE                   (MC_BASE2 + 0x10)
#define MC_INIT_FORCE_RESET                        (MC_BASE2 + 0x14)
#define MC_INIT_GEARDOWN_EN                        (MC_BASE2 + 0x18)
#define MC_INIT_DISABLE_CKE                        (MC_BASE2 + 0x1c)
#define MC_INIT_CS                                 (MC_BASE2 + 0x20)
#define MC_INIT_PRECHARGE_ALL                      (MC_BASE2 + 0x24)
#define MC_INIT_REFRESH                            (MC_BASE2 + 0x28)
#define MC_INIT_ZQ_CAL_REQ                         (MC_BASE2 + 0x2c)
#define MC_INIT_ACK                                (MC_BASE2 + 0x30)
#define MC_CFG_BL                                  (MC_BASE2 + 0x34)
#define MC_CTRLR_INIT                              (MC_BASE2 + 0x38)
#define MC_CTRLR_INIT_DONE                         (MC_BASE2 + 0x3c)
#define MC_CFG_AUTO_REF_EN                         (MC_BASE2 + 0x40)
#define MC_CFG_RAS                                 (MC_BASE2 + 0x44)
#define MC_CFG_RCD                                 (MC_BASE2 + 0x48)
#define MC_CFG_RRD                                 (MC_BASE2 + 0x4c)
#define MC_CFG_RP                                  (MC_BASE2 + 0x50)
#define MC_CFG_RC                                  (MC_BASE2 + 0x54)
#define MC_CFG_FAW                                 (MC_BASE2 + 0x58)
#define MC_CFG_RFC                                 (MC_BASE2 + 0x5c)
#define MC_CFG_RTP                                 (MC_BASE2 + 0x60)
#define MC_CFG_WR                                  (MC_BASE2 + 0x64)
#define MC_CFG_WTR                                 (MC_BASE2 + 0x68)
#define MC_CFG_PASR                                (MC_BASE2 + 0x70)
#define MC_CFG_XP                                  (MC_BASE2 + 0x74)
#define MC_CFG_XSR                                 (MC_BASE2 + 0x78)
#define MC_CFG_CL                                  (MC_BASE2 + 0x80)
#define MC_CFG_READ_TO_WRITE                       (MC_BASE2 + 0x88)
#define MC_CFG_WRITE_TO_WRITE                      (MC_BASE2 + 0x8c)
#define MC_CFG_READ_TO_READ                        (MC_BASE2 + 0x90)
#define MC_CFG_WRITE_TO_READ                       (MC_BASE2 + 0x94)
#define MC_CFG_READ_TO_WRITE_ODT                   (MC_BASE2 + 0x98)
#define MC_CFG_WRITE_TO_WRITE_ODT                  (MC_BASE2 + 0x9c)
#define MC_CFG_READ_TO_READ_ODT                    (MC_BASE2 + 0xa0)
#define MC_CFG_WRITE_TO_READ_ODT                   (MC_BASE2 + 0xa4)
#define MC_CFG_MIN_READ_IDLE                       (MC_BASE2 + 0xa8)
#define MC_CFG_MRD                                 (MC_BASE2 + 0xac)
#define MC_CFG_BT                                  (MC_BASE2 + 0xb0)
#define MC_CFG_DS                                  (MC_BASE2 + 0xb4)
#define MC_CFG_QOFF                                (MC_BASE2 + 0xb8)
#define MC_CFG_RTT                                 (MC_BASE2 + 0xc4)
#define MC_CFG_DLL_DISABLE                         (MC_BASE2 + 0xc8)
#define MC_CFG_REF_PER                             (MC_BASE2 + 0xcc)
#define MC_CFG_STARTUP_DELAY                       (MC_BASE2 + 0xd0)
#define MC_CFG_MEM_COLBITS                         (MC_BASE2 + 0xd4)
#define MC_CFG_MEM_ROWBITS                         (MC_BASE2 + 0xd8)
#define MC_CFG_MEM_BANKBITS                        (MC_BASE2 + 0xdc)
#define MC_CFG_ODT_RD_MAP_CS0                      (MC_BASE2 + 0xe0)
#define MC_CFG_ODT_RD_MAP_CS1                      (MC_BASE2 + 0xe4)
#define MC_CFG_ODT_RD_MAP_CS2                      (MC_BASE2 + 0xe8)
#define MC_CFG_ODT_RD_MAP_CS3                      (MC_BASE2 + 0xec)
#define MC_CFG_ODT_RD_MAP_CS4                      (MC_BASE2 + 0xf0)
#define MC_CFG_ODT_RD_MAP_CS5                      (MC_BASE2 + 0xf4)
#define MC_CFG_ODT_RD_MAP_CS6                      (MC_BASE2 + 0xf8)
#define MC_CFG_ODT_RD_MAP_CS7                      (MC_BASE2 + 0xfc)
#define MC_CFG_ODT_WR_MAP_CS0                      (MC_BASE2 + 0x120)
#define MC_CFG_ODT_WR_MAP_CS1                      (MC_BASE2 + 0x124)
#define MC_CFG_ODT_WR_MAP_CS2                      (MC_BASE2 + 0x128)
#define MC_CFG_ODT_WR_MAP_CS3                      (MC_BASE2 + 0x12c)
#define MC_CFG_ODT_WR_MAP_CS4                      (MC_BASE2 + 0x130)
#define MC_CFG_ODT_WR_MAP_CS5                      (MC_BASE2 + 0x134)
#define MC_CFG_ODT_WR_MAP_CS6                      (MC_BASE2 + 0x138)
#define MC_CFG_ODT_WR_MAP_CS7                      (MC_BASE2 + 0x13c)
#define MC_CFG_ODT_RD_TURN_ON                      (MC_BASE2 + 0x160)
#define MC_CFG_ODT_WR_TURN_ON                      (MC_BASE2 + 0x164)
#define MC_CFG_ODT_RD_TURN_OFF                     (MC_BASE2 + 0x168)
#define MC_CFG_ODT_WR_TURN_OFF                     (MC_BASE2 + 0x16c)
#define MC_CFG_EMR3                                (MC_BASE2 + 0x178)
#define MC_CFG_TWO_T                               (MC_BASE2 + 0x17c)
#define MC_CFG_TWO_T_SEL_CYCLE                     (MC_BASE2 + 0x180)
#define MC_CFG_REGDIMM                             (MC_BASE2 + 0x184)
#define MC_CFG_MOD                                 (MC_BASE2 + 0x188)
#define MC_CFG_XS                                  (MC_BASE2 + 0x18c)
#define MC_CFG_XSDLL                               (MC_BASE2 + 0x190)
#define MC_CFG_XPR                                 (MC_BASE2 + 0x194)
#define MC_CFG_AL_MODE                             (MC_BASE2 + 0x198)
#define MC_CFG_CWL                                 (MC_BASE2 + 0x19c)
#define MC_CFG_BL_MODE                             (MC_BASE2 + 0x1a0)
#define MC_CFG_TDQS                                (MC_BASE2 + 0x1a4)
#define MC_CFG_RTT_WR                              (MC_BASE2 + 0x1a8)
#define MC_CFG_LP_ASR                              (MC_BASE2 + 0x1ac)
#define MC_CFG_AUTO_SR                             (MC_BASE2 + 0x1b0)
#define MC_CFG_SRT                                 (MC_BASE2 + 0x1b4)
#define MC_CFG_ADDR_MIRROR                         (MC_BASE2 + 0x1b8)
#define MC_CFG_ZQ_CAL_TYPE                         (MC_BASE2 + 0x1bc)
#define MC_CFG_ZQ_CAL_PER                          (MC_BASE2 + 0x1c0)
#define MC_CFG_AUTO_ZQ_CAL_EN                      (MC_BASE2 + 0x1c4)
#define MC_CFG_MEMORY_TYPE                         (MC_BASE2 + 0x1c8)
#define MC_CFG_ONLY_SRANK_CMDS                     (MC_BASE2 + 0x1cc)
#define MC_CFG_NUM_RANKS                           (MC_BASE2 + 0x1d0)
#define MC_CFG_QUAD_RANK                           (MC_BASE2 + 0x1d4)
#define MC_CFG_EARLY_RANK_TO_WR_START              (MC_BASE2 + 0x1dc)
#define MC_CFG_EARLY_RANK_TO_RD_START              (MC_BASE2 + 0x1e0)
#define MC_CFG_PASR_BANK                           (MC_BASE2 + 0x1e4)
#define MC_CFG_PASR_SEG                            (MC_BASE2 + 0x1e8)
#define MC_INIT_MRR_MODE                           (MC_BASE2 + 0x1ec)
#define MC_INIT_MR_W_REQ                           (MC_BASE2 + 0x1f0)
#define MC_INIT_MR_ADDR                            (MC_BASE2 + 0x1f4)
#define MC_INIT_MR_WR_DATA                         (MC_BASE2 + 0x1f8)
#define MC_INIT_MR_WR_MASK                         (MC_BASE2 + 0x1fc)
#define MC_INIT_NOP                                (MC_BASE2 + 0x200)
#define MC_CFG_INIT_DURATION                       (MC_BASE2 + 0x204)
#define MC_CFG_ZQINIT_CAL_DURATION                 (MC_BASE2 + 0x208)
#define MC_CFG_ZQ_CAL_L_DURATION                   (MC_BASE2 + 0x20c)
#define MC_CFG_ZQ_CAL_S_DURATION                   (MC_BASE2 + 0x210)
#define MC_CFG_ZQ_CAL_R_DURATION                   (MC_BASE2 + 0x214)
#define MC_CFG_MRR                                 (MC_BASE2 + 0x218)
#define MC_CFG_MRW                                 (MC_BASE2 + 0x21c)
#define MC_CFG_ODT_POWERDOWN                       (MC_BASE2 + 0x220)
#define MC_CFG_WL                                  (MC_BASE2 + 0x224)
#define MC_CFG_RL                                  (MC_BASE2 + 0x228)
#define MC_CFG_CAL_READ_PERIOD                     (MC_BASE2 + 0x22c)
#define MC_CFG_NUM_CAL_READS                       (MC_BASE2 + 0x230)
#define MC_INIT_POWER_DOWN                         (MC_BASE2 + 0x23c)
#define MC_INIT_FORCE_WRITE                        (MC_BASE2 + 0x244)
#define MC_INIT_FORCE_WRITE_CS                     (MC_BASE2 + 0x248)
#define MC_CFG_CTRLR_INIT_DISABLE                  (MC_BASE2 + 0x24c)
#define MC_INIT_RDIMM_COMPLETE                     (MC_BASE2 + 0x258)
#define MC_CFG_RDIMM_LAT                           (MC_BASE2 + 0x25c)
#define MC_CFG_RDIMM_BSIDE_INVERT                  (MC_BASE2 + 0x260)
#define MC_CFG_LRDIMM                              (MC_BASE2 + 0x264)
#define MC_INIT_MEMORY_RESET_MASK                  (MC_BASE2 + 0x268)
#define MC_CFG_RD_PREAMB_TOGGLE                    (MC_BASE2 + 0x26c)
#define MC_CFG_RD_POSTAMBLE                        (MC_BASE2 + 0x270)
#define MC_CFG_PU_CAL                              (MC_BASE2 + 0x274)
#define MC_CFG_DQ_ODT                              (MC_BASE2 + 0x278)
#define MC_CFG_CA_ODT                              (MC_BASE2 + 0x27c)
#define MC_CFG_ZQLATCH_DURATION                    (MC_BASE2 + 0x280)
#define MC_INIT_CAL_SELECT                         (MC_BASE2 + 0x284)
#define MC_INIT_CAL_L_R_REQ                        (MC_BASE2 + 0x288)
#define MC_INIT_CAL_L_B_SIZE                       (MC_BASE2 + 0x28c)
#define MC_INIT_RWFIFO                             (MC_BASE2 + 0x2a0)
#define MC_INIT_RD_DQCAL                           (MC_BASE2 + 0x2a4)
#define MC_INIT_START_DQSOSC                       (MC_BASE2 + 0x2a8)
#define MC_INIT_STOP_DQSOSC                        (MC_BASE2 + 0x2ac)
#define MC_INIT_ZQ_CAL_START                       (MC_BASE2 + 0x2b0)
#define MC_CFG_WR_POSTAMBLE                        (MC_BASE2 + 0x2b4)
#define MC_INIT_CAL_L_ADDR_0                       (MC_BASE2 + 0x2bc)
#define MC_INIT_CAL_L_ADDR_1                       (MC_BASE2 + 0x2c0)
#define MC_CFG_CTRLUPD_TRIG                        (MC_BASE2 + 0x2c4)
#define MC_CFG_CTRLUPD_START_DELAY                 (MC_BASE2 + 0x2c8)
#define MC_CFG_DFI_T_CTRLUPD_MAX                   (MC_BASE2 + 0x2cc)
#define MC_CFG_CTRLR_BUSY_SEL                      (MC_BASE2 + 0x2d0)
#define MC_CFG_CTRLR_BUSY_VALUE                    (MC_BASE2 + 0x2d4)
#define MC_CFG_CTRLR_BUSY_TURN_OFF_DELAY           (MC_BASE2 + 0x2d8)
#define MC_CFG_CTRLR_BUSY_SLOW_RESTART_WINDOW      (MC_BASE2 + 0x2dc)
#define MC_CFG_CTRLR_BUSY_RESTART_HOLDOFF          (MC_BASE2 + 0x2e0)
#define MC_CFG_PARITY_RDIMM_DELAY                  (MC_BASE2 + 0x2e4)
#define MC_CFG_CTRLR_BUSY_ENABLE                   (MC_BASE2 + 0x2e8)
#define MC_CFG_ASYNC_ODT                           (MC_BASE2 + 0x2ec)
#define MC_CFG_ZQ_CAL_DURATION                     (MC_BASE2 + 0x2f0)
#define MC_CFG_MRRI                                (MC_BASE2 + 0x2f4)
#define MC_INIT_ODT_FORCE_EN                       (MC_BASE2 + 0x2f8)
#define MC_INIT_ODT_FORCE_RANK                     (MC_BASE2 + 0x2fc)
#define MC_CFG_PHYUPD_ACK_DELAY                    (MC_BASE2 + 0x300)
#define MC_CFG_MIRROR_X16_BG0_BG1                  (MC_BASE2 + 0x304)
#define MC_INIT_PDA_MR_W_REQ                       (MC_BASE2 + 0x308)
#define MC_INIT_PDA_NIBBLE_SELECT                  (MC_BASE2 + 0x30c)
#define MC_CFG_DRAM_CLK_DISABLE_IN_SELF_REFRESH    (MC_BASE2 + 0x310)
#define MC_CFG_CKSRE                               (MC_BASE2 + 0x314)
#define MC_CFG_CKSRX                               (MC_BASE2 + 0x318)
#define MC_CFG_RCD_STAB                            (MC_BASE2 + 0x31c)
#define MC_CFG_DFI_T_CTRL_DELAY                    (MC_BASE2 + 0x320)
#define MC_CFG_DFI_T_DRAM_CLK_ENABLE               (MC_BASE2 + 0x324)
#define MC_CFG_IDLE_TIME_TO_SELF_REFRESH           (MC_BASE2 + 0x328)
#define MC_CFG_IDLE_TIME_TO_POWER_DOWN             (MC_BASE2 + 0x32c)
#define MC_CFG_BURST_RW_REFRESH_HOLDOFF            (MC_BASE2 + 0x330)
#define MC_CFG_BG_INTERLEAVE                       (MC_BASE2 + 0x384)
#define MC_CFG_REFRESH_DURING_PHY_TRAINING         (MC_BASE2 + 0x3fc)

/* Backward-compat aliases for legacy short names used in run_training */
#define MC_CTRLR_SOFT_RESET    MC_CTRLR_SOFT_RESET_N
#define MC_AUTOINIT_DISABLE    MC_INIT_AUTOINIT_DISABLE

/* DFI registers (DDRCFG_BASE + 0x10000) */
#define DFI_BASE                    0x10000
#define MC_DFI_RDDATA_EN            (DFI_BASE + 0x00)
#define MC_DFI_PHY_RDLAT            (DFI_BASE + 0x04)
#define MC_DFI_PHY_WRLAT            (DFI_BASE + 0x08)
#define MC_DFI_PHYUPD_EN            (DFI_BASE + 0x0C)
#define MC_DFI_INIT_COMPLETE        (DFI_BASE + 0x34)
#define MC_DFI_INIT_START           (DFI_BASE + 0x50)

/* Memory Test Controller (MTC) registers - at DDRCFG_BASE + 0x4400 */
#define MTC_BASE                    0x4400
#define MT_EN                       (MTC_BASE + 0x00)
#define MT_EN_SINGLE                (MTC_BASE + 0x04)
#define MT_STOP_ON_ERROR            (MTC_BASE + 0x08)
#define MT_DATA_PATTERN             (MTC_BASE + 0x14)
#define MT_ADDR_PATTERN             (MTC_BASE + 0x18)
#define MT_ADDR_BITS                (MTC_BASE + 0x20)
#define MT_ERROR_STS                (MTC_BASE + 0x24)
#define MT_DONE_ACK                 (MTC_BASE + 0x28)
#define MT_START_ADDR_0             (MTC_BASE + 0xB4)
#define MT_START_ADDR_1             (MTC_BASE + 0xB8)
#define MT_ERROR_MASK_0             (MTC_BASE + 0xBC)
#define MT_ERROR_MASK_1             (MTC_BASE + 0xC0)
#define MT_ERROR_MASK_2             (MTC_BASE + 0xC4)
#define MT_ERROR_MASK_3             (MTC_BASE + 0xC8)
#define MT_ERROR_MASK_4             (MTC_BASE + 0xCC)

/* One controller CSR write: offset (from DDR_CADENCE_CTRL_BASE) and value.
 * The platform builds a const table from its board config and hands it to
 * ddr_cadence_controller_setup(). */
typedef struct {
    uint32_t off;
    uint32_t val;
} ddr_cadence_reg_t;

/* Microsecond delay, provided by the platform (the generic driver has no
 * timer of its own). */
void ddr_cadence_udelay(uint32_t us);

/* Program the controller CSRs from the platform's register table. */
void ddr_cadence_controller_setup(const ddr_cadence_reg_t *regs,
    unsigned int count);

/* Single-shot Memory Test Controller run.  Returns 0 on pass, 1 on data
 * error, DDR_CADENCE_MTC_TIMEOUT_ERROR on timeout. */
uint8_t ddr_cadence_mtc_test(uint8_t mask, uint64_t start_address,
    uint32_t size, uint8_t data_pattern, uint8_t add_pattern);

/* LPDDR4 mode-register writes.  Return 0 on ACK, 1 on no-ACK. */
uint32_t ddr_cadence_mr_masked_write(uint32_t address);
uint32_t ddr_cadence_mr_unmasked_write(uint32_t address, uint32_t data);
uint32_t ddr_cadence_mr_masked_write_x10(uint32_t address);

#endif /* DDR_CADENCE_H */
