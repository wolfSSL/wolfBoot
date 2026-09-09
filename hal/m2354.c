/* m2354.c
 *
 * HAL for the Nuvoton NuMicro M2354 (Cortex-M23), NuMaker-M2354 board.
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

/* M2354KJFAE: Cortex-M23 at 96 MHz, 1 MB APROM in two banks, 256 KB SRAM.
 * Flash erases in 2048 byte pages and programs 32-bit words through the FMC
 * ISP engine. Driven directly rather than through the Nuvoton BSP. */

#ifndef WOLFBOOT_UNIT_TEST_FLASH
#include <stdint.h>

#include "image.h"
#include "hal.h"
#include "printf.h"
#include "loader.h"   /* wolfBoot_panic() */

#ifdef TZEN
#include "hal/armv8m_tz.h"
#endif
#endif /* !WOLFBOOT_UNIT_TEST_FLASH */

/* Register layout, flash geometry and the NS alias bit. */
#include "hal/m2354.h"

/* Redirected by the host unit test, which has no flash at these addresses. */
#ifndef FLASH_READ32
#define FLASH_READ32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif

/* The app's vector table lands at WOLFBOOT_PARTITION_BOOT_ADDRESS +
 * IMAGE_HEADER_SIZE and do_boot() writes that into VTOR. 132 exceptions means
 * VTOR's low 10 bits are RES0, so it must be 1024-aligned. Getting this wrong
 * is silent: the image still boots and only exceptions go astray. */
#if defined(WOLFBOOT_PARTITION_BOOT_ADDRESS) && defined(IMAGE_HEADER_SIZE)
#if (((WOLFBOOT_PARTITION_BOOT_ADDRESS) + (IMAGE_HEADER_SIZE)) & 0x3FF) != 0
#error "M2354: application vector table must be 1024-aligned. Raise IMAGE_HEADER_SIZE (1024) or move WOLFBOOT_PARTITION_BOOT_ADDRESS."
#endif
#endif

/* Bounded spins: loop counts, not real time, sized for a 96 MHz core. */
#define CLOCK_TIMEOUT (1000000)
#define FLASH_TIMEOUT (1000000)

/* --- Flash (FMC ISP) ------------------------------------------------------
 * Every ISP function is a RAMFUNCTION: the engine stalls the flash read port
 * during a program or erase, so its driver cannot execute from APROM. */

/* Run one ISP command. Returns 0, or -1 on timeout or ISP failure. */
#ifndef WOLFBOOT_UNIT_TEST_FLASH
static int RAMFUNCTION fmc_isp_run(uint32_t cmd, uint32_t addr, uint32_t data)
{
    uint32_t timeout = FLASH_TIMEOUT;

    /* The ISP engine addresses physical flash; drop the alias bit. */
    FMC_ISPCMD = cmd;
    FMC_ISPADDR = addr & ~NS_OFFSET;
    FMC_ISPDAT = data;
    FMC_ISPTRG = FMC_ISPTRG_ISPGO;
    /* ISPTRG.ISPGO self-clears on completion */
    while ((FMC_ISPTRG & FMC_ISPTRG_ISPGO) != 0) {
        if (--timeout == 0)
            return -1;
    }
    if ((FMC_ISPCTL & FMC_ISPCTL_ISPFF) != 0) {
        /* Write-1-to-clear */
        FMC_ISPCTL |= FMC_ISPCTL_ISPFF;
        return -1;
    }
    return 0;
}

/* Program one 16-byte aligned block in a single ISP command: one round trip
 * where the single-word command would cost four. */
static int RAMFUNCTION fmc_isp_program_multi(uint32_t addr, const uint32_t *w)
{
    uint32_t timeout = FLASH_TIMEOUT;

    FMC_ISPCMD = FMC_ISPCMD_PROGRAM_MUL;
    FMC_ISPADDR = addr & ~NS_OFFSET;
    FMC_MPDAT0 = w[0];
    FMC_MPDAT1 = w[1];
    FMC_MPDAT2 = w[2];
    FMC_MPDAT3 = w[3];
    FMC_ISPTRG = FMC_ISPTRG_ISPGO;
    while ((FMC_ISPTRG & FMC_ISPTRG_ISPGO) != 0) {
        if (--timeout == 0)
            return -1;
    }
    if ((FMC_ISPCTL & FMC_ISPCTL_ISPFF) != 0) {
        FMC_ISPCTL |= FMC_ISPCTL_ISPFF;
        return -1;
    }
    return 0;
}
#endif /* !WOLFBOOT_UNIT_TEST_FLASH */

#ifndef WOLFBOOT_UNIT_TEST_FLASH
void RAMFUNCTION hal_flash_unlock(void)
{
    SYS_UNLOCK();
    FMC_ISPCTL |= FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN;
}

