/* nxp_t2080.c
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#include "target.h"
#include "printf.h"
#include "image.h" /* for RAMFUNCTION */
#include "nxp_ppc.h"
#include "nxp_t2080.h"

/* generic shared NXP QorIQ driver code */
#include "nxp_ppc.c"

#define ENABLE_IFC
#define ENABLE_BUS_CLK_CALC

#ifndef BUILD_LOADER_STAGE1
    #define ENABLE_MP   /* multi-core support */
#endif

/* Forward declarations */
static void RAMFUNCTION hal_flash_unlock_sector(uint32_t sector);
#ifdef ENABLE_MP
static void hal_mp_init(void);
#endif

/* AMD CFI Commands (Spansion/Cypress) */
#define FLASH_CMD_READ_ID            0x90
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

/* Flash IO Helpers */
#if FLASH_CFI_WIDTH == 16
#define FLASH_IO8_WRITE(sec, n, val)  *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (((val) << 8) | (val))
#define FLASH_IO16_WRITE(sec, n, val) *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))) = (val)
#define FLASH_IO8_READ(sec, n)  (uint8_t)(*((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2))))
#define FLASH_IO16_READ(sec, n)           *((volatile uint16_t*)(FLASH_BASE_ADDR + (FLASH_SECTOR_SIZE * (sec)) + ((n) * 2)))
#else
#define FLASH_IO8_WRITE(sec, n, val)      *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n))) = (val)
#define FLASH_IO8_READ(sec, n)            *((volatile uint8_t*)(FLASH_BASE_ADDR  + (FLASH_SECTOR_SIZE * (sec)) + (n)))
#endif


#ifdef DEBUG_UART
void uart_init(void)
{
    /* calc divisor for UART
     * example config values:
     *  clock_div, baud, base_clk  163 115200 300000000
     * +0.5 to round up
     */
    uint32_t div = (((SYS_CLK / 2.0) / (16 * BAUD_RATE)) + 0.5);

    while (!(get8(UART_LSR(UART_SEL)) & UART_LSR_TEMT))
       ;

    /* set ier, fcr, mcr */
    set8(UART_IER(UART_SEL), 0);
    set8(UART_FCR(UART_SEL), (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN));

    /* enable baud rate access (DLAB=1) - divisor latch access bit*/
    set8(UART_LCR(UART_SEL), (UART_LCR_DLAB | UART_LCR_WLS));
    /* set divisor */
    set8(UART_DLB(UART_SEL), (div & 0xff));
    set8(UART_DMB(UART_SEL), ((div>>8) & 0xff));
    /* disable rate access (DLAB=0) */
    set8(UART_LCR(UART_SEL), (UART_LCR_WLS));
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
            set8(UART_THR(UART_SEL), '\r');
        }
        while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
        set8(UART_THR(UART_SEL), c);
    }
}
#endif /* DEBUG_UART */

void law_init(void)
{
    /* Buffer Manager (BMan) (control) - probably not required */
    set_law(3, 0xF, 0xF4000000, LAW_TRGT_BMAN, LAW_SIZE_32MB, 1);
}

/* Clock helpers */
#ifdef ENABLE_BUS_CLK_CALC
static uint32_t hal_get_core_clk(void)
{
    /* compute core clock (system input * ratio) */
    uint32_t core_clk;
    uint32_t core_ratio = get32(CLOCKING_PLLCNGSR(0)); /* see CGA_PLL1_RAT in RCW */
    /* shift by 1 and mask */
    core_ratio = ((core_ratio >> 1) & 0x3F);
    core_clk = SYS_CLK * core_ratio;
    return core_clk;
}
static uint32_t hal_get_plat_clk(void)
{
    /* compute platform clock (system input * ratio) */
    uint32_t plat_clk;
    uint32_t plat_ratio = get32(CLOCKING_PLLPGSR); /* see SYS_PLL_RAT in RCW */
    /* shift by 1 and mask */
    plat_ratio = ((plat_ratio >> 1) & 0x1F);
    plat_clk = SYS_CLK * plat_ratio;
    return plat_clk;
}
static uint32_t hal_get_bus_clk(void)
{
    /* compute bus clock (platform clock / 2) */
    uint32_t bus_clk = hal_get_plat_clk() / 2;
    return bus_clk;
}
#else
#define hal_get_core_clk() (uint32_t)(SYS_CLK * 14)
#define hal_get_plat_clk() (uint32_t)(SYS_CLK * 4)
#define hal_get_bus_clk()  (uint32_t)(hal_get_plat_clk() / 2)
#endif

