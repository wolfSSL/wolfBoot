MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 64K /* AXISRAM1 */
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.init)
        *(.fini)
        *(.text*)
        KEEP(*(.rodata*))
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    /* The app runs in place from the XSPI window at 0x70xxxxxx while its
     * .ramcode and .data are relocated into AXISRAM at 0x24000000. That
     * ~1.28 GB delta is outside the signed range a PREL31 relocation can
     * encode, so the unwind tables cannot be linked; nothing in the app
     * throws or unwinds, so discard them. */
    /DISCARD/ :
    {
      *(.ARM.exidx*)
      *(.ARM.extab*)
    }

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    .bss :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > RAM
}

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + LENGTH(RAM));