void RAMFUNCTION hal_flash_lock(void)
{
    FMC_ISPCTL &= ~(FMC_ISPCTL_ISPEN | FMC_ISPCTL_APUEN);
    SYS_LOCK();
}
#endif /* !WOLFBOOT_UNIT_TEST_FLASH */

/* Program len bytes at address. The ISP engine programs one aligned 32-bit
 * word at a time, so unaligned head and tail bytes are merged with the word
 * already in flash. Programming can only clear bits, which is what the
 * caller's erase-then-write sequence relies on.
 */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    /* The ISP engine takes physical addresses, but a mapped read must keep
     * the alias it was given: a secure-alias read of flash attributed
     * non-secure returns zero, and the merge below would then program the
     * bytes outside the caller's range to 0x00. */
    uint32_t alias = address & NS_OFFSET;
    uint32_t addr = address & ~NS_OFFSET;
    uint32_t end;
    int i = 0;

    if (len == 0)
        return 0;
    /* A negative length is a caller bug or an integer underflow, not a
     * no-op: report it rather than folding it into the success path. */
    if (len < 0)
        return -1;
    if (data == NULL)
        return -1;

    /* APROM starts at 0, so only the top and the wrap need checking. */
    end = addr + (uint32_t)len;
    if (end > FLASH_APROM_END || end < addr)
        return -1;

    while (i < len) {
        uint32_t here = addr + (uint32_t)i;
        int remaining = len - i;

        /* Fast path: an aligned block fully covered by the caller's data
         * needs no merge and goes out in one multi-word command. */
        if ((here & (FMC_MULTI_WORD_ALIGN - 1)) == 0 &&
                remaining >= (int)FMC_MULTI_WORD_ALIGN) {
            uint32_t w[4];
            uint8_t *wb = (uint8_t *)w;
            int b;

            /* Byte-wise: the source has no alignment guarantee. */
            for (b = 0; b < (int)FMC_MULTI_WORD_ALIGN; b++)
                wb[b] = data[i + b];

            if (fmc_isp_program_multi(here, w) != 0)
                return -1;
            i += (int)FMC_MULTI_WORD_ALIGN;
            continue;
        }

        /* Slow path: merge into the word already in flash so bytes outside
         * the requested range keep their value. */
        {
            uint32_t word_addr = here & ~0x03UL;
            uint32_t offset = here - word_addr;
            uint32_t word = FLASH_READ32(word_addr | alias);
            uint8_t *b = (uint8_t *)&word;

            while (offset < 4 && i < len) {
                b[offset] = data[i];
                offset++;
                i++;
            }
            if (fmc_isp_run(FMC_ISPCMD_PROGRAM, word_addr, word) != 0)
                return -1;
        }
    }
    return 0;
}

/* True when every word of the page already reads as erased. */
static int RAMFUNCTION flash_page_is_blank(uint32_t page_addr)
{
    uint32_t off;

    for (off = 0; off < FLASH_PAGE_SIZE; off += 4) {
        if (FLASH_READ32(page_addr + off) != 0xFFFFFFFFUL)
            return 0;
    }
    return 1;
}

/* Erase every 2048 byte page overlapping [address, address + len).
 * The range is end-exclusive.
 */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    /* Physical address for the engine, the caller's alias for the blank
     * check: see hal_flash_write(). */
    uint32_t alias = address & NS_OFFSET;
    uint32_t addr = address & ~NS_OFFSET;
    uint32_t end;
    uint32_t p;

    if (len == 0)
        return 0;
    if (len < 0)
        return -1;

    end = addr + (uint32_t)len;
    /* APROM starts at 0, so only the top and the wrap need checking. */
    if (end > FLASH_APROM_END || end < addr)
        return -1;

    /* Round down to the containing page */
    p = addr & ~(FLASH_PAGE_SIZE - 1);
    while (p < end) {
        /* A page erase costs tens of milliseconds, the blank check tens of
         * microseconds, and most of an update partition is already erased. */
        if (!flash_page_is_blank(p | alias)) {
            if (fmc_isp_run(FMC_ISPCMD_PAGE_ERASE, p, 0) != 0)
                return -1;
        }
        p += FLASH_PAGE_SIZE;
    }
    return 0;
}

#ifndef WOLFBOOT_UNIT_TEST_FLASH

/* --- Clocks ---------------------------------------------------------------
 * Reset state is HIRC driving HCLK. Bring up the crystal, PLL to 96 MHz and
 * switch over. UART0 stays on HIRC so the console survives the change and a
 * failed PLL still prints. Every wait falls through on timeout: booting at
 * the reset clock beats hanging in the bootloader. */
