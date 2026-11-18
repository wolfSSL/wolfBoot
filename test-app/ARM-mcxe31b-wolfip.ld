/*
 * Linker script for the wolfBoot test application on NXP MCXE31B with wolfIP
 *
 * The application runs from the boot partition after the wolfBoot header.
 *
 * RAM is the 64 KB CM7_0 DTCM (0x20000000), which wolfBoot has already
 * ECC-initialized: data, the RAM_CODE flash routines, .bss, heap and stack.
 * The core does not cache the DTCM and the EMAC DMA reaches it through its
 * backdoor port, so the EMAC driver's descriptor rings and frame buffers
 * (its .bss) live there with no cache maintenance.
 *
 * The wolfIP stack state (sockets and their buffers) and the TFTP receive
 * buffer go in the 320 KB SRAM (0x20400000), which the core caches and only
 * the core uses. SRAM is ECC protected and holds no valid ECC after
 * power-on. With APP_RAM_ECC_INIT the reset handler clears
 * [_start_ecc_ram, _end_ecc_ram) with 64-bit stores (SRAM ECC words are 64
 * bits) before anything reads it; that clear is also what zeroes the SRAM
 * section: it is not part of .bss.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x10000  /* 64 KB DTCM */
    SRAM (rw)   : ORIGIN = 0x20400000, LENGTH = 0x50000  /* 320 KB SRAM */
}

SECTIONS
{
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

    /* wolfIP and network test state, zero-filled by the APP_RAM_ECC_INIT
     * clear. It comes before .bss so that these input sections are taken
     * before the .bss* rule below. */
    .bss_sram (NOLOAD) :
    {
        . = ALIGN(8);
        *wolfip.o(.bss .bss.* COMMON)
        *wolftftp.o(.bss .bss.* COMMON)
        *wolfip_tftp_test.o(.bss .bss.* COMMON)
        . = ALIGN(8);
    } > SRAM

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

/* Partition info for libwolfboot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack and heap indicators */
_start_heap = _end;
_end_stack = ORIGIN(RAM) + LENGTH(RAM);

/* SRAM cleared at reset with APP_RAM_ECC_INIT */
_start_ecc_ram = ORIGIN(SRAM);
_end_ecc_ram = ORIGIN(SRAM) + LENGTH(SRAM);

/* The EMAC driver's buffers must be in the DTCM */
ASSERT(_end_bss <= ORIGIN(RAM) + LENGTH(RAM), "DTCM overflow")
/* Keep room above .bss for the stack and the heap used by printf */
ASSERT(_end_stack - _end >= 0x2000, "not enough RAM left for the stack")
