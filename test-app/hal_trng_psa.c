/* hal_trng_psa.c
 *
 * PSA-backed entropy for bare-metal test-app.
 */

#include <stdint.h>
#include "psa/crypto.h"

int hal_trng_get_entropy(unsigned char *out, unsigned len)
{
    psa_status_t status;

    if (out == NULL || len == 0) {
        return -1;
    }

    status = psa_generate_random(out, len);
    return (status == PSA_SUCCESS) ? 0 : -1;
}