static void clock_init(void)
{
    uint32_t timeout;

    SYS_UNLOCK();

    /* Only bank 0 is clocked out of reset and writes to an unclocked bank
     * are silently discarded. wolfBoot lives in bank 0 so it is safe before
     * this point; the application needs all three banks. */
    CLK_AHBCLK |= CLK_AHBCLK_SRAM0CKEN | CLK_AHBCLK_SRAM1CKEN |
                  CLK_AHBCLK_SRAM2CKEN;

    /* HIRC clocks the console and HCLK parks on it below, so enable it
     * explicitly: a warm reset can arrive with it off. Unlike the BSP,
     * wolfBoot never turns it back off. */
    CLK_PWRCTL |= CLK_PWRCTL_HIRCEN;
    timeout = CLOCK_TIMEOUT;
    while ((CLK_STATUS & CLK_STATUS_HIRCSTB) == 0) {
        if (--timeout == 0)
            goto done;
    }

    /* UART0 clock: HIRC, no divide, module clock on */
    CLK_CLKSEL2 = (CLK_CLKSEL2 & ~CLK_CLKSEL2_UART0SEL_Msk) |
                  CLK_CLKSEL2_UART0SEL_HIRC;
    CLK_CLKDIV0 &= ~CLK_CLKDIV0_UART0DIV_Msk;
    CLK_APBCLK0 |= CLK_APBCLK0_UART0CKEN;

    /* Start the external crystal */
    CLK_PWRCTL |= CLK_PWRCTL_HXTEN;
    timeout = CLOCK_TIMEOUT;
    while ((CLK_STATUS & CLK_STATUS_HXTSTB) == 0) {
        if (--timeout == 0)
            goto done;
    }

    /* PLL from HXT to 96 MHz */
    CLK_PLLCTL = CLK_PLLCTL_96MHZ_HXT;
    timeout = CLOCK_TIMEOUT;
    while ((CLK_STATUS & CLK_STATUS_PLLSTB) == 0) {
        if (--timeout == 0)
            goto done;
    }

    /* All of this must happen before HCLK rises. Park HCLK on HIRC (all
     * ones in HCLKSEL) so the core is never running from the old source
     * while the regulator transitions. Matters on the warm-reset path. */
    CLK_CLKSEL0 |= CLK_CLKSEL0_HCLKSEL_Msk;

    /* Only PL0 supports 96 MHz; a warm reset can arrive lower. WRBUSY
     * guards the write, PLCBUSY reports the change completing. */
    timeout = CLOCK_TIMEOUT;
    while ((SYS_PLCTL & SYS_PLCTL_WRBUSY) != 0) {
        if (--timeout == 0)
            goto done;
    }
    SYS_PLCTL = (SYS_PLCTL & ~SYS_PLCTL_PLSEL_Msk) | SYS_PLCTL_PLSEL_PL0;
    timeout = CLOCK_TIMEOUT;
    while ((SYS_PLSTS & SYS_PLSTS_PLCBUSY) != 0) {
        if (--timeout == 0)
            goto done;
    }

    /* Flash wait states: the reset value suits 12 MHz and fetching at
     * 96 MHz with it reads garbage. 4 cycles covers 75 MHz and above. */
    FMC_CYCCTL = (FMC_CYCCTL & ~FMC_CYCCTL_CYCLE_Msk) | FMC_CYCCTL_CYCLE_96MHZ;

    /* HCLK = PLL, no divide */
    CLK_CLKDIV0 &= ~CLK_CLKDIV0_HCLKDIV_Msk;
    CLK_CLKSEL0 = (CLK_CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) |
                  CLK_CLKSEL0_HCLKSEL_PLL;

done:
    SYS_LOCK();
}

/* --- UART -----------------------------------------------------------------
 * The NuMaker-M2354 routes the Nu-Link2-Me virtual COM port to PA6 (RXD) and
 * PA7 (TXD). UART0 has ten other possible pin pairs and the wrong one fails
 * silently: the transmitter reports empty either way. */
#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

void uart_init(void)
{
#ifndef NONSECURE_APP
    /* Pin mux lives behind the secure SYS registers; the secure world has
     * already done it before handing the port over. */
    SYS_UNLOCK();
    /* PA6 = UART0_RXD, PA7 = UART0_TXD */
    SYS_GPA_MFPL &= ~(SYS_GPA_MFPL_PA6MFP_Msk | SYS_GPA_MFPL_PA7MFP_Msk);
    SYS_GPA_MFPL |= (SYS_GPA_MFPL_PA6MFP_UART0_RXD |
                     SYS_GPA_MFPL_PA7MFP_UART0_TXD);
    SYS_LOCK();
#endif

    UART0_FUNCSEL = 0; /* plain UART mode */
    UART0_FIFO |= UART_FIFO_RXRST | UART_FIFO_TXRST;
    UART0_LINE = UART_LINE_WLS_8BIT | UART_LINE_NSB_1BIT | UART_LINE_PBE_NONE;
    /* UART0 runs from HIRC, not from HCLK */
    UART0_BAUD = UART_BAUD_BAUDM1 | UART_BAUD_BAUDM0 |
                 UART_BAUD_MODE2_DIVIDER(CLK_HIRC_FREQ, 115200UL);
}

