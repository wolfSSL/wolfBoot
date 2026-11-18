/* mcxe31b.h
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
 *
 * Register definitions for NXP MCXE31B (Cortex-M7, 4MB C40ASF flash).
 * Register offsets and fields from the MCXE31x Reference Manual Rev. 3.
 */

#ifndef MCXE31B_H
#define MCXE31B_H

#include <stdint.h>

/* ============== Clock ============== */

/* The boot ROM (Secure BAF) leaves the core on FIRC 48 MHz. hal_init()
 * switches to the PLL with the RM "Option A" (high performance) dividers:
 * CORE_CLK 160 MHz, AIPS_PLAT_CLK 80 MHz, AIPS_SLOW_CLK 40 MHz (LPUART1..7
 * functional clock), HSE_CLK 80 MHz. */
#define FIRC_FREQ               48000000UL
#define FXOSC_FREQ              16000000UL    /* FRDM-MCXE31B Y1 crystal */
#define PLL_PHI0_FREQ           160000000UL
#ifndef CLOCK_SPEED
#define CLOCK_SPEED             PLL_PHI0_FREQ
#endif

/* MC_CGM clock mux 0 (system clocks) */
#define MC_CGM_BASE             0x402D8000UL
#define MC_CGM_MUX0_CSC         (*(volatile uint32_t *)(MC_CGM_BASE + 0x300))
#define MC_CGM_MUX0_CSS         (*(volatile uint32_t *)(MC_CGM_BASE + 0x304))
#define MC_CGM_MUX0_DC(n)       (*(volatile uint32_t *)(MC_CGM_BASE + 0x308 + ((n) * 4)))
#define MC_CGM_MUX0_DIV_TRIG_CTRL (*(volatile uint32_t *)(MC_CGM_BASE + 0x334))
#define MC_CGM_MUX0_DIV_TRIG    (*(volatile uint32_t *)(MC_CGM_BASE + 0x338))
#define MC_CGM_MUX0_DIV_UPD_STAT (*(volatile uint32_t *)(MC_CGM_BASE + 0x33C))

#define MC_CGM_CSC_SELCTL(x)    (((x) & 0xFUL) << 24)
#define MC_CGM_CSC_CLK_SW       (1UL << 2)
#define MC_CGM_CSS_SELSTAT(r)   (((r) >> 24) & 0xFUL)
#define MC_CGM_CSS_SWIP         (1UL << 16)
#define MC_CGM_SEL_FIRC         0x0
#define MC_CGM_SEL_PLL_PHI0     0x8
#define MC_CGM_DC_DE            (1UL << 31)
#define MC_CGM_DC_DIV(x)        (((x) & 0x7UL) << 16)
#define MC_CGM_DC_GET_DIV(r)    (((r) >> 16) & 0x7UL)
#define MC_CGM_DIV_TRIG_CTRL_TCTL (1UL << 0)
#define MC_CGM_DIV_TRIG_CTRL_HHEN (1UL << 31)
#define MC_CGM_DIV_UPD_STAT_DIV_STAT (1UL << 0)

/* MUX_0 dividers: CORE, AIPS_PLAT, AIPS_SLOW, HSE, DCM, LBIST, QSPI_MEM */
#define MC_CGM_DIV_CORE_CLK     0
#define MC_CGM_DIV_AIPS_PLAT    1
#define MC_CGM_DIV_AIPS_SLOW    2
#define MC_CGM_DIV_HSE          3
#define MC_CGM_DIV_DCM          4
#define MC_CGM_DIV_LBIST        5
#define MC_CGM_DIV_QSPI_MEM     6
#define MC_CGM_MUX0_NUM_DIV     7

/* FXOSC (fast crystal oscillator) */
#define FXOSC_BASE              0x402D4000UL
#define FXOSC_CTRL              (*(volatile uint32_t *)(FXOSC_BASE + 0x0))
#define FXOSC_STAT              (*(volatile uint32_t *)(FXOSC_BASE + 0x4))
#define FXOSC_CTRL_OSCON        (1UL << 0)
#define FXOSC_CTRL_GM_SEL(x)    (((x) & 0xFUL) << 4)
#define FXOSC_CTRL_EOCV(x)      (((x) & 0xFFUL) << 16)
#define FXOSC_CTRL_COMP_EN      (1UL << 24)
#define FXOSC_STAT_OSC_STAT     (1UL << 31)

