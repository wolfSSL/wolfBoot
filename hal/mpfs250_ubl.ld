/* PolarFire SoC MPFS250 boot mode 2 User Boot Loader (UBL) linker script
 *
 * The stub lives in authenticated sNVM pages.  The System Controller copies
 * it to the LIM address in the UBL header (taken from this ELF) and releases
 * all five harts at _reset, so VMA == LMA and nothing is relocated at start.
 *
 * Memory region:
 *   LIM - L2 Loosely Integrated Memory at 0x08000000.  Only the ways not
 *         enabled as cache back it; at reset that is almost all of it.
 */

OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
    LIM (rwx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
}

PROVIDE(STACK_SIZE = @STACK_SIZE@);

/* sNVM holds 221 pages of 236 bytes for the UBL, 60 of which go to the UBL
 * header the programmer prepends, so the image copied by the System
 * Controller (.init + .text + .data, .bss is NOLOAD) is capped here. */
MPFS_UBL_IMAGE_MAX = 221 * 236 - 60;

ASSERT(ORIGIN(LIM) >= 0x08000000 && ORIGIN(LIM) + LENGTH(LIM) <= 0x081E0000,
       "UBL must lie inside the LIM 0x08000000..0x081DFFFF")

SECTIONS
{
    .init : ALIGN(0x10)
    {
        _start_text = .;
        KEEP(*(.init))
        . = ALIGN(0x10);
    } > LIM

    .text : ALIGN(0x10)
    {
        _start_vector = .;
        KEEP(*(.isr_vector))
        KEEP(*(.trap_vector))
        . = ALIGN(0x10);
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(8);
        _end_text = .;
    } > LIM

    .data : ALIGN(0x10)
    {
        _start_data = .;
        *(.data*)
        . = ALIGN(4);
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        KEEP(*(.keystore*))
        . = ALIGN(8);
        _end_data = .;
    } > LIM

    .bss (NOLOAD) : ALIGN(0x10)
    {
        _start_bss = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
        _end = .;
    } > LIM
}

/* Nothing is copied at start: the System Controller places the whole image. */
_stored_data = ADDR(.data);

PROVIDE(_start_heap = _end);
PROVIDE(_end_stack = ORIGIN(LIM) + LENGTH(LIM));
PROVIDE(__global_pointer$ = _global_pointer);

ASSERT(_start_text == ORIGIN(LIM),
    "ERROR: UBL entry must be the first byte of the image")
ASSERT(_end_data - _start_text <= MPFS_UBL_IMAGE_MAX,
    "ERROR: UBL image exceeds the sNVM budget (221 pages)")
ASSERT(_end <= _end_stack - STACK_SIZE,
    "ERROR: UBL .bss overlaps the stack")
