MEMORY
{
    /* Secure world. On M2354 the secure view is the base address and the
     * non-secure view is base + 0x10000000, the opposite of the NXP parts.
     * FLASH ends where the NSC region begins; the keyvault is unused here
     * and its address doubles as that boundary, per the mcxn/mcxw pattern.
     */
    FLASH (rx) :          ORIGIN = 0x00000000, LENGTH = @WOLFBOOT_KEYVAULT_ADDRESS@ - @ARCH_FLASH_OFFSET@
    /* SRAM bank 0 only: banks 1 and 2 are unclocked at reset. The secure
     * world is attributed 96 KB via SCU->SRAMNSSET, but wolfBoot itself
     * links inside bank 0 so its stack is valid before hal_init() runs. */
    RAM (rwx) :           ORIGIN = 0x20000000, LENGTH = 0x00008000 /* 32 KB, bank 0 */
    FLASH_NSC (rx) :      ORIGIN = @WOLFBOOT_NSC_ADDRESS@, LENGTH = @WOLFBOOT_NSC_SIZE@
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    /* ARM exception index. ".edidx" is the wolfBoot convention for this
     * output section; it is empty for bare-metal C with no unwinding. */
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    .keystore :
    {
        . = ALIGN(4);
        KEEP(*(.keystore*))
    } > FLASH

    .gnu.sgstubs ORIGIN(FLASH_NSC) :
    {
        /* 1 KB reserved at the start of the NSC region before the secure
         * gateway stubs, matching hal/lpc55s69.ld and hal/stm32l5.ld. */
        . += 0x400;
        . = ALIGN(4);
        *(.gnu.sgstubs*) /* Secure Gateway Stubs */
        . = ALIGN(4);
    } > FLASH_NSC

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
    } > RAM
    . = ALIGN(4);
}

END_STACK = ORIGIN(RAM) + LENGTH(RAM);
