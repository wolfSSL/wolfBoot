/* renesas-rx.c
 *
 * Stubs for custom HAL implementation. Defines the
 * functions used by wolfBoot for a specific target.
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

/* HAL for Renesas RX boards */
/* Tested with:
 * - RX65N Target Board
 *
 * - RX72N Envision Kit (HMI IoT)
 *     R5F572NNHDFB 144-pin LFQFP (PLQP0144KA-B)
 *     4MB Flash, 1MB RAM, 32KB Data Flash, 240MHz, TSIP
 *     QSPI: Macronix MX25L3233FM2I-08G: 4MB QSPI Serial Flash
 *
 * - RX71M RSK+ (Renesas Starter Kit+)
 *     R5F571MLCDFC 176-pin LQFP
 *     4MB Flash, 512KB RAM, 64KB Data Flash, 240MHz, no TSIP
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
#if defined(WOLFBOOT_RENESAS_TSIP) && !defined(WOLFBOOT_RENESAS_APP)
static void hal_panic(void)
{
    while(1)
        ;
}
#endif

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
#elif defined(TARGET_rx71m)
    /* RX71M RSK+ LED0 P03 */
    PORT_PDR(0) |= (1 << 3);   /* output */
    PORT_PODR(0) &= ~(1 << 3); /* low */
#endif
}

void hal_led_off(void)
{
#if defined(TARGET_rx65n)
    /* RX65N RSK+ LED0 P73 */
    PORT_PDR(7) |= (1 << 3);  /* output */
    PORT_PODR(7) |= (1 << 3); /* high */
#elif defined(TARGET_rx72n)
    /* RX72N Envision USR LED P40 */
    PORT_PDR(4) |= (1 << 0);  /* output */
    PORT_PODR(4) |= (1 << 0); /* high */
#elif defined(TARGET_rx71m)
    /* RX71M RSK+ LED0 P03 */
    PORT_PDR(0) |= (1 << 3);  /* output */
    PORT_PODR(0) |= (1 << 3); /* high */
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

#ifndef TARGET_rx71m
static flash_err_t flash_check_error()
{
    uint32_t st = FLASH_FSTATR;

    if (st & FLASH_FSTATR_ILGLERR) return FLASH_ERR_ILGL;
    if (st & FLASH_FSTATR_PRGERR) return FLASH_ERR_PRG;
    if (st & FLASH_FSTATR_ERSERR) return FLASH_ERR_ERS;
    if (st & FLASH_FSTATR_FLWEERR) return FLASH_ERR_FLWE;
    if (st & FLASH_FSTATR_FESETERR) return FLASH_ERR_FESET;
    if (st & FLASH_FSTATR_SECERR) return FLASH_ERR_SEC;
    if (st & FLASH_FSTATR_OTERR) return FLASH_ERR_OT;

    return FLASH_OK;

}
#endif /* !TARGET_rx71m */
#ifdef DEBUG_UART

#ifndef DEBUG_UART_SCI
    #ifdef TARGET_rx72n
        /* SCI2: TXD2/PC13, RXD2/PC12 */
        #define DEBUG_UART_SCI 2
    #elif defined(TARGET_rx71m)
        /* SCI7: TXD7/P90, RXD7/P92 (RSK+ USB serial) */
        #define DEBUG_UART_SCI 7
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
#elif defined(TARGET_rx71m)
    /* PFS may only change while PMR is 0 */
    PORT_PMR(0x9) &= ~((1 << 0) | (1 << 2));
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
#elif defined(TARGET_rx71m)
    MPC_PFS(0x48) = 0xA; /* P90-TXD7 */
    MPC_PFS(0x4A) = 0xA; /* P92-RXD7 */
#else
    MPC_PFS(0xF1) = 0xA; /* PJ1-RXD8 */
    MPC_PFS(0xF2) = 0xA; /* PJ2-TXD8 */
#endif

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |=   MPC_PWPR_PFSWE;

#ifdef TARGET_rx71m
    /* Configure P90/P92 for UART */
    PORT_PMR(0x9) |= ((1 << 0) | (1 << 2));
#endif

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
    /* Return only once the last character has left the shift register */
    while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
}
void uart_read(char* buf, unsigned int sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_RDRF) == 0);
        buf[pos++] = (char)SCI_RDR(DEBUG_UART_SCI);
    }
}
#endif /* DEBUG_UART */

