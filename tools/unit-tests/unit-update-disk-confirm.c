#define WOLFBOOT_UPDATE_DISK
#define WOLFBOOT_SELF_UPDATE_MONOLITHIC
#define RAM_CODE
#define WOLFBOOT_SELF_HEADER
#define EXT_ENCRYPTED
#define ENCRYPT_WITH_CHACHA
#define HAVE_CHACHA
#define IMAGE_HEADER_SIZE 256
#define BOOT_PART_A 0
#define BOOT_PART_B 1
#define MOCK_ADDRESS_BOOT 0xCD000000
#define DISK_BOOT_CONFIRM

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <check.h>

#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "loader.h"
#include <wolfssl/wolfcrypt/chacha.h>

#define TEST_PAYLOAD_SIZE 64

/* Sized for the whole partition, not just TEST_PAYLOAD_SIZE: the
 * trailer-overlap test below stages a deliberately large image. */
static uint8_t load_buffer[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE + 1024];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

/* A real partition is far larger than its image; the boot-state record lives
 * in its last 512-byte sector. Size the mock so image and record cannot
 * overlap, which is the layout the loader requires. */
#define TEST_PART_SIZE (IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE + 1024)
#define TEST_STATE_OFF (TEST_PART_SIZE - 512)

static uint8_t part_a_image[TEST_PART_SIZE];
static uint8_t part_b_image[TEST_PART_SIZE];
/* What disk_part_size() reports. Settable, because the loader locates the
 * trailer from the size the media reports: shrinking it is the only way to
 * reach the "trailer would overlap the image" and "partition too small"
 * refusals, and TEST_PART_SIZE always leaves room for the trailer. */
static uint64_t mock_part_size;
static int mock_disk_init_ret;
static int mock_disk_close_called;
static int mock_do_boot_called;
static const uint32_t *mock_boot_address;
static int mock_fail_payload_part;
static int mock_verify_integrity_ret;
/* not_ext as the verify calls saw it: the staged image lives in RAM, so an
 * EXT_FLASH build must not route these reads back through external flash. */
static int mock_integrity_saw_not_ext;
static int mock_authenticity_saw_not_ext;
static int mock_verify_authenticity_ret;
static int mock_state_writes;

ChaCha chacha;

static void set_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)(value >> 8);
}

static void set_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)(value >> 24);
}

static void build_image(uint8_t *image, uint32_t version, uint8_t fill)
{
    memset(image, 0, IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE);
    set_u32_le(image, WOLFBOOT_MAGIC);
    set_u32_le(image + sizeof(uint32_t), TEST_PAYLOAD_SIZE);
    set_u16_le(image + IMAGE_HEADER_OFFSET, HDR_VERSION);
    set_u16_le(image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), 4);
    set_u32_le(image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), version);
    memset(image + IMAGE_HEADER_SIZE, fill, TEST_PAYLOAD_SIZE);
}
/* Same, with an explicit payload size, for the test that needs an image
 * large enough to reach the partition tail. */
static void build_image_sized(uint8_t *image, uint32_t version, uint8_t fill,
    uint32_t payload_sz)
{
    memset(image, 0, IMAGE_HEADER_SIZE + payload_sz);
    set_u32_le(image, WOLFBOOT_MAGIC);
    set_u32_le(image + sizeof(uint32_t), payload_sz);
    set_u16_le(image + IMAGE_HEADER_OFFSET, HDR_VERSION);
    set_u16_le(image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), 4);
    set_u32_le(image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), version);
    memset(image + IMAGE_HEADER_SIZE, fill, payload_sz);
}


static int mock_flash_protect_called;
static haladdr_t mock_flash_protect_addr;
static int mock_flash_protect_len;

