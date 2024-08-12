
static int locked = 1;
static int erased_boot = 0;
static int erased_update = 0;
static int erased_swap = 0;
static int erased_nvm_bank0 = 0;
static int erased_nvm_bank1 = 0;
static int erased_vault = 0;
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
    if ((address >= WOLFBOOT_PARTITION_BOOT_ADDRESS) &&
            (address < WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE)) {
        for (i = 0; i < len; i++) {
            a[i] = data[i];
        }
    }
#ifdef MOCK_KEYVAULT
    if ((address >= vault_base) && (address < vault_base + keyvault_size)) {
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
        memset(address, 0xFF, len);
        if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank0++;
        } else if (address >= WOLFBOOT_PARTITION_BOOT_ADDRESS + WOLFBOOT_PARTITION_SIZE - 2 * WOLFBOOT_SECTOR_SIZE) {
            erased_nvm_bank1++;
        }
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
#ifdef MOCK_KEYVAULT
    } else if ((address >= vault_base) && (address < vault_base + keyvault_size)) {
        erased_vault++;
        memset(address, 0xFF, len);
#endif
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