#define TIMEBASE_CLK_DIV 16
#define TIMEBASE_HZ (hal_get_plat_clk() / TIMEBASE_CLK_DIV)
#define DELAY_US  (TIMEBASE_HZ / 1000000)
static void udelay(uint32_t delay_us)
{
    wait_ticks(delay_us * DELAY_US);
}

#if defined(ENABLE_IFC) && !defined(BUILD_LOADER_STAGE1)
static int hal_flash_getid(void)
{
    uint8_t manfid[4];

    hal_flash_unlock_sector(0);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, FLASH_CMD_READ_ID);
    udelay(1000);

    manfid[0] = FLASH_IO8_READ(0, 0);  /* Manufacture Code */
    manfid[1] = FLASH_IO8_READ(0, 1);  /* Device Code 1 */
    manfid[2] = FLASH_IO8_READ(0, 14); /* Device Code 2 */
    manfid[3] = FLASH_IO8_READ(0, 15); /* Device Code 3 */

    /* Exit read info */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    udelay(1);

    wolfBoot_printf("Flash: Mfg 0x%x, Device Code 0x%x/0x%x/0x%x\n",
        manfid[0], manfid[1], manfid[2], manfid[3]);

    return 0;
}
#endif /* ENABLE_IFC && !BUILD_LOADER_STAGE1 */

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

    /* Note: hal_flash_getid() is disabled because AMD Autoselect mode
     * affects the entire flash bank. Since wolfBoot runs XIP from the same
     * bank (CS0), entering Autoselect mode crashes instruction fetch.
     * Flash write/erase use RAMFUNCTION to execute from DDR during
     * flash command mode (after .ramcode relocation in hal_init). */
#endif /* ENABLE_IFC */
}

void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint32_t reg;

    /* Map LAW for DDR */
    set_law(4, 0, DDR_ADDRESS, LAW_TRGT_DDR_1, LAW_SIZE_2GB, 0);

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
    set32(DDR_SDRAM_MODE_3, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_4, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_5, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_6, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_7, DDR_SDRAM_MODE_3_8_VAL);
    set32(DDR_SDRAM_MODE_8, DDR_SDRAM_MODE_3_8_VAL);
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

    /* CPC remains enabled (CPCE/CPCPE preserved), now all 2MB as cache */

#ifdef DEBUG_UART
    wolfBoot_printf("CPC: Released SRAM, full 2MB L2 cache enabled\n");
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

