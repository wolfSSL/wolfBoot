OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

/* App is staged by wolfBoot to DDR at WOLFBOOT_LOAD_ADDRESS=0x10000000.
 * Stack carved out from the half-MB region just above. */
MEMORY
{
    DDR_MEM(rwx)  : ORIGIN = 0x10000000, LENGTH = 0x00080000  /* 512 KB code/data/bss */
    STACK_MEM(rw) : ORIGIN = 0x10080000, LENGTH = 0x00080000  /* 512 KB stack */
}

/* The test app has no separate startup file - wolfBoot's do_boot bx's
 * directly to WOLFBOOT_LOAD_ADDRESS, which is the very first byte of the
 * raw .bin image. To guarantee that byte is the first instruction of
 * main() (regardless of toolchain ordering or LTO), we KEEP main's
 * function section at the start of .text. The ENTRY directive only
 * affects ELF metadata; the .bin payload starts wherever the linker
 * places .text first. */
ENTRY(main)
SECTIONS
{
    .text : AT (ORIGIN(DDR_MEM)) {
        _start_text = .;
        /* main() is annotated with __attribute__((section(".boot_entry")))
         * in app_zynq7000.c so it lands here, at offset 0 of the raw .bin
         * payload (= WOLFBOOT_LOAD_ADDRESS). KEEP() prevents --gc-sections
         * from dropping it. */
        KEEP(*(.boot_entry))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        *(.glue_7)
        . = ALIGN(4);
        *(.eh_frame)
        . = ALIGN(4);
        _end_text = .;
    }

    . = ALIGN(4);
    .dummy : {
        _edummy = .;
    }

    .data : AT (LOADADDR(.dummy)) {
        _start_data = .;
        *(.vectors)
        *(.data)
        _end_data = .;
    }

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _start_bss = .;
        *(.bss)
        _end_bss = .;
    }
}
_romsize = _end_data - _start_text;
_sramsize = _end_bss - _start_text;
END_STACK = _start_text;
_stack_top = ORIGIN(STACK_MEM) + LENGTH(STACK_MEM);
end = .;
