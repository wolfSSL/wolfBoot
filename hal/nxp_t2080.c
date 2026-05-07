/* nxp_t2080.c
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
#include <stdint.h>
#include <stddef.h>
#include "target.h"
#include "printf.h"
#include "image.h" /* for RAMFUNCTION */
#include "nxp_ppc.h"
#include "nxp_t2080.h"

#define ENABLE_IFC
#define ENABLE_BUS_CLK_CALC
/* #define DEBUG_FLASH */

#ifndef BUILD_LOADER_STAGE1
    #define ENABLE_MP   /* multi-core support */
#endif

/* DPAA: Logical I/O Device Number (LIODN) and QMan/BMan software-portal
 * initialization. Required before handing off to a DPAA-aware OS such
 * as VxWorks 7 or Linux: peripheral DMA is otherwise blocked by PAMU
 * (LIODN=0 has no valid window) and the QMan dequeue/enqueue paths
 * fault. Default-on for T2080. Define WOLFBOOT_NO_DPAA to disable. */
#ifndef WOLFBOOT_NO_DPAA
    #define ENABLE_DPAA
#endif

/* generic shared NXP QorIQ driver code */
#include "nxp_ppc.c"

/* Forward declarations */
static void RAMFUNCTION hal_flash_unlock_sector(uint32_t sector);
void RAMFUNCTION hal_ifc_cs0_init(void);
#ifdef ENABLE_MP
static void hal_mp_init(void);
#endif

/* AMD CFI Commands (Spansion/Cypress) */
#define AMD_CMD_RESET                0xF0
#define AMD_CMD_WRITE                0xA0
#define AMD_CMD_ERASE_START          0x80
#define AMD_CMD_ERASE_SECTOR         0x30
#define AMD_CMD_UNLOCK_START         0xAA
#define AMD_CMD_UNLOCK_ACK           0x55
#define AMD_CMD_WRITE_TO_BUFFER      0x25
#define AMD_CMD_WRITE_BUFFER_CONFIRM 0x29
#define AMD_CMD_SET_PPB_ENTRY        0xC0
#define AMD_CMD_SET_PPB_EXIT_BC1     0x90
#define AMD_CMD_SET_PPB_EXIT_BC2     0x00
#define AMD_CMD_PPB_UNLOCK_BC1       0x80
#define AMD_CMD_PPB_UNLOCK_BC2       0x30
#define AMD_CMD_PPB_LOCK_BC1         0xA0
#define AMD_CMD_PPB_LOCK_BC2         0x00

#define AMD_STATUS_TOGGLE            0x40
#define AMD_STATUS_ERROR             0x20

/* Flash unlock addresses */
#if FLASH_CFI_WIDTH == 16
#define FLASH_UNLOCK_ADDR1 0x555
#define FLASH_UNLOCK_ADDR2 0x2AA
#else
#define FLASH_UNLOCK_ADDR1 0xAAA
#define FLASH_UNLOCK_ADDR2 0x555
#endif

/* FLASH_CMD_SECTOR: sector used for flash command sequences that don't target
 * a specific sector (reset, unlock, PPB entry/exit). AMD flash command decode
 * only looks at the low address bits, so sector 0 works for all boards with
 * a properly mapped full-flash TLB entry. */
#define FLASH_CMD_SECTOR 0

/* Flash IO Helpers */
#if FLASH_CFI_WIDTH == 16
#define FLASH_IO8_WRITE(sec, n, val)      *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (((val) << 8) | (val))
#define FLASH_IO16_WRITE(sec, n, val)     *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (val)
#define FLASH_IO8_READ(sec, n)  (uint8_t)(*((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))))
#define FLASH_IO16_READ(sec, n)           *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2)))
#else
#define FLASH_IO8_WRITE(sec, n, val)      *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n))) = (val)
#define FLASH_IO8_READ(sec, n)            *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n)))
#endif


#ifdef ENABLE_DPAA
/* T2080RM 4.6.4: DPAA Logical I/O Device Number registers (DCFG block).
 * DCFG_BASE is defined in nxp_t2080.h as (CCSRBAR + 0xE0000). */
#define DCFG_USB1LIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x520))
#define DCFG_USB2LIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x524))
#define DCFG_SDMMCLIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x530))
#define DCFG_SATA1LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x550))
#define DCFG_SATA2LIODNR   ((volatile uint32_t*)(DCFG_BASE + 0x554))
#define DCFG_TDMDMALIODNR  ((volatile uint32_t*)(DCFG_BASE + 0x574))
#define DCFG_DMA1LIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x580))
#define DCFG_DMA2LIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x584))
#define DCFG_DMA3LIODNR    ((volatile uint32_t*)(DCFG_BASE + 0x588))

/* PCIe LIODN base register (PEXx_PEX_LBR @ PCIe block + 0x40).
 * T2080 has four PCIe controllers at CCSRBAR+0x240000 + (n-1)*0x10000. */
#define PCIE_BASE(n)       (CCSRBAR + 0x240000UL + (((n)-1) * 0x10000UL))
#define PCIE_LIODN(n)      ((volatile uint32_t*)(PCIE_BASE(n) + 0x40))

/* T2080RM 10.5.1 / 10.5.2: QMan (CCSR + 0x318000) and BMan (CCSR + 0x31A000).
 * Software-portal physical windows match the T1024/T1040 layout. */
#define QMAN_CCSR_BASE      (CCSRBAR + 0x318000UL)
#define QMAN_BASE_PHYS_HIGH 0xF
#define QMAN_BASE_PHYS      0xF6000000UL
#define QMAN_NUM_PORTALS    18

#define BMAN_CCSR_BASE      (CCSRBAR + 0x31A000UL)
#define BMAN_BASE_PHYS_HIGH 0xF
#define BMAN_BASE_PHYS      0xF4000000UL

/* QMan / BMan CCSR registers */
#define QMAN_LIODNR     ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xD08))
#define BMAN_LIODNR     ((volatile uint32_t*)(BMAN_CCSR_BASE + 0xD08))

/* Frame Queue Descriptor (FQD) and Packed Frame Descriptor Record (PFDR) */
#define FQD_BAR         ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC04))
#define FQD_AR          ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC10))
#define PFDR_BARE       ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC20))
#define PFDR_BAR        ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC24))
#define PFDR_AR         ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC30))

/* QMan Software-Portal Configuration: base address (upper/lower) and
 * per-portal LIO_CFG / IO_CFG. */
#define QCSP_BARE       ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC80))
#define QCSP_BAR        ((volatile uint32_t*)(QMAN_CCSR_BASE + 0xC84))
#define QCSP_LIO_CFG(n) ((volatile uint32_t*)(QMAN_CCSR_BASE + 0x1000 + ((n) * 0x10)))
#define QCSP_IO_CFG(n)  ((volatile uint32_t*)(QMAN_CCSR_BASE + 0x1004 + ((n) * 0x10)))

/* Inhibit registers in the per-portal cache-inhibited window */
#define QCSP_ISDR(n)    ((volatile uint32_t*)(QMAN_BASE_PHYS + 0x1000E08UL + ((n) * 0x1000UL)))
#define BCSP_ISDR(n)    ((volatile uint32_t*)(BMAN_BASE_PHYS + 0x1000E08UL + ((n) * 0x1000UL)))

struct liodn_id_table {
    const char* compat;
    uint32_t    id;
    void*       reg_offset;
};
#define SET_LIODN(fdtcomp, liodn, reg) \
    { .compat = fdtcomp, .id = liodn, .reg_offset = (void*)reg }

/* T2080 LIODN assignments. Values mirror NXP/CW U-Boot 2014.01
 * t2080_ids.c (board/cw/vpx3-152 uses the SoC defaults). */
static const struct liodn_id_table liodn_tbl[] = {
    SET_LIODN("fsl,qman",          62, QMAN_LIODNR),
    SET_LIODN("fsl,bman",          63, BMAN_LIODNR),
    SET_LIODN("fsl,esdhc",        552, DCFG_SDMMCLIODNR),
    SET_LIODN("fsl-usb2-mph",     553, DCFG_USB1LIODNR),
    SET_LIODN("fsl-usb2-dr",      554, DCFG_USB2LIODNR),
    SET_LIODN("fsl,pq-sata-v2",   555, DCFG_SATA1LIODNR),
    SET_LIODN("fsl,pq-sata-v2",   556, DCFG_SATA2LIODNR),
    SET_LIODN("fsl,elo3-dma",     147, DCFG_DMA1LIODNR),
    SET_LIODN("fsl,elo3-dma",     227, DCFG_DMA2LIODNR),
    SET_LIODN("fsl,elo3-dma",     226, DCFG_DMA3LIODNR),
    SET_LIODN("fsl,qoriq-pcie",   148, PCIE_LIODN(1)),
    SET_LIODN("fsl,qoriq-pcie",   228, PCIE_LIODN(2)),
    SET_LIODN("fsl,qoriq-pcie",   308, PCIE_LIODN(3)),
    SET_LIODN("fsl,qoriq-pcie",   388, PCIE_LIODN(4)),
};

/* Program LIODN registers for DPAA-managed peripherals. Mirrors
 * U-Boot's set_liodns() for the subset wolfBoot exposes. */
static void hal_liodn_init(void)
{
    int i;
    for (i = 0; i < (int)(sizeof(liodn_tbl)/sizeof(struct liodn_id_table)); i++) {
        if (liodn_tbl[i].reg_offset != NULL) {
#ifdef DEBUG_UART
            wolfBoot_printf("LIODN %s: %p=%d\n",
                liodn_tbl[i].compat, liodn_tbl[i].reg_offset,
                liodn_tbl[i].id);
#endif
            set32(liodn_tbl[i].reg_offset, liodn_tbl[i].id);
        }
    }
}