#if defined(DEBUG_UART) && defined(ENABLE_DDR)
/* DDR memory test - writes patterns and verifies readback */
static int hal_ddr_test(void)
{
    volatile uint32_t *ddr = (volatile uint32_t *)DDR_ADDRESS;
    uint32_t patterns[] = {0x55555555, 0xAAAAAAAA, 0x12345678, 0xDEADBEEF};
    uint32_t test_offsets[] = {0, 0x100, 0x1000, 0x10000, 0x100000, 0x1000000};
    int i, j;
    int errors = 0;
    uint32_t reg;

#ifdef DEBUG_DDR
    /* Show DDR controller status */
    reg = get32(DDR_SDRAM_CFG);
    wolfBoot_printf("DDR: SDRAM_CFG=0x%x (MEM_EN=%d)\n", reg,
        (reg & DDR_SDRAM_CFG_MEM_EN) ? 1 : 0);
    reg = get32(DDR_SDRAM_CFG_2);
    wolfBoot_printf("DDR: SDRAM_CFG_2=0x%x (D_INIT=%d)\n", reg,
        (reg & DDR_SDRAM_CFG_2_D_INIT) ? 1 : 0);

    /* Show DDR LAW configuration (LAW 4) */
    wolfBoot_printf("DDR LAW4: H=0x%x L=0x%x AR=0x%x\n",
        get32(LAWBARH(4)), get32(LAWBARL(4)), get32(LAWAR(4)));

    /* Read DDR TLB entry 12 using tlbre */
    {
        uint32_t mas0, mas1, mas2, mas3, mas7;
        /* Select TLB1, entry 12 */
        mas0 = (1 << 28) | (12 << 16); /* TLBSEL=1, ESEL=12 */
        mtspr(MAS0, mas0);
        __asm__ __volatile__("isync; tlbre; isync");
        mas1 = mfspr(MAS1);
        mas2 = mfspr(MAS2);
        mas3 = mfspr(MAS3);
        mas7 = mfspr(MAS7);
        wolfBoot_printf("DDR TLB12: MAS1=0x%x MAS2=0x%x MAS3=0x%x MAS7=0x%x\n",
            mas1, mas2, mas3, mas7);
        /* Check if TLB entry is valid */
        if (!(mas1 & 0x80000000)) {
            wolfBoot_printf("DDR: ERROR - TLB12 not valid!\n");
            return -1;
        }
    }
#endif /* DEBUG_DDR */

    /* Check if DDR is enabled */
    if (!(get32(DDR_SDRAM_CFG) & DDR_SDRAM_CFG_MEM_EN)) {
        wolfBoot_printf("DDR: ERROR - Memory not enabled!\n");
        return -1;
    }

    /* Check if DDR LAW is enabled */
    reg = get32(LAWAR(4));
    if (!(reg & LAWAR_ENABLE)) {
        wolfBoot_printf("DDR: ERROR - LAW4 not enabled!\n");
        return -1;
    }

#ifdef DEBUG_DDR
    /* Show DDR chip select configuration */
    wolfBoot_printf("DDR CS0: BNDS=0x%x CFG=0x%x\n",
        get32(DDR_CS_BNDS(0)), get32(DDR_CS_CONFIG(0)));
    wolfBoot_printf("DDR CS1: BNDS=0x%x CFG=0x%x\n",
        get32(DDR_CS_BNDS(1)), get32(DDR_CS_CONFIG(1)));

    /* Show DDR debug status registers */
    wolfBoot_printf("DDR DDRDSR_1=0x%x DDRDSR_2=0x%x\n",
        get32(DDR_DDRDSR_1), get32(DDR_DDRDSR_2));
    wolfBoot_printf("DDR DDRCDR_1=0x%x DDRCDR_2=0x%x\n",
        get32(DDR_DDRCDR_1), get32(DDR_DDRCDR_2));
#endif /* DEBUG_DDR */

    /* Check for pre-existing DDR errors */
    reg = get32(DDR_ERR_DETECT);
    if (reg != 0) {
        wolfBoot_printf("DDR: ERR_DETECT=0x%x (errors present)\n", reg);
#ifdef DEBUG_DDR
        wolfBoot_printf("  Bit 31 (MME): %d - Multiple errors\n", (reg >> 31) & 1);
        wolfBoot_printf("  Bit 7  (APE): %d - Address parity\n", (reg >> 7) & 1);
        wolfBoot_printf("  Bit 3  (ACE): %d - Auto calibration\n", (reg >> 3) & 1);
        wolfBoot_printf("  Bit 2  (CDE): %d - Correctable data\n", (reg >> 2) & 1);
#endif
        return -1;
    }

#ifdef DEBUG_DDR
    wolfBoot_printf("DDR Test: base=0x%x\n", DDR_ADDRESS);
#endif

    for (i = 0; i < (int)(sizeof(test_offsets)/sizeof(test_offsets[0])); i++) {
        uint32_t offset = test_offsets[i];
        volatile uint32_t *addr = ddr + (offset / sizeof(uint32_t));

        for (j = 0; j < (int)(sizeof(patterns)/sizeof(patterns[0])); j++) {
            uint32_t pattern = patterns[j];
            uint32_t readback;

            /* Write pattern */
            *addr = pattern;
            __asm__ __volatile__("sync" ::: "memory");

            /* Read back */
            readback = *addr;

            if (readback != pattern) {
                wolfBoot_printf("DDR FAIL: @0x%x wrote 0x%x read 0x%x\n",
                    (uint32_t)addr, pattern, readback);
                errors++;
            }
        }
    }

    if (errors == 0) {
        wolfBoot_printf("DDR Test: PASSED\n");
    } else {
        wolfBoot_printf("DDR Test: FAILED (%d errors)\n", errors);
    }

    return errors;
}
#endif /* DEBUG_UART && ENABLE_DDR */

