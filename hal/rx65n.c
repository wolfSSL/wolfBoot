/* rx65n.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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

/* HAL for Renesas RX65N */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "printf.h"

#include "user_settings.h"

#include <target.h>
#include "hal.h"
#include "hal/renesas-rx.h"

#define SYS_CLK (120000000) /* 120MHz */
#define PCLKA   (120000000) /* 120MHz */
#define PCLKB   (60000000)  /* 60MHz */

/* System Registers */
#define SYSTEM_BASE (0x80000)

#define SYS_SYSCR0  (*(volatile uint16_t *)(SYSTEM_BASE + 0x06))
#define SYS_SYSCR0_EXBE (1 << 1) /* External Bus Enable */

#define SYS_MSTPCRB (*(volatile uint32_t *)(SYSTEM_BASE + 0x14)) /* Module Stop Control 0=release, 1=stop */
#define SYS_MSTPCRC (*(volatile uint32_t *)(SYSTEM_BASE + 0x18)) /* Module Stop Control 0=release, 1=stop */

#define SYS_SCKCR   (*(volatile uint32_t *)(SYSTEM_BASE + 0x20)) /* System Clock Control Register */
#define SYS_SCKCR_FCK(n)  ((n) << 28)
#define SYS_SCKCR_ICK(n)  ((n) << 24)
#define SYS_SCKCR_PSTOP1    (1 << 23)
#define SYS_SCKCR_PSTOP0    (1 << 22)
#define SYS_SCKCR_BCK(n)  ((n) << 16)
#define SYS_SCKCR_PCKA(n) ((n) << 12)
#define SYS_SCKCR_PCKB(n) ((n) <<  8)
#define SYS_SCKCR_PCKC(n) ((n) <<  4)
#define SYS_SCKCR_PCKD(n) ((n) <<  0)

#define SYS_SCKCR2 (*(volatile uint16_t *)(SYSTEM_BASE + 0x24)) /* System Clock Control Register 2 */
#define SYS_SCKCR2_UCK(n) ((n) << 4)

#define SYS_SCKCR3 (*(volatile uint16_t *)(SYSTEM_BASE + 0x26)) /* System Clock Control Register 3 */
#define SYS_SCKCR3_CKSEL(n) ((n) << 8) /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */

#define SYS_PLLCR (*(volatile uint16_t *)(SYSTEM_BASE + 0x28))
#define SYS_PLLCR_PLIDIV(n) ((n) << 0) /* 0=x1, 1=x1/2, 2=x1/3 */
#define SYS_PLLCR_PLLSRCSEL   (1 << 4)    /* 0=main, 1=HOCO */
#define SYS_PLLCR_STC(n)    ((n) << 8) /* Frequency Multiplication Factor */

#define SYS_PLLCR2 (*(volatile uint8_t *)(SYSTEM_BASE + 0x2A))
#define SYS_PLLCR2_PLLEN  (1 << 0) /* PLL Stop Control: 0=PLL operating, 1=PLL stopped */

#define SYS_BCKCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x30))
#define SYS_BCKCR_BCLKDIV (1 << 0) /* 0=BCLK, 1= 1/2 BCLK */

#define SYS_MOSCCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x32))
#define SYS_MOSCCR_MOSTP (1 << 0) /* Main-clock osc: 0=operating, 1=stopped */

#define SYS_SOSCCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x33)) /* Sub-Clock Oscillator Control */
#define SYS_SOSCCR_SOSTP (1 << 0) /* Sub-clock osc: 0=operating, 1=stopped */

#define SYS_LOCOCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x34))
#define SYS_LOCOCR_LCSTP (1 << 0) /* Low-Speed On-Chip Oscillator Control: 0=On, 1=Off */

#define SYS_HOCOCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0x36))
#define SYS_HOCOCR_HCSTP (1 << 0) /* High Speed On-Chip Osc - 1=STOPPED */
#define SYS_HOCOCR2  (*(volatile uint8_t *)(SYSTEM_BASE + 0x37))
#define SYS_HOCOCR2_HCFRQ(n) ((n) << 0) /* 0=16MHz, 1=18MHz, 2=20MHz */

