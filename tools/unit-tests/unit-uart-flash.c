/* unit-uart-flash.c
 *
 * Unit tests for the UART flash transport.
 */

#define UART_FLASH

#include <check.h>
#include <stdint.h>
#include <string.h>

static const uint8_t CMD_ACK = 0x06;

static uint8_t rx_script[16];
static int rx_script_len;
static int rx_script_pos;
static uint8_t tx_log[32];
static int tx_log_len;

uint32_t wolfBoot_current_firmware_version(void)
{
    return 0;
}

int uart_tx(const uint8_t c)
{
    ck_assert_int_lt(tx_log_len, (int)sizeof(tx_log));
    tx_log[tx_log_len++] = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    if (rx_script_pos >= rx_script_len)
        return 0;

    *c = rx_script[rx_script_pos++];
    return 1;
}

#include "../../src/uart_flash.c"

static void reset_uart_script(const uint8_t *script, int len)
{
    ck_assert_int_le(len, (int)sizeof(rx_script));
    memset(rx_script, 0, sizeof(rx_script));
    memcpy(rx_script, script, len);
    rx_script_len = len;
    rx_script_pos = 0;
    memset(tx_log, 0, sizeof(tx_log));
    tx_log_len = 0;
}

START_TEST(test_ext_flash_read_timeout_returns_error)
{
    uint8_t data[2] = {0};
    uint8_t script[11];
    int ret;

    memset(script, CMD_ACK, 10);
    script[10] = 0x5a;
    reset_uart_script(script, sizeof(script));

    ret = ext_flash_read(0x12345678, data, sizeof(data));

    ck_assert_int_eq(ret, -1);
    ck_assert_uint_eq(data[0], 0x5a);
    ck_assert_uint_eq(data[1], 0x00);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *uart_flash = tcase_create("UART flash");

    tcase_add_test(uart_flash, test_ext_flash_read_timeout_returns_error);
    tcase_set_timeout(uart_flash, 20);
    suite_add_tcase(s, uart_flash);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