void hal_init(void)
{
#if defined(DEBUG_UART) && defined(ENABLE_CPLD)
    uint32_t fw;
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
#endif

    hal_flash_init();
    hal_cpld_init();

#ifdef ENABLE_CPLD
    set8(CPLD_DATA(CPLD_PROC_STATUS), 1); /* Enable proc reset */
    set8(CPLD_DATA(CPLD_WR_TEMP_ALM_OVRD), 0); /* Enable temp alarm */

#ifdef DEBUG_UART
    fw = get8(CPLD_DATA(CPLD_FW_REV));
    wolfBoot_printf("CPLD FW Rev: 0x%x\n", fw);
#endif
#endif /* ENABLE_CPLD */

#ifdef ENABLE_DDR
    /* Test DDR (when DEBUG_UART enabled) */
#ifdef DEBUG_UART
    hal_ddr_test();
#endif

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
#endif

#ifdef ENABLE_MP
    /* Start secondary cores AFTER CPC release and flash caching.
     * Secondary cores' L2 flash-invalidate on the shared cluster L2
     * must not disrupt the CPC SRAM→cache transition. Starting them
     * after ensures the cache hierarchy is fully stable. */
    hal_mp_init();
#endif
}

/* RAM-resident microsecond delay using inline timebase reads.
 * Cannot call wait_ticks() (in flash .text) from RAMFUNCTION code
 * while flash is in command mode — instruction fetch would return garbage. */
static void RAMFUNCTION ram_udelay(uint32_t delay_us)
{
    uint32_t tbl_start, tbl_now;
    uint32_t ticks = delay_us * DELAY_US;
    __asm__ __volatile__("mfspr %0,268" : "=r"(tbl_start));
    do {
        __asm__ __volatile__("mfspr %0,268" : "=r"(tbl_now));
    } while ((tbl_now - tbl_start) < ticks);
}

/* Switch flash TLB to cache-inhibited + guarded for direct flash chip access.
 * AMD flash commands require writes to reach the chip immediately and status
 * reads to come directly from the chip. With MAS2_M (cacheable), stores go
 * through the CPC coherency fabric; IFC does not support coherent writes and
 * returns a bus error (DSI). tlbre/tlbwe only modifies MAS2 and is unreliable
 * when the entry has IPROT=1; use set_tlb() to rewrite all MAS fields.
 * Must be called while flash is still in read-array mode (set_tlb lives in
 * flash .text, reachable via longcall while TLB is still M/cacheable). */
static void RAMFUNCTION hal_flash_cache_disable(void)
{
    set_tlb(1, 2,
        FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G, 0,
        FLASH_TLB_PAGESZ, 1);
}

/* Restore flash TLB to cacheable mode after flash operation.
 * Flash must be back in read-array mode before calling (AMD_CMD_RESET sent).
 * Invalidate caches afterward so stale pre-erase data is not served. */
static void RAMFUNCTION hal_flash_cache_enable(void)
{
    set_tlb(1, 2,
        FLASH_BASE_ADDR, FLASH_BASE_ADDR, FLASH_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_M, 0,
        FLASH_TLB_PAGESZ, 1);
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
        wolfBoot_printf("WP clear: CSPR0=0x%x%s\n", cspr,
            (cspr & IFC_CSPR_WP) ? " (FAILED)" : " (OK)");
    }
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
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, AMD_CMD_SET_PPB_ENTRY);

    /* Read PPB status for target sector: DQ0=0 means protected */
    ppb_status = FLASH_IO8_READ(sector, 0);

    if ((ppb_status & 0x0101) == 0x0101) {
        /* Both chips report unprotected — exit PPB mode and return */
        FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC1);
        FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC2);
        return 0;
    }

    /* Exit PPB ASO before calling printf (flash must be in read-array
     * mode for I-cache misses to fetch valid instructions) */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC1);
    FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC2);
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    ram_udelay(50);

    wolfBoot_printf("PPB: sector %d protected (0x%x), erasing all PPBs\n",
        sector, ppb_status);

    /* Re-enter PPB ASO for erase */
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
    FLASH_IO8_WRITE(0, FLASH_UNLOCK_ADDR1, AMD_CMD_SET_PPB_ENTRY);

    /* PPB Erase All (clears all sectors' PPBs) */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_PPB_UNLOCK_BC1);  /* 0x80 */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_PPB_UNLOCK_BC2);  /* 0x30 */

    /* Wait for PPB erase completion — poll for toggle stop */
    timeout = 0;
    do {
        read1 = FLASH_IO8_READ(0, 0);
        read2 = FLASH_IO8_READ(0, 0);
        if (read1 == read2)
            break;
        ram_udelay(10);
    } while (timeout++ < 100000); /* 1 second */

    /* Exit PPB ASO */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC1);
    FLASH_IO8_WRITE(0, 0, AMD_CMD_SET_PPB_EXIT_BC2);

    /* Reset to read-array mode */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    ram_udelay(50);

    if (timeout >= 100000) {
        wolfBoot_printf("PPB: erase timeout\n");
        return -1;
    }

    wolfBoot_printf("PPB: erase complete\n");
    return 0;
}