#define SYS_OSCOVFSR (*(volatile uint8_t *)(SYSTEM_BASE + 0x3C))
#define SYS_OSCOVFSR_MOOVF  (1 << 0) /* Main clock */
#define SYS_OSCOVFSR_SOOVF  (1 << 1) /* Sub clock */
#define SYS_OSCOVFSR_PLOVF  (1 << 2) /* PLL */
#define SYS_OSCOVFSR_HCOVF  (1 << 3) /* HOCO */
#define SYS_OSCOVFSR_ILCOVF (1 << 4) /* IWDT */

#define SYS_MOSCWTCR (*(volatile uint8_t *)(SYSTEM_BASE + 0xA2))
#define SYS_MOSCWTCR_MSTS(n) ((n) << 0)

/* Register Write Protection Function */
#define SYS_PRCR       (*(volatile uint16_t *)(SYSTEM_BASE + 0x3FE))
#define SYS_PRCR_PRKEY (0xA5 << 8)
#define SYS_PRCR_PRC0  (1 << 0) /* Enables writing to clock generation circuit */
#define SYS_PRCR_PRC1  (1 << 1) /* Enables writing to operating modes, clock R/W generation circuit, low power consumption, and software reset */
#define SYS_PRCR_PRC3  (1 << 3) /* Enables writing to LVD */

#define PROTECT_OFF() SYS_PRCR = (SYS_PRCR_PRKEY | SYS_PRCR_PRC0 | SYS_PRCR_PRC1 | SYS_PRCR_PRC3)
#define PROTECT_ON()  SYS_PRCR = (SYS_PRCR_PRKEY)

#define SYS_MOFCR   (*(volatile uint8_t *)(SYSTEM_BASE + 0xC293))
#define SYS_MOFCR_MOFXIN      (1 << 0)  /* OSC Force Oscillation: 0=not controlled, 1=main clock forced */
#define SYS_MOFCR_MODRV2(n) ((n) << 4)  /* OSC MHz: 0=20.1-24, 1=16.1-20, 2=8.1-16, 3=8 */
#define SYS_MOFCR_MOSEL       (1 << 6)  /* 0=resonator, 1=external clk in*/

#define SYS_HOCOPCR  (*(volatile uint8_t *)(SYSTEM_BASE + 0xC294))
#define SYS_HOCOPCR_HOCOPCNT (1 << 0) /* High-Speed On-Chip Oscillator Power Supply Control: 0=On, 1=Off */

#define SYS_RSTSR1  (*(volatile uint8_t *)(SYSTEM_BASE + 0xC291))
#define SYS_RSTSR1_CWSF (1 << 0) /* 0=Cold Start, 1=Warm Start */

/* RTC */
#define RTC_BASE (SYSTEM_BASE + 0xC400)
#define RTC_RCR3 (*(volatile uint8_t *)(RTC_BASE + 0x26))
#define RTC_RCR3_RTCEN      (1 << 0) /* Sub Clock Osc: 0=stopped, 1=operating */
#define RTC_RCR3_RTCDV(n) ((n) << 1)
#define RTC_RCR4 (*(volatile uint8_t *)(RTC_BASE + 0x28))
#define RTC_RCR4_RCKSEL     (1 << 0) /* 0=Sub Clock, 1=Main Clock */

/* Flash */
#define FLASH_BASE  0x81000
#define FLASH_ROMWT (*(volatile uint8_t *)(FLASH_BASE + 0x1C))
#define FLASH_ROMWT_ROMWT(n) ((n) << 0) /* 0=no wait, 1=one wait cycle, 2=two wait cycles */

