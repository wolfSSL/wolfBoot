#include <stdlib.h>
#include <stdint.h>

/* Allow one single sp_point to be allocated at one time */

#define SP_POINT_SIZE (244)
#define MAX_POINTS 2

#define SCRATCHBOARD_SIZE (640)
static uint8_t sp_scratchboard[SCRATCHBOARD_SIZE];
static int sp_scratchboard_in_use = 0;

static uint8_t sp_point_buffer0[SP_POINT_SIZE];
static uint8_t sp_point_buffer1[SP_POINT_SIZE];

static int point_0_in_use = 0;
static int point_1_in_use = 0;

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

void* XMALLOC(size_t n, void* heap, int type)
{
    if (n == SP_POINT_SIZE)
        return xmalloc_sp_point();
    if (n == SCRATCHBOARD_SIZE)
        return xmalloc_sp_scratchboard();
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
}
