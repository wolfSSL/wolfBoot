/* unit-update-ram-nofixed.c
 *
 * Reproducer for fallback selection in update_ram.c without fixed partitions.
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

#include "user_settings.h"
#include "wolfboot/wolfboot.h"

#define wolfBoot_dualboot_candidate_addr wolfBoot_dualboot_candidate_addr_impl
#include "libwolfboot.c"
#undef wolfBoot_dualboot_candidate_addr

static int dualboot_candidate_addr_calls;

int wolfBoot_dualboot_candidate_addr(void** addr)
{
    dualboot_candidate_addr_calls++;
    ck_assert_msg(dualboot_candidate_addr_calls == 1,
        "wolfBoot_dualboot_candidate_addr() called %d times",
        dualboot_candidate_addr_calls);
    return wolfBoot_dualboot_candidate_addr_impl(addr);
}

#include "update_ram.c"
#include "unit-mock-flash.c"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/sha256.h>

int wolfBoot_staged_ok = 0;
const uint32_t *wolfBoot_stage_address = (uint32_t *)0xFFFFFFFF;

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
    struct wolfBoot_image boot_image;

    if (wolfBoot_panicked)
        return;

    wolfBoot_staged_ok++;
    wolfBoot_stage_address = address;
    ck_assert_uint_eq((uintptr_t)address, WOLFBOOT_LOAD_ADDRESS);

    memset(&boot_image, 0, sizeof(boot_image));
    ck_assert_int_eq(wolfBoot_open_image_address(&boot_image, wolfboot_ram), 0);
    boot_image.hdr = wolfboot_ram;
    boot_image.fw_base = (void *)(uintptr_t)WOLFBOOT_LOAD_ADDRESS;
    boot_image.part = PART_BOOT;
    boot_image.not_ext = 1;
    ck_assert_int_eq(wolfBoot_verify_integrity(&boot_image), 0);
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

static void reset_mock_stats(void)
{
    wolfBoot_panicked = 0;
    wolfBoot_staged_ok = 0;
    dualboot_candidate_addr_calls = 0;
}

static void prepare_flash(void)
{
    int ret;

    ret = mmap_file("/tmp/wolfboot-unit-ext-file-nofixed.bin",
        (void *)(uintptr_t)MOCK_ADDRESS_UPDATE,
        WOLFBOOT_PARTITION_SIZE + IMAGE_HEADER_SIZE, NULL);
    ck_assert_int_ge(ret, 0);
    ret = mmap_file("/tmp/wolfboot-unit-int-file-nofixed.bin",
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

START_TEST(test_invalid_update_falls_back_to_boot_without_reselect_loop)
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

    ck_assert_int_eq(wolfBoot_staged_ok, 1);
    ck_assert_ptr_eq(wolfBoot_stage_address, (const uint32_t *)WOLFBOOT_LOAD_ADDRESS);
    cleanup_flash();
}
END_TEST

static Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfboot-update-ram-nofixed");
    TCase *tc = tcase_create("fallback");

    tcase_add_test(tc, test_invalid_update_falls_back_to_boot_without_reselect_loop);
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
    return fails;
}
