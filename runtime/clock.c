/*
 * clock.c - `std/time`: the monotonic clock `Clock.now` and `Clock.milliseconds` read.
 *
 * `Instant` and `Duration` are records of the program over an `Int64` of nanoseconds, and their arithmetic is
 * TorbScript in `std/time`; what is native is the reading, `torb_clock_now`, in nanoseconds from an unspecified
 * per-process origin. `Clock.milliseconds()` is the same reading, counted from the first one of the process.
 *
 * The monotonic read itself is the one platform difference (`torb_platform_monotonic_nanoseconds`, in platform.c).
 */

#include "torb.h"

int64_t torb_clock_now(void) {
  return torb_platform_monotonic_nanoseconds();
}

/**
 * Monotonic milliseconds from the first reading. The origin is per process and taken on the first call, so the first
 * answer is zero - which is what makes the *difference* of two readings the only thing this is used for.
 */
int64_t torb_clock_milliseconds(void) {
  static int64_t origin = 0;
  static bool have_origin = false;
  const int64_t now = torb_platform_monotonic_nanoseconds();
  if (!have_origin) {
    origin = now;
    have_origin = true;
  }
  return (now - origin) / 1000000;
}
