/* rx65n.c
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

/* HAL for Renesas RX65N */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "user_settings.h"

#include <target.h>
#include "hal.h"
#include "hal/renesas-rx.h"

#define SYS_CLK (240000000) /* 240MHz */
#define PCLKB   (60000000)  /* 60MHz */

/* System Registers */
#define SYSTEM_BASE (0x80000)

#define SYS_SYSCR0  (*(volatile uint16_t *)(SYSTEM_BASE + 0x06))
#define SYS_SYSCR0_EXBE ENDIAN_BIT16(1) /* External Bus Enable */

#define SYS_MSTPCRB (*(volatile uint32_t *)(SYSTEM_BASE + 0x14)) /* Module Stop Control 0=release, 1=stop */

#define SYS_SCKCR   (*(volatile uint32_t *)(SYSTEM_BASE + 0x20)) /* System Clock Control Register */
#define SYS_SCKCR_FCK(n)  ENDIAN_VAL32(n, 28)
#define SYS_SCKCR_ICK(n)  ENDIAN_VAL32(n, 24)
#define SYS_SCKCR_PSTOP1  ENDIAN_BIT32(23)
#define SYS_SCKCR_PSTOP0  ENDIAN_BIT32(22)
#define SYS_SCKCR_BCK(n)  ENDIAN_VAL32(n, 16)
#define SYS_SCKCR_PCKA(n) ENDIAN_VAL32(n, 12)
#define SYS_SCKCR_PCKB(n) ENDIAN_VAL32(n,  8)
#define SYS_SCKCR_PCKC(n) ENDIAN_VAL32(n,  4)
#define SYS_SCKCR_PCKD(n) ENDIAN_VAL32(n,  0)

#define SYS_SCKCR2 (*(volatile uint16_t *)(SYSTEM_BASE + 0x24)) /* System Clock Control Register 2 */
#define SYS_SCKCR2_UCK(n) ENDIAN_VAL16(n, 4)

#define SYS_SCKCR3 (*(volatile uint16_t *)(SYSTEM_BASE + 0x26)) /* System Clock Control Register 3 */
#define SYS_SCKCR3_CKSEL(n) ENDIAN_VAL16(n, 8) /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */

#define SYS_PLLCR (*(volatile uint16_t *)(SYSTEM_BASE + 0x28))
#define SYS_PLLCR_PLIDIV(n) ENDIAN_VAL16(n, 0) /* 0=x1, 1=x1/2, 2=x1/3 */
#define SYS_PLLCR_PLLSRCSEL ENDIAN_BIT16(4)    /* 0=main, 1=HOCO */
#define SYS_PLLCR_STC(n)    ENDIAN_VAL16(n, 8) /* Frequency Multiplication Factor */

#define SYS_PLLCR2 (*(volatile uint8_t *)(SYSTEM_BASE + 0x2A))
#define SYS_PLLCR2_PLLEN ENDIAN_BIT8(0) /* PLL Stop Control: 0=PLL operating, 1=PLL stopped */

#define SYS_BCKCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x30))
#define SYS_BCKCR_BCLKDIV ENDIAN_BIT8(0) /* 0=BCLK, 1= 1/2 BCLK */

#define SYS_MOSCCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x32))
#define SYS_MOSCCR_MOSTP ENDIAN_BIT8(0) /* Main-clock osc: 0=operating, 1=stopped */

#define SYS_SOSCCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x33)) /* Sub-Clock Oscillator Control */
#define SYS_SOSCCR_SOSTP ENDIAN_BIT8(0) /* Sub-clock osc: 0=operating, 1=stopped */

#define SYS_LOCOCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x34))
#define SYS_LOCOCR_LCSTP ENDIAN_BIT8(0) /* Low-Speed On-Chip Oscillator Control: 0=On, 1=Off */

#define SYS_HOCOCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x36))
#define SYS_HOCOCR_HCSTP     ENDIAN_BIT8(0) /* High Speed On-Chip Osc - 1=STOPPED */
#define SYS_HOCOCR2  (*(volatile uint8_t *)(SYSTEM_BASE + 0x37))
#define SYS_HOCOCR2_HCFRQ(n) ENDIAN_VAL8(n, 0) /* 0=16MHz, 1=18MHz, 2=20MHz */

