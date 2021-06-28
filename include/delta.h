#ifndef WOLFBOOT_DELTA_H
#define WOLFBOOT_DELTA_H

struct wb_patch_ctx {
    uint8_t *src_base;
    uint32_t src_size;
    uint8_t *patch_base;
    uint32_t patch_size;
    uint32_t p_off;
    int matching;
    uint32_t blk_sz;
    uint32_t blk_off;
};

struct wb_diff_ctx {
    uint8_t *src_a;
    uint8_t *src_b;
    uint32_t size_a, size_b, off_b;
};


typedef struct wb_patch_ctx WB_PATCH_CTX;
typedef struct wb_diff_ctx WB_DIFF_CTX;

int wb_diff_init(WB_DIFF_CTX *ctx, uint8_t *src_a, uint32_t len_a, uint8_t *src_b, uint32_t len_b);
int wb_diff(WB_DIFF_CTX *ctx, uint8_t *patch, uint32_t len);
int wb_patch_init(WB_PATCH_CTX *bm, uint8_t *src, uint32_t ssz, uint8_t *patch, uint32_t psz);
int wb_patch(WB_PATCH_CTX *ctx, uint8_t *dst, uint32_t len);

#endif

