/* Minimal stand-in for the TI CC26x2 driverlib wrappers (ti-lib.h) that
 * hal/cc26x2.c includes. Only what that file references is declared here:
 * the Fapi flash API used by hal_flash_write()/hal_flash_erase() (the unit
 * test provides those bodies), plus the UART and PRCM/VIMS entry points used
 * by uart_read()/hal_init(). The latter are not exercised by the test -- they
 * only have to compile and link, so they are no-op inlines here. */
#ifndef CC26X2_TI_LIB_STUB_H
#define CC26X2_TI_LIB_STUB_H

#include <stdint.h>
#include <stdbool.h>

/* Fapi status codes (mirror driverlib/flash.h) */
#define FAPI_STATUS_SUCCESS     0x00000000UL
#define FAPI_STATUS_FSM_BUSY    0x00000001UL
#define FAPI_STATUS_FSM_READY   0x00000002UL
#define FAPI_STATUS_FSM_ERROR   0x00000003UL

/* Flash API. Definitions live in the unit test. The data pointer is
 * non-const, matching driverlib/flash.h. */
uint32_t FlashProgram(uint8_t *pui8DataBuffer, uint32_t ui32Address,
        uint32_t ui32Count);
uint32_t FlashSectorErase(uint32_t ui32SectorAddress);
uint32_t FlashCheckFsmForReady(void);

/* UART. Definitions live in the unit test. */
#define UART0_BASE 0x40001000UL
int32_t UARTCharGet(uint32_t ui32Base);
int32_t UARTCharGetNonBlocking(uint32_t ui32Base);

/* PRCM / VIMS constants and no-op entry points used by hal_init() */
#define VIMS_BASE               0x40034000UL
#define VIMS_MODE_ENABLED       0x00000000UL
#define PRCM_DOMAIN_PERIPH      0x00000004UL
#define PRCM_DOMAIN_SERIAL      0x00000002UL
#define PRCM_DOMAIN_POWER_ON    0x00000001UL
#define PRCM_PERIPH_GPIO        0x00000010UL
#define PRCM_PERIPH_UART0       0x00000200UL

static inline void ti_lib_vims_mode_set(uint32_t base, uint32_t mode)
{
    (void)base; (void)mode;
}

static inline void ti_lib_vims_configure(uint32_t base, bool round_robin,
        bool prefetch)
{
    (void)base; (void)round_robin; (void)prefetch;
}

static inline void ti_lib_int_master_disable(void) { }
static inline void ti_lib_int_master_enable(void) { }

static inline void ti_lib_prcm_power_domain_on(uint32_t domain)
{
    (void)domain;
}

static inline uint32_t ti_lib_prcm_power_domain_status(uint32_t domain)
{
    (void)domain;
    return PRCM_DOMAIN_POWER_ON;
}

static inline void ti_lib_prcm_peripheral_run_enable(uint32_t periph)
{
    (void)periph;
}

static inline void ti_lib_prcm_load_set(void) { }
static inline bool ti_lib_prcm_load_get(void) { return true; }

#endif /* CC26X2_TI_LIB_STUB_H */
