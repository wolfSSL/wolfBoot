/* unit-mcxw-tz.c
 *
 * Unit tests for the TrustZone side of hal/mcxw.c: the SAU attribution map,
 * interrupt routing, the peripherals handed to the non-secure application,
 * the console, the benchmark timer and the S200 entropy source, built against
 * the NXP MCUXpresso SDK headers.
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

#include "unit-mmio.h"

/* MCX W layout (config/examples/mcxw-tz.config) */
#define H_TARGETS_TARGET_
#define WOLFBOOT_FIXED_PARTITIONS
#define WOLFBOOT_SECTOR_SIZE                 0x2000
#define WOLFBOOT_PARTITION_SIZE              0x58000
#define WOLFBOOT_PARTITION_BOOT_ADDRESS      0x32000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x8A000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS      0xE2000
#define WOLFBOOT_NSC_ADDRESS                 0x30000
#define WOLFBOOT_NSC_SIZE                    0x2000
#define WOLFBOOT_KEYVAULT_ADDRESS            0xE4000
#define WOLFBOOT_KEYVAULT_SIZE               0x18000

#define TZEN
#define __WOLFBOOT
#define DEBUG_UART
#define BOOT_BENCHMARK
#define WOLFCRYPT_SECURE_MODE
#define WOLFBOOT_HASH_SHA256
#define WOLFBOOT_SIGN_ECC256
/* Replace hal/armv8m_tz.h with a recording SAU */
#define TZ_INCLUDED

#define MCXW_SAU_REGIONS 8

struct sau_region {
    uint32_t start;
    uint32_t end;
    int nsc;
    int set;
};

static struct sau_region sau[MCXW_SAU_REGIONS];
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
    if (region >= MCXW_SAU_REGIONS) {
        sau_bad_region = 1;
        return;
    }
    sau[region].start = start_addr;
    sau[region].end = end_addr;
    sau[region].nsc = secure;
    sau[region].set = 1;
}

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_port.h"
#include "fsl_lpuart.h"
#include "fsl_elemu.h"
#include "fsl_sscp_mu.h"
#include "fsl_sss_sscp.h"

static FMU_Type fmu;
static SMSCM_Type smscm;
static MCM_Type mcm;
GPIO_Type mock_gpioa;
PORT_Type mock_porta;
PORT_Type mock_portc;
LPUART_Type mock_lpuart1;
TSTMR_Type mock_tstmr0;
ELEMU_Type mock_elemua;

/* Point the SDK peripheral instances at the mocks above. The MRCC clock
 * gates are absolute addresses in clock_ip_name_t, so main() maps that page */
#undef FMU0
#undef SMSCM
#undef MCM
#undef GPIOA
#undef PORTA
#undef PORTC
#undef LPUART1
#undef TSTMR0
#undef ELEMUA
#define FMU0    (&fmu)
#define SMSCM   (&smscm)
#define MCM     (&mcm)
#define GPIOA   (&mock_gpioa)
#define PORTA   (&mock_porta)
#define PORTC   (&mock_portc)
#define LPUART1 (&mock_lpuart1)
#define TSTMR0  (&mock_tstmr0)
#define ELEMUA  (&mock_elemua)

#include "../../hal/mcxw.c"

/* Physical STCM used by the secure image: hal/mcxw.ld RAM + RAM_HEAP */
#define SECURE_STCM_START 0x20000000U
#define SECURE_STCM_END   0x20015FFFU

NVIC_Type mock_nvic;
uint32_t mock_primask;
int mock_barriers;

static int boot_clock_calls;
static lpuart_config_t lpuart_cfg;
static int lpuart_init_calls;
static char uart_out[64];
static size_t uart_len;

static status_t ele_ready_ret;
static sscp_status_t sscp_init_ret;
static sss_status_t open_ret;
static sss_status_t rng_init_ret;
static sss_status_t rng_seed_ret;
static sss_status_t rng_read_ret;
static int session_open;
static int open_calls;
static int close_calls;
static int rng_live;
static int rng_free_calls;
static int rng_read_calls;

