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
static void hal_flash_unlock_sector(uint32_t sector);
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
    /* IFC CS0 - NOR Flash
     * Do NOT reprogram IFC CS0 (CSPR, AMASK, CSOR, FTIM) while executing
     * from flash (XIP) with cache-inhibited TLB (MAS2_I|MAS2_G). The boot
     * ROM already configured CS0 correctly. Reprogramming CSPR while XIP
     * can cause instruction fetch failures because there is no cache to
     * serve fetches during the chip-select decode transition.
     *
     * U-Boot avoids this by using MAS2_W|MAS2_G (write-through, cached)
     * during XIP, only switching to MAS2_I|MAS2_G after relocating to RAM.
     *
     * The LAW is also already set in boot_ppc_start.S:flash_law.
     */

    /* Note: hal_flash_getid() is disabled because AMD Autoselect mode
     * affects the entire flash bank. Since wolfBoot runs XIP from the same
     * bank (CS0), entering Autoselect mode crashes instruction fetch.
     * Flash write/erase operations will need RAMFUNCTION support.
     * TODO: Implement RAMFUNCTION for flash operations on T2080. */
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

    /* Check for pre-existing DDR errors */
    reg = get32(DDR_ERR_DETECT);
    wolfBoot_printf("DDR ERR_DETECT=0x%x\n", reg);
    if (reg != 0) {
        wolfBoot_printf("DDR: ERROR - Pre-existing DDR errors!\n");
        wolfBoot_printf("  Bit 31 (MME): %d - Multiple errors\n", (reg >> 31) & 1);
        wolfBoot_printf("  Bit 7  (APE): %d - Address parity\n", (reg >> 7) & 1);
        wolfBoot_printf("  Bit 3  (ACE): %d - Auto calibration\n", (reg >> 3) & 1);
        wolfBoot_printf("  Bit 2  (CDE): %d - Correctable data\n", (reg >> 2) & 1);
        wolfBoot_printf("DDR: Skipping memory test due to errors\n");
        return -1;
    }

    wolfBoot_printf("DDR Test: base=0x%x\n", DDR_ADDRESS);
    wolfBoot_printf("DDR: Attempting simple read at 0x%x...\n", DDR_ADDRESS);

    /* First just try to read - don't write yet */
    {
        volatile uint32_t val = *ddr;
        wolfBoot_printf("DDR: Read returned 0x%x\n", val);
    }

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
                wolfBoot_printf("  FAIL: @0x%x wrote 0x%x read 0x%x\n",
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

#ifdef ENABLE_MP
    hal_mp_init();
#endif

#if defined(DEBUG_UART) && defined(ENABLE_DDR)
    hal_ddr_test();
#endif
}

static void hal_flash_unlock_sector(uint32_t sector)
{
    /* AMD unlock sequence */
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_UNLOCK_START);
    FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR2, AMD_CMD_UNLOCK_ACK);
}

/* wait for toggle to stop and status mask to be met within microsecond timeout */
static int hal_flash_status_wait(uint32_t sector, uint16_t mask,
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

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
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
    #if FLASH_CFI_WIDTH == 16
        FLASH_IO16_WRITE(sector, offset, (nwords-1));
    #else
        FLASH_IO8_WRITE(sector, offset, (nwords-1));
    #endif

        for (i=0; i<nwords; i++) {
            const uint8_t* ptr = &data[pos];
        #if FLASH_CFI_WIDTH == 16
            FLASH_IO16_WRITE(sector, i, *((const uint16_t*)ptr));
        #else
            FLASH_IO8_WRITE(sector, i, *ptr);
        #endif
            pos += (FLASH_CFI_WIDTH/8);
        }
        FLASH_IO8_WRITE(sector, offset, AMD_CMD_WRITE_BUFFER_CONFIRM);
        /* Typical 410us */

        /* poll for program completion - max 200ms */
        ret = hal_flash_status_wait(sector, 0x44, 200*1000);
        if (ret != 0) {
            wolfBoot_printf("Flash Write: Timeout at sector %d\n", sector);
            break;
        }

        address += xfer;
        len -= xfer;
    }
    return ret;
}

