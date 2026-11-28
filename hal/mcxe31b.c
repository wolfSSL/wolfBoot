/* mcxe31b.c
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
 *
 * HAL for NXP MCXE31B (Cortex-M7, 4MB C40ASF code flash + 128KB data flash)
 * Tested on FRDM-MCXE31B.
 */

#include <stdint.h>
#include <string.h>
#include "image.h"
#include "hal.h"
#include "printf.h"

/* Override RAMFUNCTION for test-app: when RAM_CODE is set but not __WOLFBOOT,
 * we still need flash functions to run from RAM for self-programming. */
#if defined(RAM_CODE) && !defined(__WOLFBOOT)
    #undef RAMFUNCTION
    #define RAMFUNCTION __attribute__((used,section(".ramcode"),long_call))
#endif

#define DSB() __asm__ volatile ("dsb")
#define ISB() __asm__ volatile ("isb")

#define __get_PRIMASK() ({ \
    uint32_t primask; \
    __asm__ volatile ("mrs %0, primask" : "=r" (primask)); \
    primask; \
})

#define __set_PRIMASK(primask) \
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory")

#define __disable_irq() \
    __asm__ volatile ("cpsid i" ::: "memory")

#include "mcxe31b.h"

/* ============== Image Vector Table (IVT) ============== */
/* The Secure BAF boot ROM scans for this 256-byte structure at the start of
 * each code flash block (0x00400000 first) and starts CM7_0 at the vector
 * table it points to. */

#ifdef __WOLFBOOT

extern uint32_t _start_vector;

const uint32_t __attribute__((section(".ivt"))) mcxe31b_ivt[64] = {
    IVT_MARKER,                     /* 0x00: IVT marker */
    IVT_BCW_CM7_0_ENABLE,           /* 0x04: boot config: start CM7_0, no SWT0 */
    0,                              /* 0x08: reserved */
    (uint32_t)&_start_vector,       /* 0x0C: CM7_0 start address */
    0,                              /* 0x10: reserved */
    0,                              /* 0x14: CM7_1 start address (n/a) */
    0,                              /* 0x18: reserved */
    0,                              /* 0x1C: CM7_2 start address (n/a) */
    0,                              /* 0x20: reserved */
    0,                              /* 0x24: LC configuration word address */
    /* 0x28 - 0xFF: reserved (zero) */
};

#endif /* __WOLFBOOT */

/* ============== Clocks ============== */

#define CLOCK_TIMEOUT 1000000

/* Enable a partition 1 peripheral clock by its MC_ME clock request number */
static void mc_me_clock_enable(uint32_t req)
{
    uint32_t cofb = req / 32;
    uint32_t bit = 1UL << (req % 32);

    if (MC_ME_PRTN1_COFB_STAT(cofb) & bit)
        return;
    MC_ME_PRTN1_COFB_CLKEN(cofb) |= bit;
    MC_ME_PRTN1_PUPD = MC_ME_PRTN_PUPD_PCUD;
    MC_ME_CTL_KEY = MC_ME_CTL_KEY_KEY;
    MC_ME_CTL_KEY = MC_ME_CTL_KEY_INVKEY;
    while (!(MC_ME_PRTN1_COFB_STAT(cofb) & bit)) {}
}

/* Busy wait for at least n core cycles */
static void delay_cycles(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++) {}
}

/* Switch the system clocks from FIRC 48 MHz to the PLL using the RM
 * "Option A" (high performance) configuration (MCXEP172M160FB0RM Rev. 5,
 * Table 134): FXOSC 16 MHz -> PLL VCO 960 MHz -> ODIV2 480 MHz -> PHI0
 * 160 MHz. On failure the system stays on FIRC. */