static void reset_mocks(void)
{
    mock_part_size = TEST_PART_SIZE;
    memset(load_buffer, 0, sizeof(load_buffer));
    memset(part_a_image, 0, sizeof(part_a_image));
    memset(part_b_image, 0, sizeof(part_b_image));
    build_image(part_a_image, 1, 0xA1);
    build_image(part_b_image, 2, 0xB2);
    mock_disk_init_ret = 0;
    mock_disk_close_called = 0;
    mock_do_boot_called = 0;
    mock_boot_address = NULL;
    mock_fail_payload_part = -1;
    mock_verify_integrity_ret = 0;
    mock_verify_authenticity_ret = 0;
    mock_state_writes = 0;
    mock_integrity_saw_not_ext = -1;
    mock_authenticity_saw_not_ext = -1;
    mock_flash_protect_called = 0;
    mock_flash_protect_addr = 0;
    mock_flash_protect_len = 0;
    wolfBoot_panicked = 0;
}

int chacha_init(void)
{
    return 0;
}

int wc_Chacha_SetIV(ChaCha* ctx, const byte* inIv, word32 counter)
{
    (void)ctx;
    (void)inIv;
    (void)counter;
    return 0;
}

int wc_Chacha_Process(ChaCha* ctx, byte* output, const byte* input, word32 msglen)
{
    (void)ctx;
    memmove(output, input, msglen);
    return 0;
}

void wc_ForceZero(void* mem, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)mem;
    while (len-- > 0) {
        *p++ = 0;
    }
}

int wolfBoot_initialize_encryption(void)
{
    return 0;
}

int wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    memset(key, 0x5A, ENCRYPT_KEY_SIZE);
    memset(nonce, 0xC3, ENCRYPT_NONCE_SIZE);
    return 0;
}

int disk_init(int drv)
{
    (void)drv;
    return mock_disk_init_ret;
}

int disk_open(int drv)
{
    (void)drv;
    return 0;
}

void disk_close(int drv)
{
    (void)drv;
    mock_disk_close_called++;
}

int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    uint8_t *image;

    (void)drv;
    image = (part == BOOT_PART_B) ? part_b_image : part_a_image;
    if ((mock_fail_payload_part == part) && (off >= IMAGE_HEADER_SIZE) &&
            (off < TEST_STATE_OFF))
        return -1;
    if ((off > TEST_PART_SIZE) || (sz > (TEST_PART_SIZE - off)))
        return -1;
    memcpy(buf, image + off, (size_t)sz);
    return (int)sz;
}

int disk_part_write(int drv, int part, uint64_t off, uint64_t sz,
    const uint8_t *buf)
{
    uint8_t *image;

    (void)drv;
    image = (part == BOOT_PART_B) ? part_b_image : part_a_image;
    if ((off > TEST_PART_SIZE) || (sz > (TEST_PART_SIZE - off)))
        return -1;
    memcpy(image + off, buf, (size_t)sz);
    mock_state_writes++;
    return (int)sz;
}

int disk_part_size(int drv, int part, uint64_t *size)
{
    (void)drv;
    (void)part;
    if (size == NULL)
        return -1;
    *size = mock_part_size;
    return 0;
}

int wolfBoot_open_image_address(struct wolfBoot_image* img, uint8_t* image)
{
    uint32_t magic;
    uint32_t fw_size;

    memcpy(&magic, image, sizeof(magic));

    if (magic != WOLFBOOT_MAGIC)
        return -1;
    memset(img, 0, sizeof(*img));
    img->hdr = image;
    memcpy(&fw_size, image + sizeof(uint32_t), sizeof(fw_size));
    img->fw_size = fw_size;
    img->fw_base = image + IMAGE_HEADER_SIZE;
    img->hdr_ok = 1;
    return 0;
}

int wolfBoot_verify_integrity(struct wolfBoot_image* img)
{
    mock_integrity_saw_not_ext = img->not_ext;
    if (mock_verify_integrity_ret == 0)
        img->sha_ok = 1;
    return mock_verify_integrity_ret;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    mock_authenticity_saw_not_ext = img->not_ext;
    if (mock_verify_authenticity_ret == 0)
        img->signature_ok = 1;
    return mock_verify_authenticity_ret;
}

int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity)
{
    (void)capacity;
    (void)dts_addr;
    return -1;
}

void hal_prepare_boot(void)
{
}

void do_boot(const uint32_t *address)
{
    mock_do_boot_called++;
    mock_boot_address = address;
}

