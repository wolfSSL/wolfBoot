/* wolfBoot linker script for the RealTek RTL8735B (e.g. AmebaPro2 EVB).
 *
 * wolfBoot is staged into SRAM by the RealTek bootloader through a RealTek
 * RAM_FUNCTION_START_TABLE. This script places that table, its image
 * signature, and wolfBoot's text/data/bss at the fixed SRAM addresses the
 * bootloader expects (cribbed from the proven zephyr_shim/shim.ld), and
 * exports the wolfBoot partition symbols templated by the Makefile.
 *
 * The image is loaded (not XIP) into SRAM, so .data is placed in SRAM and
 * _stored_data == _start_data; isr_reset's flash->RAM .data copy is then a
 * no-op while BSS zeroing still runs.
 */

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x20106200, LENGTH = 0x39E00   /* up to ~0x20140000 */
}

ENTRY(isr_reset)

SECTIONS
{
    /* RealTek RAM start-table at the fixed address the bootloader reads. */
    .ram.func.table 0x20106200 :
    {
        __ram_start_table_start__ = .;
        KEEP(*(.ram.func.table))
        __ram_start_table_end__ = .;
    } > SRAM

    /* 10-byte "AmebaPro2\xff" image signature at its fixed address. */
    .ram.img.signature 0x201062f0 :
    {
        KEEP(*(.ram.img.signature))
    } > SRAM

    .ram.code_text 0x20106300 :
    {
        _start_text = .;
        KEEP(*(.ram.code_text))     /* RamStartFun trampoline */
        KEEP(*(.isr_vector))        /* wolfBoot ARM vector table */
        *(.text*)
        *(.rodata*)
        *(.keystore*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > SRAM

    _stored_data = .;
    .data :
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        KEEP(*(.data*))
        . = ALIGN(4);
        _end_data = .;
    } > SRAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
    } > SRAM

    /* Scratch the RealTek bootloader writes through the RAM start-table pointers
     * (e.g. the SPIC adaptor it initialized). It MUST stay outside the
     * _start_bss.._end_bss range that isr_reset (src/boot_arm.c) zeroes, so the
     * bootloader-provided data survives into hal_init(). */
    .ram.noinit (NOLOAD) :
    {
        . = ALIGN(32);
        KEEP(*(.ram.noinit))
        . = ALIGN(4);
    } > SRAM

    _end = .;

    /* wolfBoot is bare-metal with no C++/unwinding, so the ARM exception-index
     * and unwind tables are dead -- discard them (they would otherwise emit an
     * empty .ARM.exidx with an unset sh_link). */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.ARM.attributes)
        *(.comment)
        *(.eh_frame*)
    }
}

END_STACK = 0x20140000;

/* Partition symbols templated by the Makefile (raw external SPI NOR offsets). */
_wolfboot_partition_boot_address   = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size           = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address   = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
