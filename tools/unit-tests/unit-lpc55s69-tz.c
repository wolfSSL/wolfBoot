/* unit-lpc55s69-tz.c
 *
 * Unit tests for the LPC55S69 secure-world HAL in hal/lpc55s69.c: the SAU and
 * AHB secure controller attribution, the locks, the hand-over to the
 * non-secure application, the boot timer and the RNG entropy source.
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

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TZEN
#define WOLFCRYPT_SECURE_MODE
#define __WOLFBOOT
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
#define BOOT_BENCHMARK
/* Replace hal/armv8m_tz.h with a recording SAU */
#define TZ_INCLUDED

#define LPC55S69_SAU_REGIONS 8

struct sau_region {
    uint32_t start;
    uint32_t end;
    int nsc;
    int set;
};

static struct sau_region sau[LPC55S69_SAU_REGIONS];
static int sau_bad_region;
static uint32_t mock_sau_ctrl;
static uint32_t mock_shcsr;

#define SAU_CTRL                 mock_sau_ctrl
#define SAU_INIT_CTRL_ENABLE     (1U << 0)
#define SCB_SHCSR                mock_shcsr
#define SCB_SHCSR_SECUREFAULT_EN (1U << 19)

static void sau_init_region(uint32_t region, uint32_t start_addr,
        uint32_t end_addr, int secure)
{
    if (region >= LPC55S69_SAU_REGIONS) {
        sau_bad_region = 1;
        return;
    }
    sau[region].start = start_addr;
    sau[region].end = end_addr;
    sau[region].nsc = secure;
    sau[region].set = 1;
}

/* lpc55s69-tz.config layout, in place of target.h */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x200
#define WOLFBOOT_PARTITION_SIZE              0x20000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x00040000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x00060000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x00081000
#define WOLFBOOT_NSC_ADDRESS                 0x1003E000
#define WOLFBOOT_NSC_SIZE                    0x2000
#define ARCH_FLASH_OFFSET                    0x0
#define KEYVAULT_ADDRESS                     0x10082000U
#define KEYVAULT_SIZE                        0x16000U
#define LPC55S69_KEYVAULT_ADDR               KEYVAULT_ADDRESS
#define LPC55S69_KEYVAULT_LEN                KEYVAULT_SIZE

#include "fsl_device_registers.h"

static AHB_SECURE_CTRL_Type mock_ahbsc;
static SYSCON_Type mock_syscon;
static IOCON_Type mock_iocon;
RNG_Type mock_rng;
SYSCON_Type *mock_syscon_regs(void);

/* Before fsl_common.h: the inline clock gates in fsl_clock.h expand these */
#undef AHB_SECURE_CTRL
#undef SYSCON
#undef IOCON
#undef RNG
#define AHB_SECURE_CTRL (&mock_ahbsc)
#define SYSCON          (mock_syscon_regs())
#define IOCON           (&mock_iocon)
#define RNG             (&mock_rng)

#include "fsl_common.h"
#include "fsl_iap.h"
#include "../../hal/lpc55s69.h"
#include "fsl_iap_ffr.h"
#include "fsl_rng.h"

uint32_t mock_primask;
int mock_barriers;
NVIC_Type mock_nvic;
SysTick_Type mock_systick;

/* Clock gates turned on through the write-only AHBCLKCTRLSET registers */
static uint32_t clocks_on[SYSCON_AHBCLKCTRLSET_COUNT];
static int attach_calls;
static clock_attach_id_t attach_id;
static int rng_init_calls;
static int rng_read_calls;
static int rng_resets;
static status_t rng_read_ret;

SYSCON_Type *mock_syscon_regs(void)
{
    uint32_t i;

    for (i = 0; i < SYSCON_AHBCLKCTRLSET_COUNT; i++) {
        clocks_on[i] |= mock_syscon.AHBCLKCTRLSET[i];
        mock_syscon.AHBCLKCTRLSET[i] = 0;
    }
    return &mock_syscon;
}