static volatile uint32_t *mrcc(clock_ip_name_t name)
{
    return (volatile uint32_t *)(uintptr_t)(uint32_t)name;
}

static uint32_t port_mux(PORT_Type *port, uint32_t pin)
{
    return (port->PCR[pin] & PORT_PCR_MUX_MASK) >> PORT_PCR_MUX_SHIFT;
}

uint32_t CLOCK_GetIpFreq(clock_ip_name_t name)
{
    ck_assert_int_eq(name, kCLOCK_Lpuart1);
    return 16000000U;
}

void BOARD_BootClockRUN(void)
{
    boot_clock_calls++;
}

void LPUART_GetDefaultConfig(lpuart_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->baudRate_Bps = 9600U;
}

status_t LPUART_Init(LPUART_Type *base, const lpuart_config_t *config,
    uint32_t srcClock_Hz)
{
    ck_assert_ptr_eq(base, &mock_lpuart1);
    ck_assert_uint_eq(srcClock_Hz, 16000000U);
    lpuart_cfg = *config;
    lpuart_init_calls++;
    return kStatus_Success;
}

status_t LPUART_WriteBlocking(LPUART_Type *base, const uint8_t *data,
    size_t length)
{
    ck_assert_ptr_eq(base, &mock_lpuart1);
    ck_assert_uint_le(uart_len + length, sizeof(uart_out));
    memcpy(uart_out + uart_len, data, length);
    uart_len += length;
    return kStatus_Success;
}

status_t ELEMU_mu_wait_for_ready(ELEMU_Type *mu, uint32_t wait)
{
    ck_assert_ptr_eq(mu, &mock_elemua);
    ck_assert_uint_ne(wait, 0);
    return ele_ready_ret;
}

sscp_status_t sscp_mu_init(sscp_context_t *context, ELEMU_Type *base)
{
    (void)context;
    ck_assert_ptr_eq(base, &mock_elemua);
    return sscp_init_ret;
}

sss_status_t sss_sscp_open_session(sss_sscp_session_t *session,
    uint32_t sessionId, sss_type_t subsystem, sscp_context_t *sscpctx)
{
    (void)session;
    (void)sessionId;
    (void)sscpctx;
    ck_assert_uint_eq(subsystem, kType_SSS_Ele200);
    ck_assert_int_eq(session_open, 0);
    open_calls++;
    if (open_ret == kStatus_SSS_Success) {
        session_open = 1;
    }
    return open_ret;
}

sss_status_t sss_sscp_close_session(sss_sscp_session_t *session)
{
    (void)session;
    ck_assert_int_eq(session_open, 1);
    ck_assert_int_eq(rng_live, 0);
    session_open = 0;
    close_calls++;
    return kStatus_SSS_Success;
}

sss_status_t sss_sscp_rng_context_init(sss_sscp_session_t *session,
    sss_sscp_rng_t *context, uint32_t rngTypeSpecifier)
{
    (void)session;
    (void)context;
    ck_assert_int_eq(session_open, 1);
    ck_assert_uint_eq(rngTypeSpecifier, MCXW_ELE_RNG_HIGH_QUALITY);
    if (rng_init_ret == kStatus_SSS_Success) {
        rng_live = 1;
    }
    return rng_init_ret;
}

sss_status_t sss_sscp_rng_get_random(sss_sscp_rng_t *context,
    uint8_t *random_data, size_t dataLen)
{
    (void)context;
    ck_assert_int_eq(rng_live, 1);
    rng_read_calls++;
    if (random_data == NULL) {
        ck_assert_uint_eq(dataLen, 0);
        return rng_seed_ret;
    }
    memset(random_data, 0x5A, dataLen);
    return rng_read_ret;
}

sss_status_t sss_sscp_rng_free(sss_sscp_rng_t *context)
{
    (void)context;
    ck_assert_int_eq(rng_live, 1);
    rng_live = 0;
    rng_free_calls++;
    return kStatus_SSS_Success;
}