/* PLLDIG */
#define PLL_BASE                0x402E0000UL
#define PLL_PLLCR               (*(volatile uint32_t *)(PLL_BASE + 0x00))
#define PLL_PLLSR               (*(volatile uint32_t *)(PLL_BASE + 0x04))
#define PLL_PLLDV               (*(volatile uint32_t *)(PLL_BASE + 0x08))
#define PLL_PLLFM               (*(volatile uint32_t *)(PLL_BASE + 0x0C))
#define PLL_PLLFD               (*(volatile uint32_t *)(PLL_BASE + 0x10))
#define PLL_PLLODIV(n)          (*(volatile uint32_t *)(PLL_BASE + 0x80 + ((n) * 4)))
#define PLL_PLLCR_PLLPD         (1UL << 31)
#define PLL_PLLSR_LOCK          (1UL << 2)
#define PLL_PLLDV_MFI(x)        ((x) & 0xFFUL)
#define PLL_PLLDV_RDIV(x)       (((x) & 0x7UL) << 12)
#define PLL_PLLDV_ODIV2(x)      (((x) & 0x3FUL) << 25)
#define PLL_PLLDV_RESERVED      0x003F0000UL  /* keep at reset value */
#define PLL_PLLFM_SSCGBYP       (1UL << 30)
#define PLL_PLLODIV_DE          (1UL << 31)
#define PLL_PLLODIV_DIV(x)      (((x) & 0xFUL) << 16)

/* PMC: the last mile regulator must be on to run above FIRC. On the
 * FRDM-MCXE31B it builds V15 through an external NPN (Q1) driven by
 * VRC_CTRL. */
#define PMC_BASE                0x402E8000UL
#define PMC_LVSC                (*(volatile uint32_t *)(PMC_BASE + 0x0))
#define PMC_CONFIG              (*(volatile uint32_t *)(PMC_BASE + 0x4))
#define PMC_LVSC_LVD15F         (1UL << 5)
#define PMC_LVSC_LVD15S         (1UL << 13)
#define PMC_CONFIG_LMEN         (1UL << 0)
#define PMC_CONFIG_LMBCTLEN     (1UL << 1)

/* PRAMC_0 (SRAM0 controller): one read wait state above 120 MHz */
#define PRAMC0_PRCR1            (*(volatile uint32_t *)0x40264000UL)
#define PRAMC_PRCR1_FT_DIS      (1UL << 0)

/* ============== Flash memory map ============== */

#define FLASH_BASE              0x00400000UL
#define FLASH_BLOCK_SIZE        0x00100000UL  /* 4 x 1 MB code flash blocks */
#define FLASH_NUM_BLOCKS        4
#define FLASH_DATA_BASE         0x10000000UL  /* Data flash = block 4 */
#define FLASH_DATA_SIZE         0x00020000UL  /* 128 KB */
#define FLASH_SECTOR_SIZE       0x2000UL      /* 8 KB */
#define FLASH_SUPER_SECTOR_SIZE 0x10000UL     /* 64 KB */
/* Each 1 MB block: first 768 KB locked per super sector, last 256 KB per
 * sector */
#define FLASH_SUPER_SECTOR_AREA 0x000C0000UL
#define FLASH_QUAD_PAGE_SIZE    128           /* Max bytes per program op */
#define FLASH_DWORD_SIZE        8             /* ECC granularity */

/* ============== Image Vector Table (IVT) ============== */

#define IVT_MARKER              0x5AA55AA5UL
#define IVT_BCW_CM7_0_ENABLE    (1UL << 0)
#define IVT_BCW_APP_SWT_INIT    (1UL << 5)

/* ============== C40ASF flash controller ============== */

#define C40ASF_BASE             0x402EC000UL
#define C40ASF_MCR              (*(volatile uint32_t *)(C40ASF_BASE + 0x000))
#define C40ASF_MCRS             (*(volatile uint32_t *)(C40ASF_BASE + 0x004))
#define C40ASF_DATA(n)          (*(volatile uint32_t *)(C40ASF_BASE + 0x100 + ((n) * 4)))

