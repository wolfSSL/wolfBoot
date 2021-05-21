
/* see SPNU118U - January 2018  section 8.5 for linker syntax */
--retain="*(.isr_vector)"
--entry_point=isr_reset

MEMORY
{
    FLASH (RX) : ORIGIN = 0x00000000, LENGTH = 131072
    RAM (RWX) : ORIGIN = 0x08000000, LENGTH = 0x00080000
}


SECTIONS
{
    .text :
    {
        _start_text = .;
        *(.isr_vector)
        *(.text*)
        *(.const*)
        . = ALIGN(4);
        _end_text = .;
        _stored_data = .;
    } > FLASH /* FLASH0 | FLASH1 */

    .data :
    {
        _start_data = .;
        *(.data*)
        *(.ramcode)
        -l F021_API_CortexR4_BE_L2FMC_V3D16.lib (.text)
        _end_data = .;
    } load=FLASH, run=RAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > RAM

    .stack (NOLOAD) :
    {
        END_STACK = .;
        /* used for dynamic allocation */
        /* *(.sysmem*) */
    } > RAM (HIGH)

}