static void reset_mocks(void)
{
    memset(sau, 0, sizeof(sau));
    sau_bad_region = 0;
    mock_sau_ctrl = 0;
    mock_shcsr = 0;
    memset(&mock_nvic, 0, sizeof(mock_nvic));
    /* Gates present and off, as after reset */
    *mrcc(kCLOCK_GpioA) = MRCC_PR_MASK;
    *mrcc(kCLOCK_PortA) = MRCC_PR_MASK;
    *mrcc(kCLOCK_PortC) = MRCC_PR_MASK;
    *mrcc(kCLOCK_Lpuart1) = MRCC_PR_MASK;
    *mrcc(kCLOCK_Tstmr0) = MRCC_PR_MASK;
    memset(&mock_porta, 0, sizeof(mock_porta));
    memset(&mock_portc, 0, sizeof(mock_portc));
    memset(&mock_gpioa, 0, sizeof(mock_gpioa));
    boot_clock_calls = 0;
    lpuart_init_calls = 0;
    uart_len = 0;

    hal_trng_fini();
    ele_ready_ret = kStatus_Success;
    sscp_init_ret = kStatus_SSCP_Success;
    open_ret = kStatus_SSS_Success;
    rng_init_ret = kStatus_SSS_Success;
    rng_seed_ret = kStatus_SSS_Success;
    rng_read_ret = kStatus_SSS_Success;
    session_open = 0;
    open_calls = 0;
    close_calls = 0;
    rng_live = 0;
    rng_free_calls = 0;
    rng_read_calls = 0;
}

static int ns_covers(uint32_t addr)
{
    int i;

    for (i = 0; i < MCXW_SAU_REGIONS; i++) {
        if (sau[i].set && !sau[i].nsc &&
                addr >= sau[i].start && addr <= sau[i].end)
            return 1;
    }
    return 0;
}

START_TEST(test_sau_map)
{
    int i;

    reset_mocks();
    hal_init();

    ck_assert_int_eq(sau_bad_region, 0);
    ck_assert_int_eq(sau[0].set, 1);
    ck_assert_int_eq(sau[0].nsc, 1);
    ck_assert_uint_eq(sau[0].start, WOLFBOOT_NSC_ADDRESS);
    ck_assert_uint_eq(sau[0].end, WOLFBOOT_NSC_ADDRESS + WOLFBOOT_NSC_SIZE - 1);

    /* Only the BOOT partition is non-secure, not UPDATE or SWAP */
    ck_assert_uint_eq(sau[1].start, WOLFBOOT_PARTITION_BOOT_ADDRESS);
    ck_assert_uint_eq(sau[1].end,
        WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 1);
    ck_assert_uint_eq(sau[2].start, MCXW_NS_RAM_START);
    ck_assert_uint_eq(sau[2].end, MCXW_NS_RAM_END);
    ck_assert_uint_eq(sau[3].start, MCXW_UART_NS_START);
    ck_assert_uint_eq(sau[3].end, MCXW_UART_NS_END);
    ck_assert_uint_eq(sau[4].start, MCXW_GPIOA_NS_START);
    ck_assert_uint_eq(sau[4].end, MCXW_GPIOA_NS_END);
    for (i = 1; i < 5; i++)
        ck_assert_int_eq(sau[i].nsc, 0);
    for (i = 5; i < MCXW_SAU_REGIONS; i++)
        ck_assert_int_eq(sau[i].set, 0);

    ck_assert_uint_eq(mock_sau_ctrl & SAU_INIT_CTRL_ENABLE,
        SAU_INIT_CTRL_ENABLE);
    ck_assert_uint_eq(mock_shcsr & SCB_SHCSR_SECUREFAULT_EN,
        SCB_SHCSR_SECUREFAULT_EN);
}
END_TEST

