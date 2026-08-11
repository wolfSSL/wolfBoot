--stack_size=16384
--heap_size=1024
__IRQ_STACK_SIZE = 4096;
__FIQ_STACK_SIZE = 256;
__SVC_STACK_SIZE = 256;
__ABORT_STACK_SIZE = 256;
__UNDEFINED_STACK_SIZE = 256;

-e_vectors_sbl

MEMORY
{
    MSRAM_VECS  : ORIGIN = 0x70000000 , LENGTH = 0x100 
    MSRAM_0     : ORIGIN = 0x70000100 , LENGTH = 0x6FF00 
    MSRAM_1     : ORIGIN = 0x70070000 , LENGTH = 0x10000 
}

SECTIONS
{
    .vectors :
    {
    } > MSRAM_VECS , palign(8) 

    .text :
    {
        *(.keystore*)
        . = ALIGN(8);
        *(.text*)
        . = ALIGN(8);
        *(.rodata*)
        . = ALIGN(8);
        *(.data*)
        . = ALIGN(8);
    } > MSRAM_0 , palign(8)

    .bss :
    {
        _start_bss = .;
        *(.bss*)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
    } > MSRAM_1   , palign(8)
    RUN_START(__BSS_START)
    RUN_END(__BSS_END)

    .stack :
    {
    } > MSRAM_1 , palign(8)

    GROUP  :   {
    .irqstack : {
        . = . + __IRQ_STACK_SIZE;
    } align(8)
    RUN_START(__IRQ_STACK_START)
    RUN_END(__IRQ_STACK_END)
    .fiqstack : {
        . = . + __FIQ_STACK_SIZE;
    } align(8)
    RUN_START(__FIQ_STACK_START)
    RUN_END(__FIQ_STACK_END)
    .svcstack : {
        . = . + __SVC_STACK_SIZE;
    } align(8)
    RUN_START(__SVC_STACK_START)
    RUN_END(__SVC_STACK_END)
    .abortstack : {
        . = . + __ABORT_STACK_SIZE;
    } align(8)
    RUN_START(__ABORT_STACK_START)
    RUN_END(__ABORT_STACK_END)
    .undefinedstack : {
        . = . + __UNDEFINED_STACK_SIZE;
    } align(8)
    RUN_START(__UNDEFINED_STACK_START)
    RUN_END(__UNDEFINED_STACK_END)
    } > MSRAM_1  
}
