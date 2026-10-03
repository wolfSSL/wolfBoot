/* stm32h7s.c
 *
 * HAL for STM32H7S / STM32H7R (STM32H7RS series).
 *
 * wolfBoot runs from the 64 KB internal flash at 0x08000000 and keeps the
 * boot, update and swap partitions on an external Octo-SPI NOR reached
 * through XSPI2. This mirrors ST's own two-stage reference layout for the
 * part (Templates_Board: a Boot project in internal flash that hands off
 * to an Appli project executing in place from the XSPI window).
 *
 * The memory-mapped window is kept live from octospi_init() onwards.
 * wolfBoot core dereferences the boot partition header directly in
 * wolfBoot_start(), and for an external boot partition that is the raw
 * device address, so the window has to be mapped during wolfBoot's own
 * run as well as for the application.
 *
 * An indirect command has to drop the window, so each ext_flash_*
 * operation records whether it was mapped on entry and restores it before
 * returning. ext_flash_read() still issues a real SPI read command rather
 * than reading through the window, so wolfBoot's partition reads do not
 * depend on the mapping even though it happens to be enabled.
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

#ifndef WOLFBOOT_UNIT_TEST_FLASH_ERASE
#include "image.h"
#include "hal.h"
#include "hal/stm32h7s.h"
#include "printf.h"
#endif

#ifndef WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* SPI mode values for the XSPI CCR mode fields */
#define SPI_MODE_NONE       0
#define SPI_MODE_SINGLE     1

/* The same HAL is linked into the test-app, which really does execute in
 * place from the NOR. There the flash routines must run from SRAM. */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

/* ------------------------------------------------------------------ */
/* Internal flash                                                      */
/* ------------------------------------------------------------------ */

static RAMFUNCTION void flash_wait_complete(void)
{
    /* QW is set while the 16-byte write buffer holds pending data or a
     * program is in flight; BSY covers erase and option changes. */
    while ((FLASH_SR & (FLASH_SR_BSY | FLASH_SR_QW)) != 0)
        ;
}

static void RAMFUNCTION flash_clear_errors(void)
{
    /* FLASH_ISR flags are cleared by writing a 1 to the matching bit of
     * FLASH_ICR, so assign the mask rather than read-modify-write. */
    FLASH_ICR = (FLASH_ISR_EOPF | FLASH_ISR_OBLERRF | FLASH_ISR_RDSERRF |
                 FLASH_ISR_ERRORS);
}

/* A program operation fills a 128-bit write buffer; the device starts the
 * program once all four words have been written. Writes that do not cover
 * a whole quad-word are completed by reading the rest back from flash, so
 * the buffer is always full and CR.FW is never needed.
 *
 * ECC is computed per quad-word, so a quad-word can only be programmed
 * once after erase. Re-writing bytes of an already-programmed quad-word
 * raises PGSERR; a layout that needs repeated in-place flag updates in
 * internal flash must use NVM_FLASH_WRITEONCE. */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    int i = 0;
    int ii;
    uint32_t *src, *dst;

    flash_clear_errors();
    FLASH_CR |= FLASH_CR_PG;

    while (i < len) {
        flash_clear_errors();
        if ((len - i >= STM32H7S_WORD_SIZE) &&
                ((((address + i) & 0x0F) == 0) &&
                 ((((uint32_t)data) + i) & 0x0F) == 0)) {
            src = (uint32_t *)(data + i);
            dst = (uint32_t *)(address + i);
            flash_wait_complete();
            for (ii = 0; ii < STM32H7S_WORD_SIZE / 4; ii++)
                dst[ii] = src[ii];
            flash_wait_complete();
            i += STM32H7S_WORD_SIZE;
        }
        else {
            uint32_t val[STM32H7S_WORD_SIZE / 4];
            uint8_t *vbytes = (uint8_t *)(val);
            uint32_t base_addr = (address + i) & ~0x0Fu;
            int off = (int)((address + i) & 0x0F);
            dst = (uint32_t *)(base_addr);
            for (ii = 0; ii < STM32H7S_WORD_SIZE / 4; ii++)
                val[ii] = dst[ii];
            while ((off < STM32H7S_WORD_SIZE) && (i < len))
                vbytes[off++] = data[i++];
            flash_wait_complete();
            for (ii = 0; ii < STM32H7S_WORD_SIZE / 4; ii++)
                dst[ii] = val[ii];
            flash_wait_complete();
        }
        /* Checked per quad-word, not once at the end: flash_clear_errors()
         * runs on every iteration, so a later pass would wipe the flag. A
         * write-protect violation, or re-programming an already-written
         * quad-word on this ECC-per-word part, aborts the program in
         * hardware and must not be reported as success. */
        if ((FLASH_ISR & FLASH_ISR_ERRORS) != 0) {
            FLASH_CR &= ~FLASH_CR_PG;
            return -1;
        }
    }
    FLASH_ICR = FLASH_ISR_EOPF;
    FLASH_CR &= ~FLASH_CR_PG;
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
    FLASH_CR |= FLASH_CR_LOCK;
    hal_cache_invalidate();
}

