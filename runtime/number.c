/*
 * number.c - what the checked arithmetic of `torb_number.h` cannot be inline: the float routines and the one
 * conversion that needs `<math.h>`.
 *
 * The integer helpers are `static inline` in the header, because the C compiler has to see through them: an addition
 * that is a single instruction plus a branch must not become a call.
 */

#include "torb.h"

#include <math.h>

uint64_t torb_added_wrapping_u64(uint64_t first, uint64_t second) {
  return first + second;
}

uint64_t torb_multiplied_wrapping_u64(uint64_t first, uint64_t second) {
  return first * second;
}

/*
 * `base ** exponent` on magnitudes, false where it passes `limit`. The factor is squared only while a bit of the
 * exponent is left to use it: then the result is at least the square, so a square past the limit is an overflow of
 * the result as well, and nothing is ever reported that the result would not have.
 */
static bool torb_power_magnitude(uint64_t base, int64_t exponent, uint64_t limit, uint64_t *out) {
  uint64_t result = 1;
  uint64_t factor = base;
  while (exponent > 0) {
    if ((exponent & 1) != 0) {
      if (factor != 0 && result > limit / factor) {
        return false;
      }
      result *= factor;
    }
    exponent >>= 1;
    if (exponent > 0) {
      if (factor != 0 && factor > limit / factor) {
        return false;
      }
      factor *= factor;
    }
  }
  *out = result;
  return true;
}

/* The sign is known before the first multiplication: negative exactly when the base is and the exponent is odd. */
static int64_t torb_power_signed(int64_t base, int64_t exponent, int64_t lowest, int64_t highest, torb_location at) {
  bool negative;
  uint64_t magnitude;
  uint64_t limit;
  uint64_t result;
  if (exponent < 0) torb_panic_negative_exponent(exponent, at);
  negative = base < 0 && (exponent & 1) != 0;
  magnitude = base < 0 ? (uint64_t)0 - (uint64_t)base : (uint64_t)base;
  limit = negative ? (uint64_t)0 - (uint64_t)lowest : (uint64_t)highest;
  if (!torb_power_magnitude(magnitude, exponent, limit, &result)) torb_panic_overflow("**", at);
  return negative ? (int64_t)((uint64_t)0 - result) : (int64_t)result;
}

static uint64_t torb_power_unsigned(uint64_t base, int64_t exponent, uint64_t highest, torb_location at) {
  uint64_t result;
  if (exponent < 0) torb_panic_negative_exponent(exponent, at);
  if (!torb_power_magnitude(base, exponent, highest, &result)) torb_panic_overflow("**", at);
  return result;
}

int8_t torb_power_i8(int8_t base, int64_t exponent, torb_location at) {
  return (int8_t)torb_power_signed(base, exponent, INT8_MIN, INT8_MAX, at);
}

int16_t torb_power_i16(int16_t base, int64_t exponent, torb_location at) {
  return (int16_t)torb_power_signed(base, exponent, INT16_MIN, INT16_MAX, at);
}

int32_t torb_power_i32(int32_t base, int64_t exponent, torb_location at) {
  return (int32_t)torb_power_signed(base, exponent, INT32_MIN, INT32_MAX, at);
}

int64_t torb_power_i64(int64_t base, int64_t exponent, torb_location at) {
  return torb_power_signed(base, exponent, INT64_MIN, INT64_MAX, at);
}

uint8_t torb_power_u8(uint8_t base, int64_t exponent, torb_location at) {
  return (uint8_t)torb_power_unsigned(base, exponent, UINT8_MAX, at);
}

uint16_t torb_power_u16(uint16_t base, int64_t exponent, torb_location at) {
  return (uint16_t)torb_power_unsigned(base, exponent, UINT16_MAX, at);
}

uint32_t torb_power_u32(uint32_t base, int64_t exponent, torb_location at) {
  return (uint32_t)torb_power_unsigned(base, exponent, UINT32_MAX, at);
}

uint64_t torb_power_u64(uint64_t base, int64_t exponent, torb_location at) {
  return torb_power_unsigned(base, exponent, UINT64_MAX, at);
}

#define TORB_NARROWING_FROM_I64(suffix, type, lowest, highest)                    \
  bool torb_convert_i64_##suffix##_checked(int64_t value, type *out,             \
                                          torb_text *message) {                 \
    if (value < (int64_t)(lowest) || value > (int64_t)(highest)) {                \
      *message = torb_show_i64(value);                                          \
      return false;                                                              \
    }                                                                            \
    *out = (type)value;                                                          \
    return true;                                                                 \
  }