#define SYS_OSCOVFSR (*(volatile uint8_t *)(SYSTEM_BASE + 0x3C))
#define SYS_OSCOVFSR_MOOVF  ENDIAN_BIT8(0) /* Main clock */
#define SYS_OSCOVFSR_SOOVF  ENDIAN_BIT8(1) /* Sub clock */
#define SYS_OSCOVFSR_PLOVF  ENDIAN_BIT8(2) /* PLL */
#define SYS_OSCOVFSR_HCOVF  ENDIAN_BIT8(3) /* HOCO */
#define SYS_OSCOVFSR_ILCOVF ENDIAN_BIT8(4) /* IWDT */

#define SYS_MOSCWTCR (*(volatile uint8_t *)(SYSTEM_BASE + 0xA2))
#define SYS_MOSCWTCR_MSTS(n) ENDIAN_VAL8(n, 0)

/* Register Write Protection Function */
#define SYS_PRCR       (*(volatile uint16_t *)(SYSTEM_BASE + 0x3FE))
#define SYS_PRCR_PRKEY (0xA500)
#define SYS_PRCR_PRC0  ENDIAN_BIT16(0) /* Enables writing to clock generation circuit */
#define SYS_PRCR_PRC1  ENDIAN_BIT16(1) /* Enables writing to operating modes, clock R/W generation circuit, low power consumption, and software reset */
#define SYS_PRCR_PRC3  ENDIAN_BIT16(3) /* Enables writing to LVD */

#define PROTECT_OFF() SYS_PRCR = (SYS_PRCR_PRKEY | SYS_PRCR_PRC0 | SYS_PRCR_PRC1 | SYS_PRCR_PRC3)
#define PROTECT_ON()  SYS_PRCR = (SYS_PRCR_PRKEY)

#define SYS_MOFCR   (*(volatile uint8_t *)(0x8C293))
#define SYS_MOFCR_MOFXIN    ENDIAN_BIT8(0)    /* OSC Force Oscillation: 0=not controlled, 1=main clock forced */
#define SYS_MOFCR_MODRV2(n) ENDIAN_VAL8(n, 4) /* OSC MHz: 0=20.1-24, 1=16.1-20, 2=8.1-16, 3=8 */
#define SYS_MOFCR_MOSEL     ENDIAN_BIT8(6)    /* 0=resonator, 1=external clk in*/

#define SYS_HOCOPCR  (*(volatile uint8_t *)(0x8C294))
#define SYS_HOCOPCR_HOCOPCNT ENDIAN_BIT8(0) /* High-Speed On-Chip Oscillator Power Supply Control: 0=On, 1=Off */

#define SYS_RSTSR1  (*(volatile uint8_t *)(0x8C291))
#define SYS_RSTSR1_CWSF ENDIAN_BIT8(0) /* 0=Cold Start, 1=Warm Start */

/* RTC */
#define RTC_BASE 0x8C400
#define RTC_RCR3 (*(volatile uint8_t *)(RTC_BASE + 0x26))
#define RTC_RCR3_RTCEN    ENDIAN_BIT8(0) /* Sub Clock Osc: 0=stopped, 1=operating */
#define RTC_RCR3_RTCDV(n) ENDIAN_VAL8(n, 1)
#define RTC_RCR4 (*(volatile uint8_t *)(RTC_BASE + 0x28))
#define RTC_RCR4_RCKSEL   ENDIAN_BIT8(0) /* 0=Sub Clock, 1=Main Clock */

/* Flash */
#define FLASH_BASE  0x81000
#define FLASH_ROMWT (*(volatile uint8_t *)(FLASH_BASE + 0x1C))
#define FLASH_ROMWT_ROMWT(n) ENDIAN_VAL8(n, 0) /* 0=no wait, 1=one wait cycle, 2=two wait cycles */