/* wait for toggle to stop and status mask to be met within microsecond timeout.
 * RAMFUNCTION: executes from DDR while flash is in program/erase command mode. */
static int RAMFUNCTION hal_flash_status_wait(uint32_t sector, uint16_t mask,
    uint32_t timeout_us)
{
    int ret = 0;
    uint32_t timeout = 0;
    uint16_t read1, read2;

    do {
        /* detection of completion happens when reading status bits
         * DQ6 and DQ2 stop toggling (0x44) */
        read1 = FLASH_IO8_READ(sector, 0);
        if ((read1 & AMD_STATUS_TOGGLE) == 0)
            read1 = FLASH_IO8_READ(sector, 0);
        read2 = FLASH_IO8_READ(sector, 0);
        if ((read2 & AMD_STATUS_TOGGLE) == 0)
            read2 = FLASH_IO8_READ(sector, 0);
    #ifdef DEBUG_FLASH
        wolfBoot_printf("Wait toggle %x -> %x\n", read1, read2);
    #endif
        if (read1 == read2 && ((read1 & mask) == mask))
            break;
        ram_udelay(1);
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
    uint32_t i, pos, sector, offset, xfer, nwords;

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
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    ram_udelay(50);

    pos = 0;
    while (len > 0) {
        /* determine sector address */
        sector = (address / FLASH_SECTOR_SIZE);
        offset = address - (sector * FLASH_SECTOR_SIZE);
        offset /= (FLASH_CFI_WIDTH/8);
        xfer = len;
        if (xfer > FLASH_PAGE_SIZE)
            xfer = FLASH_PAGE_SIZE;
        nwords = xfer / (FLASH_CFI_WIDTH/8);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Write: Sector %d, Offset %d, Len %d, Pos %d\n",
            sector, offset, xfer, pos);
    #endif

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, offset, AMD_CMD_WRITE_TO_BUFFER);
        /* Word count (N-1) must be replicated to both chips */
        FLASH_IO8_WRITE(sector, offset, (nwords-1));

        for (i=0; i<nwords; i++) {
            const uint8_t* ptr = &data[pos];
        #if FLASH_CFI_WIDTH == 16
            FLASH_IO16_WRITE(sector, offset + i, *((const uint16_t*)ptr));
        #else
            FLASH_IO8_WRITE(sector, offset + i, *ptr);
        #endif
            pos += (FLASH_CFI_WIDTH/8);
        }
        FLASH_IO8_WRITE(sector, offset, AMD_CMD_WRITE_BUFFER_CONFIRM);
        /* Typical 410us */

        /* poll for program completion - max 200ms */
        ret = hal_flash_status_wait(sector, 0x44, 200*1000);
        if (ret != 0) {
            /* Reset flash to read-array mode BEFORE calling printf */
            FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
            ram_udelay(50);
            wolfBoot_printf("Flash Write: Timeout at sector %d\n", sector);
            break;
        }

        address += xfer;
        len -= xfer;
    }

    /* Restore flash caching — flash is back in read-array mode */
    hal_flash_cache_enable();
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret = 0;
    uint32_t sector;

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

    /* Disable flash caching — AMD commands must reach the chip directly */
    hal_flash_cache_disable();
    hal_flash_clear_wp();

    /* Reset flash to read-array mode in case previous operation left it
     * in command mode (e.g. after a timeout or incomplete operation) */
    FLASH_IO8_WRITE(0, 0, AMD_CMD_RESET);
    ram_udelay(50);

    while (len > 0) {
        /* determine sector address */
        sector = (address / FLASH_SECTOR_SIZE);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Erase: Sector %d, Addr 0x%x, Len %d\n",
            sector, address, len);
    #endif

        /* Check and clear PPB protection if set */
        if (hal_flash_ppb_unlock(sector) != 0) {
            wolfBoot_printf("Flash Erase: PPB unlock failed sector %d\n", sector);
            ret = -1;
            break;
        }

        wolfBoot_printf("Erasing sector %d...\n", sector);

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
        ret = hal_flash_status_wait(sector, 0x4C, 1100*1000);
        if (ret != 0) {
            /* Reset flash to read-array mode BEFORE calling printf */
            FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
            ram_udelay(50);
            wolfBoot_printf("Flash Erase: Timeout at sector %d\n", sector);
            break;
        }

        /* Erase succeeded — flash is back in read-array mode.
         * Reset to be safe before any printf (I-cache may miss) */
        FLASH_IO8_WRITE(sector, 0, AMD_CMD_RESET);
        ram_udelay(10);
        wolfBoot_printf("Erase sector %d: OK\n", sector);

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

}