#endif /* !WOLFBOOT_UNIT_TEST_FLASH_ERASE */

/* Erase is sector granular: eight 8 KB sectors in a single bank. The
 * sector number goes into CR.SSN and the request is armed by setting SER
 * and START in the same write. */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t end_address;
    uint32_t start;
    uint32_t p;

    if (len <= 0)
        return -1;
    /* Reject out-of-range requests before the offset arithmetic. A start
     * below the flash base wraps, and the wrapped end can compare lower
     * than the start, which would skip the loop and report success
     * without erasing anything. */
    if (address < FLASHMEM_ADDRESS_SPACE)
        return -1;
    start = address - FLASHMEM_ADDRESS_SPACE;
    if (start >= STM32H7S_FLASH_SIZE)
        return -1;
    if ((uint32_t)len > STM32H7S_FLASH_SIZE)
        return -1;

    end_address = start + (uint32_t)len - 1;
    /* WOLFBOOT_SECTOR_SIZE may be smaller than an internal sector -- this
     * target sets it to the NOR subsector size -- so an address that is
     * correctly aligned for the caller can still sit inside a sector.
     * Round down so the sector holding it is erased, and keep the end
     * address so every sector the range touches is covered. */
    for (p = start & ~((uint32_t)STM32H7S_SECTOR_SIZE - 1);
            p <= end_address; p += STM32H7S_SECTOR_SIZE) {
        uint32_t sector = p / STM32H7S_SECTOR_SIZE;
        uint32_t reg;

        if (sector >= STM32H7S_SECTOR_COUNT) {
            /* Leave the control register disarmed on this path too, the
             * same as the normal exit below. */
            FLASH_CR &= ~((uint32_t)FLASH_CR_SER);
            return -1;
        }
        flash_clear_errors();
        reg = FLASH_CR & ~(((uint32_t)FLASH_CR_SSN_MASK) <<
                FLASH_CR_SSN_SHIFT);
        reg &= ~((uint32_t)(FLASH_CR_BER | FLASH_CR_ALL_BANKS));
        FLASH_CR = reg | (sector << FLASH_CR_SSN_SHIFT) | FLASH_CR_SER |
                   FLASH_CR_START;
        DMB();
        flash_wait_complete();
        /* BSY and QW clearing only means the operation finished, not that
         * it worked: a write-protected sector completes with WRPERRF set
         * and still erased to nothing. */
        if ((FLASH_ISR & FLASH_ISR_ERRORS) != 0) {
            FLASH_CR &= ~((uint32_t)FLASH_CR_SER);
            return -1;
        }
    }
    FLASH_CR &= ~((uint32_t)FLASH_CR_SER);
    return 0;
}

#ifndef WOLFBOOT_UNIT_TEST_FLASH_ERASE

/* ------------------------------------------------------------------ */
/* Cortex-M7 cache maintenance                                         */
/* ------------------------------------------------------------------ */

static void RAMFUNCTION dcache_clean_invalidate_by_addr(uint32_t addr,
    uint32_t size)
{
    uint32_t line;

    for (line = addr & ~((uint32_t)CACHE_LINE_SIZE - 1);
            line < addr + size; line += CACHE_LINE_SIZE) {
        SCB_DCCIMVAC = line;
    }
    DSB();
    ISB();
}

/* Clean and invalidate the whole D-cache by set/way. Cost is bounded by
 * the cache geometry, where a loop over the 256 MB memory-mapped window
 * by cache line would be millions of register writes. */