struct qportal_info {
    uint16_t dliodn;        /* DQRR LIODN */
    uint16_t fliodn;        /* frame data LIODN */
    uint16_t liodn_offset;
    uint8_t  sdest;
};
#define SET_QP_INFO(dqrr, fdata, off, dest) \
    { .dliodn = (dqrr), .fliodn = (fdata), .liodn_offset = (off), .sdest = (dest) }

/* T2080 has 18 software portals; values from CW U-Boot t2080_ids.c. */
static const struct qportal_info qp_info[QMAN_NUM_PORTALS] = {
    SET_QP_INFO( 1, 27, 1, 0),
    SET_QP_INFO( 2, 28, 1, 0),
    SET_QP_INFO( 3, 29, 1, 1),
    SET_QP_INFO( 4, 30, 1, 1),
    SET_QP_INFO( 5, 31, 1, 2),
    SET_QP_INFO( 6, 32, 1, 2),
    SET_QP_INFO( 7, 33, 1, 3),
    SET_QP_INFO( 8, 34, 1, 3),
    SET_QP_INFO( 9, 35, 1, 0),
    SET_QP_INFO(10, 36, 1, 0),
    SET_QP_INFO(11, 37, 1, 1),
    SET_QP_INFO(12, 38, 1, 1),
    SET_QP_INFO(13, 39, 1, 2),
    SET_QP_INFO(14, 40, 1, 2),
    SET_QP_INFO(15, 41, 1, 3),
    SET_QP_INFO(16, 42, 1, 3),
    SET_QP_INFO(17, 43, 1, 0),
    SET_QP_INFO(18, 44, 1, 0)
};

/* Configure QMan/BMan software portals for handoff to the OS.
 *
 * - Programs the QMan portal cache-enabled base address (QCSP_BARE/BAR).
 * - Clears Frame Queue Descriptor and PFDR base/access registers so
 *   the OS can install its own DPAA tables.
 * - Writes per-portal LIO_CFG/IO_CFG (DQRR LIODN + frame-data LIODN +
 *   sdest hint).
 * - Inhibits all portals via QCSP_ISDR/BCSP_ISDR; the OS un-inhibits
 *   portals it owns.
 *
 * Note: this only programs hardware. If the customer's flat device
 * tree contains placeholder LIODNs (e.g. <0 0>), an FDT fixup pass is
 * also needed - tracked as a follow-up. */
static void hal_qbman_init(void)
{
    int i;

    /* Point QMan portals at the cache-enabled physical window */
    set32(QCSP_BARE, QMAN_BASE_PHYS_HIGH);
    set32(QCSP_BAR,  (uint32_t)QMAN_BASE_PHYS);

    /* Clear FQD / PFDR so OS owns table placement */
    set32(FQD_BAR,  0);
    set32(FQD_AR,   0);
    set32(PFDR_BARE, 0);
    set32(PFDR_BAR,  0);
    set32(PFDR_AR,   0);

    /* Inhibit every portal until OS un-inhibits its share */
    for (i = 0; i < QMAN_NUM_PORTALS; i++) {
        set32(QCSP_ISDR(i), 0x3FFFFF);
    }
    for (i = 0; i < 8; i++) {
        /* BMan inhibit: 3-bit field, 8 portals */
        set32(BCSP_ISDR(i), 0x7);
    }

    /* Program per-portal DQRR / frame LIODN */
    for (i = 0; i < (int)(sizeof(qp_info)/sizeof(struct qportal_info)); i++) {
        set32(QCSP_LIO_CFG(i),
              ((uint32_t)qp_info[i].liodn_offset << 16) |
               (uint32_t)qp_info[i].dliodn);
        set32(QCSP_IO_CFG(i),
              ((uint32_t)qp_info[i].sdest << 16) |
               (uint32_t)qp_info[i].fliodn);
    }
}
#endif /* ENABLE_DPAA */


void law_init(void)
{
    /* Buffer Manager (BMan) (control) */
    set_law(3, BMAN_BASE_PHYS_HIGH, (uint32_t)BMAN_BASE_PHYS,
        LAW_TRGT_BMAN, LAW_SIZE_32MB, 1);

#ifdef ENABLE_DPAA
    /* Queue Manager (QMan) (control) at LAW slot 12.
     * Slots 5..11 and 13..16 are used by hal_cpld_init and the OS64BIT
     * transition path; slot 12 is the only free one in the 32-bit map. */
    set_law(12, QMAN_BASE_PHYS_HIGH, (uint32_t)QMAN_BASE_PHYS,
        LAW_TRGT_QMAN, LAW_SIZE_32MB, 1);

    /* TLB entries for cached BMan/QMan portal windows so that
     * hal_qbman_init() can write the per-portal QCSP_ISDR / BCSP_ISDR
     * inhibit registers (which live in the portal physical window, not
     * CCSR). LIODN registers and per-portal QCSP_LIO_CFG/IO_CFG live
     * in CCSR and are reachable through the existing CCSR TLB.
     *
     * Use TLB slots 13 and 14 -- the OS64BIT transition path
     * (hal_os64bit_map_transition) reuses slots 3-8, 10, 11 to install
     * the 64-bit peripheral map; using lower numbers would have the
     * OS64BIT writes silently overwrite ours mid-flight. */
    set_tlb(1, 13, (uint32_t)BMAN_BASE_PHYS,
                   (uint32_t)BMAN_BASE_PHYS, BMAN_BASE_PHYS_HIGH,
                   MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
                   BOOKE_PAGESZ_16M, 1);
    set_tlb(1, 14, (uint32_t)QMAN_BASE_PHYS,
                   (uint32_t)QMAN_BASE_PHYS, QMAN_BASE_PHYS_HIGH,
                   MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
                   BOOKE_PAGESZ_16M, 1);
#endif /* ENABLE_DPAA */
}

/* Note: AMD Autoselect (READ_ID) mode is not used here because entering it
 * affects the entire flash bank. Since wolfBoot runs XIP from the same
 * bank (CS0), entering Autoselect would crash instruction fetch. */
static void hal_flash_init(void)
{
#ifdef ENABLE_IFC
    uint32_t cspr;

    /* IFC CS0 - NOR Flash
     * Do NOT reprogram IFC CS0 base address, port size, AMASK, CSOR, or
     * FTIM while executing from flash (XIP). The boot ROM already
     * configured CS0 correctly.
     *
     * However, the boot ROM may set IFC_CSPR_WP (write-protect), which
     * blocks all write cycles to the flash. This prevents AMD command
     * sequences (erase/program) from reaching the chips. Clearing just
     * the WP bit is safe during XIP — it doesn't change chip-select
     * decode, only enables write forwarding. */
    cspr = get32(IFC_CSPR(0));
#ifdef DEBUG_UART
    wolfBoot_printf("IFC CSPR0: 0x%x%s\n", cspr,
        (cspr & IFC_CSPR_WP) ? " (WP set)" : "");
#endif
    /* WP clearing is done in hal_flash_clear_wp() from RAMFUNCTION code.
     * T2080RM requires V=0 before modifying IFC_CSPR, which is not safe
     * during XIP. The RAMFUNCTION code runs from DDR with flash TLB
     * guarded, so it can safely toggle V=0 -> modify -> V=1. */
#endif /* ENABLE_IFC */
}

void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint32_t reg;

    /* Map LAW for DDR — use full DDR size.
     * For 4GB boards, a single 4GB LAW at PA 0x0 covers all DDR.
     * CW U-Boot ostype2 uses: set_ddr_laws(0, ddr_size, DDR_1). */
#if DDR_SIZE >= (4096ULL * 1024ULL * 1024ULL)
    set_law(4, 0, DDR_ADDRESS, LAW_TRGT_DDR_1, LAW_SIZE_4GB, 0);
#else
    set_law(4, 0, DDR_ADDRESS, LAW_TRGT_DDR_1, LAW_SIZE_2GB, 0);
