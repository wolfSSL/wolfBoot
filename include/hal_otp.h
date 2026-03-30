#ifndef WOLFBOOT_HAL_OTP_H
#define WOLFBOOT_HAL_OTP_H

#include <stdint.h>

static inline uint32_t hal_otp_blocks_for_length(uint32_t length,
    uint32_t block_size)
{
    return (length + block_size - 1U) / block_size;
}

#endif /* WOLFBOOT_HAL_OTP_H */
