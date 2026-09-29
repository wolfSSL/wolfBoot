/* psoc_c3.h
 *
 * Register definitions for Infineon PSOC Control C3 (CAT1B, Cortex-M33).
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
#ifndef PSOC_C3_H_INCLUDED
#define PSOC_C3_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

/* The memory clobber matters as much as the instruction: without it the
 * compiler may reorder accesses around the barrier. The divider helpers below
 * are shared with a host-built unit test, which cannot assemble the ARM
 * instructions, so there the compiler barrier alone stands in. */
#if defined(__arm__) || defined(__ARM_ARCH)
#define DMB() __asm__ volatile ("dmb" ::: "memory")
#define ISB() __asm__ volatile ("isb" ::: "memory")
#define DSB() __asm__ volatile ("dsb" ::: "memory")
#else
#define DMB() __asm__ volatile ("" ::: "memory")
#define ISB() __asm__ volatile ("" ::: "memory")
#define DSB() __asm__ volatile ("" ::: "memory")
#endif

/* Every memory is visible through four aliases: Secure/Non-secure crossed with
 * CBUS (code) and SBUS (system). Code executes from the CBUS view; the BootROM
 * flash API only accepts SBUS addresses, hence PSOC_C3_SBUS_ALIAS below. */
#define PSOC_C3_SBUS_OFFSET     (0x20000000UL)
#define PSOC_C3_SBUS_ALIAS(a)   (((uint32_t)(a)) | PSOC_C3_SBUS_OFFSET)

#define PSOC_C3_FLASH_ROW_SIZE  (512U)
/* Each row carries 16 bytes of "column 33" metadata alongside its data. */
#define PSOC_C3_FLASH_COL33_SZ  (16U)
#define PSOC_C3_FLASH_ROW_WORDS ((PSOC_C3_FLASH_ROW_SIZE + \
                                  PSOC_C3_FLASH_COL33_SZ) / sizeof(uint32_t))

/* Peripheral blocks. The non-secure view is listed; wolfBoot runs in the
 * Secure state, so PSOC_C3_PERI_OFFSET shifts every access to the secure
 * alias. Set PSOC_C3_PERI_SECURE=0 for a non-secure build. */
#ifndef PSOC_C3_PERI_SECURE
#define PSOC_C3_PERI_SECURE 1
#endif
#if PSOC_C3_PERI_SECURE
#define PSOC_C3_PERI_OFFSET (0x10000000UL)
#else
#define PSOC_C3_PERI_OFFSET (0UL)
#endif

#define PERI_BASE       (0x42000000UL + PSOC_C3_PERI_OFFSET)
#define PERI_PCLK_BASE  (0x42040000UL + PSOC_C3_PERI_OFFSET)
#define HSIOM_BASE      (0x42400000UL + PSOC_C3_PERI_OFFSET)
#define GPIO_BASE       (0x42410000UL + PSOC_C3_PERI_OFFSET)

/* PERI groups gate their slave peripherals. Out of reset most groups are
 * disabled, and touching a peripheral in a disabled group bus-faults. */
#define PERI_GR_COUNT           6
#define PERI_GR_SL_CTL(g) \
    (*(volatile uint32_t *)(PERI_BASE + 0x4000UL + \
                            ((uint32_t)(g) * 0x40UL) + 0x10UL))
/* SL_CTL2 holds each slave in reset; it must be cleared before SL_CTL
 * enables the slave, and the group's clock root must already be running or
 * the slave never completes its clock handshake. */
#define PERI_GR_SL_CTL2(g) \
    (*(volatile uint32_t *)(PERI_BASE + 0x4000UL + \
                            ((uint32_t)(g) * 0x40UL) + 0x14UL))


/* PERI_PCLK: 16 groups of 0x2000. Within a group the divider control blocks
 * are indexed by divider number and CLOCK_CTL by peripheral number. */
#define PERI_PCLK_GR(g)         (PERI_PCLK_BASE + ((uint32_t)(g) * 0x2000UL))
#define PERI_PCLK_DIV_CMD(g)    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0x000UL))
#define PERI_PCLK_CLOCK_CTL(g,i)  \
    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0xC00UL + ((uint32_t)(i) * 4UL)))
#define PERI_PCLK_DIV_16_5_CTL(g,d) \
    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0x1800UL + ((uint32_t)(d) * 4UL)))
