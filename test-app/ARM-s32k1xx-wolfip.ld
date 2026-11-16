/*
 * Linker script for the wolfBoot test application on NXP S32K148/MCXE247 with wolfIP
 *
 * The application runs from the boot partition after the wolfBoot header.
 * RAM is SRAM_U. The wolfIP stack state (wolfip.o .bss: sockets and their
 * buffers) goes in SRAM_L, which the ENET DMA and eDMA reach through its
 * backdoor port. Both are outside the code cache while it is disabled (the
 * default); enabling the cache for SRAM_L would break the DMA coherency.
 * The hottest code (memcpy and wolfIP's checksum loop) runs from SRAM_L too
 * (.ramcode_l), so the core fetches it over the code bus while its data
 * accesses use the system bus.
 *
 * SRAM is ECC protected and holds no valid ECC after power-on. With
 * APP_RAM_ECC_INIT the reset handler clears [_start_ecc_ram, _end_ecc_ram),
 * SRAM_L and SRAM_U including the heap and stack, with word stores before the
 * stack is used. This keeps the ECC valid and the ERM free of error events,
 * and is also what zeroes the SRAM_L section: it is not part of .bss.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM_L (rwx) : ORIGIN = 0x1FFE0000, LENGTH = 0x20000  /* 128KB SRAM_L */
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x1F000  /* 124KB SRAM_U */
}

SECTIONS
{
    /* Hot code run from SRAM_L, which the core fetches over its code bus in
     * parallel with its data accesses (code in SRAM_U shares the system bus
     * with the data). It comes first so that these input sections are taken
     * before the .text* rule below; its load image follows .edidx in flash
     * and is copied at reset (APP_RAMCODE_L). */
    .ramcode_l : AT (_stored_ramcode_l)
    {
        . = ALIGN(4);
        _start_ramcode_l = .;
        KEEP(*string.o(.ramcode))
        *wolfip.o(.text.csum_add)
        . = ALIGN(4);
        _end_ramcode_l = .;
    } > RAM_L

    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .keystore :
    {
        . = ALIGN(4);
        KEEP(*(.keystore*))
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_ramcode_l = ALIGN(4);

    _stored_data = _stored_ramcode_l + SIZEOF(.ramcode_l);

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    /* wolfIP stack state, zero-filled by the APP_RAM_ECC_INIT clear */
    .bss_sram_l (NOLOAD) :
    {
        . = ALIGN(16);
        *wolfip.o(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > RAM_L

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end = .;
        _end_bss = .;
        __bss_end__ = .;
    } > RAM
}

/* Partition info for libwolfboot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack and heap indicators */
_start_heap = _end;
_end_stack = ORIGIN(RAM) + LENGTH(RAM);

/* RAM cleared at reset with APP_RAM_ECC_INIT */
_start_ecc_ram = ORIGIN(RAM_L);
_end_ecc_ram = ORIGIN(RAM) + LENGTH(RAM);

/* Keep room above .bss for the stack and the heap used by printf */
ASSERT(_end_stack - _end >= 0x2000, "not enough RAM left for the stack")