/* Serial Communication Interface */
#define SCI_BASE(n) (SYSTEM_BASE + 0xA000 + ((n) * 0x20))
#define SCI_SMR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x00))
#define SCI_SMR_CKS(clk) (clk & 0x3) /* 0=PCLK, 1=PCLK/4, 2=PCLK/16, 3=PCLK/64 */
#define SCI_SMR_STOP  (1 << 3) /* 0=1 stop bit */
#define SCI_SMR_CHR   (1 << 6) /* 0=8-bit */
#define SCI_SMR_CM    (1 << 7) /* Mode: 0=Async/Simple I2C, 1=Sync/simple SPI */
#define SCI_BRR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x01)) /* Bit Rate Reg < 255 */
#define SCI_SCR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x02))
#define SCI_SCR_RE    (1 << 4)
#define SCI_SCR_TE    (1 << 5)
#define SCI_TDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x03)) /* Transmit Data Register */
#define SCI_SSR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x04))
#define SCI_SSR_TEND  (1 << 2) /* Transmit End Flag */
#define SCI_SSR_RDRF  (1 << 6) /* Receive Data Full Flag */
#define SCI_SSR_TDRE  (1 << 7) /* Transmit Data Empty Flag */
#define SCI_RDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x05)) /* Receive Data Register */
#define SCI_SCMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x06))
#define SCI_SCMR_CHR1 (1 << 4) /* 1=8-bit */
#define SCI_SCMR_SDIR (1 << 3) /* Transmitted/Received Data Transfer Direction */
#define SCI_SCMR_SINV (1 << 2) /* Transmitted/Received Data Invert */

#define SCI_SEMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x08))
#define SCI_SEMR_ASC0    (1 << 0) /* Asynchronous Mode Clock Source Select 0=external clock input */
#define SCI_SEMR_BRME    (1 << 2) /* Bit Rate Modulation Enable */
#define SCI_SEMR_ABCS    (1 << 4) /* Asynchronous Mode Base Clock Select */
#define SCI_SEMR_NFEN    (1 << 5) /* Digital Noise Filter Function Enable */
#define SCI_SEMR_BGDM    (1 << 6) /* Baud Rate Generator Double-Speed Mode Select */
#define SCI_SEMR_RXDESEL (1 << 7) /* Asynchronous Start Bit Edge Detection Select */

/* SPI */
#define SCI_SPMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x0D))
#define SCI_SPMR_SSE   (1 << 0) /* 0=SSn# pin func disabled, 1=enabled */
#define SCI_SPMR_MSS   (1 << 2) /* Master slave select: 0=master, 1=slave */
#define SCI_SPMR_CKPOL (1 << 6) /* Clock Polarity: 0=not inverted, 1=inverted */
#define SCI_SPMR_CKPH  (1 << 7) /* Clock Phase: 0=not delayed, 1=delayed */

/* MPC (Multi-Function Pin Controller) */
#define MPC_PWPR   (*(volatile uint8_t *)(SYSTEM_BASE + 0xC11F))
#define MPC_PWPR_B0WI  (1 << 7)
#define MPC_PWPR_PFSWE (1 << 6)

#define MPC_PFS(n) (*(volatile uint8_t *)(SYSTEM_BASE + 0xC0E0 + (n)))

/* Ports */
#define PORT_BASE    (SYSTEM_BASE + 0xC000)
#define PORT_PDR(n)  (*(volatile uint8_t*)(PORT_BASE + 0x00 + (n))) /* Port Direction Register: 0=Input, 1=Output */
#define PORT_PODR(n) (*(volatile uint8_t*)(PORT_BASE + 0x20 + (n))) /* Port Output Data Register: 0=Low, 1=High */
#define PORT_PIDR(n) (*(volatile uint8_t*)(PORT_BASE + 0x40 + (n))) /* Port Input Register: 0=Low input, 1=Hight Input */
#define PORT_PMR(n)  (*(volatile uint8_t*)(PORT_BASE + 0x60 + (n))) /* Port Mode Register: 0=General, 1=Peripheral */
#define PORT_ODR(n)  (*(volatile uint8_t*)(PORT_BASE + 0x80 + (n))) /* Open-Drain Control Register: 0=CMOS, 1=NMOS open-drain */
#define PORT_PCR(n)  (*(volatile uint8_t*)(PORT_BASE + 0xC0 + (n))) /* Pull-Up Resistor Control Register: 0=Disable pull-up, 1=Enable input pull-up */
#define PORT_DSCR(n) (*(volatile uint8_t*)(PORT_BASE + 0xE0 + (n))) /* Drive Capacity Control Register: 0=Normal, 1=High-drive output */

