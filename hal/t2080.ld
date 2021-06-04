OUTPUT_ARCH( "powerpc" )

ENTRY( _reset )
RESET_ADDR = 0x00000000effffffc;

/* On initial start, only a limited space(4k) is accessible. Code here
 * bootstraps to enable access to other needed address spaces
 */
BOOTSTRAP_TLB = 0x00000000effff000;

START = ##WOLFBOOT_ORIGIN##;
FLASH_LENGTH = ##WOLFBOOT_PARTITION_BOOT_ADDRESS##;

PHDRS
{
    text PT_LOAD;
    bss  PT_LOAD;
}

MEMORY
{
    FLASH (rx) : ORIGIN = START, LENGTH = FLASH_LENGTH
    RAM (rwx) : ORIGIN = 0xfdd00000, LENGTH = 0x00002000
}

SECTIONS
{

    .text :
    {
        *(.text*)
        *(.rodata*)
        *(.sdata*)
    } > FLASH

    . = ALIGN(4);

    .boot BOOTSTRAP_TLB :
    {
        KEEP(*(.boot))
    } :text = 0xffff

    .reset RESET_ADDR :
    {
        KEEP(*(.reset))
    } :text = 0xffff
    . = 0xeffffffc + 0x4;

    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

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
    } > RAM

}
