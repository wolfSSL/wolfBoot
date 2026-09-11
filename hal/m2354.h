/* m2354.h
 *
 * Register definitions for the Nuvoton NuMicro M2354 (Cortex-M23).
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

/* Register definitions transcribed from the M2354 reference manual.
 * wolfBoot does not build against the Nuvoton BSP. */

#ifndef M2354_DEF_INCLUDED
#define M2354_DEF_INCLUDED

#include <stdint.h>

/* Non-secure alias bit. The FMC ISP engine takes the physical address, so
 * this must be masked off before it reaches ISPADDR. */
#define NS_OFFSET (0x10000000UL)

/* Peripheral base addresses */
#define SYS_BASE   (0x40000000UL)
#define CLK_BASE   (0x40000200UL)
#define FMC_BASE   (0x4000C000UL)
#define SCU_BASE   (0x4002F000UL)
/* UART0 is handed to the non-secure world at boot, where it is only
 * reachable through its non-secure alias. */
#define UART0_BASE_S (0x40070000UL)
#ifdef NONSECURE_APP
#  define UART0_BASE (UART0_BASE_S + NS_OFFSET)
#else
#  define UART0_BASE (UART0_BASE_S)
#endif

/* Flash geometry (APROM) */
#define FLASH_APROM_BASE       (0x00000000UL)
#define FLASH_APROM_END        (0x00100000UL)
#define FLASH_APROM_BANK0_END  (0x00080000UL)
#define FLASH_PAGE_SIZE        (0x800UL)      /* 2048 byte erase unit */

/* TrustZone secure/non-secure flash boundary. NSCBA is a flash config
 * word, not a register: this is the address the provisioning script
 * programs. wolfBoot never touches it, and validates the boundary
 * through the live SCU->FNSADDR view instead. */
#define FMC_NSCBA_BASE         (0x00210800UL)

/* SRAM. TrustZone split follows the Nuvoton default: 96 KB secure, the
 * rest non-secure at its alias. */
#define SRAM_BASE              (0x20000000UL)
#define SRAM_SIZE              (0x00040000UL)   /* 256 KB */
#define SRAM_SECURE_SIZE       (0x00018000UL)   /* 96 KB */
#define SRAM_NS_BASE           (SRAM_BASE + NS_OFFSET + SRAM_SECURE_SIZE)
#define SRAM_NS_END            (SRAM_BASE + NS_OFFSET + SRAM_SIZE - 1UL)

/* Non-secure alias of the peripheral space */
#define PERIPH_NS_BASE         (0x50000000UL)
#define PERIPH_NS_END          (0x5FFFFFFFUL)

/*** SYS ***/
#define SYS_GPA_MFPL   (*(volatile uint32_t *)(SYS_BASE + 0x030))
#define SYS_REGLCTL    (*(volatile uint32_t *)(SYS_BASE + 0x100))
#define SYS_PLCTL      (*(volatile uint32_t *)(SYS_BASE + 0x1F8))
#define SYS_PLSTS      (*(volatile uint32_t *)(SYS_BASE + 0x1FC))

/* PL0 is the only power level supporting 96 MHz; PL1 tops out at 84 MHz. */
#define SYS_PLCTL_PLSEL_Msk    (0x3UL << 0)
#define SYS_PLCTL_PLSEL_PL0    (0x0UL << 0)
#define SYS_PLCTL_WRBUSY       (1UL << 7)
#define SYS_PLSTS_PLCBUSY      (1UL << 0)

/* NuMaker-M2354 routes the Nu-Link2-Me virtual COM port to PA6/PA7. */
#define SYS_GPA_MFPL_PA6MFP_Pos       (24)
#define SYS_GPA_MFPL_PA6MFP_Msk       (0xFUL << SYS_GPA_MFPL_PA6MFP_Pos)
#define SYS_GPA_MFPL_PA6MFP_UART0_RXD (0x7UL << SYS_GPA_MFPL_PA6MFP_Pos)
#define SYS_GPA_MFPL_PA7MFP_Pos       (28)
#define SYS_GPA_MFPL_PA7MFP_Msk       (0xFUL << SYS_GPA_MFPL_PA7MFP_Pos)
#define SYS_GPA_MFPL_PA7MFP_UART0_TXD (0x7UL << SYS_GPA_MFPL_PA7MFP_Pos)

