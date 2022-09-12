
#include <stdio.h>
#include "include/user_settings.h"
#include "include/target.h"

int main(int ac, char *av)
{
    printf("WOLFBOOT_FLASH_ADDR:  0x%08x\n", WOLFBOOT_FLASH_ADDR);
    printf("WOLFBOOT_FLASH_SIZE:  0x%08x\n", WOLFBOOT_FLASH_SIZE);
    printf("WOLFBOOT_BOOT_SIZE:   0x%08x\n", WOLFBOOT_BOOT_SIZE);
    printf("WOLFBOOT_RX_EXCVECT:  0x%08x\n", WOLFBOOT_RX_EXCVECT);
    printf("WOLFBOOT_SECTOR_SIZE: 0x%08x\n", WOLFBOOT_SECTOR_SIZE);
    printf("\n");
    printf("WOLFBOOT_PARTITION_SIZE:           0x%08x\n", 
                WOLFBOOT_PARTITION_SIZE);
    printf("WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x%08x\n", 
                WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printf("WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x%08x\n", 
                WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printf("WOLFBOOT_PARTITION_SWAP_ADDRESS:   0x%08x\n", 
                WOLFBOOT_PARTITION_SWAP_ADDRESS);
    printf("\n");
    printf("Application Entry Address:         0x%08x\n", 
                WOLFBOOT_PARTITION_BOOT_ADDRESS+IMAGE_HEADER_SIZE);        
    return 0;
}