static int clock_init(void)
{
    /* MUX_0 dividers (DIV + 1): CORE 160, AIPS_PLAT 80, AIPS_SLOW 40,
     * HSE 80, DCM 40, LBIST 40, QSPI_MEM 160 MHz */
    static const uint8_t mux0_div[MC_CGM_MUX0_NUM_DIV] = {
        0, 1, 3, 1, 3, 3, 0
    };
    uint32_t timeout;
    int i;

    /* Already running from the PLL, e.g. the test-app started by wolfBoot */
    if (MC_CGM_CSS_SELSTAT(MC_CGM_MUX0_CSS) == MC_CGM_SEL_PLL_PHI0)
        return 0;

    mc_me_clock_enable(MC_ME_REQ_FXOSC);
    mc_me_clock_enable(MC_ME_REQ_PLL);

    /* Last mile regulator: it supplies the core above FIRC speed. Start the
     * V15 ballast, wait for its soft start and for V15, then hand over. */
    if (!(PMC_CONFIG & PMC_CONFIG_LMEN)) {
        PMC_CONFIG |= PMC_CONFIG_LMBCTLEN;
        delay_cycles(FIRC_FREQ / 20000); /* 50 us */
        timeout = CLOCK_TIMEOUT;
        while ((PMC_LVSC & PMC_LVSC_LVD15S) && --timeout) {}
        if (timeout == 0)
            return -1;
        PMC_LVSC = PMC_LVSC_LVD15F;
        PMC_CONFIG |= PMC_CONFIG_LMEN;
        delay_cycles(FIRC_FREQ / 500000); /* 2 us before a clock change */
    }

    /* FXOSC: crystal mode with the reset end-of-count and gain values */
    if (!(FXOSC_STAT & FXOSC_STAT_OSC_STAT)) {
        FXOSC_CTRL = FXOSC_CTRL_COMP_EN | FXOSC_CTRL_EOCV(0x9D) |
                     FXOSC_CTRL_GM_SEL(0xC) | FXOSC_CTRL_OSCON;
        timeout = CLOCK_TIMEOUT;
        while (!(FXOSC_STAT & FXOSC_STAT_OSC_STAT) && --timeout) {}
        if (timeout == 0)
            return -1;
    }

    /* PLL: 16 MHz / 1 * 60 = 960 MHz VCO, / 2 = 480 MHz, PHI0 / 3 */
    PLL_PLLODIV(0) = 0;
    PLL_PLLODIV(1) = 0;
    PLL_PLLCR = PLL_PLLCR_PLLPD;
    PLL_PLLDV = PLL_PLLDV_RESERVED | PLL_PLLDV_ODIV2(2) | PLL_PLLDV_RDIV(1) |
                PLL_PLLDV_MFI(60);
    PLL_PLLFM = PLL_PLLFM_SSCGBYP;
    PLL_PLLFD = 0;
    PLL_PLLODIV(0) = PLL_PLLODIV_DIV(2);
    PLL_PLLCR = 0;
    timeout = CLOCK_TIMEOUT;
    while (!(PLL_PLLSR & PLL_PLLSR_LOCK) && --timeout) {}
    if (timeout == 0) {
        PLL_PLLCR = PLL_PLLCR_PLLPD;
        return -1;
    }
    PLL_PLLODIV(0) = PLL_PLLODIV_DE | PLL_PLLODIV_DIV(2);

    /* SRAM0 needs a read wait state above 120 MHz. Nothing runs from SRAM0
     * here, so it can be changed safely. */
    PRAMC0_PRCR1 |= PRAMC_PRCR1_FT_DIS;

    /* Program all MUX_0 dividers together, with the crossbar halt
     * handshake, then switch the mux to PHI0 */
    MC_CGM_MUX0_DIV_TRIG_CTRL = MC_CGM_DIV_TRIG_CTRL_TCTL |
                                MC_CGM_DIV_TRIG_CTRL_HHEN;
    for (i = 0; i < MC_CGM_MUX0_NUM_DIV; i++)
        MC_CGM_MUX0_DC(i) = MC_CGM_DC_DE | MC_CGM_DC_DIV(mux0_div[i]);
    MC_CGM_MUX0_DIV_TRIG = 0xFFFFFFFFUL;
    while (MC_CGM_MUX0_DIV_UPD_STAT & MC_CGM_DIV_UPD_STAT_DIV_STAT) {}

    MC_CGM_MUX0_CSC = MC_CGM_CSC_SELCTL(MC_CGM_SEL_PLL_PHI0) |
                      MC_CGM_CSC_CLK_SW;
    timeout = CLOCK_TIMEOUT;
    while (((MC_CGM_MUX0_CSS & MC_CGM_CSS_SWIP) ||
            MC_CGM_CSS_SELSTAT(MC_CGM_MUX0_CSS) != MC_CGM_SEL_PLL_PHI0) &&
            --timeout) {}
    if (timeout == 0)
        return -1;
    return 0;
}

/* ============== Caches ============== */

/* Code and data flash are in the Cortex-M7 "Code" region (normal memory,
 * write-through cacheable with the default memory map), so the D-cache can
 * hold flash contents that a program or erase has changed. The TCMs are not
 * cached. */