/*** CLK ***/
#define CLK_PWRCTL     (*(volatile uint32_t *)(CLK_BASE + 0x000))
#define CLK_AHBCLK     (*(volatile uint32_t *)(CLK_BASE + 0x004))
#define CLK_APBCLK0    (*(volatile uint32_t *)(CLK_BASE + 0x008))
#define CLK_CLKSEL0    (*(volatile uint32_t *)(CLK_BASE + 0x010))
#define CLK_CLKSEL2    (*(volatile uint32_t *)(CLK_BASE + 0x018))
#define CLK_CLKDIV0    (*(volatile uint32_t *)(CLK_BASE + 0x020))
#define CLK_PLLCTL     (*(volatile uint32_t *)(CLK_BASE + 0x040))
#define CLK_STATUS     (*(volatile uint32_t *)(CLK_BASE + 0x050))

#define CLK_PWRCTL_HXTEN            (1UL << 0)
#define CLK_PWRCTL_HIRCEN           (1UL << 2)
#define CLK_STATUS_HXTSTB           (1UL << 0)
#define CLK_STATUS_HIRCSTB          (1UL << 4)
#define CLK_STATUS_PLLSTB           (1UL << 2)

#define CLK_CLKSEL0_HCLKSEL_Pos     (0)
#define CLK_CLKSEL0_HCLKSEL_Msk     (0x7UL << CLK_CLKSEL0_HCLKSEL_Pos)
#define CLK_CLKSEL0_HCLKSEL_PLL     (0x2UL << CLK_CLKSEL0_HCLKSEL_Pos)
#define CLK_CLKSEL0_HCLKSEL_HIRC    (0x7UL << CLK_CLKSEL0_HCLKSEL_Pos)

#define CLK_CLKSEL2_UART0SEL_Pos    (16)
#define CLK_CLKSEL2_UART0SEL_Msk    (0x7UL << CLK_CLKSEL2_UART0SEL_Pos)
#define CLK_CLKSEL2_UART0SEL_HIRC   (0x3UL << CLK_CLKSEL2_UART0SEL_Pos)

#define CLK_CLKDIV0_HCLKDIV_Msk     (0xFUL << 0)
#define CLK_CLKDIV0_UART0DIV_Msk    (0xFUL << 8)

#define CLK_APBCLK0_UART0CKEN       (1UL << 16)

/* Only bank 0 (32 KB) is clocked out of reset. Writes to an unclocked
 * bank are silently discarded, not faulted. */
#define CLK_AHBCLK_SRAM0CKEN        (1UL << 20)
#define CLK_AHBCLK_SRAM1CKEN        (1UL << 21)
#define CLK_AHBCLK_SRAM2CKEN        (1UL << 22)

/* SRAM bank 0 is the only bank guaranteed live at reset, so wolfBoot links
 * itself entirely within it. */

/* PLL: 12 MHz HXT to 96 MHz. NR=2, NF=16, NO=2, encoded as NR-1 in
 * [13:9], NF-2 in [8:0], divider 2 as 0x4000, HXT source as 0 in bit 19. */
#define CLK_PLLCTL_96MHZ_HXT        (0x4000UL | ((2UL - 1UL) << 9) | (16UL - 2UL))

#define CLK_HXT_FREQ                (12000000UL)
#define CLK_HIRC_FREQ               (12000000UL)

/*** UART0 ***/
#define UART0_DAT      (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_FIFO     (*(volatile uint32_t *)(UART0_BASE + 0x008))
#define UART0_LINE     (*(volatile uint32_t *)(UART0_BASE + 0x00C))
#define UART0_FIFOSTS  (*(volatile uint32_t *)(UART0_BASE + 0x018))
#define UART0_BAUD     (*(volatile uint32_t *)(UART0_BASE + 0x024))
#define UART0_FUNCSEL  (*(volatile uint32_t *)(UART0_BASE + 0x030))

#define UART_FIFO_RXRST             (1UL << 1)
#define UART_FIFO_TXRST             (1UL << 2)
#define UART_LINE_WLS_8BIT          (0x3UL << 0)  /* 8 data bits */
#define UART_LINE_NSB_1BIT          (0x0UL << 2)  /* 1 stop bit  */
#define UART_LINE_PBE_NONE          (0x0UL << 3)  /* no parity   */
#define UART_FIFOSTS_TXFULL         (1UL << 23)
#define UART_FIFOSTS_TXEMPTYF       (1UL << 28)
#define UART_BAUD_BAUDM0            (1UL << 28)
#define UART_BAUD_BAUDM1            (1UL << 29)
/* Baud mode 2: BRD = ((src + baud/2) / baud) - 2 */
#define UART_BAUD_MODE2_DIVIDER(src, baud) \
    (((((src) + ((baud) / 2UL)) / (baud)) - 2UL))

