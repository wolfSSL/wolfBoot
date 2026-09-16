/* unit-update-ram-custom-trailer.c
 *
 * Tests update_ram.c with CUSTOM_PARTITION_TRAILER (custom callbacks) and
 * WOLFBOOT_FIXED_PARTITIONS. Covers the HAVE_PARTITION_TRAILERS path where
 * partition state is managed via externally-defined get/set_trailer_at.
 */
#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif

#define IMAGE_HEADER_SIZE 256
#define MOCK_ADDRESS_UPDATE 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
#define NO_FORK 1

#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "target.h"

static __thread unsigned char
    wolfboot_ram[WOLFBOOT_RAMBOOT_MAX_SIZE + IMAGE_HEADER_SIZE];

#define WOLFBOOT_LOAD_ADDRESS (((uintptr_t)wolfboot_ram) + IMAGE_HEADER_SIZE)
#define TEST_SIZE_SMALL 5300
#define DIGEST_TLV_OFF_IN_HDR 28
#define STAGE_ADDR_SENTINEL UINTPTR_MAX

#include "user_settings.h"
#include "wolfboot/wolfboot.h"

/* Custom partition trailer state (mocked)
 * Layout: [state][magic(4 bytes)]
 *   get_trailer_at(part, 0) -> magic (32-bit, base[1..4])
 *   get_trailer_at(part, 1) -> state (8-bit, base[0])
 */
static uint8_t mock_trailer_boot[5];
static uint8_t mock_trailer_update[5];

uint8_t* get_trailer_at(uint8_t part, uint32_t at)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot : mock_trailer_update;
    if (at == 0)
        return &base[1]; /* magic at base[1..4] */
    return &base[at - 1]; /* state at base[0], etc. */
}

void set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot : mock_trailer_update;
    if (at == 0)
        base[1] = val; /* magic byte 0 */
    else
        base[at - 1] = val;
}

void set_partition_magic(uint8_t part)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot : mock_trailer_update;
    /* WOLFBOOT_MAGIC_TRAIL = 0x544F4F42 on LE: bytes are 'B','O','O','T' */
    base[1] = 'B';
    base[2] = 'O';
    base[3] = 'O';
    base[4] = 'T';
    (void)part;
}

#define wolfBoot_dualboot_candidate wolfBoot_dualboot_candidate_impl
#include "libwolfboot.c"
#undef wolfBoot_dualboot_candidate

static int dualboot_candidate_calls;

int wolfBoot_dualboot_candidate(void)
{
    dualboot_candidate_calls++;
    ck_assert_msg(dualboot_candidate_calls == 1,
        "wolfBoot_dualboot_candidate() called %d times",
        dualboot_candidate_calls);
    return wolfBoot_dualboot_candidate_impl();
}

#include "update_ram.c"
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

int wolfBoot_staged_ok = 0;
const uint32_t *wolfBoot_stage_address =
    (const uint32_t *)(uintptr_t)STAGE_ADDR_SENTINEL;

void* hal_get_primary_address(void)
{
    return (void *)(uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;
}

void* hal_get_update_address(void)
{
    return (void *)(uintptr_t)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
}

void do_boot(const uint32_t *address)
{
    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
}

static int mock_flash_protect_called = 0;
static haladdr_t mock_flash_protect_addr = 0;
static int mock_flash_protect_len = 0;

int hal_flash_protect(haladdr_t address, int len)
{
    mock_flash_protect_called++;
    mock_flash_protect_addr = address;
    mock_flash_protect_len = len;
    return 0;
}

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
    dualboot_candidate_calls = 0;
    mock_flash_protect_called = 0;
    mock_flash_protect_addr = 0;
    mock_flash_protect_len = 0;
    memset(mock_trailer_boot, 0, sizeof(mock_trailer_boot));
    memset(mock_trailer_update, 0, sizeof(mock_trailer_update));
}

