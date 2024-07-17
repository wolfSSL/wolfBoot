#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define UNIT_TEST
#define WC_NO_HARDEN
#define MOCK_ADDRESS 0xCC000000
#define MOCK_ADDRESS_BOOT 0xCD000000
#define MOCK_ADDRESS_SWAP 0xCE000000
const char ENCRYPT_KEY[] = "0123456789abcdef0123456789abcdef0123456789abcdef";
#include <stdio.h>
#include "encrypt.h"
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>

static int locked = 1;
static int erased_boot = 0;
static int erased_update = 0;
static int erased_swap = 0;
static int erased_nvm_bank0 = 0;
static int erased_nvm_bank1 = 0;
const char *argv0;



/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    fail_if(locked, "Attempting to write to a locked FLASH");
    if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
#ifdef FLAGS_HOME
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
#endif
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    fail_if(locked, "Attempting to erase a locked FLASH");
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_boot++;
#ifdef FLAGS_HOME
        memset(address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
#endif
    } else if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        memset(address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else if ((address >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE)) {
        erased_swap++;
        memset(address, 0xFF, len);
    } else {
        fail("Invalid address\n");
        return -1;
    }
    return 0;
}
void hal_flash_unlock(void)
{
    fail_unless(locked, "Double unlock detected\n");
    locked--;
}
void hal_flash_lock(void)
{
    fail_if(locked, "Double lock detected\n");
    locked++;
}

void hal_prepare_boot(void)
{
}

int ext_flash_erase(uintptr_t address, int len)
{
    printf("%s", __FUNCTION__);
    if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        memset(address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
    } else if ((address >= WOLFBOOT_PARTITION_SWAP_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_SWAP_ADDRESS + WOLFBOOT_SECTOR_SIZE)) {
        erased_swap++;
        memset(address, 0xFF, len);
    } else {
        fail("Invalid address\n");
        return -1;
    }
    return 0;
}

int ext_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    fail_if(locked, "Attempting to write to a locked FLASH");
    printf("%s", __FUNCTION__);
    for (i = 0; i < len; i++) {
        a[i] = data[i];
    }
    return 0;
}

int ext_flash_read(uintptr_t address, uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    for (i = 0; i < len; i++) {
         data[i] = a[i];
    }
    return 0;
}

/* A simple mock memory */
static int mmap_file(const char *path, uint8_t *address, uint32_t len,
        uint8_t** ret_address)
{
    struct stat st = { 0 };
    uint8_t *mmaped_addr;
    int ret;
    int fd;
    int i;

    if (path == NULL)
        return -1;

    fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);
    if (fd == -1) {
        fprintf(stderr, "can't open %s\n", path);
        return -1;
    }
    fprintf(stderr, "Open file: %s success.\n", path);
    for (i = 0; i < len; i+=4) {
        const uint32_t erased_word = 0xBADBADBA;
        write(fd, &erased_word, 4);
    }
    lseek(fd, SEEK_SET, 0);

    mmaped_addr = mmap(address, len, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (mmaped_addr == MAP_FAILED) {
        fprintf(stderr, "MMAP failed.\n");
        return -1;
    }

    fprintf(stderr, "Simulator assigned %s to base %p\n", path, mmaped_addr);

    if (ret_address)
        *ret_address = mmaped_addr;

    close(fd);
    return 0;
}


/* End Mocks */

Suite *wolfboot_suite(void);


