#include "bc_tick.h"
#include "bc_log.h"
#include <time.h>

static struct timespec _bc_tick_ts_start;
static bool _bc_tick_init = false;

static void _bc_tick_diff(struct timespec *start, struct timespec *end, struct timespec *diff);

bc_tick_t bc_tick_get(void)
{
    struct timespec ts_now;
    struct timespec ts_diff;

    if (!_bc_tick_init)
    {
        if (clock_gettime(CLOCK_MONOTONIC, &_bc_tick_ts_start) != 0)
        {
            bc_log_fatal("bc_tick_init: call failed: clock_gettime");
        }
        _bc_tick_init = true;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &ts_now) != 0)
    {
        bc_log_fatal("bc_tick_get: call failed: clock_gettime");
    }

    _bc_tick_diff(&_bc_tick_ts_start, &ts_now, &ts_diff);

    return (bc_tick_t) (ts_diff.tv_sec * 1000L + ts_diff.tv_nsec / 1000000L);
}

static void _bc_tick_diff(struct timespec *start, struct timespec *end, struct timespec *diff)
{
    if ((end->tv_nsec - start->tv_nsec) < 0)
    {
        diff->tv_sec = end->tv_sec - start->tv_sec - 1;
        diff->tv_nsec = 1000000000L + end->tv_nsec - start->tv_nsec;
    }
    else
    {
        diff->tv_sec = end->tv_sec - start->tv_sec;
        diff->tv_nsec = end->tv_nsec - start->tv_nsec;
    }
}
