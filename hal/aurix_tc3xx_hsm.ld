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

/* CRT Boot Code Start */
REGION_ALIAS("BOOT",    arm_bl_flash)

/* Code and data */
REGION_ALIAS("CODE",     arm_bl_flash)
REGION_ALIAS("RODATA",   arm_bl_flash)
REGION_ALIAS("DATA", 	 arm_sram)
REGION_ALIAS("RAM_CODE", arm_sram)

/* Pickup the common sections */
INCLUDE tc3arm_sections.ld
