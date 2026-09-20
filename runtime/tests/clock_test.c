/*
 * clock_test.c - `Clock.now` is monotonic, and the arithmetic of `Instant` and `Duration`.
 *
 * Both are `int64_t` nanoseconds (torb.h); `Instant.subtract` reuses the checked `Int64` subtraction, so its overflow
 * behaviour is already pinned by number_test.c and is not repeated here.
 */

#include "harness.h"

static const torb_location somewhere = { "src/clock.trb", 4, 9 };

TORB_TEST(the_clock_never_goes_backwards) {
  torb_instant first = torb_clock_now();
  torb_instant second = torb_clock_now();
  torb_instant third = torb_clock_now();
  TORB_CHECK(torb_instant_compare(first, second) <= 0);
  TORB_CHECK(torb_instant_compare(second, third) <= 0);
  TORB_CHECK(!torb_instant_equals(first, second) || torb_instant_compare(first, second) == 0);
}

TORB_TEST(instant_subtraction_is_a_duration) {
  torb_instant earlier = torb_clock_now();
  torb_instant later = torb_clock_now();
  torb_duration elapsed = torb_instant_subtract(later, earlier, somewhere);
  TORB_CHECK(elapsed >= 0);
  /* The reverse difference is the same span, negated. */
  TORB_CHECK(torb_instant_subtract(earlier, later, somewhere) == -elapsed);
}

TORB_TEST(duration_equals_and_compare_and_seconds) {
  torb_duration one_second = torb_duration_of_seconds(1, somewhere);
  torb_duration two_seconds = torb_duration_of_seconds(2, somewhere);
  TORB_CHECK(torb_duration_equals(one_second, one_second));
  TORB_CHECK(!torb_duration_equals(one_second, two_seconds));
  TORB_CHECK_INTEGER(torb_duration_compare(one_second, two_seconds), -1);
  TORB_CHECK_INTEGER(torb_duration_compare(two_seconds, one_second), 1);
  TORB_CHECK_INTEGER(torb_duration_compare(one_second, one_second), 0);
  TORB_CHECK(torb_duration_seconds(one_second) == 1.0);
  TORB_CHECK(torb_duration_seconds(two_seconds) == 2.0);
  {
    torb_duration half_second = torb_duration_of_seconds(0, somewhere) + 500000000;
    TORB_CHECK(torb_duration_seconds(half_second) == 0.5);
  }
}

TORB_TEST(duration_show_is_fractional_seconds) {
  torb_duration one_and_a_half = torb_duration_of_seconds(1, somewhere) + 500000000;
  torb_duration zero = torb_duration_of_seconds(0, somewhere);
  torb_text shown = torb_duration_show(one_and_a_half);
  TORB_CHECK_TEXT(shown, "1.5s");
  torb_text_release(shown);
  shown = torb_duration_show(zero);
  TORB_CHECK_TEXT(shown, "0.0s");
  torb_text_release(shown);
}

TORB_TEST(seconds_to_duration_overflows_at_the_edge) {
  TORB_CHECK_INTEGER(torb_duration_of_seconds(9223372036LL, somewhere), 9223372036000000000LL);
  TORB_EXPECT_PANIC(torb_duration_of_seconds(9223372037LL, somewhere));
  TORB_CHECK_PANIC_CONTAINS("arithmetic overflow in `*`");
  TORB_EXPECT_PANIC(torb_duration_of_seconds(-9223372037LL, somewhere));
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

void torb_register_clock_tests(void) {
  TORB_ADD(milliseconds_count_from_the_first_reading_and_never_go_backwards);
  TORB_ADD(the_clock_never_goes_backwards);
  TORB_ADD(instant_subtraction_is_a_duration);
  TORB_ADD(duration_equals_and_compare_and_seconds);
  TORB_ADD(duration_show_is_fractional_seconds);
  TORB_ADD(seconds_to_duration_overflows_at_the_edge);
}