#define C40ASF_MCR_EHV          (1UL << 0)
#define C40ASF_MCR_ERS          (1UL << 4)
#define C40ASF_MCR_ESS          (1UL << 5)
#define C40ASF_MCR_PGM          (1UL << 8)

#define C40ASF_MCRS_PES         (1UL << 16)
#define C40ASF_MCRS_PEP         (1UL << 17)
#define C40ASF_MCRS_RWE         (1UL << 20)
#define C40ASF_MCRS_PEG         (1UL << 14)
#define C40ASF_MCRS_DONE        (1UL << 15)
#define C40ASF_MCRS_ERR_MASK    (C40ASF_MCRS_PES | C40ASF_MCRS_PEP | \
                                 C40ASF_MCRS_RWE)

/* ============== PFLASH controller ============== */

#define PFLASH_BASE             0x40268000UL
#define PFLASH_PFCR0            (*(volatile uint32_t *)(PFLASH_BASE + 0x000))
#define PFLASH_PFCR1            (*(volatile uint32_t *)(PFLASH_BASE + 0x004))
#define PFLASH_PEADR_L          (*(volatile uint32_t *)(PFLASH_BASE + 0x300))
/* Sector lock: blocks 0-3 = code flash, 4 = data flash */
#define PFLASH_SPELOCK(blk)     (*(volatile uint32_t *)(PFLASH_BASE + 0x340 + ((blk) * 4)))
/* Super sector lock: blocks 0-3 only */
#define PFLASH_SSPELOCK(blk)    (*(volatile uint32_t *)(PFLASH_BASE + 0x35C + ((blk) * 4)))

/* Line buffer enables; clearing them also invalidates the buffers */
#define PFLASH_PFCR_CBFEN       (1UL << 0)
#define PFLASH_PFCR_DBFEN       (1UL << 1)

/* ============== MC_ME (mode entry / clock gating) ============== */

#define MC_ME_BASE              0x402DC000UL
#define MC_ME_CTL_KEY           (*(volatile uint32_t *)(MC_ME_BASE + 0x000))
#define MC_ME_MODE_CONF         (*(volatile uint32_t *)(MC_ME_BASE + 0x004))
#define MC_ME_MODE_UPD          (*(volatile uint32_t *)(MC_ME_BASE + 0x008))
#define MC_ME_PRTN1_PUPD        (*(volatile uint32_t *)(MC_ME_BASE + 0x304))
#define MC_ME_PRTN1_COFB_STAT(n)  (*(volatile uint32_t *)(MC_ME_BASE + 0x310 + ((n) * 4)))
#define MC_ME_PRTN1_COFB_CLKEN(n) (*(volatile uint32_t *)(MC_ME_BASE + 0x330 + ((n) * 4)))

#define MC_ME_CTL_KEY_KEY       0x00005AF0UL
#define MC_ME_CTL_KEY_INVKEY    0x0000A50FUL
#define MC_ME_MODE_CONF_FUNC_RST (1UL << 1)
#define MC_ME_MODE_UPD_MODE_UPD (1UL << 0)
#define MC_ME_PRTN_PUPD_PCUD    (1UL << 0)

/* Partition 1 clock requests: REQn = COFB(n / 32) bit (n % 32) */
#define MC_ME_REQ_FXOSC         53
#define MC_ME_REQ_PLL           56
#define MC_ME_REQ_LPUART5       79

/* ============== SIUL2 (pin mux and GPIO) ============== */

#define SIUL2_BASE              0x40290000UL
#define SIUL2_MSCR(n)           (*(volatile uint32_t *)(SIUL2_BASE + 0x240 + ((n) * 4)))
/* IMCR numbers as listed in the IOMUX file (512 and up) */
#define SIUL2_IMCR(n)           (*(volatile uint32_t *)(SIUL2_BASE + 0xA40 + (((n) - 512) * 4)))
/* 8-bit GPIO data registers, byte-swapped within each 32-bit word */
#define SIUL2_GPDO(n)           (*(volatile uint8_t *)(SIUL2_BASE + 0x1300 + ((n) + 3 - 2 * ((n) % 4))))
#define SIUL2_GPDI(n)           (*(volatile uint8_t *)(SIUL2_BASE + 0x1500 + ((n) + 3 - 2 * ((n) % 4))))