/* Serial Communication Interface */
#define SCI_BASE(n) (0x8A000 + ((n) * 0x20))
#define SCI_SMR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x00))
#define SCI_SMR_CKS(clk) (clk & 0x3) /* 0=PCLK, 1=PCLK/4, 2=PCLK/16, 3=PCLK/64 */
#define SCI_SMR_STOP  ENDIAN_BIT8(3) /* 0=1 stop bit */
#define SCI_SMR_CHR   ENDIAN_BIT8(6) /* 0=8-bit */
#define SCI_BRR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x01)) /* Bit Rate Reg < 255 */
#define SCI_SCR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x02))
#define SCI_SCR_RE    ENDIAN_BIT8(4)
#define SCI_SCR_TE    ENDIAN_BIT8(5)
#define SCI_TDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x03)) /* Transmit Data Register */
#define SCI_SSR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x04))
#define SCI_SSR_TEND  ENDIAN_BIT8(2) /* Transmit End Flag */
#define SCI_SSR_RDRF  ENDIAN_BIT8(6) /* Receive Data Full Flag */
#define SCI_SSR_TDRE  ENDIAN_BIT8(7) /* Transmit Data Empty Flag */
#define SCI_RDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x05)) /* Receive Data Register */
#define SCI_SCMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x06))
#define SCI_SCMR_CHR1 ENDIAN_BIT8(4) /* 1=8-bit */
#define SCI_SEMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x08))
#define SCI_SEMR_ASC0    ENDIAN_BIT8(0) /* Asynchronous Mode Clock Source Select 0=external clock input */
#define SCI_SEMR_BRME    ENDIAN_BIT8(2) /* Bit Rate Modulation Enable */
#define SCI_SEMR_ABCS    ENDIAN_BIT8(4) /* Asynchronous Mode Base Clock Select */
#define SCI_SEMR_NFEN    ENDIAN_BIT8(5) /* Digital Noise Filter Function Enable */
#define SCI_SEMR_BGDM    ENDIAN_BIT8(6) /* Baud Rate Generator Double-Speed Mode Select */
#define SCI_SEMR_RXDESEL ENDIAN_BIT8(7) /* Asynchronous Start Bit Edge Detection Select */

/* MPC (Multi-Function Pin Controller) */
#define MPC_PWPR   (*(volatile uint8_t *)(0x8C11F))
#define MPC_PWPR_B0WI  ENDIAN_BIT8(7)
#define MPC_PWPR_PFSWE ENDIAN_BIT8(6)

#define MPC_PFS(n) (*(volatile uint8_t *)(0x8C0E0 + (n)))

/* Ports */
#define PORT_BASE(n)  (0x8C000 + (n))
#define PORT_PDR(n)   (*(volatile uint8_t*)(0x8C000 + (n)))
#define PORT_PMR(n)   (*(volatile uint8_t*)(0x8C060 + (n))) /* 0=General, 1=Peripheral */


static void hal_delay_us(uint32_t us)
{
    uint32_t delay;
    for (delay = 0; delay < (us * 240); delay++) {
        RX_NOP();
    }
}

#ifdef DEBUG_UART

/* Use SCI5 on PC3 at 115200 baud */
#ifndef DEBUG_UART_SCI
#define DEBUG_UART_SCI 5
#endif
#ifndef DEBUG_BAUD_RATE
#define DEBUG_BAUD_RATE 115200
#endif

