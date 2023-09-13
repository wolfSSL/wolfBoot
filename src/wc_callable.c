#ifdef WOLFCRYPT_SECURE_MODE
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/aes.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfboot/wolfboot.h"
#include "wolfboot/wc_secure.h"
#include "hal.h"
#include <stdint.h>

int __attribute__((cmse_nonsecure_entry))
wcs_get_random(uint8_t *rand, uint32_t size)
{
    int ret;
    WC_RNG wcs_rng;
    wc_InitRng(&wcs_rng);
    ret = wc_RNG_GenerateBlock(&wcs_rng, rand, size);
    wc_FreeRng(&wcs_rng);
    return ret;
}

void wcs_Init(void)
{
    hal_trng_init();
}

#endif
