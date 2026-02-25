/*
                  Linker File for Renesas FSP
*/

INCLUDE memory_regions.ld

/* wolfBoot override: app must start after wolfBoot bootloader + image header.
 *   WOLFBOOT_PARTITION_BOOT_ADDRESS = 0x00010000 (64KB wolfBoot)
 *   IMAGE_HEADER_SIZE               = 0x00000200 (512B, RSA2048)
 *   -> App vector table start       = 0x00010200
 * This must be placed AFTER INCLUDE memory_regions.ld and BEFORE fsp_gen.ld
 * so that fsp_gen.ld's MEMORY block uses the overridden values. 
 *
 * Please comment-out the following two lines when app runs stand-alone.
 */
FLASH_START = 0x00010200;
FLASH_LENGTH = 0x000EFE00;

INCLUDE fsp_gen.ld