#endif

    /* If DDR is already enabled then just return */
    reg = get32(DDR_SDRAM_CFG);
    if (reg & DDR_SDRAM_CFG_MEM_EN) {
        return;
    }

    /* Set clock early for clock / pin */
    set32(DDR_SDRAM_CLK_CNTL, DDR_SDRAM_CLK_CNTL_VAL);

    /* Setup DDR CS (chip select) bounds */
    set32(DDR_CS_BNDS(0), DDR_CS0_BNDS_VAL);
    set32(DDR_CS_CONFIG(0), DDR_CS0_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(0), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(1), DDR_CS1_BNDS_VAL);
    set32(DDR_CS_CONFIG(1), DDR_CS1_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(1), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(2), DDR_CS2_BNDS_VAL);
    set32(DDR_CS_CONFIG(2), DDR_CS2_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(2), DDR_CS_CONFIG_2_VAL);
    set32(DDR_CS_BNDS(3), DDR_CS3_BNDS_VAL);
    set32(DDR_CS_CONFIG(3), DDR_CS3_CONFIG_VAL);
    set32(DDR_CS_CONFIG_2(3), DDR_CS_CONFIG_2_VAL);

    /* DDR SDRAM timing configuration */
    set32(DDR_TIMING_CFG_3, DDR_TIMING_CFG_3_VAL);
    set32(DDR_TIMING_CFG_0, DDR_TIMING_CFG_0_VAL);
    set32(DDR_TIMING_CFG_1, DDR_TIMING_CFG_1_VAL);
    set32(DDR_TIMING_CFG_2, DDR_TIMING_CFG_2_VAL);
    set32(DDR_TIMING_CFG_4, DDR_TIMING_CFG_4_VAL);
    set32(DDR_TIMING_CFG_5, DDR_TIMING_CFG_5_VAL);

    set32(DDR_ZQ_CNTL, DDR_ZQ_CNTL_VAL);

    /* DDR SDRAM mode configuration */
    set32(DDR_SDRAM_MODE, DDR_SDRAM_MODE_VAL);
    set32(DDR_SDRAM_MODE_2, DDR_SDRAM_MODE_2_VAL);
    set32(DDR_SDRAM_MODE_3, DDR_SDRAM_MODE_3_VAL);
    set32(DDR_SDRAM_MODE_4, DDR_SDRAM_MODE_4_VAL);
    set32(DDR_SDRAM_MODE_5, DDR_SDRAM_MODE_5_VAL);
    set32(DDR_SDRAM_MODE_6, DDR_SDRAM_MODE_6_VAL);
    set32(DDR_SDRAM_MODE_7, DDR_SDRAM_MODE_7_VAL);
    set32(DDR_SDRAM_MODE_8, DDR_SDRAM_MODE_8_VAL);
    set32(DDR_SDRAM_MD_CNTL, DDR_SDRAM_MD_CNTL_VAL);

    /* DDR Configuration */
    set32(DDR_SDRAM_INTERVAL, DDR_SDRAM_INTERVAL_VAL);
    set32(DDR_DATA_INIT, DDR_DATA_INIT_VAL);
    set32(DDR_WRLVL_CNTL, DDR_WRLVL_CNTL_VAL);
    set32(DDR_WRLVL_CNTL_2, DDR_WRLVL_CNTL_2_VAL);
    set32(DDR_WRLVL_CNTL_3, DDR_WRLVL_CNTL_3_VAL);
    set32(DDR_SR_CNTR, 0);
    set32(DDR_SDRAM_RCW_1, 0);
    set32(DDR_SDRAM_RCW_2, 0);
    set32(DDR_DDRCDR_1, DDR_DDRCDR_1_VAL);
    set32(DDR_SDRAM_CFG_2, (DDR_SDRAM_CFG_2_VAL | DDR_SDRAM_CFG_2_D_INIT));
    set32(DDR_INIT_ADDR, 0);
    set32(DDR_INIT_EXT_ADDR, 0);
    set32(DDR_DDRCDR_2, DDR_DDRCDR_2_VAL);
    set32(DDR_ERR_DISABLE, 0);
    set32(DDR_ERR_INT_EN, DDR_ERR_INT_EN_VAL);
    set32(DDR_ERR_SBE, DDR_ERR_SBE_VAL);

    /* Set values, but do not enable the DDR yet */
    set32(DDR_SDRAM_CFG, DDR_SDRAM_CFG_VAL & ~DDR_SDRAM_CFG_MEM_EN);
    __asm__ __volatile__("sync;isync");

    /* busy wait for ~500us */
    udelay(500);
    __asm__ __volatile__("sync;isync");

    /* Enable controller */
    reg = get32(DDR_SDRAM_CFG) & ~DDR_SDRAM_CFG_BI;
    set32(DDR_SDRAM_CFG, reg | DDR_SDRAM_CFG_MEM_EN);
    __asm__ __volatile__("sync;isync");

    /* Wait for data initialization to complete */
    while (get32(DDR_SDRAM_CFG_2) & DDR_SDRAM_CFG_2_D_INIT) {
        /* busy wait loop - throttle polling */
        udelay(10000);
    }

#endif /* ENABLE_DDR */
}

void hal_early_init(void)
{
    /* Enable timebase on core 0 */
    set32(RCPM_PCTBENR, (1 << 0));

    /* Only invalidate the CPC if it is NOT configured as SRAM.
     * When CPC SRAM is active (used as stack), writing CPCFI|CPCLFC
     * without preserving CPCE would disable the CPC and corrupt the
     * stack. Skip invalidation when SRAMEN is set (T2080RM 8.4.2.2). */
    if (!(get32((volatile uint32_t*)(CPC_BASE + CPCSRCR0)) & CPCSRCR0_SRAMEN)) {
        set32((volatile uint32_t*)(CPC_BASE + CPCCSR0),
            (CPCCSR0_CPCFI | CPCCSR0_CPCLFC));
        /* Wait for self-clearing invalidate bits */
        while (get32((volatile uint32_t*)(CPC_BASE + CPCCSR0)) &
            (CPCCSR0_CPCFI | CPCCSR0_CPCLFC));
    }

    /* Set DCSR space = 1G */
    set32(DCFG_DCSR, (get32(DCFG_DCSR) | CORENET_DCSR_SZ_1G));
    get32(DCFG_DCSR); /* read back to sync */

    hal_ddr_init();
}

static void hal_cpld_init(void)
{
#ifdef ENABLE_CPLD
    /* CPLD IFC Timing Parameters */
    set32(IFC_FTIM0(3), (IFC_FTIM0_GPCM_TACSE(16UL) |
                         IFC_FTIM0_GPCM_TEADC(16UL) |
                         IFC_FTIM0_GPCM_TEAHC(16UL)));
    set32(IFC_FTIM1(3), (IFC_FTIM1_GPCM_TACO(16UL) |
                         IFC_FTIM1_GPCM_TRAD(31UL)));
    set32(IFC_FTIM2(3), (IFC_FTIM2_GPCM_TCS(16UL) |
                         IFC_FTIM2_GPCM_TCH(8UL) |
                         IFC_FTIM2_GPCM_TWP(31UL)));
    set32(IFC_FTIM3(3), 0);

    /* CPLD IFC Definitions (CS3) */
    set32(IFC_CSPR_EXT(3), CPLD_BASE_PHYS_HIGH);
    set32(IFC_CSPR(3),     (IFC_CSPR_PHYS_ADDR(CPLD_BASE) |
                            IFC_CSPR_PORT_SIZE_16 |
                            IFC_CSPR_MSEL_GPCM |
                            IFC_CSPR_V));
    set32(IFC_AMASK(3), IFC_AMASK_64KB);
    set32(IFC_CSOR(3), 0);

    /* IFC - CPLD (use LAW 5; LAW 2 is used for CPC SRAM) */
    set_law(5, CPLD_BASE_PHYS_HIGH, CPLD_BASE,
        LAW_TRGT_IFC, LAW_SIZE_4KB, 1);

    /* CPLD - TBL=1, Entry 17 */
    set_tlb(1, 17, CPLD_BASE, CPLD_BASE, CPLD_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G,
        0, BOOKE_PAGESZ_4K, 1);
#endif
}

#ifdef ENABLE_FMAN
/* FMAN microcode upload for T2080.
 * Firmware is in NOR flash at FMAN_FW_ADDR (typically 0xFFE60000).
 * Uses same QE firmware format as T1040. */
#ifndef FMAN_FW_ADDR
#define FMAN_FW_ADDR    0xFFE60000UL
#endif
#define FMAN_BASE       (CCSRBAR + 0x400000UL)
#define FMAN_IRAM       (FMAN_BASE + 0xC4000UL)
#define FMAN_IRAM_IADD  ((volatile uint32_t*)(FMAN_IRAM + 0x0))
#define FMAN_IRAM_IDATA ((volatile uint32_t*)(FMAN_IRAM + 0x4))
#define FMAN_IRAM_IREADY ((volatile uint32_t*)(FMAN_IRAM + 0xC))
#define FMAN_IRAM_IADD_AIE  0x80000000
#define FMAN_IRAM_READY     0x80000000

/* Reuse QE firmware structures (same format as T1040) */
#if (defined(__IAR_SYSTEMS_ICC__) && (__IAR_SYSTEMS_ICC__ > 8)) || \
    defined(__GNUC__)
    #define QE_PACKED __attribute__ ((packed))
#else
    #define QE_PACKED
#endif
#define QE_MAX_RISC 4
struct qe_header {
    uint32_t length;
    uint8_t  magic[3];
    uint8_t  version;
} QE_PACKED;
struct qe_soc {
    uint16_t model;
    uint8_t  major;
    uint8_t  minor;
} QE_PACKED;
struct qe_microcode {
    uint8_t  id[32];
    uint32_t traps[16];
    uint32_t eccr;
    uint32_t iram_offset;
    uint32_t count;
    uint32_t code_offset;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  revision;
    uint8_t  padding;
    uint8_t  reserved[4];
} QE_PACKED;
struct qe_firmware {
    struct qe_header    header;
    uint8_t             id[62];
    uint8_t             split;
    uint8_t             count;
    struct qe_soc       soc;
    uint8_t             padding[4];
    uint64_t            extended_modes;
    uint32_t            vtraps[8];
    uint8_t             reserved[4];
    struct qe_microcode microcode[1];
} QE_PACKED;

static int hal_fman_init(void)
{
    const struct qe_firmware *fw = (const struct qe_firmware *)FMAN_FW_ADDR;
    const struct qe_header *hdr = &fw->header;
    unsigned int i;

    /* Check firmware magic */
    if (hdr->magic[0] != 'Q' || hdr->magic[1] != 'E' || hdr->magic[2] != 'F') {
        wolfBoot_printf("FMAN: no firmware at 0x%x\n", FMAN_FW_ADDR);
        return -1;
    }

    for (i = 0; i < fw->count; i++) {
        const struct qe_microcode *ucode = &fw->microcode[i];
        const uint32_t *code;
        unsigned int j;

        if (!ucode->code_offset)
            continue;

        code = (const uint32_t *)((const uint8_t *)fw + ucode->code_offset);
        wolfBoot_printf("FMAN: uploading '%s' v%u.%u.%u\n",
            ucode->id, ucode->major, ucode->minor, ucode->revision);

        set32(FMAN_IRAM_IADD, FMAN_IRAM_IADD_AIE);
        for (j = 0; j < ucode->count; j++) {
            set32(FMAN_IRAM_IDATA, code[j]);
        }
        set32(FMAN_IRAM_IADD, 0);
        {
            int timeout = 1000000;
            while ((get32(FMAN_IRAM_IDATA) != code[0]) && --timeout)
                ;
            if (!timeout) {
                wolfBoot_printf("FMAN: upload timeout\n");
                return -1;
            }
        }
        set32(FMAN_IRAM_IREADY, FMAN_IRAM_READY);
    }

    return 0;
}
#endif /* ENABLE_FMAN */

