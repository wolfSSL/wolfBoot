/* renesas-rx.c
 *
 * Stubs for custom HAL implementation. Defines the
 * functions used by wolfBoot for a specific target.
 *
 * Copyright (C) 2022 wolfSSL Inc.
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

/* HAL for Renesas RX boards */
/* Tested with:
 * - RX65N Target Board
 *
 * - RX72N Envision Kit (HMI IoT)
 *     R5F572NNHDFB 144-pin LFQFP (PLQP0144KA-B)
 *     4MB Flash, 1MB RAM, 32KB Data Flash, 240MHz, TSIP
 *     QSPI: Macronix MX25L3233FM2I-08G: 4MB QSPI Serial Flash
 */


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <target.h>
#include "image.h"
#include "hal.h"
#include "printf.h"
#include "renesas-rx.h"
#include "printf.h"
#if defined(__CCRX__)
#include "r_smc_entry.h"
#endif
#if defined(WOLFBOOT_RENESAS_TSIP) && \
   !defined(WOLFBOOT_RENESAS_APP)
    #include "wolfssl/wolfcrypt/settings.h"
    #include "wolfssl/wolfcrypt/types.h"
    #include "wolfssl/wolfcrypt/wc_port.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas-tsip-crypt.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_sync.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_tsip_types.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_cmn.h"
    static TsipUserCtx pkInfo;

    #include "key_data.h"
#endif

/* forward declaration */
int hal_flash_init(void);

static void hal_panic(void)
{
    while(1)
        ;
}

#ifdef ENABLE_LED
void hal_led_on(void)
{
#if defined(TARGET_rx65n)
    /* RX65N RSK+ LED0 P73 */
    PORT_PDR(7) |= (1 << 3);   /* output */
    PORT_PODR(7) &= ~(1 << 3); /* low */
#elif defined(TARGET_rx72n)
    /* RX72N Envision USR LED P40 */
    PORT_PDR(4) |= (1 << 0);   /* output */
    PORT_PODR(4) &= ~(1 << 0); /* low */
#endif
}
void hal_led_off(void)
{
#ifdef TARGET_rx65n
    /* RX65N RSK+ LED0 P73 */
    PORT_PDR(7) |= (1 << 3);  /* output */
    PORT_PODR(7) |= (1 << 3); /* high */
#else
    /* RX72N Envision USR LED P40 */
    PORT_PDR(4) |= (1 << 0);  /* output */
    PORT_PODR(4) |= (1 << 0); /* high */
#endif
}
#endif

void hal_delay_us(uint32_t us)
{
    uint32_t delay;
    for (delay = 0; delay < (us * (SYS_CLK / 1000000)); delay++) {
        RX_NOP();
    }
}

#ifdef DEBUG_UART

#ifndef DEBUG_UART_SCI
    #ifdef TARGET_rx72n
        /* SCI2: TXD2/PC13, RXD2/PC12 */
        #define DEBUG_UART_SCI 2
    #else
        /* SCI8: TXD8/PJ2, RXD8/PJ1 */
        #define DEBUG_UART_SCI 8
    #endif
#endif
#ifndef DEBUG_BAUD_RATE
#define DEBUG_BAUD_RATE 115200
#endif