#define PERI_PCLK_DIV_16_CTL(g,d) \
    (*(volatile uint32_t *)(PERI_PCLK_GR(g) + 0x1400UL + ((uint32_t)(d) * 4UL)))
#define PERI_PCLK_DIV_INT16_Pos     (8UL)
#define PERI_PCLK_DIV_EN            (1UL << 0)

/* The EN bit in a divider's control register is status only. A divider is
 * turned on through DIV_CMD, which also handles phase alignment. */
#define PERI_PCLK_DIV_CMD_ENABLE    (1UL << 31)
#define PERI_PCLK_DIV_CMD_PA_TYPE_Pos (24U)
#define PERI_PCLK_DIV_CMD_PA_DIV_Pos  (16U)
#define PERI_PCLK_DIV_CMD_TYPE_Pos    (8U)
/* PA_DIV_SEL 0xFF with PA_TYPE_SEL 3 means "no phase alignment". */
#define PERI_PCLK_DIV_CMD_PA_NONE \
    ((3UL << PERI_PCLK_DIV_CMD_PA_TYPE_Pos) | \
     (0xFFUL << PERI_PCLK_DIV_CMD_PA_DIV_Pos))
#define PERI_PCLK_DIV_TYPE_16       (1UL)
#define PERI_PCLK_DIV_TYPE_16_5     (2UL)
#define PERI_PCLK_DIV_16_5_EN       (1UL << 0)
/* Bound on the divider-enable handshake, so a misrouted clock index reports
 * rather than spinning forever. */
#ifndef PSOC_C3_PERI_TIMEOUT
#define PSOC_C3_PERI_TIMEOUT        (1000000UL)
#endif
#define PERI_PCLK_DIV_16_5_FRAC_Pos (3U)
#define PERI_PCLK_DIV_16_5_INT_Pos  (8U)
/* CLOCK_CTL selects a divider: type 2 is the 16.5 (fractional) divider. */
#define PERI_PCLK_CLOCK_CTL_TYPE_Pos (8U)

/* HSIOM: one 0x10 block per port, PORT_SEL0 covers pins 0-3, SEL1 pins 4-7,
 * one byte of mux select per pin. */
/* Pins 0-3 live in PORT_SEL0 and pins 4-7 in PORT_SEL1, one byte each. */
#define HSIOM_PORT_SEL(p, pin) \
    (*(volatile uint32_t *)(HSIOM_BASE + ((uint32_t)(p) * 0x10UL) + \
                            (((uint32_t)(pin) < 4U) ? 0x00UL : 0x04UL)))
#define HSIOM_SEL_SHIFT(pin)    ((((uint32_t)(pin)) & 3U) * 8U)

/* Per-port security attribution for the pins, one 0x10 block per port with a
 * bit per pin: 1 marks the pin non-secure. The BootROM leaves the pins it used
 * marked non-secure, and while a pin is non-secure the port's HSIOM and GPIO
 * registers ignore secure writes and read back as zero. Clear the bit for
 * every pin before configuring it. */
#define HSIOM_SECURE_PRT_NSMASK(p) \
    (*(volatile uint32_t *)(HSIOM_BASE + 0x1000UL + ((uint32_t)(p) * 0x10UL)))

/* GPIO: one 0x80 block per port. CFG holds 4 bits of drive mode per pin. */
#define GPIO_PRT_OUT(p) \
    (*(volatile uint32_t *)(GPIO_BASE + ((uint32_t)(p) * 0x80UL) + 0x00UL))
#define GPIO_PRT_CFG(p) \
    (*(volatile uint32_t *)(GPIO_BASE + ((uint32_t)(p) * 0x80UL) + 0x44UL))

#define GPIO_CFG_DM_HIGHZ       (0x8UL)  /* input buffer on, no drive */
#define GPIO_CFG_DM_STRONG      (0x6UL)  /* strong drive, input buffer off */
#define GPIO_CFG_DM_OD_LOW      (0xCUL)  /* open drain drives low, input on */
#define GPIO_CFG_DM_MASK        (0xFUL)

/* SCB instances. SCB4 and SCB5 are absent on the smaller packages. */
#define SCB0_BASE (0x42820000UL + PSOC_C3_PERI_OFFSET)
#define SCB1_BASE (0x42840000UL + PSOC_C3_PERI_OFFSET)
#define SCB2_BASE (0x42850000UL + PSOC_C3_PERI_OFFSET)
#define SCB3_BASE (0x42860000UL + PSOC_C3_PERI_OFFSET)
#define SCB4_BASE (0x42870000UL + PSOC_C3_PERI_OFFSET)
#define SCB5_BASE (0x42C00000UL + PSOC_C3_PERI_OFFSET)

