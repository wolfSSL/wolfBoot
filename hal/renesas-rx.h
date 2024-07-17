/* renesas-rx.h
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

#ifndef _WOLFBOOT_RENESAS_RX_H_
#define _WOLFBOOT_RENESAS_RX_H_

#if defined(__CCRX__)
    #define RX_NOP() nop()
#elif defined(__GNUC__)
    #define RX_NOP() __asm("nop")
#elif defined(__ICCRX__)
    #define RX_NOP() __no_operation()
#endif

/* API's */
void hal_delay_us(uint32_t us);


/* Clocks */
#ifdef TARGET_rx72n
#define SYS_CLK (240000000) /* 240MHz */
#else
#define SYS_CLK (120000000) /* 120MHz */
#endif
#define PCLKA   (120000000) /* 120MHz */
#define PCLKB   (60000000)  /* 60MHz */
#define FCLK    (60000000)  /* 60MHz */

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
#define SYS_PLLCR_STC(n)    (((n) & 0x7F) << 8) /* Frequency Multiplication Factor */

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

#define SYS_SWRR (*(volatile uint16_t *)(SYSTEM_BASE + 0xC2))
#define SYS_SWRR_RESET 0xA501

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
#define FLASH_BASE  (SYSTEM_BASE + 0x1000)

#define FLASH_FWEPROR (*(volatile uint8_t *)(SYSTEM_BASE + 0xC296)) /* Flash P/E Protect Register */
#define FLASH_FWEPROR_FLWE 0x1 /* 0x01 = Program, block erase, and blank check are enabled */
#define FLASH_FWEPROR_FLWD 0x2 /* 0x02 = Program, block erase, and blank check are disabled */

#define FLASH_FCMDR  (*(volatile uint16_t *)(0x007FE0A0)) /* read only copy of two most recent FACI commands */

#define FLASH_FSTATR (*(volatile uint32_t *)(0x007FE080))
#define FLASH_FSTATR_FLWEERR   (1 << 6)  /* Flash P/E Protect Error Flag: 1=error */
#define FLASH_FSTATR_PRGSPD    (1 << 8)  /* Program Suspend Status Flag: 1=suspend */
#define FLASH_FSTATR_ERSSPD    (1 << 9)  /* Erase Suspend Status Flag */
#define FLASH_FSTATR_DBFULL    (1 << 10) /* Data Buffer Full Flag */
#define FLASH_FSTATR_SUSRDY    (1 << 11) /* Suspend Ready Flag */
#define FLASH_FSTATR_PRGERR    (1 << 12) /* Program Error Flag */
#define FLASH_FSTATR_ERSERR    (1 << 13) /* Erase Error Flag */
#define FLASH_FSTATR_ILGLERR   (1 << 14) /* Illegal Error Flag */
#define FLASH_FSTATR_FRDY      (1 << 15) /* Flash Ready Flag */
#define FLASH_FSTATR_OTERR     (1 << 20) /* Other Error Flag */
#define FLASH_FSTATR_SECERR    (1 << 21) /* Security Error Flag */
#define FLASH_FSTATR_FESETERR  (1 << 22) /* FENTRY Setting Error Flag */
#define FLASH_FSTATR_ILGCOMERR (1 << 23) /* Illegal Command Error Flag */

#define FLASH_FPCKAR (*(volatile uint16_t *)(0x007FE0E4))
#define FLASH_FPCKAR_KEY       (0x1E00)
#define FLASH_FPCKAR_PCKA(pck) ((pck) & 0xFF)

#define FLASH_FENTRYR (*(volatile uint16_t *)(0x007FE084))
#define FLASH_FENTRYR_KEY       (0xAA00)
#define FLASH_FENTRYR_CODE_READ (0)
#define FLASH_FENTRYR_CODE_PR   (1 << 0) /* Code Flash Memory P/E Mode Entry */
#define FLASH_FENTRYR_DATA_READ (0)
#define FLASH_FENTRYR_DATA_PE   (1 << 7) /* Data Flash Memory P/E Mode Entry */

