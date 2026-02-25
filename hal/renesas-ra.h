/* renesas-ra.h
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1335, USA
 */

/* Base addresses */
#define R_SYSTEM_BASE   0x4001E000UL
#define R_FACI_BASE     0x407FE000UL
#define R_FCACHE_BASE   0x4001C100UL

/* Register access macros */
#define REG8(addr)      (*(volatile uint8_t *)(addr))
#define REG16(addr)     (*(volatile uint16_t *)(addr))
#define REG32(addr)     (*(volatile uint32_t *)(addr))

/* --- FACI HP registers --- */
/* 0x407FE080: Flash Status Register */
#define FLASH_FSTATR    REG32(R_FACI_BASE + 0x80)
/* 0x407FE084: P/E Mode Entry Register */
#define FLASH_FENTRYR   REG16(R_FACI_BASE + 0x84)
/* 0x407FE030: Flash Start Address Register */
#define FLASH_FSADDR    REG32(R_FACI_BASE + 0x30)
/* 0x407FE01C: Flash Reset Register */
#define FLASH_FRESETR   REG8 (R_FACI_BASE + 0x1C)

/* --- System register --- */
/* 0x4001E416: Flash Write/Erase Protect Register */
#define FLASH_FWEPROR         REG8(R_SYSTEM_BASE + 0x416)
#define FLASH_FWEPROR_FLWE    (1 << 0)   /* 1 = P/E enabled  */
#define FLASH_FWEPROR_FLWD    (1 << 1)   /* 1 = P/E disabled */

/* --- FACI command issuing area (code flash) --- */
#define FACI_CMD_AREA   0x407E0000UL
#define FACI_CMD8       (*(volatile uint8_t  *)FACI_CMD_AREA)
#define FACI_CMD16      (*(volatile uint16_t *)FACI_CMD_AREA)

/* --- FACI command bytes --- */
#define FACI_CMD_PROGRAM      0xE8   /* Program (128-byte unit) */
#define FACI_CMD_PROGRAM_LEN  0x40   /* 64 words = 128 bytes   */
#define FACI_CMD_BLOCK_ERASE  0x20   /* Block erase            */
#define FACI_CMD_EXECUTE      0xD0   /* Execute                */
#define FACI_CMD_STATUS_CLR   0x50   /* Clear status           */
#define FACI_CMD_FORCED_STOP  0xB3   /* Forced stop / cancel   */

/* Minimum program unit: bytes per write operation */
#define FACI_PROG_UNIT        128

/* --- FSTATR status flags --- */
#define FLASH_FSTATR_FLWEERR    (1 << 6)  /* P/E Protect Error    */
#define FLASH_FSTATR_PRGSPD     (1 << 8)  /* Program Suspend      */
#define FLASH_FSTATR_ERSSPD     (1 << 9)  /* Erase Suspend        */
#define FLASH_FSTATR_DBFULL     (1 << 10) /* Data Buffer Full     */
#define FLASH_FSTATR_SUSRDY     (1 << 11) /* Suspend Ready        */
#define FLASH_FSTATR_PRGERR     (1 << 12) /* Program Error        */
#define FLASH_FSTATR_ERSERR     (1 << 13) /* Erase Error          */
#define FLASH_FSTATR_ILGLERR    (1 << 14) /* Illegal Error        */
#define FLASH_FSTATR_FRDY       (1 << 15) /* Flash Ready          */
#define FLASH_FSTATR_OTERR      (1 << 20) /* Other Error          */
#define FLASH_FSTATR_SECERR     (1 << 21) /* Security Error       */
#define FLASH_FSTATR_FESETERR   (1 << 22) /* FENTRY Setting Error */
#define FLASH_FSTATR_ILGCOMERR  (1 << 23) /* Illegal Command Err  */

/* 0x407FE0E4: Flash Sequencer Processing Clock Frequency Notification */
#define FLASH_FPCKAR        REG16(R_FACI_BASE + 0xE4)
/* KEY must be written as 0x1E in bits[15:8] to enable the PCKA write.
 * PCKA[7:0] = PCLKA frequency in MHz, minus 1. */
