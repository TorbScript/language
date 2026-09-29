/*
 * clock_test.c - `Clock.now` is monotonic, `Clock.milliseconds` counts from the first reading, and `Clock.timestamp`
 * reads the wall clock from 1970.
 *
 * `Instant` and `Duration` are records of the program over an `Int64` of nanoseconds, so their arithmetic is
 * TorbScript and pinned by tests/conformance/time.trb; the runtime only reads the clock.
 */

#include "harness.h"

TORB_TEST(the_clock_never_goes_backwards) {
  int64_t first = torb_clock_now();
  int64_t second = torb_clock_now();
  int64_t third = torb_clock_now();
  TORB_CHECK(first <= second);
  TORB_CHECK(second <= third);
}

/**
 * `Clock.milliseconds` counts from the first reading of the process, so the first answer is zero and no answer is ever
 * smaller than one before it. Only the difference of two readings is meaningful, which is all that is asserted.
 */
TORB_TEST(milliseconds_count_from_the_first_reading_and_never_go_backwards) {
  int64_t first = torb_clock_milliseconds();
  int64_t second = torb_clock_milliseconds();
  TORB_CHECK(first >= 0);
  TORB_CHECK(second >= first);
}

/**
 * `Clock.timestamp` counts from 1970 in nanoseconds. The wall clock may be set backwards between two readings, so only
 * that a reading is a date of this century is asserted: after 2024-01-01 and before 2100-01-01 - a reading in seconds,
 * in 100-nanosecond ticks or from 1601 would be far outside.
 */
TORB_TEST(the_wall_clock_reads_nanoseconds_since_1970) {
  int64_t now = torb_clock_wall_nanoseconds();
  TORB_CHECK(now > 1704067200LL * 1000000000LL);
  TORB_CHECK(now < 4102444800LL * 1000000000LL);
}

void torb_register_clock_tests(void) {
  TORB_ADD(milliseconds_count_from_the_first_reading_and_never_go_backwards);
  TORB_ADD(the_clock_never_goes_backwards);
  TORB_ADD(the_wall_clock_reads_nanoseconds_since_1970);
}