/* RSPI */
#define RSPI_BASE(n)     (SYSTEM_BASE + 0x50100 + ((n) * 0x40)) /* n=0-2 (RSPI0,RSPI1,RSPI2) */
#define RSPI_SPCR(n)     (*(volatile uint8_t *)(RSPI_BASE(n) + 0x00)) /* Control */
#define RSPI_SPCR_SPMS      (1 << 0) /* RSPI Mode Select 0=SPI operation (4-wire method) */
#define RSPI_SPCR_TXMD      (1 << 1)
#define RSPI_SPCR_MSTR      (1 << 3) /* 0=Slave, 1=Master */
#define RSPI_SPCR_SPE       (1 << 6) /* 1=Enable RSPI */
#define RSPI_SPPCR(n)    (*(volatile uint8_t *)(RSPI_BASE(n) + 0x02)) /* Pin Control */
#define RSPI_SPPCR_MOIFV    (1 << 4) /* MOSI Idle Fixed Value */
#define RSPI_SPPCR_MOIDE    (1 << 5) /* MOSI Idle Value Fixing Enable */
#define RSPI_SPSR(n)     (*(volatile uint8_t *)(RSPI_BASE(n) + 0x03)) /* Status */
#define RSPI_SPSR_OVRF      (1 << 0) /* Overrun Error Flag */
#define RSPI_SPSR_IDLNF     (1 << 1) /* Idle Flag */
#define RSPI_SPSR_MODF      (1 << 2) /* Mode Fault Error Flag */
#define RSPI_SPSR_PERF      (1 << 3) /* Parity Error Flag */
#define RSPI_SPSR_UDRF      (1 << 4) /* Underrun Error Flag */
#define RSPI_SPSR_SPTEF     (1 << 5) /* Transmit Buffer Empty Flag */
#define RSPI_SPSR_SPRF      (1 << 7) /* Receive Buffer Full Flag */
#define RSPI_SPSR8(n)    (*(volatile uint8_t  *)(RSPI_BASE(n) + 0x04)) /* Data */
#define RSPI_SPSR16(n)   (*(volatile uint16_t *)(RSPI_BASE(n) + 0x04)) /* Data */
#define RSPI_SPSR32(n)   (*(volatile uint32_t *)(RSPI_BASE(n) + 0x04)) /* Data */
#define RSPI_SPSCR(n)    (*(volatile uint32_t *)(RSPI_BASE(n) + 0x08)) /* Sequence Control */
#define RSPI_SPSCR_SPSLN(s) ((s) & 0x7) /* Sequence Length Specification: 0=seq len 1 */
#define RSPI_SPBR(n)     (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0A)) /* Bit Rate = PCLKA / (2 x (n + 1) X 2^n): 1=30 Mbps, 2=20Mpbs, 3=15Mbps, 4=12Mbps, 5=10Mbps */
#define RSPI_SPDCR(n)    (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0B)) /* Data Control */
#define RSPI_SPDCR_SPFC(f)  ((f) & 0x3) /* Number of Frames Specification: 0=1 frame, 1=2 frames */
#define RSPI_SPDCR_SPRDTD   (1 << 4) /* Receive/Transmit Data Select */
#define RSPI_SPDCR_SPLW     (1 << 5) /* Longword Access/ Word Access Specification */
#define RSPI_SPDCR_SPBYT    (1 << 6) /* Byte Access Specification */
#define RSPI_SPCKD(n)    (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0C)) /* Clock Delay */
#define RSPI_SPCKD_SCKDL(d) ((d) & 0x7) /* RSPCK Delay Setting: 0=1 RSPCK, 1=2 RSPCK */
#define RSPI_SSLND(n)    (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0D)) /* Slave Select Negation Delay */
#define RSPI_SSLND_SLNDL(d) ((d) & 0x7) /* RSPCK Delay Setting: 0=1 RSPCK, 1=2 RSPCK */
#define RSPI_SPND(n)     (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0D)) /* Next-Access Delay */
#define RSPI_SPND_SPNDL(d)  ((d) & 0x7) /*  Next-Access Delay Setting: 0=1RSPCK+2PCLK, 1=2RSPCK+2PCLK */
#define RSPI_SPCR2(n)    (*(volatile uint8_t *)(RSPI_BASE(n) + 0x0F)) /* Control 2 */
#define RSPI_SPCR2_SPPE     (1 << 0) /* Parity Enable */
#define RSPI_SPCR2_SPOE     (1 << 1) /* Parity Mode */
#define RSPI_SPCMD(n, m) (*(volatile uint16_t *)(RSPI_BASE(n) + 0x10 + (((m) & 0x7)*2))) /* Command Register m */
#define RSPI_SPCMD_CPHA     (1 << 0) /* Phase: 0=Data sampling on odd edge, data variation on even edge */
#define RSPI_SPCMD_CPOL     (1 << 1) /* Polarity: 0=RSPCK is low when idle, 1=High when idle */
#define RSPI_SPCMD_BRDV(d)  (((d) & 0x3) << 2) /* Bit Rate Division : 0=none,1=div2,2=div4,3=div8 */
#define RSPI_SPCMD_SSLA(s)  (((s) & 0x7) << 4) /* Signal Assert 0=SSL0, 1=SSL1 */
#define RSPI_SPCMD_SSLKP    (1 << 7) /* Signal Level Keeping: 0=Deassert on transfer complete, 1=Keep asserted */
#define RSPI_SPCMD_SPB(l)   (((l) & 0xF) << 8) /* Data Length: 7=8bits, 15=16=bits, 1=24bits, 3=32bits */
#define RSPI_SPCMD_LSBF     (1 << 12) /* LSB First: 0=MSB First, 1=LSB First */
#define RSPI_SPCMD_SPNDEN   (1 << 13) /* Next-Access Delay Enable */
#define RSPI_SPCMD_SLNDEN   (1 << 14) /* SSL Negation Delay Setting Enable */
#define RSPI_SPCMD_SCKDEN   (1 << 15) /* RSPCK Delay Setting Enable */
#define RSPI_SPDCR2(n)   (*(volatile uint8_t *)(RSPI_BASE(n) + 0x20)) /* Data Control 2 */
#define RSPI_SPDCR2_BYSW    (1 << 0) /* Byte Swap: 0=Byte swapping of SPDR data disabled, 1=Byte swapping of SPDR data enabled */


