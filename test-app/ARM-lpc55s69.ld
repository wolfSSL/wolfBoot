_Min_Heap_Size  = 0x00000100; /* minimal heap (not using malloc) */
_Min_Stack_Size = 0x00008000; /* required amount of stack */

MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx) : ORIGIN = 0x20020000, LENGTH = 160K
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
    } > RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    PROVIDE ( _start_heap = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
    PROVIDE ( END_STACK = . );
    PROVIDE ( _end_stack = . );
  } > RAM
}

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