void uart_write(const char *buf, unsigned int sz)
{
    while (sz-- > 0) {
        char c = *buf++;
        if (c == '\n') {
            while ((UART0_FIFOSTS & UART_FIFOSTS_TXFULL) != 0)
                ;
            UART0_DAT = '\r';
        }
        while ((UART0_FIFOSTS & UART_FIFOSTS_TXFULL) != 0)
            ;
        UART0_DAT = (uint32_t)(uint8_t)c;
    }
}
#endif /* DEBUG_UART || !__WOLFBOOT */

/* --- TrustZone ------------------------------------------------------------
 * wolfBoot owns the secure world; the application lives in APROM bank 1 at
 * the +NS_OFFSET alias. Note the polarity: secure is the base address here,
 * the opposite of the NXP ARMv8-M parts. The SAU and SCU are programmed
 * below; NSCBA is provisioned out of band and only read. */
#if defined(TZEN) && !defined(NONSECURE_APP)

/* NSCBA holds a plain size; derive it from the configured partition address
 * so the build and the hardware cannot drift apart. */
#define M2354_SECURE_FLASH_SIZE (WOLFBOOT_PARTITION_BOOT_ADDRESS - NS_OFFSET)

/* Read only: a bootloader that moves its own secure boundary at runtime can
 * brick the part. A mismatch means the SAU regions and linker script describe
 * a layout the hardware does not have, which is not recoverable here. */
static void hal_tz_check_nscba(void)
{
    if (SCU_FNSADDR != M2354_SECURE_FLASH_SIZE) {
        wolfBoot_panic();
    }
}

static void hal_scu_init(void)
{
    unsigned int i;

    /* One bit per 16 KB block, set to hand it to the non-secure world. */
    for (i = SRAM_SECURE_SIZE / SCU_SRAM_BLOCK_SIZE; i < SCU_SRAM_BLOCKS; i++) {
        SCU_SRAMNSSET |= (1UL << i);
    }

    /* Peripherals stay secure (the reset state). UART0 is handed over in
     * hal_prepare_boot so wolfBoot keeps its console until then. */
}

static void hal_sau_init(void)
{
    /* Secure gateway veneers */
    sau_init_region(0, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1, 1);

    /* Non-secure flash: boot, update and swap, in the non-secure alias */
    sau_init_region(1, WOLFBOOT_PARTITION_BOOT_ADDRESS,
            FLASH_APROM_END + NS_OFFSET - 1, 0);

    /* Non-secure SRAM */
    sau_init_region(2, SRAM_NS_BASE, SRAM_NS_END, 0);

    /* Non-secure peripheral alias */
    sau_init_region(3, PERIPH_NS_BASE, PERIPH_NS_END, 0);

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* No SCB_SHCSR_SECUREFAULT_EN: ARMv8-M baseline has no SecureFault, so
     * secure faults escalate to HardFault and slot 7 is reserved. */
}

/* Hand UART0 and its pins over, deferred to the boot handoff. */
static void periph_unsecure(void)
{
    SCU_PNSSET(SCU_UART0_ATTR / 32) |= (1UL << (SCU_UART0_ATTR & 31));
    SCU_IONSSET(SCU_PORTA_INDEX) |= (1UL << 6) | (1UL << 7);
    /* Match interrupt attribution to the peripheral. */
    NVIC_ITNS(UART0_IRQn / 32) |= (1UL << (UART0_IRQn & 31));
}

#endif /* TZEN && !NONSECURE_APP */

/* --- Init and boot handoff ------------------------------------------------ */

void hal_init(void)
{
#ifndef NONSECURE_APP
    /* The clock tree is behind the secure SYS registers; the secure world
     * has already brought it up. */
    clock_init();
#endif

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
#endif

#if defined(TZEN) && !defined(NONSECURE_APP)
    /* Check the hardware split before programming anything that uses it. */
    hal_tz_check_nscba();
    hal_scu_init();
    hal_sau_init();
#endif
}

void hal_prepare_boot(void)
{
#if defined(DEBUG_UART) && defined(__WOLFBOOT)
    /* Let the last byte shift out before the application takes the pins. */
    while ((UART0_FIFOSTS & UART_FIFOSTS_TXEMPTYF) == 0)
        ;
#endif
    hal_flash_lock();
#if defined(TZEN) && !defined(NONSECURE_APP)
    periph_unsecure();
#endif
}

#endif /* !WOLFBOOT_UNIT_TEST_FLASH */
