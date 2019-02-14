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
/* external flash interface */
int  ext_flash_write(uint32_t address, const uint8_t *data, int len);
int  ext_flash_read(uint32_t address, uint8_t *data, int len);
int  ext_flash_erase(uint32_t address, int len);
void ext_flash_lock(void);
void ext_flash_unlock(void);
#endif
    
#endif /* H_HAL_FLASH_ */
