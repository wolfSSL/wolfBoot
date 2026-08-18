/* f28p55x.c
 *
 * HAL for the TI LAUNCHXL-F28P55X (TMS320F28P550SJ, C2000 C28x DSP).
 *
 * wolfBoot runs from flash bank0 (0x80000) in a word-addressed, CHAR_BIT==16
 * environment built with the TI cl2000 compiler.  It verifies a signed image
 * resident in bank1 (BOOT partition) and branches to it in place (XIP); see
 * src/boot_c2000.c for the handoff and docs/Targets.md for the flash map.
 *
 * Clocks/flash-waitstates/GPIO come from the C2000Ware device support
 * (Device_init); flash program/erase use the TI Flash API (Fapi) and must run
 * from RAM (RAMFUNCTION -> .TI.ramfunc).  The console is SCIA on GPIO28/29
 * (the XDS110 virtual COM, 115200 8N1), wired to wolfBoot_printf via DEBUG_UART.
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

#include <stdint.h>
#include <target.h>
#include "image.h"

#include "driverlib.h"
#include "device.h"
#include "FlashTech.h"

/* Flash controller base the Fapi library operates on (matches Flash_initModule
 * in Device_init).  Confirm against the Fapi sector table for the phase-2
 * program/erase path. */
#ifndef WOLFBOOT_C2000_FLASH_BASE
#define WOLFBOOT_C2000_FLASH_BASE FLASH0CTRL_BASE
#endif

/* Fapi programs a 64-bit (8 x 16-bit word) main-array slice at a time. */
#define C2000_FLASH_PGM_WORDS 8

/* --------------------------------------------------------------------- */
/* SCIA console (XDS110 virtual COM) - wolfBoot_printf via DEBUG_UART      */
/* --------------------------------------------------------------------- */
/* Debug-only mirror of all UART output into a RAM buffer, so boot progress can
 * be read back over JTAG (symbols g_log / g_logpos) independent of SCI flow (an
 * attached debugger garbles/stalls the XDS110 backchannel).  Placed in a NOINIT
 * section (RAMGS_SURV, 0xD000) that neither wolfBoot's nor the booted app's .bss
 * clears, so the boot log survives the do_boot() handoff and can be read back
 * after the app is running (g_logpos is reset in uart_init).  Gated on DEBUG so
 * production builds do not reserve the RAM or mirror every console byte. */
#ifdef DEBUG
#define WOLF_LOG_SZ 2048
#pragma DATA_SECTION(g_log,    ".survivelog")
#pragma DATA_SECTION(g_logpos, ".survivelog")
volatile char          g_log[WOLF_LOG_SZ];
volatile unsigned long g_logpos;
#endif

void uart_init(void)
{
#ifdef DEBUG
    g_logpos = 0;   /* reset the survive-log at each boot */
#endif
    /* RX pin */
    GPIO_setPinConfig(DEVICE_GPIO_CFG_SCIRXDA);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_SCIRXDA, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_SCIRXDA, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(DEVICE_GPIO_PIN_SCIRXDA, GPIO_QUAL_ASYNC);

    /* TX pin */
    GPIO_setPinConfig(DEVICE_GPIO_CFG_SCITXDA);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_SCITXDA, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_SCITXDA, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(DEVICE_GPIO_PIN_SCITXDA, GPIO_QUAL_ASYNC);

    SCI_performSoftwareReset(SCIA_BASE);
    SCI_setConfig(SCIA_BASE, DEVICE_LSPCLK_FREQ, 115200,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_resetChannels(SCIA_BASE);
    SCI_resetRxFIFO(SCIA_BASE);
    SCI_resetTxFIFO(SCIA_BASE);
    SCI_enableFIFO(SCIA_BASE);
    SCI_enableModule(SCIA_BASE);
    SCI_performSoftwareReset(SCIA_BASE);
}

/* Bounded-spin SCI put: wait a limited time for FIFO space, then drop.  This
 * flushes cleanly when the JTAG probe is detached (FIFO drains) yet never
 * stalls the CPU when a debug session is holding the XDS110 backchannel. */
static void sci_putc(uint16_t c)
{
    uint32_t spin = 20000;
    while ((SCI_getTxFIFOStatus(SCIA_BASE) == SCI_FIFO_TX16) && (spin-- > 0))
        ;
    /* If the bounded wait expired with the FIFO still full, drop the byte
     * rather than writing into a full FIFO (a stalled backchannel must not
     * wedge the console). */
    if (SCI_getTxFIFOStatus(SCIA_BASE) == SCI_FIFO_TX16)
        return;
    SCI_writeCharNonBlocking(SCIA_BASE, c);
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
#ifdef DEBUG
        if (g_logpos < (unsigned long)sizeof(g_log))
            g_log[g_logpos++] = buf[i];
#endif
        if (buf[i] == '\n')
            sci_putc((uint16_t)'\r');
        sci_putc((uint16_t)(buf[i] & 0xFF));
    }
}