#define FLASH_FPCKAR_KEY    (0x1E00U)

/* PCLKA frequency for FPCKAR.PCKA (ICLK=200MHz, PCLKA_DIV=/2 -> 100MHz) */
#ifndef RA_PCLKA
 #define RA_PCLKA           100000000U  /* 100 MHz */
#endif
#define RA_PCLKA_MHZ        (RA_PCLKA / 1000000U)

/* --- FENTRYR constants --- */
/* Upper byte must be 0xAA on write */
#define FLASH_FENTRYR_KEY       (0xAA00)
#define FLASH_FENTRYR_CODE_READ (0)
#define FLASH_FENTRYR_CODE_PR   (1 << 0) /* Code Flash P/E mode  */

/* 0x407FE044: Flash P/E Mode Entry Protection Register
 * CEPROT bit0=1 silently blocks FENTRYR writes (P/E mode cannot be entered).
 * KEY=0xD9 must be written in bits[15:8] simultaneously with CEPROT.
 * Write FMEPROT_KEY|0 to clear CEPROT before entering P/E mode. */
#define FLASH_FMEPROT           REG16(R_FACI_BASE + 0x44)
#define FLASH_FMEPROT_KEY       (0xD900U)

/* UART registers (SCI7: P613=TXD7, P614=RXD7, J23 on EK-RA6M4) */
#define R_SCI7_BASE 0x40118700UL
#define SCI_SMR    REG8(R_SCI7_BASE + 0x00)  /* Serial Mode */
#define SCI_BRR    REG8(R_SCI7_BASE + 0x01)  /* Baud Rate   */
#define SCI_SCR    REG8(R_SCI7_BASE + 0x02)  /* Control     */
#define SCI_TDR    REG8(R_SCI7_BASE + 0x03)  /* Transmit Data */
#define SCI_SSR    REG8(R_SCI7_BASE + 0x04)  /* Status */
#define SCI_SCMR   REG8(R_SCI7_BASE + 0x06)  /* */
#define SCI_SEMR   REG8(R_SCI7_BASE + 0x07)  /* */
#define SCI_SCR_TE      (1u << 5)
#define SCI_SCR_RE      (1u << 4)
#define SCI_SSR_TEND    (1u << 2)
#define SCI_SSR_TDRE    (1u << 7)
#define SCI_SEMR_BGDM   (1u << 6)
#define SCI_SEMR_ABCS   (1u << 4)
#define SCI_SCMR_CHR1   (1u << 4)
#define R_MSTP_MSTPCRB  REG32(0x40084004UL)
#define MSTPCRB_SCI7    (1u << 24)   /* MSTPCRB bit24 = SCI7 */
#define R_PFS_P613      REG32(0x400809B4UL)
#define R_PFS_P614      REG32(0x400809B8UL)
#define PFS_PMR         (1u << 16)
#define PFS_PDR         (1u << 2)
#define PFS_PODR        (1u << 0)
#define PFS_PSEL_SCI7   (0x05u << 24)  /* PSEL=0x05: SCI1/3/5/7/9 odd channels */
#define R_PMISC_PWPR    REG8(0x40080D03UL)
#define PWPR_PFSWE      (1u << 6)
#define PWPR_B0WI       (1u << 7)
#ifndef DEBUG_BAUD_RATE
 #define DEBUG_BAUD_RATE 115200U
#endif
#ifndef RA_PCLKB
 #define RA_PCLKB        50000000U
#endif
#define SCI_BRR_VAL    (RA_PCLKB / (8U * DEBUG_BAUD_RATE) - 1U)
/* --- Flash error codes (used by flash_check_error) --- */
typedef enum {
    FLASH_OK        = 0,
    FLASH_ERR_ILGL,
    FLASH_ERR_PRG,
    FLASH_ERR_ERS,
    FLASH_ERR_FLWE,
    FLASH_ERR_FESET,
    FLASH_ERR_SEC,
    FLASH_ERR_OT,
} flash_err_t;
