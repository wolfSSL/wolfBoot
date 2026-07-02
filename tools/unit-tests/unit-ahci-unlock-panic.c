/* unit-ahci-unlock-panic.c
 *
 * Regression test for sata_unlock_disk() leaking the plaintext disk-unlock
 * secret on the stack when the post-unlock security-state check fails.
 * That path calls panic() directly, bypassing the cleanup: label that
 * zeroizes the secret buffer, so the secret survives on the stack for as
 * long as the (real) panic() halt loop keeps the DRAM contents alive.
 */

#include <check.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <x86/ata.h>

#define WOLFBOOT_ATA_DISK_LOCK
#define WOLFBOOT_ATA_DISK_LOCK_PASSWORD "unit-test-secret"

static jmp_buf panic_jmp;
static int panic_count = 0;

/* Captured by the ata_security_unlock_device() mock below: the address and
 * length of sata_unlock_disk()'s local `secret` buffer. panic() runs
 * synchronously from inside sata_unlock_disk() (before any stack unwinding),
 * so reading through this pointer from panic() observes the exact stack
 * contents an attacker halting the device at that point would see. */
static const uint8_t *mock_secret_ptr;
static size_t mock_secret_len;
static uint8_t panic_secret_snapshot[64];
static size_t panic_secret_snapshot_len;

/* Satisfies __attribute__((noreturn)) via longjmp, like unit-x86-paging-oob.c
 * does for the same reason: allows the test to resume after a panic() call
 * without executing any code that follows it. */
__attribute__((noreturn)) void panic(void)
{
    panic_count++;
    if (mock_secret_ptr != NULL && mock_secret_len <= sizeof(panic_secret_snapshot)) {
        memcpy(panic_secret_snapshot, mock_secret_ptr, mock_secret_len);
        panic_secret_snapshot_len = mock_secret_len;
    }
    longjmp(panic_jmp, 1);
}

/* Mocked ATA security layer: ahci.c calls these as extern functions
 * (defined in src/x86/ata.c, not included here) to drive the security
 * state machine. Controlled by the test to reach the state-mismatch path
 * at the end of sata_unlock_disk() without any real hardware. */
static enum ata_security_state mock_states[8];
static int mock_states_idx;
static int mock_unlock_ret;
static int mock_identify_ret;
static int mock_freeze_ret;

enum ata_security_state ata_security_get_state(int drv)
{
    (void)drv;
    ck_assert_int_lt(mock_states_idx, 8);
    return mock_states[mock_states_idx++];
}

int ata_security_unlock_device(int drv, const char *passphrase, int master)
{
    (void)drv; (void)master;
    mock_secret_ptr = (const uint8_t *)passphrase;
    mock_secret_len = strlen(WOLFBOOT_ATA_DISK_LOCK_PASSWORD);
    return mock_unlock_ret;
}

int ata_identify_device(int drv)
{
    (void)drv;
    return mock_identify_ret;
}

int ata_security_freeze_lock(int drv)
{
    (void)drv;
    return mock_freeze_ret;
}

int ata_security_set_password(int drv, int master, const char *passphrase)
{
    (void)drv; (void)master; (void)passphrase;
    return 0;
}

#include "../../src/x86/ahci.c"

static void reset_mocks(void)
{
    memset(mock_states, 0, sizeof(mock_states));
    mock_states_idx = 0;
    mock_unlock_ret = 0;
    mock_identify_ret = 0;
    mock_freeze_ret = 0;
    mock_secret_ptr = NULL;
    mock_secret_len = 0;
    memset(panic_secret_snapshot, 0, sizeof(panic_secret_snapshot));
    panic_secret_snapshot_len = 0;
    panic_count = 0;
}

/* Drive under ATA_SEC4 (locked), unlock command succeeds, but the drive
 * never actually reaches ATA_SEC5/SEC6 (e.g. a faulty/hostile drive that
 * acknowledges the unlock command without changing state). This forces
 * sata_unlock_disk() into the state-mismatch branch that calls panic()
 * directly at src/x86/ahci.c:507, per the report's description: "This path
 * is reachable when the drive does not reach the expected SEC5/SEC6
 * security state after a successful secret retrieval." */
START_TEST(test_unlock_zeroizes_secret_before_state_mismatch_panic)
{
    int r;

    reset_mocks();
    mock_states[0] = ATA_SEC4; /* initial state: locked */
    mock_states[1] = ATA_SEC4; /* state after unlock+identify: still locked */
    mock_states[2] = ATA_SEC4; /* final check: still not SEC6 with freeze=1 */

    if (setjmp(panic_jmp) == 0) {
        r = sata_unlock_disk(0, 1);
        ck_abort_msg("sata_unlock_disk returned %d instead of panicking "
                     "on state mismatch", r);
    }

    ck_assert_int_eq(panic_count, 1);
    ck_assert_uint_eq(panic_secret_snapshot_len, strlen(WOLFBOOT_ATA_DISK_LOCK_PASSWORD));
    for (r = 0; r < (int)panic_secret_snapshot_len; r++) {
        ck_assert_msg(panic_secret_snapshot[r] == 0,
            "plaintext unlock secret still on the stack at panic(): "
            "byte %d = 0x%02x", r, panic_secret_snapshot[r]);
    }
}
END_TEST

static Suite *ahci_unlock_panic_suite(void)
{
    Suite *s = suite_create("ahci_unlock_panic");
    TCase *tc = tcase_create("state_mismatch_zeroize");
    tcase_add_test(tc, test_unlock_zeroizes_secret_before_state_mismatch_panic);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = ahci_unlock_panic_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
