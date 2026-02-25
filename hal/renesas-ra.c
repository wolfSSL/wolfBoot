/* renesas-ra.c
 *
 * wolfBoot HAL for Renesas RA series (RA6M4).
 * Direct FACI HP register access — no FSP driver dependency.
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

#include <stdint.h>
#include <string.h>

#include "user_settings.h"
#include <target.h>
#include "hal.h"
#include "renesas-ra.h"

void uart_init();
void uart_write(const char* buf, unsigned int sz);

#if defined(WOLFSSL_RENESAS_SCEPROTECT_CRYPTONLY) && \
    !defined(WOLFBOOT_RENESAS_APP)
#   include "wolfssl/wolfcrypt/wc_port.h"
#   include "wolfssl/wolfcrypt/port/Renesas/renesas-sce-crypt.h"
#   include "wolfssl/wolfcrypt/port/Renesas/renesas_sync.h"
    User_SCEPKCbInfo pkInfo;
    sce_rsa2048_public_wrapped_key_t wrapped_rsapub2048;
#endif

#define FLASH_READY() while (!(FLASH_FSTATR & FLASH_FSTATR_FRDY));

static inline void hal_panic(void)
{
    while(1)
        ;
}

/* ------------------------------------------------------------------ */
/* flash_check_error: read FSTATR and return first error found         */
/* ------------------------------------------------------------------ */
static flash_err_t RAMFUNCTION flash_check_error(void)
{
    uint32_t st = FLASH_FSTATR;

    if (st & FLASH_FSTATR_ILGLERR)return FLASH_ERR_ILGL;
    if (st & FLASH_FSTATR_PRGERR) return FLASH_ERR_PRG;
    if (st & FLASH_FSTATR_ERSERR) return FLASH_ERR_ERS;
    if (st & FLASH_FSTATR_FLWEERR)return FLASH_ERR_FLWE;
    if (st & FLASH_FSTATR_FESETERR) return FLASH_ERR_FESET;
    if (st & FLASH_FSTATR_SECERR) return FLASH_ERR_SEC;
    if (st & FLASH_FSTATR_OTERR) return FLASH_ERR_OT;

    return FLASH_OK;
}

/* ------------------------------------------------------------------ */
/* SCE initialisation (WOLFBOOT_RENESAS_SCEPROTECT only)              */
/* ------------------------------------------------------------------ */
#if defined(WOLFBOOT_RENESAS_SCEPROTECT) && \
    !defined(WOLFBOOT_RENESAS_APP)
static int sipInitDone = 0;

int hal_renesas_init(void)
{
    fsp_err_t err;
    uint32_t *pubkey;

    if (sipInitDone)
        return 0;

    pubkey = keystore_get_buffer(0);

    err = wolfCrypt_Init();
    if (err != 0) {
        wolfBoot_printf("ERROR: wolfCrypt_Init %d\n", err);
        hal_panic();
    }

    XMEMSET(&pkInfo, 0, sizeof(pkInfo));
    pkInfo.sce_wrapped_key_rsapub2048 =
        (sce_rsa2048_public_wrapped_key_t*)&wrapped_rsapub2048;
    XMEMCPY(&wrapped_rsapub2048.value, (uint32_t*)pubkey,
        sizeof(wrapped_rsapub2048.value));

    wrapped_rsapub2048.type = SCE_KEY_INDEX_TYPE_RSA2048_PUBLIC;
    pkInfo.keyflgs_crypt.bits.rsapub2048_installedkey_set = 1;
    pkInfo.keyflgs_crypt.bits.message_type = 1;
    err = wc_CryptoCb_CryptInitRenesasCmn(NULL, &pkInfo);
    if (err < 0) {
        wolfBoot_printf("ERROR: wc_CryptoCb_CryptInitRenesasCmn %d\n",
            err);
        return err;
    }
    sipInitDone = 1;
    return 0;
}
#endif /* WOLFBOOT_RENESAS_SCEPROTECT */