static void RAMFUNCTION dcache_clean_invalidate_all(void)
{
    uint32_t sets, ways;

    if ((SCB_CCR & SCB_CCR_DC) == 0)
        return;
    CSSELR = 0;
    DSB();
    sets = (CCSIDR >> 13) & 0x7FFF;
    do {
        ways = (CCSIDR >> 3) & 0x3FF;
        do {
            SCB_DCCISW = ((sets & 0x1FF) << 5) | ((ways & 0x3) << 30);
        } while (ways-- != 0);
    } while (sets-- != 0);
    DSB();
    ISB();
}

void RAMFUNCTION hal_cache_invalidate(void)
{
    /* Anything wolfBoot programmed into the NOR may still be cached from
     * the memory-mapped window, and the window is too large to walk by
     * line, so flush the whole cache instead. */
    dcache_clean_invalidate_all();
    SCB_ICIALLU = 0;
    DSB();
    ISB();
}

static void dcache_enable(void)
{
    uint32_t sets, ways;

    if ((SCB_CCR & SCB_CCR_DC) != 0)
        return;
    /* Invalidate the whole D-cache by set/way before enabling it. */
    CSSELR = 0;
    DSB();
    sets = (CCSIDR >> 13) & 0x7FFF;
    do {
        ways = (CCSIDR >> 3) & 0x3FF;
        do {
            SCB_DCISW = ((sets & 0x1FF) << 5) | ((ways & 0x3) << 30);
        } while (ways-- != 0);
    } while (sets-- != 0);
    DSB();
    SCB_CCR |= SCB_CCR_DC;
    DSB();
    ISB();
}

static void icache_enable(void)
{
    if ((SCB_CCR & SCB_CCR_IC) != 0)
        return;
    SCB_ICIALLU = 0;
    DSB();
    ISB();
    SCB_CCR |= SCB_CCR_IC;
    DSB();
    ISB();
}

static void RAMFUNCTION dcache_disable(void)
{
    if ((SCB_CCR & SCB_CCR_DC) == 0)
        return;
    /* Clean before clearing DC so dirty lines are written back rather
     * than discarded, which is the documented Cortex-M7 sequence. */
    dcache_clean_invalidate_all();
    DSB();
    SCB_CCR &= ~((uint32_t)SCB_CCR_DC);
    DSB();
    ISB();
}

static void RAMFUNCTION icache_disable(void)
{
    DSB();
    ISB();
    SCB_CCR &= ~((uint32_t)SCB_CCR_IC);
    SCB_ICIALLU = 0;
    DSB();
    ISB();
}

/* ------------------------------------------------------------------ */
/* XSPI2: external Octo-SPI NOR                                        */
/* ------------------------------------------------------------------ */

static int RAMFUNCTION octospi_mmap_active(void)
{
    return ((OCTOSPI_CR & OCTOSPI_CR_FMODE_MASK) == OCTOSPI_CR_FMODE_MMAP);
}

/* Indirect-mode command helper. Memory-mapped mode has to be aborted
 * before an indirect command, and is left off on return: callers that
 * were using the window restore it once their whole operation is done,
 * rather than once per command. */
