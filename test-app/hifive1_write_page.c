#include <stdint.h>
#include "hal.h"
#include "target.h"
#include "wolfboot/wolfboot.h"

#define PAGESIZE (0x1000) /* Flash sector: 4K */
extern uint8_t flash_page[];


#ifdef APP_DEBUG_WRITE_PAGE
__attribute__((used,naked,section(".ramcode.user"))) 
void write_page(uint32_t dst)
{
    asm volatile ("addi sp, sp, -4");
    asm volatile ("sw ra, 0(sp)");
    if (dst == 0x60000)
        wolfBoot_erase_partition(0x01);
    hal_flash_write(dst, flash_page, PAGESIZE);
    asm volatile ("lw a4, 0(sp)");
    asm volatile ("addi sp, sp, 4");
    asm volatile ("jr a4");
}
#endif

__attribute__((used,section(".ramcode.user"))) 
void write_page(uint32_t dst)
{
    if (dst == 0x60000)
        wolfBoot_erase_partition(0x01);
    hal_flash_write(dst, flash_page, PAGESIZE);
}
