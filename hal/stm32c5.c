/* stm32c5.c
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

/* STM32C5 family (e.g. STM32C5A3ZGT6 on NUCLEO-C5A3ZG). Cortex-M33
 * without TrustZone in this configuration.  Dual-bank 1 MB flash on
 * the -ZG variant (2 x 512 KB), 8 KB pages, 128-bit (quad-word) write
 * quantum with per-quad-word ECC.
 *
 * Default sysclk after reset is HSIDIV3 = HSIS / 3 (RCC_CFGR1.SW=0).
 * This port brings SYSCLK to 144 MHz via the PSIS clock chain (HSE
 * 48 MHz reference) in clock_init().  When WOLFBOOT_RESTORE_CLOCK
 * is defined (default in options.mk), hal_prepare_boot() switches
 * SYSCLK back to HSIDIV3 before handoff but leaves PSIS, PSI and
 * HSE running so the loaded firmware's own clock_psi_on() bumps
 * SYSCLK back up to 144 MHz cleanly without re-stabilizing HSE.
 * Pass WOLFBOOT_RESTORE_CLOCK=0 to skip the SYSCLK switch entirely
 * and have the loaded firmware inherit PSIS @ 144 MHz directly.
 */

#include <stdint.h>
#include <string.h>
#include <image.h>
#include "hal/stm32c5.h"
#include "hal.h"
#include "printf.h"


static void RAMFUNCTION flash_set_waitstates(unsigned int waitstates)
{
    uint32_t reg = FLASH_ACR;
    if ((reg & FLASH_ACR_LATENCY_MASK) != waitstates) {
        FLASH_ACR = (reg & ~FLASH_ACR_LATENCY_MASK) | waitstates;
        while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != waitstates)
            ;
    }
}

static RAMFUNCTION void flash_wait_complete(void)
{
    while ((FLASH_SR & (FLASH_SR_BSY | FLASH_SR_WBNE | FLASH_SR_DBNE)) != 0)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* On STM32C5, error flags are cleared via the dedicated FLASH_CCR
     * register (write 1 to clear).  Always clear all known flags so a
     * stale error from a prior cycle does not block the next operation.
     */
    FLASH_CCR = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR |
                FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |
                FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPTCHANGEERR;
}

/* Mask of FLASH_SR error flags worth aborting on. */
#define FLASH_SR_ERR_MASK (FLASH_SR_WRPERR | FLASH_SR_PGSERR | \
                           FLASH_SR_STRBERR | FLASH_SR_INCERR | \
                           FLASH_SR_OPTCHANGEERR)

/* C5 flash programming requires 128-bit (quad-word, 16-byte) writes
 * to produce valid ECC.  Partial writes leave the per-quad-word ECC
 * undefined and reads come back with bit-flipped "corrected" data.
 * For unaligned heads/tails we read the existing flash content and
 * merge so that every written quad-word is a complete ECC block.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    uint32_t qword[4];
    uint8_t *qword_bytes = (uint8_t *)qword;
    uint8_t *dst = (uint8_t *)address;

    while (i < len) {
        uint32_t cur_addr = (uint32_t)dst + i;
        uint32_t *dst_aligned = (uint32_t *)(cur_addr & 0xFFFFFFF0U);
        int byte_offset = (int)(cur_addr - (uint32_t)dst_aligned);
        int write_len = 16 - byte_offset;
        int j;
        uint32_t sr;

        if (write_len > len - i)
            write_len = len - i;

        /* Build the full 16-byte ECC block: start with the existing
         * flash content for any head/tail bytes outside the requested
         * range, then overlay the caller's bytes.  memcpy keeps the
         * source pointer alignment-agnostic.
         */
        memcpy(qword_bytes, (const uint8_t *)dst_aligned, 16);
        memcpy(qword_bytes + byte_offset, data + i, write_len);

        flash_wait_complete();
        flash_clear_errors();

        FLASH_CR |= FLASH_CR_PG;
        for (j = 0; j < 4; j++) {
            dst_aligned[j] = qword[j];
            ISB();
        }
        flash_wait_complete();

        sr = FLASH_SR;
        if ((sr & FLASH_SR_EOP) != 0)
            FLASH_CCR = FLASH_CCR_CLR_EOP;
        if ((sr & FLASH_SR_ERR_MASK) != 0) {
            flash_clear_errors();
            FLASH_CR &= ~FLASH_CR_PG;
            hal_cache_invalidate();
            return -1;
        }

        FLASH_CR &= ~FLASH_CR_PG;
        i += write_len;
        DSB();
    }
    hal_cache_invalidate();
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) != 0) {
        FLASH_KEYR = FLASH_KEY1;
        DMB();
        FLASH_KEYR = FLASH_KEY2;
        DMB();
        while ((FLASH_CR & FLASH_CR_LOCK) != 0)
            ;
    }
}