static void prepare_flash(void)
{
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-ext-file-custom-trailer.bin",
        (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert_int_ge(ret, 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file-custom-trailer.bin",
        (void *)(uintptr_t)MOCK_ADDRESS_BOOT,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert_int_ge(ret, 0);

    ext_flash_unlock();
    ext_flash_erase(WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_erase(WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    ext_flash_lock();
}

static void cleanup_flash(void)
{
    munmap((void *)WOLFBOOT_PARTITION_BOOT_ADDRESS,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
    munmap((void *)WOLFBOOT_PARTITION_UPDATE_ADDRESS,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE);
}

static int add_payload(uint8_t part, uint32_t version, uint32_t size)
{
    uint32_t word;
    uint16_t word16;
    int i;
    uint8_t *base = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
    int ret;
    wc_Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];

    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret != 0)
        return ret;

    if (part == PART_UPDATE)
        base = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    srandom(part);

    ext_flash_unlock();
    ext_flash_write((uintptr_t)base, "WOLF", 4);
    ext_flash_write((uintptr_t)base + 4, (void *)&size, 4);

    word = 4 << 16 | HDR_VERSION;
    ext_flash_write((uintptr_t)base + 8, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + 12, (void *)&version, 4);

    word = 2 << 16 | HDR_IMG_TYPE;
    ext_flash_write((uintptr_t)base + 16, (void *)&word, 4);
    word16 = HDR_IMG_TYPE_AUTH_NONE | HDR_IMG_TYPE_APP;
    ext_flash_write((uintptr_t)base + 20, (void *)&word16, 2);

    ret = wc_Sha256Update(&sha, base, DIGEST_TLV_OFF_IN_HDR);
    if (ret != 0)
        return ret;

    size += IMAGE_HEADER_SIZE;
    for (i = IMAGE_HEADER_SIZE; i < (int)size; i += 4) {
        uint32_t rand_word = (random() << 16) | random();
        ext_flash_write((uintptr_t)base + i, (void *)&rand_word, 4);
    }
    for (i = IMAGE_HEADER_SIZE; i < (int)size; i += WOLFBOOT_SHA_BLOCK_SIZE) {
        int len = WOLFBOOT_SHA_BLOCK_SIZE;

        if (((int)size - i) < len)
            len = (int)size - i;
        ret = wc_Sha256Update(&sha, base + i, len);
        if (ret != 0)
            return ret;
    }

    ret = wc_Sha256Final(&sha, digest);
    if (ret != 0)
        return ret;
    wc_Sha256Free(&sha);

    word = SHA256_DIGEST_SIZE << 16 | HDR_SHA256;
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR, (void *)&word, 4);
    ext_flash_write((uintptr_t)base + DIGEST_TLV_OFF_IN_HDR + 4, digest,
        SHA256_DIGEST_SIZE);
    ext_flash_lock();

    return 0;
}

/* Test 1: Update partition in UPDATING state transitions to TESTING after boot */
START_TEST(test_custom_trailer_updating_sets_testing)
{
    uint8_t state;

    reset_mock_stats();
    prepare_flash();
    ck_assert_int_eq(add_payload(PART_BOOT, 1, TEST_SIZE_SMALL), 0);
    ck_assert_int_eq(add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL), 0);

    /* Set the update partition to UPDATING state via custom trailer */
    set_partition_magic(PART_UPDATE);
    mock_trailer_update[0] = IMG_STATE_UPDATING; /* state at base[0] */

    wolfBoot_start();

    /* After boot, the update partition should be in TESTING state */
    ck_assert_int_eq(wolfBoot_staged_ok, 1);
    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_UPDATE, &state), 0);
    ck_assert_int_eq(state, IMG_STATE_TESTING);

    cleanup_flash();
}
END_TEST

/* Test 2: Invalid update falls back to boot partition */
START_TEST(test_custom_trailer_invalid_update_falls_back_to_boot)
{
    uint8_t bad_digest[SHA256_DIGEST_SIZE];

    reset_mock_stats();
    prepare_flash();
    ck_assert_int_eq(add_payload(PART_BOOT, 1, TEST_SIZE_SMALL), 0);
    ck_assert_int_eq(add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL), 0);

    memset(bad_digest, 0xBA, sizeof(bad_digest));
    ext_flash_unlock();
    ext_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + DIGEST_TLV_OFF_IN_HDR + 4,
        bad_digest, sizeof(bad_digest));
    ext_flash_lock();

    wolfBoot_start();

    /* Falls back to boot partition (version 1) */
    ck_assert_int_eq(wolfBoot_staged_ok, 1);
    ck_assert_int_eq(wolfBoot_panicked, 0);

    cleanup_flash();
}
END_TEST

/* Test 3: Newer update is preferred over boot */
START_TEST(test_custom_trailer_newer_update_prefers_update)
{
    int candidate;

    reset_mock_stats();
    prepare_flash();
    ck_assert_int_eq(add_payload(PART_BOOT, 1, TEST_SIZE_SMALL), 0);
    ck_assert_int_eq(add_payload(PART_UPDATE, 2, TEST_SIZE_SMALL), 0);

    candidate = wolfBoot_dualboot_candidate_impl();

    ck_assert_int_eq(candidate, PART_UPDATE);
    cleanup_flash();
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-update-ram-custom-trailer");
    TCase *tc = tcase_create("custom_trailer");

    tcase_add_test(tc, test_custom_trailer_updating_sets_testing);
    tcase_add_test(tc, test_custom_trailer_invalid_update_falls_back_to_boot);
    tcase_add_test(tc, test_custom_trailer_newer_update_prefers_update);
    tcase_set_timeout(tc, 5);
    suite_add_tcase(s, tc);

    return s;
}

int main(int argc, char *argv[])
{
    int fails;
    Suite *s;
    SRunner *sr;

    argv0 = strdup(argv[0]);
    (void)argc;

    s = wolfboot_suite();
    sr = srunner_create(s);
#if (NO_FORK == 1)
    srunner_set_fork_status(sr, CK_NOFORK);
#endif
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (fails == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
