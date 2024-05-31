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

#ifdef USE_RENESAS_BSP
    #define wolfBoot_printf printf
    #include "r_flash_rx_if.h"
    #include "r_flash_rx.h"
#else
    #include "printf.h"
#endif

#if defined(WOLFBOOT_RENESAS_TSIP)  && \
    !defined(WOLFBOOT_RENESAS_APP)
    #include "wolfssl/wolfcrypt/settings.h"
    #include "wolfssl/wolfcrypt/wc_port.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas-tsip-crypt.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_sync.h"
    #include "key_data.h"
    #include "wolfssl/wolfcrypt/port/Renesas/renesas_tsip_types.h"
    TsipUserCtx pkInfo;
#endif

#if defined(EXT_FLASH) && defined(TEST_FLASH)
static int test_flash(void);
#endif

static void hal_panic(void)
{
    while(1)
        ;
}

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
        /* Use SCI5 TXD5/PC3, RXD5/PC5 */
        #define DEBUG_UART_SCI 5
    #endif
#endif
#ifndef DEBUG_BAUD_RATE
#define DEBUG_BAUD_RATE 115200
#endif

void uart_init(void)
{
    /* Release SCI module stop (clear bit) */
    /* bit 31=SCI0, 30=SCI1, 29=SCI2, 28=SCI3, 27=SCI4, 26=SCI5, 25=SCI6, 24=SCI7 */
    PROTECT_OFF();
    SYS_MSTPCRB &= ~(1 << (31-DEBUG_UART_SCI));
    PROTECT_ON();

    /* Disable RX/TX */
    SCI_SCR(DEBUG_UART_SCI) = 0;

#ifdef TARGET_rx72n
    /* Configure P13/P12 for UART */
    PORT_PMR(0x1) |= ((1 << 2) | (1 << 3));
#else
    /* Configure PC3/PC2 for UART */
    PORT_PMR(0xC) |= ((1 << 2) | (1 << 3));
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
    MPC_PFS(0xC2) = 0xA; /* PC2-RXD5 */
    MPC_PFS(0xC3) = 0xA; /* PC3-TXD5 */
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
#define CFG_PLL_MUL (SYS_CLK / CFG_HCO_FRQ)

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
    #define PLL_MUL_STC ((uint8_t)(CFG_PLL_MUL - 1))
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
#if defined(WOLFBOOT_RENESAS_TSIP) &&\
    !defined(WOLFBOOT_RENESAS_APP)
    int err;
    uint32_t key_type = 0;
    int tsip_key_type = -1;
    struct rsa2048_pub *encrypted_user_key_data;
#endif

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

#ifdef USE_RENESAS_BSP
    if (R_FLASH_Open() != FLASH_SUCCESS)
        hal_panic();
#endif

#if defined(WOLFBOOT_RENESAS_TSIP) && \
    !defined(WOLFBOOT_RENESAS_APP)
    err = wolfCrypt_Init();
    if (err != 0) {
       wolfBoot_printf("ERROR: wolfCrypt_Init %d\n", err);
       hal_panic();
    }

    /* retrive installed pubkey data from flash */
    encrypted_user_key_data = (struct rsa2048_pub*)keystore_get_buffer(0);

    key_type = keystore_get_key_type(0);
    switch(key_type){

        case AUTH_KEY_RSA2048:
            tsip_key_type = TSIP_RSA2048;
            break;
        case AUTH_KEY_RSA4096:
            tsip_key_type = TSIP_RSA4096;
            break;
        case AUTH_KEY_ED448:
        case AUTH_KEY_ECC384:
        case AUTH_KEY_ECC521:
        case AUTH_KEY_ED25519:
        case AUTH_KEY_ECC256:
        case AUTH_KEY_RSA3072:
        default:
            tsip_key_type = -1;
            break;
    }

    if (tsip_key_type == -1) {
        wolfboot_printf("key type (%d) not supported\n", key_type);
        hal_panic();
    }
    /* inform user key */
    tsip_inform_user_keys_ex((byte*)&encrypted_user_key_data->wufpk,
                            (byte*)&encrypted_user_key_data->initial_vector,
                            (byte*)&encrypted_user_key_data->encrypted_user_key,
                            0/* dummy */);
    /* TSIP specific RSA public key */
    if (tsip_use_PublicKey_buffer_crypt(&pkInfo,
                (const char*)&encrypted_user_key_data->encrypted_user_key,
                 RSA2048_PUB_SIZE,
                 tsip_key_type) != 0) {
            wolfboot_printf("ERROR tsip_use_PublicKey_buffer\n");
            hal_panic();
    }
    /* Init Crypt Callback */
    pkInfo.sing_hash_type = sha256_mac;
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);
    if (err < 0) {
        wolfboot_printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n", err);
        hal_panic();
    }
