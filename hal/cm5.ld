/* cm5.ld
 *
 * Linker script for the Raspberry Pi Compute Module 5 (BCM2712, Cortex-A76).
 *
 * The EEPROM bootloader loads the image named by kernel= in config.txt to
 * kernel_address (0x200000) and its BL31 enters it in place at EL2, so
 * wolfBoot links at 0x200000. RAM below 0x80000 belongs to that BL31 and must
 * not be used. DDR_MEM ends at 0x3c000000, inside the first RAM bank of every
 * CM5 variant. The initial stack grows down from 0x200000.
 */
MEMORY
{
    DDR_MEM(rwx): ORIGIN = 0x00200000, LENGTH = 0x3be00000
}
ENTRY(_vector_table);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        KEEP(*(.keystore))
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > DDR_MEM
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > DDR_MEM

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DDR_MEM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > DDR_MEM
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(__end__ = .);
    PROVIDE(_heap_start = .);
}

END_STACK = _start_text;
/* RAM-boot staging addresses: the firmware loads wolfBoot plus the
 * concatenated signed app to 0x200000, so the app lands at 0x200000 + its
 * bin-assemble offset. */
kernel_addr  = 0x02C0000;   /* 0x200000 load + 0xC0000 bin-assemble offset */
update_addr  = 0x12C0000;
dts_addr     = 0x0280000;
kernel_load_addr = 0x20000000;
dts_load_addr = 0x21000000;

_wolfboot_partition_boot_address = kernel_addr;
_wolfboot_partition_update_address = update_addr;

/* An overflow would not fail the link; it would corrupt the staged DTB or the
 * unverified application at runtime. */
ASSERT(_end <= dts_addr,
    "cm5: image overruns dts_addr - raise dts_addr/kernel_addr in hal/cm5.ld")