TORB_NARROWING_FROM_I64(i8, int8_t, INT8_MIN, INT8_MAX)
TORB_NARROWING_FROM_I64(i16, int16_t, INT16_MIN, INT16_MAX)
TORB_NARROWING_FROM_I64(i32, int32_t, INT32_MIN, INT32_MAX)
TORB_NARROWING_FROM_I64(u8, uint8_t, 0, UINT8_MAX)
TORB_NARROWING_FROM_I64(u16, uint16_t, 0, UINT16_MAX)
TORB_NARROWING_FROM_I64(u32, uint32_t, 0, UINT32_MAX)

bool torb_convert_i64_u64_checked(int64_t value, uint64_t *out, torb_text *message) {
  if (value < 0) {
    *message = torb_show_i64(value);
    return false;
  }
  *out = (uint64_t)value;
  return true;
}

bool torb_convert_u64_i64_checked(uint64_t value, int64_t *out, torb_text *message) {
  if (value > (uint64_t)INT64_MAX) {
    *message = torb_show_u64(value);
    return false;
  }
  *out = (int64_t)value;
  return true;
}

int32_t torb_compare_f64(double first, double second) {
  /* A total order: `nan` above everything, `-0.0` equal to `0.0`, so a sort terminates (decided gap 5). */
  bool first_is_nan = isnan(first) ? true : false;
  bool second_is_nan = isnan(second) ? true : false;
  if (first_is_nan || second_is_nan) {
    if (first_is_nan && second_is_nan) {
      return 0;
    }
    return first_is_nan ? 1 : -1;
  }
  if (first < second) {
    return -1;
  }
  if (first > second) {
    return 1;
  }
  return 0;
}

int32_t torb_compare_f32(float first, float second) {
  return torb_compare_f64((double)first, (double)second);
}

bool torb_convert_f64_i64_checked(double value, int64_t *out, torb_text *message) {
  double truncated;
  if (isnan(value) || isinf(value)) {
    *message = torb_show_f64(value);
    return false;
  }
  truncated = value < 0.0 ? ceil(value) : floor(value);
  /* 2^63 is not representable as an `Int64`, and `(double)INT64_MAX` rounds up to it. */
  if (truncated < -9223372036854775808.0 || truncated >= 9223372036854775808.0) {
    *message = torb_show_f64(value);
    return false;
  }
  *out = (int64_t)truncated;
  return true;
}

double torb_remainder_f64(double first, double second) {
  return fmod(first, second);
}

float torb_remainder_f32(float first, float second) {
  return fmodf(first, second);
}

double torb_square_root_f64(double value) {
  return sqrt(value);
}

double torb_power_f64(double base, double exponent) {
  return pow(base, exponent);
}

double torb_exponential_f64(double value) {
  return exp(value);
}

double torb_natural_logarithm_f64(double value) {
  return log(value);
}

double torb_sine_f64(double value) {
  return sin(value);
}

double torb_cosine_f64(double value) {
  return cos(value);
}

double torb_tangent_f64(double value) {
  return tan(value);
}

double torb_arc_sine_f64(double value) {
  return asin(value);
}

double torb_arc_cosine_f64(double value) {
  return acos(value);
}

double torb_arc_tangent_f64(double value) {
  return atan(value);
}

double torb_arc_tangent_divided_f64(double value, double by) {
  return atan2(value, by);
}

double torb_floor_f64(double value) {
  return floor(value);
}

double torb_ceiling_f64(double value) {
  return ceil(value);
}

double torb_round_f64(double value) {
  return round(value);
}

bool torb_is_nan_f64(double value) {
  return isnan(value) ? true : false;
}

/*
 * `std/math`: thin wrappers over `<math.h>`. A domain error (`torb_math_natural_log(-1.0)`,
 * `torb_math_arc_sine(2.0)`, ...) answers `nan` from libm itself, exactly like `torb_square_root_f64` already does;
 * none of these ever panics. Bit-identical results across platforms are only guaranteed where libm itself guarantees
 * them - the runtime does not try to improve on libm.
 */

double torb_math_power(double base, double exponent) {
  return pow(base, exponent);
}

double torb_math_exponential(double value) {
  return exp(value);
}

double torb_math_natural_log(double value) {
  return log(value);
}

double torb_math_logarithm(double value, double base) {
  return log(value) / log(base);
}

double torb_math_sine(double value) {
  return sin(value);
}

double torb_math_cosine(double value) {
  return cos(value);
}

double torb_math_tangent(double value) {
  return tan(value);
}

double torb_math_arc_sine(double value) {
  return asin(value);
}

double torb_math_arc_cosine(double value) {
  return acos(value);
}

double torb_math_arc_tangent(double value) {
  return atan(value);
}

double torb_math_arc_tangent2(double y, double x) {
  return atan2(y, x);
}
