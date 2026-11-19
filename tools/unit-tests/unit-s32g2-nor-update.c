/* unit-s32g2-nor-update.c
 *
 * Unit tests for tools/scripts/s32g2/s32g2-nor-update.py against the
 * NVM_FLASH_JOURNAL trailer code in src/libwolfboot.c. The two partitions are
 * a file mapped at the mock partition addresses: the script writes the file,
 * wolfBoot reads and writes the mapping. Each boot runs in a forked child, so
 * it starts without journal RAM state.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "image.h"

#define PART_SIZE   WOLFBOOT_PARTITION_SIZE
#define SECT_SIZE   WOLFBOOT_SECTOR_SIZE
#define BOOT_OFF    0
#define UPDATE_OFF  PART_SIZE
#define IMAGE_LEN   0x1234
#define IMAGE_VER   7

#define SCRIPT      "../scripts/s32g2/s32g2-nor-update.py"

static char flash_path[] = "/tmp/unit-s32g2-nor-update-XXXXXX";
static char image_path[] = "/tmp/unit-s32g2-nor-image-XXXXXX";
static uint8_t *boot_part = (uint8_t *)WOLFBOOT_PARTITION_BOOT_ADDRESS;
static uint8_t *update_part = (uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS;

/* Results reported by a child */
struct shared {
    int      ret;
    uint8_t  st;
    uint32_t version;
    int      violations; /* programs over a byte that is not erased */
};
static struct shared *sh;

static uint8_t arg_part, arg_state;

int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)address;
    int i;

    for (i = 0; i < len; i++) {
        if (dst[i] != FLASH_BYTE_ERASED)
            sh->violations++;
        dst[i] = data[i];
    }
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    memset((void *)(uintptr_t)address, FLASH_BYTE_ERASED, len);
    return 0;
}
int hal_flash_is_erased(haladdr_t address, int len)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)address;
    int i;

    for (i = 0; i < len; i++) {
        if (p[i] != FLASH_BYTE_ERASED)
            return 0;
    }
    return 1;
}
void hal_flash_unlock(void) {}
void hal_flash_lock(void) {}

static void boot(void (*fn)(void))
{
    pid_t pid;
    int st = 0;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        fn();
        _exit(0);
    }
    waitpid(pid, &st, 0);
    ck_assert(WIFEXITED(st) && (WEXITSTATUS(st) == 0));
}

static void child_get_state(void)
{
    sh->ret = wolfBoot_get_partition_state(arg_part, &sh->st);
    sh->version = wolfBoot_update_firmware_version();
}

static void child_set_state(void)
{
    sh->ret = wolfBoot_set_partition_state(arg_part, arg_state);
}

static void get_state(uint8_t part)
{
    arg_part = part;
    boot(child_get_state);
}

static void set_state(uint8_t part, uint8_t state)
{
    arg_part = part;
    arg_state = state;
    boot(child_set_state);
    ck_assert_int_eq(sh->ret, 0);
}

