#include <stdlib.h>
#include <stdint.h>

/* Allow one single sp_point to be allocated at one time */

#define SP_POINT_SIZE (244)
#define MAX_POINTS 2
#define SCRATCHBOARD_SIZE (640)
#define TMP_BUFFER_SIZE (124)
#define SP_DIGITS_SIZE (400)
#define SP_NORMALIZER_SIZE (128)

static uint8_t sp_scratchboard[SCRATCHBOARD_SIZE];
static int sp_scratchboard_in_use = 0;

static uint8_t sp_point_buffer0[SP_POINT_SIZE];
static uint8_t sp_point_buffer1[SP_POINT_SIZE];
static uint8_t tmp_buffer[TMP_BUFFER_SIZE];
static uint8_t sp_three_points[SP_POINT_SIZE * 3];
static uint8_t sp_digits[SP_DIGITS_SIZE];
static uint8_t sp_normalizer[SP_NORMALIZER_SIZE];

static int point_0_in_use = 0;
static int point_1_in_use = 0;
static int tmp_buffer_in_use = 0;
static int sp_three_points_in_use = 0;
static int sp_digits_in_use = 0;
static int sp_normalizer_in_use = 0;

static void* xmalloc_sp_point(void)
{
    if (point_0_in_use) {
        if (point_1_in_use)
            return NULL;
        point_1_in_use++;
        return sp_point_buffer1;
    }
    point_0_in_use++;
    return sp_point_buffer0;
}

static void* xmalloc_sp_scratchboard(void)
{
    if (sp_scratchboard_in_use)
            return NULL;
    sp_scratchboard_in_use++;
    return sp_scratchboard;
}

static void* xmalloc_sp_tmpbuffer(void)
{
    if (tmp_buffer_in_use)
            return NULL;
    tmp_buffer_in_use++;
    return tmp_buffer;
}

static void* xmalloc_sp_three_points(void)
{
    if (sp_three_points_in_use)
            return NULL;
    sp_three_points_in_use++;
    return sp_three_points;
}

static void* xmalloc_sp_digits(void)
{
    if (sp_digits_in_use)
            return NULL;
    sp_digits_in_use++;
    return sp_digits;
}

static void* xmalloc_sp_normalizer(void)
{
    if (sp_normalizer_in_use)
            return NULL;
    sp_normalizer_in_use++;
    return sp_normalizer;
}


void* XMALLOC(size_t n, void* heap, int type)
{
    if (n == SP_POINT_SIZE)
        return xmalloc_sp_point();
    if (n == SCRATCHBOARD_SIZE)
        return xmalloc_sp_scratchboard();
    if (n == TMP_BUFFER_SIZE)
        return xmalloc_sp_tmpbuffer();
    if (n == 3 * SP_POINT_SIZE)
        return xmalloc_sp_three_points();
    if (n == SP_DIGITS_SIZE)
        return xmalloc_sp_digits();
    if (n == SP_NORMALIZER_SIZE)
        return xmalloc_sp_normalizer();
    return NULL;
}

void XFREE(void *ptr)
{
    if (ptr == sp_point_buffer0)
        point_0_in_use = 0;
    if (ptr == sp_point_buffer1)
        point_1_in_use = 0;
    if (ptr == sp_scratchboard)
        sp_scratchboard_in_use = 0;
    if (ptr == tmp_buffer)
        tmp_buffer_in_use = 0;
    if (ptr == sp_three_points)
        sp_three_points_in_use = 0;
    if (ptr == sp_digits)
        sp_digits_in_use = 0;
    if (ptr == sp_normalizer)
        sp_normalizer_in_use = 0;
}