#if defined(EXT_FLASH) && defined(TEST_FLASH)
static int test_flash(void);
#endif

static void hal_delay_us(uint32_t us)
{
    uint32_t delay;
    for (delay = 0; delay < (us * 240); delay++) {
        RX_NOP();
    }
}

#ifdef SPI_FLASH

/* ISSI IS25WP256E: 4MB QSPI Serial Flash */
/* RSPI1: P27: RSPCKB-A, P26: MOSIB-A, P30: MISOB-A, P31 SSLB0-A */
#ifndef FLASH_RSPI_PORT
#define FLASH_RSPI_PORT 1 /* RSPI1 */
#endif
#ifndef FLASH_CLK_HZ
#define FLASH_CLK_HZ 15000000
#endif
#define FLASH_SPI_USE_HW_CS
void spi_init(int polarity, int phase)
{
    /* Release RSPI1 module stop (clear bit) */
    /* SYS_MSTPCRB: bit 17=RSPI0, 16=RSPI1, SYS_MSTPCRC: bit 22=RSPI2 */
    PROTECT_OFF();
    SYS_MSTPCRB &= ~(1 << 16);
    PROTECT_ON();

    /* Configure P26-27 and P30-31 for alt mode */
    PORT_PMR(0x2) |= ((1 << 6) | (1 << 7));
    PORT_PMR(0x3) |= (1 << 0);
    PORT_PDR(0x3) &= ~(1 << 0); /* input */
#ifdef FLASH_SPI_USE_HW_CS
    PORT_PMR(0x3) |= (1 << 1);
#else
    PORT_PDR(0x3) |= (1 << 1); /* output */
#endif

    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |=  MPC_PWPR_PFSWE;

    /* Pin Function Select */
    MPC_PFS(0x76) = 0xD; /* P26 = MOSIB-A */
    MPC_PFS(0x77) = 0xD; /* P27 = RSPCKB-A */
    MPC_PFS(0x78) = 0xD; /* P30 = MISOB-A */
#ifdef FLASH_SPI_USE_HW_CS
    MPC_PFS(0x79) = 0xD; /* P31 = SSLB0-A */
#endif

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |=   MPC_PWPR_PFSWE;

    /* Configure RSPI */
    RSPI_SPPCR(FLASH_RSPI_PORT) = (RSPI_SPPCR_MOIFV | RSPI_SPPCR_MOIDE); /* enable idle fixing */
    RSPI_SPSCR(FLASH_RSPI_PORT) = RSPI_SPSCR_SPSLN(0); /* seq len 1 */
    RSPI_SPBR(FLASH_RSPI_PORT)  = 5; /* 5Mbps */
    RSPI_SPDCR(FLASH_RSPI_PORT) = (RSPI_SPDCR_SPFC(0) | RSPI_SPDCR_SPBYT); /* frames=1, SPDR=byte */
    RSPI_SPCKD(FLASH_RSPI_PORT) = RSPI_SPCKD_SCKDL(0); /* 1 clock delay (SSL assert and first clock cycle) */
    RSPI_SSLND(FLASH_RSPI_PORT) = RSPI_SSLND_SLNDL(0); /* 1 clock delay (last clock cycle and SSL negation) */
    RSPI_SPND(FLASH_RSPI_PORT)  = RSPI_SPND_SPNDL(0); /* Next-Access Delay: 1RSPCK+2PCLK */
    RSPI_SPCR2(FLASH_RSPI_PORT) = 0; /* no parity */
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) = (
        RSPI_SPCMD_BRDV(1) | /* div/1 */
        RSPI_SPCMD_SSLA(0) | /* slave select 0 */
        RSPI_SPCMD_SSLKP |   /* keep signal level between transfers */
        RSPI_SPCMD_SPB(7) |  /* 8-bit data */
        RSPI_SPCMD_SPNDEN |  /* enable Next-Access Delay */
        RSPI_SPCMD_SCKDEN    /* enable RSPCK Delay */
    );
    if (polarity)
        RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_CPOL;
    if (phase)
        RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_CPHA;

    /* Master SPI operation (4-wire method) */
    RSPI_SPCR(FLASH_RSPI_PORT) = RSPI_SPCR_MSTR;
}

