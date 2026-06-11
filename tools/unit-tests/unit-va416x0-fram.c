#include <check.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define WOLFBOOT_UNIT_TEST_VA416X0_FRAM

#define FRAM_SIZE (256U * 1024U)
#define ROM_SPI_BANK 0
#define SPI_NUM_BANKS 1
#define SPI_STATUS_TFE_Msk 0x01U
#define SPI_STATUS_BUSY_Msk 0x02U
#define SPI_FIFO_CLR_RXFIFO_Msk 0x01U
#define SPI_FIFO_CLR_TXFIFO_Msk 0x02U
#define RAMFUNCTION

typedef enum {
    hal_status_ok = 0,
    hal_status_badParam = 1,
    hal_status_err = 2
} hal_status_t;

typedef enum {
    hal_spi_state_reset = 0,
    hal_spi_state_ready = 1
} hal_spi_state_t;

typedef struct {
    uint32_t STATUS;
    uint32_t FIFO_CLR;
} mock_spi_bank_t;

typedef struct {
    mock_spi_bank_t BANK[SPI_NUM_BANKS];
} mock_spi_regs_t;

typedef struct {
    bool blockmode;
    bool bmstall;
    int clkDiv;
    bool loopback;
    bool mdlycap;
    int mode;
    int ms;
    uint8_t chipSelect;
    int wordLen;
} hal_spi_init_t;

typedef struct {
    bool locked;
    hal_spi_state_t state;
    mock_spi_bank_t *spi;
    hal_spi_init_t init;
} hal_spi_handle_t;

enum {
    hal_spi_clkmode_0 = 0,
    hal_spi_ms_master = 1
};

static mock_spi_regs_t mock_vor_spi;
#define VOR_SPI (&mock_vor_spi)

static hal_status_t transmit_script[8];
static bool transmit_close_flags[8];
static int transmit_script_len;
static int transmit_call_count;

static void set_transmit_script(const hal_status_t *script, int len)
{
    int i;

    memset(transmit_close_flags, 0, sizeof(transmit_close_flags));
    for (i = 0; i < len; i++) {
        transmit_script[i] = script[i];
    }
    transmit_script_len = len;
    transmit_call_count = 0;
}

hal_status_t HAL_Spi_Init(hal_spi_handle_t *handle)
{
    (void)handle;
    return hal_status_ok;
}

hal_status_t HAL_Spi_Transmit(hal_spi_handle_t *handle, uint8_t *data,
    uint32_t len, uint32_t timeout, bool close)
{
    (void)handle;
    (void)data;
    (void)len;
    (void)timeout;

    ck_assert_int_lt(transmit_call_count, transmit_script_len);
    transmit_close_flags[transmit_call_count] = close;

    return transmit_script[transmit_call_count++];
}

hal_status_t HAL_Spi_TransmitReceive(hal_spi_handle_t *handle, uint8_t *tx,
    uint8_t *rx, uint32_t tx_len, uint32_t rx_skip, uint32_t rx_len,
    uint32_t timeout, bool close)
{
    (void)handle;
    (void)tx;
    (void)rx;
    (void)tx_len;
    (void)rx_skip;
    (void)rx_len;
    (void)timeout;
    (void)close;
    return hal_status_ok;
}

void HAL_Timer_DelayMs(uint32_t delay)
{
    (void)delay;
}

int wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

#include "../../hal/va416x0.c"

static void reset_spi_mocks(void)
{
    memset(&mock_vor_spi, 0, sizeof(mock_vor_spi));
    mock_vor_spi.BANK[0].STATUS = SPI_STATUS_TFE_Msk;
}

START_TEST(test_fram_write_command_failure_aborts_split_transaction)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    hal_status_t init_script[] = {
        hal_status_ok, hal_status_ok, hal_status_ok
    };
    hal_status_t fail_script[] = {
        hal_status_ok, hal_status_err
    };
    hal_status_t success_script[] = {
        hal_status_ok, hal_status_ok, hal_status_ok
    };

    reset_spi_mocks();

    set_transmit_script(init_script, 3);
    ck_assert_int_eq(FRAM_Init(0, 0), hal_status_ok);
    ck_assert_int_eq(spiHandle.state, hal_spi_state_ready);

    set_transmit_script(fail_script, 2);
    ck_assert_int_eq(FRAM_Write(0, 0x20, buf, sizeof(buf)), hal_status_err);
    ck_assert_int_eq(spiHandle.state, hal_spi_state_ready);
    ck_assert_uint_eq(mock_vor_spi.BANK[0].FIFO_CLR,
        SPI_FIFO_CLR_RXFIFO_Msk | SPI_FIFO_CLR_TXFIFO_Msk);
    ck_assert_int_eq(transmit_call_count, 2);
    ck_assert_int_eq(transmit_close_flags[0], true);
    ck_assert_int_eq(transmit_close_flags[1], false);

    set_transmit_script(success_script, 3);
    ck_assert_int_eq(FRAM_Write(0, 0x24, buf, sizeof(buf)), hal_status_ok);
    ck_assert_int_eq(transmit_call_count, 3);
    ck_assert_int_eq(transmit_close_flags[0], true);
    ck_assert_int_eq(transmit_close_flags[1], false);
    ck_assert_int_eq(transmit_close_flags[2], true);
}
END_TEST

Suite *va416x0_fram_suite(void)
{
    Suite *s = suite_create("va416x0-fram");
    TCase *tc = tcase_create("fram");

    tcase_add_test(tc, test_fram_write_command_failure_aborts_split_transaction);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    int fails;
    Suite *s = va416x0_fram_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}
