/* unit-ata-security-passphrase-zeroize.c
 *
 * Regression test for security_command_passphrase() leaving the plaintext
 * disk-unlock passphrase resident in the file-static ATA command DMA buffer
 * ("buffer" in src/x86/ata.c) after it returns, instead of wiping it like
 * ahci.c does for its own copies of the same secret (ahci_secret_zeroize()).
 * Exercised through the public ata_security_unlock_device() wrapper, which
 * is exactly how sata_unlock_disk() reaches it.
 */

#include <check.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <x86/ahci.h>
#include <x86/ata.h>

#define WOLFBOOT_ATA_DISK_LOCK

/* Mocked AHCI port registers. security_command_passphrase() reaches these
 * only through find_cmd_slot() (SACT/CI, to allocate a slot) and
 * exec_cmd_slot_ex() (TFD/IS/CI, to wait for command completion). An
 * always-idle model is enough to drive a real command to synchronous
 * completion without simulating actual AHCI hardware; mock_slots_full
 * additionally lets a test force prepare_cmd_h2d_slot() down its "no free
 * slot" error path. */
static int mock_slots_full;

/* When set, port IS reads report a task-file error so
 * ata_cmd_complete_async() takes its error exit. */
static int mock_tfes;

uint32_t mmio_read32(uintptr_t address)
{
    (void)address;
    if (mock_slots_full)
        return 0xFFFFFFFF;
    if (mock_tfes)
        return AHCI_PORT_IS_TFES;
    return 0;
}

void mmio_write32(uintptr_t address, uint32_t value)
{
    (void)address;
    (void)value;
}

void panic(void)
{
    ck_abort_msg("panic!");
}

#include "../../src/x86/ata.c"

/* struct ata_drive stores clb_port/ctable_port as uint32_t "physical"
 * addresses, matching the real x86 target's 32-bit DMA pointers.  MAP_32BIT
 * keeps these allocations inside that range on a 64-bit test host so the
 * truncating uint32_t assignment below doesn't lose address bits. */
static uint8_t *clb_mem;
static uint8_t *ctable_mem;

static void setup(void)
{
    mock_slots_full = 0;
    mock_tfes = 0;
    clb_mem = mmap(NULL, sizeof(struct hba_cmd_header) * 32,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(clb_mem, MAP_FAILED);
    ctable_mem = mmap(NULL, sizeof(struct hba_cmd_table),
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    ck_assert_ptr_ne(ctable_mem, MAP_FAILED);

    memset(&ATA_Drv[0], 0, sizeof(ATA_Drv[0]));
    ATA_Drv[0].ahci_base = 0x10000; /* arbitrary: mmio_* mocks ignore it */
    ATA_Drv[0].ahci_port = 0;
    ATA_Drv[0].clb_port = (uint32_t)(uintptr_t)clb_mem;
    ATA_Drv[0].ctable_port = (uint32_t)(uintptr_t)ctable_mem;
}

static void teardown(void)
{
    munmap(clb_mem, sizeof(struct hba_cmd_header) * 32);
    munmap(ctable_mem, sizeof(struct hba_cmd_table));
}

static void assert_password_field_zero(const char *ctx)
{
    int i;
    for (i = 0; i < ATA_SECURITY_PASSWORD_LEN; i++) {
        ck_assert_msg(buffer[ATA_SECURITY_PASSWORD_OFFSET + i] == 0,
            "%s: plaintext passphrase still resident in static ATA command "
            "buffer: byte %d = 0x%02x", ctx, i,
            buffer[ATA_SECURITY_PASSWORD_OFFSET + i]);
    }
}

/* Reachable path taken every time sata_unlock_disk() unlocks a drive
 * (ata_st == ATA_SEC4): the command dispatches and completes synchronously.
 * Per the report, nothing overwrites the password bytes on return other
 * than the next unrelated command that happens to reuse the buffer. */
START_TEST(test_unlock_zeroizes_passphrase_after_command_completes)
{
    static const char passphrase[] = "unit-test-disk-secret";
    int r;

    r = ata_security_unlock_device(0, passphrase, 0);
    ck_assert_int_eq(r, 0);

    assert_password_field_zero("after successful SECURITY UNLOCK");
}
END_TEST

/* Reachable when the HBA has no free command slot: security_command_passphrase()
 * still memcpy()s the passphrase into the static buffer before checking
 * `slot < 0`, so the secret is written even though no ATA command is ever
 * dispatched. */
START_TEST(test_unlock_zeroizes_passphrase_on_no_free_slot)
{
    static const char passphrase[] = "unit-test-disk-secret";
    int r;

    mock_slots_full = 1;
    r = ata_security_unlock_device(0, passphrase, 0);
    ck_assert_int_eq(r, -1);

    assert_password_field_zero("after no-free-slot error return");
}
END_TEST

START_TEST(test_erase_unit_async_zeroizes_on_completion)
{
    static const char passphrase[] = "unit-test-disk-secret";
    volatile uint8_t *pw =
        (volatile uint8_t *)buffer + ATA_SECURITY_PASSWORD_OFFSET;
    int r;
    int i;

    r = ata_security_erase_unit(0, passphrase, 0);
    ck_assert_int_eq(r, ATA_ERR_BUSY);

    /* Command is in flight: scrubbing now would race the HBA DMA, so
     * the passphrase must still be present until completion. */
    for (i = 0; i < (int)strlen(passphrase); i++)
        ck_assert_uint_eq(pw[i], (uint8_t)passphrase[i]);

    r = ata_cmd_complete_async();
    ck_assert_int_eq(r, 0);

    assert_password_field_zero("after async SECURITY ERASE UNIT completion");
}
END_TEST

START_TEST(test_erase_unit_async_zeroizes_on_port_error)
{
    static const char passphrase[] = "unit-test-disk-secret";
    int r;

    r = ata_security_erase_unit(0, passphrase, 0);
    ck_assert_int_eq(r, ATA_ERR_BUSY);

    mock_tfes = 1;
    r = ata_cmd_complete_async();
    ck_assert_int_eq(r, -1);

    assert_password_field_zero("after async port-error completion");
}
END_TEST

static Suite *ata_security_passphrase_zeroize_suite(void)
{
    Suite *s = suite_create("ata_security_passphrase_zeroize");
    TCase *tc = tcase_create("zeroize");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_unlock_zeroizes_passphrase_after_command_completes);
    tcase_add_test(tc, test_unlock_zeroizes_passphrase_on_no_free_slot);
    tcase_add_test(tc, test_erase_unit_async_zeroizes_on_completion);
    tcase_add_test(tc, test_erase_unit_async_zeroizes_on_port_error);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = ata_security_passphrase_zeroize_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