/* ------------------------------------------------------------------ */
/* Debug UART (SCI7, P613=TXD7, J23 pin2 on EK-RA6M4)                */
/* ------------------------------------------------------------------ */
#ifdef DEBUG_UART
void uart_init(void)
{
    /* Release SCI7 from module stop */
    R_MSTP_MSTPCRB &= ~MSTPCRB_SCI7;

    /* Disable TX/RX for configuration */
    SCI_SCR = 0x00u;

    /* Async, 8N1, internal clock (CKS=00) */
    SCI_SMR = 0x00u;

    /* CHR1=1: 8-bit character length */
    SCI_SCMR |= SCI_SCMR_CHR1;

    /* BGDM=1, ABCS=1: divide clock by 8 */
    SCI_SEMR = SCI_SEMR_BGDM | SCI_SEMR_ABCS;

    /* Baud rate register */
    SCI_BRR = (uint8_t)SCI_BRR_VAL;

    /* Enable PFS write */
    R_PMISC_PWPR = 0x00u;
    R_PMISC_PWPR = PWPR_PFSWE;

    /* P613: TXD7 peripheral output, idle-high */
    R_PFS_P613 = PFS_PSEL_SCI7 | PFS_PMR | PFS_PDR | PFS_PODR;

    /* Disable PFS write */
    R_PMISC_PWPR = 0x00u;
    R_PMISC_PWPR = PWPR_B0WI;

    /* Enable transmit */
    SCI_SCR = SCI_SCR_TE;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        char c = buf[i];
        if (c == '\n') {
            while (!(SCI_SSR & SCI_SSR_TEND));
            SCI_TDR = '\r';
        }
        while (!(SCI_SSR & SCI_SSR_TEND));
        SCI_TDR = (uint8_t)c;
    }
}
#endif /* DEBUG_UART */

/* ------------------------------------------------------------------ */
/* hal_flash_init: one-time flash controller setup                    */
/* ------------------------------------------------------------------ */
static int hal_flash_init(void)
{
    /* Notify FACI of PCLKA frequency so timing parameters are correct.
     * FSP sets this in R_FLASH_HP_Open(); without it the FACI uses
     * whatever power-on default is in FPCKAR, causing ILGLERR on any
     * P/E operation.  KEY=0x1E, PCKA = (PCLKA_MHz - 1). */
    FLASH_FPCKAR = (uint16_t)(FLASH_FPCKAR_KEY | (RA_PCLKA_MHZ - 1U));

    /* Enter read mode and wait for ready */
    FLASH_FENTRYR = FLASH_FENTRYR_KEY;
    FLASH_READY()

    /* Enable program/erase for the lifetime of wolfBoot */
    FLASH_FWEPROR = FLASH_FWEPROR_FLWE;
    return 0;
}

/* Linker-generated symbols — BSP_CFG_C_RUNTIME_INIT=0, so FSP does not
 * initialize these sections automatically.
 */
/* .ram_code_from_flash + .data : copy from flash LMA to RAM VMA */
extern uint32_t __ram_from_flash$$Base;
extern uint32_t __ram_from_flash$$Limit;
extern uint32_t __ram_from_flash$$Load;
/* .bss : must be zeroed (warm reset leaves RAM with stale values) */
extern uint32_t __ram_zero$$Base;
extern uint32_t __ram_zero$$Limit;

static void copy_ram_sections(void)
{
    uint32_t *src;
    uint32_t *dst;

    /* 1. Copy RAMFUNCTION code + .data from flash to RAM.
     *    Do NOT call memcpy() here — wolfBoot's memcpy is itself in this
     *    region and has not been copied yet. */
    src = &__ram_from_flash$$Load;
    dst = &__ram_from_flash$$Base;
    while (dst < &__ram_from_flash$$Limit)
    {
        *dst++ = *src++;
    }

    /* 2. Zero .bss.
     *    A debugger warm-reset does not clear RAM, so without this step
     *    global variables expected to be 0 will contain stale values. */
    dst = &__ram_zero$$Base;
    while (dst < &__ram_zero$$Limit)
    {
        *dst++ = 0u;
    }
}

/* ------------------------------------------------------------------ */
/* hal_init                                                            */
/* ------------------------------------------------------------------ */
void hal_init(void)
{
#ifndef WOLFBOOT_RENESAS_APP
    /* wolfBoot only: BSP_CFG_C_RUNTIME_INIT=0, so FSP startup skips BSS
     * zero-init and .data copy.  Do them manually here.
     * When building as app (WOLFBOOT_RENESAS_APP defined), the FSP startup
     * already ran SystemRuntimeInit(0) which handles both — calling
     * copy_ram_sections() again would re-zero .bss and clobber
     * SystemCoreClock and other BSS variables. */
    copy_ram_sections();
#endif

    hal_flash_init();

#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif

#if defined(WOLFBOOT_RENESAS_SCEPROTECT) && \
    !defined(WOLFBOOT_RENESAS_APP)
    if (hal_renesas_init() != 0) {
        wolfBoot_printf("ERROR: hal_renesas_init\n");
        hal_panic();
    }
#endif
}