static int clock_on(clock_ip_name_t clk)
{
    (void)mock_syscon_regs();
    return (clocks_on[CLK_GATE_ABSTRACT_REG_OFFSET(clk)] &
            (1UL << CLK_GATE_ABSTRACT_BITS_SHIFT(clk))) != 0U;
}

#define PAGE        LPC55S69_FLASH_PAGE_SIZE
/* 0: erased and never programmed */
static uint8_t programmed[LPC55S69_FLASH_SIZE / LPC55S69_FLASH_PAGE_SIZE];
static int program_calls;

status_t FLASH_Init(flash_config_t *config)
{
    config->PFlashTotalSize = LPC55S69_FLASH_SIZE;
    return kStatus_FLASH_Success;
}

status_t FFR_Init(flash_config_t *config)
{
    (void)config;
    return kStatus_Success;
}

status_t FLASH_VerifyErase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes)
{
    (void)config;
    ck_assert_uint_eq(lengthInBytes, PAGE);
    ck_assert_uint_lt(start, LPC55S69_FLASH_SIZE);
    return programmed[start / PAGE] ? kStatus_FLASH_CommandFailure :
        kStatus_FLASH_Success;
}

status_t FLASH_Erase(flash_config_t *config, uint32_t start,
        uint32_t lengthInBytes, uint32_t key)
{
    (void)config;
    (void)start;
    (void)lengthInBytes;
    (void)key;
    ck_abort_msg("unexpected flash erase");
    return kStatus_Fail;
}

status_t FLASH_Program(flash_config_t *config, uint32_t start,
        const uint8_t *src, uint32_t lengthInBytes)
{
    (void)config;
    (void)src;
    ck_assert_uint_eq(lengthInBytes, PAGE);
    ck_assert_uint_lt(start, LPC55S69_FLASH_SIZE);
    ck_assert_msg(!programmed[start / PAGE], "page 0x%x programmed twice",
            start);
    programmed[start / PAGE] = 1;
    program_calls++;
    return kStatus_FLASH_Success;
}

void BOARD_BootClockFROHF96M(void)
{
}

int wolfCrypt_Init(void)
{
    return 0;
}

void CLOCK_AttachClk(clock_attach_id_t connection)
{
    attach_calls++;
    attach_id = connection;
}

void RESET_PeripheralReset(reset_ip_name_t peripheral)
{
    if (peripheral == kRNG_RST_SHIFT_RSTn)
        rng_resets++;
}

void RNG_Init(RNG_Type *base)
{
    ck_assert_ptr_eq(base, &mock_rng);
    rng_init_calls++;
}

status_t RNG_GetRandomData(RNG_Type *base, void *data, size_t dataSize)
{
    ck_assert_ptr_eq(base, &mock_rng);
    rng_read_calls++;
    memset(data, 0xA5, dataSize);
    return rng_read_ret;
}

#include "../../hal/lpc55s69.c"

#define AHBSC_S        0x33333333U
#define AHBSC_RULE(reg, n) (((reg) >> ((n) * 4U)) & 0x3U)

static int sau_covers(uint32_t addr, int nsc)
{
    int i;

    for (i = 0; i < LPC55S69_SAU_REGIONS; i++) {
        if (sau[i].set && (sau[i].nsc == nsc) &&
                (addr >= sau[i].start) && (addr <= sau[i].end))
            return 1;
    }
    return 0;
}

