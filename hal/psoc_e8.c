/* psoc_e8.c
 *
 * HAL for Infineon PSOC Edge E8 (CAT1D, Cortex-M33).
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

#include "image.h"
#include "hal.h"
#include "printf.h"
#include "hal/psoc_e8.h"
#include <target.h>

/* Console: KIT_PSE84_AI routes the KitProg3 COM port to SCB2 P6.5/P6.7. */
#ifndef PSOC_E8_UART_SCB
#define PSOC_E8_UART_SCB        2
#endif
#define PSOC_E8_UART_SCB_BASE   PSOC_E8_SCB_BASE(PSOC_E8_UART_SCB)
#ifndef PSOC_E8_UART_PORT
#define PSOC_E8_UART_PORT       6
#endif
#ifndef PSOC_E8_UART_RX_PIN
#define PSOC_E8_UART_RX_PIN     5
#endif
#ifndef PSOC_E8_UART_TX_PIN
#define PSOC_E8_UART_TX_PIN     7
#endif
#ifndef PSOC_E8_UART_HSIOM_SEL
#define PSOC_E8_UART_HSIOM_SEL  20
#endif

/* Clock destination of the console SCB (PCLK_SCB2_CLOCK_SCB_EN = 0x101). */
#ifndef PSOC_E8_UART_PCLK_GR
#define PSOC_E8_UART_PCLK_GR    1
#endif
#ifndef PSOC_E8_UART_PCLK_IDX
#define PSOC_E8_UART_PCLK_IDX   1
#endif
#ifndef PSOC_E8_UART_PCLK_DIV
#define PSOC_E8_UART_PCLK_DIV   0
#endif

/* CLK_HF10 as extended boot leaves it; wolfBoot does not touch the clocks. */
#ifndef PSOC_E8_PCLK_HZ
#define PSOC_E8_PCLK_HZ         100000000UL
#endif
#ifndef PSOC_E8_UART_BAUD
#define PSOC_E8_UART_BAUD       115200UL
#endif
#define PSOC_E8_UART_OVS        8UL

static int peri_init_done;

/* Ungate PERI0 group 1, which holds the SCBs. Ungating groups 3-5 as well
 * takes the core off the debug port. */
void psoc_e8_peri_init(void)
{
    if (peri_init_done)
        return;
    peri_init_done = 1;

    PERI_GR_SL_CTL2(PSOC_E8_PERI_GR_SCB) = 0;
    PERI_GR_SL_CTL(PSOC_E8_PERI_GR_SCB) = 0xFFFFFFFFUL;
    DSB();
}

void psoc_e8_pin_setup(int port, int pin, uint32_t sel, uint32_t drive)
{
    uint32_t v;

    HSIOM_SECURE_PRT_NSMASK(port) &= ~(1UL << pin);

    v = HSIOM_PORT_SEL(port, pin);
    v &= ~(0xFFUL << HSIOM_SEL_SHIFT(pin));
    v |= (sel << HSIOM_SEL_SHIFT(pin));
    HSIOM_PORT_SEL(port, pin) = v;

    v = GPIO_PRT_CFG(port);
    v &= ~(GPIO_CFG_DM_MASK << ((uint32_t)pin * 4U));
    v |= (drive << ((uint32_t)pin * 4U));
    GPIO_PRT_CFG(port) = v;
}

/* Route a 16.5 fractional divider at target_hz to a clock destination. An
 * unclocked peripheral bus-faults, so a failure here must stop the caller. */
int psoc_e8_pclk_setup(uint32_t gr, uint32_t idx, uint32_t div,
        uint32_t pclk_hz, uint32_t target_hz)
{
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;
    uint32_t scaled;

    if (target_hz == 0U)
        return -1;

    /* The divider produces pclk / (int + 1 + frac / 32). */
    scaled = (uint32_t)(((uint64_t)pclk_hz * 32U) / target_hz);
    if (scaled < 32U)
        scaled = 32U;
    if (scaled > (0x10000UL * 32U))
        scaled = 0x10000UL * 32U;

    PERI_PCLK_DIV_16_5_CTL(gr, div) =
        (((scaled / 32U) - 1U) << PERI_PCLK_DIV_16_5_INT_Pos) |
        ((scaled % 32U) << PERI_PCLK_DIV_16_5_FRAC_Pos);
    PERI_PCLK_CLOCK_CTL(gr, idx) =
        (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_CLOCK_CTL_TYPE_Pos) | div;
    PERI_PCLK_DIV_CMD(gr) = PERI_PCLK_DIV_CMD_ENABLE |
        PERI_PCLK_DIV_CMD_PA_NONE |
        (PERI_PCLK_DIV_TYPE_16_5 << PERI_PCLK_DIV_CMD_TYPE_Pos) | div;

    while (timeout > 0) {
        if ((PERI_PCLK_DIV_16_5_CTL(gr, div) & PERI_PCLK_DIV_16_5_EN) != 0)
            return 0;
        timeout--;
    }
    return -1;
}

