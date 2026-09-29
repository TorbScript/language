/*
 * clock.c - `std/time`: the monotonic clock `Clock.now` and `Clock.milliseconds` read, and the wall clock
 * `Clock.timestamp` reads.
 *
 * `Instant`, `Duration` and `Timestamp` are records of the program over an `Int64` of nanoseconds, and their arithmetic
 * is TorbScript in `std/time`; what is native is the reading: `torb_clock_now` in nanoseconds from an unspecified
 * per-process origin, `torb_clock_wall_nanoseconds` in nanoseconds since 1970-01-01 00:00:00 UTC.
 * `Clock.milliseconds()` is the monotonic reading, counted from the first one of the process.
 *
 * The reads themselves are the platform differences (`torb_platform_monotonic_nanoseconds` and
 * `torb_platform_wall_nanoseconds`, in platform.c).
 */

#include "torb.h"
#include "torb_pool.h"

int64_t torb_clock_now(void) {
  return torb_platform_monotonic_nanoseconds();
}

int64_t torb_clock_wall_nanoseconds(void) {
  return torb_platform_wall_nanoseconds();
}

static torb_mutex torb_clock_mutex = TORB_MUTEX_INITIALIZER;
static int64_t torb_clock_origin = 0;
static uint32_t torb_clock_has_origin = 0u;

/**
 * Monotonic milliseconds from the first reading. The origin is per process and taken on the first call, so the first
 * answer is zero - which is what makes the *difference* of two readings the only thing this is used for. Two workers
 * that read first at the same time agree on one origin: it is taken under a lock and published with a release.
 */
int64_t torb_clock_milliseconds(void) {
  if (torb_atomic_load_u32(&torb_clock_has_origin) == 0u) {
    torb_mutex_lock(&torb_clock_mutex);
    if (torb_clock_has_origin == 0u) {
      torb_clock_origin = torb_platform_monotonic_nanoseconds();
      torb_atomic_store_u32(&torb_clock_has_origin, 1u);
    }
    torb_mutex_unlock(&torb_clock_mutex);
  }
  /* Read after the origin, so no caller ever sees a reading from before it */
  return (torb_platform_monotonic_nanoseconds() - torb_clock_origin) / 1000000;
}

/* The platform reads the frequency of its counter on the first reading; this one happens before a second thread. */
void torb_clock_prepare(void) {
  (void)torb_platform_monotonic_nanoseconds();
}