/* LOCO clock is used out of reset */
#ifdef TARGET_rx71m
/* This function will switch to the main clock (crystal) through PLL */
#ifndef RX_MAIN_XTAL_HZ
#define RX_MAIN_XTAL_HZ (24000000) /* RSK+ RX71M X1 */
#endif
#if RX_MAIN_XTAL_HZ < 8000000 || RX_MAIN_XTAL_HZ > 24000000
    #error RX_MAIN_XTAL_HZ must be within the 8-24MHz PLL input range
#endif
#if (SYS_CLK % RX_MAIN_XTAL_HZ) != 0
    #error SYS_CLK must be a whole multiple of RX_MAIN_XTAL_HZ
#endif
#if RX_MAIN_XTAL_HZ > 20000000
    #define CFG_MOFCR_MODRV2 0
#elif RX_MAIN_XTAL_HZ > 16000000
    #define CFG_MOFCR_MODRV2 1
#elif RX_MAIN_XTAL_HZ > 8000000
    #define CFG_MOFCR_MODRV2 2
#else
    #define CFG_MOFCR_MODRV2 3
#endif
#define CFG_CKSEL 2 /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */
#define CFD_PLL_DIV (0)
#define CFG_PLL_MUL (SYS_CLK / RX_MAIN_XTAL_HZ)
#else
/* This function will switch to using on-chip HOCO through PLL */
#define CFG_CKSEL 1 /* 0=LOCO, 1=HOCO, 2=Main, 3=Sub, 4=PLL */
#define CFG_HCO_FRQ (16000000)
#define CFD_PLL_DIV (0)
#define CFG_PLL_MUL (SYS_CLK / (CFG_HCO_FRQ / 2))
#endif

void hal_clk_init(void)
{
    uint32_t reg, i;
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
    /* MOFXIN=0 (not controlled), MODRV2 by crystal, MOSEL=0 (resonator) */
    SYS_MOFCR = SYS_MOFCR_MODRV2(CFG_MOFCR_MODRV2);

    /* OSC stabilization time (9.98 ms * (264 kHZ) + 16)/32 = 82.83) */
    SYS_MOSCWTCR = SYS_MOSCWTCR_MSTS(83);

    /* Enable Main OSC */
    SYS_MOSCCR = 0;
    if (SYS_MOSCCR == 0) { RX_NOP(); }
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_MOOVF) == 0) { RX_NOP(); }
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
        #define PLL_SRCSEL 0 /* main */
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
#elif defined(TARGET_rx71m)
    SYS_MEMWAIT = SYS_MEMWAIT_MEMWAIT; /* required for ICLK > 120MHz */
    if (SYS_MEMWAIT == SYS_MEMWAIT_MEMWAIT) { RX_NOP(); }
#else
    FLASH_ROMWT = FLASH_ROMWT_ROMWT(2); /* (1=50-100MHz, 2= >100MHz) */
    reg = FLASH_ROMWT;
#endif

    /* ---- Clock Select ---- */
#if defined(TARGET_rx71m)
    reg = (
        SYS_SCKCR_ICK(0)  | /* System Clock (ICK)=0:               1/1 = 240MHz */
        SYS_SCKCR_BCK(1)  | /* External Bus Clock (BCK)=1:         1/2 = 120MHz */
        SYS_SCKCR_FCK(2)  | /* Flash-IF Clock FCK=2:               1/4 = 60MHz */
        SYS_SCKCR_PCKA(1) | /* Peripheral Module Clock A (PCKA)=1: 1/2 = 120MHz */
        SYS_SCKCR_PCKB(2) | /* Peripheral Module Clock B (PCKB)=2: 1/4 = 60MHz */
        SYS_SCKCR_PCKC(2) | /* Peripheral Module Clock C (PCKC)=2: 1/4 = 60MHz */
        SYS_SCKCR_PCKD(2) | /* Peripheral Module Clock D (PCKD)=2: 1/4 = 60MHz */
        SYS_SCKCR_PSTOP1 |  /* BCLK Pin Output  (PSTOP1): 0=Disabled */
        SYS_SCKCR_PSTOP0    /* SDCLK Pin Output (PSTOP0): 0=Disabled */
    );
