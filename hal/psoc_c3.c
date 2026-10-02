/* psoc_c3.c
 *
 * HAL for Infineon PSOC Control C3 (CAT1B, Cortex-M33).
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
#include <string.h>

#include "image.h"
#include "hal.h"
#include "printf.h"
#include "hal/psoc_c3.h"
#include <target.h>

/* Console SCB and its pin muxing. Defaults match the PSOC Control C3
 * evaluation kit, where the on-board debug probe's virtual COM port lands on
 * SCB3 P6.2/P6.3. */
#ifndef PSOC_C3_UART_SCB
#define PSOC_C3_UART_SCB        3
#endif
#define PSOC_C3_UART_SCB_BASE   PSOC_C3_SCB_BASE(PSOC_C3_UART_SCB)
#ifndef PSOC_C3_UART_PORT
#define PSOC_C3_UART_PORT       6
#endif
#ifndef PSOC_C3_UART_RX_PIN
#define PSOC_C3_UART_RX_PIN     2
#endif
#ifndef PSOC_C3_UART_TX_PIN
#define PSOC_C3_UART_TX_PIN     3
#endif
#ifndef PSOC_C3_UART_HSIOM_SEL
#define PSOC_C3_UART_HSIOM_SEL  16
#endif

/* Peripheral clock routing for the console SCB. The index is NOT the one the
 * PSC3M5 headers give for SCB3: on a C3M6 the console is driven through
 * group 4 index 6, confirmed by perturbing the divider on a running part.
 * Treat these as per-part values and override them for another variant. */
#ifndef PSOC_C3_UART_PCLK_GR
#define PSOC_C3_UART_PCLK_GR    4
#endif
#ifndef PSOC_C3_UART_PCLK_IDX
#define PSOC_C3_UART_PCLK_IDX   6
#endif
#ifndef PSOC_C3_UART_PCLK_DIV
#define PSOC_C3_UART_PCLK_DIV   1
#endif

/* Frequency feeding the peripheral clock group. wolfBoot does not touch the
 * clock tree, so this is the power-on IHO rate. */
#ifndef PSOC_C3_PCLK_HZ
#define PSOC_C3_PCLK_HZ         48000000UL
#endif
#ifndef PSOC_C3_UART_BAUD
#define PSOC_C3_UART_BAUD       115200UL
#endif
#define PSOC_C3_UART_OVS        8UL

/* The BootROM refresh bookkeeping is indexed by absolute 128 KB sector, so this
 * must cover the whole device rather than just the partitions. Eight entries
 * span 1 MB, comfortably more than any part in the family. */
#ifndef PSOC_C3_FLASH_SECTORS
#define PSOC_C3_FLASH_SECTORS   8
#endif

/* BootROM flash API: a table of function pointers at a fixed address. Only the
 * three blocking row operations are used; the 29 preceding entries are skipped
 * by the reserved field. The pointers are range-checked before first use, so a
 * table layout change fails loudly rather than branching into hyperspace. */
#define PSOC_C3_ROM_BASE        (0x10800000UL)
#define PSOC_C3_ROM_SIZE        (0x10000UL)
#define PSOC_C3_ROM_FUNC_ADDR   (0x1080FF6CUL)
#define PSOC_C3_ROM_SKIP        29

#define CYBOOT_FLASH_SUCCESS    (0x0D50B002UL)
/* flags == 0 selects a blocking operation and lets the ROM compute the row's
 * column-33 metadata itself. */
#define CYBOOT_FLAGS_BLOCKING   (0UL)

typedef struct {
    uint32_t min_count;
    uint32_t max_count;
    uint32_t min_page_addr;
    uint32_t scratch_row_idx;
} cyboot_flash_refresh_t;

typedef struct {
    uint32_t flags;
    uint32_t hv_params_addr;
    cyboot_flash_refresh_t *refresh;
    void (*callback_pre_irq)(void *ctx);
    void (*callback_post_irq)(void *ctx);
    void (*callback_complete)(void *ctx);
    uint32_t callback_param;
    uint32_t state;
    uint32_t flash_addr;
    uint32_t data_addr;
    uint32_t reserved[2];
} cyboot_flash_context_t;

typedef uint32_t (*cyboot_flash_erase_row_t)(uint32_t addr,
        cyboot_flash_context_t *ctx);
typedef uint32_t (*cyboot_flash_row_op_t)(uint32_t addr, uint32_t *data,
        cyboot_flash_context_t *ctx);