static void setup(void)
{
    memset(sau, 0, sizeof(sau));
    memset(&mock_nvic, 0, sizeof(mock_nvic));
    memset(&mock_ahbsc, 0, sizeof(mock_ahbsc));
    memset(&mock_syscon, 0, sizeof(mock_syscon));
    memset(&mock_iocon, 0, sizeof(mock_iocon));
    memset(&mock_systick, 0, sizeof(mock_systick));
    memset(clocks_on, 0, sizeof(clocks_on));
    /* Reset values of the lock and control registers */
    mock_ahbsc.MISC_CTRL_REG = 0x0000AAAAU;
    mock_ahbsc.MISC_CTRL_DP_REG = 0x0000AAAAU;
    mock_ahbsc.CPU0_LOCK_REG = 0x800002AAU;
    sau_bad_region = 0;
    mock_sau_ctrl = 0;
    mock_shcsr = 0;
    memset(programmed, 0, sizeof(programmed));
    /* A keyvault page that already holds data */
    programmed[(KEYVAULT_ADDRESS - 0x10000000U + PAGE) / PAGE] = 1;
    program_calls = 0;
    flash_ready = 0;
    hal_init();
    hal_trng_fini();
    attach_calls = 0;
    rng_init_calls = rng_read_calls = rng_resets = 0;
    rng_read_ret = kStatus_Success;
}

START_TEST(test_init_fills_keyvault_pages)
{
    uint32_t off;
    uint32_t kv = KEYVAULT_ADDRESS - 0x10000000U;

    ck_assert_int_eq(flash_ready, 1);
    for (off = kv; off < kv + KEYVAULT_SIZE; off += PAGE)
        ck_assert_msg(programmed[off / PAGE], "keyvault page 0x%x", off);
    for (off = 0; off < WOLFBOOT_PARTITION_BOOT_ADDRESS; off += PAGE)
        ck_assert_msg(programmed[off / PAGE], "bootloader page 0x%x", off);
    /* The HWPUF record is left alone; the existing vault page kept */
    ck_assert(!programmed[0x80000U / PAGE]);
    ck_assert_int_eq(program_calls, (int)((WOLFBOOT_PARTITION_BOOT_ADDRESS +
            2U * WOLFBOOT_PARTITION_SIZE + WOLFBOOT_SECTOR_SIZE +
            KEYVAULT_SIZE) / PAGE) - 1);
}
END_TEST

START_TEST(test_sau_enabled_and_well_formed)
{
    int i;

    ck_assert_int_eq(sau_bad_region, 0);
    ck_assert_uint_ne(mock_sau_ctrl & SAU_INIT_CTRL_ENABLE, 0);
    ck_assert_uint_ne(mock_shcsr & SCB_SHCSR_SECUREFAULT_EN, 0);
    for (i = 0; i < LPC55S69_SAU_REGIONS; i++) {
        if (!sau[i].set)
            continue;
        ck_assert_uint_le(sau[i].start, sau[i].end);
        ck_assert_uint_eq(sau[i].start & 0x1FU, 0);
        ck_assert_uint_eq(sau[i].end & 0x1FU, 0x1FU);
    }
}
END_TEST

START_TEST(test_nsc_region_is_veneers_only)
{
    int i;
    int nsc_regions = 0;

    for (i = 0; i < LPC55S69_SAU_REGIONS; i++) {
        if (sau[i].set && sau[i].nsc) {
            nsc_regions++;
            ck_assert_uint_eq(sau[i].start, WOLFBOOT_NSC_ADDRESS);
            ck_assert_uint_eq(sau[i].end,
                    WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1);
        }
    }
    ck_assert_int_eq(nsc_regions, 1);
}
END_TEST

START_TEST(test_application_resources_non_secure)
{
    static const uint32_t ns_addrs[] = {
        WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1,
        0x20020000, 0x2002FFFF,     /* SRAM2 */
        0x40086000, 0x40086FFF,     /* FLEXCOMM0 (console) */
        0x4008C000, 0x4008FFFF      /* GPIO */
    };
    unsigned int i;

    for (i = 0; i < sizeof(ns_addrs) / sizeof(ns_addrs[0]); i++)
        ck_assert_msg(sau_covers(ns_addrs[i], 0), "0x%08x not non-secure",
                ns_addrs[i]);
}
END_TEST

