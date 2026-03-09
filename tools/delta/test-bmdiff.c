#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

static jmp_buf exit_jmp;
static int exit_code = -1;
static int mmap_calls = 0;
static int patch_init_called = 0;
static int perror_called = 0;

static int mock_stat(const char *path, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    if (strcmp(path, "source.bin") == 0) {
        st->st_size = 16;
        return 0;
    }
    if (strcmp(path, "patch.bin") == 0) {
        st->st_size = 8;
        return 0;
    }
    return -1;
}

static int mock_open(const char *path, int flags, ...)
{
    (void)flags;
    if ((strcmp(path, "source.bin") == 0) || (strcmp(path, "patch.bin") == 0))
        return 3;
    return -1;
}

static void *mock_mmap(void *addr, size_t len, int prot, int flags, int fd,
    off_t offset)
{
    static uint8_t source_map[16];

    (void)addr;
    (void)len;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;

    mmap_calls++;
    if (mmap_calls == 1)
        return source_map;

    return MAP_FAILED;
}

static void mock_perror(const char *s)
{
    (void)s;
    perror_called = 1;
}

static void mock_exit(int code)
{
    exit_code = code;
    longjmp(exit_jmp, 1);
}

#define WOLFBOOT_SECTOR_SIZE 1024
#define stat(path, st) mock_stat(path, st)
#define open(path, flags, ...) mock_open(path, flags, ##__VA_ARGS__)
#define mmap(addr, len, prot, flags, fd, offset) \
    mock_mmap(addr, len, prot, flags, fd, offset)
#define perror(s) mock_perror(s)
#define exit(code) mock_exit(code)
#define main bmdiff_tool_main
#include "bmdiff.c"
#undef main
#undef exit
#undef perror
#undef mmap
#undef open
#undef stat

int wb_patch_init(WB_PATCH_CTX *bm, uint8_t *src, uint32_t ssz, uint8_t *patch,
    uint32_t psz)
{
    (void)bm;
    (void)src;
    (void)ssz;
    (void)patch;
    (void)psz;

    patch_init_called = 1;
    mock_exit(99);
    return -1;
}

int wb_patch(WB_PATCH_CTX *ctx, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    (void)dst;
    (void)len;
    return 0;
}

int wb_diff_init(WB_DIFF_CTX *ctx, uint8_t *src_a, uint32_t len_a,
    uint8_t *src_b, uint32_t len_b)
{
    (void)ctx;
    (void)src_a;
    (void)len_a;
    (void)src_b;
    (void)len_b;
    return 0;
}

int wb_diff(WB_DIFF_CTX *ctx, uint8_t *patch, uint32_t len)
{
    (void)ctx;
    (void)patch;
    (void)len;
    return 0;
}

int main(void)
{
    char *argv[] = { (char *)"bmpatch", (char *)"source.bin", (char *)"patch.bin" };

    if (setjmp(exit_jmp) == 0)
        bmdiff_tool_main(3, argv);

    assert(exit_code == 3);
    assert(perror_called == 1);
    assert(patch_init_called == 0);
    return 0;
}