void uart_init(void)
{
    /* Release SCI module stop (clear bit) */
    PROTECT_OFF();
#if DEBUG_UART_SCI >= 0 && DEBUG_UART_SCI <= 7
    /* bit 31=SCI0, 30=SCI1, 29=SCI2, 28=SCI3, 27=SCI4, 26=SCI5, 25=SCI6, 24=SCI7 */
    SYS_MSTPCRB &= ~(1 << (31-DEBUG_UART_SCI));
#elif DEBUG_UART_SCI >= 8 && DEBUG_UART_SCI <= 11
    /* bit 27=SCI8, 26=SCI9, 25=SCI10, 24=SCI11 */
    SYS_MSTPCRC &= ~(1 << (27-(DEBUG_UART_SCI-8)));
#else
    #error SCI module stop not known
#endif
    PROTECT_ON();

    /* Disable RX/TX */
    SCI_SCR(DEBUG_UART_SCI) = 0;

#ifdef TARGET_rx72n
    /* Configure P13/P12 for UART */
    PORT_PMR(0x1) |= ((1 << 2) | (1 << 3));
#else
    /* Configure PJ1/PJ2 for UART */
    PORT_PMR(0x12) |= ((1 << 1) | (1 << 2));
#endif

    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |=  MPC_PWPR_PFSWE;

    /* Enable TXD/RXD */
    /* SCI Function Select = 0xA (UART) */
#ifdef TARGET_rx72n
    MPC_PFS(0xA) = 0xA; /* P12-RXD2 */
    MPC_PFS(0xB) = 0xA; /* P13-TXD2 */
#else
    MPC_PFS(0xF1) = 0xA; /* PJ1-RXD8 */
    MPC_PFS(0xF2) = 0xA; /* PJ2-TXD8 */
#endif

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
/* This function will switch to using on-chip HOCO through PLL */
#define CFG_CKSEL 1 /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */
#define CFG_HCO_FRQ (16000000)
#define CFD_PLL_DIV (0)
#define CFG_PLL_MUL (SYS_CLK / (CFG_HCO_FRQ / 2))

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

        /* Set 16MHz -> CFG_HCO_FRQ */
        SYS_HOCOCR2 = SYS_HOCOCR2_HCFRQ(0);

        /* Enable HOCO */
        SYS_HOCOCR &= ~SYS_HOCOCR_HCSTP;
        reg = SYS_HOCOCR; /* dummy ready (required) */
    }
    /* Wait for HOCO oscillator stabilization */
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
    /* convert multiplier to STC value */
    #define PLL_MUL_STC ((CFG_PLL_MUL * 2) - 1)
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
    /* Flash Wait Cycles */
#ifdef TARGET_rx72n
    /* Flash Wait Cycles  */
    FLASH_MEMWAIT = FLASH_MEMWAIT_MEMWAIT(1); /* (1=<120MHz) */
    reg = FLASH_MEMWAIT;
#else
    FLASH_ROMWT = FLASH_ROMWT_ROMWT(2); /* (1=50-100MHz, 2= >100MHz) */
    reg = FLASH_ROMWT;
#endif

    /* ---- Clock Select ---- */
#if SYS_CLK >= 240000000
    reg = (
        SYS_SCKCR_ICK(1)  | /* System Clock (ICK)=1:               1/2 = 240MHz */
        SYS_SCKCR_BCK(2)  | /* External Bus Clock (BCK)=2:         1/4 = 120MHz */
        SYS_SCKCR_FCK(3)  | /* Flash-IF Clock FCK=3:               1/8 = 60MHz */
        SYS_SCKCR_PCKA(2) | /* Peripheral Module Clock A (PCKA)=2: 1/4 = 120MHz */
        SYS_SCKCR_PCKB(3) | /* Peripheral Module Clock D (PCKB)=3: 1/8 = 60MHz */
        SYS_SCKCR_PCKC(3) | /* Peripheral Module Clock C (PCKC)=3: 1/8 = 60MHz */
        SYS_SCKCR_PCKD(3) | /* Peripheral Module Clock D (PCKD)=3: 1/8 = 60MHz */
        SYS_SCKCR_PSTOP1 |  /* BCLK Pin Output  (PSTOP1): 0=Disabled */
        SYS_SCKCR_PSTOP0    /* SDCLK Pin Output (PSTOP0): 0=Disabled */
    );
#else
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
#endif
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
#if defined(WOLFBOOT_RENESAS_TSIP) && \
    !defined(WOLFBOOT_RENESAS_APP)
    int err;
    uint32_t key_type = 0;
    int tsip_key_type = -1;
    struct enc_pub_key *encrypted_user_key_data;
#endif

/* For CCRX, mcu_clock_setup() in resetprg.c will set up clocks. */
#if defined(_GNUC_)
    hal_clk_init();