START_TEST(test_secure_resources_not_non_secure)
{
    static const uint32_t s_addrs[] = {
        0x00000000, 0x10000000,     /* wolfBoot */
        WOLFBOOT_NSC_ADDRESS,
        WOLFBOOT_NSC_ADDRESS & 0x0FFFFFFFU,
        WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1,
        WOLFBOOT_PARTITION_SWAP_ADDRESS,
        0x00080000,                 /* HWPUF provisioning record */
        KEYVAULT_ADDRESS, KEYVAULT_ADDRESS & 0x0FFFFFFFU,
        KEYVAULT_ADDRESS + KEYVAULT_SIZE - 1,
        0x20000000, 0x2001FFFF,     /* SRAM0-1, wolfBoot */
        0x20030000, 0x20040000,     /* SRAM3, SRAM4 */
        0x30000000, 0x30020000,     /* secure SRAM alias */
        0x40000000, 0x40001000,     /* SYSCON, IOCON */
        0x40020000,                 /* PMC */
        0x40034000, 0x40035000,     /* FLASH controller, PRINCE */
        0x4003A000, 0x4003B000,     /* RNG, PUF */
        0x40082000,                 /* DMA0 */
        0x40085FFF, 0x40087000,     /* around FLEXCOMM0 */
        0x400A4000, 0x400A5000,     /* HASHCRYPT, CASPER */
        0x400A7000, 0x400A8000,     /* DMA1, SECGPIO */
        0x400AC000,                 /* AHB_SECURE_CTRL */
        0x40100000,                 /* USB SRAM */
        0x50086000, 0x5008C000      /* secure aliases of the NS peripherals */
    };
    unsigned int i;

    for (i = 0; i < sizeof(s_addrs) / sizeof(s_addrs[0]); i++)
        ck_assert_msg(!sau_covers(s_addrs[i], 0), "0x%08x is non-secure",
                s_addrs[i]);
}
END_TEST

START_TEST(test_only_console_irq_targets_non_secure)
{
    unsigned int i;

    ck_assert_uint_eq(mock_nvic.ITNS[0], 1UL << FLEXCOMM0_IRQn);
    for (i = 1; i < sizeof(mock_nvic.ITNS) / sizeof(mock_nvic.ITNS[0]); i++)
        ck_assert_uint_eq(mock_nvic.ITNS[i], 0);
}
END_TEST

START_TEST(test_ahbsc_flash_rules_match_boot_partition)
{
    uint32_t region;
    uint32_t start;
    uint32_t rule;
    int ns;

    for (region = 0; region < 24U; region++) {
        start = region * LPC55S69_AHBSC_FLASH_REGION;
        ns = (start >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (start < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE);
        rule = AHBSC_RULE(mock_ahbsc.SEC_CTRL_FLASH_ROM[0].
                SEC_CTRL_FLASH_MEM_RULE[region / 8U], region % 8U);
        ck_assert_msg(rule == (ns ? 0U : 3U), "flash 0x%05x rule %u", start,
                rule);
    }
    ck_assert_uint_eq(AHBSC_RULE(mock_ahbsc.SEC_CTRL_FLASH_ROM[0].SLAVE_RULE,
                0), 0);
    ck_assert_uint_eq(AHBSC_RULE(mock_ahbsc.SEC_CTRL_FLASH_ROM[0].SLAVE_RULE,
                1), 3);
    for (region = 0; region < 4U; region++)
        ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_FLASH_ROM[0].
                SEC_CTRL_ROM_MEM_RULE[region], AHBSC_S);
}
END_TEST

