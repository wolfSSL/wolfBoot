/* ddr_cadence.c
 *
 * Generic Cadence DDR controller driver: controller CSR programming, the
 * Memory Test Controller engine, and the LPDDR4 mode-register protocol.
 * Split from the PolarFire SoC HAL; the PHY, PLL and training stay in the
 * platform, which composes these calls.
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
#include "ddr_cadence.h"

void ddr_cadence_controller_setup(const ddr_cadence_reg_t *regs,
    unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; i++) {
        DDRC_REG(regs[i].off) = regs[i].val;
    }
    ddrc_mb();
}

uint8_t ddr_cadence_mtc_test(uint8_t mask, uint64_t start_address,
    uint32_t size, uint8_t data_pattern, uint8_t add_pattern)
{
    uint32_t timeout;
    uint32_t mask0, mask1, mask2, mask3, mask4;

    /* Configure common memory test interface */
    DDRC_REG(MT_STOP_ON_ERROR) = 0U;
    DDRC_REG(MT_EN_SINGLE) = 0U;
    DDRC_REG(MT_DATA_PATTERN) = (uint32_t)data_pattern;
    DDRC_REG(MT_ADDR_PATTERN) =
        (add_pattern == DDR_CADENCE_MTC_ADD_RANDOM) ? 1U : 0U;

    /* Set start address and size (number of addresses = 2^size) */
    if (add_pattern != DDR_CADENCE_MTC_ADD_RANDOM) {
        DDRC_REG(MT_START_ADDR_0) = (uint32_t)(start_address & 0xFFFFFFFFUL);
        DDRC_REG(MT_START_ADDR_1) = (uint32_t)(start_address >> 32);
    } else {
        DDRC_REG(MT_START_ADDR_0) = 0U;
        DDRC_REG(MT_START_ADDR_1) = 0U;
    }
    DDRC_REG(MT_ADDR_BITS) = size;

    /* Configure per-lane error masks.  Default to all errors masked, then
     * unmask the bits belonging to each lane in `mask`.  Per-lane bit
     * positions taken verbatim from HSS mss_ddr.c:3652-3691. */
    mask0 = 0xFFFFFFFFU;
    mask1 = 0xFFFFFFFFU;
    mask2 = 0xFFFFFFFFU;
    mask3 = 0xFFFFFFFFU;
    mask4 = 0xFFFFFFFFU;
    if (mask & 0x01U) {
        mask0 &= 0xFFFFFF00U; mask1 &= 0xFFFFF00FU;
        mask2 &= 0xFFFF00FFU; mask3 &= 0xFFF00FFFU;
    }
    if (mask & 0x02U) {
        mask0 &= 0xFFFF00FFU; mask1 &= 0xFFF00FFFU;
        mask2 &= 0xFF00FFFFU; mask3 &= 0xF00FFFFFU;
    }
    if (mask & 0x04U) {
        mask0 &= 0xFF00FFFFU; mask1 &= 0xF00FFFFFU;
        mask2 &= 0x00FFFFFFU; mask3 &= 0x0FFFFFFFU;
        mask4 &= 0xFFFFFFF0U;
    }
    if (mask & 0x08U) {
        mask0 &= 0x00FFFFFFU; mask1 &= 0x0FFFFFFFU;
        mask2 &= 0xFFFFFFF0U; mask3 &= 0xFFFFFF00U;
        mask4 &= 0xFFFFF00FU;
    }
    if (mask & 0x10U) {
        mask1 &= 0xFFFFFFF0U; mask2 &= 0xFFFFFF0FU;
        mask3 &= 0xFFFFF0FFU; mask4 &= 0xFFFF0FFFU;
    }
    DDRC_REG(MT_ERROR_MASK_0) = mask0;
    DDRC_REG(MT_ERROR_MASK_1) = mask1;
    DDRC_REG(MT_ERROR_MASK_2) = mask2;
    DDRC_REG(MT_ERROR_MASK_3) = mask3;
    DDRC_REG(MT_ERROR_MASK_4) = mask4;

    /* Fire the test (toggle MT_EN_SINGLE 0 -> 1) */
    DDRC_REG(MT_EN) = 0U;
    DDRC_REG(MT_EN_SINGLE) = 0U;
    DDRC_REG(MT_EN_SINGLE) = 1U;
    ddrc_mb();

    /* Poll MT_DONE_ACK with the same HSS timeout (0xFFFFFF). */
    timeout = 0xFFFFFFU;
    while ((DDRC_REG(MT_DONE_ACK) & 0x01U) == 0U) {
        if (timeout-- == 0U)
            return DDR_CADENCE_MTC_TIMEOUT_ERROR;
    }

    return (uint8_t)(DDRC_REG(MT_ERROR_STS) & 0x01U);
}

uint32_t ddr_cadence_mr_masked_write(uint32_t address)
{
    uint32_t ack;

    DDRC_REG(MC_INIT_CS)         = 0x1UL;
    DDRC_REG(MC_INIT_MR_WR_MASK) = 0xFFFFFUL;
    DDRC_REG(MC_INIT_MR_ADDR)    = address;
    DDRC_REG(MC_INIT_MR_WR_DATA) = 0x0UL;
    DDRC_REG(MC_INIT_MR_W_REQ)   = 0x1UL;
    DDRC_REG(MC_INIT_MR_W_REQ)   = 0x0UL;
    ddr_cadence_udelay(5);

    ack = DDRC_REG(MC_INIT_ACK);
    if (ack != 0U) {
        return 0U;
    }
    return 1U;
}

uint32_t ddr_cadence_mr_unmasked_write(uint32_t address, uint32_t data)
{
    uint32_t ack;

    DDRC_REG(MC_INIT_CS)         = 0x1UL;
    DDRC_REG(MC_INIT_MR_WR_MASK) = 0x0UL;     /* unmasked: write all bits */
    DDRC_REG(MC_INIT_MR_ADDR)    = address;
    DDRC_REG(MC_INIT_MR_WR_DATA) = data;
    DDRC_REG(MC_INIT_MR_W_REQ)   = 0x1UL;
    DDRC_REG(MC_INIT_MR_W_REQ)   = 0x0UL;
    ddr_cadence_udelay(5);

    ack = DDRC_REG(MC_INIT_ACK);
    if (ack != 0U) {
        return 0U;
    }
    return 1U;
}

/* Issue the masked MR write 10 times: the LPDDR4 init sequence repeats it to
 * guarantee the register lands (matches the pre-split HSS-derived loop).
 * Accumulates: returns nonzero if ANY of the 10 attempts did not ACK. */
uint32_t ddr_cadence_mr_masked_write_x10(uint32_t address)
{
    uint32_t i;
    uint32_t error = 0U;

    for (i = 0U; i < 10U; i++) {
        error |= ddr_cadence_mr_masked_write(address);
    }
    return error;
}