/* SMP Multi-Processor Driver */
#ifdef ENABLE_MP

/* from boot_ppc_mp.S */
extern uint32_t _secondary_start_page;
extern uint32_t _second_half_boot_page;
extern uint32_t _spin_table[];
extern uint32_t _spin_table_addr;
extern uint32_t _bootpg_addr;

/* Startup additional cores with spin table and synchronize the timebase.
 * spin_table_ddr: DDR address of the spin table (for checking status) */
static void hal_mp_up(uint32_t bootpg, uint32_t spin_table_ddr)
{
    uint32_t all_cores, active_cores, whoami;
    int timeout = 50, i;

    whoami = get32(PIC_WHOAMI); /* Get current running core number */
    all_cores = ((1 << CPU_NUMCORES) - 1); /* mask of all cores */
    active_cores = (1 << whoami); /* current running cores */

    wolfBoot_printf("MP: Starting cores (boot page %p, spin table %p)\n",
        bootpg, spin_table_ddr);

    /* Set the boot page translation register */
    set32(LCC_BSTRH, 0);
    set32(LCC_BSTRL, bootpg);
    set32(LCC_BSTAR, (LCC_BSTAR_EN |
                      LCC_BSTAR_LAWTRGT(LAW_TRGT_DDR_1) |
                      LAW_SIZE_4KB));
    (void)get32(LCC_BSTAR); /* read back to sync */

    /* Enable time base on current core only */
    set32(RCPM_PCTBENR, (1 << whoami));

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

    /* Disable all timebases */
    set32(RCPM_PCTBENR, 0);

    /* Reset our timebase */
    mtspr(SPRN_TBWU, 0);
    mtspr(SPRN_TBWL, 0);

    /* Enable timebase for all cores */
    set32(RCPM_PCTBENR, all_cores);
}

static void hal_mp_init(void)
{
    uint32_t *fixup = (uint32_t*)&_secondary_start_page;
    uint32_t bootpg, second_half_ddr, spin_table_ddr;
    int i_tlb = 0; /* always 0 */
    size_t i;
    const volatile uint32_t *s;
    volatile uint32_t *d;

    /* Assign virtual boot page at end of LAW-mapped DDR region.
     * DDR LAW maps 2GB (LAW_SIZE_2GB) starting at DDR_ADDRESS.
     * DDR_SIZE may exceed 32-bit range (e.g. 8GB), so use the LAW-mapped
     * size to ensure bootpg fits in 32 bits and is accessible. */
    bootpg = DDR_ADDRESS + 0x80000000UL - BOOT_ROM_SIZE;

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

    /* Map reset page to bootpg so we can copy code there.
     * Boot page translation will redirect secondary core fetches from
     * 0xFFFFF000 to bootpg in DDR. */
    disable_tlb1(i_tlb);
    set_tlb(1, i_tlb, BOOT_ROM_ADDR, bootpg, 0, /* tlb, epn, rpn, urpn */
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), /* perms, wimge */
        0, BOOKE_PAGESZ_4K, 1); /* ts, esel, tsize, iprot */

    /* Copy first half (startup code) to DDR via BOOT_ROM_ADDR mapping.
     * Uses cache-inhibited TLB to ensure data reaches DDR immediately. */
    s = (const uint32_t*)fixup;
    d = (uint32_t*)BOOT_ROM_ADDR;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* Write _bootpg_addr and _spin_table_addr into the DDR first-half copy.
     * These variables are .long 0 in the linked .bootmp (flash), and direct
     * stores to their flash addresses silently fail on XIP builds.
     * Calculate offsets within the boot page and write via BOOT_ROM_ADDR. */
    {
        volatile uint32_t *bp = (volatile uint32_t*)(BOOT_ROM_ADDR +
            ((uint32_t)&_bootpg_addr - (uint32_t)&_secondary_start_page));
        volatile uint32_t *st = (volatile uint32_t*)(BOOT_ROM_ADDR +
            ((uint32_t)&_spin_table_addr - (uint32_t)&_secondary_start_page));
        *bp = second_half_ddr;
        *st = spin_table_ddr;
    }

    /* Copy second half (spin loop + spin table) directly to DDR.
     * Master has DDR TLB (entry 12, MAS2_M). Flush cache after copy
     * to ensure secondary cores see the data. */
    s = (const uint32_t*)&_second_half_boot_page;
    d = (uint32_t*)second_half_ddr;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }
    flush_cache(second_half_ddr, BOOT_ROM_SIZE);

    /* start cores and wait for them to be enabled */
    hal_mp_up(bootpg, spin_table_ddr);
}
#endif /* ENABLE_MP */

