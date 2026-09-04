/* unit-update-disk-fsp.c
 *
 * Unit tests for the WOLFBOOT_FSP (x86 FSP) boot path of update_disk.c:
 * the low-memory (tolum) size check must reject a slot and try the other
 * one, like every other per-slot rejection in the retry loop.
 */

#define WOLFBOOT_UPDATE_DISK
#define WOLFBOOT_SELF_UPDATE_MONOLITHIC
#define RAM_CODE
#define WOLFBOOT_SELF_HEADER
#define IMAGE_HEADER_SIZE 256
#define BOOT_PART_A 0
#define BOOT_PART_B 1
#define MOCK_ADDRESS_BOOT 0xCD000000

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <check.h>

#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "loader.h"
#include "stage2_params.h"
#include "x86/common.h"

#define TEST_PAYLOAD_SIZE 64

static uint8_t load_buffer[TEST_PAYLOAD_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

static uint8_t part_a_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static uint8_t part_b_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static int mock_do_boot_called;
static const uint32_t *mock_boot_address;
static struct stage2_parameter mock_stage2_params;

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
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

/* fw_size may legitimately claim more than the bytes actually present in
 * the partition buffer: the low-memory check must reject such a slot
 * before the payload is read. */
static void build_image(uint8_t *image, uint32_t version, uint32_t fw_size,
    uint8_t fill)
{
    memset(image, 0, IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE);
    set_u32_le(image, WOLFBOOT_MAGIC);
    set_u32_le(image + sizeof(uint32_t), fw_size);
    set_u16_le(image + IMAGE_HEADER_OFFSET, HDR_VERSION);
    set_u16_le(image + IMAGE_HEADER_OFFSET + sizeof(uint16_t), 4);
    set_u32_le(image + IMAGE_HEADER_OFFSET + 2 * sizeof(uint16_t), version);
    memset(image + IMAGE_HEADER_SIZE, fill, TEST_PAYLOAD_SIZE);
}

static void reset_mocks(void)
{
    memset(load_buffer, 0, sizeof(load_buffer));
    build_image(part_a_image, 7, TEST_PAYLOAD_SIZE, 0xA1);
    build_image(part_b_image, 7, TEST_PAYLOAD_SIZE, 0xB2);
    mock_do_boot_called = 0;
    mock_boot_address = NULL;
    /* The image must fit between load_address and tolum: exactly one
     * TEST_PAYLOAD_SIZE payload. */
    mock_stage2_params.tolum =
        (uint32_t)((uintptr_t)load_buffer + TEST_PAYLOAD_SIZE);
    wolfBoot_panicked = 0;
}

/* --- mocks ---------------------------------------------------------- */

int disk_init(int drv)
{
    (void)drv;
    return 0;
}

int disk_open(int drv)
{
    (void)drv;
    return 0;
}

void disk_close(int drv)
{
    (void)drv;
}

int disk_part_read(int drv, int part, uint64_t off, uint64_t sz, uint8_t *buf)
{
    uint8_t *image;
    uint64_t max = IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE;

    (void)drv;
    image = (part == BOOT_PART_B) ? part_b_image : part_a_image;
    if ((off > max) || (sz > (max - off)))
        return -1;
    memcpy(buf, image + off, (size_t)sz);
    return (int)sz;
}

uint32_t wolfBoot_get_blob_version(uint8_t *blob)
{
    uint8_t *p = blob + IMAGE_HEADER_OFFSET;
    uint8_t *end = blob + IMAGE_HEADER_SIZE;
    uint16_t type;
    uint16_t len;
    uint32_t version = 0;

    while (((uintptr_t)p + 4) <= (uintptr_t)end) {
        type = (uint16_t)(p[0] | (p[1] << 8));
        if (type == 0)
            break;
        len = (uint16_t)(p[2] | (p[3] << 8));
        if (type == HDR_VERSION) {
            memcpy(&version, p + 4, sizeof(version));
            break;
        }
        p += 4 + len;
    }
    return version;
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
    img->sha_ok = 1;
    return 0;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    img->signature_ok = 1;
    return 0;
}

int wolfBoot_get_dts_size(void *dts_addr, uint32_t capacity)
{
    (void)capacity;
    (void)dts_addr;
    return -1;
}

struct stage2_parameter *stage2_get_parameters(void)
{
    return &mock_stage2_params;
}

void x86_log_memory_load(uint32_t start, uint32_t end, const char *name)
{
    (void)start;
    (void)end;
    (void)name;
}

void hal_prepare_boot(void)
{
}

int hal_flash_protect(haladdr_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void do_boot(const uint32_t *address)
{
    mock_do_boot_called++;
    mock_boot_address = address;
}

#include "update_disk.c"

/* --- tests ---------------------------------------------------------- */

START_TEST(test_fsp_oversized_slot_falls_back_to_other_slot)
{
    /* Slot A declares a payload that does not fit in low memory; slot B
     * holds a good image. Boot must continue with B instead of aborting. */
    reset_mocks();
    build_image(part_a_image, 7, TEST_PAYLOAD_SIZE * 2, 0xA1);
    build_image(part_b_image, 7, TEST_PAYLOAD_SIZE, 0xB2);

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_ptr_eq(mock_boot_address, (const uint32_t *)WOLFBOOT_LOAD_ADDRESS);
    ck_assert_int_eq(memcmp(load_buffer, part_b_image + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

START_TEST(test_fsp_both_slots_oversized_panics)
{
    /* Neither slot fits: the retry loop must exhaust and panic. */
    reset_mocks();
    build_image(part_a_image, 7, TEST_PAYLOAD_SIZE * 2, 0xA1);
    build_image(part_b_image, 7, TEST_PAYLOAD_SIZE * 2, 0xB2);

    wolfBoot_start();

    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 0);
}
END_TEST

START_TEST(test_fsp_fitting_slot_boots)
{
    /* Both slots fit and versions are equal: primary (A) boots. */
    reset_mocks();

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_ptr_eq(mock_boot_address, (const uint32_t *)WOLFBOOT_LOAD_ADDRESS);
    ck_assert_int_eq(memcmp(load_buffer, part_a_image + IMAGE_HEADER_SIZE,
        TEST_PAYLOAD_SIZE), 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk-fsp");

    tcase_add_test(tc, test_fsp_oversized_slot_falls_back_to_other_slot);
    tcase_add_test(tc, test_fsp_both_slots_oversized_panics);
    tcase_add_test(tc, test_fsp_fitting_slot_boots);
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