/* Run the script on the flash file. Return its exit status. */
static int script(const char *args)
{
    char cmd[512];
    int st;

    snprintf(cmd, sizeof(cmd), "python3 " SCRIPT " --dev %s --boot %d "
        "--update %d --part-size %d --sector %d %s >/dev/null 2>&1",
        flash_path, BOOT_OFF, UPDATE_OFF, PART_SIZE, SECT_SIZE, args);
    st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int script_stage(void)
{
    char args[128];

    snprintf(args, sizeof(args), "stage %s", image_path);
    return script(args);
}

static void write_image(const uint8_t *img, size_t len)
{
    FILE *f = fopen(image_path, "wb");

    ck_assert_ptr_nonnull(f);
    ck_assert_uint_eq(fwrite(img, 1, len, f), len);
    fclose(f);
}

/* A wolfBoot header with a version field, then a payload */
static void make_image(uint8_t *img, size_t len, uint32_t version)
{
    static const uint8_t hdr[] = { 'W', 'O', 'L', 'F', 0, 0, 0, 0,
        0x01, 0x00, 0x04, 0x00 };
    uint32_t payload = (uint32_t)(len - IMAGE_HEADER_SIZE);
    size_t i;

    for (i = 0; i < len; i++)
        img[i] = (uint8_t)(i * 7 + 3);
    memset(img, 0xFF, IMAGE_HEADER_SIZE);
    memcpy(img, hdr, sizeof(hdr));
    memcpy(img + 4, &payload, 4);
    memcpy(img + sizeof(hdr), &version, 4);
}

static void setup(void)
{
    static uint8_t img[IMAGE_LEN];

    memset(boot_part, FLASH_BYTE_ERASED, PART_SIZE);
    memset(update_part, FLASH_BYTE_ERASED, PART_SIZE);
    memset(sh, 0, sizeof(*sh));
    make_image(img, sizeof(img), IMAGE_VER);
    write_image(img, sizeof(img));
}

static void teardown(void)
{
}

/* A staged image reads as an update request with the image's version. */
START_TEST(test_stage)
{
    static uint8_t img[IMAGE_LEN];

    /* Stale content and log records must not survive */
    memset(update_part, 0x5A, PART_SIZE);
    ck_assert_int_eq(script_stage(), 0);

    make_image(img, sizeof(img), IMAGE_VER);
    ck_assert_mem_eq(update_part, img, sizeof(img));
    /* The log sector is erased and only the marker is programmed in it */
    ck_assert_int_eq(hal_flash_is_erased(
        WOLFBOOT_PARTITION_UPDATE_ADDRESS + PART_SIZE - SECT_SIZE,
        SECT_SIZE - 5), 1);
    get_state(PART_UPDATE);
    ck_assert_int_eq(sh->ret, 0);
    ck_assert_uint_eq(sh->st, IMG_STATE_UPDATING);
    ck_assert_uint_eq(sh->version, IMAGE_VER);
    get_state(PART_BOOT);
    ck_assert_int_eq(sh->ret, -1);
}
END_TEST

/* The confirm record is the one wolfBoot writes itself, in the same slot,
 * and wolfBoot keeps using the log after it. */
START_TEST(test_confirm)
{
    static uint8_t own[SECT_SIZE];
    uint8_t *log = update_part + PART_SIZE - SECT_SIZE;

    ck_assert_int_eq(script_stage(), 0);
    set_state(PART_UPDATE, IMG_STATE_TESTING);
    set_state(PART_UPDATE, IMG_STATE_SUCCESS);
    memcpy(own, log, SECT_SIZE);

    ck_assert_int_eq(script_stage(), 0);
    set_state(PART_UPDATE, IMG_STATE_TESTING);
    ck_assert_int_eq(script("confirm"), 0);
    ck_assert_mem_eq(log, own, SECT_SIZE);
    get_state(PART_UPDATE);
    ck_assert_int_eq(sh->ret, 0);
    ck_assert_uint_eq(sh->st, IMG_STATE_SUCCESS);

    set_state(PART_UPDATE, IMG_STATE_TESTING);
    get_state(PART_UPDATE);
    ck_assert_uint_eq(sh->st, IMG_STATE_TESTING);
    ck_assert_int_eq(script("confirm"), 0);
    get_state(PART_UPDATE);
    ck_assert_uint_eq(sh->st, IMG_STATE_SUCCESS);
    ck_assert_int_eq(sh->violations, 0);
}
END_TEST

/* The partition in the testing state is the one confirmed. */
START_TEST(test_confirm_boot)
{
    set_state(PART_BOOT, IMG_STATE_TESTING);
    ck_assert_int_eq(script("confirm"), 0);
    get_state(PART_BOOT);
    ck_assert_int_eq(sh->ret, 0);
    ck_assert_uint_eq(sh->st, IMG_STATE_SUCCESS);
    get_state(PART_UPDATE);
    ck_assert_int_eq(sh->ret, -1);
}
END_TEST

/* Nothing is written when no partition is being tested. */
START_TEST(test_confirm_nothing)
{
    static uint8_t before[PART_SIZE];

    ck_assert_int_eq(script("confirm"), 1);
    ck_assert_int_eq(script_stage(), 0);
    memcpy(before, update_part, PART_SIZE);
    ck_assert_int_eq(script("confirm"), 1);
    ck_assert_mem_eq(update_part, before, PART_SIZE);

    set_state(PART_UPDATE, IMG_STATE_TESTING);
    ck_assert_int_eq(script("confirm"), 0);
    memcpy(before, update_part, PART_SIZE);
    ck_assert_int_eq(script("confirm"), 1);
    ck_assert_mem_eq(update_part, before, PART_SIZE);
}
END_TEST

/* A file that is not a signed image, or that would reach the log sector, is
 * refused before anything is erased. */
START_TEST(test_stage_refused)
{
    static uint8_t img[PART_SIZE];
    static uint8_t before[PART_SIZE];

    memset(update_part, 0x5A, PART_SIZE);
    memcpy(before, update_part, PART_SIZE);

    make_image(img, IMAGE_LEN, IMAGE_VER);
    img[0] = 'X';
    write_image(img, IMAGE_LEN);
    ck_assert_int_ne(script_stage(), 0);
    ck_assert_mem_eq(update_part, before, PART_SIZE);

    make_image(img, PART_SIZE - SECT_SIZE + 1, IMAGE_VER);
    write_image(img, PART_SIZE - SECT_SIZE + 1);
    ck_assert_int_ne(script_stage(), 0);
    ck_assert_mem_eq(update_part, before, PART_SIZE);

    make_image(img, PART_SIZE - SECT_SIZE, IMAGE_VER);
    write_image(img, PART_SIZE - SECT_SIZE);
    ck_assert_int_eq(script_stage(), 0);
    get_state(PART_UPDATE);
    ck_assert_uint_eq(sh->st, IMG_STATE_UPDATING);

    ck_assert_int_eq(script("stage"), 2);
    ck_assert_int_eq(script("erase"), 2);
}
END_TEST

Suite *wolfboot_suite(void)
{
    Suite *s = suite_create("wolfBoot-s32g2-nor-update");
    TCase *tc = tcase_create("nor-update");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_stage);
    tcase_add_test(tc, test_confirm);
    tcase_add_test(tc, test_confirm_boot);
    tcase_add_test(tc, test_confirm_nothing);
    tcase_add_test(tc, test_stage_refused);
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    int fd;
    Suite *s;
    SRunner *sr;
    void *b, *u;

    fd = mkstemp(image_path);
    if (fd < 0)
        return 1;
    close(fd);
    fd = mkstemp(flash_path);
    if ((fd < 0) || (ftruncate(fd, 2 * PART_SIZE) != 0))
        return 1;
    sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    b = mmap(boot_part, PART_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_FIXED, fd, BOOT_OFF);
    u = mmap(update_part, PART_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_FIXED, fd, UPDATE_OFF);
    if ((sh == MAP_FAILED) || (b == MAP_FAILED) || (u == MAP_FAILED))
        return 1;

    s = wolfboot_suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    unlink(flash_path);
    unlink(image_path);
    return fails;
}
