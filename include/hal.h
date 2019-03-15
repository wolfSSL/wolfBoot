#ifndef H_HAL_
#define H_HAL_


#include <inttypes.h>
#include "target.h"
void hal_init(void);
int hal_flash_write(uint32_t address, const uint8_t *data, int len);
int hal_flash_erase(uint32_t address, int len);
void hal_flash_unlock(void);
void hal_flash_lock(void);
void hal_prepare_boot(void);

#ifdef EXT_FLASH

#   ifndef SPI_FLASH
        /* external flash interface */
        int  ext_flash_write(uint32_t address, const uint8_t *data, int len);
        int  ext_flash_read(uint32_t address, uint8_t *data, int len);
        int  ext_flash_erase(uint32_t address, int len);
        void ext_flash_lock(void);
        void ext_flash_unlock(void);
#   else
#       include "spi_flash.h"
#       define ext_flash_lock() do{}while(0)
#       define ext_flash_unlock() do{}while(0)
#       define ext_flash_read spi_flash_read
#       define ext_flash_write spi_flash_write

        static int ext_flash_erase(uint32_t address, int len)
        {
            uint32_t end = address + len - 1;
            uint32_t p;
            for (p = address; p <= end; p += SPI_FLASH_SECTOR_SIZE)
                spi_flash_sector_erase(p);
            return 0;
        }
#   endif /* !SPI_FLASH */

#endif /* EXT_FLASH */
    
#endif /* H_HAL_FLASH_ */
