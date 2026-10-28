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
#include "spi_drv.h"
#include "spi_flash.h"
#include <target.h>

#define PSOC_E8_UART_SCB_BASE   PSOC_E8_SCB_BASE(PSOC_E8_UART_SCB)

#ifndef NONSECURE_APP

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
        uint32_t addr_len, int last)
{
    uint32_t ss = SMIF_CMD_SS(dev) | SMIF_CMD_MODE_TX;
    uint32_t i;

    if (smif_push_cmd(ss | ((last && (addr_len == 0U)) ? SMIF_CMD_LAST : 0) |
            op) != 0)
        return -1;
    for (i = addr_len; i > 0U; i--) {
        if (smif_push_cmd(ss | ((last && (i == 1U)) ? SMIF_CMD_LAST : 0) |
                ((addr >> ((i - 1U) * 8U)) & 0xFFU)) != 0)
            return -1;
    }
    return 0;
}

/* The device extended boot configured for XIP. */
static int RAMFUNCTION smif_device(void)
{
    int n;

    for (n = 0; n < SMIF_DEVICES; n++) {
        if ((SMIF_DEVICE_CTL(n) & SMIF_DEVICE_CTL_ENABLED) != 0)
            return n;
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

/* src/qspi_flash.c bus hooks. Extended boot has already configured SMIF0 and
 * the device for XIP, so there is nothing to set up or release. */
void spi_init(int polarity, int phase)
{
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
}

/* Set when a status read shows a failed program or erase. The flags are
 * cleared on the spot, so the driver's next poll reads ready; the HAL checks
 * this to report the failure. */
static int qspi_failed;

/* Program data is staged here first: the source is often in XIP flash
 * itself, and an XIP read while a page program waits for its data
 * deadlocks. */
static uint8_t qspi_page[SPI_FLASH_PAGE_SIZE];

/* qspi_transfer() for src/qspi_flash.c over the SMIF MMIO command interface,
 * single-line only. Extended boot leaves XIP enabled; MMIO commands run
 * alongside it while nothing executes from the device. */
int RAMFUNCTION qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t *data, uint32_t dataSz, uint32_t dataMode)
{
    int dev = smif_device();
    uint32_t timeout;
    uint32_t i;

    (void)addrMode;
    (void)alt;
    (void)altMode;
    (void)dataMode;
    if ((dev < 0) || (altSz != 0U) || (dummySz != 0U) ||
        ((fmode == QSPI_MODE_WRITE) && (dataSz > sizeof(qspi_page))) ||
        ((dataSz != 0U) && (data == NULL)))
        return -1;
    if (fmode == QSPI_MODE_WRITE) {
        for (i = 0; i < dataSz; i++)
            qspi_page[i] = data[i];
    }
    if ((smif_wait_idle() != 0) ||
        (smif_send(dev, cmd, addr, addrSz, (dataSz == 0U)) != 0))
        return -1;
    if (dataSz == 0U)
        return 0;

    if (fmode == QSPI_MODE_WRITE) {
        if (smif_push_cmd(SMIF_CMD_MODE_TX_COUNT | SMIF_CMD_LAST |
                (dataSz - 1U)) != 0)
            return -1;
        for (i = 0; i < dataSz; i++) {
            if (smif_wait_fifo(&SMIF_TX_DATA_STATUS) != 0)
                return -1;
            SMIF_TX_DATA_WR1 = qspi_page[i];
        }
        return 0;
    }

    if (smif_push_cmd(SMIF_CMD_MODE_RX_COUNT | SMIF_CMD_LAST |
            (dataSz - 1U)) != 0)
        return -1;
    /* The receiver needs 1 us to enter receive mode before its FIFO status
     * is valid. */
    psoc_e8_delay_us(1);
    for (i = 0; i < dataSz; i++) {
        timeout = PSOC_E8_PERI_TIMEOUT;
        while (((SMIF_RX_DATA_STATUS & SMIF_FIFO_USED_Msk) == 0U) &&
               (timeout > 0U))
            timeout--;
        if (timeout == 0U)
            return -1;
        data[i] = (uint8_t)SMIF_RX_DATA_RD1;
    }
    /* A failed program or erase (e.g. a second program into a 16-byte ECC
     * unit) leaves the part busy until its failure flags are cleared, even
     * across a warm reset, where extended boot can then no longer read it. */
    if ((cmd == NOR_CMD_RDSR) &&
        ((data[0] & (NOR_SR_E_ERR | NOR_SR_P_ERR)) != 0U)) {
        (void)smif_send(dev, NOR_CMD_CLPEF, 0, 0, 1);
        qspi_failed = 1;
        return -1;
    }
    return 0;
}

static uint32_t RAMFUNCTION irq_save(void)
{
    uint32_t primask = 0;

#if defined(__arm__) || defined(__ARM_ARCH)
    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    __asm__ volatile ("cpsid i" ::: "memory");
#endif
    return primask;
}

static void RAMFUNCTION irq_restore(uint32_t primask)
{
#if defined(__arm__) || defined(__ARM_ARCH)
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory");
#else
    (void)primask;
#endif
}

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
 * data == NULL fills with 0xFF, so an erased range reads like erased NOR. */
static int RAMFUNCTION rram_write_block(uint32_t off, const uint8_t *data,
        uint32_t len)
{
    volatile uint32_t *blk = (volatile uint32_t *)(uintptr_t)
        ((PSOC_E8_RRAM_SBUS_S + off) & ~(RRAM_BLOCK_SIZE - 1U));
    uint32_t pos = off & (RRAM_BLOCK_SIZE - 1U);
    uint32_t phys = off / RRAM_BLOCK_SIZE;
    uint8_t *b = (uint8_t *)rram_blk;
    uint32_t verr;
    uint32_t i;
    uint32_t t;
    int ecc_fail;

    RRAMC_NVM_ADDR = phys;
    if (rram_wait_idle() != 0)
        return -1;
    /* Setting NVM_ADDR reads the block; one that fails ECC is rebuilt from
     * zeros, like the vendor driver. */
    ecc_fail = ((RRAMC_NVM_STATUS & RRAMC_NVM_STATUS_ECC4MRD) != 0U);
    for (i = 0; i < RRAM_BLOCK_SIZE / sizeof(uint32_t); i++)
        rram_blk[i] = ecc_fail ? 0U : blk[i];
    for (i = 0; i < len; i++)
        b[pos + i] = (data != NULL) ? data[i] : 0xFFU;
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

/* RRAM aliases go to the RRAM controller, anything else is an SMIF0 alias
 * programmed through src/qspi_flash.c. Nothing may run from QSPI while it is
 * busy, so interrupts stay masked. */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t off = address & (PSOC_E8_SMIF_WINDOW - 1U);
    uint32_t primask;
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

    primask = irq_save();
    qspi_failed = 0;
    while ((len > 0) && (ret == 0)) {
        ret = spi_flash_sector_erase(off);
        if (qspi_failed)
            ret = -1;
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
    uint32_t primask;
    int ret;

    if ((data == NULL) || (len <= 0))
        return -1;
    primask = irq_save();
    if (is_rram(address, &off)) {
        ret = rram_write(off, data, len);
        irq_restore(primask);
        return ret;
    }
    qspi_failed = 0;
    ret = spi_flash_write(off, data, len);
    if (qspi_failed)
        ret = -1;
    smif_invalidate_caches();
    irq_restore(primask);
    return ret;
}

#endif /* !NONSECURE_APP */

/* Extend the cycle counter to 64 bits. Each read recovers at most one wrap,
 * about 21 s at 200 MHz. */
uint64_t hal_get_timer_us(void)
{
    static uint64_t hi;
    static uint32_t last;
    uint32_t now;

    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    now = DWT_CYCCNT;
    if (now < last)
        hi += 0x100000000ULL;
    last = now;
    return (hi + now) / (PSOC_E8_CPU_HZ / 1000000UL);
}

void RAMFUNCTION psoc_e8_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t chunk;

    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    /* Wait in 1 s steps so the cycle count stays within 32 bits. */
    while (us > 0U) {
        chunk = (us > 1000000UL) ? 1000000UL : us;
        start = DWT_CYCCNT;
        while ((DWT_CYCCNT - start) < (chunk * (PSOC_E8_CPU_HZ / 1000000UL)))
            ;
        us -= chunk;
    }
}

#ifndef __WOLFBOOT
/* wolfBoot's own arch_reboot() lives in src/boot_arm.c. */
void arch_reboot(void)
{
    AIRCR = AIRCR_VKEY | AIRCR_SYSRESETREQ;
    DSB();
    while (1)
        ;
}
#endif

#if defined(NONSECURE_APP)

/* The flash controller belongs to the Secure world; the application updates
 * through the wolfBoot_nsc_* gateway calls instead. */
void hal_flash_unlock(void)
{
}

void hal_flash_lock(void)
{
}

int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

/* The console belongs to the Secure world; print through its gateway. */
void uart_init(void)
{
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int n;

    while (sz > 0U) {
        n = (sz > PSOC_E8_NSC_UART_MAX) ? PSOC_E8_NSC_UART_MAX : sz;
        psoc_e8_nsc_uart_write(buf, n);
        buf += n;
        sz -= n;
    }
}

#elif defined(DEBUG_UART) || !defined(__WOLFBOOT)

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
#ifdef TZEN
    hal_gtzc_init();
    hal_tz_sau_init();
#endif
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
