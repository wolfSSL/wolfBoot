/* unit-qspi-flash.c
 *
 * Unit tests for qspi_flash.c.
 */

#define QSPI_FLASH
#define QSPI_FLASH_READY_TRIES 4

#include <check.h>
#include <stdint.h>
#include <string.h>

static int program_call_count;
static uint32_t program_sizes[8];
static const uint8_t *program_ptrs[8];
static uint32_t program_addrs[8];
static int write_enable_call_count;
static int write_enable_status_seq[8];
static int current_write_enable_call;

void spi_init(int polarity, int phase)
{
    (void)polarity;
    (void)phase;
}

void spi_release(void)
{
}

#include "../../src/qspi_flash.c"

int qspi_transfer(uint8_t fmode, const uint8_t cmd,
    uint32_t addr, uint32_t addrSz, uint32_t addrMode,
    uint32_t alt, uint32_t altSz, uint32_t altMode,
    uint32_t dummySz,
    uint8_t* data, uint32_t dataSz, uint32_t dataMode)
{
    (void)fmode;
    (void)addrSz;
    (void)addrMode;
    (void)alt;
    (void)altSz;
    (void)altMode;
    (void)dummySz;
    (void)dataMode;

    if (cmd == WRITE_ENABLE_CMD) {
        write_enable_call_count++;
        current_write_enable_call = write_enable_call_count;
        return 0;
    }

    if (cmd == READ_SR_CMD) {
        ck_assert_ptr_nonnull(data);
        ck_assert_uint_ge(dataSz, 1);
        if (current_write_enable_call > 0 &&
            write_enable_status_seq[current_write_enable_call - 1] != 0) {
            data[0] = 0;
        } else {
            data[0] = FLASH_SR_WRITE_EN;
        }
        return 0;
    }

    if (cmd == FLASH_WRITE_CMD) {
        ck_assert_int_lt(program_call_count, (int)(sizeof(program_sizes) / sizeof(program_sizes[0])));
        program_sizes[program_call_count] = dataSz;
        program_ptrs[program_call_count] = data;
        program_addrs[program_call_count] = addr;
        program_call_count++;
        return 0;
    }

    return 0;
}

static void setup(void)
{
    program_call_count = 0;
    memset(program_sizes, 0, sizeof(program_sizes));
    memset(program_ptrs, 0, sizeof(program_ptrs));
    memset(program_addrs, 0, sizeof(program_addrs));
    write_enable_call_count = 0;
    current_write_enable_call = 0;
    memset(write_enable_status_seq, 0, sizeof(write_enable_status_seq));
}

START_TEST(test_qspi_write_splits_last_page_to_remaining_bytes)
{
    uint8_t buf[300];
    int ret;

    memset(buf, 0xA5, sizeof(buf));

    ret = spi_flash_write(0x1000, buf, sizeof(buf));

    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(program_call_count, 2);
    ck_assert_uint_eq(program_sizes[0], FLASH_PAGE_SIZE);
    ck_assert_ptr_eq(program_ptrs[0], buf);
    ck_assert_uint_eq(program_addrs[0], 0x1000);
    ck_assert_uint_eq(program_sizes[1], sizeof(buf) - FLASH_PAGE_SIZE);
    ck_assert_ptr_eq(program_ptrs[1], buf + FLASH_PAGE_SIZE);
    ck_assert_uint_eq(program_addrs[1], 0x1000 + FLASH_PAGE_SIZE);
}
END_TEST

START_TEST(test_qspi_write_stops_after_midloop_write_enable_failure)
{
    uint8_t buf[FLASH_PAGE_SIZE * 3];
    int ret;

    memset(buf, 0x3C, sizeof(buf));
    write_enable_status_seq[1] = -1;

    ret = spi_flash_write(0x2000, buf, sizeof(buf));

    ck_assert_int_ne(ret, 0);
    ck_assert_int_eq(program_call_count, 1);
    ck_assert_uint_eq(program_sizes[0], FLASH_PAGE_SIZE);
    ck_assert_ptr_eq(program_ptrs[0], buf);
    ck_assert_uint_eq(program_addrs[0], 0x2000);
}
END_TEST

static Suite *qspi_flash_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("QSPI Flash");
    tc = tcase_create("Write");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_qspi_write_splits_last_page_to_remaining_bytes);
    tcase_add_test(tc, test_qspi_write_stops_after_midloop_write_enable_failure);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = qspi_flash_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
