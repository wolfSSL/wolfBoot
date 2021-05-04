/*----------------------------------------------------------------------------*/
/* Linker Settings                                                            */

--retain="*(.intvecs)"

/* USER CODE BEGIN (1) */
/* USER CODE END */

/*----------------------------------------------------------------------------*/
/* Memory Map                                                                 */

MEMORY
{
    VECTORS (X)  : origin=0x00000000 length=0x00000020
    KERNEL  (RX) : origin=0x00000020 length=0x00008000 
    FLASH0  (RX) : origin=0x00008020 length=0x001F7FE0
    FLASH1  (RX) : origin=0x00200000 length=0x00200000
    STACKS  (RW) : origin=0x08000000 length=0x00000800
    KRAM    (RW) : origin=0x08000800 length=0x00000800
    RAM     (RW) : origin=(0x08000800+0x00000800) length=(0x0007F800 - 0x00000800)
}

/*----------------------------------------------------------------------------*/
/* Section Configuration                                                      */

SECTIONS
{
    .intvecs : {} > VECTORS
    /* FreeRTOS Kernel in protected region of Flash */
    .kernelTEXT  align(32) : {} > KERNEL
    .cinit       align(32) : {} > KERNEL
    .pinit       align(32) : {} > KERNEL
    /* Rest of code to user mode flash region */
    .text        align(32) : {} > FLASH0 | FLASH1
    .const       align(32) : {} > FLASH0 | FLASH1
    /* FreeRTOS Kernel data in protected region of RAM */
    .kernelBSS    : {} > KRAM
    .kernelHEAP   : {} > RAM
    .bss          : {} > RAM
    .data         : {} > RAM    
    .sysmem       : {} > RAM
    FEE_TEXT_SECTION align(32) : {} > FLASH0 | FLASH1
    FEE_CONST_SECTION align(32): {} > FLASH0 | FLASH1
    FEE_DATA_SECTION : {} > RAM
}
