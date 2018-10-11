#ifndef __BOOTUTIL_CRYPTO_H_
#define __BOOTUTIL_CRYPTO_H_

#include <wolfssl/wolfcrypt/sha256.h>
#include <stdint.h>


typedef wc_Sha256 bootutil_sha256_context;

static inline void bootutil_sha256_init(bootutil_sha256_context *ctx)
{
    wc_InitSha256(ctx);
}

static inline void bootutil_sha256_update(bootutil_sha256_context *ctx,
                                          const void *data,
                                          uint32_t data_len)
{
    wc_Sha256Update(ctx, data, data_len);
}

static inline void bootutil_sha256_finish(bootutil_sha256_context *ctx,
                                          uint8_t *output)
{
    wc_Sha256Final(ctx, output);
}

#endif