/* --------------------------------------------------------------------- */
/* Flash API (Fapi) helpers - execute from RAM                            */
/* --------------------------------------------------------------------- */
void RAMFUNCTION hal_flash_unlock(void)
{
    /* (Re)initialize the Flash API for the active system frequency and select
     * bank0 as the FMC context.  Fapi has no global write-enable; per-command
     * sector protection is cleared in the write/erase paths. */
    (void)Fapi_initializeAPI((Fapi_FmcRegistersType *)WOLFBOOT_C2000_FLASH_BASE,
                             DEVICE_SYSCLK_FREQ / 1000000U);
    (void)Fapi_setActiveFlashBank(Fapi_FlashBank0);
}

void RAMFUNCTION hal_flash_lock(void)
{
    /* No persistent lock state to restore for Fapi. */
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /* address is a C28x flash word address; data cells each hold one octet
     * (the octet-per-cell header/trailer storage).  Program 8 words per Fapi
     * command; pad a short tail with 0xFFFF (leaves those cells erased). */
    const uint16_t *src = (const uint16_t *)data;
    uint32_t addr = address;
    int remaining = len;
    Fapi_StatusType st;
    uint16_t block[C2000_FLASH_PGM_WORDS];
    int i;

    while (remaining > 0) {
        for (i = 0; i < C2000_FLASH_PGM_WORDS; i++) {
            if (i < remaining)
                block[i] = src[i];
            else
                block[i] = 0xFFFFU;
        }
        st = Fapi_issueProgrammingCommand((uint32 *)addr, (uint16 *)block,
                                          C2000_FLASH_PGM_WORDS, 0, 0,
                                          Fapi_AutoEccGeneration);
        if (st != Fapi_Status_Success)
            return -1;
        while (Fapi_checkFsmForReady() == Fapi_Status_FsmBusy)
            ;
        if (Fapi_getFsmStatus() != 3)
            return -1;
        addr += C2000_FLASH_PGM_WORDS;
        src += C2000_FLASH_PGM_WORDS;
        remaining -= C2000_FLASH_PGM_WORDS;
    }
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    /* Erase every flash sector overlapping [address, address+len).  The Fapi
     * sector granularity is WOLFBOOT_SECTOR_SIZE (confirm against the Fapi
     * sector table). */
    uint32_t addr = address;
    uint32_t end = address + (uint32_t)len;
    Fapi_StatusType st;

    while (addr < end) {
        st = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector,
                                               (uint32 *)addr);
        if (st != Fapi_Status_Success)
            return -1;
        while (Fapi_checkFsmForReady() != Fapi_Status_FsmReady)
            ;
        if (Fapi_getFsmStatus() != 3)
            return -1;
        addr += WOLFBOOT_SECTOR_SIZE;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* wolfBoot HAL entry points                                              */
/* --------------------------------------------------------------------- */
#ifdef __WOLFBOOT
void hal_init(void)
{
    /* Device_init: 150 MHz PLL, flash wait states, and the .TI.ramfunc
     * copy-to-RAM (RamfuncsLoadStart -> RamfuncsRunStart). */
    Device_init();
    Device_initGPIO();
    uart_init();
    /* Prepare the Flash API for any later program/erase (phase-2 updates). */
    hal_flash_unlock();
}

void hal_prepare_boot(void)
{
    uint32_t spin = 200000;
    /* Quiesce before the XIP handoff: drain the SCI TX FIFO, then wait for the
     * transmit shift register to empty so the final byte is fully clocked out
     * before the application runs (otherwise a byte in flight is corrupted if
     * the app re-touches the clock).  Both waits are bounded so a stalled FIFO
     * (e.g. JTAG holding the backchannel) cannot hang the handoff. */
    while ((SCI_getTxFIFOStatus(SCIA_BASE) != SCI_FIFO_TX0) && (spin-- > 0))
        ;
    spin = 200000;
    while (SCI_isTransmitterBusy(SCIA_BASE) && (spin-- > 0))
        ;
    DINT;
}
#endif /* __WOLFBOOT */
