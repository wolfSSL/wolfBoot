OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_vector_table)

MEMORY
{
    /*The flash address range on LS1028A RDB is 0x20000000 - 0x23FFFFFF.*/
    FLASH (rx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@

    /* DDR4 - 2GB */
    DRAM (rwx) : ORIGIN = 0x80001000 , LENGTH = 0xBFFFFFFF

    /* OCRAM 128K for startup RAM */
    OCRAM (rwx) : ORIGIN = 0x18000000, LENGTH = 128K
}

SECTIONS
{
    PROVIDE (_DDR_ADDRESS = 0x80001000);
    PROVIDE (_OCRAM_ADDRESS = 0x18000000);
    PROVIDE (_FLASH_ADDRESS = @WOLFBOOT_ORIGIN@);
    PROVIDE (_CORE_NUMBER = 0);
    PROVIDE (_MEMORY_SIZE = LENGTH(OCRAM));
    PROVIDE (_FLASH_SIZE = LENGTH(FLASH));
    PROVIDE (_STACK_SIZE = 20K);

    .boot :
    {
        PROVIDE(_vector_table = .);
        ._vector_table = .;
        . = ALIGN(0x800);
        KEEP(*(.vector_table))
        KEEP(*(.boot*))
    } > OCRAM


    /* Read-only sections, merged into text segment: */
    .interp         : { *(.interp) }
    .note.gnu.build-id : { *(.note.gnu.build-id) }
    .hash           : { *(.hash) }
    .gnu.hash       : { *(.gnu.hash) }
    .dynsym         : { *(.dynsym) }
    .dynstr         : { *(.dynstr) }
    .gnu.version    : { *(.gnu.version) }
    .gnu.version_d  : { *(.gnu.version_d) }
    .gnu.version_r  : { *(.gnu.version_r) }
    .rela.init      : { *(.rela.init) }
    .rela.fini      : { *(.rela.fini) }
    .rela.rodata    : { *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*) }
    .rela.data.rel.ro   : { *(.rela.data.rel.ro .rela.data.rel.ro.* .rela.gnu.linkonce.d.rel.ro.*) }
    .rela.data      : { *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*) }
    .rela.tdata     : { *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*) }
    .rela.tbss      : { *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*) }
    .rela.ctors     : { *(.rela.ctors) }
    .rela.dtors     : { *(.rela.dtors) }
    .rela.got       : { *(.rela.got) }
    .rela.bss       : { *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*) }

    .text :
    {
        _start_text = .;
        *(.text*)
        *(.note.*)
        . = ALIGN(8);
        _end_text = .;
    } > OCRAM

    .rodata :
    {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(8);
        _rodata_end = .;
    } > OCRAM

    PROVIDE(_stored_data = .);

    .data :
    {
        _start_data = .;
        KEEP(*(.data .data.* .gnu.linkonce.d.*))
        . = ALIGN(8);
        KEEP(*(.ramcode))
        . = ALIGN(8);
        _end_data = .;
    } > OCRAM

    .bss :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > OCRAM

    . = ALIGN(16);
    .stack :
    {
        _start_stack = .;
        . = . + _STACK_SIZE;
        _end_stack = .;
    } > OCRAM

    PROVIDE(_stack_base = .);
}