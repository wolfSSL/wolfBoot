/* Linker script for the wolfBoot test app on the i.MX95 Cortex-M7.
 *
 * The generic ARM.ld is not usable here: it gives RAM only 16 KiB and defines
 * no heap symbols, and wolfCrypt's test/benchmark need both a real heap (RSA
 * and ECC key material) and syscalls.c's _sbrk, which references
 * _Min_Heap_Size.
 *
 * Code runs from the BOOT partition in the M7's reserved DDR window; data and
 * heap live in DTCM (core view 0x20000000, 256 KiB at the reset-default
 * M7_CFG[TCM_SIZE] split). Keeping the working set in TCM is the point of the
 * exercise -- these numbers are meant to characterise the real-time core, not
 * DDR latency.
 */

_Min_Heap_Size  = 0x00018000; /* 96 KiB - RSA/ECC test vectors and key structs */
_Min_Stack_Size = 0x00008000; /* 32 KiB */

MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 256K /* DTCM, core view */
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
        _end = .;
    } > RAM

    /* Reserve heap and stack explicitly so the link fails loudly if DTCM is
     * over-committed, rather than overflowing at run time. */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE ( _start_heap = . );
        . = . + _Min_Heap_Size;
        . = ALIGN(8);
    } > RAM

    .stack (NOLOAD) :
    {
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