typedef struct {
    uint32_t reserved[PSOC_C3_ROM_SKIP];
    cyboot_flash_erase_row_t erase_row;
    cyboot_flash_row_op_t program_row;
    cyboot_flash_row_op_t write_row;
} psoc_c3_rom_flash_t;

#define PSOC_C3_ROM ((const psoc_c3_rom_flash_t *)PSOC_C3_ROM_FUNC_ADDR)

/* The ROM write path updates the refresh bookkeeping through ctx->refresh even
 * when the refresh feature is unused, and it writes one entry per sector. A
 * NULL pointer or an undersized array here corrupts whatever follows it. */
static cyboot_flash_refresh_t flash_refresh[PSOC_C3_FLASH_SECTORS];
static cyboot_flash_context_t flash_ctx;
/* Row staging buffer: too large for the stack under -Wstack-usage. */
static uint32_t flash_row[PSOC_C3_FLASH_ROW_WORDS];
static int flash_rom_checked;
static int flash_rom_ok;
static int peri_init_done;

/* Group 0 is enabled out of reset; the rest are not, and a slave in a gated
 * group bus-faults on first access. Release the slave reset before enabling,
 * the order the vendor startup uses. Slaves that do not exist read back as
 * zero, so writing all ones stays correct whatever a variant populates. */
void psoc_c3_peri_init(void)
{
    int g;

    if (peri_init_done)
        return;
    peri_init_done = 1;

    for (g = 1; g < PERI_GR_COUNT; g++) {
        PERI_GR_SL_CTL2(g) = 0;
        PERI_GR_SL_CTL(g) = 0xFFFFFFFFUL;
    }
    DSB();
}

/* A pin is unusable until its bit is cleared here: while it is marked
 * non-secure the port's HSIOM and GPIO registers discard secure writes and
 * read back zero. */
void psoc_c3_pin_setup(int port, int pin, uint32_t sel, uint32_t drive)
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

static int RAMFUNCTION rom_ptr_valid(void *fn)
{
    uint32_t a = (uint32_t)fn;
    return ((a >= PSOC_C3_ROM_BASE) &&
            (a < (PSOC_C3_ROM_BASE + PSOC_C3_ROM_SIZE)) &&
            ((a & 1U) != 0U));
}

/* Validated on first use, not from hal_init(): the test application links this
 * HAL without __WOLFBOOT and never calls hal_init(), so gating flash on
 * hal_init() state made every application write fail silently -- which is the
 * path wolfBoot_update_trigger() takes. */
static int RAMFUNCTION flash_rom_ready(void)
{
    if (flash_rom_checked == 0) {
        flash_rom_checked = 1;
        flash_rom_ok = (rom_ptr_valid((void *)PSOC_C3_ROM->erase_row) &&
                        rom_ptr_valid((void *)PSOC_C3_ROM->program_row) &&
                        rom_ptr_valid((void *)PSOC_C3_ROM->write_row));
    }
    return flash_rom_ok;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

/* A len that is not a whole number of rows erases the row the final byte falls
 * in, rounding up. wolfBoot only ever passes sector multiples. */
int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    uint32_t status;

    if ((len <= 0) || (flash_rom_ready() == 0))
        return -1;
    if ((address % PSOC_C3_FLASH_ROW_SIZE) != 0)
        return -1;

    while (len > 0) {
        flash_ctx.flags = CYBOOT_FLAGS_BLOCKING;
        flash_ctx.refresh = flash_refresh;
        status = PSOC_C3_ROM->erase_row(PSOC_C3_SBUS_ALIAS(address),
                    &flash_ctx);
        if (status != CYBOOT_FLASH_SUCCESS)
            return -1;
        address += PSOC_C3_FLASH_ROW_SIZE;
        len -= (int)PSOC_C3_FLASH_ROW_SIZE;
    }
    return 0;
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    uint32_t row_start;
    uint32_t offset;
    uint32_t chunk;
    uint32_t status;

    if ((data == NULL) || (len <= 0) || (flash_rom_ready() == 0))
        return -1;

    while (len > 0) {
        row_start = address & ~(PSOC_C3_FLASH_ROW_SIZE - 1U);
        offset = address - row_start;
        chunk = PSOC_C3_FLASH_ROW_SIZE - offset;
        if (chunk > (uint32_t)len)
            chunk = (uint32_t)len;

        /* write_row erases before programming, so carry over the rest of the
         * row. */
        memcpy(flash_row, (const void *)row_start, PSOC_C3_FLASH_ROW_SIZE);
        memcpy((uint8_t *)flash_row + offset, data, chunk);
        /* The ROM fills the column-33 trailer itself for a blocking write;
         * clear it so no bytes of the previous row are carried into this one. */
        memset((uint8_t *)flash_row + PSOC_C3_FLASH_ROW_SIZE, 0,
                PSOC_C3_FLASH_COL33_SZ);

        flash_ctx.flags = CYBOOT_FLAGS_BLOCKING;
        flash_ctx.refresh = flash_refresh;
        status = PSOC_C3_ROM->write_row(PSOC_C3_SBUS_ALIAS(row_start),
                    flash_row, &flash_ctx);
        if (status != CYBOOT_FLASH_SUCCESS)
            return -1;

        address += chunk;
        data += chunk;
        len -= (int)chunk;
    }
    return 0;
}