START_TEST (test_nvm_update_with_encryption)
{
    int ret, i;
    const char BOOT[] = "BOOT";
    const uint32_t *boot_word = (const uint32_t *)BOOT;
    uint8_t st;
    uint32_t *magic;
    uint8_t *dst, *src;
    uint8_t part = PART_UPDATE;
    uint32_t base_addr = WOLFBOOT_PARTITION_UPDATE_ADDRESS;
    uint32_t home_off = 0;

    ret = mmap_file("/tmp/wolfboot-unit-file.bin", MOCK_ADDRESS,
            WOLFBOOT_PARTITION_SIZE, NULL);
    fail_if(ret < 0);
#ifdef FLAGS_HOME
    ret = mmap_file("/tmp/wolfboot-unit-int-file.bin", MOCK_ADDRESS_BOOT,
            WOLFBOOT_PARTITION_SIZE, NULL);
    fail_if(ret < 0);
    part = PART_BOOT;
    base_addr = WOLFBOOT_PARTITION_BOOT_ADDRESS;
    home_off = PART_BOOT_ENDFLAGS - PART_UPDATE_ENDFLAGS;
#endif
    ret = mmap_file("/tmp/wolfboot-unit-swap.bin", MOCK_ADDRESS_SWAP,
            WOLFBOOT_SECTOR_SIZE, NULL);
    fail_if(ret < 0);

    /* Sanity */
    fail_if(home_off > WOLFBOOT_SECTOR_SIZE);

    /* unlock the flash to allow operations */
    hal_flash_unlock();

    /* Check swap erase */
    wolfBoot_erase_partition(PART_SWAP);
    fail_if(erased_swap != 1);
    for (i = 0; i < WOLFBOOT_SECTOR_SIZE; i+=4) {
        uint32_t *word = ((uint32_t *)(WOLFBOOT_PARTITION_SWAP_ADDRESS + i));
        fail_if(*word != 0xFFFFFFFF);
    }

    erased_update = 0;
    wolfBoot_erase_partition(part);
#ifndef FLAGS_HOME
    fail_if(erased_update != 1);
#else
    fail_if(erased_boot != 1);
#endif
    /* Erased flag sectors: select '0' by default */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select default fresh sector\n");

    /* Force a good 'magic' at the end of sector 1 by setting the magic word */
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_NEW);
    magic = get_partition_magic(PART_UPDATE);
    fail_if(*magic != *boot_word,
            "Failed to read back 'BOOT' trailer at the end of the partition");

    /* Current selected should now be 1 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select good fresh sector\n");

    erased_nvm_bank1 = 0;
    erased_nvm_bank0 = 0;

    /* Calling 'set_partition_state' should change the current sector */
    wolfBoot_set_partition_state(PART_UPDATE, IMG_STATE_UPDATING);

    /* Current selected should now be 0 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank1 == 0, "Did not erase the non-selected bank");

    erased_nvm_bank1 = 0;
    erased_nvm_bank0 = 0;

    /* Check state is read back correctly */
    ret = wolfBoot_get_partition_state(PART_UPDATE, &st);
    fail_if(ret != 0, "Failed to read back state\n");
    fail_if(st != IMG_STATE_UPDATING, "Bootloader in the wrong state\n");

    /* Check that reading did not change the current sector */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select right sector after reading\n");

    /* Update one sector flag, it should change nvm sector */
    wolfBoot_set_update_sector_flag(0, SECT_FLAG_SWAPPING);

    /* Current selected should now be 1 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank0 == 0, "Did not erase the non-selected bank");

    /* Check sector state is read back correctly */
    ret = wolfBoot_get_update_sector_flag(0, &st);
    fail_if (ret != 0, "Failed to read sector flag state\n");
    fail_if (st != SECT_FLAG_SWAPPING, "Wrong sector flag state\n");

    /* Check that reading did not change the current sector (1) */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select right sector after reading sector state\n");

    /* Update sector flag, again. it should change nvm sector */
    erased_nvm_bank1 = 0;
    erased_nvm_bank0 = 0;
    wolfBoot_set_update_sector_flag(0, SECT_FLAG_UPDATED);

    /* Current selected should now be 0 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank1 == 0, "Did not erase the non-selected bank");

    /* Check sector state is read back correctly */
    ret = wolfBoot_get_update_sector_flag(0, &st);
    fail_if (ret != 0, "Failed to read sector flag state\n");
    fail_if (st != SECT_FLAG_UPDATED, "Wrong sector flag state\n");

    /* Check that reading did not change the current sector (0) */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select right sector after reading sector state\n");

    /* Update sector flag, again. it should change nvm sector */
    erased_nvm_bank1 = 0;
    erased_nvm_bank0 = 0;
    wolfBoot_set_update_sector_flag(1, SECT_FLAG_SWAPPING);

    /* Current selected should now be 1 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank0 == 0, "Did not erase the non-selected bank");

    /* Check sector state is read back correctly */
    ret = wolfBoot_get_update_sector_flag(1, &st);
    fail_if (ret != 0, "Failed to read sector flag state\n");
    fail_if (st != SECT_FLAG_SWAPPING, "Wrong sector flag state\n");

    /* Check that reading did not change the current sector (1) */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select right sector after reading sector state\n");

    /* Update sector flag, again. it should change nvm sector */
    erased_nvm_bank1 = 0;
    erased_nvm_bank0 = 0;
    wolfBoot_set_update_sector_flag(1, SECT_FLAG_UPDATED);

    /* Copy flags from 0 to 1 */
    src = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE);
    dst = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - (2 * WOLFBOOT_SECTOR_SIZE));
    for (i = 0; i < WOLFBOOT_SECTOR_SIZE; i++)
        dst[i] = src[i];

    /* Force-erase 4B of sector flags in 0 */
    dst = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - (8 + home_off +
                TRAILER_SKIP + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE));
    for (i = 0; i < 4; i++)
        dst[i] = 0xFF;

    /* This should fall back to 1 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select most recent sector after deleting flags\n");

    /* Start over, update some sector flags */
    wolfBoot_erase_partition(PART_UPDATE);
    wolfBoot_set_update_sector_flag(0, SECT_FLAG_UPDATED);
    wolfBoot_set_update_sector_flag(1, SECT_FLAG_UPDATED);
    wolfBoot_set_update_sector_flag(2, SECT_FLAG_UPDATED);
    wolfBoot_set_update_sector_flag(3, SECT_FLAG_UPDATED);
    wolfBoot_set_update_sector_flag(4, SECT_FLAG_SWAPPING);
    st = IMG_STATE_UPDATING;
    wolfBoot_set_partition_state(PART_UPDATE, &st);

    /* Current selected should now be 1 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank0 == 0, "Did not erase the non-selected bank");

    /* Check sector state is read back correctly */
    for (i = 0; i < 4; i++) {
        ret = wolfBoot_get_update_sector_flag(i, &st);
        fail_if (ret != 0, "Failed to read sector flag state\n");
        fail_if (st != SECT_FLAG_UPDATED, "Wrong sector flag state\n");

    }
    ret = wolfBoot_get_update_sector_flag(4, &st);
    fail_if (ret != 0, "Failed to read sector flag state\n");
    fail_if (st != SECT_FLAG_SWAPPING, "Wrong sector flag state\n");

    /* Check that reading did not change the current sector (1) */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 1, "Failed to select right sector after reading sector state\n");

    /* Copy flags from 1 to 0 */
    src = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - (2 * WOLFBOOT_SECTOR_SIZE));
    dst = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - (WOLFBOOT_SECTOR_SIZE));
    for (i = 0; i < WOLFBOOT_SECTOR_SIZE; i++)
        dst[i] = src[i];

    /* Force to F0 last sector flag in 0, so that the sector '4' is 'updated' */
    dst = (uint8_t *)(base_addr + WOLFBOOT_PARTITION_SIZE - (8 + home_off +
                TRAILER_SKIP + ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE));
    dst[0] = 0xF0;

    /* Check if still there */
    ret = wolfBoot_get_update_sector_flag(4, &st);
    fail_if (ret != 0, "Failed to read sector flag state\n");
    fail_if (st != SECT_FLAG_UPDATED, "Wrong sector flag state\n");

    /* This should fall back to 0 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select most recent sector after deleting flags\n");


    /* Erase partition and start over */
    erased_update = 0;
    erased_boot = 0;
    wolfBoot_erase_partition(part);
