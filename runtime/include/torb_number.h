/*
 * torb_number.h - the checked arithmetic of every integer width, and the numeric conversions.
 *
 * Included by torb.h; never included on its own.
 *
 * Overflow checks are on in every profile: they are semantics, not diagnostics ("Integer overflow panics, in every
 * back end"). Division truncates toward zero and the remainder takes the sign of the dividend; `x / 0`, `x % 0` and
 * the smallest value of a signed type divided by `-1` panic (decided gap 2). A shift by a negative amount or by the
 * width of the type or more panics (decided gap 3).
 *
 * All parameters are plain numbers, so there is no ownership to state. `at` is borrowed.
 *
 * The narrow widths compute in 64 bits and check the range, which is exact and needs no builtin. Only the 64 bit
 * operations need the compiler's overflow builtins, and they have a portable fallback so MSVC works.
 */

#ifndef TORB_NUMBER_H
#define TORB_NUMBER_H

#ifndef TORB_H
#error "include torb.h, not torb_number.h"
#endif

/* ------------------------------------------------------------------------------------- the narrow signed widths --- */

#define TORB_DEFINE_NARROW_SIGNED(suffix, type, lowest, highest, width)                                            \
  static inline type torb_add_##suffix(type first, type second, torb_location at) {                                \
    int64_t result = (int64_t)first + (int64_t)second;                                                             \
    if (result < (lowest) || result > (highest)) torb_panic_overflow("+", at);                                     \
    return (type)result;                                                                                           \
  }                                                                                                                \
  static inline type torb_subtract_##suffix(type first, type second, torb_location at) {                           \
    int64_t result = (int64_t)first - (int64_t)second;                                                             \
    if (result < (lowest) || result > (highest)) torb_panic_overflow("-", at);                                     \
    return (type)result;                                                                                           \
  }                                                                                                                \
  static inline type torb_multiply_##suffix(type first, type second, torb_location at) {                           \
    int64_t result = (int64_t)first * (int64_t)second;                                                             \
    if (result < (lowest) || result > (highest)) torb_panic_overflow("*", at);                                      \
    return (type)result;                                                                                           \
  }                                                                                                                \
  static inline type torb_divide_##suffix(type first, type second, torb_location at) {                             \
    if (second == 0) torb_panic_division_by_zero("/", at);                                                          \
    if (first == (type)(lowest) && second == (type)-1) torb_panic_overflow("/", at);                                \
    return (type)(first / second);                                                                                  \
  }                                                                                                                \
  static inline type torb_remainder_##suffix(type first, type second, torb_location at) {                          \
    if (second == 0) torb_panic_division_by_zero("%", at);                                                          \
    if (first == (type)(lowest) && second == (type)-1) torb_panic_overflow("%", at);                                \
    return (type)(first % second);                                                                                  \
  }                                                                                                                \
  static inline type torb_negate_##suffix(type value, torb_location at) {                                          \
    if (value == (type)(lowest)) torb_panic_overflow("-", at);                                                      \
    return (type)(-value);                                                                                          \
  }                                                                                                                \
  static inline type torb_absolute_##suffix(type value, torb_location at) {                                        \
    if (value == (type)(lowest)) torb_panic_overflow("absolute", at);                                               \
    return value < 0 ? (type)(-value) : value;                                                                      \
  }                                                                                                                \
  static inline type torb_bitwise_and_##suffix(type first, type second) { return (type)(first & second); }          \
  static inline type torb_bitwise_or_##suffix(type first, type second) { return (type)(first | second); }           \
  static inline type torb_bitwise_exclusive_or_##suffix(type first, type second) { return (type)(first ^ second); } \
  static inline type torb_bitwise_not_##suffix(type value) { return (type)(~(uint64_t)(uint##width##_t)value); }    \
  static inline type torb_shifted_left_##suffix(type value, int64_t by, torb_location at) {                        \
    if (by < 0 || by >= (width)) torb_panic_shift_amount(by, (width), at);                                          \
    return (type)(uint##width##_t)((uint64_t)(uint##width##_t)value << by);                                         \
  }                                                                                                                \
  static inline type torb_shifted_right_##suffix(type value, int64_t by, torb_location at) {                       \
    if (by < 0 || by >= (width)) torb_panic_shift_amount(by, (width), at);                                          \
    if (value >= 0) return (type)((uint64_t)(uint##width##_t)value >> by);                                          \
    return (type)(uint##width##_t)(~((~(uint64_t)(uint##width##_t)value & UINT##width##_MAX) >> by));               \
  }

TORB_DEFINE_NARROW_SIGNED(i8, int8_t, INT8_MIN, INT8_MAX, 8)
TORB_DEFINE_NARROW_SIGNED(i16, int16_t, INT16_MIN, INT16_MAX, 16)
TORB_DEFINE_NARROW_SIGNED(i32, int32_t, INT32_MIN, INT32_MAX, 32)

/* ----------------------------------------------------------------------------------- the narrow unsigned widths --- */

#define TORB_DEFINE_NARROW_UNSIGNED(suffix, type, highest, width)                                                   \
  static inline type torb_add_##suffix(type first, type second, torb_location at) {                                \
    uint64_t result = (uint64_t)first + (uint64_t)second;                                                          \
    if (result > (uint64_t)(highest)) torb_panic_overflow("+", at);                                                 \
    return (type)result;                                                                                            \
  }                                                                                                                \
  static inline type torb_subtract_##suffix(type first, type second, torb_location at) {                           \
    if (second > first) torb_panic_overflow("-", at);                                                               \
    return (type)(first - second);                                                                                  \
  }                                                                                                                \
  static inline type torb_multiply_##suffix(type first, type second, torb_location at) {                           \
    uint64_t result = (uint64_t)first * (uint64_t)second;                                                           \
    if (result > (uint64_t)(highest)) torb_panic_overflow("*", at);                                                 \
    return (type)result;                                                                                            \
  }                                                                                                                \
  static inline type torb_divide_##suffix(type first, type second, torb_location at) {                             \
    if (second == 0) torb_panic_division_by_zero("/", at);                                                          \
    return (type)(first / second);                                                                                  \
  }                                                                                                                \
  static inline type torb_remainder_##suffix(type first, type second, torb_location at) {                          \
    if (second == 0) torb_panic_division_by_zero("%", at);                                                          \
    return (type)(first % second);                                                                                  \
  }                                                                                                                \
  static inline type torb_bitwise_and_##suffix(type first, type second) { return (type)(first & second); }          \
  static inline type torb_bitwise_or_##suffix(type first, type second) { return (type)(first | second); }           \
  static inline type torb_bitwise_exclusive_or_##suffix(type first, type second) { return (type)(first ^ second); } \
  static inline type torb_bitwise_not_##suffix(type value) { return (type)(~(uint64_t)value & (uint64_t)(highest)); }\
  static inline type torb_shifted_left_##suffix(type value, int64_t by, torb_location at) {                        \
    if (by < 0 || by >= (width)) torb_panic_shift_amount(by, (width), at);                                          \
    return (type)(((uint64_t)value << by) & (uint64_t)(highest));                                                   \
  }                                                                                                                \
  static inline type torb_shifted_right_##suffix(type value, int64_t by, torb_location at) {                       \
    if (by < 0 || by >= (width)) torb_panic_shift_amount(by, (width), at);                                          \
    return (type)((uint64_t)value >> by);                                                                           \
  }

