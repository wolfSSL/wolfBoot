#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define UNIT_TEST
#define WC_NO_HARDEN
#define MOCK_ADDRESS 0xCC000000
#include <stdio.h>
#include "libwolfboot.c"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <check.h>

static int locked = 0;
static int erased_boot = 0;
static int erased_update = 0;
static int erased_nvm_bank0 = 0;
static int erased_nvm_bank1 = 0;





/* Mocks */
void hal_init(void)
{
}
int hal_flash_write(haladdr_t address, const uint8_t *data, int len)
{
    int i;
    uint8_t *a = (uint8_t *)address;
    if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
    return 0;
}
int hal_flash_erase(haladdr_t address, int len)
{
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_boot++;
    } else if ((address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        erased_update++;
        if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
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

/* A simple mock memory */
static int mmap_file(const char *path, uint8_t *address, uint8_t** ret_address)
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
    for (i = 0; i < WOLFBOOT_PARTITION_SIZE; i+=4) {
        const uint32_t erased_word = 0xFFFFFFFF;
        write(fd, &erased_word, 4); 
    }
    lseek(fd, SEEK_SET, 0); 

    mmaped_addr = mmap(address, WOLFBOOT_PARTITION_SIZE, PROT_READ | PROT_WRITE,
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


START_TEST (test_nvm_select_fresh_sector)
{
    int ret;
    const char BOOT[] = "BOOT";
    uint8_t st;
    ret = mmap_file("/tmp/wolfboot-unit-file.bin", MOCK_ADDRESS, NULL);

    /* Erased flag sectors: select '0' by default */
    ret = nvm_select_fresh_sector(PART_UPDATE);
    fail_if(ret != 0, "Failed to select default fresh sector\n");

    /* Force a good 'magic' at the end of sector 1 */
    hal_flash_write(WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 
            (WOLFBOOT_SECTOR_SIZE + 4), BOOT, 4);

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



}
END_TEST


Suite *wolfboot_suite(void)
{

    /* Suite initialization */
    Suite *s = suite_create("wolfBoot-NVM-workarounds");

    /* Test cases */
    TCase *nvm_select_fresh_sector = tcase_create("NVM select fresh sector");
    tcase_add_test(nvm_select_fresh_sector, test_nvm_select_fresh_sector);
    suite_add_tcase(s, nvm_select_fresh_sector);

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