#ifdef ENABLE_DDR
/* Release CPC SRAM back to L2 cache mode.
 * Call after stack is relocated to DDR (done in boot_entry_C).
 * This gives us the full 2MB CPC as L3 cache for better performance.
 *
 * Before releasing CPC SRAM, .ramcode (RAMFUNCTION) is copied to DDR
 * and TLB9 is remapped: VA 0xF8F00000 -> PA DDR_RAMCODE_ADDR so that
 * RAMFUNCTION code (memcpy, wolfBoot_start, etc.) continues to work. */
static void hal_reconfigure_cpc_as_cache(void)
{
    volatile uint32_t *cpc_csr0 = (volatile uint32_t *)(CPC_BASE + CPCCSR0);
    volatile uint32_t *cpc_srcr0 = (volatile uint32_t *)(CPC_BASE + CPCSRCR0);
    uint32_t reg;

    /* Linker symbols for .ramcode section boundaries */
    extern unsigned int _start_ramcode;
    extern unsigned int _end_ramcode;
    uint32_t ramcode_size = (uint32_t)&_end_ramcode - (uint32_t)&_start_ramcode;

    /* Step 1: Copy .ramcode from CPC SRAM to DDR.
     * Must use volatile loop — memcpy itself is in .ramcode! */
    if (ramcode_size > 0) {
        volatile const uint32_t *src = (volatile const uint32_t *)&_start_ramcode;
        volatile uint32_t *dst = (volatile uint32_t *)DDR_RAMCODE_ADDR;
        volatile uint32_t *end = (volatile uint32_t *)(DDR_RAMCODE_ADDR +
                                                        ramcode_size);
        while (dst < end) {
            *dst++ = *src++;
        }

        /* Ensure all stores have drained before flushing cache lines */
        __asm__ __volatile__("sync" ::: "memory");

        /* Flush D-cache and invalidate I-cache for the DDR copy */
        flush_cache(DDR_RAMCODE_ADDR, ramcode_size);

        /* Step 2: Remap TLB9: same VA (0xF8F00000) -> DDR physical address.
         * All .ramcode references use VA 0xF8F00000, so this makes them
         * transparently access the DDR copy instead of CPC SRAM. */
        set_tlb(1, 9,
            L2SRAM_ADDR, DDR_RAMCODE_ADDR, 0,
            MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0,
            INITIAL_SRAM_BOOKE_SZ, 1);

        /* Ensure TLB update and I-cache pick up new mapping */
        invalidate_icache();
    }

#ifdef DEBUG_UART
    wolfBoot_printf("Ramcode: copied %d bytes to DDR, TLB9 remapped\n",
        ramcode_size);
#endif

    /* Step 3: Flush the CPC to push any dirty SRAM data out.
     * Read-modify-write to preserve CPCE/CPCPE enable bits. */
    reg = *cpc_csr0;
    reg |= CPCCSR0_CPCFL;
    *cpc_csr0 = reg;
    __asm__ __volatile__("sync; isync" ::: "memory");

    /* Step 4: Poll until flush completes (CPCFL clears) */
    while (*cpc_csr0 & CPCCSR0_CPCFL);

    /* Step 5: Disable SRAM mode - release all ways back to cache */
    *cpc_srcr0 = 0;
    __asm__ __volatile__("sync; isync" ::: "memory");

    /* Step 6: Disable CPC SRAM LAW (no longer needed — TLB9 now routes
     * to DDR via LAW4, not CPC SRAM via LAW2).
     * Keep TLB9 — it's remapped to DDR and still in use. */
    set32(LAWAR(2), 0);

    /* Step 7: Flash invalidate CPC to start fresh as cache */
    reg = *cpc_csr0;
    reg |= CPCCSR0_CPCFI;
    *cpc_csr0 = reg;
    __asm__ __volatile__("sync; isync" ::: "memory");
    while (*cpc_csr0 & CPCCSR0_CPCFI);

    /* Step 8: Enable parity/ECC now that SRAM is released and cache is clean.
     * CPCPE was intentionally omitted during ASM init to avoid ECC machine
     * checks on uninitialized SRAM (cold power cycle).  Safe to enable here:
     * SRAM mode is off, CPC is freshly invalidated, no stale data. */
    reg = *cpc_csr0;
    reg |= CPCCSR0_CPCPE;
    *cpc_csr0 = reg;
    __asm__ __volatile__("sync; isync" ::: "memory");

    /* CPC is now fully enabled (CPCE|CPCPE), all 2MB as L3 cache */

#ifdef DEBUG_UART
    wolfBoot_printf("CPC: Released SRAM, full 2MB L3 CPC cache enabled\n");
#endif
}

/* Make flash TLB cacheable for XIP code performance.
 * Changes TLB Entry 2 (flash) from MAS2_I|MAS2_G to MAS2_M.
 * This enables L1 I-cache + L2 + CPC to cache flash instructions. */
static void hal_flash_enable_caching(void)
{
    /* Rewrite flash TLB entry with cacheable attributes.
     * MAS2_M = memory coherent, enables caching */
    set_tlb(1, 2,
        FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0,
        FLASH_TLB_PAGESZ, 1);

    /* Invalidate L1 I-cache so new TLB attributes take effect */
    invalidate_icache();

#ifdef DEBUG_UART
    wolfBoot_printf("Flash: caching enabled (L1+L2+CPC)\n");
#endif
}
#endif /* ENABLE_DDR */


void hal_init(void)
{
    uint32_t bucsr;
#ifdef DEBUG_UART
    uint32_t ddr_ratio;
  #ifdef ENABLE_CPLD
    uint32_t fw;
  #endif
#endif

    /* Enable timebase on core 0 */
    set32(RCPM_PCTBENR, (1 << 0));

    law_init();

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot Init\n", 14);
#ifndef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("Build: %s %s\n", __DATE__, __TIME__);
#endif
    wolfBoot_printf("System Clock: %lu MHz\n",
        (unsigned long)(SYS_CLK / 1000000));
    wolfBoot_printf("Platform Clock: %lu MHz\n",
        (unsigned long)(hal_get_plat_clk() / 1000000));
    wolfBoot_printf("Core Clock: %lu MHz\n",
        (unsigned long)(hal_get_core_clk() / 1000000));
    wolfBoot_printf("Bus Clock: %lu MHz\n",
        (unsigned long)(hal_get_bus_clk() / 1000000));
    wolfBoot_printf("Timebase: %lu MHz\n",
        (unsigned long)(TIMEBASE_HZ / 1000000));
    ddr_ratio = get32(CLOCKING_PLLDGSR);
    ddr_ratio = ((ddr_ratio >> 1) & 0x3F);
    wolfBoot_printf("DDR Clock: %lu MHz (%lu MT/s, ratio %lu:1)\n",
        (unsigned long)(SYS_CLK / 1000000 * ddr_ratio),
        (unsigned long)(SYS_CLK / 1000000 * ddr_ratio * 2),
        (unsigned long)ddr_ratio);
#endif

    hal_flash_init();
#ifdef ENABLE_IFC
    hal_ifc_cs0_init(); /* Set IFC CS0 BA to match flash TLB (RAMFUNCTION) */
#endif

#ifdef DEBUG_UART
    /* Dump LAW BARH values to verify 36-bit addressing */
    wolfBoot_printf("LAW0: BARH=0x%x BARL=0x%x LAWAR=0x%x\n",
        get32(LAWBARH(0)), get32(LAWBARL(0)), get32(LAWAR(0)));
    wolfBoot_printf("LAW1: BARH=0x%x BARL=0x%x LAWAR=0x%x\n",
        get32(LAWBARH(1)), get32(LAWBARL(1)), get32(LAWAR(1)));
    wolfBoot_printf("LAW4: BARH=0x%x BARL=0x%x LAWAR=0x%x\n",
        get32(LAWBARH(4)), get32(LAWBARL(4)), get32(LAWAR(4)));
#endif

    hal_cpld_init();

#ifdef ENABLE_CPLD
    set8(CPLD_DATA(CPLD_PROC_STATUS), 1); /* Enable proc reset */
    set8(CPLD_DATA(CPLD_WR_TEMP_ALM_OVRD), 0); /* Enable temp alarm */

#ifdef DEBUG_UART
    fw = get8(CPLD_DATA(CPLD_FW_REV));
    wolfBoot_printf("CPLD FW Rev: 0x%x\n", fw);
#endif
#endif /* ENABLE_CPLD */

#ifdef ENABLE_DPAA
    /* Program LIODNs and configure QMan/BMan software portals before
     * any DPAA-capable peripheral is touched and before the OS handoff.
     * Without this, VxWorks (and DPAA-aware Linux) faults during early
     * peripheral DMA setup because LIODN=0 has no PAMU window. */
    hal_liodn_init();
    hal_qbman_init();
#endif

#ifdef ENABLE_FMAN
    hal_fman_init();
#endif

#ifdef ENABLE_DDR
    /* Stack is already in DDR (relocated in boot_entry_C via
     * ddr_call_with_stack trampoline before main() was called).
     *
     * Now release CPC SRAM back to L2 cache and enable flash caching.
     * This dramatically improves ECC signature verification performance:
     * - CPC (2MB) becomes L3 cache for all memory accesses
     * - Flash code is cached by L1 I-cache + L2 + CPC
     * - Stack/data in DDR is cached by L1 D-cache + L2 + CPC */
    hal_reconfigure_cpc_as_cache();
    hal_flash_enable_caching();

    /* Enable branch prediction now that DDR stack and cache hierarchy
     * are fully configured.  Disabled during early ASM boot to avoid
     * speculative fetches during hardware init. */
    bucsr = BUCSR_STAC_EN | BUCSR_LS_EN | BUCSR_BBFI | BUCSR_BPEN;
    __asm__ __volatile__("mtspr %0, %1; isync" :: "i"(SPRN_BUCSR), "r"(bucsr));
#endif

    /* Note: previously had a duplicate `set32(DCFG_BRR, 0x0F)` here
     * mislabelled as "enable hardware threading" -- DCFG+0xE4 is
     * actually DCFG_BRR (Boot Release Register), not threading enable,
     * and hal_mp_init() already writes the same value. Removed: it
     * caused secondaries to be released pre-OS, which CW U-Boot does
     * not do for VxWorks 7 64-bit (ossel=ostype2). VxWorks releases
     * its own secondaries via the ePAPR spin-table protocol. */

#ifdef ENABLE_MP
    /* Start secondary cores AFTER CPC release and flash caching.
     * Secondary cores' L2 flash-invalidate on the shared cluster L2
     * must not disrupt the CPC SRAM→cache transition. Starting them
     * after ensures the cache hierarchy is fully stable. */
    hal_mp_init();
#endif
}