#define FLASH_FAEINT (*(volatile uint8_t *)(0x007FE014))
#define FLASH_FAEINT_DFAEIE  (1 << 3)
#define FLASH_FAEINT_CMDLKIE (1 << 4)
#define FLASH_FAEINT_CFAEIE  (1 << 7)

#define FLASH_FSADDR (*(volatile uint32_t *)(0x007FE030))
#define FLASH_FEADDR (*(volatile uint32_t *)(0x007FE034))

#define FLASH_FAWMON (*(volatile uint32_t *)(0x007FE0DC))
#define FLASH_FAWMON_FAWS(a)  ((a) & 0xFFF)        /* Flash Access Window Start Address * 2 */
#define FLASH_FAWMON_FAWE(a) (((a) & 0xFFF) << 16) /* Flash access window end address */
#define FLASH_FAWMON_FSPR  (1 << 15) /* Access Window Protection: 1=with protection */
/* 0=FFFF C000h to FFFF DFFFh are used as the start-up area
 * 1=FFFF E000h to FFFF FFFFh are used as the start-up area
 */
#define FLASH_FAWMON_BTFLG (1UL << 31) /* Start-up Area Select */

#define FLASH_FACI_CMD_AREA (0x007E0000UL)
#define FLASH_FACI_CMD8  (*(volatile uint8_t  *)FLASH_FACI_CMD_AREA)
#define FLASH_FACI_CMD16 (*(volatile uint16_t *)FLASH_FACI_CMD_AREA)

#define FCU_RAM_AREA (0x007F8000)
#define FCU_RAM_SIZE (4096)

/* Target specific flash settings */
#ifdef TARGET_rx72n
    #define FLASH_SIZE 0x400000UL /* 4MB */

    #define FLASH_MEMWAIT (*(volatile uint8_t *)(FLASH_BASE + 0x1C))
    #define FLASH_MEMWAIT_MEMWAIT(n) ((n) << 0) /* 0=no wait, 1=one wait cycle (ICLK > 120MHz) */
#else
    #define FLASH_SIZE 0x200000UL /* 2MB */

    #define FLASH_ROMWT   (*(volatile uint8_t *)(FLASH_BASE + 0x1C))
    #define FLASH_ROMWT_ROMWT(n)     ((n) << 0) /* 0=no wait, 1=one wait cycle, 2=two wait cycles */
#endif

#define FLASH_BOOT_BLOCK_START (0xFFFF0000UL)
#define FLASH_BLOCK_SIZE_SMALL  (8192)
#define FLASH_BLOCK_SIZE_MEDIUM (32768)

#define FLASH_BLOCK_SIZE(addr) \
    (((addr) >= FLASH_BOOT_BLOCK_START) ? \
         FLASH_BLOCK_SIZE_SMALL : FLASH_BLOCK_SIZE_MEDIUM)

#define FLASH_ADDR (0xFFFFFFFFUL - FLASH_SIZE + 1)
#define IS_FLASH_ADDR(addr) ((addr) >= FLASH_ADDR ? 1 : 0)

/* FCAI Commands (RX72N RM Table 62.11) */
#define FLASH_FACI_CMD_PROGRAM                  0xE8
#define FLASH_FACI_CMD_PROGRAM_CODE_LENGTH      64
#define FLASH_FACI_CMD_PROGRAM_DATA_LENGTH      2
#define FLASH_FACI_CMD_BLOCK_ERASE              0x20
#define FLASH_FACI_CMD_PROGRAM_ERASE_SUSPEND    0xB0
#define FLASH_FACI_CMD_STATUS_CLEAR             0x50
#define FLASH_FACI_CMD_FORCED_STOP              0xB3
#define FLASH_FACI_CMD_BLANK_CHECK              0x71
#define FLASH_FACI_CMD_CONFIGURATION_SET        0x40
#define FLASH_FACI_CMD_CONFIGURATION_LENGTH     8
#define FLASH_FACI_CMD_LOCK_BIT_PROGRAM         0x77
#define FLASH_FACI_CMD_LOCK_BIT_READ            0x71
#define FLASH_FACI_CMD_FINAL                    0xD0


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