#elif SYS_CLK >= 240000000
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

/* RX71M: UCK stays at its reset value 0001b while USB is unused */
#if CFG_CKSEL == 2 && !defined(TARGET_rx71m) /* USB only on main clock */
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

#if defined(WOLFBOOT_RENESAS_TSIP) && !defined(WOLFBOOT_RENESAS_APP)
static int sipInitDone = 0;
int hal_renesas_init(void)
{
    int err;
    uint32_t key_type = 0;
    int tsip_key_type = -1;
    /* This structure is generated using Renesas Security Key Management Tool
     * See docs/Renesas.md */
    struct enc_pub_key *encrypted_user_key_data;

    if (sipInitDone)
        return 0;

    err = wolfCrypt_Init();
    if (err != 0) {
        return err;
    }

    /* retrive installed pubkey data from flash */
    encrypted_user_key_data = (struct enc_pub_key*)keystore_get_buffer(0);

    key_type = keystore_get_key_type(0);
    switch (key_type) {
        case AUTH_KEY_RSA2048:
            tsip_key_type = TSIP_KEY_TYPE_RSA2048;
            break;
        case AUTH_KEY_RSA3072:
            tsip_key_type = TSIP_KEY_TYPE_RSA3072;
            break;
        case AUTH_KEY_RSA4096:
            tsip_key_type = TSIP_KEY_TYPE_RSA4096;
            break;
        case AUTH_KEY_ECC256:
            tsip_key_type = TSIP_KEY_TYPE_ECDSAP256;
            break;
        case AUTH_KEY_ECC384:
            tsip_key_type = TSIP_KEY_TYPE_ECDSAP384;
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
        return -1;
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
        return -1;
    }

    /* Init Crypt Callback */
    pkInfo.sign_hash_type = sha256_mac; /* TSIP does not support SHA2-384/512 */
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);
    if (err < 0) {
        wolfBoot_printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n", err);
        return -1;
    }
    sipInitDone = 1;
    return 0;
}
#endif /* TSIP */


#ifdef WATCHDOG
/* External watchdog (e.g. MAX6316-MAX6322): toggle the WDI GPIO so a signal
 * edge restarts its timer. Pin set by WATCHDOG_WDI_PORT/WATCHDOG_WDI_PIN. */
#ifndef WATCHDOG_WDI_PORT
#define WATCHDOG_WDI_PORT 0 /* PORT0 */
#endif
#ifndef WATCHDOG_WDI_PIN
#define WATCHDOG_WDI_PIN  0
#endif

static void hal_watchdog_init(void)
{
    /* Drive WDI as a general-purpose CMOS output */
    PORT_PMR(WATCHDOG_WDI_PORT) &= (uint8_t)~(1U << WATCHDOG_WDI_PIN);
    PORT_PDR(WATCHDOG_WDI_PORT) |= (uint8_t) (1U << WATCHDOG_WDI_PIN);
}

/* RAMFUNCTION: callable from the RAM-resident flash paths. */
void RAMFUNCTION wolfBoot_watchdog_feed(void)
{
    /* Toggle WDI: any transition restarts the external watchdog timer */
    PORT_PODR(WATCHDOG_WDI_PORT) ^= (uint8_t)(1U << WATCHDOG_WDI_PIN);
}
#endif /* WATCHDOG */

void hal_init(void)
{
#if defined(WOLFBOOT_RENESAS_TSIP) && !defined(WOLFBOOT_RENESAS_APP)
    int err;
#endif

/* For CCRX, mcu_clock_setup() in resetprg.c will set up clocks. */
#if defined(__GNUC__)
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

#ifdef WATCHDOG
    hal_watchdog_init();
#endif

#if defined(WOLFBOOT_RENESAS_TSIP) && !defined(WOLFBOOT_RENESAS_APP)
    err = hal_renesas_init();
    if (err != 0) {
        wolfBoot_printf("ERROR: hal_renesas_init %d\n", err);
        hal_panic();
    }
#endif
}

