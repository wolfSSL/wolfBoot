/* dice.h
 *
 * DICE helpers and PSA attestation token builder.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 */

#ifndef WOLFBOOT_DICE_H
#define WOLFBOOT_DICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int wolfBoot_dice_get_token(const uint8_t *challenge,
                            size_t challenge_size,
                            uint8_t *token_buf,
                            size_t token_buf_size,
                            size_t *token_size);

int wolfBoot_dice_get_token_size(size_t challenge_size, size_t *token_size);

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_DICE_H */
