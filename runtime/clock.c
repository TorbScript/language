/*
 * clock.c - `std/time`: `Clock.now`, and the arithmetic of `Instant` and `Duration`.
 *
 * Both are represented as `int64_t` nanoseconds (see torb.h): `Instant` a monotonic reading with an unspecified
 * per-process origin, `Duration` a signed nanosecond span. Neither is a counted block - they are plain numbers, like
 * `Int64` is - so every parameter here is a value, not a pointer, and "borrowed" is the only ownership there is.
 *
 * The monotonic read itself is the one platform difference (`torb_platform_monotonic_nanoseconds`, in platform.c);
 * everything else here is portable arithmetic. `Instant - Instant` reuses the checked `Int64` subtraction from
 * `torb_number.h`, so a difference that could never happen in practice (the two readings billions of years apart)
 * panics instead of silently wrapping, the same as every other subtraction in the language.
 */

#include "torb.h"

torb_instant torb_clock_now(void) {
  return torb_platform_monotonic_nanoseconds();
}

bool torb_instant_equals(torb_instant first, torb_instant second) {
  return first == second;
}

int32_t torb_instant_compare(torb_instant first, torb_instant second) {
  if (first < second) {
    return -1;
  }
  if (first > second) {
    return 1;
  }
  return 0;
}

torb_duration torb_instant_subtract(torb_instant first, torb_instant second, torb_location at) {
  return torb_subtract_i64(first, second, at);
}

bool torb_duration_equals(torb_duration first, torb_duration second) {
  return first == second;
}

int32_t torb_duration_compare(torb_duration first, torb_duration second) {
  if (first < second) {
    return -1;
  }
  if (first > second) {
    return 1;
  }
  return 0;
}

double torb_duration_seconds(torb_duration duration) {
  return (double)duration / 1000000000.0;
}

torb_text torb_duration_show(torb_duration duration) {
  torb_text formatted = torb_show_f64(torb_duration_seconds(duration));
  torb_text parts[2];
  torb_text result;
  parts[0] = formatted;
  parts[1] = torb_text_from_cstring("s");
  result = torb_text_concat(parts, 2u);
  torb_text_release(formatted);
  torb_text_release(parts[1]);
  return result;
}

/* One second is 1e9 nanoseconds, so `seconds` beyond this magnitude overflows an `int64_t` nanosecond count:
   `INT64_MAX / 1000000000` truncated is 9223372036. */
#define TORB_MAXIMUM_SECONDS 9223372036LL
#define TORB_NANOSECONDS_PER_SECOND 1000000000LL

torb_duration torb_duration_of_seconds(int64_t seconds, torb_location at) {
  if (seconds > TORB_MAXIMUM_SECONDS || seconds < -TORB_MAXIMUM_SECONDS) {
    torb_panic_overflow("*", at);
  }
  return seconds * TORB_NANOSECONDS_PER_SECOND;
}
