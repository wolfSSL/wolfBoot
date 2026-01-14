/* arm_tee_ns_interface_stub.c
 *
 * Minimal non-Zephyr dispatcher for bare-metal test-app.
 */

#include "arm_tee_ns_interface.h"
#include "psa/error.h"
#include <stddef.h>

int32_t arm_tee_ns_interface_dispatch(arm_tee_veneer_fn fn,
                                      uint32_t arg0, uint32_t arg1,
                                      uint32_t arg2, uint32_t arg3)
{
    if (fn == NULL) {
        return (int32_t)PSA_ERROR_INVALID_ARGUMENT;
    }

    return fn(arg0, arg1, arg2, arg3);
}

uint32_t arm_tee_ns_interface_init(void)
{
    return PSA_SUCCESS;
}