void hal_prepare_boot(void)
{
}

/* ------------------------------------------------------------------ */
/* hal_flash_unlock: enter code flash P/E mode                        */
/* ------------------------------------------------------------------ */
void RAMFUNCTION hal_flash_unlock(void)
{
    /* Ensure P/E is enabled (FWEPROR.FLWE=1).
     * FWEPROR is NOT in the FACI register block, so FRESETR does not
     * reset it.  Set it first so it is ready when P/E mode is entered.
     */
    FLASH_FWEPROR = FLASH_FWEPROR_FLWE;

    /* Reset the flash sequencer (FRESETR=1) to unconditionally clear any
     * stale ILGLERR / PRGERR / ERSERR / OTERR flags left by a previous
     * interrupted P/E operation. 
     */
    FLASH_FRESETR = 0x01U;
    FLASH_READY()
    FLASH_FRESETR = 0x00U;
    FLASH_READY()

    /* FRESETR may reset FPCKAR to its power-on default (0x0000).
     * Re-set it here (FRDY=1 guaranteed above) so the FACI uses the
     * correct PCLKA frequency for all timing parameters.
     * KEY=0x1E, PCKA = PCLKA_MHz - 1. */
    FLASH_FPCKAR = (uint16_t)(FLASH_FPCKAR_KEY | (RA_PCLKA_MHZ - 1U));

    /* Clear P/E mode entry protection: FMEPROT.CEPROT=0.*/
    FLASH_FMEPROT = (uint16_t)(FLASH_FMEPROT_KEY | 0U);

    /* Enter code flash P/E mode.*/
    FLASH_FENTRYR = FLASH_FENTRYR_KEY | FLASH_FENTRYR_CODE_PR;
    FLASH_READY()
}

/* ------------------------------------------------------------------ */
/* hal_flash_lock: return to read mode                                 */
/* ------------------------------------------------------------------ */
void RAMFUNCTION hal_flash_lock(void)
{
    FLASH_READY()
    FLASH_FENTRYR = FLASH_FENTRYR_KEY;
    FLASH_READY()
}