int hal_flash_erase(uint32_t address, int len)
{
    int ret = 0;
    uint32_t sector;

    /* adjust for flash base */
    if (address >= FLASH_BASE_ADDR)
        address -= FLASH_BASE_ADDR;

    while (len > 0) {
        /* determine sector address */
        sector = (address / FLASH_SECTOR_SIZE);

    #ifdef DEBUG_FLASH
        wolfBoot_printf("Flash Erase: Sector %d, Addr 0x%x, Len %d\n",
            sector, address, len);
    #endif

        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, FLASH_UNLOCK_ADDR1, AMD_CMD_ERASE_START);
        hal_flash_unlock_sector(sector);
        FLASH_IO8_WRITE(sector, 0, AMD_CMD_ERASE_SECTOR);
        /* block erase timeout = 50us - for additional sectors */
        /* Typical is 200ms (max 1100ms) */

        /* poll for erase completion - max 1.1 sec */
        ret = hal_flash_status_wait(sector, 0x4C, 1100*1000);
        if (ret != 0) {
            wolfBoot_printf("Flash Erase: Timeout at sector %d\n", sector);
            break;
        }

        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }
    return ret;
}

void hal_flash_unlock(void)
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

/* Startup additional cores with spin table and synchronize the timebase */
static void hal_mp_up(uint32_t bootpg)
{
    uint32_t all_cores, active_cores, whoami;
    int timeout = 50, i;

    whoami = get32(PIC_WHOAMI); /* Get current running core number */
    all_cores = ((1 << CPU_NUMCORES) - 1); /* mask of all cores */
    active_cores = (1 << whoami); /* current running cores */

    wolfBoot_printf("MP: Starting cores (boot page %p, spin table %p)\n",
        bootpg, (uint32_t)_spin_table);

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
            uint32_t* entry = (uint32_t*)(
                  (uint8_t*)_spin_table + (i * ENTRY_SIZE) + ENTRY_ADDR_LOWER);
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
    uint32_t bootpg;
    int i_tlb = 0; /* always 0 */
    size_t i;
    const volatile uint32_t *s;
    volatile uint32_t *d;

    /* Assign virtual boot page at end of LAW-mapped DDR region.
     * DDR LAW maps 2GB (LAW_SIZE_2GB) starting at DDR_ADDRESS.
     * DDR_SIZE may exceed 32-bit range (e.g. 8GB), so use the LAW-mapped
     * size to ensure bootpg fits in 32 bits and is accessible. */
    bootpg = DDR_ADDRESS + 0x80000000UL - BOOT_ROM_SIZE;

    /* Store the boot page address for use by additional CPU cores */
    _bootpg_addr = (uint32_t)&_second_half_boot_page;

    /* Store location of spin table for other cores */
    _spin_table_addr = (uint32_t)_spin_table;

    /* Flush bootpg before copying to invalidate any stale cache lines */
    flush_cache(bootpg, BOOT_ROM_SIZE);

    /* Map reset page to bootpg so we can copy code there */
    disable_tlb1(i_tlb);
    set_tlb(1, i_tlb, BOOT_ROM_ADDR, bootpg, 0, /* tlb, epn, rpn, urpn */
        (MAS3_SX | MAS3_SW | MAS3_SR), (MAS2_I | MAS2_G), /* perms, wimge */
        0, BOOKE_PAGESZ_4K, 1); /* ts, esel, tsize, iprot */

    /* copy startup code to virtually mapped boot address */
    /* do not use memcpy due to compiler array bounds report (not valid) */
    s = (const uint32_t*)fixup;
    d = (uint32_t*)BOOT_ROM_ADDR;
    for (i = 0; i < BOOT_ROM_SIZE/4; i++) {
        d[i] = s[i];
    }

    /* start core and wait for it to be enabled */
    hal_mp_up(bootpg);
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