void spi_release(void)
{
    /* Disable SPI master */
    RSPI_SPCR(FLASH_RSPI_PORT) &= ~RSPI_SPCR_SPE;
}

void spi_cs_on(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
#ifdef FLASH_SPI_USE_HW_CS
    /* Enable SPI Master */
    RSPI_SPCR(FLASH_RSPI_PORT) |= RSPI_SPCR_SPE;
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) |= RSPI_SPCMD_SSLKP;
#else
    PORT_PODR(0x3) &= ~(1 << 1); /* drive low */
#endif
}
void spi_cs_off(uint32_t base, int pin)
{
    (void)base;
    (void)pin;
#ifdef FLASH_SPI_USE_HW_CS
    RSPI_SPCMD(FLASH_RSPI_PORT, 0) &= ~RSPI_SPCMD_SSLKP;
    RSPI_SPCR(FLASH_RSPI_PORT) &= ~RSPI_SPCR_SPE;
#else
    PORT_PODR(0x3) |= (1 << 1); /* drive high */
#endif
}

void spi_write(const char byte)
{
    while ((RSPI_SPSR(FLASH_RSPI_PORT) & RSPI_SPSR_SPTEF) == 0);
    RSPI_SPSR8(FLASH_RSPI_PORT) = byte;
}
uint8_t spi_read(void)
{
    while ((RSPI_SPSR(FLASH_RSPI_PORT) & RSPI_SPSR_SPTEF) == 0);
    return RSPI_SPSR8(FLASH_RSPI_PORT);
}
#endif /* SPI_FLASH */


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
    SYS_MSTPCRB &= ~(1 << 26);
    PROTECT_ON();

    /* Disable RX/TX */
    SCI_SCR(DEBUG_UART_SCI) = 0;

    /* Configure PC3 for UART (TXD5) and PC2 UART (RXD5) */
    PORT_PMR(0xC) |= ((1 << 2) | (1 << 3));

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
void uart_write(const char* buf, unsigned int sz)
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
        RTC_RCR4 &= ~RTC_RCR4_RCKSEL; /* select sub-clock */
        for (i=0; i<4; i++) {
            reg = RTC_RCR4; /* dummy read (required) */
        }
        if ((RTC_RCR4 & RTC_RCR4_RCKSEL) != 0) { RX_NOP(); }
        RTC_RCR3 &= ~RTC_RCR3_RTCEN; /* stop osc */
        for (i=0; i<4; i++) {
            reg = RTC_RCR3; /* dummy read (required) */
        }
        if ((RTC_RCR3 & RTC_RCR3_RTCEN) != 0) { RX_NOP(); }
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
        PLL_SRCSEL |                    /* clock source (0=main, 1=HOCO) */
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
#if defined(EXT_FLASH) && defined(TEST_FLASH)
    if (test_flash() != 0) {
        wolfBoot_printf("Flash Test Failed!\n");
    }
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