START_TEST(test_ahbsc_sram_rules)
{
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM2[0].SLAVE_RULE, 0);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM2[0].MEM_RULE[0], 0);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM2[0].MEM_RULE[1], 0);

    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAMX[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAMX[0].MEM_RULE[0], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM0[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM0[0].MEM_RULE[0], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM0[0].MEM_RULE[1], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM1[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM1[0].MEM_RULE[0], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM1[0].MEM_RULE[1], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM3[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM3[0].MEM_RULE[0], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM3[0].MEM_RULE[1], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM4[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_RAM4[0].MEM_RULE[0], AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_USB_HS[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_USB_HS[0].MEM_RULE[0], AHBSC_S);
}
END_TEST

START_TEST(test_ahbsc_peripheral_rules)
{
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].SLAVE_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE0_MEM_CTRL0, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE0_MEM_CTRL1, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE0_MEM_CTRL2, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE1_MEM_CTRL0, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE1_MEM_CTRL1, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE1_MEM_CTRL2, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_APB_BRIDGE[0].
            SEC_CTRL_APB_BRIDGE1_MEM_CTRL3, AHBSC_S);

    /* Only FLEXCOMM0 and the GPIO block are open to the application */
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT8_SLAVE0_RULE, AHBSC_S &
            ~AHB_SECURE_CTRL_SEC_CTRL_AHB_PORT8_SLAVE0_RULE_FLEXCOMM0_RULE_MASK);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT8_SLAVE1_RULE, AHBSC_S &
            ~AHB_SECURE_CTRL_SEC_CTRL_AHB_PORT8_SLAVE1_RULE_GPIO0_RULE_MASK);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT9_SLAVE0_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT9_SLAVE1_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT10[0].SLAVE0_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT10[0].SLAVE1_RULE, AHBSC_S);
    ck_assert_uint_eq(mock_ahbsc.SEC_CTRL_AHB_PORT10[0].
            SEC_CTRL_AHB_SEC_CTRL_MEM_RULE[0], AHBSC_S);
}
END_TEST

START_TEST(test_ahbsc_checking_enabled_and_locked)
{
    const uint32_t checking =
        AHB_SECURE_CTRL_MISC_CTRL_REG_ENABLE_SECURE_CHECKING_MASK;
    const uint32_t lock = AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK_MASK;

    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_REG & checking,
            AHB_SECURE_CTRL_MISC_CTRL_REG_ENABLE_SECURE_CHECKING(1U));
    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_DP_REG & checking,
            AHB_SECURE_CTRL_MISC_CTRL_REG_ENABLE_SECURE_CHECKING(1U));
    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_REG & lock,
            AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK(2U));
    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_DP_REG & lock,
            AHB_SECURE_CTRL_MISC_CTRL_REG_WRITE_LOCK(1U));
    /* Every other control keeps its reset value */
    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_REG & ~(checking | lock),
            0x0000AAAAU & ~(checking | lock));
    ck_assert_uint_eq(mock_ahbsc.MISC_CTRL_DP_REG & ~(checking | lock),
            0x0000AAAAU & ~(checking | lock));
}
END_TEST

START_TEST(test_sau_write_locked)
{
    const uint32_t locks = AHB_SECURE_CTRL_CPU0_LOCK_REG_LOCK_SAU_MASK |
        AHB_SECURE_CTRL_CPU0_LOCK_REG_CPU0_LOCK_REG_LOCK_MASK;

    ck_assert_uint_eq(mock_ahbsc.CPU0_LOCK_REG & locks,
            AHB_SECURE_CTRL_CPU0_LOCK_REG_LOCK_SAU(1U) |
            AHB_SECURE_CTRL_CPU0_LOCK_REG_CPU0_LOCK_REG_LOCK(1U));
    /* wolfBoot still sets the non-secure VTOR before the jump */
    ck_assert_uint_eq(mock_ahbsc.CPU0_LOCK_REG & ~locks, 0x800002AAU & ~locks);
}
END_TEST

START_TEST(test_prepare_boot_hands_over_leds)
{
    const uint32_t mux = IOCON_FUNC0 | IOCON_MODE_PULLUP | IOCON_DIGITAL_EN;

    mock_systick.CTRL = SysTick_CTRL_ENABLE_Msk;
    hal_prepare_boot();
    ck_assert_uint_eq(mock_iocon.PIO[LPC55S69_LED_PORT][LPC55S69_LED_BLUE_PIN],
            mux);
    ck_assert_uint_eq(mock_iocon.PIO[LPC55S69_LED_PORT][LPC55S69_LED_RED_PIN],
            mux);
    ck_assert_uint_eq(
            mock_iocon.PIO[LPC55S69_LED_PORT][LPC55S69_LED_GREEN_PIN], mux);
    ck_assert(clock_on(kCLOCK_Iocon));
    ck_assert(clock_on(kCLOCK_Gpio1));
    ck_assert_uint_eq(mock_systick.CTRL, 0);
}
END_TEST