START_TEST(test_sau_keeps_secure)
{
    static const uint32_t secure_addrs[] = {
        0x00000000U,                               /* wolfBoot */
        WOLFBOOT_NSC_ADDRESS - 1,
        WOLFBOOT_PARTITION_BOOT_ADDRESS - 1,
        WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        WOLFBOOT_PARTITION_SWAP_ADDRESS,
        WOLFBOOT_KEYVAULT_ADDRESS,
        WOLFBOOT_KEYVAULT_ADDRESS + WOLFBOOT_KEYVAULT_SIZE - 1,
        0x10000000U,                               /* secure flash alias */
        0x40015000U,                               /* SMSCM */
        0x4001C000U,                               /* MRCC */
        0x40020000U,                               /* FMU0 */
        0x40024000U,                               /* ELEMUA */
        0x40038000U,                               /* LPUART0 */
        0x40042000U,                               /* PORTA */
        0x40044000U,                               /* PORTC */
        0x48020000U,                               /* GPIOB */
        0x50020000U,                               /* FMU0 secure alias */
        0x30000000U,                               /* secure STCM */
    };
    uint32_t a;
    size_t i;

    reset_mocks();
    hal_init();
    for (i = 0; i < sizeof(secure_addrs) / sizeof(secure_addrs[0]); i++)
        ck_assert_msg(!ns_covers(secure_addrs[i]),
            "0x%08x is non-secure", secure_addrs[i]);
    /* The non-secure SRAM never aliases the secure image's STCM */
    for (a = SECURE_STCM_START; a <= SECURE_STCM_END; a += 0x100U)
        ck_assert_msg(!ns_covers(a), "STCM 0x%08x is non-secure", a);
    ck_assert(!ns_covers(SECURE_STCM_END));
}
END_TEST

START_TEST(test_irq_routing)
{
    int i;

    reset_mocks();
    hal_init();
    ck_assert_uint_eq(mock_nvic.ITNS[LPUART1_IRQn / 32],
        1U << (LPUART1_IRQn % 32));
    for (i = 0; i < 16; i++) {
        if (i != LPUART1_IRQn / 32)
            ck_assert_uint_eq(mock_nvic.ITNS[i], 0);
    }
}
END_TEST

START_TEST(test_prepare_boot_hands_over_leds_only)
{
    uint32_t pin;

    reset_mocks();
    mock_gpioa.PCNS = 0;
    hal_prepare_boot();
    ck_assert_uint_eq(mock_gpioa.PCNS, MCXW_LED_PINS_MASK);
    ck_assert_uint_eq(mock_gpioa.ICNS, 0);
    ck_assert_uint_ne(*mrcc(kCLOCK_GpioA) & MRCC_CC_MASK, 0);
    ck_assert_uint_ne(*mrcc(kCLOCK_PortA) & MRCC_CC_MASK, 0);
    for (pin = 0; pin < PORT_PCR_COUNT; pin++) {
        if ((MCXW_LED_PINS_MASK & (1U << pin)) != 0U)
            ck_assert_uint_eq(port_mux(&mock_porta, pin), kPORT_MuxAsGpio);
        else
            ck_assert_uint_eq(port_mux(&mock_porta, pin), 0);
    }
}
END_TEST

START_TEST(test_uart)
{
    reset_mocks();
    hal_init();
    ck_assert_int_eq(boot_clock_calls, 1);
    ck_assert_int_eq(lpuart_init_calls, 1);
    ck_assert_uint_eq(lpuart_cfg.baudRate_Bps, MCXW_UART_BAUD);
    ck_assert(lpuart_cfg.enableTx);
    ck_assert_uint_eq((*mrcc(kCLOCK_Lpuart1) & MRCC_MUX_MASK) >>
        MRCC_MUX_SHIFT, kCLOCK_IpSrcFro192M);
    ck_assert_uint_ne(*mrcc(kCLOCK_PortC) & MRCC_CC_MASK, 0);
    ck_assert_uint_eq(port_mux(&mock_portc, MCXW_UART_RX_PIN), kPORT_MuxAlt3);
    ck_assert_uint_eq(port_mux(&mock_portc, MCXW_UART_TX_PIN), kPORT_MuxAlt3);

    uart_write("a\nb\n", 4);
    ck_assert_uint_eq(uart_len, 6);
    ck_assert_mem_eq(uart_out, "a\r\nb\r\n", 6);
}
END_TEST