/* QSPI NOR erase sector (NOR_CMD_SE4). */
#ifndef PSOC_E8_QSPI_SECTOR_SIZE
#define PSOC_E8_QSPI_SECTOR_SIZE    (0x40000UL)
#endif

/* Status polls allowed per erase or program. */
#ifndef PSOC_E8_QSPI_TIMEOUT
#define PSOC_E8_QSPI_TIMEOUT        (20000000UL)
#endif

static int RAMFUNCTION smif_wait_fifo(volatile uint32_t *status)
{
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    while (((*status & SMIF_FIFO_USED_Msk) >= SMIF_FIFO_DEPTH) &&
           (timeout > 0))
        timeout--;
    return (timeout == 0) ? -1 : 0;
}

static int RAMFUNCTION smif_push_cmd(uint32_t entry)
{
    if (smif_wait_fifo(&SMIF_TX_CMD_STATUS) != 0)
        return -1;
    SMIF_TX_CMD_WR = entry;
    return 0;
}

/* Single-line opcode plus addr_len address bytes, MSB first. last releases
 * chip select after the final byte; otherwise a data phase follows. */
static int RAMFUNCTION smif_send(int dev, uint8_t op, uint32_t addr,
        int addr_len, int last)
{
    uint32_t ss = SMIF_CMD_SS(dev) | SMIF_CMD_MODE_TX;
    int i;

    if (smif_push_cmd(ss | ((last && (addr_len == 0)) ? SMIF_CMD_LAST : 0) |
            op) != 0)
        return -1;
    for (i = addr_len - 1; i >= 0; i--) {
        if (smif_push_cmd(ss | ((last && (i == 0)) ? SMIF_CMD_LAST : 0) |
                ((addr >> (i * 8)) & 0xFFU)) != 0)
            return -1;
    }
    return 0;
}

static int RAMFUNCTION smif_read_sr(int dev, uint8_t *sr)
{
    volatile uint32_t settle = 200;
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    if (smif_send(dev, NOR_CMD_RDSR, 0, 0, 0) != 0)
        return -1;
    if (smif_push_cmd(SMIF_CMD_MODE_RX_COUNT | SMIF_CMD_LAST) != 0)
        return -1;
    /* About 1 us for the receiver to enter receive mode. */
    while (settle > 0)
        settle--;
    while (((SMIF_RX_DATA_STATUS & SMIF_FIFO_USED_Msk) == 0) && (timeout > 0))
        timeout--;
    if (timeout == 0)
        return -1;
    *sr = (uint8_t)SMIF_RX_DATA_RD1;
    return 0;
}

/* A failed program or erase (e.g. a second program into a 16-byte ECC unit)
 * leaves WIP stuck until the flags are cleared, even across a warm reset, and
 * extended boot can then no longer read the flash. */
static int RAMFUNCTION smif_wait_ready(int dev)
{
    uint32_t timeout = PSOC_E8_QSPI_TIMEOUT;
    uint8_t sr = NOR_SR_WIP;

    while (timeout > 0) {
        if (smif_read_sr(dev, &sr) != 0)
            return -1;
        if ((sr & (NOR_SR_E_ERR | NOR_SR_P_ERR)) != 0) {
            (void)smif_send(dev, NOR_CMD_CLPEF, 0, 0, 1);
            return -1;
        }
        if ((sr & NOR_SR_WIP) == 0)
            return 0;
        timeout--;
    }
    return -1;
}

/* The XIP device extended boot configured; 3- or 4-byte addressing only. */
static int RAMFUNCTION smif_device(int *addr_len)
{
    int n;

    for (n = 0; n < SMIF_DEVICES; n++) {
        if ((SMIF_DEVICE_CTL(n) & SMIF_DEVICE_CTL_ENABLED) != 0) {
            *addr_len = (int)(SMIF_DEVICE_ADDR_CTL(n) &
                              SMIF_ADDR_CTL_SIZE_Msk) + 1;
            if ((*addr_len != 3) && (*addr_len != 4))
                return -1;
            return n;
        }
    }
    return -1;
}

