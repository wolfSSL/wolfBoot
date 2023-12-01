OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
SEARCH_DIR(.)
ENTRY(isr_reset)
#  define ROM_ORIGIN 0x8000 + 0x200
#  define ROM_LENGTH 0x100000
#  define RAM_ORIGIN 0x20000000
#  define RAM_LENGTH 0x40000
#  define TCM_ORIGIN 0x3000000
#endif

#ifndef __XC32_TCM_LENGTH
#  define __XC32_TCM_LENGTH 0x0
#elif (defined(__XC32_TCM_LENGTH)  && __XC32_TCM_LENGTH != 0x0 && __XC32_TCM_LENGTH != 0x800 && __XC32_TCM_LENGTH != 0xc00 && __XC32_TCM_LENGTH != 0x1000)
#  warning Non-standard ITCM length, using default 0x1000
#  undef __XC32_TCM_LENGTH
#  define __XC32_TCM_LENGTH 0x1000
#endif
#ifndef BKUPRAM_ORIGIN
#  define BKUPRAM_ORIGIN 0x47000000
#endif
#ifndef BKUPRAM_LENGTH
#  define BKUPRAM_LENGTH 0x2000
#elif (BKUPRAM_LENGTH > 0x2000)
#  error BKUPRAM_LENGTH is greater than the max size of 0x2000
#endif

MEMORY
{
  FLASH (LRX) : ORIGIN = ROM_ORIGIN, LENGTH = (ROM_LENGTH - 0x8200)
  RAM (WX!R) : ORIGIN = RAM_ORIGIN, LENGTH = RAM_LENGTH
  TCM (WX) : ORIGIN = TCM_ORIGIN, LENGTH = __XC32_TCM_LENGTH
  BRAM : ORIGIN = BKUPRAM_ORIGIN, LENGTH = BKUPRAM_LENGTH
}

SECTIONS
{
    .vectors :
    {
        . = ALIGN(4);
        _sfixed = .;
        KEEP(*(.vectors .vectors.* .vectors_default .vectors_default.*))
        KEEP(*(.isr_vector))
        KEEP(*(.reset*))
        KEEP(*(.after_vectors))
    } > FLASH 
    
    .text :
    {
        . = ALIGN(4);
        *(.glue_7t) *(.glue_7)
        *(.gnu.linkonce.r.*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)

        . = ALIGN(4);
        KEEP(*(.init))
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;

        . = ALIGN(0x4);
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))

        . = ALIGN(4);
        KEEP(*(.fini))

        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        __fini_array_end = .;

        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        . = ALIGN(4);
        _efixed = .;
    } > FLASH 

    PROVIDE_HIDDEN (__exidx_start = .);
    .ARM.exidx :
    {
      *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH 
    PROVIDE_HIDDEN (__exidx_end = .);
    . = ALIGN(4);
    _etext = .;
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        _sbss = . ;
        _szero = .;
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        _ebss = . ;
        _ezero = .;
    } > RAM 

    . = ALIGN(4);
    _end = . ;
    _ram_end_ = ORIGIN(RAM) + LENGTH(RAM) -1 ;
    
    .bkupram_bss :
    {
        *(.bkupram_bss .bkupram_bss.*)
        *(.pbss .pbss.*)
    } > BRAM
}


__rom_end = ORIGIN(FLASH) + LENGTH(FLASH);
__ram_end = ORIGIN(RAM) + LENGTH(RAM);
