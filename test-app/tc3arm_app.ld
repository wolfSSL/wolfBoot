/* tc3arm_bootloader.ld */
/* Example linker script for TC3xx ARM bootloader */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

/* This is defined in tc3arm_crt.c */
ENTRY(tc3arm_crt_entry)

/* Include generic TC3XX memory */
INCLUDE tc3_mem.ld

/* Reallocate memory based on device */
INCLUDE tc3_layout.ld

/* Reallocate ARM memory */
INCLUDE tc3arm_layout.ld

MEMORY {
    TEST_APP (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
}

/* CRT Boot Code Start */
REGION_ALIAS("BOOT",    TEST_APP)

/* Code and data */
REGION_ALIAS("CODE",     TEST_APP)
REGION_ALIAS("RODATA",   TEST_APP)
REGION_ALIAS("DATA", 	 arm_sram)
REGION_ALIAS("RAM_CODE", arm_sram)

ASSERT(ORIGIN(TEST_APP) >= ORIGIN(arm_app_flash), "test app not located within arm_app_flash")
ASSERT((ORIGIN(TEST_APP) + LENGTH(TEST_APP)) <= (ORIGIN(arm_app_flash) + LENGTH(arm_app_flash)), "test app memory region overflows arm_app_flash")

/* Pickup the common sections */
INCLUDE tc3arm_sections.ld

PROVIDE(_start_text = ADDR(.tc3arm_crt_boot));
