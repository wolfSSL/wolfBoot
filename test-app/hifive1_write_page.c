#include <stdint.h>
#include "hal.h"
#define PAGESIZE (0x1000) /* Flash sector: 4K */
extern uint8_t flash_page[];

__attribute__((used,section(".ramcode.user"))) 
void write_page(uint32_t dst)
{
    hal_flash_erase(dst, PAGESIZE);
    hal_flash_write(dst, flash_page, PAGESIZE);
}