static int RAMFUNCTION octospi_cmd(uint8_t fmode, uint8_t cmd,
    uint32_t addr, uint32_t addrMode,
    uint8_t *data, uint32_t dataSz, uint32_t dataMode,
    uint32_t dummyCycles)
{
    uint32_t ccr;

    if (octospi_mmap_active()) {
        OCTOSPI_CR |= OCTOSPI_CR_ABORT;
        while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
            ;
    }
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF | OCTOSPI_FCR_CTEF | OCTOSPI_FCR_CSMF;

    OCTOSPI_CR = (OCTOSPI_CR & ~OCTOSPI_CR_FMODE_MASK) |
                 OCTOSPI_CR_FMODE(fmode);

    if (dataSz > 0)
        OCTOSPI_DLR = dataSz - 1;

    ccr = OCTOSPI_CCR_IMODE(SPI_MODE_SINGLE) | OCTOSPI_CCR_ISIZE(0);
    if (addrMode != SPI_MODE_NONE)
        ccr |= OCTOSPI_CCR_ADMODE(addrMode) | OCTOSPI_CCR_ADSIZE(3);
    if (dataMode != SPI_MODE_NONE)
        ccr |= OCTOSPI_CCR_DMODE(dataMode);
    OCTOSPI_CCR = ccr;
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(dummyCycles);
    OCTOSPI_IR = cmd;

    if (addrMode != SPI_MODE_NONE)
        OCTOSPI_AR = addr;

    /* Byte-wide DR access driven by FLEVEL rather than FTF: a threshold
     * flag can stop being reasserted for the trailing bytes once the FIFO
     * has drained below the threshold, which stalls transfers larger than
     * the 32-byte FIFO. FLEVEL is correct for any size. */
    if (dataSz > 0 && data != NULL) {
        uint32_t sr;
        while (dataSz > 0) {
            sr = OCTOSPI_SR;
            if (sr & OCTOSPI_SR_TEF)
                goto octospi_err;
            if (fmode == 0) {
                if (OCTOSPI_SR_FLEVEL(sr) < 32) {
                    OCTOSPI_DR = *data;
                    data++;
                    dataSz--;
                }
            }
            else {
                if (OCTOSPI_SR_FLEVEL(sr) > 0) {
                    *data = OCTOSPI_DR;
                    data++;
                    dataSz--;
                }
                else if (sr & OCTOSPI_SR_TCF) {
                    /* Transfer complete with the FIFO empty and bytes
                     * still outstanding means DLR disagreed with dataSz. */
                    goto octospi_err;
                }
            }
        }
    }

    while (!(OCTOSPI_SR & (OCTOSPI_SR_TCF | OCTOSPI_SR_TEF)))
        ;
    if (OCTOSPI_SR & OCTOSPI_SR_TEF)
        goto octospi_err;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF;
    return 0;

octospi_err:
    OCTOSPI_FCR = OCTOSPI_FCR_CTEF;
    OCTOSPI_CR |= OCTOSPI_CR_ABORT;
    while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
        ;
    return -1;
}

static int RAMFUNCTION octospi_read_status(uint8_t *sr)
{
    *sr = 0;
    /* A failed status read must not look like "ready": sr would stay zero
     * and a caller testing WIP would treat the device as idle. */
    return octospi_cmd(1, READ_SR_CMD, 0, SPI_MODE_NONE,
                       sr, 1, SPI_MODE_SINGLE, 0);
}

static int RAMFUNCTION octospi_write_enable(void)
{
    uint8_t sr;
    uint32_t tries;

    if (octospi_cmd(0, WRITE_ENABLE_CMD, 0, SPI_MODE_NONE,
                    NULL, 0, SPI_MODE_NONE, 0) < 0)
        return -1;

    /* The command completing only says it reached the device, not that the
     * latch was set: a busy device discards it. Without this check the
     * following program or erase is silently dropped, the device never
     * goes busy, and octospi_wait_ready() reports success on flash that
     * was never modified. */
    for (tries = 0; tries < OCTOSPI_READY_TRIES; tries++) {
        if (octospi_read_status(&sr) < 0)
            return -1;
        if ((sr & NOR_SR_WEL) != 0 && (sr & NOR_SR_WIP) == 0)
            return 0;
    }
    return -1;
}

static int RAMFUNCTION octospi_wait_ready(void)
{
    uint8_t sr;
    uint32_t tries;

    for (tries = 0; tries < OCTOSPI_READY_TRIES; tries++) {
        if (octospi_read_status(&sr) < 0)
            return -1;
        if ((sr & NOR_SR_WIP) == 0)
            return 0;
    }
    return -1;
}

static void RAMFUNCTION octospi_enable_mmap(void)
{
    if (octospi_mmap_active()) {
        OCTOSPI_CR |= OCTOSPI_CR_ABORT;
        while (OCTOSPI_CR & OCTOSPI_CR_ABORT)
            ;
    }
    while (OCTOSPI_SR & OCTOSPI_SR_BUSY)
        ;
    OCTOSPI_FCR = OCTOSPI_FCR_CTCF | OCTOSPI_FCR_CTEF | OCTOSPI_FCR_CSMF;

    OCTOSPI_CR = (OCTOSPI_CR & ~OCTOSPI_CR_FMODE_MASK) |
                 OCTOSPI_CR_FMODE_MMAP;
    OCTOSPI_CCR = OCTOSPI_CCR_IMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ISIZE(0) |
                  OCTOSPI_CCR_ADMODE(SPI_MODE_SINGLE) |
                  OCTOSPI_CCR_ADSIZE(3) |
                  OCTOSPI_CCR_DMODE(SPI_MODE_SINGLE);
    OCTOSPI_TCR = OCTOSPI_TCR_DCYC(OCTOSPI_READ_DUMMY);
    OCTOSPI_IR = FAST_READ_4B_CMD;
    DSB();
    ISB();
}