/*** FMC (flash ISP) ***/
#define FMC_ISPCTL     (*(volatile uint32_t *)(FMC_BASE + 0x00))
#define FMC_ISPADDR    (*(volatile uint32_t *)(FMC_BASE + 0x04))
#define FMC_ISPDAT     (*(volatile uint32_t *)(FMC_BASE + 0x08))
#define FMC_ISPCMD     (*(volatile uint32_t *)(FMC_BASE + 0x0C))
#define FMC_ISPTRG     (*(volatile uint32_t *)(FMC_BASE + 0x10))
#define FMC_CYCCTL     (*(volatile uint32_t *)(FMC_BASE + 0x4C))

/* Flash access cycles: 4 is the setting for HCLK at or above 75 MHz. */
#define FMC_CYCCTL_CYCLE_Msk   (0xFUL << 0)
#define FMC_CYCCTL_CYCLE_96MHZ (4UL)

#define FMC_ISPCTL_ISPEN            (1UL << 0)
#define FMC_ISPCTL_APUEN            (1UL << 3)
#define FMC_ISPCTL_ISPFF            (1UL << 6)
#define FMC_ISPTRG_ISPGO            (1UL << 0)

#define FMC_ISPCMD_PROGRAM          (0x21UL)
#define FMC_ISPCMD_PAGE_ERASE       (0x22UL)
#define FMC_ISPCMD_PROGRAM_MUL      (0x27UL)

/* Multi-word program: 16 bytes per ISP command instead of four. */
#define FMC_MPDAT0     (*(volatile uint32_t *)(FMC_BASE + 0x80))
#define FMC_MPDAT1     (*(volatile uint32_t *)(FMC_BASE + 0x84))
#define FMC_MPDAT2     (*(volatile uint32_t *)(FMC_BASE + 0x88))
#define FMC_MPDAT3     (*(volatile uint32_t *)(FMC_BASE + 0x8C))
#define FMC_MULTI_WORD_ALIGN        (16UL)

/*** SCU (TrustZone peripheral/SRAM attribution) ***/
#define SCU_PNSSET(n)  (*(volatile uint32_t *)(SCU_BASE + 0x000 + ((n) * 4)))
#define SCU_SRAMNSSET  (*(volatile uint32_t *)(SCU_BASE + 0x024))
#define SCU_IONSSET(n) (*(volatile uint32_t *)(SCU_BASE + 0x140 + ((n) * 4)))
/* Read-only live view of NSCBA, as a plain size. */
#define SCU_FNSADDR    (*(volatile uint32_t *)(SCU_BASE + 0x028))

/* SCU peripheral attribution index for UART0: PNSSET[3] bit 16 */
#define SCU_UART0_ATTR         (96 + 16)
/* IONSSET is indexed per GPIO port; port A is index 0 */
#define SCU_PORTA_INDEX        (0)
#define SCU_SRAM_BLOCK_SIZE    (16384UL)
#define SCU_SRAM_BLOCKS        (16)

/* System reset, used by the test application */
#define AIRCR                  (*(volatile uint32_t *)(0xE000ED0CUL))
#define AIRCR_VKEY             (0x05FAUL << 16)
#define AIRCR_SYSRESETREQ      (1UL << 2)

/* ARMv8-M NVIC interrupt target non-secure registers */
#define NVIC_ITNS(n)   (*(volatile uint32_t *)(0xE000E380UL + ((n) * 4)))
#define UART0_IRQn             (36)

/* Register write-protection key sequence */
#define SYS_UNLOCK() do {          \
        SYS_REGLCTL = 0x59UL;      \
        SYS_REGLCTL = 0x16UL;      \
        SYS_REGLCTL = 0x88UL;      \
    } while (SYS_REGLCTL == 0UL)

#define SYS_LOCK() do { SYS_REGLCTL = 0UL; } while (0)

#endif /* M2354_DEF_INCLUDED */