void uart_init(void)
{
    /* Release SCI5 module stop (clear bit) */
    /* bit 31=SCI0, 30=SCI1, 29=SCI2, 28=SCI3, 27=SCI4, 26=SCI5, 25=SCI6, 24=SCI7 */
    PROTECT_OFF();
    SYS_MSTPCRB &= ~ENDIAN_BIT32(26);
    PROTECT_ON();

    /* Disable RX/TX */
    SCI_SCR(DEBUG_UART_SCI) = 0;

    /* Configure PC3 for UART (TXD5) and PC2 UART (RXD5) */
    PORT_PMR(0xC) |= (ENDIAN_BIT32(2) | ENDIAN_BIT32(3));

    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |=  MPC_PWPR_PFSWE;

    /* Enable TXD5/RXD5 */
    /* SCI5 Function Select = 0xA */
    MPC_PFS(0xC2) = 0xA; /* RXD5 */
    MPC_PFS(0xC3) = 0xA; /* TXD5 */

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |=   MPC_PWPR_PFSWE;

    /* baud rate table: */
    /* divisor, abcs, bgdm, cks
     * 8,       1,    1,    0
     * 16,      0,    1,    0
     * 32,      0,    0,    0
     * 64,      0,    1,    1
     * 128,     0,    0,    1
     * 256,     0,    1,    2
     * 512,     0,    0,    2 (using this one)
     * 1024,    0,    1,    3
     * 2048,    0,    0,    3
     */

    /* 8-bit, 1-stop, no parity, cks=2 (/512), bgdm=0, abcs=0 */
    SCI_BRR(DEBUG_UART_SCI) = (PCLKB / (512 * DEBUG_BAUD_RATE)) - 1;
    SCI_SEMR(DEBUG_UART_SCI) &= ~SCI_SEMR_ABCS;
    SCI_SEMR(DEBUG_UART_SCI) &= ~SCI_SEMR_BGDM;
    SCI_SMR(DEBUG_UART_SCI) = SCI_SMR_CKS(2);
    SCI_SCMR(DEBUG_UART_SCI) |= SCI_SCMR_CHR1;
    /* Enable TX/RX */
    SCI_SCR(DEBUG_UART_SCI) = (SCI_SCR_RE | SCI_SCR_TE);
}
void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
            SCI_TDR(DEBUG_UART_SCI) = '\r';
        }
        while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
        SCI_TDR(DEBUG_UART_SCI) = c;
    }
}
#endif /* DEBUG_UART */

/* LOCO clock is used out of reset */
/* This function will switch to using on-chip HOCO through PLL at 240MHz */
#define CFG_CKSEL 1 /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */
#define CFD_PLL_DIV (0)
#define CFG_PLL_MUL (15.0)

void hal_clk_init(void)
{
    uint32_t reg, i;
    uint16_t stc;
    uint8_t  cksel = CFG_CKSEL;

    PROTECT_OFF(); /* write protect off */

    /* ---- High Speed OSC (HOCO) ---- */
#if CFG_CKSEL == 1
    if (SYS_HOCOCR & SYS_HOCOCR_HCSTP) {
        /* Turn on power to HOCO */
        SYS_HOCOPCR &= ~SYS_HOCOPCR_HOCOPCNT;
        /* Stop HOCO */
        SYS_HOCOCR |= SYS_HOCOCR_HCSTP;
        /* Wait for HOCO to stop */
        while (SYS_OSCOVFSR & SYS_OSCOVFSR_HCOVF) { RX_NOP(); }

        /* Set 16MHz */
        SYS_HOCOCR2 = SYS_HOCOCR2_HCFRQ(0);

        /* Enable HOCO */
        SYS_HOCOCR &= ~SYS_HOCOCR_HCSTP;
        reg = SYS_HOCOCR; /* dummy ready (required) */
    }
    /* Wait for HOCO oscisllator stabilization */
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_HCOVF) == 0) { RX_NOP(); }
#else
	if (SYS_HOCOCR & SYS_HOCOCR_HCSTP) {
    	/* Turn off power to HOCO */
	    SYS_HOCOPCR |= SYS_HOCOPCR_HOCOPCNT;
	}
#endif

    /* ---- Main-Clock ---- */
#if CFG_CKSEL == 2
	/* MOFXIN=0 (not controlled), MODRV2=0 (24MHz), MOSEL=0 (resonator) */
    SYS_MOFCR = 0;

    /* OSC stabilization time (9.98 ms * (264 kHZ) + 16)/32 = 82.83) */
    SYS_MOSCWTCR = SYS_MOSCWTCR_MSTS(83);

    /* Enable Main OSC */
    SYS_MOSCCR = 0;
    reg = SYS_MOSCCR; /* dummy read (required) */
    while (SYS_MOSCCR != 0) { RX_NOP(); }
#else
    /* Stop main clock */
    SYS_MOSCCR = SYS_MOSCCR_MOSTP;
    reg = SYS_MOSCCR; /* dummy read (required) */
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_MOOVF) != 0) { RX_NOP(); }
#endif

    /* ---- RTC Clock ---- */
    if ((SYS_RSTSR1 & SYS_RSTSR1_CWSF) == 0) { /* cold start */
        /* Stop the RTC sub-clock */
        RTC_RCR4 = 0;
        for (i=0; i<4; i++) {
            reg = RTC_RCR4; /* dummy read (required) */
        }
        while (RTC_RCR4 != 0) { RX_NOP(); }
        RTC_RCR3 = 0;
        for (i=0; i<4; i++) {
            reg = RTC_RCR3; /* dummy read (required) */
        }
        while (RTC_RCR3 != 0) { RX_NOP(); }
    }

    /* ---- Sub-Clock OSC ---- */