void hal_prepare_boot(void)
{
#if defined(TARGET_rx71m) && defined(WOLFBOOT_RESTORE_CLOCK)
    /* Run from the LOCO with the PLL and main oscillator stopped, so the
     * application can set up its own clocks */
    PROTECT_OFF();
    SYS_LOCOCR = 0;
    hal_delay_us(44); /* LOCO settling time */
    SYS_SCKCR3 = SYS_SCKCR3_CKSEL(0);
    if (SYS_SCKCR3 == 0) { RX_NOP(); }
    SYS_SCKCR = 0;
    if (SYS_SCKCR == 0) { RX_NOP(); }
    SYS_MEMWAIT = 0;
    if (SYS_MEMWAIT == 0) { RX_NOP(); }
    SYS_PLLCR2 = SYS_PLLCR2_PLLEN;
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_PLOVF) != 0) { RX_NOP(); }
    SYS_MOSCCR = SYS_MOSCCR_MOSTP;
    while ((SYS_OSCOVFSR & SYS_OSCOVFSR_MOOVF) != 0) { RX_NOP(); }
    PROTECT_ON();
#endif
}

#ifdef __CCRX__
/* copy RAM functions from ROM to RAM */
static void copyfuncs(void)
{
    unsigned char *dst, *src;
    size_t n;

    src = __sectop("PFRAM");
    dst = __sectop("RPFRAM");
    n = (size_t)((unsigned char*)__secend("PFRAM") -
                 (unsigned char*)__sectop("PFRAM"));
    while(src < __secend("PFRAM")) {
        *dst++ = *src++;
    }

    wolfBoot_printf("RAM Function start = 0x%p\n", __sectop("RPFRAM"));
    wolfBoot_printf("RAM Function end   = 0x%p\n", __secend("RPFRAM"));
    wolfBoot_printf("RAM Function size  = %d\n", n);
}
#endif

#ifdef TARGET_rx71m /* RX71M code flash driver */
#if !defined(__CCRX__) && (!defined(RAM_CODE) || !defined(__WOLFBOOT))
    #error RX71M flash driver must run from RAM: define RAM_CODE and __WOLFBOOT
#endif

#ifdef __CCRX__
/* FCLK from the Smart Configurator clock settings */
#define FLASH_FCLK_MHZ ((uint32_t)(BSP_FCLK_HZ / 1000000UL))
#else
#define FLASH_FCLK_MHZ (FCLK / 1000000UL)
#endif

/* Timeouts as poll counts; each poll takes at least one ICLK */
#define FLASH_POLLS_PER_MS    (SYS_CLK / 1000UL)
#define FLASH_PROGRAM_TIMEOUT (8UL * FLASH_POLLS_PER_MS)
#define FLASH_ERASE_TIMEOUT   (640UL * FLASH_POLLS_PER_MS)
#define FLASH_MODE_TIMEOUT    (1UL * FLASH_POLLS_PER_MS)

#define FLASH_UNIT_SIZE \
    (FLASH_FACI_CMD_PROGRAM_CODE_LENGTH * FLASH_FACI_CMD_PROGRAM_DATA_LENGTH)

/* One 256-byte write, kept in RAM and sent to the FACI 16 bits at a time */
static uint16_t flash_unit[FLASH_FACI_CMD_PROGRAM_CODE_LENGTH];

#ifdef __CCRX__
#pragma section FRAM
#endif

static int RAMFUNCTION flash_wait_complete(uint32_t limit)
{
    uint32_t i;
    int ret = FLASH_ERR_TIMEOUT;

    for (i = 0; i < limit; i++) {
        if ((FLASH_FSTATR & FLASH_FSTATR_FRDY) != 0) {
            ret = FLASH_OK;
            break;
        }
    }
    if (ret != FLASH_OK) {
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_FORCED_STOP;
        for (i = 0; i < FLASH_MODE_TIMEOUT; i++) {
            if ((FLASH_FSTATR & FLASH_FSTATR_FRDY) != 0)
                break;
        }
    }
    return ret;
}