#ifdef TARGET_rx72n
#define MPC_PFS(n) (*(volatile uint8_t *)(SYSTEM_BASE + 0xC140 + (n)))
#else
#define MPC_PFS(n) (*(volatile uint8_t *)(SYSTEM_BASE + 0xC0E0 + (n)))
#endif

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

/* QSPI */
#define QSPI_BASE (SYSTEM_BASE + 0x9E00)
#define QSPI_SPCR           (*(volatile uint8_t *)(QSPI_BASE + 0x00)) /* QSPI Control Register */
#define QSPI_SPCR_MSTR      (1 << 3) /* 0=Slave, 1=Master */
#define QSPI_SPCR_SPE       (1 << 6) /* 1=Enable RSPI */
#define QSPI_SSLP           (*(volatile uint8_t *)(QSPI_BASE + 0x01)) /* QSPI Slave Select Polarity */
#define QSPI_SSLP_SSLP      (1 << 0) /* 0=active low (default), 1=active high */
#define QSPI_SPPCR       (*(volatile uint8_t *)(QSPI_BASE + 0x02)) /* Pin Control */
#define QSPI_SPPCR_IO2FV    (1 << 1) /* Single-/Dual-SPI Mode QIO2 */
#define QSPI_SPPCR_IO3FV    (1 << 2) /* Single-/Dual-SPI Mode QIO3 */
#define QSPI_SPPCR_MOIFV    (1 << 4) /* MOSI Idle Fixed Value */
#define QSPI_SPPCR_MOIDE    (1 << 5) /* MOSI Idle Value Fixing Enable */
#define QSPI_SPSR        (*(volatile uint8_t *)(QSPI_BASE + 0x03)) /* Status */
#define QSPI_SPSR_SPSSLF    (1 << 4) /* QSSL Negation Flag */
#define QSPI_SPSR_SPTEF     (1 << 5) /* Transmit Buffer Empty Flag */
#define QSPI_SPSR_TREND     (1 << 6) /* Transmit End Flag */
#define QSPI_SPSR_SPRFF     (1 << 7) /* Receive Buffer Full Flag */
#define QSPI_SPDR8       (*(volatile uint8_t  *)(QSPI_BASE + 0x04)) /* Data */
#define QSPI_SPDR16      (*(volatile uint16_t *)(QSPI_BASE + 0x04)) /* Data */
#define QSPI_SPDR32      (*(volatile uint32_t *)(QSPI_BASE + 0x04)) /* Data */
#define QSPI_SPSCR       (*(volatile uint8_t *)(QSPI_BASE + 0x08)) /* Sequence Control */
#define QSPI_SPSCR_SPSC(s) ((s) & 0x3) /* Number of SPCMDn register to be referenced (n = 0 to 3) */
#define QSPI_SPSSR       (*(volatile uint8_t *)(QSPI_BASE + 0x09)) /* Sequence Status */
#define QSPI_SPSSR_MASK  (0x3) /* Sequence Status: 0=SPCMD0, 1=SPCMD1, 2=SPCMD2, 3=SPCMD3 */
#define QSPI_SPBR        (*(volatile uint8_t *)(QSPI_BASE + 0x0A)) /* Bit Rate = f(PCLK) / (2 x m x 2^n): 1=30 Mbps, 2=15Mpbs, 3=10Mbps, 4=7.5Mbps, 5=6Mbps */
#define QSPI_SPDCR       (*(volatile uint8_t *)(QSPI_BASE + 0x0B)) /* Data Control */
#define QSPI_SPDCR_TXDMY    (1 << 7) /* Dummy Data Transmission Enable */
#define QSPI_SPCKD       (*(volatile uint8_t *)(QSPI_BASE + 0x0C)) /* Clock Delay */
#define QSPI_SPCKD_SCKDL(d) ((d) & 0x7) /* Clock Delay Setting: 0=1.5, 1=2.5, 3=3.5 QSPCLK */
#define QSPI_SSLND       (*(volatile uint8_t *)(QSPI_BASE + 0x0D)) /* Slave Select Negation Delay */
#define QSPI_SSLND_SLNDL(d) ((d) & 0x7) /* QSSL Neg Delay Setting: 0=1, 1=2 QSPCLK */
#define QSPI_SPND        (*(volatile uint8_t *)(QSPI_BASE + 0x0D)) /* Next-Access Delay */
#define QSPI_SPND_SPNDL(d)  ((d) & 0x7) /*  Next-Access Delay Setting: 0=1, 1=2 QSPCLK */
#define QSPI_SPCMD(n)    (*(volatile uint16_t *)(QSPI_BASE + 0x10 + (((n) & 0x3) * 2))) /* Command Register */
#define QSPI_SPCMD_CPHA     (1 << 0) /* Phase: 0=Data sampling on odd edge, data variation on even edge */
#define QSPI_SPCMD_CPOL     (1 << 1) /* Polarity: 0=QSPCLK is low when idle, 1=High when idle */
#define QSPI_SPCMD_BRDV_MASK (0x3 << 2)
#define QSPI_SPCMD_BRDV(d)  (((d) & 0x3) << 2) /* Bit Rate Division : 0=none,1=div2,2=div4,3=div8 */
#define QSPI_SPCMD_SPRW     (1 << 4) /* SPI Read/Write Access: 0=Write operation (QIO1 and QIO0/QIO3 to QIO0: Output), 1=Read operation (QIO1 and QIO0/QIO3 to QIO0: Input) */
#define QSPI_SPCMD_SPREAD   QSPI_SPCMD_SPRW
#define QSPI_SPCMD_SPWRITE  0
#define QSPI_SPCMD_SPIMOD(n) (((n) & 0x3) << 5) /* SPI Operating Mode: 0=Single SPI, 1=Dual SPI, 2=Quad SPI */
#define QSPI_SPCMD_SSLKP    (1 << 7) /* Signal Level Keeping: 0=Deassert on transfer complete, 1=Keep asserted */
#define QSPI_SPCMD_SPB_MASK (0xF << 8)
#define QSPI_SPCMD_SPB(l)   (((l) & 0xF) << 8) /* Data Length: 0=1 byte, 1=2 bytes, 2=4 bytes */
#define QSPI_SPCMD_LSBF     (1 << 12) /* LSB First: 0=MSB First, 1=LSB First */
#define QSPI_SPCMD_SPNDEN   (1 << 13) /* Next-Access Delay Enable */
#define QSPI_SPCMD_SLNDEN   (1 << 14) /* SSL Negation Delay Setting Enable */
#define QSPI_SPCMD_SCKDEN   (1 << 15) /* QSPCLK Delay Setting Enable */
#define QSPI_SPBFCR      (*(volatile uint8_t *)(QSPI_BASE + 0x18)) /* Buffer Control */
#define QSPI_SPBFCR_RXTRG(n)  ((n) & 0x7)       /* Receive Buffer Data Trigger Num */
#define QSPI_SPBFCR_TXTRG(n) (((n) & 0x7) << 3) /* Transmit Buffer Data Trigger Num */
#define QSPI_SPBFCR_RXRST    (1 << 6) /* Receive Buffer Data Reset */
#define QSPI_SPBFCR_TXRST    (1 << 7) /* Transmit Buffer Data Reset */
#define QSPI_SPBDCR       (*(volatile uint16_t *)(QSPI_BASE + 0x1A)) /* Buffer Data Count Set */
#define QSPI_SPBDCR_RXBC  (QSPI_SPBDCR & 0x3F)
#define QSPI_SPBDCR_TXBC ((QSPI_SPBDCR >> 8) & 0x3F)
#define QSPI_SPBMUL(n)   (*(volatile uint32_t *)(QSPI_BASE + 0x1C + (((n) & 0x3) * 4))) /* Transfer Data Length Multiplier Setting */

#define QSPI_FIFO_SIZE 32 /* bytes */


#endif /* !_WOLFBOOT_RENESAS_RX_H_ */