static void octospi_gpio_init(void)
{
    uint32_t pin;

    RCC_AHB4ENR |= OCTOSPI_GPIO_RCC_EN;
    DMB();
    for (pin = 0; pin < 16; pin++) {
        if ((OCTOSPI_GPIO_PIN_MASK & (1U << pin)) == 0)
            continue;
        GPIO_MODE(OCTOSPI_GPIO_BASE) &= ~(0x3U << (pin * 2));
        GPIO_MODE(OCTOSPI_GPIO_BASE) |= (GPIO_MODE_AF << (pin * 2));
        GPIO_OTYPE(OCTOSPI_GPIO_BASE) &= ~(1U << pin);
        GPIO_PUPD(OCTOSPI_GPIO_BASE) &= ~(0x3U << (pin * 2));
        GPIO_OSPD(OCTOSPI_GPIO_BASE) &= ~(0x3U << (pin * 2));
        GPIO_OSPD(OCTOSPI_GPIO_BASE) |=
            (GPIO_SPEED_VERY_HIGH << (pin * 2));
        if (pin < 8) {
            GPIO_AFL(OCTOSPI_GPIO_BASE) &= ~(0xFU << (pin * 4));
            GPIO_AFL(OCTOSPI_GPIO_BASE) |=
                (OCTOSPI_GPIO_AF << (pin * 4));
        }
        else {
            GPIO_AFH(OCTOSPI_GPIO_BASE) &= ~(0xFU << ((pin - 8) * 4));
            GPIO_AFH(OCTOSPI_GPIO_BASE) |=
                (OCTOSPI_GPIO_AF << ((pin - 8) * 4));
        }
    }
}

static void octospi_init(void)
{
    RCC_AHB5ENR |= RCC_AHB5ENR_XSPI2EN | RCC_AHB5ENR_XSPIMEN;
    DMB();

    /* XSPIM_CR is only writable while both XSPI instances are disabled.
     * Clearing it selects direct routing, XSPI2 to port 2, which is how
     * the NOR is wired on the reference board. */
    XSPI1_CR &= ~((uint32_t)XSPI_CR_EN);
    OCTOSPI_CR &= ~((uint32_t)OCTOSPI_CR_EN);
    DMB();
    RCC_AHB5RSTR |= RCC_AHB5RSTR_XSPI2RST;
    DMB();
    RCC_AHB5RSTR &= ~((uint32_t)RCC_AHB5RSTR_XSPI2RST);
    DMB();
    XSPIM_CR = 0;
    DMB();

    OCTOSPI_DCR1 = OCTOSPI_DCR1_DEVSIZE(FLASH_DEVICE_SIZE_LOG2 - 1) |
                   OCTOSPI_DCR1_CSHT(3) | OCTOSPI_DCR1_CKMODE_0;
    OCTOSPI_DCR2 = OCTOSPI_DCR2_PRESCALER(OCTOSPI_PRESCALER);
    OCTOSPI_CR = OCTOSPI_CR_FTHRES(4) | OCTOSPI_CR_EN;
    DSB();

    /* Leave the window mapped. wolfBoot_start() in src/update_flash.c
     * dereferences boot.hdr directly -- for an external boot partition
     * that is the raw device address -- so the mapping has to be live for
     * wolfBoot's own run, not just for the application. */
    octospi_enable_mmap();
}

static uint32_t RAMFUNCTION ext_flash_addr(uintptr_t address)
{
    /* Accept either an absolute address inside the memory-mapped window
     * or an offset already relative to the device, so the boot partition
     * can be configured at its execute-in-place address while update and
     * swap stay device relative. */
    /* Bounded by the populated device rather than the whole 256 MB
     * aperture, so a device-relative offset can never be mistaken for an
     * address inside the window. */
    if (address >= OCTOSPI_MEM_BASE &&
            address < (OCTOSPI_MEM_BASE + FLASH_DEVICE_SIZE))
        return (uint32_t)(address - OCTOSPI_MEM_BASE);
    return (uint32_t)address;
}

