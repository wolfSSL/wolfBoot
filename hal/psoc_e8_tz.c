/* psoc_e8_tz.c
 *
 * TrustZone setup for Infineon PSOC Edge E8 (CAT1D, Cortex-M33).
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

/* The SAU decides how the core sees an address and the memory protection
 * controllers decide what the bus allows per block; both are programmed. The
 * CM33 runs Secure and Non-secure code in the same protection context (PC2),
 * so the controllers are configured for PC2 and the TrustZone NS attribute
 * alone separates the two worlds. Peripherals stay Secure; the Non-secure
 * application reaches the console through a gateway. */

#include <stdint.h>
#include "image.h"
#include "hal.h"
#include "wolfboot/wc_secure.h"
#include "printf.h"
#include "hal/psoc_e8.h"
#include <target.h>

#if defined(TZEN) && !defined(NONSECURE_APP)

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
#include <arm_cmse.h>
#endif

/* Extended boot uses SAU regions 0 and 1 (Non-secure peripheral and SFLASH
 * windows) and leaves the SAU enabled. */
#define SAU_REGION_BOOT_NS  2
#define SAU_REGION_SRAM_NS  3
#define SAU_REGION_NSC      4

/* The application runs from the boot partition and uses SRAM1. */
#define NS_SRAM_BASE        (0x24080000UL)
#define NS_SRAM_SIZE        (0x00080000UL)
#define NS_BOOT_OFFSET      (WOLFBOOT_PARTITION_BOOT_ADDRESS & \
                             (PSOC_E8_SMIF_WINDOW - 1U))

static void sau_region(uint32_t region, uint32_t start, uint32_t end,
        uint32_t att)
{
    SAU_RNR = region;
    SAU_RBAR = start & ~0x1FUL;
    SAU_RLAR = (end & ~0x1FUL) | att | SAU_RLAR_ENABLE;
}

/* Grant PC2 non-secure read/write over [offset, offset + len) of one MPC,
 * through its ROT view. A range that is not block aligned is not opened at
 * all, rather than rounded out over Secure neighbours. Each LUT word holds
 * eight 4-bit block entries, and AUTO_INC would move BLK_IDX between the read
 * and the write of one update, so it is held off meanwhile. */
static void mpc_set_ns(uint32_t base, uint32_t offset, uint32_t len)
{
    uint32_t blk = 1UL << ((MPC_ROT_BLK_CFG(base) &
                            MPC_ROT_BLK_CFG_SIZE_Msk) + 5U);
    uint32_t nblk = (MPC_ROT_BLK_MAX(base) + 1UL) * MPC_ROT_BLKS_PER_IDX;
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;
    uint32_t first = offset / blk;
    uint32_t last = (offset + len - 1U) / blk;
    uint32_t ctrl;
    uint32_t idx;
    uint32_t lut;
    uint32_t b;

    if ((len == 0U) || ((offset % blk) != 0U) || ((len % blk) != 0U) ||
        (last >= nblk))
        return;
    while (((MPC_ROT_BLK_CFG(base) & MPC_ROT_BLK_CFG_INIT_BUSY) != 0U) &&
           (timeout > 0U))
        timeout--;
    if (timeout == 0U)
        return;

    ctrl = MPC_ROT_CTRL(base);
    MPC_ROT_CTRL(base) = ctrl & ~MPC_ROT_CTRL_AUTO_INC;
    MPC_ROT_BLK_PC(base) = PSOC_E8_PC_CM33;
    for (idx = first / MPC_ROT_BLKS_PER_IDX;
         idx <= last / MPC_ROT_BLKS_PER_IDX; idx++) {
        MPC_ROT_BLK_IDX(base) = idx;
        lut = MPC_ROT_BLK_LUT(base);
        for (b = 0; b < MPC_ROT_BLKS_PER_IDX; b++) {
            uint32_t n = (idx * MPC_ROT_BLKS_PER_IDX) + b;
            if ((n >= first) && (n <= last)) {
                lut &= ~(0xFUL << (b * 4U));
                lut |= (MPC_ROT_NS_RW << (b * 4U));
            }
        }
        MPC_ROT_BLK_LUT(base) = lut;
    }
    MPC_ROT_CTRL(base) = ctrl;
    DSB();
}

/* Memory attribution. Named for the generic wolfBoot hook. */
void hal_gtzc_init(void)
{
    /* Only the boot partition is handed over: the application executes from
     * it. wolfBoot reads it through the same Non-secure alias, so this runs
     * before the image is verified. Update and swap stay Secure. */
    mpc_set_ns(SMIF0_AHB_MPC_BASE, NS_BOOT_OFFSET, WOLFBOOT_PARTITION_SIZE);
    mpc_set_ns(RAMC1_MPC_BASE, 0, NS_SRAM_SIZE);
}

