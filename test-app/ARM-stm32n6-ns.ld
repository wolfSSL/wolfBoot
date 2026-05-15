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

    /* Drop ARM EABI unwind tables. clang emits .ARM.exidx PREL31
     * entries for every function including the libwolfboot.o
     * RAMFUNCTIONs whose VMA is in RAM (0x2401xxxx) -- the PREL31
     * delta from an exidx entry in FLASH (0x7018xxxx) to those
     * functions is ~1.27 GB, beyond PREL31's signed ~1.07 GB range,
     * and lld errors at link time. arm-none-eabi-gcc doesn't emit
     * these for embedded ARM by default; clang for ARM EABI does
     * regardless of -fno-unwind-tables. The test-app is bare-metal
     * and never invokes stack unwinding, so discard the sections
     * outright. */
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
