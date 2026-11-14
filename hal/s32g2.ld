/* s32g2.ld - link map for the NXP S32G2: wolfBoot is the Boot ROM's
 * application image, copied into the 8 MB system SRAM and entered at
 * WOLFBOOT_ORIGIN (0x340A0000) at EL3. Everything stays below 0x343F0000:
 * inside SRAM controller 0's range, which the startup code ECC-initialises,
 * and below the Boot ROM's reservation at 0x343FF000.
 */
MEMORY
{
    SRAM_MEM(rwx): ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = 0x343F0000 - @WOLFBOOT_ORIGIN@
}
ENTRY(_vector_table);

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.boot*))
        *(.text*)
        *(.rodata*)
        *(.note.*)
        . = ALIGN(4);
        _end_text = .;
    } > SRAM_MEM
    .ARM.exidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > SRAM_MEM

    .data :
    {
        PROVIDE(_stored_data = .);
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        /* Loaded data too: left to follow _end_data, the table would sit
         * in the range the startup clears, and a symbol reached through it
         * (a weak one, for instance) would read as zero. */
        *(.got*)
        /* The SRAM controller initialises 512-byte granules; the startup
         * clears from here, so nothing loaded may share this granule. */
        . = ALIGN(512);
        _end_data = .;
    } > SRAM_MEM

    /* From here on the SRAM was not written by the Boot ROM copy, so the
     * startup code must run the SRAM controller's initialisation over it
     * before any store (an ECC read-modify-write on uninitialised SRAM
     * faults). _end_data to END_STACK is that range. */
    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
    } > SRAM_MEM

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        . += 0x40000;      /* 256 KB: the ECC verify, FIT parse and inflate run here */
        . = ALIGN(16);
        END_STACK = .;
        _end = .;
    } > SRAM_MEM
    . = ALIGN(4);
}
