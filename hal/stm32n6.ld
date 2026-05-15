/* wolfBoot linker script for STM32N6
 *
 * Boot ROM copies FSBL into AXISRAM2 at WOLFBOOT_ORIGIN (0x34180400).
 * When TZEN=1 the build also passes -mcmse / --cmse-implib, so the
 * linker emits an (empty) .gnu.sgstubs section even though no
 * __cmse_nonsecure_entry functions are exposed. The section is placed
 * inline at the end of .text (same FLASH region as wolfBoot's code) so
 * any veneer trampoline -- if a future build adds NSC entry points --
 * stays within Thumb branch range of the secure code that calls it.
 * This matches ST's Template_FSBL_XIP linker layout.
 *
 * Note: an NSC veneer reachable from the non-secure app requires the
 * veneer's link address to lie inside an SAU-NSC region in IDAU-NS
 * space (i.e. an alias 0x2418xxxx of AXISRAM2). On the STM32N6 the
 * Thumb branch reach between AXISRAM2's secure (0x3418xxxx) and NS
 * (0x2418xxxx) aliases is ~256 MB, which the linker cannot resolve
 * directly. Cross-alias NSC will need an explicit trampoline path
 * (e.g. via a function pointer register) when WOLFCRYPT_TZ=1 lands.
 */

MEMORY
{
    FLASH (rwx) : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = @BOOTLOADER_PARTITION_SIZE@
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

    .ARM.exidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    /* Explicit ALIGN() address expression on the section name is
     * required by lld -- it errors "no address assigned to the veneers
     * output section" when .gnu.sgstubs is placed via implicit `:`
     * even though GNU ld accepts the latter. Matches the explicit
     * address (ORIGIN(FLASH_NSC)) pattern in hal/stm32h5.ld /
     * stm32u5.ld / stm32l5.ld. */
    .gnu.sgstubs ALIGN(32) :
    {
        _sNSCVeneer = .;
        *(.gnu.sgstubs*)
        _eNSCVeneer = .;
        . = ALIGN(4);
    } > FLASH

    _stored_data = .;
    .data :
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > FLASH

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
    } > FLASH
    . = ALIGN(4);
}

END_STACK = ORIGIN(FLASH) + LENGTH(FLASH);
