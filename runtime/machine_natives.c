/*
 * machine_natives.c - generated from the natives manifest by `torb natives --header`. Do not edit.
 *
 * One thunk per ready function of the runtime, in the manifest's sorted order: how the bytecode VM calls the
 * runtime with the words of its registers (docs/design/VM.md section 6, runtime/machine.c).
 */

#include "torb.h"
#include "torb_natives.h"
#include "torb_machine.h"

#include <string.h>

/* torb_added_wrapping_u64 */
static void torb_machine_native_0(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  uint64_t a1 = (uint64_t)words[base + operands[2]];
  uint64_t r = torb_added_wrapping_u64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_blocking_turn */
static void torb_machine_native_1(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_task *r = torb_blocking_turn();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_ceiling_f64 */
static void torb_machine_native_2(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_ceiling_f64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_channel_close */
static void torb_machine_native_3(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_channel *a0 = (torb_channel *)(intptr_t)words[base + operands[1]];
  torb_channel_close(a0);
}

/* torb_channel_end */
static void torb_machine_native_4(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_channel *a0 = (torb_channel *)(intptr_t)words[base + operands[1]];
  torb_channel_end(a0);
}

/* torb_channel_offered */
static void torb_machine_native_5(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_channel *a0 = (torb_channel *)(intptr_t)words[base + operands[1]];
  const void *a1 = (const void *)(words + base + operands[2]);
  torb_task *r = torb_channel_offered(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_channel_received */
static void torb_machine_native_6(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_channel *a0 = (torb_channel *)(intptr_t)words[base + operands[1]];
  torb_task *r = torb_channel_received(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_char_byte_length_of */
static void torb_machine_native_7(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  int64_t r = torb_char_byte_length_of(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_is_digit */
static void torb_machine_native_8(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  bool r = torb_char_is_digit(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_is_letter */
static void torb_machine_native_9(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  bool r = torb_char_is_letter(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_is_whitespace */
static void torb_machine_native_10(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  bool r = torb_char_is_whitespace(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_to_lower_case */
static void torb_machine_native_11(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  torb_char r = torb_char_to_lower_case(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_to_upper_case */
static void torb_machine_native_12(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  torb_char r = torb_char_to_upper_case(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_char_try_from_i64 */
static void torb_machine_native_13(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  torb_char t1 = (torb_char)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_char_try_from_i64(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_clock_milliseconds */
static void torb_machine_native_14(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_clock_milliseconds();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_clock_now */
static void torb_machine_native_15(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_clock_now();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_compare_f64 */
static void torb_machine_native_16(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double a1 = torb_machine_double_of(words[base + operands[2]]);
  int32_t r = torb_compare_f64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_f64_i64_checked */
static void torb_machine_native_17(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_f64_i64_checked(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_i16_checked */
static void torb_machine_native_18(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  int16_t t1 = (int16_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_i16_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_i32_checked */
static void torb_machine_native_19(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  int32_t t1 = (int32_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_i32_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_i8_checked */
static void torb_machine_native_20(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  int8_t t1 = (int8_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_i8_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_u16_checked */
static void torb_machine_native_21(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  uint16_t t1 = (uint16_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_u16_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_u32_checked */
static void torb_machine_native_22(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  uint32_t t1 = (uint32_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_u32_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_u64_checked */
static void torb_machine_native_23(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  uint64_t *a1 = (uint64_t *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_u64_checked(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_i64_u8_checked */
static void torb_machine_native_24(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t *p1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  uint8_t t1 = (uint8_t)*p1;
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_i64_u8_checked(a0, &t1, a2);
  *p1 = (int64_t)t1;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_convert_u64_i64_checked */
static void torb_machine_native_25(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_convert_u64_i64_checked(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_environment_entries */
static void torb_machine_native_26(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  torb_list *a1 = (torb_list *)torb_machine_address(words, words[base + operands[2]]);
  torb_environment_entries(a0, a1);
}

/* torb_environment_get */
static void torb_machine_native_27(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  bool r = torb_environment_get(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_absolute_path */
static void torb_machine_native_28(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_file_absolute_path(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_close */
static void torb_machine_native_29(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_file **a0 = (torb_file **)torb_machine_address(words, words[base + operands[1]]);
  torb_file_close(a0);
}

/* torb_file_create_directory */
static void torb_machine_native_30(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  bool r = torb_file_create_directory(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_exists */
static void torb_machine_native_31(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  bool r = torb_file_exists(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_is_directory */
static void torb_machine_native_32(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  bool r = torb_file_is_directory(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_list */
static void torb_machine_native_33(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_list *a1 = (torb_list *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_file_list(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_open */
static void torb_machine_native_34(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_file **a1 = (torb_file **)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_file_open(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_read_all */
static void torb_machine_native_35(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_file **a0 = (torb_file **)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  bool r = torb_file_read_all(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_read_text */
static void torb_machine_native_36(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_file_read_text(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_file_write_text */
static void torb_machine_native_37(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_file_write_text(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_floor_f64 */
static void torb_machine_native_38(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_floor_f64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_hash_bool */
static void torb_machine_native_39(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  bool a0 = (bool)words[base + operands[1]];
  uint64_t r = torb_hash_bool(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_hash_char */
static void torb_machine_native_40(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  uint64_t r = torb_hash_char(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_hash_combine */
static void torb_machine_native_41(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  uint64_t a1 = (uint64_t)words[base + operands[2]];
  uint64_t r = torb_hash_combine(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_hash_i64 */
static void torb_machine_native_42(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  uint64_t r = torb_hash_i64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_hash_u64 */
static void torb_machine_native_43(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  uint64_t r = torb_hash_u64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_is_nan_f64 */
static void torb_machine_native_44(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  bool r = torb_is_nan_f64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_list_add */
static void torb_machine_native_45(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  const void *a1 = (const void *)(words + base + operands[2]);
  torb_list_add(a0, a1);
}

/* torb_list_clear */
static void torb_machine_native_46(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  torb_list_clear(a0);
}

/* torb_list_compact */
static void torb_machine_native_47(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  torb_list_compact(a0);
}

/* torb_list_get */
static void torb_machine_native_48(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  bool r = torb_list_get(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_list_insert */
static void torb_machine_native_49(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  const void *a2 = (const void *)(words + base + operands[3]);
  torb_list_insert(a0, a1, a2, torb_machine_location(operands[4]));
}

/* torb_list_length */
static void torb_machine_native_50(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t r = torb_list_length(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_list_remove_at */
static void torb_machine_native_51(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  bool r = torb_list_remove_at(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_list_replace */
static void torb_machine_native_52(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t a2 = (int64_t)words[base + operands[3]];
  torb_list a3;
  memcpy(&a3, words + base + operands[4], sizeof a3);
  torb_list_replace(a0, a1, a2, a3, torb_machine_location(operands[5]));
}

/* torb_list_reverse */
static void torb_machine_native_53(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  torb_list_reverse(a0);
}

/* torb_list_set */
static void torb_machine_native_54(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  const void *a2 = (const void *)(words + base + operands[3]);
  torb_list_set(a0, a1, a2, torb_machine_location(operands[4]));
}

/* torb_list_slice */
static void torb_machine_native_55(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t a2 = (int64_t)words[base + operands[3]];
  torb_list r = torb_list_slice(a0, a1, a2, torb_machine_location(operands[4]));
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_machine_install */
static void torb_machine_native_56(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: the VM cannot call `torb_machine_install` yet", torb_location_unknown);
}

/* torb_machine_load */
static void torb_machine_native_57(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t r = torb_machine_load(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_machine_operate */
static void torb_machine_native_58(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  torb_list a2;
  memcpy(&a2, words + base + operands[3], sizeof a2);
  int64_t a3 = (int64_t)words[base + operands[4]];
  int64_t r = torb_machine_operate(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_machine_place_float */
static void torb_machine_native_59(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  double a2 = torb_machine_double_of(words[base + operands[3]]);
  torb_machine_place_float(a0, a1, a2);
}

/* torb_machine_place_text */
static void torb_machine_native_60(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list *a0 = (torb_list *)torb_machine_address(words, words[base + operands[1]]);
  int64_t a1 = (int64_t)words[base + operands[2]];
  torb_text a2;
  memcpy(&a2, words + base + operands[3], sizeof a2);
  torb_machine_place_text(a0, a1, a2);
}

/* torb_machine_store */
static void torb_machine_native_61(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  int64_t a1 = (int64_t)words[base + operands[2]];
  torb_machine_store(a0, a1);
}

/* torb_map_clear */
static void torb_machine_native_62(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map *a0 = (torb_map *)torb_machine_address(words, words[base + operands[1]]);
  torb_map_clear(a0);
}

/* torb_map_entry_after */
static void torb_machine_native_63(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  void *a3 = (void *)torb_machine_address(words, words[base + operands[4]]);
  memset(a3, 0, sizeof(int64_t));
  bool r = torb_map_entry_after(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_map_get */
static void torb_machine_native_64(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  const void *a1 = (const void *)(words + base + operands[2]);
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  bool r = torb_map_get(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_map_length */
static void torb_machine_native_65(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t r = torb_map_length(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_map_remove */
static void torb_machine_native_66(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map *a0 = (torb_map *)torb_machine_address(words, words[base + operands[1]]);
  const void *a1 = (const void *)(words + base + operands[2]);
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  bool r = torb_map_remove(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_map_set */
static void torb_machine_native_67(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_map *a0 = (torb_map *)torb_machine_address(words, words[base + operands[1]]);
  const void *a1 = (const void *)(words + base + operands[2]);
  const void *a2 = (const void *)(words + base + operands[3]);
  torb_map_set(a0, a1, a2);
}

/* torb_math_arc_cosine */
static void torb_machine_native_68(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_arc_cosine(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_arc_sine */
static void torb_machine_native_69(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_arc_sine(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_arc_tangent */
static void torb_machine_native_70(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_arc_tangent(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_arc_tangent2 */
static void torb_machine_native_71(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double a1 = torb_machine_double_of(words[base + operands[2]]);
  double r = (double)torb_math_arc_tangent2(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_cosine */
static void torb_machine_native_72(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_cosine(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_exponential */
static void torb_machine_native_73(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_exponential(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_logarithm */
static void torb_machine_native_74(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double a1 = torb_machine_double_of(words[base + operands[2]]);
  double r = (double)torb_math_logarithm(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_natural_log */
static void torb_machine_native_75(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_natural_log(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_power */
static void torb_machine_native_76(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double a1 = torb_machine_double_of(words[base + operands[2]]);
  double r = (double)torb_math_power(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_sine */
static void torb_machine_native_77(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_sine(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_math_tangent */
static void torb_machine_native_78(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_math_tangent(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_multiplied_wrapping_u64 */
static void torb_machine_native_79(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  uint64_t a1 = (uint64_t)words[base + operands[2]];
  uint64_t r = torb_multiplied_wrapping_u64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_os_bsd_sysctl_integer */
#if defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_80(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  int64_t r = torb_os_bsd_sysctl_integer(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_80(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_bsd_sysctl_integer` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_bsd_sysctl_text */
#if defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_81(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  int64_t r = torb_os_bsd_sysctl_text(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_81(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_bsd_sysctl_text` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_bsd_uptime */
#if defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_82(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t *a0 = (int64_t *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  int64_t r = torb_os_bsd_uptime(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_82(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_bsd_uptime` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_linux_read_system_file */
#if defined(__linux__)
static void torb_machine_native_83(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  int64_t r = torb_os_linux_read_system_file(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_83(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_linux_read_system_file` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_macos_user_temporary_directory */
#if defined(__APPLE__)
static void torb_machine_native_84(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  int64_t r = torb_os_macos_user_temporary_directory(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_84(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_macos_user_temporary_directory` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_posix_account */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_85(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t *a0 = (int64_t *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  torb_text *a4 = (torb_text *)torb_machine_address(words, words[base + operands[5]]);
  int64_t r = torb_os_posix_account(a0, a1, a2, a3, a4);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_85(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_posix_account` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_posix_configuration */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_86(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t r = torb_os_posix_configuration(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_86(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_posix_configuration` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_posix_effective_user_identifier */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_87(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_os_posix_effective_user_identifier();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_87(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_posix_effective_user_identifier` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_posix_host_name */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_88(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  int64_t r = torb_os_posix_host_name(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_88(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_posix_host_name` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_posix_system_names */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
static void torb_machine_native_89(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  torb_text *a4 = (torb_text *)torb_machine_address(words, words[base + operands[5]]);
  torb_text *a5 = (torb_text *)torb_machine_address(words, words[base + operands[6]]);
  int64_t r = torb_os_posix_system_names(a0, a1, a2, a3, a4, a5);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_89(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_posix_system_names` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_computer_name */
#if defined(_WIN32)
static void torb_machine_native_90(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  int64_t r = torb_os_windows_computer_name(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_90(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_computer_name` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_known_folder */
#if defined(_WIN32)
static void torb_machine_native_91(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  int64_t r = torb_os_windows_known_folder(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_91(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_known_folder` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_registry_integer */
#if defined(_WIN32)
static void torb_machine_native_92(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  int64_t r = torb_os_windows_registry_integer(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_92(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_registry_integer` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_registry_text */
#if defined(_WIN32)
static void torb_machine_native_93(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  int64_t r = torb_os_windows_registry_text(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_93(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_registry_text` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_system_information */
#if defined(_WIN32)
static void torb_machine_native_94(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t *a0 = (int64_t *)torb_machine_address(words, words[base + operands[1]]);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  int64_t r = torb_os_windows_system_information(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_94(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_system_information` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_temporary_directory */
#if defined(_WIN32)
static void torb_machine_native_95(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  torb_text *a1 = (torb_text *)torb_machine_address(words, words[base + operands[2]]);
  int64_t r = torb_os_windows_temporary_directory(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_95(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_temporary_directory` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_tick_count */
#if defined(_WIN32)
static void torb_machine_native_96(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_os_windows_tick_count();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_96(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_tick_count` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_os_windows_version */
#if defined(_WIN32)
static void torb_machine_native_97(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t *a0 = (int64_t *)torb_machine_address(words, words[base + operands[1]]);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  int64_t r = torb_os_windows_version(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}
#else
static void torb_machine_native_97(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_panic_text("internal error: `torb_os_windows_version` does not exist on this operating system", torb_location_unknown);
}
#endif

/* torb_panic */
static void torb_machine_native_98(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_panic(a0, torb_machine_location(operands[2]));
}

/* torb_parse_f64 */
static void torb_machine_native_99(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  double *a1 = (double *)torb_machine_address(words, words[base + operands[2]]);
  bool r = torb_parse_f64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_parse_i64 */
static void torb_machine_native_100(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  bool r = torb_parse_i64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_parse_i64_digits */
static void torb_machine_native_101(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_parse_i64_digits(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_parse_u64 */
static void torb_machine_native_102(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  uint64_t *a1 = (uint64_t *)torb_machine_address(words, words[base + operands[2]]);
  bool r = torb_parse_u64(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_pause */
static void torb_machine_native_103(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_task *r = torb_pause();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_print_error_parts */
static void torb_machine_native_104(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  const torb_text *a0 = (const void *)(words + base + operands[1]);
  size_t a1 = (size_t)words[base + operands[2]];
  torb_print_error_parts(a0, a1);
}

/* torb_print_parts */
static void torb_machine_native_105(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  const torb_text *a0 = (const void *)(words + base + operands[1]);
  size_t a1 = (size_t)words[base + operands[2]];
  torb_print_parts(a0, a1);
}

/* torb_process_arguments */
static void torb_machine_native_106(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_list r = torb_machine_process_arguments();
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_process_executable_path */
static void torb_machine_native_107(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  bool r = torb_process_executable_path(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_process_exit */
static void torb_machine_native_108(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  torb_process_exit(a0);
}

/* torb_process_run */
static void torb_machine_native_109(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_list a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  int64_t r = torb_process_run(a0, a1, a2, a3);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_process_run_feeding */
static void torb_machine_native_110(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_list a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text a2;
  memcpy(&a2, words + base + operands[3], sizeof a2);
  torb_text *a3 = (torb_text *)torb_machine_address(words, words[base + operands[4]]);
  torb_text *a4 = (torb_text *)torb_machine_address(words, words[base + operands[5]]);
  int64_t r = torb_process_run_feeding(a0, a1, a2, a3, a4);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_process_run_inheriting */
static void torb_machine_native_111(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_list a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text *a2 = (torb_text *)torb_machine_address(words, words[base + operands[3]]);
  int64_t r = torb_process_run_inheriting(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_read_line */
static void torb_machine_native_112(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text *a0 = (torb_text *)torb_machine_address(words, words[base + operands[1]]);
  bool r = torb_read_line(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_round_f64 */
static void torb_machine_native_113(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_round_f64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_set_add */
static void torb_machine_native_114(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set *a0 = (torb_set *)torb_machine_address(words, words[base + operands[1]]);
  const void *a1 = (const void *)(words + base + operands[2]);
  torb_set_add(a0, a1);
}

/* torb_set_clear */
static void torb_machine_native_115(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set *a0 = (torb_set *)torb_machine_address(words, words[base + operands[1]]);
  torb_set_clear(a0);
}

/* torb_set_contains */
static void torb_machine_native_116(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  const void *a1 = (const void *)(words + base + operands[2]);
  bool r = torb_set_contains(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_set_item_after */
static void torb_machine_native_117(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t *a1 = (int64_t *)torb_machine_address(words, words[base + operands[2]]);
  void *a2 = (void *)torb_machine_address(words, words[base + operands[3]]);
  memset(a2, 0, sizeof(int64_t));
  bool r = torb_set_item_after(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_set_length */
static void torb_machine_native_118(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t r = torb_set_length(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_set_remove */
static void torb_machine_native_119(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_set *a0 = (torb_set *)torb_machine_address(words, words[base + operands[1]]);
  const void *a1 = (const void *)(words + base + operands[2]);
  bool r = torb_set_remove(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_show_bool */
static void torb_machine_native_120(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  bool a0 = (bool)words[base + operands[1]];
  torb_text r = torb_show_bool(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_char */
static void torb_machine_native_121(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  torb_text r = torb_show_char(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_char_nested */
static void torb_machine_native_122(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_char a0 = (torb_char)words[base + operands[1]];
  torb_text r = torb_show_char_nested(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_f64 */
static void torb_machine_native_123(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  torb_text r = torb_show_f64(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_i64 */
static void torb_machine_native_124(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t a0 = (int64_t)words[base + operands[1]];
  torb_text r = torb_show_i64(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_u64 */
static void torb_machine_native_125(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  uint64_t a0 = (uint64_t)words[base + operands[1]];
  torb_text r = torb_show_u64(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_show_void */
static void torb_machine_native_126(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_void a0 = (torb_void)words[base + operands[1]];
  torb_text r = torb_show_void(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_sleep */
static void torb_machine_native_127(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  torb_task *r = torb_sleep(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_square_root_f64 */
static void torb_machine_native_128(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  double a0 = torb_machine_double_of(words[base + operands[1]]);
  double r = (double)torb_square_root_f64(a0);
  if (operands[0] >= 0) words[base + operands[0]] = torb_machine_word_of_double(r);
}

/* torb_task_cancel */
static void torb_machine_native_129(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_task *a0 = (torb_task *)(intptr_t)words[base + operands[1]];
  torb_task_cancel(a0);
}

/* torb_task_completed_within */
static void torb_machine_native_130(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_task *a0 = (torb_task *)(intptr_t)words[base + operands[1]];
  int64_t a1 = (int64_t)words[base + operands[2]];
  torb_task *r = torb_task_completed_within(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)(intptr_t)r;
}

/* torb_task_result */
static void torb_machine_native_131(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_task *a0 = (torb_task *)(intptr_t)words[base + operands[1]];
  void *a1 = (void *)torb_machine_address(words, words[base + operands[2]]);
  memset(a1, 0, sizeof(int64_t));
  bool r = torb_task_result(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_test_case */
static void torb_machine_native_132(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  const int64_t *a1 = words + base + operands[2];
  torb_machine_test_case(a0, a1);
}

/* torb_test_group */
static void torb_machine_native_133(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  const int64_t *a1 = words + base + operands[2];
  torb_machine_test_group(a0, a1);
}

/* torb_text_byte_at */
static void torb_machine_native_134(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t *p2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  uint8_t t2 = (uint8_t)*p2;
  bool r = torb_text_byte_at(a0, a1, &t2);
  *p2 = (int64_t)t2;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_char_at */
static void torb_machine_native_135(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t *p2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  torb_char t2 = (torb_char)*p2;
  bool r = torb_text_char_at(a0, a1, &t2);
  *p2 = (int64_t)t2;
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_contains */
static void torb_machine_native_136(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  bool r = torb_text_contains(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_ends_with */
static void torb_machine_native_137(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  bool r = torb_text_ends_with(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_hash */
static void torb_machine_native_138(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  uint64_t r = torb_text_hash(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_index_of */
static void torb_machine_native_139(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_text_index_of(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_is_empty */
static void torb_machine_native_140(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  bool r = torb_text_is_empty(a0);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_last_index_of */
static void torb_machine_native_141(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  int64_t *a2 = (int64_t *)torb_machine_address(words, words[base + operands[3]]);
  bool r = torb_text_last_index_of(a0, a1, a2);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_repeat */
static void torb_machine_native_142(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  torb_text r = torb_text_repeat(a0, a1, torb_machine_location(operands[3]));
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_replace */
static void torb_machine_native_143(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_text a2;
  memcpy(&a2, words + base + operands[3], sizeof a2);
  torb_text r = torb_text_replace(a0, a1, a2);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_show_nested */
static void torb_machine_native_144(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text r = torb_text_show_nested(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_slice */
static void torb_machine_native_145(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  int64_t a1 = (int64_t)words[base + operands[2]];
  int64_t a2 = (int64_t)words[base + operands[3]];
  torb_text r = torb_text_slice(a0, a1, a2, torb_machine_location(operands[4]));
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_split */
static void torb_machine_native_146(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  torb_list r = torb_text_split(a0, a1);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_starts_with */
static void torb_machine_native_147(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text a1;
  memcpy(&a1, words + base + operands[2], sizeof a1);
  bool r = torb_text_starts_with(a0, a1);
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_text_to_lower_case */
static void torb_machine_native_148(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text r = torb_text_to_lower_case(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_to_upper_case */
static void torb_machine_native_149(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text r = torb_text_to_upper_case(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_text_trim */
static void torb_machine_native_150(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  torb_text a0;
  memcpy(&a0, words + base + operands[1], sizeof a0);
  torb_text r = torb_text_trim(a0);
  if (operands[0] >= 0) memcpy(words + base + operands[0], &r, sizeof r);
}

/* torb_workers_blocking */
static void torb_machine_native_151(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_workers_blocking();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

/* torb_workers_count */
static void torb_machine_native_152(int64_t *words, int64_t base, const int64_t *operands) {
  (void)words;
  (void)base;
  (void)operands;
  int64_t r = torb_workers_count();
  if (operands[0] >= 0) words[base + operands[0]] = (int64_t)r;
}

const torb_machine_native torb_machine_natives[] = {
  torb_machine_native_0,
  torb_machine_native_1,
  torb_machine_native_2,
  torb_machine_native_3,
  torb_machine_native_4,
  torb_machine_native_5,
  torb_machine_native_6,
  torb_machine_native_7,
  torb_machine_native_8,
  torb_machine_native_9,
  torb_machine_native_10,
  torb_machine_native_11,
  torb_machine_native_12,
  torb_machine_native_13,
  torb_machine_native_14,
  torb_machine_native_15,
  torb_machine_native_16,
  torb_machine_native_17,
  torb_machine_native_18,
  torb_machine_native_19,
  torb_machine_native_20,
  torb_machine_native_21,
  torb_machine_native_22,
  torb_machine_native_23,
  torb_machine_native_24,
  torb_machine_native_25,
  torb_machine_native_26,
  torb_machine_native_27,
  torb_machine_native_28,
  torb_machine_native_29,
  torb_machine_native_30,
  torb_machine_native_31,
  torb_machine_native_32,
  torb_machine_native_33,
  torb_machine_native_34,
  torb_machine_native_35,
  torb_machine_native_36,
  torb_machine_native_37,
  torb_machine_native_38,
  torb_machine_native_39,
  torb_machine_native_40,
  torb_machine_native_41,
  torb_machine_native_42,
  torb_machine_native_43,
  torb_machine_native_44,
  torb_machine_native_45,
  torb_machine_native_46,
  torb_machine_native_47,
  torb_machine_native_48,
  torb_machine_native_49,
  torb_machine_native_50,
  torb_machine_native_51,
  torb_machine_native_52,
  torb_machine_native_53,
  torb_machine_native_54,
  torb_machine_native_55,
  torb_machine_native_56,
  torb_machine_native_57,
  torb_machine_native_58,
  torb_machine_native_59,
  torb_machine_native_60,
  torb_machine_native_61,
  torb_machine_native_62,
  torb_machine_native_63,
  torb_machine_native_64,
  torb_machine_native_65,
  torb_machine_native_66,
  torb_machine_native_67,
  torb_machine_native_68,
  torb_machine_native_69,
  torb_machine_native_70,
  torb_machine_native_71,
  torb_machine_native_72,
  torb_machine_native_73,
  torb_machine_native_74,
  torb_machine_native_75,
  torb_machine_native_76,
  torb_machine_native_77,
  torb_machine_native_78,
  torb_machine_native_79,
  torb_machine_native_80,
  torb_machine_native_81,
  torb_machine_native_82,
  torb_machine_native_83,
  torb_machine_native_84,
  torb_machine_native_85,
  torb_machine_native_86,
  torb_machine_native_87,
  torb_machine_native_88,
  torb_machine_native_89,
  torb_machine_native_90,
  torb_machine_native_91,
  torb_machine_native_92,
  torb_machine_native_93,
  torb_machine_native_94,
  torb_machine_native_95,
  torb_machine_native_96,
  torb_machine_native_97,
  torb_machine_native_98,
  torb_machine_native_99,
  torb_machine_native_100,
  torb_machine_native_101,
  torb_machine_native_102,
  torb_machine_native_103,
  torb_machine_native_104,
  torb_machine_native_105,
  torb_machine_native_106,
  torb_machine_native_107,
  torb_machine_native_108,
  torb_machine_native_109,
  torb_machine_native_110,
  torb_machine_native_111,
  torb_machine_native_112,
  torb_machine_native_113,
  torb_machine_native_114,
  torb_machine_native_115,
  torb_machine_native_116,
  torb_machine_native_117,
  torb_machine_native_118,
  torb_machine_native_119,
  torb_machine_native_120,
  torb_machine_native_121,
  torb_machine_native_122,
  torb_machine_native_123,
  torb_machine_native_124,
  torb_machine_native_125,
  torb_machine_native_126,
  torb_machine_native_127,
  torb_machine_native_128,
  torb_machine_native_129,
  torb_machine_native_130,
  torb_machine_native_131,
  torb_machine_native_132,
  torb_machine_native_133,
  torb_machine_native_134,
  torb_machine_native_135,
  torb_machine_native_136,
  torb_machine_native_137,
  torb_machine_native_138,
  torb_machine_native_139,
  torb_machine_native_140,
  torb_machine_native_141,
  torb_machine_native_142,
  torb_machine_native_143,
  torb_machine_native_144,
  torb_machine_native_145,
  torb_machine_native_146,
  torb_machine_native_147,
  torb_machine_native_148,
  torb_machine_native_149,
  torb_machine_native_150,
  torb_machine_native_151,
  torb_machine_native_152,
};

const size_t torb_machine_native_count = 153;