/* Switch flash TLB to cache-inhibited + guarded for direct flash chip access.
 * AMD flash commands require writes to reach the chip immediately and status
 * reads to come directly from the chip. With MAS2_M (cacheable), stores go
 * through the CPC coherency fabric; IFC does not support coherent writes and
 * returns a bus error (DSI). */
static void RAMFUNCTION hal_flash_cache_disable(void)
{
    set_tlb(1, 2, FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0, FLASH_TLB_PAGESZ, 1);
}

/* Restore flash TLB to cacheable mode after flash operation.
 * Flash must be back in read-array mode before calling (AMD_CMD_RESET sent).
 * Invalidate caches afterward so stale pre-erase data is not served. */
static void RAMFUNCTION hal_flash_cache_enable(void)
{
    set_tlb(1, 2, FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0, FLASH_TLB_PAGESZ, 1);
    invalidate_dcache();
    invalidate_icache();
}

/* Clear IFC write-protect. T2080RM says IFC_CSPR should only be written
 * when V=0. Must be called from RAMFUNCTION (DDR) with flash TLB set to
 * guarded (MAS2_G) so no speculative access occurs while V is briefly 0. */
static void RAMFUNCTION hal_flash_clear_wp(void)
{
    uint32_t cspr = get32(IFC_CSPR(0));
    if (cspr & IFC_CSPR_WP) {
        /* Clear V first, then modify WP, then re-enable V */
        set32(IFC_CSPR(0), cspr & ~(IFC_CSPR_WP | IFC_CSPR_V));
        __asm__ __volatile__("sync; isync");
        set32(IFC_CSPR(0), (cspr & ~IFC_CSPR_WP) | IFC_CSPR_V);
        __asm__ __volatile__("sync; isync");
        /* Verify WP cleared */
        cspr = get32(IFC_CSPR(0));
    #ifdef DEBUG_FLASH
        wolfBoot_printf("WP clear: CSPR0=0x%x%s\n", cspr,
            (cspr & IFC_CSPR_WP) ? " (FAILED)" : " (OK)");
    #endif
    }
}

/* Initialize IFC CS0 with the correct base address for the NOR flash.
 * The RCW default may have BA=0 (CSPR=0x141) which doesn't match the
 * flash LAW/TLB at FLASH_BASE_ADDR. CW U-Boot sets CSPR=0xF0000105.
 * Must be called from RAMFUNCTION with flash TLB guarded. */
void RAMFUNCTION hal_ifc_cs0_init(void)
{
    /* Match CW U-Boot IFC CS0 configuration exactly:
     * CSPR_EXT=0x0F, CSPR=0xF0000105, AMASK=0xF0000000
     * BA=0xF000 (flash at 0xF0000000), PORT_SIZE=16-bit, GPCM, V=1
     * MSEL=GPCM (0x4) is required to match U-Boot -- previously this
     * code set MSEL=NOR (0) which differed from CW U-Boot's CSPR. */
    uint32_t cspr = get32(IFC_CSPR(0));

    /* Only update if BA doesn't match flash base */
    if ((cspr & 0xFFFF0000) != IFC_CSPR_PHYS_ADDR(FLASH_BASE_ADDR)) {
        /* Clear V, update all IFC CS0 registers, re-enable V */
        set32(IFC_CSPR(0), cspr & ~IFC_CSPR_V);
        __asm__ __volatile__("sync; isync");
        set32(IFC_CSPR_EXT(0), (uint32_t)FLASH_BASE_PHYS_HIGH);
        set32(IFC_AMASK(0), IFC_AMASK_256MB);
        set32(IFC_CSPR(0), IFC_CSPR_PHYS_ADDR(FLASH_BASE_ADDR) |
                           IFC_CSPR_PORT_SIZE_16 |
                           IFC_CSPR_MSEL_GPCM | IFC_CSPR_V);
        __asm__ __volatile__("sync; isync");
    }

#ifdef ENABLE_OS64BIT
    /* IFC CS1/2/3 setup for VxWorks/Linux 64-bit (matches CW U-Boot
     * post-init state). wolfBoot previously set CSPR but NOT AMASK,
     * leaving AMASK = 0 -- chip-select region size is unbounded.
     * AMASK values from CW U-Boot dump:
     *   CS1 (FPGA  8-bit @ 0xEE600000): AMASK=0xFFF80000 (512 KB)
     *   CS2 (NVRAM      @ 0xEE700000): AMASK=0xFFF80000 (512 KB)
     *   CS3 (FPGA 32-bit @ 0xEE400000): AMASK=0xFFE00000 (2 MB) */

    /* IFC CS1: FPGA 8-bit at 0xEE600000 (GPCM, 8-bit port, 512 KB) */
    set32(IFC_CSPR_EXT(1), 0xF);
    set32(IFC_AMASK(1),    IFC_AMASK_512KB);
    set32(IFC_CSPR(1), IFC_CSPR_PHYS_ADDR(0xEE600000) |
                       IFC_CSPR_PORT_SIZE_8 | IFC_CSPR_MSEL_GPCM | IFC_CSPR_V);

    /* IFC CS2: NVRAM at 0xEE700000 (GPCM, 8-bit port, 512 KB) */
    set32(IFC_CSPR_EXT(2), 0xF);
    set32(IFC_AMASK(2),    IFC_AMASK_512KB);
    set32(IFC_CSPR(2), IFC_CSPR_PHYS_ADDR(0xEE700000) |
                       IFC_CSPR_PORT_SIZE_8 | IFC_CSPR_MSEL_GPCM | IFC_CSPR_V);

    /* IFC CS3: FPGA 32-bit at 0xEE400000 (GPCM, 16-bit port, 2 MB) */
    set32(IFC_CSPR_EXT(3), 0xF);
    set32(IFC_AMASK(3),    IFC_AMASK_2MB);
    set32(IFC_CSPR(3), IFC_CSPR_PHYS_ADDR(0xEE400000) |
                       IFC_CSPR_PORT_SIZE_16 | IFC_CSPR_MSEL_GPCM | IFC_CSPR_V);

    /* CSOR mismatch (wolfBoot at reset default 0xC vs CW U-Boot CS0=
     * 0xF000801, CS1/3=0x2F0C0000, CS2=0x0F000000) intentionally NOT
     * touched here: changing CS0 CSOR while wolfBoot is still XIPing
     * from flash hangs the next instruction fetch (timing mismatch
     * mid-fetch). Would need to be done from a RAM-resident path with
     * flash text already cached, or after switching to DDR-resident
     * code. Not critical for OS-handoff matching since the OS
     * immediately reprograms IFC for its own timings. */
    __asm__ __volatile__("sync; isync");
#endif
}

static void RAMFUNCTION hal_flash_unlock_sector(uint32_t sector)
{
    /* AMD unlock sequence */
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
}

/* Check and clear PPB (Persistent Protection Bits) for a sector.
 * S29GL01GS has per-sector non-volatile protection bits. If set, erase/program
 * fails with DQ5 error. PPB erase is chip-wide (clears ALL sectors).
 * Returns: 0 if unprotected or successfully cleared, -1 on failure. */
static int RAMFUNCTION hal_flash_ppb_unlock(uint32_t sector)
{
    uint16_t ppb_status;
    uint16_t read1, read2;
    uint32_t timeout;

    /* Enter PPB ASO (Address Space Overlay) */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR1, AMD_CMD_SET_PPB_ENTRY);

    /* Read PPB status for target sector: DQ0=0 means protected.
     * On 16-bit bus, must read both chip lanes to check both devices. */
#if FLASH_CFI_WIDTH == 16
    ppb_status = FLASH_IO16_READ(sector, 0);
    if ((ppb_status & 0x0101) == 0x0101) {
#else
    ppb_status = FLASH_IO8_READ(sector, 0);
    if ((ppb_status & 0x01) == 0x01) {
#endif
        /* Both chips report unprotected — exit PPB mode and return */
        FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC1);
        FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC2);
        return 0;
    }

    /* Exit PPB ASO before calling printf (flash must be in read-array
     * mode for I-cache misses to fetch valid instructions) */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC1);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC2);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_RESET);
    udelay(50);

#ifdef DEBUG_FLASH
    wolfBoot_printf("PPB: sector %d protected (0x%x), erasing all PPBs\n",
        sector, ppb_status);
#endif

    /* Re-enter PPB ASO for erase */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, FLASH_UNLOCK_ADDR1, AMD_CMD_SET_PPB_ENTRY);

    /* PPB Erase All (clears all sectors' PPBs) */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_PPB_UNLOCK_BC1);  /* 0x80 */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_PPB_UNLOCK_BC2);  /* 0x30 */

    /* Wait for PPB erase completion — poll for toggle stop.
     * On 16-bit bus, read both chip lanes to ensure both complete. */
    timeout = 0;
    do {
#if FLASH_CFI_WIDTH == 16
        read1 = FLASH_IO16_READ(FLASH_CMD_SECTOR, 0);
        read2 = FLASH_IO16_READ(FLASH_CMD_SECTOR, 0);
#else
        read1 = FLASH_IO8_READ(FLASH_CMD_SECTOR, 0);
        read2 = FLASH_IO8_READ(FLASH_CMD_SECTOR, 0);
#endif
        if (read1 == read2)
            break;
        udelay(10);
    } while (timeout++ < 100000); /* 1 second */

    /* Exit PPB ASO */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC1);
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_SET_PPB_EXIT_BC2);

    /* Reset to read-array mode */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_RESET);
    udelay(50);

    if (timeout >= 100000) {
    #ifdef DEBUG_FLASH
        wolfBoot_printf("PPB: erase timeout\n");
    #endif
        return -1;
    }