/* After an error the FACI refuses commands until released:
 * FCUERR, FRDTCT and FLWEERR need a forced stop, other errors a status
 * clear */
static void RAMFUNCTION flash_clear_errors(void)
{
    if ((FLASH_FASTAT & (FLASH_FASTAT_CFAE | FLASH_FASTAT_DFAE)) != 0) {
        FLASH_FASTAT &= (uint8_t)~(FLASH_FASTAT_CFAE | FLASH_FASTAT_DFAE);
    }
    if ((FLASH_FSTATR & (FLASH_FSTATR_FCUERR | FLASH_FSTATR_FRDTCT |
            FLASH_FSTATR_FLWEERR)) != 0) {
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_FORCED_STOP;
    }
    else {
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_STATUS_CLEAR;
    }
    (void)flash_wait_complete(FLASH_MODE_TIMEOUT);
}

static int RAMFUNCTION flash_check_error(void)
{
    uint32_t st = FLASH_FSTATR;
    int ret = FLASH_OK;

    if ((st & FLASH_FSTATR_ILGLERR) != 0)
        ret = FLASH_ERR_ILGL;
    else if ((st & FLASH_FSTATR_PRGERR) != 0)
        ret = FLASH_ERR_PRG;
    else if ((st & FLASH_FSTATR_ERSERR) != 0)
        ret = FLASH_ERR_ERS;
    else if ((st & FLASH_FSTATR_FLWEERR) != 0)
        ret = FLASH_ERR_FLWE;
    else if ((st & (FLASH_FSTATR_FCUERR | FLASH_FSTATR_FRDTCT)) != 0)
        ret = FLASH_ERR_FCU;
    else if ((FLASH_FASTAT & FLASH_FASTAT_CMDLK) != 0)
        ret = FLASH_ERR_UNKNOWN;

    if (ret != FLASH_OK)
        flash_clear_errors();
    return ret;
}

static uint32_t RAMFUNCTION flash_irq_save(void)
{
#ifdef __CCRX__
    uint32_t psw = (uint32_t)get_psw();

    clrpsw_i();
#else
    uint32_t psw = __builtin_rx_mvfc(0);

    __builtin_rx_clrpsw('I');
#endif
    return psw;
}

static void RAMFUNCTION flash_irq_restore(uint32_t psw)
{
    if ((psw & RX_PSW_I) != 0) {
#ifdef __CCRX__
        setpsw_i();
#else
        __builtin_rx_setpsw('I');
#endif
    }
}

/* mode: FLASH_FENTRYR_CODE_PR (code flash) or FLASH_FENTRYR_DATA_PE
 * (data flash) */
static int RAMFUNCTION flash_enter_pe_mode(uint16_t mode)
{
    uint32_t i;
    int ret = FLASH_ERR_TIMEOUT;

    FLASH_FWEPROR = FLASH_FWEPROR_FLWE;
    FLASH_FENTRYR = (uint16_t)(FLASH_FENTRYR_KEY | mode);
    for (i = 0; i < FLASH_MODE_TIMEOUT; i++) {
        if (FLASH_FENTRYR == mode) {
            ret = FLASH_OK;
            break;
        }
    }
    return ret;
}

static int RAMFUNCTION flash_enter_read_mode(void)
{
    int ret;

    ret = flash_wait_complete(FLASH_PROGRAM_TIMEOUT);
    if ((FLASH_FASTAT & FLASH_FASTAT_CMDLK) != 0)
        flash_clear_errors();
    FLASH_FENTRYR = (uint16_t)(FLASH_FENTRYR_KEY | FLASH_FENTRYR_CODE_READ);
    /* Stay in RAM until code flash is readable again */
    while (FLASH_FENTRYR != FLASH_FENTRYR_CODE_READ);
    FLASH_FWEPROR = FLASH_FWEPROR_FLWD;
    return ret;
}