static void RAMFUNCTION dcache_invalidate_range(uint32_t addr, uint32_t len)
{
    uint32_t line;

    if (!(SCB_CCR & SCB_CCR_DC))
        return;
    DSB();
    for (line = addr & ~(CACHE_LINE_SIZE - 1); line < addr + len;
            line += CACHE_LINE_SIZE) {
        SCB_DCIMVAC = line;
    }
    DSB();
    ISB();
}

/* Operate on every D-cache line by set/way: DCISW invalidates, DCCISW cleans
 * and invalidates */
static void dcache_setway(volatile uint32_t *op)
{
    uint32_t sets, ways, ccsidr;

    SCB_CSSELR = 0; /* L1 data cache */
    DSB();
    ccsidr = SCB_CCSIDR;
    sets = (ccsidr >> 13) & 0x7FFF;
    do {
        ways = (ccsidr >> 3) & 0x3FF;
        do {
            *op = ((sets & 0x1FF) << 5) | ((ways & 0x3) << 30);
        } while (ways-- != 0);
    } while (sets-- != 0);
    DSB();
}

static void cache_enable(void)
{
    if (!(SCB_CCR & SCB_CCR_IC)) {
        DSB();
        ISB();
        SCB_ICIALLU = 0;
        DSB();
        ISB();
        SCB_CCR |= SCB_CCR_IC;
        DSB();
        ISB();
    }
    if (!(SCB_CCR & SCB_CCR_DC)) {
        dcache_setway(&SCB_DCISW);
        SCB_CCR |= SCB_CCR_DC;
        DSB();
        ISB();
    }
}

/* Hand over with both caches off and empty */
static void cache_disable(void)
{
    if (SCB_CCR & SCB_CCR_DC) {
        dcache_setway(&SCB_DCCISW);
        SCB_CCR &= ~SCB_CCR_DC;
        DSB();
        ISB();
    }
    if (SCB_CCR & SCB_CCR_IC) {
        DSB();
        ISB();
        SCB_CCR &= ~SCB_CCR_IC;
        SCB_ICIALLU = 0;
        DSB();
        ISB();
    }
}

#ifdef DEBUG_UART

#ifndef UART_BAUDRATE
#define UART_BAUDRATE 115200
#endif

/* LPUART5 functional clock, from the clock configuration actually in use
 * (the PLL setup may have failed and left the system on FIRC) */
static uint32_t aips_slow_clk(void)
{
    uint32_t src = FIRC_FREQ;

    if (MC_CGM_CSS_SELSTAT(MC_CGM_MUX0_CSS) == MC_CGM_SEL_PLL_PHI0)
        src = PLL_PHI0_FREQ;
    return src / (MC_CGM_DC_GET_DIV(MC_CGM_MUX0_DC(MC_CGM_DIV_AIPS_SLOW)) + 1);
}

void uart_init(void)
{
    uint32_t clk = aips_slow_clk();
    uint32_t osr, sbr, err;
    uint32_t best_osr = 16, best_sbr = 1, best_err = 0xFFFFFFFFUL;

    /* Pick the oversampling ratio (8..32) with the smallest baud error */
    for (osr = 8; osr <= 32; osr++) {
        sbr = (clk + (UART_BAUDRATE * osr / 2)) / (UART_BAUDRATE * osr);
        if (sbr == 0 || sbr > 0x1FFF)
            continue;
        err = clk / (osr * sbr);
        err = (err > UART_BAUDRATE) ? err - UART_BAUDRATE :
                                      UART_BAUDRATE - err;
        if (err < best_err) {
            best_err = err;
            best_osr = osr;
            best_sbr = sbr;
        }
    }

    mc_me_clock_enable(MC_ME_REQ_LPUART5);

    /* Pin mux: TX output, RX input routed through IMCR */
    SIUL2_MSCR(UART_TX_PAD) = SIUL2_MSCR_OBE | SIUL2_MSCR_SSS(UART_TX_SSS);
    SIUL2_MSCR(UART_RX_PAD) = SIUL2_MSCR_IBE;
    SIUL2_IMCR(UART_RX_IMCR) = UART_RX_IMCR_SSS;

    LPUART_CTRL = 0;
    LPUART_BAUD = ((best_osr - 1) << LPUART_BAUD_OSR_SHIFT) |
                  (best_sbr << LPUART_BAUD_SBR_SHIFT);
    LPUART_CTRL = LPUART_CTRL_TE | LPUART_CTRL_RE;
}