/* Page program wraps inside its own 256-byte page, so each command has to
 * stop at the next page boundary, and the source is staged through RAM so
 * the page-program command reads from a stable buffer.
 *
 * The source must already be in RAM. Only the first page is copied while
 * the memory-mapped window is still up, so a source pointer into that
 * window would read back garbage from the second page onwards. Every
 * wolfBoot caller passes a RAM buffer. */
/* Staging copy done here rather than with memcpy(). Everything this
 * function calls has to be resident while the memory-mapped window is
 * down, and where libc or wolfBoot's own string.c ends up linked is not
 * something this HAL should depend on. The volatile destination stops the
 * compiler turning the loop back into a memcpy() call. */
static void RAMFUNCTION ram_copy(volatile uint8_t *dst, const uint8_t *src,
    uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        dst[i] = src[i];
}

static int RAMFUNCTION nor_flash_write(uint32_t offset, const uint8_t *data,
    int len)
{
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t page_off, write_sz;
    int remaining = len;
    int ret = 0;
    int was_mmap;

    if (len <= 0)
        return 0;

    was_mmap = octospi_mmap_active();

    while (remaining > 0) {
        page_off = offset & (FLASH_PAGE_SIZE - 1);
        write_sz = FLASH_PAGE_SIZE - page_off;
        if ((int)write_sz > remaining)
            write_sz = (uint32_t)remaining;

        ram_copy(page_buf, data, write_sz);

        if (octospi_write_enable() < 0) {
            ret = -1;
            break;
        }
        ret = octospi_cmd(0, PAGE_PROG_4B_CMD, offset, SPI_MODE_SINGLE,
                page_buf, write_sz, SPI_MODE_SINGLE, 0);
        if (ret < 0)
            break;
        if (octospi_wait_ready() < 0) {
            ret = -1;
            break;
        }
        offset += write_sz;
        data += write_sz;
        remaining -= (int)write_sz;
    }
    /* A caller executing in place from this device needs the window back
     * before control returns to code that is not in RAM. */
    if (was_mmap)
        octospi_enable_mmap();
    return ret;
}

static int RAMFUNCTION nor_flash_erase(uint32_t offset, int len)
{
    uint32_t end;
    uint32_t addr;
    int ret = 0;
    int was_mmap;

    if (len <= 0)
        return -1;

    was_mmap = octospi_mmap_active();
    end = offset + (uint32_t)len;
    for (addr = offset; addr < end; addr += FLASH_SECTOR_SIZE) {
        if (octospi_write_enable() < 0) {
            ret = -1;
            break;
        }
        ret = octospi_cmd(0, SEC_ERASE_4B_CMD, addr, SPI_MODE_SINGLE,
                NULL, 0, SPI_MODE_NONE, 0);
        if (ret < 0)
            break;
        if (octospi_wait_ready() < 0) {
            ret = -1;
            break;
        }
    }
    if (was_mmap)
        octospi_enable_mmap();
    return ret;
}

int RAMFUNCTION ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    uint32_t off = ext_flash_addr(address);
    int was_mmap;
    int ret;

    if (len <= 0)
        return 0;

    was_mmap = octospi_mmap_active();
    /* Read as an SPI command rather than through the memory-mapped
     * window: no wolfBoot read path then depends on mmap being live, so
     * an update that erases and programs this same device cannot pull the
     * window out from under a read in progress. */
    ret = octospi_cmd(1, FAST_READ_4B_CMD, off, SPI_MODE_SINGLE,
            data, (uint32_t)len, SPI_MODE_SINGLE, OCTOSPI_READ_DUMMY);
    if (was_mmap)
        octospi_enable_mmap();
    if (ret < 0)
        return -1;
    return len;
}

int RAMFUNCTION ext_flash_write(uintptr_t address, const uint8_t *data,
    int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_write(off, data, len);

    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(
            (uint32_t)(OCTOSPI_MEM_BASE + off), (uint32_t)len);
    return ret;
}

int RAMFUNCTION ext_flash_erase(uintptr_t address, int len)
{
    uint32_t off = ext_flash_addr(address);
    int ret = nor_flash_erase(off, len);

    if (ret == 0 && len > 0)
        dcache_clean_invalidate_by_addr(
            (uint32_t)(OCTOSPI_MEM_BASE + off), (uint32_t)len);
    return ret;
}