/* Select an SCB by instance number so the console and SPI peripherals are a
 * build option rather than an edit. Folds to a constant for a literal n. */
#define PSOC_C3_SCB_BASE(n) \
    ((n) == 0 ? SCB0_BASE : (n) == 1 ? SCB1_BASE : \
     (n) == 2 ? SCB2_BASE : (n) == 3 ? SCB3_BASE : \
     (n) == 4 ? SCB4_BASE : SCB5_BASE)

#define SCB_CTRL(b)          (*(volatile uint32_t *)((b) + 0x000UL))
#define SCB_SPI_CTRL(b)      (*(volatile uint32_t *)((b) + 0x020UL))
#define SCB_UART_CTRL(b)     (*(volatile uint32_t *)((b) + 0x040UL))
#define SCB_UART_TX_CTRL(b)  (*(volatile uint32_t *)((b) + 0x044UL))
#define SCB_UART_RX_CTRL(b)  (*(volatile uint32_t *)((b) + 0x048UL))
#define SCB_TX_CTRL(b)       (*(volatile uint32_t *)((b) + 0x200UL))
#define SCB_TX_FIFO_CTRL(b)  (*(volatile uint32_t *)((b) + 0x204UL))
#define SCB_TX_FIFO_STATUS(b) (*(volatile uint32_t *)((b) + 0x208UL))
#define SCB_TX_FIFO_WR(b)    (*(volatile uint32_t *)((b) + 0x240UL))
#define SCB_RX_CTRL(b)       (*(volatile uint32_t *)((b) + 0x300UL))
#define SCB_RX_FIFO_CTRL(b)  (*(volatile uint32_t *)((b) + 0x304UL))
#define SCB_RX_FIFO_STATUS(b) (*(volatile uint32_t *)((b) + 0x308UL))
#define SCB_RX_FIFO_RD(b)    (*(volatile uint32_t *)((b) + 0x340UL))

#define SCB_CTRL_ENABLED     (1UL << 31)
#define SCB_CTRL_MODE_I2C    (0UL << 24)
#define SCB_CTRL_MODE_SPI    (1UL << 24)
#define SCB_CTRL_MODE_UART   (2UL << 24)
#define SCB_CTRL_OVS_MASK    (0xFUL)
/* In I2C mode the low and high clock phases are counted separately. */
#define SCB_CTRL_OVS_HIGH_Pos (4UL)
/* Data width fields are encoded as (bits - 1). */
#define SCB_DATA_WIDTH(n)    ((uint32_t)((n) - 1))

#define SCB_TX_FIFO_USED_Msk     (0x1FFUL)
#define SCB_TX_FIFO_SR_VALID_Msk (1UL << 15)
#define SCB_RX_FIFO_USED_Msk     (0x1FFUL)

/* I2C master. INTR_M flags are write-one-to-clear. */
#define SCB_I2C_CTRL(b)      (*(volatile uint32_t *)((b) + 0x60UL))
#define SCB_I2C_STATUS(b)    (*(volatile uint32_t *)((b) + 0x64UL))
#define SCB_I2C_M_CMD(b)     (*(volatile uint32_t *)((b) + 0x68UL))
#define SCB_INTR_M(b)        (*(volatile uint32_t *)((b) + 0xF00UL))
#define SCB_INTR_M_MASK(b)   (*(volatile uint32_t *)((b) + 0xF08UL))
#define SCB_I2C_CFG(b)       (*(volatile uint32_t *)((b) + 0x70UL))
#define SCB_I2C_CTRL_MASTER_MODE  (1UL << 31)
#define SCB_I2C_CTRL_HIGH_PHASE_Pos (0UL)
#define SCB_I2C_CTRL_LOW_PHASE_Pos  (4UL)
/* Filter trims the vendor driver programs. Bit 1 of SDA_IN_FILT_TRIM is the
 * SCB clock enable (Cypress ID 282226), so leaving this register at reset
 * leaves the block unclocked and no transfer ever starts. */
#define SCB_I2C_CFG_DEFAULT  (0x002A1013UL)
/* I2C is fixed at 8 data bits, MSB first, and the transmitter must be open
 * drain on both lines. */