void RAMFUNCTION uart_tx(uint8_t byte)
{
    while (!(LPUART_STAT & LPUART_STAT_TDRE)) {}
    LPUART_DATA = byte;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        if (buf[i] == '\n') {
            uart_tx('\r');
        }
        uart_tx(buf[i]);
    }
    while (!(LPUART_STAT & LPUART_STAT_TC)) {}
}

/* Non-blocking read: returns 1 if a character was read, 0 otherwise */
int RAMFUNCTION uart_read(char* c)
{
    uint32_t stat = LPUART_STAT;

    if (stat & (LPUART_STAT_OR | LPUART_STAT_NF | LPUART_STAT_FE |
                LPUART_STAT_PF)) {
        LPUART_STAT = stat; /* write 1 to clear */
    }
    if (stat & LPUART_STAT_RDRF) {
        *c = (char)(LPUART_DATA & 0xFF);
        return 1;
    }
    return 0;
}

#endif /* DEBUG_UART */

/* Return the PFLASH lock register and bit covering a flash address. The first
 * 768 KB of each code block is locked per 64 KB super sector, the last 256 KB
 * (and the data flash) per 8 KB sector. */
static volatile uint32_t* RAMFUNCTION flash_lock_reg(uint32_t address,
    uint32_t *bit)
{
    uint32_t blk, off;

    if (address >= FLASH_DATA_BASE) {
        *bit = 1UL << ((address - FLASH_DATA_BASE) / FLASH_SECTOR_SIZE);
        return &PFLASH_SPELOCK(4);
    }
    blk = (address - FLASH_BASE) / FLASH_BLOCK_SIZE;
    off = (address - FLASH_BASE) % FLASH_BLOCK_SIZE;
    if (off < FLASH_SUPER_SECTOR_AREA) {
        *bit = 1UL << (off / FLASH_SUPER_SECTOR_SIZE);
        return &PFLASH_SSPELOCK(blk);
    }
    *bit = 1UL << ((off - FLASH_SUPER_SECTOR_AREA) / FLASH_SECTOR_SIZE);
    return &PFLASH_SPELOCK(blk);
}

/* Clearing and restoring the line buffer invalidates stale reads of
 * the flash array after a program or erase */
static void RAMFUNCTION flash_invalidate_buffers(void)
{
    uint32_t pfcr0 = PFLASH_PFCR0;
    uint32_t pfcr1 = PFLASH_PFCR1;
    uint32_t mask = PFLASH_PFCR_CBFEN | PFLASH_PFCR_DBFEN;

    PFLASH_PFCR0 = pfcr0 & ~mask;
    PFLASH_PFCR1 = pfcr1 & ~mask;
    DSB();
    PFLASH_PFCR0 = pfcr0;
    PFLASH_PFCR1 = pfcr1;
    DSB();
    ISB();
}

/* Run the high voltage step of a program (PGM) or erase (ERS) sequence whose
 * interlock writes (PEADR_L and DATA) are already done. addr/len is the
 * flash range that the operation modifies. */
static int RAMFUNCTION flash_hv_operation(uint32_t op, uint32_t addr,
    uint32_t len)
{
    int ret = 0;
    uint32_t primask;

    /* No flash fetches (e.g. interrupt handlers) while the controller is
     * busy: reading the block being modified is a read-while-write error */
    primask = __get_PRIMASK();
    __disable_irq();

    C40ASF_MCR = op;
    C40ASF_MCR = op | C40ASF_MCR_EHV;
    while (!(C40ASF_MCRS & C40ASF_MCRS_DONE)) {}
    if (!(C40ASF_MCRS & C40ASF_MCRS_PEG) ||
            (C40ASF_MCRS & C40ASF_MCRS_ERR_MASK)) {
        ret = -1;
    }
    C40ASF_MCR = op;
    C40ASF_MCR = 0;

    flash_invalidate_buffers();
    dcache_invalidate_range(addr, len);
    __set_PRIMASK(primask);
    return ret;
}

static void RAMFUNCTION flash_prepare(void)
{
    while (!(C40ASF_MCRS & C40ASF_MCRS_DONE)) {}
    /* Clear sticky errors from earlier operations (write 1 to clear) */
    C40ASF_MCRS = C40ASF_MCRS_ERR_MASK;
}

/* Program up to one quad page (128 bytes). words[] holds the whole quad page
 * image; only words whose bit is set in mask are written to DATAn, the
 * others receive no program pulses. */