void RAMFUNCTION ext_flash_lock(void)
{
}

void RAMFUNCTION ext_flash_unlock(void)
{
}

/* ------------------------------------------------------------------ */
/* Clocks and console                                                  */
/* ------------------------------------------------------------------ */

/* Bring the core up to its rated speed. Reset leaves the part on HSI at
 * 64 MHz, which would make verifying a multi-megabyte image needlessly
 * slow, so PLL1 is taken to 600 MHz using ST's reference dividers for
 * this device. See the clock profile comment in hal/stm32h7s.h.
 *
 * The order matters: raise the regulator and the flash latency before the
 * frequency, and set the bus prescalers before switching the system clock
 * over, so no domain is ever momentarily out of spec. */
/* Select the core supply and switch on the XSPI I/O supply.
 *
 * Both are the software's job after a power-on reset; nothing runs before
 * wolfBoot on this part. The low five bits of PWR_CSR2 are write-once and
 * come out of reset with SDEN and LDOEN both set, which is the "not yet
 * selected" state rather than a working configuration. Until a selection
 * is committed and PWR_SR1.ACTVOSRDY rises, a write to PWR_CSR4.VOS does
 * not take and VOSRDY never rises either.
 *
 * Only a true power-on reset returns PWR to that state. A software reset,
 * NRST, or a debugger attach leaves an earlier configuration in place,
 * and ST's external memory loader configures it as a side effect, so the
 * omission is invisible in all of those cases.
 *
 * Returns 0 once the supply is usable, -1 on timeout. */
static int pwr_supply_config(void)
{
    uint32_t tries;

    if ((PWR_CSR2 & PWR_CSR2_SUPPLY_MASK) == PWR_CSR2_SUPPLY_UNSET) {
        PWR_CSR2 = (PWR_CSR2 & ~((uint32_t)PWR_CSR2_SUPPLY_MASK)) |
                   PWR_SUPPLY_CONFIG;
        DMB();
    }
    /* Already-committed settings cannot be changed, so a configuration
     * left by an earlier stage is used as it stands. */
    for (tries = 0; tries < PWR_READY_TRIES; tries++) {
        if ((PWR_SR1 & PWR_SR1_ACTVOSRDY) != 0)
            break;
    }
    if (tries >= PWR_READY_TRIES)
        return -1;

    /* The XSPI I/O supply is off at reset and must not be switched on
     * until the core supply is stable, so this follows ACTVOSRDY. Without
     * it the NOR cannot be read and opening the boot partition fails. */
    PWR_CSR2 |= PWR_CSR2_EN_XSPIM;
    DMB();
    return 0;
}

