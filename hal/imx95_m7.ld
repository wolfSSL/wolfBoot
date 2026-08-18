/* Linker script for wolfBoot on the i.MX95 Cortex-M7.
 *
 * Linked for the *core* view of TCM. The Linux remoteproc driver loads through
 * the system view (ITCM 0x203C0000, DTCM 0x20400000 at the default 256K/256K
 * TCM split) and imx_rproc translates between the two. Linking for the system
 * view produces an image that loads cleanly and faults on the first fetch.
 *
 * Sizes assume M7_CFG[TCM_SIZE] = 000b (256 KiB ITCM + 256 KiB DTCM), which is
 * the reset default. The System Manager on the M33 owns that field.
 */

MEMORY
{
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    DTCM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        . = ALIGN(8);
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > ITCM

    /* Consumed by the remoteproc ELF loader. Not required to start the core,
     * but RPMsg needs it, so keep it even when empty. */
    .resource_table :
    {
        . = ALIGN(8);
        KEEP(*(.resource_table*))
        . = ALIGN(8);
    } > ITCM

    /* Public keys emitted by the keytool (KEYSTORE_SECTION). Placed
     * explicitly so it cannot land after _stored_data as an orphan. */
    .keystore :
    {
        . = ALIGN(8);
        KEEP(*(.keystore*))
        . = ALIGN(8);
    } > ITCM

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > ITCM

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > DTCM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > DTCM
    . = ALIGN(4);
}

END_STACK = ORIGIN(DTCM) + LENGTH(DTCM);