#if CFG_CKSEL == 3
    /* TODO: Add support for running from sub-clock */
#else
    /* Stop the sub-clock */
    SYS_SOSCCR = SYS_SOSCCR_SOSTP;
    reg = SYS_SOSCCR; /* dummy read (required) */
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_SOOVF) != 0) { RX_NOP(); }
#endif

#if CFG_CKSEL == 1 || CFG_CKSEL == 2
    /* ---- PLL ---- */
    /* Frequency Multiplication Factor */
    #if CFG_CKSEL == 2
        #define PLL_SRCSEL /* main */
    #else
        #define PLL_SRCSEL SYS_PLLCR_PLLSRCSEL /* HOCO */
    #endif
    #define PLL_MUL_STC (((uint8_t)((float)CFG_PLL_MUL * 2.0)) - 1)
    reg = (
        SYS_PLLCR_PLIDIV(CFD_PLL_DIV) | /* no div */
        PLL_SRCSEL |                    /* clock source (0=main, 1=HOCO */
        SYS_PLLCR_STC(PLL_MUL_STC)      /* multiplier */
    );
    SYS_PLLCR = reg;
    SYS_PLLCR2 = 0; /* enable PLL */
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_PLOVF) == 0) { RX_NOP(); }
    cksel = 4; /* PLL */
#endif

    /* ---- FLASH ---- */
    /* Flash Wait Cycles (1=50-100MHz, 2= >100MHz) */
    FLASH_ROMWT = FLASH_ROMWT_ROMWT(2);
    reg = FLASH_ROMWT;


    /* ---- Clock Select ---- */
    reg = (
        SYS_SCKCR_ICK(1)  | /* System Clock (ICK)=1:               1/2 = 120MHz */
        SYS_SCKCR_BCK(1)  | /* External Bus Clock (BCK)=1:         1/2 = 120MHz */
        SYS_SCKCR_FCK(2)  | /* Flash-IF Clock FCK=2:               1/4 = 60MHz */
        SYS_SCKCR_PCKA(1) | /* Peripheral Module Clock A (PCKA)=1: 1/2 = 120MHz */
        SYS_SCKCR_PCKB(2) | /* Peripheral Module Clock D (PCKB)=2: 1/4 = 60MHz */
        SYS_SCKCR_PCKC(2) | /* Peripheral Module Clock C (PCKC)=2: 1/4 = 60MHz */
        SYS_SCKCR_PCKD(2) | /* Peripheral Module Clock D (PCKD)=2: 1/4 = 60MHz */
        SYS_SCKCR_PSTOP1 |  /* BCLK Pin Output  (PSTOP1): 0=Disabled */
        SYS_SCKCR_PSTOP0    /* SDCLK Pin Output (PSTOP0): 0=Disabled */
    );
    SYS_SCKCR = reg;
    reg = SYS_SCKCR; /* dummy read (required) */

#if CFG_CKSEL == 2 /* USB only on main clock */
    /* USB Clock=4: 1/5 = 48MHz */
    SYS_SCKCR2 |= SYS_SCKCR2_UCK(4);
    reg = SYS_SCKCR2; /* dummy read (required) */
#endif

    /* Clock Source */
    SYS_SCKCR3 = SYS_SCKCR3_CKSEL(cksel);
    reg = SYS_SCKCR3; /* dummy read (required) */

    /* ---- Low Speed OSC (LOCO) ---- */
#if CFG_CKSEL != 0
    /* Disable on-chip Low Speed Oscillator */
    SYS_LOCOCR |= SYS_LOCOCR_LCSTP;
    hal_delay_us(25);
#endif

    PROTECT_ON(); /* write protect on */
}

void hal_init(void)
{
    hal_clk_init();
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif
    return;
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}
void hal_flash_unlock(void)
{
    return;
}
void hal_flash_lock(void)
{
    return;
}
void hal_prepare_boot(void)
{
    return;
}