void hal_tz_sau_init(void)
{
    uint32_t boot = WOLFBOOT_PARTITION_BOOT_ADDRESS;

    sau_region(SAU_REGION_BOOT_NS, boot, boot + WOLFBOOT_PARTITION_SIZE - 1U,
            0);
    sau_region(SAU_REGION_SRAM_NS, NS_SRAM_BASE,
            NS_SRAM_BASE + NS_SRAM_SIZE - 1U, 0);
    /* Without NSC an SG instruction is not a valid entry point and the first
     * call takes a SecureFault (SFSR.INVEP). */
    sau_region(SAU_REGION_NSC, WOLFBOOT_NSC_ADDRESS,
            WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1U, SAU_RLAR_NSC);
    SAU_CTRL |= SAU_CTRL_ENABLE;
    DSB();
    ISB();
}

void hal_tz_sau_ns_region(void)
{
    hal_tz_sau_init();
}

/* The crypto block's TRNG seeds the Secure DRBG. Both health monitors run and
 * a failure in either stops the generator, which is reported as an error. */
void hal_trng_init(void)
{
    CRYPTO_CTL |= CRYPTO_CTL_ENABLED;
    CRYPTO_TR_MON_CMD = 0;
    CRYPTO_TR_CTL1 = 0;
    CRYPTO_INTR_CLR(CRYPTO_INTR_TR_ALL);
    CRYPTO_TR_GARO_CTL = CRYPTO_TR_POLY;
    CRYPTO_TR_FIRO_CTL = CRYPTO_TR_POLY;
    CRYPTO_TR_CTL0 = CRYPTO_TR_CTL0_DEFAULT;
    CRYPTO_TR_CTL1 = CRYPTO_TR_CTL1_ALL_RO;
    CRYPTO_TR_MON_CTL = CRYPTO_TR_MON_BS_TR;
    CRYPTO_TR_MON_RC_CTL = CRYPTO_TR_RC_CUTOFF;
    CRYPTO_TR_MON_AP_CTL = CRYPTO_TR_AP_CTL;
    CRYPTO_TR_MON_CMD = CRYPTO_TR_MON_START_AP_RC;
}

void hal_trng_fini(void)
{
    CRYPTO_TR_MON_CMD = 0;
    CRYPTO_TR_CTL1 = 0;
}

static int trng_word(volatile uint32_t *word)
{
    uint32_t timeout = PSOC_E8_PERI_TIMEOUT;

    while (((CRYPTO_STATUS != 0U) ||
            ((CRYPTO_INTR & CRYPTO_INTR_TR_INITIALIZED) == 0U)) &&
           (timeout > 0U))
        timeout--;
    if (timeout == 0U)
        return -1;
    CRYPTO_INTR_CLR(CRYPTO_INTR_TR_DATA_AVAILABLE);
    CRYPTO_TR_CTL2 = 32U;
    CRYPTO_TR_CMD = 1U;
    timeout = PSOC_E8_PERI_TIMEOUT;
    while (((CRYPTO_INTR & (CRYPTO_INTR_TR_DATA_AVAILABLE |
             CRYPTO_INTR_TR_AP_ERROR | CRYPTO_INTR_TR_RC_ERROR)) == 0U) &&
           (timeout > 0U))
        timeout--;
    if ((timeout == 0U) ||
        ((CRYPTO_INTR & (CRYPTO_INTR_TR_AP_ERROR | CRYPTO_INTR_TR_RC_ERROR))
            != 0U))
        return -1;
    *word = CRYPTO_TR_RESULT;
    return 0;
}

int hal_trng_get_entropy(unsigned char *out, unsigned len)
{
    volatile uint32_t word = 0;
    unsigned i;

    for (i = 0; i < len; i++) {
        if ((i & 3U) == 0U) {
            if (trng_word(&word) != 0) {
                word = 0;
                return -1;
            }
        }
        out[i] = (unsigned char)(word >> ((i & 3U) * 8U));
    }
    /* Raw entropy seeds the DRBG: do not leave it on the stack. */
    word = 0;
    return 0;
}

/* The console stays Secure; the Non-secure application prints through this
 * gateway. The buffer must lie in Non-secure memory, or a caller could have
 * the Secure world print Secure data back. */
CSME_NSE_API
void psoc_e8_nsc_uart_write(const char *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U) || (len > PSOC_E8_NSC_UART_MAX))
        return;
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    if (cmse_check_address_range((void *)buf, (size_t)len, CMSE_NONSECURE)
            == NULL)
        return;
#endif
#ifdef DEBUG_UART
    uart_write(buf, (unsigned int)len);
#endif
}

#endif /* TZEN && !NONSECURE_APP */