#endif

#ifdef ENABLE_LED
    hal_led_off();
#endif

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif

    hal_flash_init();

#if defined(WOLFBOOT_RENESAS_TSIP) && \
    !defined(WOLFBOOT_RENESAS_APP)
    err = wolfCrypt_Init();
    if (err != 0) {
       wolfBoot_printf("ERROR: wolfCrypt_Init %d\n", err);
       hal_panic();
    }

    /* retrive installed pubkey data from flash */
    encrypted_user_key_data = (struct enc_pub_key*)keystore_get_buffer(0);

    key_type = keystore_get_key_type(0);
    switch (key_type) {
        case AUTH_KEY_RSA2048:
            tsip_key_type = TSIP_RSA2048;
            break;
        case AUTH_KEY_RSA3072:
            tsip_key_type = TSIP_RSA3072;
            break;
        case AUTH_KEY_RSA4096:
            tsip_key_type = TSIP_RSA4096;
            break;
        case AUTH_KEY_ECC256:
            tsip_key_type = TSIP_ECCP256;
            break;
        case AUTH_KEY_ECC384:
            tsip_key_type = TSIP_ECCP384;
            break;
        case AUTH_KEY_ECC521:
        case AUTH_KEY_ED25519:
        case AUTH_KEY_ED448:
        default:
            tsip_key_type = -1;
            break;
    }
    if (tsip_key_type == -1) {
        wolfBoot_printf("key type (%d) not supported\n", key_type);
        hal_panic();
    }

    /* Load encrypted UFPK (User Factory Programming Key) */
    tsip_inform_user_keys_ex(
        (byte*)&encrypted_user_key_data->wufpk,
        (byte*)&encrypted_user_key_data->initial_vector,
        (byte*)&encrypted_user_key_data->encrypted_user_key,
        0/* dummy */
    );

    /* Load a wrapped public key into TSIP */
    if (tsip_use_PublicKey_buffer_crypt(&pkInfo,
                (const char*)&encrypted_user_key_data->encrypted_user_key,
                sizeof(encrypted_user_key_data->encrypted_user_key),
                tsip_key_type) != 0) {
        wolfBoot_printf("ERROR tsip_use_PublicKey_buffer\n");
        hal_panic();
    }

    /* Init Crypt Callback */
    pkInfo.sign_hash_type = sha256_mac; /* TSIP does not support SHA2-384/512 */
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);
    if (err < 0) {
        wolfBoot_printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n", err);
        hal_panic();
    }
#endif /* TSIP */
}

void hal_prepare_boot(void)
{

}

int hal_flash_init(void)
{
    /* Flash Write Enable */
    FLASH_FWEPROR = FLASH_FWEPROR_FLWE;

    /* Disable FCU interrupts */
    FLASH_FAEINT &= ~(
        FLASH_FAEINT_DFAEIE |
        FLASH_FAEINT_CMDLKIE |
        FLASH_FAEINT_CFAEIE);

    /* Set the flash clock speed */
    FLASH_FPCKAR = (FLASH_FPCKAR_KEY |
        FLASH_FPCKAR_PCKA(FCLK / 1000000UL));

    return 0;
}

/* write up to 128 bytes at a time */
#define FLASH_FACI_CODE_BLOCK_SZ \
    (FLASH_FACI_CMD_PROGRAM_CODE_LENGTH * FLASH_FACI_CMD_PROGRAM_DATA_LENGTH)
