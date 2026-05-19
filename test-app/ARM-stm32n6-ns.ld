/* Non-secure test-app linker for STM32N6 (TZEN=1)
 *
 * FLASH uses the XSPI2 NOR secure-alias address (0x7xxxxxxx). On N6
 * the IDAU defers to SAU for this range, so the NS app's vector table
 * and code execute from 0x70020400+ where SAU classifies the region
 * as Non-Secure. Mirrors ST's Template_Isolation_XIP reference, which
 * links the NS app at 0x70180400 rather than the 0x9xxxxxxx alias.
 * RAM uses the NS alias of AXISRAM1 (0x24010000), kept below
 * AXISRAM2's NS alias (0x24180000) which is reserved for wolfBoot.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@,
                 LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx) : ORIGIN = 0x24010000, LENGTH = 64K
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
}

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack  = ORIGIN(RAM) + LENGTH(RAM));