#endif

}

void hal_prepare_boot(void)
{

}

#define IS_FLASH(addr) (addr) >= FLASH_ADDR ? 1 : 0

#ifdef USE_RENESAS_BSP

#define MIN_PROG (0x8000)
#define ALIGN_FLASH(a) ((a) / MIN_PROG * MIN_PROG)
static uint8_t save[MIN_PROG];

int blockWrite(const uint8_t *data, uint32_t addr, int len)
{
    for(; len; len-=MIN_PROG, data+=MIN_PROG, addr+=MIN_PROG) {
        memcpy(save, data, MIN_PROG); /* for the case "data" ls a flash address */
        if(R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            return -1;
    }
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t addr, const uint8_t *data, int len)
{
    uint32_t save_len = 0;

    if (addr != ALIGN_FLASH(addr)) {
        save_len = (addr - ALIGN_FLASH(addr)) < (uint32_t)len ?
            (addr - ALIGN_FLASH(addr)) : (uint32_t)len;
        memcpy(save, (const void *)ALIGN_FLASH(addr), MIN_PROG);
        memcpy(save + (addr - ALIGN_FLASH(addr)), data, save_len);
        addr   = ALIGN_FLASH(addr);
        if (R_FLASH_Erase((flash_block_address_t)addr, 1) != FLASH_SUCCESS)
            return -1;
        if (R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            return -1;
        len -= save_len;
        data += save_len;
        addr += MIN_PROG;
    }

    if (len > 0) {
        if (blockWrite(data, addr, ALIGN_FLASH(len)) < 0)
            goto error;
        addr += ALIGN_FLASH(len);
        data += ALIGN_FLASH(len);
        len  -= ALIGN_FLASH(len);
    }

    if (len > 0) {
        memcpy(save, (const void *)addr, MIN_PROG);
        memcpy(save, data, len);
        if (R_FLASH_Erase((flash_block_address_t)addr, 1) != FLASH_SUCCESS)
            return -1;
        if (R_FLASH_Write((uint32_t)save, addr, MIN_PROG) != FLASH_SUCCESS)
            goto error;
    }
    return 0;
error:
    return -1;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    /* last blocks are 8KB */
    int block_size = address >= 0xffff0000UL ?
         FLASH_CF_SMALL_BLOCK_SIZE :  FLASH_CF_MEDIUM_BLOCK_SIZE;

    if (len % block_size != 0)
        return -1;
    for ( ; len; address+=block_size, len-=block_size) {
        if (R_FLASH_Erase((flash_block_address_t)address, 1)
                != FLASH_SUCCESS)
            return -1;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_access_window_config_t info;

    info.start_addr = (uint32_t) FLASH_CF_BLOCK_INVALID;
    info.end_addr   = (uint32_t) FLASH_CF_BLOCK_0;
    R_BSP_InterruptsDisable();
    if(R_FLASH_Control(FLASH_CMD_ACCESSWINDOW_SET, (void *)&info)
        != FLASH_SUCCESS)
        hal_panic();
    R_BSP_InterruptsEnable();
    return;
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_access_window_config_t info;
    info.start_addr = (uint32_t) FLASH_CF_BLOCK_END;
    info.end_addr   = (uint32_t) FLASH_CF_BLOCK_END;
    R_BSP_InterruptsDisable();
    if(R_FLASH_Control(FLASH_CMD_ACCESSWINDOW_SET, (void *)&info)
        != FLASH_SUCCESS)
        hal_panic();
    R_BSP_InterruptsEnable();
    return;
}

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    flash_cmd_t cmd = FLASH_CMD_SWAPFLAG_TOGGLE;
    wolfBoot_printf("FLASH_CMD_SWAPFLAG_TOGGLE=%d\n", FLASH_CMD_SWAPFLAG_TOGGLE);
    hal_flash_unlock();
    if(R_FLASH_Control(cmd, NULL) != FLASH_SUCCESS)
        hal_panic();
    hal_flash_lock();

}
#else

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
#endif /* USE_RENESAS_BSP */

void* hal_get_primary_address(void)
{
    return (void*)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void*)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}



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
