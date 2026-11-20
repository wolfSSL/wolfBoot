/* Legacy BIOS/MBR boot. wolfboot.bin is the MBR sector (0x7C00) followed by
 * the stage the MBR loads at 0x7E00. Everything must stay below the Linux
 * real-mode setup area at 0x90000. */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_mbr_start)

STAGE_BASE  = 0x7E00;
STAGE_LIMIT = 0x90000;
STACK_SIZE  = 0x4000;

SECTIONS
{
    .mbr 0x7C00 :
    {
        KEEP(*(.mbr))
    }

    .text16 STAGE_BASE :
    {
        _start_wolfboot = .;
        KEEP(*(.text16))
        KEEP(*(.data16))
    }
    ASSERT(. <= 0x10000, "real-mode code and data must stay below 64 KB")

    .text :
    {
        _start_text = .;
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        _start_data = .;
        *(.data*)
        _end_data = .;
    }
    _stage_load_end = .;

    .bss (NOLOAD) : ALIGN(16)
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        _end_bss = .;
        __bss_end__ = .;
        . += STACK_SIZE;
        _stack_top = .;
    }
    _end_wb = .;
    _end = .;

    _stage_sectors = (_stage_load_end - STAGE_BASE + 511) / 512;
    ASSERT(_end_wb <= STAGE_LIMIT, "wolfBoot overlaps the Linux real-mode area")

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}