#if defined(EXT_FLASH) && !defined(SPI_FLASH) && !defined(QSPI_FLASH)
void ext_flash_lock(void)
{
    /* no op */
}

void ext_flash_unlock(void)
{
    /* no op */
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    //memcpy(flash_base + address, data, len);
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    //memcpy(data, flash_base + address, len);
    return len;
}

int ext_flash_erase(uintptr_t address, int len)
{
    //memset(flash_base + address, FLASH_BYTE_ERASED, len);
    return 0;
}
#endif /* EXT_FLASH */

#if defined(EXT_FLASH) && defined(TEST_FLASH)
#ifndef TEST_ADDRESS
#define TEST_ADDRESS 0x200000 /* 2MB */
#endif
/* #define TEST_FLASH_READONLY */
static int test_flash(void)
{
    int ret;
    uint32_t i;
    uint32_t pageData[WOLFBOOT_SECTOR_SIZE/4]; /* force 32-bit alignment */

#ifndef TEST_FLASH_READONLY
    /* Erase sector */
    ret = ext_flash_erase(TEST_ADDRESS, WOLFBOOT_SECTOR_SIZE);
    wolfBoot_printf("Erase Sector: Ret %d\n", ret);

    /* Write Pages */
    for (i=0; i<sizeof(pageData); i++) {
        ((uint8_t*)pageData)[i] = (i & 0xff);
    }
    ret = ext_flash_write(TEST_ADDRESS, (uint8_t*)pageData, sizeof(pageData));
    wolfBoot_printf("Write Page: Ret %d\n", ret);
#endif /* !TEST_FLASH_READONLY */

    /* Read page */
    memset(pageData, 0, sizeof(pageData));
    ret = ext_flash_read(TEST_ADDRESS, (uint8_t*)pageData, sizeof(pageData));
    wolfBoot_printf("Read Page: Ret %d\n", ret);

    wolfBoot_printf("Checking...\n");
    /* Check data */
    for (i=0; i<sizeof(pageData); i++) {
        wolfBoot_printf("check[%3d] %02x\n", i, pageData[i]);
        if (((uint8_t*)pageData)[i] != (i & 0xff)) {
            wolfBoot_printf("Check Data @ %d failed\n", i);
            return -i;
        }
    }

    wolfBoot_printf("Flash Test Passed\n");
    return ret;
}
#endif /* EXT_FLASH && TEST_FLASH */
