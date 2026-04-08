#define WOLFBOOT_UPDATE_DISK
#define WOLFBOOT_SKIP_BOOT_VERIFY
#define WOLFBOOT_SELF_UPDATE_MONOLITHIC
#define WOLFBOOT_SELF_HEADER
#define EXT_ENCRYPTED
#define ENCRYPT_WITH_CHACHA
#define HAVE_CHACHA
#define IMAGE_HEADER_SIZE 256
#define BOOT_PART_A 0
#define BOOT_PART_B 1

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <check.h>

#include "target.h"
#include "wolfboot/wolfboot.h"
#include "image.h"
#include "loader.h"
#include <wolfssl/wolfcrypt/chacha.h>

#define TEST_PAYLOAD_SIZE 64

static uint8_t load_buffer[TEST_PAYLOAD_SIZE];
#define WOLFBOOT_LOAD_ADDRESS ((uintptr_t)load_buffer)

static uint8_t part_a_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static uint8_t part_b_image[IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE];
static int mock_disk_init_ret;
static int mock_disk_close_called;
static int mock_do_boot_called;
static const uint32_t *mock_boot_address;

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

static void reset_mocks(void)
{
    memset(load_buffer, 0, sizeof(load_buffer));
    memset(part_a_image, 0, sizeof(part_a_image));
    memset(part_b_image, 0, sizeof(part_b_image));
    build_image(part_a_image, 1, 0xA1);
    build_image(part_b_image, 2, 0xB2);
    mock_disk_init_ret = 0;
    mock_disk_close_called = 0;
    mock_do_boot_called = 0;
    mock_boot_address = NULL;
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

void ForceZero(void* mem, size_t len)
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
    uint64_t max = IMAGE_HEADER_SIZE + TEST_PAYLOAD_SIZE;

    (void)drv;
    image = (part == BOOT_PART_B) ? part_b_image : part_a_image;
    if ((off > max) || (sz > (max - off)))
        return -1;
    memcpy(buf, image + off, (size_t)sz);
    return (int)sz;
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
    (void)img;
    return 0;
}

int wolfBoot_verify_authenticity(struct wolfBoot_image* img)
{
    (void)img;
    return 0;
}

int wolfBoot_get_dts_size(void *dts_addr)
{
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

#include "update_disk.c"

START_TEST(test_update_disk_zeroizes_key_material_on_panic)
{
    size_t i;

    reset_mocks();
    mock_disk_init_ret = -1;

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 1);
    for (i = 0; i < ENCRYPT_KEY_SIZE; i++) {
        ck_assert_uint_eq(disk_encrypt_key[i], 0);
    }
    for (i = 0; i < ENCRYPT_NONCE_SIZE; i++) {
        ck_assert_uint_eq(disk_encrypt_nonce[i], 0);
    }
}
END_TEST

START_TEST(test_update_disk_zeroizes_key_material_before_boot)
{
    size_t i;

    reset_mocks();

    wolfBoot_start();

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_disk_close_called, 1);
    ck_assert_int_eq(mock_do_boot_called, 1);
    ck_assert_ptr_eq(mock_boot_address, (const uint32_t *)WOLFBOOT_LOAD_ADDRESS);
    for (i = 0; i < ENCRYPT_KEY_SIZE; i++) {
        ck_assert_uint_eq(disk_encrypt_key[i], 0);
    }
    for (i = 0; i < ENCRYPT_NONCE_SIZE; i++) {
        ck_assert_uint_eq(disk_encrypt_nonce[i], 0);
    }
}
END_TEST

START_TEST(test_get_decrypted_blob_version_rejects_truncated_version_tlv)
{
    uint8_t hdr[IMAGE_HEADER_SIZE + 2];
    uint8_t *p;

    memset(hdr, 0, sizeof(hdr));
    set_u32_le(hdr, WOLFBOOT_MAGIC);

    p = hdr + IMAGE_HEADER_SIZE - 6;
    {
        uint8_t *q;

        for (q = hdr + IMAGE_HEADER_OFFSET; q < p; q += 2) {
            q[0] = 0xFF;
            if (q + 1 < p)
                q[1] = 0x00;
        }
    }
    set_u16_le(p, HDR_VERSION);
    set_u16_le(p + sizeof(uint16_t), 4);
    p[4] = 0x11;
    p[5] = 0x22;
    p[6] = 0x33;
    p[7] = 0x44;

    ck_assert_uint_eq(get_decrypted_blob_version(hdr), 0);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot");
    TCase *tc = tcase_create("update-disk");

    tcase_add_test(tc, test_update_disk_zeroizes_key_material_on_panic);
    tcase_add_test(tc, test_update_disk_zeroizes_key_material_before_boot);
    tcase_add_test(tc, test_get_decrypted_blob_version_rejects_truncated_version_tlv);
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