int RAMFUNCTION hal_flash_write(uint32_t addr, const uint8_t *data, int len)
{
    int ret, i, chunk;
    uint8_t codeblock[FLASH_FACI_CODE_BLOCK_SZ];
    uint16_t* data16 = (uint16_t*)data;

    while (len > 0) {
        /* handle partial remainder */
        if (len < FLASH_FACI_CODE_BLOCK_SZ) {
            uint8_t *src = (uint8_t*)addr;
            int remain = FLASH_FACI_CODE_BLOCK_SZ - len;
            memcpy(codeblock, data16, len);
            memcpy(codeblock + len, src + len, remain);
            data16 = (uint16_t*)codeblock;
        }

        FLASH_FSADDR = addr;
        /* flash program command */
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_PROGRAM;
        /* number of 16-bit blocks: for code blocks is always 0x40 (64) */
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_PROGRAM_CODE_LENGTH;

        /* write 64 * 2 bytes */
        for (i=0; i < FLASH_FACI_CMD_PROGRAM_CODE_LENGTH; i++) {
            FLASH_FACI_CMD16 = *data16++;

            /* wait for data buffer not full */
            while (FLASH_FSTATR & FLASH_FSTATR_DBFULL);
        }
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_FINAL;

        /* Wait for FCU operation to complete */
        while ((FLASH_FSTATR & FLASH_FSTATR_FRDY) == 0);

        len -= FLASH_FACI_CODE_BLOCK_SZ;
        addr += FLASH_FACI_CODE_BLOCK_SZ;
    }
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int block_size;

    /* verify this is a flash address */
    if (!IS_FLASH_ADDR(address)) {
        return -1;
    }

    /* make sure len is multiple of block sizze */
    block_size = FLASH_BLOCK_SIZE(address);
    if (len % block_size != 0) {
        return -1;
    }

    /* erase block(s) */
    while (len > 0) {
        FLASH_FSADDR = address;

        FLASH_FACI_CMD8 = FLASH_FACI_CMD_BLOCK_ERASE;
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_FINAL;

        /* Wait for FCU operation to complete */
        while ((FLASH_FSTATR & FLASH_FSTATR_FRDY) == 0);

        address += block_size;
        len -= block_size;
    }
    return 0;
}

static int RAMFUNCTION hal_flash_write_faw(uint32_t faw)
{
    volatile uint8_t* cmdArea = (volatile uint8_t*)FLASH_FACI_CMD_AREA;

#ifndef BIG_ENDIAN_ORDER
  #if defined(__CCRX__)
    faw = _builtin_revl(faw);
  #elif defined(__GNUC__)
    faw = __builtin_bswap32(faw);
  #endif
#endif

    hal_flash_unlock();

    /* Flash Access Window Write */
    FLASH_FSADDR = 0x00FF5D60; /* FAW Register Start */
    FLASH_FACI_CMD8  = FLASH_FACI_CMD_CONFIGURATION_SET;
    FLASH_FACI_CMD8  = FLASH_FACI_CMD_CONFIGURATION_LENGTH; /* len=8 */
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD16 = (uint16_t)(faw & 0xFFFF);
    FLASH_FACI_CMD16 = (uint16_t)((faw >> 16) & 0xFFFF);
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD16 = 0xFFFF;
    FLASH_FACI_CMD8  = FLASH_FACI_CMD_FINAL;

    /* Wait for FCU operation to complete */
    while ((FLASH_FSTATR & FLASH_FSTATR_FRDY) == 0);

    hal_flash_lock();

    return 0;
}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    uint32_t faw = FLASH_FAWMON;
    faw ^= FLASH_FAWMON_BTFLG; /* flip BTFLG */
    hal_flash_write_faw(faw);
}

void RAMFUNCTION hal_flash_unlock(void)
{
    /* Enable code flash entry for program/erase */
    FLASH_FENTRYR = (FLASH_FENTRYR_KEY |
        FLASH_FENTRYR_DATA_READ | FLASH_FENTRYR_CODE_PR);

    /* Make sure any pending FACI commands are cancelled */
    FLASH_FCMDR = FLASH_FACI_CMD_FORCED_STOP;
    while ((FLASH_FSTATR & FLASH_FSTATR_FRDY) == 0);

    return;
}
void RAMFUNCTION hal_flash_lock(void)
{
    /* Disable code flash entry */
    FLASH_FENTRYR = (FLASH_FENTRYR_KEY |
        FLASH_FENTRYR_CODE_READ | FLASH_FENTRYR_DATA_READ);
    return;
}

void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}