/* Program a peripheral clock destination and start its divider.
 *
 * The divider's EN bit is status, not control: only DIV_CMD starts it. Touching
 * an SCB before its divider runs bus-faults, so callers must not proceed on a
 * failure here. Divider types are not uniform across groups, and a destination
 * programmed with a type its group does not provide silently produces no clock.
 */
int psoc_c3_pclk_setup(uint32_t gr, uint32_t idx, uint32_t div, uint32_t type,
        uint32_t pclk_hz, uint32_t target_hz)
{
    uint32_t timeout = PSOC_C3_PERI_TIMEOUT;
    uint32_t int_div;
    uint32_t frac_div;

    if (target_hz == 0U)
        return -1;

    if (type == PERI_PCLK_DIV_TYPE_16_5) {
        psoc_c3_div16_5(pclk_hz, target_hz, &int_div, &frac_div);
        PERI_PCLK_DIV_16_5_CTL(gr, div) =
            (int_div << PERI_PCLK_DIV_16_5_INT_Pos) |
            (frac_div << PERI_PCLK_DIV_16_5_FRAC_Pos);
    }
    else {
        int_div = psoc_c3_div16(pclk_hz, target_hz);
        PERI_PCLK_DIV_16_CTL(gr, div) =
            ((int_div - 1U) << PERI_PCLK_DIV_INT16_Pos);
    }

    PERI_PCLK_CLOCK_CTL(gr, idx) = (type << PERI_PCLK_CLOCK_CTL_TYPE_Pos) | div;
    PERI_PCLK_DIV_CMD(gr) = PERI_PCLK_DIV_CMD_ENABLE |
        PERI_PCLK_DIV_CMD_PA_NONE |
        (type << PERI_PCLK_DIV_CMD_TYPE_Pos) | div;

    while (timeout > 0) {
        if (type == PERI_PCLK_DIV_TYPE_16_5) {
            if ((PERI_PCLK_DIV_16_5_CTL(gr, div) & PERI_PCLK_DIV_16_5_EN) != 0)
                return 0;
        }
        else if ((PERI_PCLK_DIV_16_CTL(gr, div) & PERI_PCLK_DIV_EN) != 0) {
            return 0;
        }
        timeout--;
    }
    return -1;
}

#if defined(DEBUG_UART) || !defined(__WOLFBOOT)

static void uart_pins_setup(void)
{
    psoc_c3_pin_setup(PSOC_C3_UART_PORT, PSOC_C3_UART_RX_PIN,
            PSOC_C3_UART_HSIOM_SEL, GPIO_CFG_DM_HIGHZ);
    psoc_c3_pin_setup(PSOC_C3_UART_PORT, PSOC_C3_UART_TX_PIN,
            PSOC_C3_UART_HSIOM_SEL, GPIO_CFG_DM_STRONG);
}

/* Set once the console SCB has a running clock; every register access is
 * gated on it. */
static int uart_ready;

static int uart_clock_setup(void)
{
    return psoc_c3_pclk_setup(PSOC_C3_UART_PCLK_GR, PSOC_C3_UART_PCLK_IDX,
            PSOC_C3_UART_PCLK_DIV, PERI_PCLK_DIV_TYPE_16_5, PSOC_C3_PCLK_HZ,
            PSOC_C3_UART_BAUD * PSOC_C3_UART_OVS);
}