TORB_DEFINE_NARROW_UNSIGNED(u8, uint8_t, UINT8_MAX, 8)
TORB_DEFINE_NARROW_UNSIGNED(u16, uint16_t, UINT16_MAX, 16)
TORB_DEFINE_NARROW_UNSIGNED(u32, uint32_t, UINT32_MAX, 32)

/* --------------------------------------------------------------------------------------------------- Int64 --- */

static inline int64_t torb_add_i64(int64_t first, int64_t second, torb_location at) {
#if TORB_HAS_OVERFLOW_BUILTINS
  int64_t result;
  if (__builtin_add_overflow(first, second, &result)) torb_panic_overflow("+", at);
  return result;
#else
  uint64_t result = (uint64_t)first + (uint64_t)second;
  if (((((uint64_t)first ^ result) & ((uint64_t)second ^ result)) >> 63) != 0) torb_panic_overflow("+", at);
  return (int64_t)result;
#endif
}

static inline int64_t torb_subtract_i64(int64_t first, int64_t second, torb_location at) {
#if TORB_HAS_OVERFLOW_BUILTINS
  int64_t result;
  if (__builtin_sub_overflow(first, second, &result)) torb_panic_overflow("-", at);
  return result;
#else
  uint64_t result = (uint64_t)first - (uint64_t)second;
  if (((((uint64_t)first ^ (uint64_t)second) & ((uint64_t)first ^ result)) >> 63) != 0) torb_panic_overflow("-", at);
  return (int64_t)result;
#endif
}