void hal_prepare_boot(void)
{

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
    fdt->totalsize += 2048; /* expand by 2KB */
    wolfBoot_printf("FDT: Expanded (2KB) to %d bytes\n", fdt->totalsize);

    /* fixup the memory region - single bank */
    off = fdt_find_devtype(fdt, -1, "memory");
    if (off != -FDT_ERR_NOTFOUND) {
        /* build addr/size as 64-bit */
        uint8_t ranges[sizeof(uint64_t) * 2], *p = ranges;
        *(uint64_t*)p = cpu_to_fdt64(DDR_ADDRESS);
        p += sizeof(uint64_t);
        *(uint64_t*)p = cpu_to_fdt64(DDR_SIZE);
        p += sizeof(uint64_t);
        wolfBoot_printf("FDT: Set memory, start=0x%x, size=0x%x\n",
            DDR_ADDRESS, (uint32_t)DDR_SIZE);
        fdt_setprop(fdt, off, "reg", ranges, (int)(p - ranges));
    }

    /* fixup CPU status and release address and enable method */
    off = fdt_find_devtype(fdt, -1, "cpu");
    while (off != -FDT_ERR_NOTFOUND) {
        int core;
    #ifdef ENABLE_MP
        uint64_t core_spin_table;
    #endif

        reg = (uint32_t*)fdt_getprop(fdt, off, "reg", NULL);
        if (reg == NULL)
            break;
        core = (int)fdt32_to_cpu(*reg);
        if (core >= CPU_NUMCORES) {
            break; /* invalid core index */
        }

    #ifdef ENABLE_MP
        /* calculate location of spin table for core */
        core_spin_table = (uint64_t)((uintptr_t)(
                  (uint8_t*)_spin_table + (core * ENTRY_SIZE)));

        fdt_fixup_str(fdt, off, "cpu", "status", (core == 0) ? "okay" : "disabled");
        fdt_fixup_val64(fdt, off, "cpu", "cpu-release-addr", core_spin_table);
        fdt_fixup_str(fdt, off, "cpu", "enable-method", "spin-table");
    #endif
        fdt_fixup_val(fdt, off, "cpu", "timebase-frequency", TIMEBASE_HZ);
        fdt_fixup_val(fdt, off, "cpu", "clock-frequency", hal_get_core_clk());
        fdt_fixup_val(fdt, off, "cpu", "bus-frequency", hal_get_plat_clk());

        off = fdt_find_devtype(fdt, off, "cpu");
    }

    /* fixup the soc clock */
    off = fdt_find_devtype(fdt, -1, "soc");
    if (off != -FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "soc", "bus-frequency", hal_get_plat_clk());
    }

    /* fixup the serial clocks */
    off = fdt_find_devtype(fdt, -1, "serial");
    while (off != -FDT_ERR_NOTFOUND) {
        fdt_fixup_val(fdt, off, "serial", "clock-frequency", hal_get_bus_clk());
        off = fdt_find_devtype(fdt, off, "serial");
    }

#endif /* !BUILD_LOADER_STAGE1 */
    (void)dts_addr;
    return 0;
}
#endif /* MMU */
