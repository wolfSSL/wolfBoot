/* unit-boot-x86_fsp.c
 *
 * Unit tests for selected boot_x86_fsp helpers.
 */

#include <check.h>
#include <stdint.h>
#include <string.h>

#define TEST_CFG_SIZE 256
#define TEST_READ_LIMIT 200

static uint8_t test_cfg[TEST_CFG_SIZE];
static unsigned int test_read_count;

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    (void)bus;
    (void)dev;
    (void)fun;

    test_read_count++;
    ck_assert_msg(test_read_count < TEST_READ_LIMIT,
                  "pci_get_capability exceeded read limit on cyclic list");
    return test_cfg[off];
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fun, uint8_t off)
{
    uint16_t value;

    (void)bus;
    (void)dev;
    (void)fun;

    test_read_count++;
    ck_assert_msg(test_read_count < TEST_READ_LIMIT,
                  "pci_get_capability exceeded read limit on cyclic list");
    memcpy(&value, &test_cfg[off], sizeof(value));
    return value;
}

#include "../../src/boot_x86_fsp.c"

static void setup(void)
{
    memset(test_cfg, 0, sizeof(test_cfg));
    test_read_count = 0;
}

START_TEST(test_pci_get_capability_finds_requested_capability)
{
    uint8_t cap_off = 0;
    uint16_t status = PCI_STATUS_CAP_LIST;

    memcpy(&test_cfg[PCI_STATUS_OFFSET], &status, sizeof(status));
    test_cfg[PCI_CAP_OFFSET] = 0x40;
    test_cfg[0x40] = 0x01;
    test_cfg[0x41] = 0x48;
    test_cfg[0x48] = PCI_PCIE_CAP_ID;
    test_cfg[0x49] = 0x00;

    ck_assert_int_eq(pci_get_capability(0, 0, 0, PCI_PCIE_CAP_ID, &cap_off), 0);
    ck_assert_uint_eq(cap_off, 0x48);
}
END_TEST

START_TEST(test_pci_get_capability_rejects_cyclic_capability_lists)
{
    uint8_t cap_off = 0xAA;
    uint16_t status = PCI_STATUS_CAP_LIST;

    memcpy(&test_cfg[PCI_STATUS_OFFSET], &status, sizeof(status));
    test_cfg[PCI_CAP_OFFSET] = 0x40;
    test_cfg[0x40] = 0x01;
    test_cfg[0x41] = 0x48;
    test_cfg[0x48] = 0x05;
    test_cfg[0x49] = 0x40;

    ck_assert_int_eq(pci_get_capability(0, 0, 0, PCI_PCIE_CAP_ID, &cap_off), -1);
    ck_assert_uint_eq(cap_off, 0xAA);
}
END_TEST

static Suite *boot_x86_fsp_suite(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("boot_x86_fsp");
    tc = tcase_create("pci_get_capability");
    tcase_add_checked_fixture(tc, setup, NULL);
    tcase_add_test(tc, test_pci_get_capability_finds_requested_capability);
    tcase_add_test(tc, test_pci_get_capability_rejects_cyclic_capability_lists);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s;
    SRunner *sr;
    int failed;

    s = boot_x86_fsp_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