static inline int64_t torb_multiply_i64(int64_t first, int64_t second, torb_location at) {
#if TORB_HAS_OVERFLOW_BUILTINS
  int64_t result;
  if (__builtin_mul_overflow(first, second, &result)) torb_panic_overflow("*", at);
  return result;
#elif TORB_HAS_INT128
  __int128 result = (__int128)first * (__int128)second;
  if (result < (__int128)INT64_MIN || result > (__int128)INT64_MAX) torb_panic_overflow("*", at);
  return (int64_t)result;
#else
  if (first == 0 || second == 0) return 0;
  if (first == -1 && second == INT64_MIN) torb_panic_overflow("*", at);
  if (second == -1 && first == INT64_MIN) torb_panic_overflow("*", at);
  {
    int64_t result = (int64_t)((uint64_t)first * (uint64_t)second);
    if (result / second != first) torb_panic_overflow("*", at);
    return result;
  }
#endif
}

static inline int64_t torb_divide_i64(int64_t first, int64_t second, torb_location at) {
  if (second == 0) torb_panic_division_by_zero("/", at);
  if (first == INT64_MIN && second == -1) torb_panic_overflow("/", at);
  return first / second;
}

static inline int64_t torb_remainder_i64(int64_t first, int64_t second, torb_location at) {
  if (second == 0) torb_panic_division_by_zero("%", at);
  if (first == INT64_MIN && second == -1) torb_panic_overflow("%", at);
  return first % second;
}

static inline int64_t torb_negate_i64(int64_t value, torb_location at) {
  if (value == INT64_MIN) torb_panic_overflow("-", at);
  return -value;
}

static inline int64_t torb_absolute_i64(int64_t value, torb_location at) {
  if (value == INT64_MIN) torb_panic_overflow("absolute", at);
  return value < 0 ? -value : value;
}

static inline int64_t torb_bitwise_and_i64(int64_t first, int64_t second) { return first & second; }
static inline int64_t torb_bitwise_or_i64(int64_t first, int64_t second) { return first | second; }
static inline int64_t torb_bitwise_exclusive_or_i64(int64_t first, int64_t second) { return first ^ second; }
static inline int64_t torb_bitwise_not_i64(int64_t value) { return (int64_t)(~(uint64_t)value); }

static inline int64_t torb_shifted_left_i64(int64_t value, int64_t by, torb_location at) {
  if (by < 0 || by >= 64) torb_panic_shift_amount(by, 64, at);
  return (int64_t)((uint64_t)value << by);
}

static inline int64_t torb_shifted_right_i64(int64_t value, int64_t by, torb_location at) {
  if (by < 0 || by >= 64) torb_panic_shift_amount(by, 64, at);
  if (value >= 0) return (int64_t)((uint64_t)value >> by);
  return (int64_t)(~(~(uint64_t)value >> by));
}

/* -------------------------------------------------------------------------------------------------- UInt64 --- */

static inline uint64_t torb_add_u64(uint64_t first, uint64_t second, torb_location at) {
  uint64_t result = first + second;
  if (result < first) torb_panic_overflow("+", at);
  return result;
}

static inline uint64_t torb_subtract_u64(uint64_t first, uint64_t second, torb_location at) {
  if (second > first) torb_panic_overflow("-", at);
  return first - second;
}

static inline uint64_t torb_multiply_u64(uint64_t first, uint64_t second, torb_location at) {
  uint64_t result = first * second;
  if (first != 0 && result / first != second) torb_panic_overflow("*", at);
  return result;
}

static inline uint64_t torb_divide_u64(uint64_t first, uint64_t second, torb_location at) {
  if (second == 0) torb_panic_division_by_zero("/", at);
  return first / second;
}

static inline uint64_t torb_remainder_u64(uint64_t first, uint64_t second, torb_location at) {
  if (second == 0) torb_panic_division_by_zero("%", at);
  return first % second;
}

