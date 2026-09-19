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

#define TORB_NARROWING_FROM_I64(suffix, type, lowest, highest)                    \
  bool torb_convert_i64_##suffix##_checked(int64_t value, type *out) {            \
    if (value < (int64_t)(lowest) || value > (int64_t)(highest)) {                \
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

bool torb_convert_i64_u64_checked(int64_t value, uint64_t *out) {
  if (value < 0) {
    return false;
  }
  *out = (uint64_t)value;
  return true;
}

bool torb_convert_u64_i64_checked(uint64_t value, int64_t *out) {
  if (value > (uint64_t)INT64_MAX) {
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

bool torb_convert_f64_i64_checked(double value, int64_t *out) {
  double truncated;
  if (isnan(value) || isinf(value)) {
    return false;
  }
  truncated = value < 0.0 ? ceil(value) : floor(value);
  /* 2^63 is not representable as an `Int64`, and `(double)INT64_MAX` rounds up to it. */
  if (truncated < -9223372036854775808.0 || truncated >= 9223372036854775808.0) {
    return false;
  }
  *out = (int64_t)truncated;
  return true;
}

double torb_square_root_f64(double value) {
  return sqrt(value);
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