#define SIUL2_MSCR_OBE          (1UL << 21)
#define SIUL2_MSCR_IBE          (1UL << 19)
#define SIUL2_MSCR_SSS(x)       ((x) & 0xFUL)

/* Pad numbers: port * 32 + pin */
#define PAD_PTB22               54
#define PAD_PTC14               78
#define PAD_PTC16               80
#define PAD_PTE3                131
#define PAD_PTE14               142

/* FRDM-MCXE31B: MCU-Link VCOM is LPUART5 on PTE14 (TX) / PTE3 (RX) */
#define UART_TX_PAD             PAD_PTE14
#define UART_TX_SSS             4
#define UART_RX_PAD             PAD_PTE3
#define UART_RX_IMCR            704
#define UART_RX_IMCR_SSS        1

/* FRDM-MCXE31B: RGB LED D1 */
#define LED_RED_PAD             PAD_PTC16
#define LED_GREEN_PAD           PAD_PTB22
#define LED_BLUE_PAD            PAD_PTC14

/* ============== LPUART ============== */

#define LPUART5_BASE            0x4033C000UL
#define LPUART_BAUD             (*(volatile uint32_t *)(LPUART5_BASE + 0x10))
#define LPUART_STAT             (*(volatile uint32_t *)(LPUART5_BASE + 0x14))
#define LPUART_CTRL             (*(volatile uint32_t *)(LPUART5_BASE + 0x18))
#define LPUART_DATA             (*(volatile uint32_t *)(LPUART5_BASE + 0x1C))

#define LPUART_BAUD_SBR_SHIFT   0
#define LPUART_BAUD_OSR_SHIFT   24
#define LPUART_STAT_RDRF        (1UL << 21)
#define LPUART_STAT_TC          (1UL << 22)
#define LPUART_STAT_TDRE        (1UL << 23)
#define LPUART_STAT_OR          (1UL << 19)
#define LPUART_STAT_NF          (1UL << 18)
#define LPUART_STAT_FE          (1UL << 17)
#define LPUART_STAT_PF          (1UL << 16)
#define LPUART_CTRL_RE          (1UL << 18)
#define LPUART_CTRL_TE          (1UL << 19)
#define LPUART_CTRL_RIE         (1UL << 21)
#define LPUART_IRQn             146           /* LPUART5 */

/* ============== Cortex-M7 caches ============== */

#define SCB_CCR                 (*(volatile uint32_t *)0xE000ED14UL)
#define SCB_CCR_DC              (1UL << 16)
#define SCB_CCR_IC              (1UL << 17)
#define SCB_CCSIDR              (*(volatile uint32_t *)0xE000ED80UL)
#define SCB_CSSELR              (*(volatile uint32_t *)0xE000ED84UL)
#define SCB_ICIALLU             (*(volatile uint32_t *)0xE000EF50UL)
#define SCB_DCIMVAC             (*(volatile uint32_t *)0xE000EF5CUL)
#define SCB_DCISW               (*(volatile uint32_t *)0xE000EF60UL)
#define SCB_DCCISW              (*(volatile uint32_t *)0xE000EF74UL)
#define CACHE_LINE_SIZE         32

/* ============== NVIC ============== */

#define NVIC_ISER(n)            (*(volatile uint32_t *)(0xE000E100UL + ((n) * 4)))
#define NVIC_IPR(n)             (*(volatile uint8_t *)(0xE000E400UL + (n)))
#define NVIC_PRIO_BITS          4
#define NVIC_EnableIRQ(irq)     (NVIC_ISER((irq) >> 5) = (1UL << ((irq) & 0x1F)))
#define NVIC_SetPriority(irq, prio) \
    (NVIC_IPR(irq) = (uint8_t)(((prio) << (8 - NVIC_PRIO_BITS)) & 0xFF))

/* ============== SysTick ============== */

#define SYST_CSR                (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR                (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR                (*(volatile uint32_t *)0xE000E018)
#define SYST_CSR_ENABLE         (1UL << 0)
#define SYST_CSR_TICKINT        (1UL << 1)
#define SYST_CSR_CLKSOURCE      (1UL << 2)

#ifdef DEBUG_UART
void uart_init(void);
void uart_tx(uint8_t byte);
void uart_write(const char* buf, unsigned int sz);
int uart_read(char* c);
#endif

#endif /* MCXE31B_H */
