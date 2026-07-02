/* Minimal stand-in for the NXP MCUXpresso SDK ROM flash API declarations
 * used by hal/mcxa.c's hal_flash_write()/hal_flash_erase(). The unit test
 * provides the definitions of these functions. */
#ifndef FSL_ROMAPI_STUB_H
#define FSL_ROMAPI_STUB_H

status_t FLASH_ProgramPhrase(flash_config_t *config, uint32_t start,
        uint8_t *src, uint32_t len);
status_t FLASH_EraseSector(flash_config_t *config, uint32_t start,
        uint32_t len, uint32_t key);

#define kFLASH_ApiEraseKey 0x6b65796b

#endif /* FSL_ROMAPI_STUB_H */