#ifdef DEBUG_FLASH
    wolfBoot_printf("PPB: erase complete\n");
#endif
    return 0;
}

/* wait for DQ6 toggle to stop within microsecond timeout.
 * RAMFUNCTION: executes from DDR while flash is in program/erase command mode. */
static int RAMFUNCTION hal_flash_status_wait(uint32_t sector, uint32_t timeout_us)
{
    int ret = 0;
    uint32_t timeout = 0;
    uint16_t read1, read2;

    /* Replicate 8-bit AMD toggle/error bits to both bytes for parallel chips */
#if FLASH_CFI_WIDTH == 16
    uint16_t toggle16 = (AMD_STATUS_TOGGLE << 8) | AMD_STATUS_TOGGLE;
    uint16_t error16  = (AMD_STATUS_ERROR  << 8) | AMD_STATUS_ERROR;
#else
    uint16_t toggle16 = AMD_STATUS_TOGGLE;
    uint16_t error16  = AMD_STATUS_ERROR;
#endif

    do {
        /* AMD toggle detection: DQ6 toggles on consecutive reads during
         * program/erase. When the operation completes, DQ6 reflects actual
         * data and consecutive reads return the same value.
         * NOTE: Do NOT check programmed data bits against a mask here —
         * after write completes, the data depends on what was written, not
         * on any fixed status bits. Only erase guarantees 0xFF data. */
#if FLASH_CFI_WIDTH == 16
        read1 = FLASH_IO16_READ(sector, 0);
        read2 = FLASH_IO16_READ(sector, 0);
#else
        read1 = FLASH_IO8_READ(sector, 0);
        read2 = FLASH_IO8_READ(sector, 0);
#endif
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Wait toggle %x -> %x\n", read1, read2);
    #endif
        /* DQ6 stopped toggling → operation complete */
        if (((read1 ^ read2) & toggle16) == 0)
            break;
        /* Check DQ5 (error) on both chips while still toggling */
        if (read1 & error16) {
            /* Read one more time to confirm it's not a false DQ5 */
#if FLASH_CFI_WIDTH == 16
            read1 = FLASH_IO16_READ(sector, 0);
            read2 = FLASH_IO16_READ(sector, 0);
#else
            read1 = FLASH_IO8_READ(sector, 0);
            read2 = FLASH_IO8_READ(sector, 0);
#endif
            if (((read1 ^ read2) & toggle16) == 0)
                break; /* toggle stopped — was a race, not an error */
            ret = -2; /* DQ5 error — program/erase failed */
            break;
        }
        udelay(1);
    } while (timeout++ < timeout_us);
    if (timeout >= timeout_us) {
        ret = -1; /* timeout */
    }
#ifdef DEBUG_FLASH
    wolfBoot_printf("Wait done (%d tries): %x -> %x\n",
        timeout, read1, read2);
#endif
    return ret;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int ret = 0;
    uint32_t i, sector, offset, nwords;
    const uint32_t width_bytes = FLASH_CFI_WIDTH / 8;
    uint32_t addr_off = address;

    /* Bounds check */
    if (addr_off >= FLASH_BASE_ADDR)
        addr_off -= FLASH_BASE_ADDR;
    if (addr_off + (uint32_t)len > FLASH_BANK_SIZE)
        return -1;

    /* Enforce alignment to flash bus width */
    if ((address % width_bytes) != 0 || (len % width_bytes) != 0) {
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Write: unaligned addr 0x%x or len %d "
            "(need %d-byte alignment)\n", address, len, width_bytes);
    #endif
        return -1;
    }

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

#ifdef DEBUG_FLASH
    wolfBoot_printf("Flash Write: Ptr %p -> Addr 0x%x (len %d)\n",
        data, address, len);
#endif

    /* Disable flash caching — AMD commands must reach the chip directly */
    hal_flash_cache_disable();
    hal_flash_clear_wp();

    /* Reset flash to read-array mode in case previous operation left it
     * in command mode (e.g. after a timeout or incomplete operation) */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_RESET);
    udelay(50);

    /* Program one word at a time using AMD single-word program (0xA0).
     * Each word requires: unlock + 0xA0 + data → poll.
     * Typical program time: 60-120us per word.
     * This is simpler and more reliable than Write-Buffer-Program (WBP),
     * which had DQ1 abort/timeout issues on this IFC + S29GL01GS
     * combination. WBP can be re-enabled as an optimization once
     * single-word program is verified working on hardware. */
    nwords = (uint32_t)len / width_bytes;
    for (i = 0; i < nwords; i++) {
        sector = address / FLASH_SECTOR_SIZE;
        offset = (address - (sector * FLASH_SECTOR_SIZE)) / width_bytes;

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_WRITE);
    #if FLASH_CFI_WIDTH == 16
        {
            /* Build 16-bit value from bytes to avoid unaligned access */
            const uint8_t *p = &data[i * 2];
            uint16_t val = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
            FLASH_IO16_WRITE(sector, offset, val);
        }
    #else
        FLASH_IO8_WRITE(sector, offset, data[i]);
    #endif

        /* Poll for program completion (typical 60-120us, max 200ms) */
        ret = hal_flash_status_wait(sector, 200 * 1000);
        if (ret != 0) {
            FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
            udelay(50);
        #ifdef DEBUG_FLASH
            wolfBoot_printf("Flash Write: %s at addr 0x%x\n",
                ret == -2 ? "DQ5 error" : "Timeout",
                (uint32_t)(FLASH_BASE_ADDR + address));
        #endif
            break;
        }

        address += width_bytes;
    }

    /* Restore flash caching — flash is back in read-array mode */
    hal_flash_cache_enable();
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret = 0;
    uint32_t sector;
    uint32_t addr_off = address;

    /* Bounds check */
    if (addr_off >= FLASH_BASE_ADDR)
        addr_off -= FLASH_BASE_ADDR;
    if (addr_off + (uint32_t)len > FLASH_BANK_SIZE)
        return -1;

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

    /* Disable flash caching — AMD commands must reach the chip directly */
    hal_flash_cache_disable();
    hal_flash_clear_wp();

    /* Reset flash to read-array mode in case previous operation left it
     * in command mode (e.g. after a timeout or incomplete operation) */
    FLASH_IO8_WRITE(FLASH_CMD_SECTOR, 0, AMD_CMD_RESET);
    udelay(50);

    while (len > 0) {
        /* determine sector address */
        sector = (address / FLASH_SECTOR_SIZE);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Erase: Sector %d, Addr 0x%x, Len %d\n",
            sector, address, len);
    #endif

        /* Check and clear PPB protection if set */
        if (hal_flash_ppb_unlock(sector) != 0) {
        #ifdef DEBUG_FLASH
            wolfBoot_printf("Flash Erase: PPB unlock failed sector %d\n", sector);
        #endif
            ret = -1;
            break;
        }

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Erasing sector %d...\n", sector);
    #endif

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_ERASE_START);
        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, 0, AMD_CMD_ERASE_SECTOR);
        /* block erase timeout = 50us - for additional sectors */
        /* Typical is 200ms (max 1100ms) */

        /* poll for erase completion - max 1.1 sec
         * NOTE: Do NOT call wolfBoot_printf while flash is in erase mode.
         * With cache-inhibited TLB, I-cache misses fetch from flash which
         * returns status data instead of instructions. */
        ret = hal_flash_status_wait(sector, 1100*1000);
        if (ret != 0) {
            /* Reset flash to read-array mode BEFORE calling printf */
            FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
            udelay(50);
        #ifdef DEBUG_FLASH
            wolfBoot_printf("Flash Erase: Timeout at sector %d\n", sector);
        #endif
            break;
        }

        /* Erase succeeded — flash is back in read-array mode.
         * Reset to be safe before any printf (I-cache may miss) */
        FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
        udelay(10);
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Erase sector %d: OK\n", sector);
    #endif

        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }

    /* Restore flash caching — flash is back in read-array mode */
    hal_flash_cache_enable();
    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    /* Per-sector unlock is done in hal_flash_write/erase before each operation.
     * The previous non-volatile PPB protection mode (C0h) approach caused
     * unnecessary wear on PPB cells since it was called on every boot. */
    hal_flash_unlock_sector(0);
}

void hal_flash_lock(void)
{
    /* intentional no-op: per-sector unlock is done in hal_flash_write/erase */
}

/* SMP Multi-Processor Driver */
#ifdef ENABLE_MP

/* from boot_ppc_mp.S */
extern uint32_t _secondary_start_page;
extern uint32_t _second_half_boot_page;
extern uint32_t _spin_table[];
extern uint32_t _spin_table_addr;

/* DDR address of the spin table, set during hal_mp_init() and reused in
 * hal_dts_fixup() for cpu-release-addr fixups. Also read by boot_ppc.c
 * pre-jump dump to capture spin-table contents at handoff. */
uint32_t g_spin_table_ddr = 0;
extern uint32_t _bootpg_addr;

/* Startup additional cores with spin table and synchronize the timebase.
 * spin_table_ddr: DDR address of the spin table (for checking status) */
