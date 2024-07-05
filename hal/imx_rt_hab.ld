/* Specify the memory areas */
MEMORY
{
  FLASH(rx)  : ORIGIN = @ARCH_FLASH_OFFSET@ + 0x2000, LENGTH = @BOOTLOADER_PARTITION_SIZE@
  RAM(rwx)   : ORIGIN = 0x20000000, LENGTH = 0x0001FFF0
}

/* Define output sections */
SECTIONS
{
    .text :
    {
        _start_text = @ARCH_FLASH_OFFSET@;
        KEEP(*(.isr_vector))
        . = ALIGN(0x8);
        *(.text*)
        *(.rodata*)
        *(.glue_7)               /* glue arm to thumb code */
        *(.glue_7t)              /* glue thumb to arm code */
        *(.eh_frame)
        KEEP (*(.init))
        KEEP (*(.fini))
        . = ALIGN(4);
        _end_text = .;
    } > FLASH
    .ARM.extab :
    {
      *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH
    .ARM :
    {
      __exidx_start = .;
      *(.ARM.exidx*)
      __exidx_end = .;
    } > FLASH
    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
        KEEP(*(.data*))
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
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