static inline uint64_t torb_bitwise_and_u64(uint64_t first, uint64_t second) { return first & second; }
static inline uint64_t torb_bitwise_or_u64(uint64_t first, uint64_t second) { return first | second; }
static inline uint64_t torb_bitwise_exclusive_or_u64(uint64_t first, uint64_t second) { return first ^ second; }
static inline uint64_t torb_bitwise_not_u64(uint64_t value) { return ~value; }

static inline uint64_t torb_shifted_left_u64(uint64_t value, int64_t by, torb_location at) {
  if (by < 0 || by >= 64) torb_panic_shift_amount(by, 64, at);
  return value << by;
}

static inline uint64_t torb_shifted_right_u64(uint64_t value, int64_t by, torb_location at) {
  if (by < 0 || by >= 64) torb_panic_shift_amount(by, 64, at);
  return value >> by;
}

/**
 * The only arithmetic in the language that wraps instead of panicking (decided gap 3), and it exists so that a hash
 * function can mix bits. `UInt64` only.
 *
 * Ordinary functions, not `static inline`, because the manifest maps them to a runtime symbol and `torb_natives.h`
 * declares them - a `static inline` cannot be declared twice with external linkage.
 */
uint64_t torb_added_wrapping_u64(uint64_t first, uint64_t second);
uint64_t torb_multiplied_wrapping_u64(uint64_t first, uint64_t second);

/* --------------------------------------------------------------------------------------------- conversions --- */

/**
 * The narrowing conversions of `TryFrom`. The runtime answers the check; the lowering builds the
 * `Result<Int32, NumberRangeError>` around it, because a `Result` is a layout of the program and not of the runtime.
 * Ordinary functions for the same reason as the wrapping pair above.
 *
 * `message` is the `.Fallible` convention of the manifest: a `NumberRangeError` carries a `message`, no parameter of
 * `tryFrom` names it, and what went out of range is something only the value knows - so the runtime writes it, exactly
 * as it writes the `message` of an `IoError`. It is set on failure alone and untouched on success.
 */
bool torb_convert_i64_i8_checked(int64_t value, int8_t *out, torb_text *message);
bool torb_convert_i64_i16_checked(int64_t value, int16_t *out, torb_text *message);
bool torb_convert_i64_i32_checked(int64_t value, int32_t *out, torb_text *message);
bool torb_convert_i64_u8_checked(int64_t value, uint8_t *out, torb_text *message);
bool torb_convert_i64_u16_checked(int64_t value, uint16_t *out, torb_text *message);
bool torb_convert_i64_u32_checked(int64_t value, uint32_t *out, torb_text *message);
bool torb_convert_i64_u64_checked(int64_t value, uint64_t *out, torb_text *message);
bool torb_convert_u64_i64_checked(uint64_t value, int64_t *out, torb_text *message);
/** `Int64.tryFrom(Float64)`: truncation toward zero. A `nan`, an infinity and anything out of range answer false. */
bool torb_convert_f64_i64_checked(double value, int64_t *out, torb_text *message);

/**
 * `a % b` on a float: `fmod`, the one arithmetic operator of a float that is not a C operator. A remainder by zero
 * answers `nan` and never panics, exactly as `a / 0.0` answers an infinity - only the integers panic (decided gap 2).
 */
double torb_remainder_f64(double first, double second);
float torb_remainder_f32(float first, float second);

/* -------------------------------------------------------------------------------------------------- floats --- */

/** `==` is IEEE-754: `nan != nan`, and `0.0 == -0.0` (decided gap 5). */
static inline bool torb_equal_f64(double first, double second) { return first == second; }
static inline bool torb_equal_f32(float first, float second) { return first == second; }

/**
 * `compare` is a **total** order: `nan` is above everything and `-0.0` compares equal to `0.0`, so a sort terminates
 * whatever pivot it picks and whatever is in the list (decided gap 5). -1, 0 or 1.
 */
int32_t torb_compare_f64(double first, double second);
int32_t torb_compare_f32(float first, float second);

double torb_square_root_f64(double value);
double torb_floor_f64(double value);
double torb_ceiling_f64(double value);
/** Half away from zero, like `round` in C99. */
double torb_round_f64(double value);
bool torb_is_nan_f64(double value);

#endif /* TORB_NUMBER_H */