/* Drop stale XIP lines from the SMIF cache and the CM33 I-cache. */
static void RAMFUNCTION smif_invalidate_caches(void)
{
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    SMIF_CACHE_MAINT_ALL = SMIF_CACHE_INVALIDATE_ALL;
    while (((SMIF_CACHE_MAINT_STATUS & SMIF_CACHE_MAINT_ONGOING) != 0) &&
           (timeout > 0))
        timeout--;
    SMIF_SLOW_CA_CMD = SMIF_CA_CMD_INV;
    SMIF_FAST_CA_CMD = SMIF_CA_CMD_INV;
    ICACHE0_CMD = ICACHE_CMD_INV;
    DSB();
    ISB();
}

static int RAMFUNCTION smif_wait_idle(void)
{
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    while (((SMIF_STATUS & SMIF_STATUS_BUSY) != 0) && (timeout > 0))
        timeout--;
    return (timeout == 0) ? -1 : 0;
}

static int RAMFUNCTION qspi_erase_sector(int dev, int addr_len, uint32_t off)
{
    if (smif_wait_idle() != 0)
        return -1;
    if (smif_send(dev, NOR_CMD_WREN, 0, 0, 1) != 0)
        return -1;
    if (smif_send(dev, (addr_len == 4) ? NOR_CMD_SE4 : NOR_CMD_SE, off,
            addr_len, 1) != 0)
        return -1;
    return smif_wait_ready(dev);
}

/* The source is often in XIP flash itself, and an XIP read while a page
 * program waits for its data deadlocks, so each page is staged here. */
static uint8_t qspi_page[NOR_PAGE_SIZE];

/* Program within one page. */
static int RAMFUNCTION qspi_program(int dev, int addr_len, uint32_t off,
        const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
        qspi_page[i] = data[i];
    if (smif_wait_idle() != 0)
        return -1;
    if (smif_send(dev, NOR_CMD_WREN, 0, 0, 1) != 0)
        return -1;
    if (smif_send(dev, (addr_len == 4) ? NOR_CMD_PP4 : NOR_CMD_PP, off,
            addr_len, 0) != 0)
        return -1;
    if (smif_push_cmd(SMIF_CMD_MODE_TX_COUNT | SMIF_CMD_LAST | (len - 1U)) != 0)
        return -1;
    for (i = 0; i < len; i++) {
        if (smif_wait_fifo(&SMIF_TX_DATA_STATUS) != 0)
            return -1;
        SMIF_TX_DATA_WR1 = qspi_page[i];
    }
    return smif_wait_ready(dev);
}

static uint32_t RAMFUNCTION irq_save(void)
{
    uint32_t primask;

    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");
    return primask;
}