#ifndef FLAGS_HOME
    fail_if(erased_update != 1);
#else
    fail_if(erased_boot != 1);
#endif

    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select right sector after reading sector state\n");

    /* re-lock the flash: update_trigger implies unlocking/locking */
    hal_flash_lock();

    /* Triggering update to set flags */
    wolfBoot_update_trigger();

    /* Current selected should now be 0 */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select updating fresh sector\n");
    fail_if(erased_nvm_bank1 == 0, "Did not erase the non-selected bank");

    magic = get_partition_magic(PART_UPDATE);
    fail_if(*magic != *boot_word,
            "Failed to read back 'BOOT' trailer at the end of the partition");

    /* Sanity check at the end of the operations. */
    fail_unless(locked, "The FLASH was left unlocked.\n");


}
END_TEST


Suite *wolfboot_suite(void)
{
    /* Suite initialization */
    Suite *s = suite_create("wolfboot");

    /* Test cases */
    TCase *nvm_update_with_encryption = tcase_create("NVM update with encryption");
    tcase_add_test(nvm_update_with_encryption, test_nvm_update_with_encryption);
    suite_add_tcase(s, nvm_update_with_encryption);

    return s;
}


int main(int argc, char *argv[])
{
    int fails;
    argv0 = strdup(argv[0]);
    Suite *s = wolfboot_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);
    return fails;
}