void uart_init(void)
{
    psoc_c3_peri_init();
    uart_pins_setup();
    /* An SCB without a running divider bus-faults on first access, so a
     * failed clock leaves the console silent rather than taking the fault.
     * uart_write() checks the same flag, since the caller cannot. */
    uart_ready = 0;
    if (uart_clock_setup() != 0)
        return;

    SCB_CTRL(PSOC_C3_UART_SCB_BASE) = 0;
    SCB_UART_CTRL(PSOC_C3_UART_SCB_BASE) = 0;         /* standard UART */
    SCB_UART_TX_CTRL(PSOC_C3_UART_SCB_BASE) = 1;      /* one stop bit */
    SCB_UART_RX_CTRL(PSOC_C3_UART_SCB_BASE) = 1;
    SCB_TX_CTRL(PSOC_C3_UART_SCB_BASE) = SCB_DATA_WIDTH(8);
    SCB_RX_CTRL(PSOC_C3_UART_SCB_BASE) = SCB_DATA_WIDTH(8);
    SCB_TX_FIFO_CTRL(PSOC_C3_UART_SCB_BASE) = 0x3F;
    SCB_RX_FIFO_CTRL(PSOC_C3_UART_SCB_BASE) = 0x3F;
    SCB_CTRL(PSOC_C3_UART_SCB_BASE) = SCB_CTRL_ENABLED | SCB_CTRL_MODE_UART |
        ((PSOC_C3_UART_OVS - 1UL) & SCB_CTRL_OVS_MASK);
    uart_ready = 1;
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;
    uint32_t timeout;

    if (!uart_ready)
        return;
    for (i = 0; i < sz; i++) {
        /* USED is the low field; bits 16 and up are the FIFO pointers. */
        timeout = PSOC_C3_PERI_TIMEOUT;
        while (((SCB_TX_FIFO_STATUS(PSOC_C3_UART_SCB_BASE) &
                 SCB_TX_FIFO_USED_Msk) >= SCB_TX_FIFO_SAFE_DEPTH) &&
               (timeout > 0))
            timeout--;
        if (timeout == 0)
            return;     /* console is stuck; drop the rest rather than hang */
        SCB_TX_FIFO_WR(PSOC_C3_UART_SCB_BASE) = (uint32_t)(uint8_t)buf[i];
    }
}

#endif /* DEBUG_UART || !__WOLFBOOT */

#ifdef __WOLFBOOT

#ifdef WOLFBOOT_TPM
/* Where the TPM's interface-select strap lands. On the evaluation kit it
 * arrives on mikroBUS INT, which is P7.7. */
#ifndef PSOC_C3_TPM_SEL_PORT
#define PSOC_C3_TPM_SEL_PORT    7
#endif
#ifndef PSOC_C3_TPM_SEL_PIN
#define PSOC_C3_TPM_SEL_PIN     7
#endif

/* The TPM latches its bus from the CONFIG/PIRQ# strap while its reset is low:
 * high selects SPI, low selects I2C. On the evaluation kit that strap is
 * mikroBUS INT (P7.7), and the carrier's reset comes from XRES through an RC,
 * so the TPM leaves reset about a millisecond after the CPU and this lands
 * inside the window. The level is held afterwards because PIRQ# shares the
 * pin and this port polls rather than using the interrupt. */
static void tpm_iface_select(void)
{
    psoc_c3_pin_setup(PSOC_C3_TPM_SEL_PORT, PSOC_C3_TPM_SEL_PIN, 0,
            GPIO_CFG_DM_STRONG);
#ifdef WOLFBOOT_TPM_I2C
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) &= ~(1UL << PSOC_C3_TPM_SEL_PIN);
#else
    GPIO_PRT_OUT(PSOC_C3_TPM_SEL_PORT) |= (1UL << PSOC_C3_TPM_SEL_PIN);
#endif
}
#endif

void hal_init(void)
{
    psoc_c3_peri_init();
#ifdef WOLFBOOT_TPM
    tpm_iface_select();
#endif
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", sizeof("wolfBoot HAL Init\n") - 1);
    if (flash_rom_ready() == 0) {
        uart_write("BootROM flash table invalid\n",
                sizeof("BootROM flash table invalid\n") - 1);
    }
#endif
}


void hal_prepare_boot(void)
{
#ifdef DEBUG_UART
    uint32_t timeout = PSOC_C3_PERI_TIMEOUT;

    if (!uart_ready)
        return;
    /* Drain the shift register before the application touches the SCB. A
     * stuck console must not hold up the handoff. */
    while (((SCB_TX_FIFO_STATUS(PSOC_C3_UART_SCB_BASE) &
             (SCB_TX_FIFO_USED_Msk | SCB_TX_FIFO_SR_VALID_Msk)) != 0U) &&
           (timeout > 0))
        timeout--;
#endif
}

#endif /* __WOLFBOOT */
