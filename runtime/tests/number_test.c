/*
 * number_test.c - the checked arithmetic of every width, the shifts, and the conversions.
 *
 * Decided gap 2 (truncation toward zero, the remainder takes the sign of the dividend, `x / 0` and
 * `smallest / -1` panic) and decided gap 3 (a shift by a negative amount or by the width or more panics) are pinned
 * here for all eight integer widths.
 */

#include "harness.h"

static const torb_location somewhere = { "src/number.trb", 4, 9 };

static bool parse_whole(const char *digits, int64_t *out) {
  torb_text text = torb_text_from_cstring(digits);
  bool parsed = torb_parse_i64(text, out);
  torb_text_release(text);
  return parsed;
}

static bool parse_unsigned(const char *digits, uint64_t *out) {
  torb_text text = torb_text_from_cstring(digits);
  bool parsed = torb_parse_u64(text, out);
  torb_text_release(text);
  return parsed;
}

static bool parse_radix(const char *digits, int64_t radix, int64_t *out) {
  torb_text text = torb_text_from_cstring(digits);
  bool parsed = torb_parse_i64_digits(text, radix, out);
  torb_text_release(text);
  return parsed;
}

TORB_TEST(addition_overflows_at_every_width) {
  TORB_EXPECT_PANIC(torb_add_i8(127, 1, somewhere));
  TORB_CHECK_PANIC_CONTAINS("arithmetic overflow in `+`");
  TORB_EXPECT_PANIC(torb_add_i16(32767, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_i32(2147483647, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_i64(INT64_MAX, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_u8(255, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_u16(65535, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_u32(4294967295u, 1, somewhere));
  TORB_EXPECT_PANIC(torb_add_u64(UINT64_MAX, 1, somewhere));
}

TORB_TEST(subtraction_overflows_at_every_width) {
  TORB_EXPECT_PANIC(torb_subtract_i8(-128, 1, somewhere));
  TORB_EXPECT_PANIC(torb_subtract_i16(-32768, 1, somewhere));
  TORB_EXPECT_PANIC(torb_subtract_i32(-2147483647 - 1, 1, somewhere));
  TORB_EXPECT_PANIC(torb_subtract_i64(INT64_MIN, 1, somewhere));
  TORB_EXPECT_PANIC(torb_subtract_u8(0, 1, somewhere));
  TORB_EXPECT_PANIC(torb_subtract_u64(0, 1, somewhere));
  TORB_CHECK_INTEGER(torb_subtract_i64(INT64_MIN, 0, somewhere), INT64_MIN);
}

TORB_TEST(multiplication_overflows_at_every_width) {
  TORB_EXPECT_PANIC(torb_multiply_i8(64, 2, somewhere));
  TORB_EXPECT_PANIC(torb_multiply_i16(256, 256, somewhere));
  TORB_EXPECT_PANIC(torb_multiply_i32(65536, 65536, somewhere));
  TORB_EXPECT_PANIC(torb_multiply_i64(INT64_MAX, 2, somewhere));
  TORB_EXPECT_PANIC(torb_multiply_i64(INT64_MIN, -1, somewhere));
  TORB_EXPECT_PANIC(torb_multiply_u64(UINT64_MAX, 2, somewhere));
  TORB_CHECK_INTEGER(torb_multiply_i64(3037000499LL, 3037000499LL, somewhere), 9223372030926249001LL);
  TORB_CHECK_INTEGER(torb_multiply_i64(0, INT64_MIN, somewhere), 0);
  TORB_CHECK_INTEGER(torb_multiply_i64(-1, INT64_MAX, somewhere), -INT64_MAX);
}

TORB_TEST(division_truncates_toward_zero) {
  TORB_CHECK_INTEGER(torb_divide_i64(-7, 2, somewhere), -3);
  TORB_CHECK_INTEGER(torb_divide_i64(7, -2, somewhere), -3);
  TORB_CHECK_INTEGER(torb_divide_i64(7, 2, somewhere), 3);
  TORB_CHECK_INTEGER(torb_remainder_i64(-7, 2, somewhere), -1);
  TORB_CHECK_INTEGER(torb_remainder_i64(7, -2, somewhere), 1);
  TORB_CHECK_INTEGER(torb_divide_i32(-7, 2, somewhere), -3);
  TORB_CHECK_INTEGER(torb_remainder_i8(-7, 2, somewhere), -1);
}

TORB_TEST(division_by_zero_and_the_smallest_value_by_minus_one_panic) {
  TORB_EXPECT_PANIC(torb_divide_i64(1, 0, somewhere));
  TORB_CHECK_PANIC_CONTAINS("division by zero in `/`");
  TORB_EXPECT_PANIC(torb_remainder_i64(1, 0, somewhere));
  TORB_CHECK_PANIC_CONTAINS("division by zero in `%`");
  TORB_EXPECT_PANIC(torb_divide_u32(1, 0, somewhere));
  TORB_EXPECT_PANIC(torb_divide_i64(INT64_MIN, -1, somewhere));
  TORB_CHECK_PANIC_CONTAINS("arithmetic overflow in `/`");
  TORB_EXPECT_PANIC(torb_remainder_i64(INT64_MIN, -1, somewhere));
  TORB_EXPECT_PANIC(torb_divide_i8(-128, -1, somewhere));
  TORB_EXPECT_PANIC(torb_divide_i16(-32768, -1, somewhere));
  TORB_EXPECT_PANIC(torb_divide_i32(-2147483647 - 1, -1, somewhere));
}

TORB_TEST(negate_and_absolute_overflow_at_the_smallest_value) {
  TORB_EXPECT_PANIC(torb_negate_i64(INT64_MIN, somewhere));
  TORB_EXPECT_PANIC(torb_absolute_i64(INT64_MIN, somewhere));
  TORB_EXPECT_PANIC(torb_negate_i8(-128, somewhere));
  TORB_CHECK_INTEGER(torb_absolute_i8(-127, somewhere), 127);
  TORB_CHECK_INTEGER(torb_absolute_i64(-5, somewhere), 5);
  TORB_CHECK_INTEGER(torb_negate_i32(2147483647, somewhere), -2147483647);
}

TORB_TEST(the_bit_operations_keep_their_width) {
  TORB_CHECK_INTEGER(torb_bitwise_and_i64(0xF0F0, 0x0FF0), 0x00F0);
  TORB_CHECK_INTEGER(torb_bitwise_or_i32(0xF0, 0x0F), 0xFF);
  TORB_CHECK_INTEGER(torb_bitwise_exclusive_or_u8(0xFFu, 0x0Fu), 0xF0);
  TORB_CHECK_INTEGER(torb_bitwise_not_u8(0x00u), 0xFF);
  TORB_CHECK_INTEGER(torb_bitwise_not_i8(0), -1);
  TORB_CHECK_INTEGER(torb_bitwise_not_i64(0), -1);
  TORB_CHECK_INTEGER(torb_bitwise_not_u16(0x00FFu), 0xFF00);
}

TORB_TEST(shifts_are_arithmetic_for_signed_and_logical_for_unsigned) {
  TORB_CHECK_INTEGER(torb_shifted_right_i64(-8, 1, somewhere), -4);
  TORB_CHECK_INTEGER(torb_shifted_right_i64(-1, 63, somewhere), -1);
  TORB_CHECK_INTEGER(torb_shifted_right_i8(-8, 1, somewhere), -4);
  TORB_CHECK_INTEGER(torb_shifted_right_i32(-1024, 4, somewhere), -64);
  TORB_CHECK_INTEGER(torb_shifted_right_u8(0x80u, 7, somewhere), 1);
  TORB_CHECK_INTEGER(torb_shifted_left_i64(1, 62, somewhere), 4611686018427387904LL);
  /* Bits that leave the type are dropped, not a panic: a shift is a bit operation. */
  TORB_CHECK_INTEGER(torb_shifted_left_u8(0xFFu, 4, somewhere), 0xF0);
  TORB_CHECK_INTEGER(torb_shifted_left_i8(1, 7, somewhere), -128);
}

TORB_TEST(a_shift_out_of_range_panics) {
  TORB_EXPECT_PANIC(torb_shifted_left_i64(1, 64, somewhere));
  TORB_CHECK_PANIC_CONTAINS("shift by 64, which is not between 0 and 63");
  TORB_EXPECT_PANIC(torb_shifted_left_i64(1, -1, somewhere));
  TORB_EXPECT_PANIC(torb_shifted_right_i8(1, 8, somewhere));
  TORB_CHECK_PANIC_CONTAINS("not between 0 and 7");
  TORB_EXPECT_PANIC(torb_shifted_right_u32(1, 32, somewhere));
  TORB_EXPECT_PANIC(torb_shifted_left_u16(1, 16, somewhere));
}

TORB_TEST(the_wrapping_operations_of_uint64_do_not_panic) {
  TORB_CHECK(torb_added_wrapping_u64(UINT64_MAX, 1u) == 0u);
  TORB_CHECK(torb_multiplied_wrapping_u64(UINT64_MAX, 2u) == UINT64_MAX - 1u);
  /* The FNV-1a step of a hash written in TorbScript reaches the runtime's own answer. */
  TORB_CHECK(torb_multiplied_wrapping_u64(torb_bitwise_exclusive_or_u64(14695981039346656037ULL, 0x61u),
                                          1099511628211ULL)
             == torb_hash_bytes("a", 1u));
}

TORB_TEST(the_narrowing_conversions_check_their_range) {
  int32_t narrow = 0;
  uint8_t byte = 0;
  int64_t whole = 0;
  torb_text message = torb_text_empty();
  TORB_CHECK(torb_convert_i64_i32_checked(2147483647, &narrow, &message));
  TORB_CHECK_INTEGER(narrow, 2147483647);
  TORB_CHECK(!torb_convert_i64_i32_checked(2147483648LL, &narrow, &message));
  /* The `message` of the `NumberRangeError`: what went out of range, which only the value knows */
  TORB_CHECK_TEXT(message, "2147483648");
  torb_text_release(message);
  TORB_CHECK(!torb_convert_i64_i32_checked(-2147483649LL, &narrow, &message));
  torb_text_release(message);
  TORB_CHECK(torb_convert_i64_u8_checked(255, &byte, &message));
  TORB_CHECK(!torb_convert_i64_u8_checked(256, &byte, &message));
  torb_text_release(message);
  TORB_CHECK(!torb_convert_i64_u8_checked(-1, &byte, &message));
  torb_text_release(message);
  TORB_CHECK(torb_convert_f64_i64_checked(-2.9, &whole, &message));
  TORB_CHECK_INTEGER(whole, -2);
  TORB_CHECK(torb_convert_f64_i64_checked(2.9, &whole, &message));
  TORB_CHECK_INTEGER(whole, 2);
  TORB_CHECK(!torb_convert_f64_i64_checked(1.0e300, &whole, &message));
  TORB_CHECK_TEXT(message, "1e300");
  torb_text_release(message);
  TORB_CHECK(!torb_convert_f64_i64_checked(9223372036854775808.0, &whole, &message));
  torb_text_release(message);
  {
    double nothing = 0.0;
    TORB_CHECK(!torb_convert_f64_i64_checked(nothing / nothing, &whole, &message));
    TORB_CHECK_TEXT(message, "nan");
    torb_text_release(message);
  }
}

/** `a % b` on a float is `fmod`, and a remainder by zero is `nan` rather than a panic (decided gap 2). */
TORB_TEST(the_remainder_of_two_floats_is_fmod) {
  TORB_CHECK(torb_remainder_f64(7.5, 2.0) == 1.5);
  TORB_CHECK(torb_remainder_f64(-7.5, 2.0) == -1.5);
  TORB_CHECK(torb_remainder_f32(7.5f, 2.0f) == 1.5f);
  {
    double answered = torb_remainder_f64(1.0, 0.0);
    TORB_CHECK(answered != answered);
  }
}

TORB_TEST(the_total_order_of_floats_puts_nan_above_everything) {
  double nothing = 0.0;
  double not_a_number = nothing / nothing;
  TORB_CHECK_INTEGER(torb_compare_f64(1.0, 2.0), -1);
  TORB_CHECK_INTEGER(torb_compare_f64(2.0, 1.0), 1);
  TORB_CHECK_INTEGER(torb_compare_f64(0.0, -0.0), 0);
  TORB_CHECK_INTEGER(torb_compare_f64(not_a_number, 1.0e308), 1);
  TORB_CHECK_INTEGER(torb_compare_f64(1.0e308, not_a_number), -1);
  TORB_CHECK_INTEGER(torb_compare_f64(not_a_number, not_a_number), 0);
  /* `==` stays IEEE-754, which is the point of the two being different. */
  TORB_CHECK(!torb_equal_f64(not_a_number, not_a_number));
  TORB_CHECK(torb_equal_f64(0.0, -0.0));
}

TORB_TEST(parsing_integers_checks_the_range) {
  int64_t value = 0;
  uint64_t unsigned_value = 0u;
  TORB_CHECK(parse_whole("42", &value));
  TORB_CHECK_INTEGER(value, 42);
  TORB_CHECK(parse_whole("-9223372036854775808", &value));
  TORB_CHECK_INTEGER(value, INT64_MIN);
  TORB_CHECK(!parse_whole("9223372036854775808", &value));
  TORB_CHECK(!parse_whole("", &value));
  TORB_CHECK(!parse_whole("4x", &value));
  TORB_CHECK(parse_unsigned("18446744073709551615", &unsigned_value));
  TORB_CHECK(unsigned_value == UINT64_MAX);
  TORB_CHECK(parse_radix("ff", 16, &value));
  TORB_CHECK_INTEGER(value, 255);
  TORB_CHECK(parse_radix("1_0", 2, &value));
  TORB_CHECK_INTEGER(value, 2);
  TORB_CHECK(!parse_radix("g", 16, &value));
}

/** `|a - b| < 1e-9`, for the results that are not exact in `double` (a logarithm in another base, `arcTangent2`). */
static bool close_enough(double a, double b) {
  double difference = a - b;
  if (difference < 0.0) {
    difference = -difference;
  }
  return difference < 1e-9;
}

TORB_TEST(math_functions_match_known_values) {
  TORB_CHECK(torb_math_power(2.0, 10.0) == 1024.0);
  TORB_CHECK(torb_math_exponential(0.0) == 1.0);
  TORB_CHECK(torb_math_natural_log(1.0) == 0.0);
  TORB_CHECK(close_enough(torb_math_logarithm(8.0, 2.0), 3.0));
  TORB_CHECK(torb_math_sine(0.0) == 0.0);
  TORB_CHECK(torb_math_cosine(0.0) == 1.0);
  TORB_CHECK(torb_math_tangent(0.0) == 0.0);
  TORB_CHECK(torb_math_arc_sine(0.0) == 0.0);
  TORB_CHECK(torb_math_arc_cosine(1.0) == 0.0);
  TORB_CHECK(torb_math_arc_tangent(0.0) == 0.0);
  TORB_CHECK(close_enough(torb_math_arc_tangent2(1.0, 1.0), 0.7853981633974483));
  /* `arcTangent2` picks the quadrant from the sign of both arguments, which a plain `arcTangent(y / x)` cannot. */
  TORB_CHECK(close_enough(torb_math_arc_tangent2(1.0, -1.0), 2.356194490192345));
}

TORB_TEST(math_domain_errors_answer_nan_and_never_panic) {
  TORB_CHECK(torb_is_nan_f64(torb_math_natural_log(-1.0)));
  TORB_CHECK(torb_is_nan_f64(torb_math_arc_sine(2.0)));
  TORB_CHECK(torb_is_nan_f64(torb_math_arc_sine(-2.0)));
  TORB_CHECK(torb_is_nan_f64(torb_math_arc_cosine(2.0)));
  TORB_CHECK(torb_is_nan_f64(torb_math_power(-1.0, 0.5)));
  /* A pole, not a domain error: `log(0)` is `-infinity`, which is still not a panic. */
  {
    double zero = 0.0;
    double negative_infinity = -1.0 / zero;
    TORB_CHECK(torb_math_natural_log(0.0) == negative_infinity);
    TORB_CHECK(!torb_is_nan_f64(torb_math_natural_log(0.0)));
  }
}

void torb_register_number_tests(void) {
  TORB_ADD(addition_overflows_at_every_width);
  TORB_ADD(subtraction_overflows_at_every_width);
  TORB_ADD(multiplication_overflows_at_every_width);
  TORB_ADD(division_truncates_toward_zero);
  TORB_ADD(division_by_zero_and_the_smallest_value_by_minus_one_panic);
  TORB_ADD(negate_and_absolute_overflow_at_the_smallest_value);
  TORB_ADD(the_bit_operations_keep_their_width);
  TORB_ADD(shifts_are_arithmetic_for_signed_and_logical_for_unsigned);
  TORB_ADD(a_shift_out_of_range_panics);
  TORB_ADD(the_wrapping_operations_of_uint64_do_not_panic);
  TORB_ADD(the_narrowing_conversions_check_their_range);
  TORB_ADD(the_remainder_of_two_floats_is_fmod);
  TORB_ADD(the_total_order_of_floats_puts_nan_above_everything);
  TORB_ADD(parsing_integers_checks_the_range);
  TORB_ADD(math_functions_match_known_values);
  TORB_ADD(math_domain_errors_answer_nan_and_never_panic);
}
