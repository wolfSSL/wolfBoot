_Min_Heap_Size  = 0x00000100; /* minimal heap (not using malloc) */
_Min_Stack_Size = 0x00003000; /* required amount of stack */

/* Memory areas */
MEMORY
{
  IRAM  (rx)    :ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
  EBI   (xrw)   :ORIGIN = 0x10000000, LENGTH = 16M
  RAM0  (xrw)   :ORIGIN = 0x1FFF8000, LENGTH = 32K
  RAM1  (xrw)   :ORIGIN = 0x20000000, LENGTH = 32K
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into IRAM */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >IRAM

  /* The program code and other data goes into IRAM */
  .text :
  {
    . = ALIGN(4);
    _start_text = .;
    *(.text)           /* .text  sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >IRAM

  /* Constant data goes into IRAM */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >IRAM

  .ARM.extab   : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >IRAM

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >IRAM

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >IRAM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >IRAM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >IRAM

  /* jlink rtt section, goes into fixed location in RAM0 */
  .jlink_rtt (NOLOAD) :
  {
    . = ALIGN(4);
    . = ABSOLUTE(0x1fff8000);
    *(.ARM.__at_0x1fff8000)
  } >RAM0

  /* used by the startup to initialize data */
  _stored_data = LOADADDR(.data);

  /* Initialized data sections goes into RAM0, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _start_data = .;   /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _end_data = .;     /* define a global symbol at data end */
  } >RAM0 AT> IRAM

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _start_bss = .;     /* define a global symbol at bss start */
    __bss_start__ = _start_bss;
    *(.bss)
    *(.bss*)
    /* *(COMMON) */

    . = ALIGN(4);
    _end_bss = .;         /* define a global symbol at bss end */
    __bss_end__ = _end_bss;
  } >RAM0

  /* DMA control block, goes into fixed location in RAM1 */
  .dma_blk (NOLOAD) :
  {
    . = ALIGN(128);
    /**(.ARM.__at_0x20000000) */
    *(dma_blk)
  } >RAM1

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
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
  } >RAM0

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