static void RAMFUNCTION irq_restore(uint32_t primask)
{
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

#ifndef PSOC_E8_RRAM_TIMEOUT
#define PSOC_E8_RRAM_TIMEOUT        (1000000UL)
#endif

static uint32_t rram_blk[RRAM_BLOCK_SIZE / sizeof(uint32_t)];

static int RAMFUNCTION rram_wait_idle(void)
{
    uint32_t timeout = PSOC_E8_RRAM_TIMEOUT;

    while (((RRAMC_NVM_STATUS & RRAMC_NVM_STATUS_BUSY) != 0) &&
           (timeout > 0))
        timeout--;
    return (timeout == 0) ? -1 : 0;
}

static int RAMFUNCTION rram_lock(void)
{
    if ((IPC0_RRAM_ACQUIRE & IPC_ACQUIRE_SUCCESS) == 0)
        return -1;
    /* MS_CTL faults from here, so unlike the vendor driver the active
     * protection context is not cross-checked. */
    RRAMC_PC_MASK |= RRAMC_PC_MASK_PC_LOCK_Msk;
    if ((RRAMC_PC_MASK & RRAMC_PC_MASK_PC_LOCK_Msk) !=
            RRAMC_PC_MASK_PC_LOCK_Msk) {
        RRAMC_PC_MASK &= ~RRAMC_PC_MASK_PC_LOCK_Msk;
        IPC0_RRAM_RELEASE = 0;
        return -1;
    }
    return 0;
}

static void RAMFUNCTION rram_unlock(void)
{
    RRAMC_PC_MASK &= ~RRAMC_PC_MASK_PC_LOCK_Msk;
    IPC0_RRAM_RELEASE = 0;
}

/* Write within one 16-byte block; off / 16 is the physical block index.
 * data == NULL writes zeros, the RRAM form of an erase. */
static int RAMFUNCTION rram_write_block(uint32_t off, const uint8_t *data,
        uint32_t len)
{
    volatile uint32_t *blk = (volatile uint32_t *)((PSOC_E8_RRAM_SBUS_S +
                                 off) & ~(RRAM_BLOCK_SIZE - 1U));
    uint32_t pos = off & (RRAM_BLOCK_SIZE - 1U);
    uint32_t phys = off / RRAM_BLOCK_SIZE;
    uint8_t *b = (uint8_t *)rram_blk;
    uint32_t verr;
    uint32_t i;
    uint32_t t;

    RRAMC_NVM_ADDR = phys;
    if (rram_wait_idle() != 0)
        return -1;
    /* A block failing ECC is rebuilt from zeros, like the vendor driver. */
    for (i = 0; i < RRAM_BLOCK_SIZE / sizeof(uint32_t); i++) {
        rram_blk[i] = ((RRAMC_NVM_STATUS & RRAMC_NVM_STATUS_ECC_MULTI) != 0) ?
            0U : blk[i];
    }
    for (i = 0; i < len; i++)
        b[pos + i] = (data != NULL) ? data[i] : 0U;
    if (RRAMC_NVM_ADDR != phys)
        return -1;

    for (t = 0; t < RRAM_WRITE_TRIALS; t++) {
        for (i = 0; i < RRAM_BLOCK_SIZE / sizeof(uint32_t); i++)
            blk[i] = rram_blk[i];
        RRAMC_NVM_PROG |= RRAMC_NVM_PROG_STARTOP;
        if (rram_wait_idle() != 0)
            return -1;
        verr = (RRAMC_NVM_STATUS & RRAMC_NVM_STATUS_VERR_Msk) >>
            RRAMC_NVM_STATUS_VERR_Pos;
        /* 0-1 ok, 2 ok on the last try, 5-7 fatal, else retry. */
        if ((verr <= 1U) || ((verr == 2U) && (t == RRAM_WRITE_TRIALS - 1U)))
            return 0;
        if (verr >= 5U)
            return -1;
    }
    return -1;
}

static int RAMFUNCTION rram_write(uint32_t off, const uint8_t *data, int len)
{
    uint32_t chunk;
    int ret = 0;

    if ((off + (uint32_t)len) > PSOC_E8_RRAM_SIZE)
        return -1;
    if (rram_lock() != 0)
        return -1;
    RRAMC_NVM_PROG = (RRAMC_NVM_PROG & ~RRAMC_NVM_PROG_ACTION_Msk) |
        RRAMC_NVM_PROG_IDLE;
    RRAMC_NVM_PROG = (RRAMC_NVM_PROG & ~RRAMC_NVM_PROG_ACTION_Msk) |
        RRAMC_NVM_PROG_IND_WRITE;
    while ((len > 0) && (ret == 0)) {
        chunk = RRAM_BLOCK_SIZE - (off & (RRAM_BLOCK_SIZE - 1U));
        if (chunk > (uint32_t)len)
            chunk = (uint32_t)len;
        ret = rram_write_block(off, data, chunk);
        off += chunk;
        if (data != NULL)
            data += chunk;
        len -= (int)chunk;
    }
    RRAMC_NVM_PROG = (RRAMC_NVM_PROG & ~RRAMC_NVM_PROG_ACTION_Msk) |
        RRAMC_NVM_PROG_IDLE;
    rram_unlock();
    ICACHE0_CMD = ICACHE_CMD_INV;
    DSB();
    ISB();
    return ret;
}

/* Any RRAM alias (0x02/0x12/0x22/0x32000000) selects the RRAM path. */
static int RAMFUNCTION is_rram(uint32_t address, uint32_t *off)
{
    uint32_t a = (address & 0x0FFFFFFFUL) -
        (PSOC_E8_RRAM_SBUS_S & 0x0FFFFFFFUL);

    if (((address >> 28) > 3U) || (a >= PSOC_E8_RRAM_SIZE))
        return 0;
    *off = a;
    return 1;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* RRAM aliases go to the RRAM controller, anything else is an SMIF0 alias.
 * Nothing may run from QSPI while it is busy, so interrupts stay masked. */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t off = address & (PSOC_E8_SMIF_WINDOW - 1U);
    uint32_t primask;
    int addr_len = 0;
    int dev;
    int ret = 0;

    if (len <= 0)
        return -1;
    if (is_rram(address, &off)) {
        primask = irq_save();
        ret = rram_write(off, NULL, len);
        irq_restore(primask);
        return ret;
    }
    if (((off % PSOC_E8_QSPI_SECTOR_SIZE) != 0) ||
        (((uint32_t)len % PSOC_E8_QSPI_SECTOR_SIZE) != 0))
        return -1;
    dev = smif_device(&addr_len);
    if (dev < 0)
        return -1;

    primask = irq_save();
    while ((len > 0) && (ret == 0)) {
        ret = qspi_erase_sector(dev, addr_len, off);
        off += PSOC_E8_QSPI_SECTOR_SIZE;
        len -= (int)PSOC_E8_QSPI_SECTOR_SIZE;
    }
    smif_invalidate_caches();
    irq_restore(primask);
    return ret;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t off = address & (PSOC_E8_SMIF_WINDOW - 1U);
    uint32_t chunk;
    uint32_t primask;
    int addr_len = 0;
    int dev;
    int ret = 0;

    if ((data == NULL) || (len <= 0))
        return -1;
    if (is_rram(address, &off)) {
        primask = irq_save();
        ret = rram_write(off, data, len);
        irq_restore(primask);
        return ret;
    }
    dev = smif_device(&addr_len);
    if (dev < 0)
        return -1;

    primask = irq_save();
    while ((len > 0) && (ret == 0)) {
        chunk = NOR_PAGE_SIZE - (off % NOR_PAGE_SIZE);
        if (chunk > (uint32_t)len)
            chunk = (uint32_t)len;
        ret = qspi_program(dev, addr_len, off, data, chunk);
        off += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    smif_invalidate_caches();
    irq_restore(primask);
    return ret;
}

#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

/* Set once the console SCB is clocked; gates every register access. */
static int uart_ready;

void uart_init(void)
{
    psoc_e8_peri_init();
    psoc_e8_pin_setup(PSOC_E8_UART_PORT, PSOC_E8_UART_RX_PIN,
            PSOC_E8_UART_HSIOM_SEL, GPIO_CFG_DM_HIGHZ);
    psoc_e8_pin_setup(PSOC_E8_UART_PORT, PSOC_E8_UART_TX_PIN,
            PSOC_E8_UART_HSIOM_SEL, GPIO_CFG_DM_STRONG);

    uart_ready = 0;
    if (psoc_e8_pclk_setup(PSOC_E8_UART_PCLK_GR, PSOC_E8_UART_PCLK_IDX,
            PSOC_E8_UART_PCLK_DIV, PSOC_E8_PCLK_HZ,
            PSOC_E8_UART_BAUD * PSOC_E8_UART_OVS) != 0)
        return;

    SCB_CTRL(PSOC_E8_UART_SCB_BASE) = 0;
    SCB_UART_CTRL(PSOC_E8_UART_SCB_BASE) = 0;         /* standard UART */
    SCB_UART_TX_CTRL(PSOC_E8_UART_SCB_BASE) = 1;      /* one stop bit */
    SCB_UART_RX_CTRL(PSOC_E8_UART_SCB_BASE) = 1;
    SCB_TX_CTRL(PSOC_E8_UART_SCB_BASE) = SCB_DATA_WIDTH(8);
    SCB_RX_CTRL(PSOC_E8_UART_SCB_BASE) = SCB_DATA_WIDTH(8);
    SCB_TX_FIFO_CTRL(PSOC_E8_UART_SCB_BASE) = 0x3F;
    SCB_RX_FIFO_CTRL(PSOC_E8_UART_SCB_BASE) = 0x3F;
    SCB_CTRL(PSOC_E8_UART_SCB_BASE) = SCB_CTRL_ENABLED | SCB_CTRL_MODE_UART |
        ((PSOC_E8_UART_OVS - 1UL) & SCB_CTRL_OVS_MASK);
    uart_ready = 1;
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;
    uint32_t timeout;

    if (!uart_ready)
        return;
    for (i = 0; i < sz; i++) {
        timeout = PSOC_E8_PERI_TIMEOUT;
        while (((SCB_TX_FIFO_STATUS(PSOC_E8_UART_SCB_BASE) &
                 SCB_TX_FIFO_USED_Msk) >= SCB_TX_FIFO_SAFE_DEPTH) &&
               (timeout > 0))
            timeout--;
        if (timeout == 0)
            return;
        SCB_TX_FIFO_WR(PSOC_E8_UART_SCB_BASE) = (uint32_t)(uint8_t)buf[i];
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */

#ifdef __WOLFBOOT

void hal_init(void)
{
    psoc_e8_peri_init();
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
#endif
}

void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    if (!uart_ready)
        return;
    while (((SCB_TX_FIFO_STATUS(PSOC_E8_UART_SCB_BASE) &
             (SCB_TX_FIFO_USED_Msk | SCB_TX_FIFO_SR_VALID_Msk)) != 0U) &&
           (timeout > 0))
        timeout--;
#endif
}

#endif /* __WOLFBOOT */
