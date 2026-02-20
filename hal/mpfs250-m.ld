/* PolarFire SoC MPFS250 M-Mode Linker Script for wolfBoot
 *
 * This linker script is for running wolfBoot in Machine Mode (M-mode)
 * directly from eNVM, executing from L2 SRAM.
 *
 * Boot flow:
 *   1. CPU starts at eNVM reset vector (0x20220100)
 *   2. Startup code in eNVM copies main code to L2_SCRATCH
 *   3. Jumps to L2_SCRATCH for execution
 *
 * The first 0x100 bytes of eNVM are reserved for the boot ROM secure boot
 * meta information added by mpfsBootmodeProgrammer.
 *
 * Memory regions:
 *   FLASH_ENVM  - Embedded NVM (128KB - 0x100 for header)
 *   L2_SCRATCH  - L2 Scratchpad SRAM (256KB) - execution and data
 */

OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

MEMORY
{
    /* The first 0x100 bytes of eNVM are used for boot ROM secure boot meta information
     * This offset is added by mpfsBootmodeProgrammer (bootmode 1) */
    FLASH_ENVM (rx)   : ORIGIN = 0x20220100, LENGTH = 128k - 0x100

    /* L2 Scratchpad SRAM - 256KB available
     * Used for code execution, data, and stack in M-mode
     * Address range: 0x0A000000 - 0x0A03FFFF */
    L2_SCRATCH (rwx)  : ORIGIN = @WOLFBOOT_ORIGIN@, LENGTH = 256k
}

/* Stack size for the boot hart (E51 in M-mode)
 * ECC384 signature verification with sp_int needs significant stack
 * for big number temporaries and point multiplication */
PROVIDE(STACK_SIZE = 64k);

SECTIONS
{
    /*
     * Reset vector and early initialization code
     * This section MUST be in eNVM (VMA = LMA) since CPU starts here.
     * It copies the main code to L2_SCRATCH and jumps there.
     */
    .init : ALIGN(0x10)
    {
        _start_text = .;
        KEEP(*(.init))
        . = ALIGN(0x10);
    } > FLASH_ENVM

    /*
     * Main code section - runs from L2_SCRATCH, stored in FLASH_ENVM
     * The .init code will copy this section to L2_SCRATCH before jumping here.
     */
    .text : ALIGN(0x10)
    {
        _start_text_sram = .;
        _start_vector = .;
        KEEP(*(.isr_vector))
        KEEP(*(.trap_vector))
        . = ALIGN(0x10);
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        _end_text = .;
    } > L2_SCRATCH AT > FLASH_ENVM

    /* Provide load address for copying from flash */
    _stored_text = LOADADDR(.text);
    _stored_data = LOADADDR(.data);

    /* Initialized data section */
    .data : ALIGN(0x10)
    {
        _start_data = .;
        KEEP(*(.ramcode*))
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
        /* Global pointer is set to .sdata + 0x800 for efficient access
         * to small data using gp-relative addressing (+/- 2KB range) */
        _global_pointer = . + 0x800;
        *(.sdata*)
        . = ALIGN(4);
        /* Public key store - must be in a copied section so it's available
         * in L2 SRAM after startup copies .data from eNVM */
        KEEP(*(.keystore*))
        . = ALIGN(4);
        _end_data = .;
    } > L2_SCRATCH AT > FLASH_ENVM

    /* Uninitialized data section (cleared to zero on startup) */
    .bss (NOLOAD) : ALIGN(0x10)
    {
        _start_bss = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > L2_SCRATCH
}

/* Heap starts after BSS (between _end and stack) */
PROVIDE(_start_heap = _end);

/* Stack configuration for multi-hart boot
 * Memory layout at end of L2_SCRATCH:
 *   [code/data/bss/heap] ... [secondary stacks] [main stack]
 *
 * Stack sizes (defined in config or header):
 *   STACK_SIZE_PER_HART = 8192 (8KB per hart)
 *   STACK_SIZE = 64K (64KB for main hart E51)
 *
 * Total stack area: STACK_SIZE + 4 * STACK_SIZE_PER_HART = 48KB
 */
PROVIDE(STACK_SIZE_PER_HART = 8192);

/* End of L2 scratchpad */
PROVIDE(_l2_scratch_end = ORIGIN(L2_SCRATCH) + LENGTH(L2_SCRATCH));

/* Main hart (E51) stack at very end, grows downward */
PROVIDE(_end_stack = _l2_scratch_end);
PROVIDE(_main_hart_stack_top = _end_stack);
PROVIDE(_main_hart_stack_bottom = _main_hart_stack_top - STACK_SIZE);

/* Main hart HLS location (at top of main stack minus 64 bytes) */
PROVIDE(_main_hart_hls = _main_hart_stack_top - 64);

/* Secondary hart stacks below main hart stack
 * Hart 1 stack: _main_hart_stack_bottom - STACK_SIZE_PER_HART * 0 to - STACK_SIZE_PER_HART * 1
 * Hart 2 stack: _main_hart_stack_bottom - STACK_SIZE_PER_HART * 1 to - STACK_SIZE_PER_HART * 2
 * etc.
 */
PROVIDE(_secondary_hart_stack_base = _main_hart_stack_bottom - 4 * STACK_SIZE_PER_HART);

/* Provide symbols for M-mode startup code */
PROVIDE(__global_pointer$ = _global_pointer);

/* Size of text section to copy (for startup code) */
PROVIDE(_text_size = _end_text - _start_text_sram);