/* address must be 256-byte aligned; data must be in RAM, since code flash
 * cannot be read in P/E mode */
static int RAMFUNCTION flash_write_256(uint32_t address, const uint16_t *data)
{
    uint32_t i, j, psw;
    int ret, mode_ret;

    psw = flash_irq_save();
    ret = flash_enter_pe_mode(FLASH_FENTRYR_CODE_PR);
    if (ret == FLASH_OK) {
        FLASH_FSADDR = address;
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_PROGRAM;
        FLASH_FACI_CMD8 = FLASH_FACI_CMD_PROGRAM_CODE_LENGTH;
        for (i = 0; i < FLASH_FACI_CMD_PROGRAM_CODE_LENGTH &&
                ret == FLASH_OK; i++) {
            for (j = 0; j < FLASH_PROGRAM_TIMEOUT; j++) {
                if ((FLASH_FSTATR & FLASH_FSTATR_DBFULL) == 0)
                    break;
            }
            if (j == FLASH_PROGRAM_TIMEOUT)
                ret = FLASH_ERR_TIMEOUT;
            else
                FLASH_FACI_CMD16 = data[i];
        }
        if (ret == FLASH_OK) {
            FLASH_FACI_CMD8 = FLASH_FACI_CMD_FINAL;
            ret = flash_wait_complete(FLASH_PROGRAM_TIMEOUT);
        }
        else {
            FLASH_FACI_CMD8 = FLASH_FACI_CMD_FORCED_STOP;
        }
    }
    if (ret == FLASH_OK)
        ret = flash_check_error();
    mode_ret = flash_enter_read_mode();
    flash_irq_restore(psw);
    if (ret == FLASH_OK)
        ret = mode_ret;
    return ret;
}

/* [address, address + len) must lie inside code flash */
static int RAMFUNCTION flash_range_valid(uint32_t address, int len)
{
    return (len >= 0 && IS_FLASH_ADDR(address) &&
        (len == 0 || (uint32_t)(len - 1) <= (0xFFFFFFFFUL - address)));
}

/* The FCU runs firmware copied from ROM to FCURAM; do this once before any
 * write or erase */
#ifdef __CCRX__
/* keep it out of hal_flash_init(), which runs from ROM */
#pragma noinline(flash_fcu_firmware_copy)
#endif
static int RAMFUNCTION flash_fcu_firmware_copy(void)
{
    const volatile uint32_t *src = (const volatile uint32_t *)FCU_FIRMWARE_ADDR;
    volatile uint32_t *dst = (volatile uint32_t *)FCU_RAM_AREA;
    uint32_t i;
    int ret, mode_ret;

    ret = flash_enter_read_mode();
    if (ret == FLASH_OK) {
        FLASH_FCURAME = (uint16_t)(FLASH_FCURAME_KEY | FLASH_FCURAME_FCRME |
            FLASH_FCURAME_FRAMTRAN);
        for (i = 0; i < FCU_RAM_SIZE / sizeof(uint32_t); i++) {
            dst[i] = src[i];
        }
        FLASH_FCURAME = FLASH_FCURAME_KEY;

        /* Copying to FCURAM leaves error flags set; a forced stop clears
         * them */
        ret = flash_enter_pe_mode(FLASH_FENTRYR_DATA_PE);
        if (ret == FLASH_OK) {
            FLASH_FACI_CMD8 = FLASH_FACI_CMD_FORCED_STOP;
            ret = flash_wait_complete(FLASH_MODE_TIMEOUT);
        }
        mode_ret = flash_enter_read_mode();
        if (ret == FLASH_OK)
            ret = mode_ret;
    }
    return ret;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint8_t *unit = (uint8_t *)flash_unit;
    const uint8_t *cur;
    uint32_t base, off, n, i;
    int changed, blank;
    int ret = FLASH_OK;

    if (data == NULL || !flash_range_valid(address, len))
        ret = -1;

    while (ret == FLASH_OK && len > 0) {
        base = address & ~(uint32_t)(FLASH_UNIT_SIZE - 1);
        off = address - base;
        n = FLASH_UNIT_SIZE - off;
        if (n > (uint32_t)len)
            n = (uint32_t)len;

        cur = (const uint8_t *)(uintptr_t)base;
        changed = 0;
        blank = 1;
        for (i = 0; i < FLASH_UNIT_SIZE; i++) {
            unit[i] = cur[i];
            if (i >= off && i < off + n && data[i - off] != cur[i]) {
                unit[i] = data[i - off];
                changed = 1;
            }
            if (cur[i] != 0xFF)
                blank = 0;
        }

        if (changed && !blank)
            ret = FLASH_ERR_NOT_BLANK;
        else if (changed)
            ret = flash_write_256(base, flash_unit);

        address += n;
        data += n;
        len -= (int)n;
    }
    return ret;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t block_size, psw;
    int ret = FLASH_OK;
    int mode_ret;

    if (!flash_range_valid(address, len))
        ret = -1;

    while (ret == FLASH_OK && len > 0) {
        block_size = FLASH_BLOCK_SIZE(address);
        if ((address & (block_size - 1)) != 0 || (uint32_t)len < block_size) {
            ret = -1;
        }
        else {
            psw = flash_irq_save();
            ret = flash_enter_pe_mode(FLASH_FENTRYR_CODE_PR);
            if (ret == FLASH_OK) {
                FLASH_FCPSR = 0; /* erase suspend is never used */
                FLASH_FSADDR = address;
                FLASH_FACI_CMD8 = FLASH_FACI_CMD_BLOCK_ERASE;
                FLASH_FACI_CMD8 = FLASH_FACI_CMD_FINAL;
                ret = flash_wait_complete(FLASH_ERASE_TIMEOUT);
            }
            if (ret == FLASH_OK)
                ret = flash_check_error();
            mode_ret = flash_enter_read_mode();
            flash_irq_restore(psw);
            if (ret == FLASH_OK)
                ret = mode_ret;

            address += block_size;
            len -= (int)block_size;
        }
    }
    return ret;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}