START_TEST(test_benchmark_timer_counts_fro1m_ticks)
{
    ck_assert_uint_eq(hal_get_timer_us(), 0);
    ck_assert_uint_ne(mock_syscon.CLOCK_CTRL &
            SYSCON_CLOCK_CTRL_FRO1MHZ_CLK_ENA_MASK, 0);
    ck_assert_int_eq(attach_calls, 1);
    ck_assert_int_eq(attach_id, kFRO1M_to_SYSTICK0);
    ck_assert_uint_eq(mock_systick.LOAD, SysTick_LOAD_RELOAD_Msk);
    ck_assert_uint_eq(mock_systick.CTRL, SysTick_CTRL_ENABLE_Msk);

    /* Down-counter wraps from 0 to the 24-bit reload value */
    mock_systick.VAL = SysTick_LOAD_RELOAD_Msk - 99U;
    ck_assert_uint_eq(hal_get_timer_us(), 100);
    mock_systick.VAL -= 400U;
    ck_assert_uint_eq(hal_get_timer_us(), 500);
    ck_assert_int_eq(attach_calls, 1);
}
END_TEST

START_TEST(test_trng_needs_init)
{
    unsigned char buf[8];

    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(rng_read_calls, 0);
}
END_TEST

START_TEST(test_trng_init_and_read)
{
    unsigned char buf[8];
    unsigned int i;

    memset(buf, 0, sizeof(buf));
    hal_trng_init();
    ck_assert_int_eq(rng_init_calls, 1);
    ck_assert_int_eq(rng_resets, 1);
    ck_assert(clock_on(kCLOCK_Rng));
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), 0);
    ck_assert_int_eq(rng_read_calls, 1);
    for (i = 0; i < sizeof(buf); i++)
        ck_assert_uint_eq(buf[i], 0xA5);
    ck_assert_int_eq(hal_trng_get_entropy(NULL, 4), -1);
    ck_assert_int_eq(rng_read_calls, 1);
}
END_TEST

START_TEST(test_trng_read_failure_and_fini)
{
    unsigned char buf[8];

    hal_trng_init();
    rng_read_ret = kStatus_Fail;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    hal_trng_fini();
    rng_read_ret = kStatus_Success;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(rng_read_calls, 1);
}
END_TEST

static Suite *lpc55s69_tz_suite(void)
{
    Suite *s = suite_create("lpc55s69-tz");
    TCase *tc = tcase_create("lpc55s69-tz");

    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_init_fills_keyvault_pages);
    tcase_add_test(tc, test_sau_enabled_and_well_formed);
    tcase_add_test(tc, test_nsc_region_is_veneers_only);
    tcase_add_test(tc, test_application_resources_non_secure);
    tcase_add_test(tc, test_secure_resources_not_non_secure);
    tcase_add_test(tc, test_only_console_irq_targets_non_secure);
    tcase_add_test(tc, test_ahbsc_flash_rules_match_boot_partition);
    tcase_add_test(tc, test_ahbsc_sram_rules);
    tcase_add_test(tc, test_ahbsc_peripheral_rules);
    tcase_add_test(tc, test_ahbsc_checking_enabled_and_locked);
    tcase_add_test(tc, test_sau_write_locked);
    tcase_add_test(tc, test_prepare_boot_hands_over_leds);
    tcase_add_test(tc, test_benchmark_timer_counts_fro1m_ticks);
    tcase_add_test(tc, test_trng_needs_init);
    tcase_add_test(tc, test_trng_init_and_read);
    tcase_add_test(tc, test_trng_read_failure_and_fini);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = lpc55s69_tz_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