void RAMFUNCTION hal_flash_lock(void)
{
    flash_wait_complete();
    if ((FLASH_CR & FLASH_CR_LOCK) == 0)
        FLASH_CR |= FLASH_CR_LOCK;
}

void RAMFUNCTION hal_flash_opt_unlock(void)
{
    flash_wait_complete();
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        DMB();
        FLASH_OPTKEYR = FLASH_OPTKEY2;
        DMB();
        while ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) != 0)
            ;
    }
}

/* Re-lock the option control register only.  Callers that actually
 * want to commit modified option bytes must set OPTSTRT themselves
 * (and wait for completion) before calling this; setting OPTSTRT here
 * unconditionally would trigger an option-byte program/reload on
 * every lock and is a foot-gun.
 */
void RAMFUNCTION hal_flash_opt_lock(void)
{
    flash_wait_complete();
    if ((FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) == 0)
        FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
}

/* Page erase.  PNB[5:0] selects the page within the bank; BKSEL (bit 31)
 * selects bank 2 when the address falls into the upper half of flash.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end;
    uint32_t p;
    uint32_t sr;

    flash_clear_errors();
    if (len <= 0)
        return -1;
    if (address < ARCH_FLASH_OFFSET)
        return -1;
    /* Reject ranges that extend past the last valid flash byte
     * up-front rather than partially erasing and returning success.
     */
    if ((uint64_t)address + (uint64_t)len > (uint64_t)FLASH_TOP + 1U)
        return -1;

    /* End-exclusive bound so a single-page erase (e.g. len ==
     * FLASH_PAGE_SIZE, or smaller len that still resolves to one
     * page) actually runs the loop body.
     */
    end = address + (uint32_t)len;
    for (p = address; p < end; p += FLASH_PAGE_SIZE) {
        uint32_t reg;
        uint32_t bksel = 0;
        uint32_t base;

        if (p >= FLASH_BANK2_BASE) {
            bksel = FLASH_CR_BKSEL;
            base = FLASH_BANK2_BASE;
        } else {
            base = FLASHMEM_ADDRESS_SPACE;
        }

        reg = FLASH_CR & ~((uint32_t)(FLASH_CR_PNB_MASK << FLASH_CR_PNB_SHIFT) |
                           FLASH_CR_BKSEL);
        reg |= (((p - base) / FLASH_PAGE_SIZE) << FLASH_CR_PNB_SHIFT) |
               FLASH_CR_PER | bksel;
        FLASH_CR = reg;
        DMB();
        FLASH_CR |= FLASH_CR_STRT;
        flash_wait_complete();

        sr = FLASH_SR;
        if ((sr & FLASH_SR_ERR_MASK) != 0) {
            flash_clear_errors();
            FLASH_CR &= ~FLASH_CR_PER;
            hal_cache_invalidate();
            return -1;
        }
    }
    FLASH_CR &= ~FLASH_CR_PER;
    hal_cache_invalidate();
    return 0;
}

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)

/* RAMFUNCTION reset helper for hal_flash_dualbank_swap().  Mirrors the
 * test app's system_reset(): preserves AIRCR.PRIGROUP, DSB before/after.
 */
static void RAMFUNCTION stm32c5_reboot(void)
{
    uint32_t prigroup = AIRCR & 0x0700U;
    DSB();
    AIRCR = AIRCR_VKEY | prigroup | AIRCR_SYSRESETREQ;
    DSB();
    while (1)
        ;
}