/* Bit order. SPI NOR and the TCG PTP SPI TPM are both MSB first, and I2C is
 * MSB first by definition, so every mode this HAL supports sets it. */
#define SCB_CTRL_MSB_FIRST   (1UL << 8)
#define SCB_I2C_RX_CTRL_VAL  (SCB_DATA_WIDTH(8) | (1UL << 8))
#define SCB_I2C_TX_CTRL_VAL  (SCB_DATA_WIDTH(8) | (1UL << 8) | \
                              (1UL << 16) | (1UL << 17))
#define SCB_I2C_STATUS_BUS_BUSY   (1UL << 0)
#define SCB_I2C_M_CMD_M_START     (1UL << 0)
#define SCB_I2C_M_CMD_M_ACK       (1UL << 2)
#define SCB_I2C_M_CMD_M_NACK      (1UL << 3)
#define SCB_I2C_M_CMD_M_STOP      (1UL << 4)
#define SCB_INTR_M_I2C_ARB_LOST   (1UL << 0)
#define SCB_INTR_M_I2C_NACK       (1UL << 1)
#define SCB_INTR_M_I2C_ACK        (1UL << 2)
#define SCB_INTR_M_I2C_STOP       (1UL << 4)
#define SCB_INTR_M_I2C_BUS_ERROR  (1UL << 8)
#define SCB_INTR_M_ALL            (0x0000011FUL)
/* Stay well inside the smallest FIFO any SCB instance provides. */
#define SCB_TX_FIFO_SAFE_DEPTH   (8UL)

#define SCB_SPI_CTRL_MASTER      (1UL << 31)
#define SCB_SPI_CTRL_CPHA        (1UL << 2)
#define SCB_SPI_CTRL_CPOL        (1UL << 3)

/* Compute the 16.5 fractional divider fields for a target frequency. The
 * divider produces pclk / (int_div + 1 + frac_div/32), so the whole divisor is
 * computed in 32nds to keep the fractional part. Kept in the header so it can
 * be exercised by a host unit test. */
static inline void psoc_c3_div16_5(uint32_t pclk_hz, uint32_t target_hz,
        uint32_t *int_div, uint32_t *frac_div)
{
    uint32_t scaled;

    if ((target_hz == 0U) || (int_div == NULL) || (frac_div == NULL))
        return;

    scaled = (uint32_t)(((uint64_t)pclk_hz * 32U) / target_hz);
    if (scaled < 32U)
        scaled = 32U;                   /* divisor of 1: cannot go faster */
    if (scaled > (0x10000UL * 32U))
        scaled = 0x10000UL * 32U;       /* INT is 16 bits */
    *int_div = (scaled / 32U) - 1U;
    *frac_div = scaled % 32U;
}

/* Integer divider form. The register holds the divisor less one, so this
 * returns the divisor itself. Kept in the header beside the fractional form
 * so both can be exercised by a host unit test.
 *
 * This one drives I2C, whose configured rate is a ceiling rather than a
 * target, so the divisor rounds up: rounding to nearest would pick the
 * faster side of a non-integral ratio and run the bus out of spec. */
static inline uint32_t psoc_c3_div16(uint32_t pclk_hz, uint32_t target_hz)
{
    uint32_t int_div;

    if (target_hz == 0U)
        return 1U;
    int_div = (pclk_hz + target_hz - 1U) / target_hz;
    if (int_div == 0U)
        int_div = 1U;
    if (int_div > 0x10000U)             /* INT16_DIV holds divisor - 1 */
        int_div = 0x10000U;
    return int_div;
}

/* Ungate the peripheral groups. Idempotent, and required before any SCB is
 * touched: a slave in a gated group bus-faults on first access. */
/* Program a peripheral clock destination and start its divider. Returns 0 on
 * success; a caller must not touch the peripheral if this fails, because an
 * SCB without a running divider bus-faults on first access. */
int psoc_c3_pclk_setup(uint32_t gr, uint32_t idx, uint32_t div, uint32_t type,
        uint32_t pclk_hz, uint32_t target_hz);

void psoc_c3_peri_init(void);

/* Claim a pin as secure, route it to an HSIOM function and set its drive
 * mode. Pins must be claimed before HSIOM or GPIO will accept a write. */
void psoc_c3_pin_setup(int port, int pin, uint32_t sel, uint32_t drive);

#endif /* PSOC_C3_H_INCLUDED */