/* ------------------------------------------------------------------ */
/* hal_flash_erase: erase len bytes starting at address               */
/*   address must be block-aligned                                     */
/*   len must be a multiple of the block size                          */
/* ------------------------------------------------------------------ */
int RAMFUNCTION hal_flash_erase(uint32_t address, int int_len)
{
    uint32_t len = (uint32_t)int_len;

    while (len > 0) {
        uint32_t block_size;

#ifdef WOLFBOOT_DUALBANK
        block_size =
            ((address <= 0x80000 && address >= 0x10000) ||
              address >= 0x210000)
            ? (32 * 1024) : (8 * 1024);
#else
        block_size = (address >= 0x10000)
            ? (32 * 1024) : (8 * 1024);
#endif

        if (len < block_size)
            return -1;

        FLASH_FSADDR  = address;
        FACI_CMD8     = FACI_CMD_BLOCK_ERASE;
        FACI_CMD8     = FACI_CMD_EXECUTE;
        FLASH_READY()

        if (flash_check_error() != FLASH_OK) {
            FACI_CMD8 = FACI_CMD_FORCED_STOP;
            FLASH_READY()
            FACI_CMD8 = FACI_CMD_STATUS_CLR;
            return -1;
        }

        address += block_size;
        len     -= block_size;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* flash_write_128: write exactly FACI_PROG_UNIT (128) bytes          */
/*   addr must be 128-byte aligned                                    */
/*   data must point to a RAM buffer (not code flash)                 */
/* ------------------------------------------------------------------ */
static int RAMFUNCTION flash_write_128(uint32_t addr,
                                       const uint8_t *data)
{
    int i;
    const uint16_t *d16 = (const uint16_t *)data;

    FLASH_READY() /* ensure FCU idle */
    FLASH_FSADDR = addr;
    FACI_CMD8    = FACI_CMD_PROGRAM;
    FACI_CMD8    = FACI_CMD_PROGRAM_LEN;   /* 0x40 = 64 words */
    for (i = 0; i < 64; i++) {
        while (FLASH_FSTATR & FLASH_FSTATR_DBFULL);
        FACI_CMD16 = d16[i];
    }
    FACI_CMD8 = FACI_CMD_EXECUTE;
    FLASH_READY()

    if (flash_check_error() != FLASH_OK) {
        FACI_CMD8 = FACI_CMD_FORCED_STOP;
        FLASH_READY()
        FACI_CMD8 = FACI_CMD_STATUS_CLR;
        return -1;
    }
    return 0;
}

/* Static buffer used by hal_flash_write (must be in RAM) */
static uint8_t prog_buf[FACI_PROG_UNIT];

/* ------------------------------------------------------------------ */
/* flash_read_sector: read FACI_PROG_UNIT bytes from code flash       */
/*   Must be called in read mode (FENTRYR.FENTRY0 = 0).               */
/*   Temporarily exits P/E mode, reads, then re-enters P/E mode.      */
/* ------------------------------------------------------------------ */
static void RAMFUNCTION flash_read_sector(uint32_t addr, uint8_t *buf)
{
    /* Exit P/E mode to read flash correctly (P/E mode returns 0xFF) */
    FLASH_FENTRYR = FLASH_FENTRYR_KEY;
    while (!(FLASH_FSTATR & FLASH_FSTATR_FRDY));

    memcpy(buf, (const uint8_t *)addr, FACI_PROG_UNIT);

    /* Re-enter P/E mode (FMEPROT already cleared in hal_flash_unlock) */
    FLASH_FENTRYR = FLASH_FENTRYR_KEY | FLASH_FENTRYR_CODE_PR;
    while (!(FLASH_FSTATR & FLASH_FSTATR_FRDY));
}

/* ------------------------------------------------------------------ */
/* hal_flash_write: write int_len bytes to addr                       */
/*   Unaligned start/end are handled with read-modify-write.          */
/*   Reads from code flash require exiting P/E mode first             */
/*   (RA6M4 FACI HP returns 0xFF for all reads while in P/E mode).   */
/* ------------------------------------------------------------------ */
int RAMFUNCTION hal_flash_write(uint32_t addr,
                                const uint8_t *data, int int_len)
{
    uint32_t len       = (uint32_t)int_len;
    uint32_t unaligned = addr % FACI_PROG_UNIT;

    /* --- Handle unaligned start --- */
    if (unaligned) {
        uint32_t aligned = addr - unaligned;
        uint32_t chunk   = FACI_PROG_UNIT - unaligned;
        if (chunk > len)
            chunk = len;

        flash_read_sector(aligned, prog_buf);
        memcpy(prog_buf + unaligned, data, chunk);
        if (flash_write_128(aligned, prog_buf) < 0)
            return -1;

        addr += chunk;
        data += chunk;
        len  -= chunk;
    }

    /* --- Aligned 128-byte blocks --- */
    while (len >= FACI_PROG_UNIT) {
        memcpy(prog_buf, data, FACI_PROG_UNIT);
        if (flash_write_128(addr, prog_buf) < 0)
            return -1;
        addr += FACI_PROG_UNIT;
        data += FACI_PROG_UNIT;
        len  -= FACI_PROG_UNIT;
    }

    /* --- Handle trailing partial block --- */
    if (len > 0) {
        flash_read_sector(addr, prog_buf);
        memcpy(prog_buf, data, len);
        if (flash_write_128(addr, prog_buf) < 0)
            return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Dual-bank support                                                   */
/* ------------------------------------------------------------------ */
#ifdef WOLFBOOT_DUALBANK

/* FSUAC: Flash Startup Area Control Register */
#define FLASH_FSUAC         REG16(R_FACI_BASE + 0x54)
#define FLASH_FSUAC_KEY     (0x6600)
#define FLASH_FSUAC_SAS_MSK (0x3)

void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    uint16_t cur = FLASH_FSUAC & FLASH_FSUAC_SAS_MSK;
    hal_flash_unlock();
    FLASH_FSUAC = (uint16_t)(FLASH_FSUAC_KEY | (cur ^ 1u));
    hal_flash_lock();
}

void *hal_get_primary_address(void)
{
    return (void *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void *hal_get_update_address(void)
{
    return (void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}

#endif /* WOLFBOOT_DUALBANK */
