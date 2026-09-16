/* unit-custom-trailer-nopart.c
 *
 * Compile + behaviour gate for the CUSTOM_PARTITION_TRAILER /
 * WOLFBOOT_NO_PARTITIONS configuration: no WOLFBOOT_FIXED_PARTITIONS, so
 * wolfboot_magic_trail and the fixed partition addresses are excluded from
 * libwolfboot.c. The partition state API must still compile and work through
 * the externally-defined get/set_trailer_at backend, while the
 * fixed-partition functions (sector flags, erase, trigger, success) are
 * absent.
 */
#ifndef WOLFBOOT_HASH_SHA256
    #define WOLFBOOT_HASH_SHA256
#endif

#define NO_FORK 1

#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "target.h"

#include "user_settings.h"
#include "wolfboot/wolfboot.h"

/* Custom partition trailer backend (mocked).
 * Layout per partition: [state(1)][magic(4)]
 *   get_trailer_at(part, 0) -> magic (base+1 .. base+4)
 *   get_trailer_at(part, 1) -> state (base+0)
 */
static uint8_t mock_trailer_boot[5];
static uint8_t mock_trailer_update[5];

uint8_t* get_trailer_at(uint8_t part, uint32_t at)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot
                                        : mock_trailer_update;
    if (at == 0)
        return &base[1];
    return &base[at - 1];
}

void set_trailer_at(uint8_t part, uint32_t at, uint8_t val)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot
                                        : mock_trailer_update;
    if (at == 0)
        base[1] = val;
    else
        base[at - 1] = val;
}

void set_partition_magic(uint8_t part)
{
    uint8_t *base = (part == PART_BOOT) ? mock_trailer_boot
                                        : mock_trailer_update;
    /* WOLFBOOT_MAGIC_TRAIL = 0x544F4F42 on LE: 'B','O','O','T' */
    base[1] = 'B';
    base[2] = 'O';
    base[3] = 'O';
    base[4] = 'T';
    (void)part;
}

#include "libwolfboot.c"
#include "unit-mock-flash.c"

static void reset_trailers(void)
{
    memset(mock_trailer_boot, 0, sizeof(mock_trailer_boot));
    memset(mock_trailer_update, 0, sizeof(mock_trailer_update));
}

/* State API round-trips through the custom backend with no fixed
 * partitions present. */
START_TEST(test_set_get_partition_state)
{
    uint8_t st = 0;

    reset_trailers();

    /* No magic yet: set writes the magic then the state. */
    ck_assert_int_eq(wolfBoot_set_partition_state(PART_BOOT,
        IMG_STATE_TESTING), 0);
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_BOOT, &st), 0);
    ck_assert_uint_eq(st, IMG_STATE_TESTING);

    /* Update partition is independent. */
    ck_assert_int_eq(wolfBoot_set_partition_state(PART_UPDATE,
        IMG_STATE_UPDATING), 0);
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_UPDATE, &st), 0);
    ck_assert_uint_eq(st, IMG_STATE_UPDATING);

    /* PART_NONE is rejected. */
    ck_assert_int_eq(wolfBoot_set_partition_state(PART_NONE, 0), -1);
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_NONE, &st), -1);

    /* get on a partition without magic is rejected. */
    reset_trailers();
    ck_assert_int_eq(wolfBoot_get_partition_state(PART_BOOT, &st), -1);
}
END_TEST

int main(int argc, char *argv[])
{
    int failed;
    Suite *s;
    TCase *tc;
    SRunner *sr;

    s = suite_create("custom-trailer-nopart");
    tc = tcase_create("state-api");

    tcase_add_checked_fixture(tc, reset_trailers, NULL);
    tcase_add_test(tc, test_set_get_partition_state);
    suite_add_tcase(s, tc);

    sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    (void)argc;
    (void)argv;
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
