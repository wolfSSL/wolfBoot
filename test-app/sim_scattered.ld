ENTRY(main)

SECTIONS
{
    . = 0x100000;
    __r1_start = .;
    .r1text : { *(.r1text) }
    .r1data : { *(.r1data) }
    __r1_end = .;

    . = 0x140000;
    __r2_start = .;
    .r2text : { *(.r2text) }
    .r2data : { *(.r2data) }
    __r2_end = .;

    . = 0x180000;
    __r3_start = .;
    .r3text : { *(.r3text) }
    .r3data : { *(.r3data) }

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    __r3_end = .;

    PROVIDE(__image_start = 0x100000);
    PROVIDE(__image_end = .);
}
