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

/* Delay helper using timebase */
#define DELAY_US (SYS_CLK / 1000000)
static void udelay(uint32_t delay_us)
{
    wait_ticks(delay_us * DELAY_US);
}

static void hal_flash_init(void)
{
    /* IFC - NOR Flash */
    /* LAW is also set in boot_ppc_start.S:flash_law */
    set_law(1, FLASH_BASE_PHYS_HIGH, FLASH_BASE, LAW_TRGT_IFC, LAW_SIZE_128MB, 1);

    /* NOR IFC Flash Timing Parameters */
    set32(IFC_FTIM0(0), (IFC_FTIM0_NOR_TACSE(4) |
                         IFC_FTIM0_NOR_TEADC(5) |
                         IFC_FTIM0_NOR_TEAHC(5)));
    set32(IFC_FTIM1(0), (IFC_FTIM1_NOR_TACO(53) |
                         IFC_FTIM1_NOR_TRAD(26) |
                         IFC_FTIM1_NOR_TSEQ(19)));
    set32(IFC_FTIM2(0), (IFC_FTIM2_NOR_TCS(4) |
                         IFC_FTIM2_NOR_TCH(4) |
                         IFC_FTIM2_NOR_TWPH(14) |
                         IFC_FTIM2_NOR_TWP(28)));
    set32(IFC_FTIM3(0), 0);
    /* NOR IFC Definitions (CS0) */
    set32(IFC_CSPR_EXT(0), 0xF);
    set32(IFC_CSPR(0),     (IFC_CSPR_PHYS_ADDR(FLASH_BASE) |
                            IFC_CSPR_PORT_SIZE_16 |
                            IFC_CSPR_MSEL_NOR |
                            IFC_CSPR_V));
    set32(IFC_AMASK(0), IFC_AMASK_128MB);
    set32(IFC_CSOR(0), 0x0000000C); /* TRHZ (80 clocks for read enable high) */
}

static void hal_ddr_init(void)
{
#ifdef ENABLE_DDR
    uint32_t reg;

    /* Map LAW for DDR */
    set_law(4, 0, DDR_ADDRESS, LAW_TRGT_DDR_1, LAW_SIZE_2GB, 0);

    /* If DDR is already enabled then just return */
    if (get32(DDR_SDRAM_CFG) & DDR_SDRAM_CFG_MEM_EN) {
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

    /* IFC - CPLD */
    set_law(2, CPLD_BASE_PHYS_HIGH, CPLD_BASE,
        LAW_TRGT_IFC, LAW_SIZE_4KB, 1);

    /* CPLD - TBL=1, Entry 17 */
    set_tlb(1, 17, CPLD_BASE, CPLD_BASE, CPLD_BASE_PHYS_HIGH,
        MAS3_SX | MAS3_SW | MAS3_SR, MAS2_I | MAS2_G,
        0, BOOKE_PAGESZ_4K, 1);
#endif
}

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
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    /* TODO: Implement NOR flash write using IFC */
    return 0;
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    /* TODO: Implement NOR flash erase using IFC */
    return 0;
}

void hal_flash_unlock(void)
{
    /* Disable all flash protection bits */
    /* enter Non-volatile protection mode (C0h) */
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xAAAA;
    *((volatile uint16_t*)(FLASH_BASE + 0x554)) = 0x5555;
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xC0C0;
    /* clear all protection bit (80h/30h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x8080;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x3030;
    /* exit Non-volatile protection mode (90h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x9090;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
}

void hal_flash_lock(void)
{
    /* Enable all flash protection bits */
    /* enter Non-volatile protection mode (C0h) */
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xAAAA;
    *((volatile uint16_t*)(FLASH_BASE + 0x554)) = 0x5555;
    *((volatile uint16_t*)(FLASH_BASE + 0xAAA)) = 0xC0C0;
    /* set all protection bit (A0h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0xA0A0;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
    /* exit Non-volatile protection mode (90h/00h) */
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x9090;
    *((volatile uint16_t*)(FLASH_BASE + 0x000)) = 0x0000;
}

void hal_prepare_boot(void)
{

}

#ifdef MMU
void* hal_get_dts_address(void)
{
    return (void*)WOLFBOOT_DTS_BOOT_ADDRESS;
}
#endif