static void hal_mp_up(uint32_t bootpg, uint32_t spin_table_ddr)
{
    uint32_t all_cores, active_cores, whoami;
    int timeout = 10000, i; /* 10000 * 100us = 1s, matches U-Boot convention */

    whoami = get32(PIC_WHOAMI); /* Get current running core number */
    all_cores = ((1 << CPU_NUMCORES) - 1); /* mask of all cores */
    active_cores = (1 << whoami); /* current running cores */

    wolfBoot_printf("MP: Starting cores (boot page %p, spin table %p)\n",
        bootpg, spin_table_ddr);

    /* Enable time base on current core only */
    set32(RCPM_PCTBENR, (1 << whoami));

#ifdef ENABLE_OS64BIT
    /* VxWorks 7 64-bit (ossel=ostype2) handoff matches CW U-Boot's
     * profile: BSTRL/BSTAR stay disabled, DCFG_BRR stays 0, and
     * secondary cores remain held in reset. The OS releases its own
     * secondaries via the ePAPR spin-table protocol using
     * cpu-release-addr from the FDT, which still points at the
     * spin_table_ddr we set up below. Skip the active release here. */
    (void)bootpg;
    (void)all_cores;
    (void)spin_table_ddr;
    (void)active_cores;
    (void)timeout;
    (void)i;
#else
    /* Set the boot page translation register */
    set32(LCC_BSTRH, 0);
    set32(LCC_BSTRL, bootpg);
    set32(LCC_BSTAR, (LCC_BSTAR_EN |
                      LCC_BSTAR_LAWTRGT(LAW_TRGT_DDR_1) |
                      LAW_SIZE_4KB));
    (void)get32(LCC_BSTAR); /* read back to sync */

    /* Release the CPU core(s) */
    set32(DCFG_BRR, all_cores);
    __asm__ __volatile__("sync; isync; msync");

    /* wait for other core(s) to start */
    while (timeout) {
        for (i = 0; i < CPU_NUMCORES; i++) {
            volatile uint32_t* entry = (volatile uint32_t*)(
                  spin_table_ddr + (i * ENTRY_SIZE) + ENTRY_ADDR_LOWER);
            if (*entry) {
                active_cores |= (1 << i);
            }
        }
        if ((active_cores & all_cores) == all_cores) {
            break;
        }

        udelay(100);
        timeout--;
    }

    if (timeout == 0) {
        wolfBoot_printf("MP: Timeout enabling additional cores!\n");
    }

    /* Synchronize and reset timebase across all cores.
     * On e6500, mtspr to TBL/TBU (SPR 284/285) may cause an illegal
     * instruction exception — skip timebase reset if secondary cores
     * did not start (timebase sync only matters for multi-core). */
    if ((active_cores & all_cores) == all_cores) {
        /* Disable all timebases */
        set32(RCPM_PCTBENR, 0);

        /* Reset our timebase */
        mtspr(SPRN_TBWU, 0);
        mtspr(SPRN_TBWL, 0);

        /* Enable timebase for all cores */
        set32(RCPM_PCTBENR, all_cores);
    } else {
        /* Only re-enable timebase for boot core */
        set32(RCPM_PCTBENR, (1 << whoami));
    }
#endif /* !ENABLE_OS64BIT */
}

static void hal_mp_init(void)
{
    uint32_t *fixup = (uint32_t*)&_secondary_start_page;
    uint32_t bootpg, second_half_ddr, spin_table_ddr;
#ifdef BOARD_CW_VPX3152
    volatile uint32_t *bp, *st;
#endif
    size_t i;
    const volatile uint32_t *s;
    volatile uint32_t *d;

    /* Assign virtual boot page at end of LAW-mapped DDR region.
     * DDR LAW maps 2GB (LAW_SIZE_2GB) starting at DDR_ADDRESS.
     * DDR_SIZE may exceed 32-bit range (e.g. 8GB), so use the LAW-mapped
     * size to ensure bootpg fits in 32 bits and is accessible.
     *
     * VPX3-152 / VxWorks 7 ostype2: the cw_152_64.dtb has /memory.reg
     * with a hole at 0x7E400000-0x7FFFFFFF (between region 2 ending at
     * 0x7E3FFFFF and region 3 starting at 0x80000000). Putting bootpg /
     * spin_table at 0x7FFFF000 / 0x7FFFE000 lands them inside that hole,
     * which production CW U-Boot ostype2 also does -- but VxWorks may
     * not install TLB mappings for hole addresses when walking /memory.
     * For the silent-boot diagnosis, place bootpg JUST BELOW the hole
     * (top of region 2) so spin_table is inside declared /memory. */
#if defined(ENABLE_OS64BIT) && defined(BOARD_CW_VPX3152)
    bootpg = DDR_ADDRESS + 0x7E400000UL - BOOT_ROM_SIZE;
#else
    bootpg = DDR_ADDRESS + 0x80000000UL - BOOT_ROM_SIZE;
#endif

    /* Second half boot page (spin loop + spin table) goes just below.
     * For XIP flash builds, .bootmp is in flash — secondary cores can't
     * write to flash, so the spin table MUST be in DDR. */
    second_half_ddr = bootpg - BOOT_ROM_SIZE;

    /* DDR addresses for second half symbols */
    spin_table_ddr = second_half_ddr +
        ((uint32_t)_spin_table - (uint32_t)&_second_half_boot_page);

    /* Flush DDR destination before copying */
    flush_cache(bootpg, BOOT_ROM_SIZE);
    flush_cache(second_half_ddr, BOOT_ROM_SIZE);

#ifdef BOARD_CW_VPX3152
    /* VPX3-152: TLB1 Entry 2 (256MB flash) covers 0xF0000000-0xFFFFFFFF
     * which includes BOOT_ROM_ADDR (0xFFFFF000). Creating a TLB1 Entry 0
     * at that VA would cause a multi-hit machine check on e6500.
     * Instead, copy boot page code directly to DDR via the DDR TLB and
     * flush D-cache to ensure secondary cores see the data. */

    /* Copy first half (startup code) directly to DDR at bootpg */
    s = (const uint32_t*)fixup;
    d = (volatile uint32_t*)bootpg;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* Write _bootpg_addr and _spin_table_addr into the DDR copy */
    bp = (volatile uint32_t*)(bootpg +
        ((uint32_t)&_bootpg_addr - (uint32_t)&_secondary_start_page));
    st = (volatile uint32_t*)(bootpg +
        ((uint32_t)&_spin_table_addr - (uint32_t)&_secondary_start_page));
    *bp = second_half_ddr;
    *st = spin_table_ddr;

    /* Flush boot page from D-cache to DDR so secondary cores see it */
    flush_cache(bootpg, BOOT_ROM_SIZE);
#else
    /* Non-VPX3: map BOOT_ROM_ADDR → DDR bootpg via TLB1 Entry 0 with
     * cache-inhibited attributes so writes go directly to DDR. */
    disable_tlb1(0);
    set_tlb(1, 0, BOOT_ROM_ADDR, bootpg, 0,
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G),
        0, BOOKE_PAGESZ_4K, 1);

    /* Copy first half (startup code) to DDR via BOOT_ROM_ADDR mapping */
    s = (const uint32_t*)fixup;
    d = (volatile uint32_t*)BOOT_ROM_ADDR;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* Write _bootpg_addr and _spin_table_addr into the DDR copy */
    {
        volatile uint32_t *bp = (volatile uint32_t*)(BOOT_ROM_ADDR +
            ((uint32_t)&_bootpg_addr - (uint32_t)&_secondary_start_page));
        volatile uint32_t *st = (volatile uint32_t*)(BOOT_ROM_ADDR +
            ((uint32_t)&_spin_table_addr - (uint32_t)&_secondary_start_page));
        *bp = second_half_ddr;
        *st = spin_table_ddr;
    }
#endif

    /* Copy second half (spin loop + spin table) directly to DDR.
     * Flush cache after copy to ensure secondary cores see the data. */
    s = (const uint32_t*)&_second_half_boot_page;
    d = (volatile uint32_t*)second_half_ddr;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }
    flush_cache(second_half_ddr, BOOT_ROM_SIZE);

    /* Persist DDR spin-table base for use in hal_dts_fixup() */
    g_spin_table_ddr = spin_table_ddr;

    /* start cores and wait for them to be enabled */
    hal_mp_up(bootpg, spin_table_ddr);
}
#endif /* ENABLE_MP */

void hal_prepare_boot(void)
{
    /* Intentionally minimal. Flash TLB switch to cache-inhibit and any
     * other pre-OS-jump state changes happen in boot_ppc.c::do_boot()
     * AFTER the FDT fixups + debug prints, since those run from flash
     * and would each take many ms each on uncached IFC reads. */
}

/* Public wrapper for boot_ppc.c::do_boot() - switch flash TLB to
 * MAS2_I|MAS2_G to match CW U-Boot's pre-VxWorks state (TLB#2 WIMG=I|G,
 * MAS2=0xF000000A). Mismatched flash cache attributes between
 * bootloader and OS can cause stale instruction fetches if the OS reads
 * from flash. Must be called AFTER all FDT walks / debug prints --
 * those run from flash and become very slow once cache is off.
 *
 * Also aligns small but observable pre-jump state items to CW U-Boot's
 * profile when chasing VxWorks 7 64-bit silent boot:
 *   - DUART1 MCR = 3   (DTR+RTS asserted; U-Boot sets this, our driver
 *                       leaves it at the post-reset 0)
 *   - TCR = 0x04000000 (matches U-Boot's leftover; wolfBoot was clearing
 *                       it; VxWorks 7 BSP early code may inherit) */
void RAMFUNCTION hal_flash_cache_disable_pre_os(void)
{
    hal_flash_cache_disable();
#ifdef ENABLE_OS64BIT
    /* DUART1 modem control: DTR+RTS asserted, matching CW U-Boot's
     * pre-bootm value. */
    set8(UART_MCR(0), 0x03);
    /* TCR=0 matches CW U-Boot's pre-bootm value. WRC != 0 would let
     * the watchdog fire silently after VxWorks starts. */
    mtspr(SPRN_TCR, 0);
    __asm__ __volatile__("isync" ::: "memory");
#endif
}

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}