int hal_flash_protect(haladdr_t address, int len)
{
    mock_flash_protect_called++;
    mock_flash_protect_addr = address;
    mock_flash_protect_len = len;
    return 0;
}

#include "update_disk.c"


/* Mirror of the on-disk trailer the loader writes: "BOOT" in the last four
 * bytes of the partition, state immediately below it. Same layout as the
 * flash partition trailer, with default-polarity IMG_STATE_* values. */
#define ST_NEW       0xFFU
#define ST_UPDATING  0x70U
#define ST_TESTING   0x10U
#define ST_SUCCESS   0x00U

static void set_state(uint8_t *part, uint8_t state)
{
    uint8_t *t = part + TEST_PART_SIZE - 8;

    memset(t, 0xFF, 8);
    t[3] = state;
    t[4] = 'B'; t[5] = 'O'; t[6] = 'O'; t[7] = 'T';
}

/* No trailer at all: the tail is left exactly as an imaging tool leaves it. */
static void set_no_trailer(uint8_t *part, uint8_t fill)
{
    memset(part + TEST_PART_SIZE - 8, fill, 8);
}

/* The same, at an arbitrary partition size, for the tests that shrink it. */
static void set_state_at(uint8_t *part, size_t part_sz, uint8_t state)
{
    uint8_t *t = part + part_sz - 8;

    memset(t, 0xFF, 8);
    t[3] = state;
    t[4] = 'B'; t[5] = 'O'; t[6] = 'O'; t[7] = 'T';
}

static uint8_t get_state_at(const uint8_t *part, size_t part_sz)
{
    const uint8_t *t = part + part_sz - 8;

    if ((t[4] != 'B') || (t[5] != 'O') || (t[6] != 'O') || (t[7] != 'T'))
        return ST_NEW;
    return t[3];
}

static uint8_t get_state(const uint8_t *part)
{
    const uint8_t *t = part + TEST_PART_SIZE - 8;

    if ((t[4] != 'B') || (t[5] != 'O') || (t[6] != 'O') || (t[7] != 'T'))
        return ST_NEW;
    return t[3];
}

/* A slot with no update staged must boot and must NOT be armed. This is the
 * property that stops the feature stranding a system whose OS never confirms:
 * without it, boot 1 arms A, boot 2 retires A and arms B, boot 3 retires B,
 * and nothing is left to boot. */
START_TEST(test_no_update_staged_is_not_armed)
{
    reset_mocks();
    build_image(part_a_image, 7, 0xA1);
    memset(part_b_image, 0, sizeof(part_b_image));
    set_state(part_a_image, ST_SUCCESS);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_uint_eq(get_state(part_a_image), ST_SUCCESS);
    ck_assert_int_eq(mock_state_writes, 0);
}
END_TEST

/* A blank partition tail must read as NEW, not as SUCCESS. An imaged tail is
 * usually 0x00, which IS IMG_STATE_SUCCESS, so the magic is what keeps a
 * never-written slot from looking already-confirmed. */
START_TEST(test_blank_tail_reads_as_new)
{
    reset_mocks();
    build_image(part_a_image, 7, 0xA1);
    memset(part_b_image, 0, sizeof(part_b_image));
    /* 0x00 is what an imaging tool leaves, and 0x00 is also IMG_STATE_SUCCESS */
    set_no_trailer(part_a_image, 0x00);

    wolfBoot_start();

    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(mock_state_writes, 0);
    ck_assert_uint_eq(get_state(part_a_image), ST_NEW);
}
END_TEST

/* The magic must actually gate the state read. A tail with no magic whose
 * bytes happen to look like IMG_STATE_UPDATING must still read as NEW: if
 * the magic check is dropped, this slot gets armed on the strength of
 * uninitialised media. */
START_TEST(test_no_magic_is_not_mistaken_for_updating)
{
    reset_mocks();
    build_image(part_a_image, 7, 0xA1);
    memset(part_b_image, 0, sizeof(part_b_image));
    set_no_trailer(part_a_image, ST_UPDATING);

    wolfBoot_start();

    ck_assert_int_eq(mock_do_boot_called, 1);
    /* no magic, so NEW, so nothing armed and nothing written */
    ck_assert_int_eq(mock_state_writes, 0);
}
END_TEST