/* Toggle FLASH_OPTCR.SWAP_BANK via the OPTSR_CUR / OPTSR_PRG / OPTSTRT
 * sequence and reboot.  The new bank mapping takes effect on reset (no
 * separate OBL_LAUNCH register on C5).  Modeled on hal/stm32h5.c.
 */
void RAMFUNCTION hal_flash_dualbank_swap(void)
{
    uint32_t cur;

    cur = FLASH_OPTSR_CUR & FLASH_OPTSR_SWAP_BANK;
    flash_clear_errors();
    hal_flash_unlock();
    hal_flash_opt_unlock();

    if (cur != 0)
        FLASH_OPTSR_PRG &= ~FLASH_OPTSR_SWAP_BANK;
    else
        FLASH_OPTSR_PRG |=  FLASH_OPTSR_SWAP_BANK;

    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    DMB();
    hal_flash_opt_lock();
    hal_flash_lock();
    stm32c5_reboot();
}

/* Mirror the wolfBoot image into bank 2 so 0x08000000 boots correctly
 * regardless of which physical bank SWAP_BANK currently maps there.
 * Only runs when bank 2 doesn't already match.  Stages 4 KB at a time
 * via a static RAM buffer to keep stack pressure bounded.
 */
#define BOOTLOADER_COPY_MEM_SIZE  (0x1000)
static uint8_t bootloader_copy_mem[BOOTLOADER_COPY_MEM_SIZE];

static void fork_bootloader(void)
{
    uint32_t src = FLASHMEM_ADDRESS_SPACE;
    uint32_t dst = FLASH_BANK2_BASE;
    int i;

    if (memcmp((const void *)src, (const void *)dst, BOOTLOADER_SIZE) == 0)
        return;

    hal_flash_unlock();
    hal_flash_erase(dst, BOOTLOADER_SIZE);
    for (i = 0; i < (int)BOOTLOADER_SIZE; i += BOOTLOADER_COPY_MEM_SIZE) {
        memcpy(bootloader_copy_mem, (const void *)(src + i),
               BOOTLOADER_COPY_MEM_SIZE);
        hal_flash_write(dst + i, bootloader_copy_mem,
                        BOOTLOADER_COPY_MEM_SIZE);
    }
    hal_flash_lock();
}

#endif /* DUALBANK_SWAP && __WOLFBOOT */

/* --- UART: USART2 on PA2 (TX) / PA3 (RX), AF7 (NUCLEO-C5A3ZG VCP).
 * Register/peripheral macros live in stm32c5.h; the values below are
 * board/clock specific and stay here.
 */

#define UART_TX_PIN         (2)
#define UART_RX_PIN         (3)
#define UART_PIN_AF         (7)

/* PCLK1 == SYSCLK after clock_init (PPRE1 = /1). */
#define USART2_PCLK         (144000000U)

#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

static void uart2_pins_setup(void)
{
    uint32_t reg;

    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    reg = RCC_AHB2ENR;
    (void)reg;

    reg = GPIOA_MODER & ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_TX_PIN * 2));
    reg = GPIOA_MODER & ~(0x3u << (UART_RX_PIN * 2));
    GPIOA_MODER = reg | (0x2u << (UART_RX_PIN * 2));

    reg = GPIOA_AFRL & ~(0xFu << (UART_TX_PIN * 4));
    GPIOA_AFRL = reg | (UART_PIN_AF << (UART_TX_PIN * 4));
    reg = GPIOA_AFRL & ~(0xFu << (UART_RX_PIN * 4));
    GPIOA_AFRL = reg | (UART_PIN_AF << (UART_RX_PIN * 4));

    GPIOA_PUPDR &= ~(0x3u << (UART_TX_PIN * 2));
    GPIOA_PUPDR &= ~(0x3u << (UART_RX_PIN * 2));
}

void uart_init(void)
{
    uint32_t reg;

    uart2_pins_setup();

    RCC_APB1LENR |= RCC_APB1LENR_USART2EN;
    reg = RCC_APB1LENR;
    (void)reg;

    USART2_CR1 &= ~UART_CR1_UE;
    USART2_BRR = USART2_PCLK / 115200;
    USART2_CR1 |= UART_CR1_TE | UART_CR1_RE | UART_CR1_UE;
}