START_TEST(test_timer)
{
    reset_mocks();
    /* L and H are read-only in the SDK register struct */
    *(uint32_t *)&mock_tstmr0.H = 0x1;
    *(uint32_t *)&mock_tstmr0.L = 0x234;
    ck_assert_uint_eq(hal_get_timer_us(), 0x100000234ULL);
    ck_assert_uint_ne(*mrcc(kCLOCK_Tstmr0) & MRCC_CC_MASK, 0);
    *(uint32_t *)&mock_tstmr0.L = 0x300;
    ck_assert_uint_eq(hal_get_timer_us(), 0x100000300ULL);
}
END_TEST

START_TEST(test_trng_needs_init)
{
    unsigned char buf[8];

    reset_mocks();
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    ck_assert_int_eq(rng_read_calls, 0);
}
END_TEST

START_TEST(test_trng_init_and_read)
{
    unsigned char buf[8];

    reset_mocks();
    hal_trng_init();
    ck_assert_int_eq(open_calls, 1);
    ck_assert_int_eq(rng_read_calls, 1);
    memset(buf, 0, sizeof(buf));
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), 0);
    ck_assert_uint_eq(buf[0], 0x5A);
    ck_assert_uint_eq(buf[7], 0x5A);

    /* A second init keeps the open session */
    hal_trng_init();
    ck_assert_int_eq(open_calls, 1);

    rng_read_ret = kStatus_SSS_Fail;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    hal_trng_fini();
    ck_assert_int_eq(rng_free_calls, 1);
    ck_assert_int_eq(close_calls, 1);
    ck_assert_int_eq(session_open, 0);
    rng_read_ret = kStatus_SSS_Success;
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
    hal_trng_fini();
    ck_assert_int_eq(close_calls, 1);
}
END_TEST

START_TEST(test_trng_enclave_not_ready)
{
    unsigned char buf[4];

    reset_mocks();
    ele_ready_ret = kStatus_Fail;
    hal_trng_init();
    ck_assert_int_eq(open_calls, 0);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    reset_mocks();
    sscp_init_ret = kStatus_SSCP_Fail;
    hal_trng_init();
    ck_assert_int_eq(open_calls, 0);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    reset_mocks();
    open_ret = kStatus_SSS_Fail;
    hal_trng_init();
    ck_assert_int_eq(close_calls, 0);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);
}
END_TEST

START_TEST(test_trng_failures_release_session)
{
    unsigned char buf[4];

    reset_mocks();
    rng_init_ret = kStatus_SSS_Fail;
    hal_trng_init();
    ck_assert_int_eq(session_open, 0);
    ck_assert_int_eq(close_calls, 1);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    reset_mocks();
    rng_seed_ret = kStatus_SSS_Fail;
    hal_trng_init();
    ck_assert_int_eq(rng_live, 0);
    ck_assert_int_eq(rng_free_calls, 1);
    ck_assert_int_eq(session_open, 0);
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), -1);

    /* A later init can still succeed */
    rng_seed_ret = kStatus_SSS_Success;
    hal_trng_init();
    ck_assert_int_eq(hal_trng_get_entropy(buf, sizeof(buf)), 0);
}
END_TEST

Suite *mcxw_tz_suite(void)
{
    Suite *s = suite_create("mcxw-tz");
    TCase *tc = tcase_create("mcxw-tz");

    tcase_add_test(tc, test_sau_map);
    tcase_add_test(tc, test_sau_keeps_secure);
    tcase_add_test(tc, test_irq_routing);
    tcase_add_test(tc, test_prepare_boot_hands_over_leds_only);
    tcase_add_test(tc, test_uart);
    tcase_add_test(tc, test_timer);
    tcase_add_test(tc, test_trng_needs_init);
    tcase_add_test(tc, test_trng_init_and_read);
    tcase_add_test(tc, test_trng_enclave_not_ready);
    tcase_add_test(tc, test_trng_failures_release_session);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s;
    SRunner *sr;

    if (unit_mmio_map(MRCC_BASE, 0x1000) == NULL) {
        fprintf(stderr, "cannot map the MRCC page at 0x%08x\n",
            (unsigned)MRCC_BASE);
        return 1;
    }
    s = mcxw_tz_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