#ifdef __CCRX__
#pragma section
#endif

int hal_flash_init(void)
{
#ifdef __CCRX__
    copyfuncs();
#endif
    FLASH_FAEINT &= (uint8_t)~(FLASH_FAEINT_ECRCTIE | FLASH_FAEINT_DFAEIE |
        FLASH_FAEINT_CMDLKIE | FLASH_FAEINT_CFAEIE);
    FLASH_FPCKAR = (uint16_t)(FLASH_FPCKAR_KEY |
        FLASH_FPCKAR_PCKA(FLASH_FCLK_MHZ));
    return flash_fcu_firmware_copy();
}
#else /* !TARGET_rx71m */
int hal_flash_init(void)
{
#ifdef __CCRX__
    copyfuncs();
#endif
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
#ifdef __CCRX__
#pragma section FRAM
#endif
int RAMFUNCTION hal_flash_write(uint32_t addr, const uint8_t *data, int len)
{
    int i;
    uint8_t codeblock[FLASH_FACI_CODE_BLOCK_SZ];
    uint16_t* data16 = (uint16_t*)data;
    uint32_t block_base;
    uint32_t offset;
    int      write_size;
    int      ret;

    while (len > 0) {
        /* Align address to 128-byte boundary */
        block_base = addr & ~(FLASH_FACI_CODE_BLOCK_SZ - 1);
        offset = addr - block_base;

        memcpy(codeblock, (uint8_t*)block_base, FLASH_FACI_CODE_BLOCK_SZ);
        write_size = FLASH_FACI_CODE_BLOCK_SZ - offset;
        if (write_size > len)
            write_size = len;

        memcpy(&codeblock[offset], data, write_size);
        data16 = (uint16_t*)codeblock;

        FLASH_FSADDR = block_base;
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
        if ((ret = flash_check_error()) != FLASH_OK) {
            return ret;
        }
        len -= write_size;
        addr += write_size;
        data += write_size;
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
#ifdef __CCRX__
#pragma section
#endif
#endif /* !TARGET_rx71m */
#if !defined(WOLFBOOT_NO_PARTITIONS) && !defined(TARGET_library)
void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}
#endif
