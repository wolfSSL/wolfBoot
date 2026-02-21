/* PolarFire SoC MPFS250 Linker Script for test application */

/* Memory configuration for PolarFire SoC MPFS250 test app */
/* TODO: Update with actual memory map for MPFS250 */

_Min_Heap_Size  = 0x00002000; /* required amount of heap  */
_Min_Stack_Size = 0x00002000; /* required amount of stack */

OUTPUT_ARCH( "riscv" )

ENTRY( _reset )

/* Provide M-mode symbols as dummy values for test-app (not used at runtime) */
PROVIDE(_main_hart_hls = 0);

/* Memory areas */
MEMORY
{
  /* For RAM boot (NO_XIP), use WOLFBOOT_LOAD_ADDRESS for execution
   * For XIP boot, use WOLFBOOT_TEST_APP_ADDRESS (partition + header) */
  IRAM   (rwx)   :ORIGIN = @WOLFBOOT_LOAD_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
  DDR    (rwx)   :ORIGIN = 0x80000000, LENGTH = 1M
  LSRAM  (rwx)   :ORIGIN = 0x08000000, LENGTH = 128K
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into IRAM */
  .isr_vector :
  {
    . = ALIGN(8);
    KEEP(*(.isr_vector)) /* Startup code */
    _start_vector = .;
    . = ALIGN(8);
  } >IRAM

  /* The program code and other data goes into IRAM */
  .text :
  {
    . = ALIGN(8);
    _start_text = .;
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(8);
    _etext = .;        /* define a global symbol at end of code */
  } >IRAM

  /* used by the startup to initialize data */
  _stored_data = .;

  /* Initialized data sections - keep in same region as code for RAM boot */
  .data : AT (_stored_data)
  {
    . = ALIGN(8);
    _start_data = .;   /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(8);
    _end_data = .;     /* define a global symbol at data end */
  } >IRAM

  /* Uninitialized data section */
  /* Mark as NOLOAD so it's not included in binary (zero-initialized at runtime) */
  . = ALIGN(8);
  .bss (NOLOAD) :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _start_bss = .;     /* define a global symbol at bss start */
    __bss_start__ = _start_bss;
    *(.bss)
    *(.bss*)
    /* *(COMMON) */

    . = ALIGN(8);
    _end_bss = .;         /* define a global symbol at bss end */
    __bss_end__ = _end_bss;
  } >IRAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  /* Mark as NOLOAD so it's not included in binary (zero-initialized at runtime) */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    PROVIDE ( _start_heap = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
    PROVIDE ( END_STACK = . );
    PROVIDE ( _end_stack = . );
  } >IRAM

  /* Global pointer for RISC-V */
  . = ALIGN(8);
  _global_pointer = .;

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}