int hal_dts_fixup(void* dts_addr)
{
#ifndef BUILD_LOADER_STAGE1
    struct fdt_header *fdt = (struct fdt_header *)dts_addr;
    int off;
    uint32_t *reg;

    /* verify the FDT is valid */
    off = fdt_check_header(dts_addr);
    if (off != 0) {
        wolfBoot_printf("FDT: Invalid header! %d\n", off);
        return off;
    }

    /* display FDT information */
    wolfBoot_printf("FDT: Version %d, Size %d\n",
        fdt_version(fdt), fdt_totalsize(fdt));

    /* expand total size */
    {
        uint32_t new_size = (uint32_t)fdt_totalsize(fdt) + 2048U;
        fdt_set_totalsize(fdt, new_size);
        wolfBoot_printf("FDT: Expanded (2KB) to %d bytes\n",
            fdt_totalsize(fdt));
    }

#ifdef ENABLE_OS64BIT
    /* /memreserve/ entries: keep VxWorks/Linux away from the spin-table
     * page (wolfBoot places it at g_spin_table_ddr, page-aligned down)
     * and the top-of-DDR scratch pages. CW U-Boot's ostype2 reserves its
     * own spin-table page (0x7fee4000); wolfBoot's spin table lives
     * elsewhere, so we reserve based on the actual runtime address. */
    {
        int rsv_ret;
        uint64_t spin_pg = (uint64_t)(g_spin_table_ddr & ~0xFFFU);

        rsv_ret = fdt_add_mem_rsv(fdt, spin_pg, 0x1000ULL);
        if (rsv_ret != 0) {
            wolfBoot_printf("FDT: failed to reserve spin-table page "
                "@ 0x%llx: %d\n", spin_pg, rsv_ret);
            return rsv_ret;
        }
        rsv_ret = fdt_add_mem_rsv(fdt, 0x7ffff000ULL, 0x1000ULL);
        if (rsv_ret != 0) {
            wolfBoot_printf("FDT: failed to reserve boot page "
                "@ 0x7ffff000: %d\n", rsv_ret);
            return rsv_ret;
        }
        rsv_ret = fdt_add_mem_rsv(fdt, 0xfffff000ULL, 0x1000ULL);
        if (rsv_ret != 0) {
            wolfBoot_printf("FDT: failed to reserve top-of-4GB page "
                "@ 0xfffff000: %d\n", rsv_ret);
            return rsv_ret;
        }
    }
#endif

    /* fixup the memory region.
     *
     * IMPORTANT: production CW U-Boot's fdt_fixup_memory only writes
     * /memory if the node is missing (`if (off < 0)`). The cw_152_64.dtb
     * already has /memory.reg populated with the 3-region layout that
     * VxWorks 7 64-bit expects. Overwriting it -- which wolfBoot was
     * doing unconditionally -- causes VxWorks's libfdt-driven memory
     * setup to disagree with what its assembly prologue installed and
     * silent-fail before the UART driver comes up. Skip the write when
     * the node already has a non-empty `reg` property. */
    off = fdt_find_devtype(fdt, -1, "memory");
    if (off >= 0) {
        int reg_len = 0;
        const void *existing = fdt_getprop(fdt, off, "reg", &reg_len);
        if (existing != NULL && reg_len > 0) {
            wolfBoot_printf("FDT: /memory already has reg (%d bytes), keep\n",
                reg_len);
            goto memory_fixup_done;
        }
    }
    if (off >= 0) {
#ifdef ENABLE_OS64BIT
        /* For 64-bit OS: 3-region memory layout matching CW U-Boot's
         * fdt_fixup_memory_vxworks(). Regions avoid the peripheral hole
         * at 0x7E400000-0x7FFFFFFF (reserved for CPU release/spin
         * table). All three regions live at low PA (high cell == 0);
         * the 4GB DDR LAW set in hal_ddr_init covers them all. */
        uint64_t start[3], size[3];
        int num_regions = 2;
        start[0] = cpu_to_fdt64(0x000000000ULL);
        size[0]  = cpu_to_fdt64(0x040000000ULL); /* 1GB */
        start[1] = cpu_to_fdt64(0x040000000ULL);
        size[1]  = cpu_to_fdt64(0x03E400000ULL); /* ~993MB (1G-4M for CPU release) */
    #if DDR_SIZE >= (4096ULL * 1024ULL * 1024ULL) /* 4GB+ */
        num_regions = 3;
        start[2] = cpu_to_fdt64(0x080000000ULL);
        size[2]  = cpu_to_fdt64(DDR_SIZE - 0x080000000ULL); /* upper DDR */
    #endif
        wolfBoot_printf("FDT: Set memory (%d regions, OS 64-bit)\n", num_regions);
        {
            uint64_t reg[6]; /* max 3 start/size pairs */
            int i;
            for (i = 0; i < num_regions; i++) {
                reg[i*2]   = start[i];
                reg[i*2+1] = size[i];
            }
            fdt_setprop(fdt, off, "reg", reg, num_regions * 2 * sizeof(uint64_t));
        }
#else
        /* 32-bit OS: single contiguous DDR region */
        {
            uint64_t ranges[2];
            ranges[0] = cpu_to_fdt64(DDR_ADDRESS);
            ranges[1] = cpu_to_fdt64(DDR_SIZE);
            wolfBoot_printf("FDT: Set memory, start=0x%x, size=0x%x\n",
                DDR_ADDRESS, (uint32_t)DDR_SIZE);
            fdt_setprop(fdt, off, "reg", ranges, sizeof(ranges));
        }
#endif
    }
memory_fixup_done:

    /* fixup CPU status and release address and enable method */
    off = fdt_find_devtype(fdt, -1, "cpu");
    while (off >= 0) {
        uint32_t thread_id;
        int core;
    #ifdef ENABLE_MP
        uint64_t core_spin_table;
    #endif

        reg = (uint32_t*)fdt_getprop(fdt, off, "reg", NULL);
        if (reg == NULL)
            break;
        thread_id = fdt32_to_cpu(*reg);
    #ifdef CORE_E6500
        /* e6500 has 2 threads per core. DTB reg values are thread IDs
         * (0,1 for core0; 2,3 for core1; 4,5 for core2; 6,7 for core3).
         * Convert to physical core ID for spin table indexing. */
        core = (int)(thread_id >> 1);
    #else
        core = (int)thread_id;
    #endif
        if (core >= CPU_NUMCORES) {
            /* Skip invalid cores but continue scanning */
            off = fdt_find_devtype(fdt, off, "cpu");
            continue;
        }

    #ifdef ENABLE_MP
        /* All cores get cpu-release-addr and enable-method = "spin-table"
         * (matches CW U-Boot ostype2 fixup). Boot CPU also gets a release
         * addr — it isn't actually waiting there, but VxWorks/Linux still
         * read the property and reject the node if absent or zero. */
        core_spin_table = (uint64_t)(g_spin_table_ddr +
            (core * ENTRY_SIZE));
        fdt_fixup_val64(fdt, off, "cpu", "cpu-release-addr",
            core_spin_table);
        fdt_fixup_str(fdt, off, "cpu", "enable-method", "spin-table");
        if (core == 0) {
            fdt_fixup_str(fdt, off, "cpu", "status", "okay");
        }
        else {
            fdt_fixup_str(fdt, off, "cpu", "status", "disabled");
        }
    #endif
    #ifndef BOARD_CW_VPX3152
        /* CW VPX3-152: skip cpu/soc/clockgen/serial frequency fixups —
         * the CW base DTB (cw_152_64.dtb) already has the correct values,
         * and CW U-Boot's ft_cpu_setup() does not touch them. Adding them
         * here produces a divergent FDT vs the known-working U-Boot path
         * (extra cpu freq properties + off-by-1/3 rounding on soc/clockgen).
         * Other T2080 targets (RDB) still need these because their base
         * DTBs lack the properties. */
        fdt_fixup_val(fdt, off, "cpu", "timebase-frequency", TIMEBASE_HZ);
        fdt_fixup_val(fdt, off, "cpu", "clock-frequency", hal_get_core_clk());
        fdt_fixup_val(fdt, off, "cpu", "bus-frequency", hal_get_plat_clk());
    #endif

        off = fdt_find_devtype(fdt, off, "cpu");
    }

#ifndef BOARD_CW_VPX3152
    /* fixup the soc clock */
    off = fdt_find_devtype(fdt, -1, "soc");
    if (off >= 0) {
        fdt_fixup_val(fdt, off, "soc", "bus-frequency", hal_get_plat_clk());
    }

    /* fixup clockgen frequency — VxWorks/Linux use this to derive all
     * clocks via PLL ratios. Match U-Boot's ft_cpu_setup behavior. */
    off = fdt_node_offset_by_compatible(fdt, -1, "fsl,qoriq-clockgen-2.0");
    if (off >= 0) {
        fdt_fixup_val(fdt, off, "clockgen", "clock-frequency", SYS_CLK);
    }

    /* fixup the serial clocks */
    off = fdt_find_devtype(fdt, -1, "serial");
    while (off >= 0) {
        fdt_fixup_val(fdt, off, "serial", "clock-frequency", hal_get_bus_clk());
        off = fdt_find_devtype(fdt, off, "serial");
    }
#endif

    /* fixup /chosen bootargs -- override the DTB's baked-in bootargs
     * with WOLFBOOT_BOOTARGS from .config. Production CW U-Boot leaves
     * the DTB value untouched during bootm; we override here so users
     * can change boot parameters without reflashing the DTB. */
#ifdef WOLFBOOT_BOOTARGS
    off = fdt_find_node_offset(fdt, -1, "chosen");
    if (off < 0) {
        off = fdt_add_subnode(fdt, 0, "chosen");
    }
    if (off >= 0) {
        fdt_fixup_str(fdt, off, "chosen", "bootargs", WOLFBOOT_BOOTARGS);
    }
#endif

#endif /* !BUILD_LOADER_STAGE1 */
    (void)dts_addr;
    return 0;
}
#endif /* MMU */