void uart_write(const char *buf, unsigned int sz)
{
    while (sz-- > 0) {
        while ((USART2_ISR & UART_ISR_TXE) == 0)
            ;
        USART2_TDR = *buf++;
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */

/* Generous polling budgets.  HSE on a 48 MHz crystal typically takes
 * a few ms to stabilize; the SYSCLK/PSIS switches complete in a few
 * cycles.  These are spin loops, not wall-clock counters, so the
 * actual wait scales with whatever clock is currently driving the
 * core (HSIDIV3 at boot -> very slow -> larger budget needed).
 */
#define HSE_TIMEOUT             0x100000u
#define PSI_TIMEOUT             0x10000u
#define SW_TIMEOUT              0x10000u

/* Bring SYSCLK to 144 MHz via the PSIS clock chain.  Mirrors the
 * sequence emitted by STM32CubeMX's mx_rcc_init() for this target:
 * HSE -> PSI (ref 48 MHz, out 144 MHz) -> PSIS -> SYSCLK, all bus
 * prescalers /1, flash 4 WS + WRHIGHFREQ delay 2.  On any timeout
 * we leave the chip on its reset clock so boot still proceeds.
 */
static void clock_psi_on(void)
{
    uint32_t reg;
    volatile uint32_t timeout;

    /* 1. Enable HSE (48 MHz on NUCLEO-C5A3ZG). */
    if ((RCC_CR1 & RCC_CR1_HSEON) == 0)
        RCC_CR1 |= RCC_CR1_HSEON;
    timeout = HSE_TIMEOUT;
    while (((RCC_CR1 & RCC_CR1_HSERDY) == 0) && (--timeout != 0))
        ;
    if (timeout == 0)
        return;

    /* 2. Configure PSI: ref source = HSE, ref freq = 48 MHz,
     *    output = 144 MHz.  PSIS/PSIDIV3/PSIK are all off at reset
     *    so a plain field write is safe on first boot.
     */
    reg = RCC_CR2 & ~RCC_CR2_PSI_FIELDS_MASK;
    reg |= RCC_CR2_PSIREFSRC_HSE | RCC_CR2_PSIREF_48MHZ |
           RCC_CR2_PSIFREQ_144MHZ;
    RCC_CR2 = reg;

    /* 3. Enable PSIS, wait for ready. */
    RCC_CR1 |= RCC_CR1_PSISON;
    timeout = PSI_TIMEOUT;
    while (((RCC_CR1 & RCC_CR1_PSISRDY) == 0) && (--timeout != 0))
        ;
    if (timeout == 0)
        return;

    /* 4. All bus prescalers /1.  Reset value is already 0; explicit
     *    write keeps the intent visible.
     */
    RCC_CFGR2 = 0;

    /* 5. Flash 4 WS + prefetch BEFORE switching SYSCLK to 144 MHz. */
    flash_set_waitstates(FLASH_ACR_LATENCY_4WS);
    FLASH_ACR |= FLASH_ACR_PRFTEN;

    /* 6. Switch SYSCLK to PSIS. */
    reg = RCC_CFGR1 & ~RCC_CFGR1_SW_MASK;
    RCC_CFGR1 = reg | RCC_CFGR1_SW_PSIS;
    timeout = SW_TIMEOUT;
    while (((RCC_CFGR1 & RCC_CFGR1_SWS_MASK) != RCC_CFGR1_SWS_PSIS) &&
           (--timeout != 0))
        ;
    if (timeout == 0)
        return;  /* SYSCLK is still HSIDIV3; skip 144-MHz-only setup. */

    /* 7. Programming delay 2 (required at HCLK >= 136 MHz). */
    reg = FLASH_ACR & ~FLASH_ACR_WRHIGHFREQ_MASK;
    FLASH_ACR = reg | FLASH_ACR_WRHIGHFREQ_DELAY2;
}

#ifdef WOLFBOOT_RESTORE_CLOCK
/* Lightweight system-clock restore: switch SYSCLK back to HSIDIV3.
 * PSIS, PSI, HSE, and FLASH_ACR are deliberately left at their
 * PSIS-runtime values (4 WS / WRHIGHFREQ delay 2 are over-provisioned
 * for HSIDIV3 16 MHz but harmless).  Mirrors ST's
 * HAL_RCC_ResetSystemClock() rather than the full HAL_RCC_Reset().
 *
 * Why not also disable HSE / tear down PSIS?  Rapid HSE disable
 * followed by re-enable in the loaded firmware's own clock_psi_on()
 * is not reliable on this part - the oscillator does not always
 * stabilize fast enough on a back-to-back cycle, the wait for
 * HSERDY times out, and the chip stays at HSIDIV3 with the loaded
 * firmware's UART baud divisor wrong by 9x.  Leaving HSE on means
 * clock_psi_on() in the loaded firmware skips the re-enable wait
 * entirely (HSEON / HSERDY already set), which both fixes the boot
 * hang and makes the loaded firmware's startup faster.  Loaded
 * firmware that needs the chip in a fully reset oscillator state
 * can clear RCC itself.
 */
static void clock_psi_off(void)
{
    volatile uint32_t timeout;

    /* 1. Make sure HSIDIV3 is on (the post-reset source). */
    if ((RCC_CR1 & RCC_CR1_HSIDIV3ON) == 0)
        RCC_CR1 |= RCC_CR1_HSIDIV3ON;
    timeout = PSI_TIMEOUT;
    while (((RCC_CR1 & RCC_CR1_HSIDIV3RDY) == 0) && (--timeout != 0))
        ;

    /* 2. Switch SYSCLK back to HSIDIV3. */
    RCC_CFGR1 = (RCC_CFGR1 & ~RCC_CFGR1_SW_MASK) | RCC_CFGR1_SW_HSIDIV3;
    timeout = SW_TIMEOUT;
    while (((RCC_CFGR1 & RCC_CFGR1_SWS_MASK) != RCC_CFGR1_SWS_HSIDIV3) &&
           (--timeout != 0))
        ;

    /* FLASH_ACR and RCC_CFGR2 are deliberately left at their PSIS
     * runtime values; over-provisioned for HSIDIV3 16 MHz but
     * harmless.  HSE / PSI tear-down is also deliberately not done
     * here - the crystal does not reliably restart in the window
     * between clock_psi_off() and the loaded firmware's own
     * clock_psi_on() during a swap-then-jump (no reset between).
     */
}
#endif /* WOLFBOOT_RESTORE_CLOCK */

void hal_init(void)
{
    clock_psi_on();
    hal_cache_enable(1);

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
#endif

#if defined(DUALBANK_SWAP) && defined(__WOLFBOOT)
    /* Make sure both bank starts hold an identical wolfBoot image so
     * that 0x08000000 boots correctly after a SWAP_BANK toggle.
     */
    fork_bootloader();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    /* Drain UART TX shift register before any SYSCLK change - otherwise
     * the trailing byte (still being shifted out at PSIS / 144 MHz BRR)
     * gets corrupted when SYSCLK switches and the next byte from the
     * loaded firmware is preceded by garbage on the wire.
     */
    while ((USART2_ISR & UART_ISR_TC) == 0)
        ;
#endif
#ifdef WOLFBOOT_RESTORE_CLOCK
    clock_psi_off();
#endif
}

void RAMFUNCTION hal_cache_enable(int way)
{
    uint32_t reg;

    /* Mask WAYSEL before OR-in so passing way=0 after a prior
     * way=1 call actually switches back to 1-way (1WAY == 0).
     */
    reg = ICACHE_CR & ~ICACHE_CR_WAYSEL;
    ICACHE_CR = reg | (way ? ICACHE_CR_2WAYS : ICACHE_CR_1WAY);
    ICACHE_CR |= ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_disable(void)
{
    ICACHE_CR &= ~ICACHE_CR_CEN;
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    if ((ICACHE_CR & ICACHE_CR_CEN) == 0)
        return;
    if ((ICACHE_SR & ICACHE_SR_BUSYF) == 0)
        ICACHE_CR |= ICACHE_CR_CACHEINV;
    while ((ICACHE_SR & ICACHE_SR_BSYENDF) == 0)
        ;
    /* ICACHE_SR is write-1-to-clear; use a plain store so we ack
     * only BSYENDF and don't accidentally clear other latched
     * flags such as ERRF.
     */
    ICACHE_SR = ICACHE_SR_BSYENDF;
}
