/*
 * memory_test.c - reference counting, make-unique, immortal values, and the live-block counter.
 *
 * This is the part TorbScript cannot reach: from the language a count is invisible, so the rules of BACKEND 2.1
 * ("in place if the count is 1, copy first otherwise") are pinned here.
 */

#include "harness.h"

/* A compound literal inside TORB_EXPECT_PANIC would be "clobbered by longjmp", so the locations are statics. */
static const torb_location in_main = { "src/main.trb", 12, 5 };
static const torb_location in_deep = { "src/deep.trb", 3, 1 };

/** A `Boxed` layout with one managed field, in the shape the C emitter writes. */
typedef struct sample {
  torb_header header;
  torb_text name;
  int64_t value;
} sample;

static void sample_drop(void *block) {
  sample *value = (sample *)block;
  torb_text_release(value->name);
}

static void sample_retain_children(void *block) {
  sample *value = (sample *)block;
  torb_retain(value->name.storage);
}

static sample *sample_new(const char *name, int64_t value) {
  sample *result = (sample *)torb_allocate(sizeof(sample), TORB_BLOCK_RECORD);
  result->name = torb_text_from_cstring(name);
  result->value = value;
  return result;
}

TORB_TEST(allocation_starts_at_one_and_frees_at_zero) {
  size_t before = torb_live_block_count();
  sample *value = sample_new("first", 1);
  TORB_CHECK(torb_live_block_count() == before + 2u); /* the record and its name's storage */
  TORB_CHECK(torb_is_unique(value));
  torb_release(value, sample_drop);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

TORB_TEST(retain_and_release_balance) {
  sample *value = sample_new("second", 2);
  torb_retain(value);
  torb_retain(value);
  TORB_CHECK(!torb_is_unique(value));
  TORB_CHECK_INTEGER(value->header.count, 3);
  torb_release(value, sample_drop);
  torb_release(value, sample_drop);
  TORB_CHECK(torb_is_unique(value));
  torb_release(value, sample_drop);
}

TORB_TEST(make_unique_keeps_the_block_when_it_is_unique) {
  sample *value = sample_new("third", 3);
  sample *after = (sample *)torb_make_unique(value, sizeof(sample), sample_retain_children, sample_drop);
  TORB_CHECK(after == value);
  TORB_CHECK_INTEGER(after->header.count, 1);
  torb_release(after, sample_drop);
}

TORB_TEST(make_unique_copies_exactly_once_when_it_is_shared) {
  sample *value = sample_new("fourth", 4);
  sample *copy;
  size_t blocks;
  torb_retain(value);
  blocks = torb_live_block_count();
  copy = (sample *)torb_make_unique(value, sizeof(sample), sample_retain_children, sample_drop);
  TORB_CHECK(copy != value);
  TORB_CHECK_INTEGER(copy->header.count, 1);
  TORB_CHECK_INTEGER(value->header.count, 1);
  /* One new record; the name's storage is shared, not copied. */
  TORB_CHECK_INTEGER(torb_live_block_count(), blocks + 1u);
  TORB_CHECK_INTEGER(copy->name.storage->header.count, 2);
  TORB_CHECK(copy->name.storage == value->name.storage);
  copy->value = 40;
  TORB_CHECK_INTEGER(value->value, 4);
  torb_release(copy, sample_drop);
  torb_release(value, sample_drop);
}

TORB_TEST(make_unique_of_a_static_value_always_copies) {
  static struct {
    torb_header header;
    int64_t value;
  } immortal = { TORB_IMMORTAL_HEADER(TORB_BLOCK_RECORD), 7 };
  void *copy = torb_make_unique(&immortal, sizeof immortal, NULL, NULL);
  TORB_CHECK(copy != (void *)&immortal);
  TORB_CHECK(torb_is_unique(copy));
  torb_release(copy, NULL);
  /* The static block is untouched by retain and release. */
  torb_retain(&immortal);
  torb_release(&immortal, NULL);
  TORB_CHECK_INTEGER(immortal.header.count, (long long)TORB_IMMORTAL_COUNT);
}

TORB_TEST(a_null_block_is_a_unique_no_op) {
  torb_retain(NULL);
  torb_release(NULL, NULL);
  TORB_CHECK(torb_is_unique(NULL));
  TORB_CHECK(torb_make_unique(NULL, 16u, NULL, NULL) == NULL);
}

TORB_TEST(raw_buffers_are_counted_too) {
  size_t before = torb_live_block_count();
  void *buffer = torb_raw_allocate(64u);
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 1u);
  torb_raw_free(buffer, 64u);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

TORB_TEST(making_a_block_immortal_takes_it_out_of_the_live_count) {
  size_t before = torb_live_block_count();
  void *block = torb_allocate(32u, TORB_BLOCK_RECORD);
  torb_make_immortal(block);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
  /* Deliberately not freed: an immortal block never is. */
}

/** A closure environment in the shape the C emitter writes: the header, the drop function, then the captures. */
typedef struct sample_environment {
  torb_header header;
  torb_drop_function drop;
  torb_text captured;
} sample_environment;

static void sample_environment_drop(void *block) {
  sample_environment *value = (sample_environment *)block;
  torb_text_release(value->captured);
}

/*
 * Releasing a closure goes through its environment, because a closure value has the type of every closure of its shape:
 * which captures are inside one, and therefore what a release has to release, is only known to the closure that built it
 * (BACKEND 5.8). So the drop function travels in the block.
 */
TORB_TEST(releasing_an_environment_runs_the_drop_function_it_carries) {
  size_t before = torb_live_block_count();
  sample_environment *environment = (sample_environment *)torb_allocate(sizeof(sample_environment), TORB_BLOCK_ENVIRONMENT);
  uint8_t *data = NULL;
  environment->drop = sample_environment_drop;
  environment->captured = torb_text_allocate(2u, &data);
  data[0] = 'a';
  data[1] = 'b';
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 2u);
  torb_environment_release((torb_environment *)environment);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

/** A closure without captures has no environment at all, and releasing that is nothing. */
TORB_TEST(releasing_the_environment_of_a_closure_without_captures_is_a_no_op) {
  size_t before = torb_live_block_count();
  torb_environment_release(NULL);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

/** A closure that is copied shares its environment, so the captures die with the last copy and not with the first. */
TORB_TEST(a_copied_closure_shares_its_environment) {
  size_t before = torb_live_block_count();
  sample_environment *environment = (sample_environment *)torb_allocate(sizeof(sample_environment), TORB_BLOCK_ENVIRONMENT);
  uint8_t *data = NULL;
  environment->drop = sample_environment_drop;
  environment->captured = torb_text_allocate(1u, &data);
  data[0] = 'x';
  torb_retain(environment);
  torb_environment_release((torb_environment *)environment);
  TORB_CHECK_INTEGER(torb_live_block_count(), before + 2u);
  torb_environment_release((torb_environment *)environment);
  TORB_CHECK_INTEGER(torb_live_block_count(), before);
}

TORB_TEST(a_panic_hook_catches_a_panic_and_the_suite_goes_on) {
  TORB_EXPECT_PANIC(torb_panic_text("something broke", in_main));
  TORB_CHECK_PANIC_CONTAINS("panic: something broke");
  TORB_CHECK_PANIC_CONTAINS("at src/main.trb:12:5");
}

TORB_TEST(the_frame_counter_panics_at_the_limit) {
  uint32_t saved = torb_frame_limit;
  uint32_t index;
  torb_frame_limit = 8u;
  for (index = 0u; index < 8u; index += 1u) {
    torb_enter_frame(torb_location_unknown);
  }
  TORB_EXPECT_PANIC(torb_enter_frame(in_deep));
  TORB_CHECK_PANIC_CONTAINS("stack overflow");
  torb_frame_limit = saved;
}

void torb_register_memory_tests(void) {
  TORB_ADD(allocation_starts_at_one_and_frees_at_zero);
  TORB_ADD(retain_and_release_balance);
  TORB_ADD(make_unique_keeps_the_block_when_it_is_unique);
  TORB_ADD(make_unique_copies_exactly_once_when_it_is_shared);
  TORB_ADD(make_unique_of_a_static_value_always_copies);
  TORB_ADD(a_null_block_is_a_unique_no_op);
  TORB_ADD(raw_buffers_are_counted_too);
  TORB_ADD(making_a_block_immortal_takes_it_out_of_the_live_count);
  TORB_ADD(releasing_an_environment_runs_the_drop_function_it_carries);
  TORB_ADD(releasing_the_environment_of_a_closure_without_captures_is_a_no_op);
  TORB_ADD(a_copied_closure_shares_its_environment);
  TORB_ADD(a_panic_hook_catches_a_panic_and_the_suite_goes_on);
  TORB_ADD(the_frame_counter_panics_at_the_limit);
}