static void clock_config(void)
{
    /* The device boots on HSI; make sure it is actually stable. */
    RCC_CR |= RCC_CR_HSION;
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
        ;

    /* Needed for the NOR in every configuration, so it is done before the
     * clock options below and outside them. */
    if (pwr_supply_config() < 0)
        return; /* stay on HSI rather than hang waiting on the regulator */

#if (STM32H7S_HSI_ONLY == 0)
    {
    uint32_t tries;

    /* Voltage scaling range 0 is required for the top frequency. */
    PWR_CSR4 |= PWR_CSR4_VOS;
    for (tries = 0; tries < PWR_READY_TRIES; tries++) {
        if ((PWR_CSR4 & PWR_CSR4_VOSRDY) != 0)
            break;
    }
    if (tries >= PWR_READY_TRIES)
        return; /* leave the core on HSI */
    }

    /* More wait states before going faster, never after. The reference
     * manual requires reading the register back to confirm the new
     * latency has been taken into account before the clock is raised. */
    FLASH_ACR = FLASH_LATENCY_HIGH;
    while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) !=
            FLASH_ACR_LATENCY(FLASH_LATENCY_WS))
        ;
    DSB();

    /* Park both PLLs while they are reprogrammed. Safe because the core
     * is running from HSI at this point, not from a PLL. */
    RCC_CR &= ~((uint32_t)(RCC_CR_PLL1ON | RCC_CR_PLL2ON));
    while ((RCC_CR & (RCC_CR_PLL1RDY | RCC_CR_PLL2RDY)) != 0)
        ;

    /* HSI into both PLLs, with their reference pre-dividers. */
    RCC_PLLCKSELR = RCC_PLLCKSELR_PLLSRC_HSI |
                    RCC_PLLCKSELR_DIVM1(PLL1_M) |
                    RCC_PLLCKSELR_DIVM2(PLL2_M);

    /* PLL1 reference is 2 MHz and PLL2's is 16 MHz, so they sit in
     * different input ranges. Both VCOs land in the wide range, which is
     * VCOSEL clear. Fractional mode stays off. */
    RCC_PLLCFGR = RCC_PLLCFGR_PLL1RGE(RCC_PLL_RGE_2_4MHZ) |
                  RCC_PLLCFGR_PLL2RGE(RCC_PLL_RGE_8_16MHZ);

    RCC_PLL1DIVR1 = RCC_PLLDIVR1_DIVN(PLL1_N) | RCC_PLLDIVR1_DIVP(PLL1_P) |
                    RCC_PLLDIVR1_DIVQ(PLL1_Q) | RCC_PLLDIVR1_DIVR(PLL1_R);
    RCC_PLL1DIVR2 = RCC_PLLDIVR2_DIVS(PLL1_S) | RCC_PLLDIVR2_DIVT(PLL1_T);

    RCC_PLL2DIVR1 = RCC_PLLDIVR1_DIVN(PLL2_N) | RCC_PLLDIVR1_DIVP(PLL2_P) |
                    RCC_PLLDIVR1_DIVQ(PLL2_Q) | RCC_PLLDIVR1_DIVR(PLL2_R);
    RCC_PLL2DIVR2 = RCC_PLLDIVR2_DIVS(PLL2_S) | RCC_PLLDIVR2_DIVT(PLL2_T);

    /* PLL1 P feeds the system clock; PLL2 S feeds the XSPI2 kernel. */
    RCC_PLLCFGR |= RCC_PLLCFGR_PLL1PEN | RCC_PLLCFGR_PLL2SEN;
    DSB();

    RCC_CR |= RCC_CR_PLL1ON | RCC_CR_PLL2ON;
    while ((RCC_CR & (RCC_CR_PLL1RDY | RCC_CR_PLL2RDY)) !=
            (RCC_CR_PLL1RDY | RCC_CR_PLL2RDY))
        ;

    /* Retarget the XSPI2 kernel clock to PLL2 S before the system clock
     * moves. Its reset source is HCLK, so doing this afterwards would
     * leave the kernel briefly derived from a much faster bus clock.
     * octospi_init() derives its prescaler from the result. */
    RCC_CCIPR1 = (RCC_CCIPR1 & ~((uint32_t)RCC_CCIPR1_XSPI2SEL_MASK)) |
                 RCC_CCIPR1_XSPI2SEL_PLL2S;
    DSB();

    /* Divide the buses down before the switch, so HCLK and the APBs are
     * already in range the instant the system clock becomes 600 MHz. */
    RCC_CDCFGR = RCC_CDCFGR_CPRE(RCC_PRE_DIV1);
    RCC_BMCFGR = RCC_BMCFGR_BMPRE(RCC_PRE_DIV2);
    RCC_APBCFGR = RCC_APBCFGR_PPRE1(RCC_PPRE_DIV2) |
                  RCC_APBCFGR_PPRE2(RCC_PPRE_DIV2) |
                  RCC_APBCFGR_PPRE4(RCC_PPRE_DIV2) |
                  RCC_APBCFGR_PPRE5(RCC_PPRE_DIV2);
    DSB();

    RCC_CFGR = (RCC_CFGR & ~((uint32_t)RCC_CFGR_SW_MASK)) | RCC_CFGR_SW_PLL1;
    while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL1)
        ;
#endif /* STM32H7S_HSI_ONLY == 0 */
}

/* uart_init() and uart_write() live in hal/uart/uart_drv_stm32h7s.c:
 * options.mk only defines DEBUG_UART when that source file exists, and
 * the test-app links the same object. */

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

void hal_init(void)
{
    clock_config();
    icache_enable();
    octospi_gpio_init();
    octospi_init();
    dcache_enable();
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL init\r\n", 19);
#endif
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void hal_prepare_boot(void)
{
    /* Hand the application a live memory-mapped window so it can execute
     * in place, then drop the caches so it starts from a known state. */
    octospi_enable_mmap();
    dcache_disable();
    icache_disable();
}

#endif /* !WOLFBOOT_UNIT_TEST_FLASH_ERASE */