static int RAMFUNCTION flash_program_qpage(uint32_t qpage,
    const uint32_t *words, uint32_t mask)
{
    volatile uint32_t *lock;
    uint32_t bit;
    int i, ret;
    int first = 1;

    flash_prepare();
    lock = flash_lock_reg(qpage, &bit);
    *lock &= ~bit;

    for (i = 0; i < (FLASH_QUAD_PAGE_SIZE / 4); i++) {
        if (!(mask & (1UL << i)))
            continue;
        if (first) {
            /* Interlock write: selects the quad page to program */
            PFLASH_PEADR_L = qpage + (i * 4);
            first = 0;
        }
        C40ASF_DATA(i) = words[i];
    }
    ret = flash_hv_operation(C40ASF_MCR_PGM, qpage, FLASH_QUAD_PAGE_SIZE);
    *lock |= bit;
    return ret;
}

static int RAMFUNCTION flash_erase_sector(uint32_t address)
{
    volatile uint32_t *lock;
    uint32_t bit;
    int ret;

    flash_prepare();
    lock = flash_lock_reg(address, &bit);
    *lock &= ~bit;

    /* Erase interlock write: sector address plus one DATA write */
    PFLASH_PEADR_L = address;
    C40ASF_DATA(0) = 0;
    ret = flash_hv_operation(C40ASF_MCR_ERS, address, FLASH_SECTOR_SIZE);
    *lock |= bit;
    return ret;
}

void hal_init(void)
{
    /* SWT0 is disabled because the IVT boot configuration word does not
     * request it */
    int clk_ret = clock_init();

    cache_enable();

#ifdef DEBUG_UART
    uart_init();
#endif

#if defined(DEBUG_UART) && defined(__WOLFBOOT)
#ifdef WOLFBOOT_REPRODUCIBLE_BUILD
    wolfBoot_printf("wolfBoot Version: %s\n", LIBWOLFBOOT_VERSION_STRING);
#else
    wolfBoot_printf("wolfBoot Version: %s (%s %s)\n",
        LIBWOLFBOOT_VERSION_STRING, __DATE__, __TIME__);
#endif
    if (clk_ret != 0)
        wolfBoot_printf("PLL setup failed: running from FIRC 48 MHz\n");
#else
    (void)clk_ret;
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    /* Leave the UART idle so the application can reinitialize it */
    while (!(LPUART_STAT & LPUART_STAT_TC)) {}
    LPUART_CTRL = 0;
#endif
    /* The application starts with the clocks left at 160 MHz and with both
     * caches off */
    cache_disable();
}

/* Program any length at any address. Writes are split per quad page; each
 * 64-bit ECC segment that changes is merged with the current flash contents
 * and programmed once. Segments that are unchanged are skipped, because the
 * C40ASF allows only one program per ECC segment between erases. */
int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t words[FLASH_QUAD_PAGE_SIZE / 4];
    uint8_t *buf = (uint8_t *)words;
    uint32_t qpage, start, end, off, mask;
    int ret;

    while (len > 0) {
        qpage = address & ~(FLASH_QUAD_PAGE_SIZE - 1);
        start = address - qpage;
        end = start + (uint32_t)len;
        if (end > FLASH_QUAD_PAGE_SIZE)
            end = FLASH_QUAD_PAGE_SIZE;

        memcpy(buf, (const void *)qpage, FLASH_QUAD_PAGE_SIZE);
        memcpy(buf + start, data, end - start);

        mask = 0;
        for (off = start & ~(FLASH_DWORD_SIZE - 1); off < end;
                off += FLASH_DWORD_SIZE) {
            if (memcmp(buf + off, (const void *)(qpage + off),
                    FLASH_DWORD_SIZE) != 0) {
                mask |= 3UL << (off / 4);
            }
        }
        if (mask != 0) {
            ret = flash_program_qpage(qpage, words, mask);
            if (ret != 0)
                return ret;
        }
        data += end - start;
        address += end - start;
        len -= (int)(end - start);
    }
    return 0;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    int ret;

    if (len <= 0)
        return -1;

    address &= ~(FLASH_SECTOR_SIZE - 1);
    while (len > 0) {
        ret = flash_erase_sector(address);
        if (ret != 0)
            return ret;
        address += FLASH_SECTOR_SIZE;
        len -= FLASH_SECTOR_SIZE;
    }
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}
