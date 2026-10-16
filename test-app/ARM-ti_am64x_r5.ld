--stack_size=32768
--heap_size=1024
__IRQ_STACK_SIZE = 4096;
__FIQ_STACK_SIZE = 256;
__SVC_STACK_SIZE = 256;
__ABORT_STACK_SIZE = 256;
__UNDEFINED_STACK_SIZE = 256;

-e_vectors

MEMORY
{
    R5F_VECS    : ORIGIN = 0x0 , LENGTH = 0x40 
    MSRAM_45    : ORIGIN = 0x70100000 , LENGTH = 0xC0000 
}

SECTIONS
{
    .vectors  : {
    } > R5F_VECS , palign(8) 

    .text :
    {
        *(.text*)
        *(.rodata*)
        *(.data*)
    } > MSRAM_45 , palign(8)

    .bss :
    {
        *(.bss*)
    } > MSRAM_45 , palign(8)
    RUN_START(__BSS_START)
    RUN_END(__BSS_END)

    .stack :
    {
    } > MSRAM_45 , palign(8)

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
    } > MSRAM_45  
}

_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;