/* A staged update is promoted to TESTING before handoff. */
START_TEST(test_staged_update_is_armed)
{
    reset_mocks();
    build_image(part_a_image, 7, 0xA1);
    memset(part_b_image, 0, sizeof(part_b_image));
    set_state(part_a_image, ST_UPDATING);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_uint_eq(get_state(part_a_image), ST_TESTING);
}
END_TEST

/* A slot left TESTING did not confirm: it is skipped, and the other slot
 * boots. Nothing is written on this path. */
START_TEST(test_unconfirmed_slot_is_skipped)
{
    reset_mocks();
    build_image(part_a_image, 7, 0xA1);
    build_image(part_b_image, 7, 0xB2);
    set_state(part_a_image, ST_TESTING);
    set_state(part_b_image, ST_SUCCESS);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, part_b_image + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
    /* the retired slot is left as it was: the reap writes nothing */
    ck_assert_uint_eq(get_state(part_a_image), ST_TESTING);
    ck_assert_int_eq(mock_state_writes, 0);
}
END_TEST

/* Skipping a slot also drops its version from the ceiling, which is what
 * lets an older confirmed slot boot. */
START_TEST(test_skipped_slot_lets_older_slot_boot)
{
    reset_mocks();
    build_image(part_a_image, 4, 0xA1);
    build_image(part_b_image, 5, 0xB2);
    set_state(part_a_image, ST_SUCCESS);
    set_state(part_b_image, ST_TESTING);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(memcmp(load_buffer, part_a_image + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

/* Anti-rollback is NOT relaxed. A slot that merely fails verification keeps
 * its version, so an older slot is still refused. Only a slot retired for
 * failing to boot steps aside. */
START_TEST(test_verification_failure_does_not_relax_antirollback)
{
    reset_mocks();
    build_image(part_a_image, 4, 0xA1);
    build_image(part_b_image, 5, 0xB2);
    set_state(part_a_image, ST_SUCCESS);
    set_state(part_b_image, ST_SUCCESS);
    mock_verify_authenticity_ret = -1;

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 1);
    ck_assert_int_eq(mock_do_boot_called, 0);
}
END_TEST

/* An image that fills its partition leaves no tail to claim, so the arming
 * write must be refused rather than writing over the image it just verified.
 * The boot still proceeds: the consequence of no trailer is no confirmation,
 * not a dead system.
 *
 * The partition is kept at or above DISK_TRAILER_MIN_PART and the image made
 * large enough to reach the tail, so the overlap check is the only thing
 * that can refuse this - the minimum-size check cannot also fire and make
 * the test pass for the wrong reason. */
START_TEST(test_arming_is_refused_when_the_trailer_would_hit_the_image)
{
    const uint32_t big = 1024;
    const uint64_t part_sz = IMAGE_HEADER_SIZE + big;   /* image to the end */
    uint8_t *tail;

    reset_mocks();
    build_image_sized(part_a_image, 4, 0xA1, big);
    build_image(part_b_image, 4, 0xB2);
    mock_part_size = part_sz;
    ck_assert(part_sz >= DISK_TRAILER_MIN_PART);

    /* A trailer at the offset that size implies, which is inside the image. */
    tail = part_a_image + part_sz - DISK_TRAILER_SZ;
    memset(tail, 0xFF, DISK_TRAILER_SZ);
    tail[3] = ST_UPDATING;
    tail[4] = 'B'; tail[5] = 'O'; tail[6] = 'O'; tail[7] = 'T';

    wolfBoot_start();

    /* Booted anyway ... */
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(wolfBoot_panicked, 0);
    /* ... and the refusal left the image bytes alone: still UPDATING, never
     * promoted to TESTING over the top of the payload. */
    ck_assert_uint_eq(tail[3], ST_UPDATING);
}
END_TEST

/* A partition too small to hold a trailer at all must not be armed, and must
 * still boot. */
START_TEST(test_partition_too_small_for_a_trailer_still_boots)
{
    reset_mocks();
    build_image(part_a_image, 4, 0xA1);
    build_image(part_b_image, 4, 0xB2);

    /* Below DISK_TRAILER_MIN_PART, but still clear of the image: the trailer
     * would land at 392 and the image ends at 320, so the overlap check
     * would NOT refuse this. The minimum-size check is the only guard, which
     * is what makes this test specific to it. */
    mock_part_size = 400;
    ck_assert(mock_part_size < DISK_TRAILER_MIN_PART);
    ck_assert(mock_part_size - DISK_TRAILER_SZ >=
        IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE);

    /* A valid UPDATING trailer where that size puts it, so the only reason
     * not to arm is the refusal under test. */
    set_state_at(part_a_image, (size_t)mock_part_size, ST_UPDATING);

    wolfBoot_start();

    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_int_eq(wolfBoot_panicked, 0);
    /* Nothing was written: the tail still reads UPDATING. */
    ck_assert_uint_eq(get_state_at(part_a_image, (size_t)mock_part_size),
        ST_UPDATING);
}
END_TEST

/* Both slots unconfirmed: neither may boot.
 *
 * Reaping zeroes both versions, which makes max_ver zero and so leaves the
 * anti-rollback guard inactive - it only fires when something newer exists.
 * Nothing but the explicit TESTING refusal in the retry loop stops the first
 * slot booting here, which is exactly the hole a failover (selected ^= 1)
 * opens on the other paths into a slot. */
START_TEST(test_two_unconfirmed_slots_boot_neither)
{
    reset_mocks();
    build_image(part_a_image, 4, 0xA1);
    build_image(part_b_image, 4, 0xB2);
    set_state(part_a_image, ST_TESTING);
    set_state(part_b_image, ST_TESTING);

    wolfBoot_start();

    ck_assert_int_eq(mock_do_boot_called, 0);
    /* More than one panic: with both versions reaped to zero the "no valid
     * OS image in either partition" guard fires first, and wolfBoot_panic()
     * halts on a real target but returns in this harness. What matters is
     * that nothing booted. */
    ck_assert_int_gt(wolfBoot_panicked, 0);
}
END_TEST

/* A verification failure on the selected slot must not fail over onto a slot
 * still marked TESTING. Slot B here is a fully readable, valid image; only its
 * unconfirmed TESTING state keeps it from booting, so if the failover ever
 * lands on it do_boot fires and this fails - which would be exactly the retry
 * loop the boot-state record exists to prevent. */
START_TEST(test_failover_does_not_land_on_a_testing_slot)
{
    reset_mocks();
    build_image(part_a_image, 5, 0xA1);
    build_image(part_b_image, 4, 0xB2);
    set_state(part_a_image, ST_SUCCESS);
    set_state(part_b_image, ST_TESTING);
    mock_fail_payload_part = BOOT_PART_A;   /* only the selected slot fails */

    wolfBoot_start();

    ck_assert_int_eq(mock_do_boot_called, 0);
    ck_assert_int_gt(wolfBoot_panicked, 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-confirm");

    tcase_add_test(tc, test_no_update_staged_is_not_armed);
    tcase_add_test(tc, test_blank_tail_reads_as_new);
    tcase_add_test(tc, test_no_magic_is_not_mistaken_for_updating);
    tcase_add_test(tc, test_staged_update_is_armed);
    tcase_add_test(tc, test_unconfirmed_slot_is_skipped);
    tcase_add_test(tc, test_skipped_slot_lets_older_slot_boot);
    tcase_add_test(tc, test_verification_failure_does_not_relax_antirollback);
    tcase_add_test(tc, test_arming_is_refused_when_the_trailer_would_hit_the_image);
    tcase_add_test(tc, test_partition_too_small_for_a_trailer_still_boots);
    tcase_add_test(tc, test_two_unconfirmed_slots_boot_neither);
    tcase_add_test(tc, test_failover_does_not_land_on_a_testing_slot);
    suite_add_tcase(s, tc);

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
