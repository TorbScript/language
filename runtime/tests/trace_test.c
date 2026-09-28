/*
 * trace_test.c - the `?` return trace of the `dev` profile (`torb_return_trace_mark` in torb.h): which entries form the
 * chain a top-level `?` prints, as a native program's macros and the VM's kernel operations leave them.
 *
 * Every test starts from the trace of the thread the suite runs on, rewound to where it stood, so the tests do not see
 * each other's entries.
 */

#include "harness.h"

static torb_location torb_test_site(uint32_t line) {
  return TORB_LOCATION("app/src/main.trb", line, 5);
}

/** A failure handed on through three `?`s: every entry is in the chain, the innermost first. */
TORB_TEST(test_a_chain_of_three) {
  torb_return_trace *trace = torb_return_trace_current();
  int64_t start = torb_return_trace_mark();
  int64_t outer = torb_return_trace_mark();
  int64_t middle = torb_return_trace_mark();
  int64_t inner = torb_return_trace_mark();
  torb_return_trace_record(inner, torb_test_site(10));
  torb_return_trace_record(middle, torb_test_site(20));
  torb_return_trace_record(outer, torb_test_site(30));
  TORB_CHECK(trace->next == start + 3);
  TORB_CHECK(torb_return_trace_chain_start(trace) == start);
  torb_return_trace_rewind(start);
}

/** A `?` whose operand succeeded drops what the operand recorded. */
TORB_TEST(test_a_success_rewinds) {
  torb_return_trace *trace = torb_return_trace_current();
  int64_t start = torb_return_trace_mark();
  int64_t mark = torb_return_trace_mark();
  torb_return_trace_record(torb_return_trace_mark(), torb_test_site(10));
  torb_return_trace_rewind(mark);
  TORB_CHECK(trace->next == start);
  torb_return_trace_rewind(start);
}

/**
 * A failure a `match` handled was recorded before the `?` of the next failure began, so it is no part of that chain:
 * the entry before the chain's innermost one was recorded before that one's mark.
 */
TORB_TEST(test_a_handled_failure_is_not_in_the_next_chain) {
  torb_return_trace *trace = torb_return_trace_current();
  int64_t start = torb_return_trace_mark();
  torb_return_trace_record(torb_return_trace_mark(), torb_test_site(10));
  int64_t outer = torb_return_trace_mark();
  int64_t inner = torb_return_trace_mark();
  torb_return_trace_record(inner, torb_test_site(20));
  torb_return_trace_record(outer, torb_test_site(30));
  TORB_CHECK(torb_return_trace_chain_start(trace) == start + 1);
  torb_return_trace_rewind(start);
}

/** A mark never moves the trace forward: one taken on another thread says nothing about this one. */
TORB_TEST(test_a_rewind_never_goes_forward) {
  torb_return_trace *trace = torb_return_trace_current();
  int64_t start = torb_return_trace_mark();
  torb_return_trace_rewind(start + 5);
  TORB_CHECK(trace->next == start);
}

/** The ring keeps the last entries: a chain longer than it starts at the oldest one it still holds. */
TORB_TEST(test_a_chain_longer_than_the_ring) {
  torb_return_trace *trace = torb_return_trace_current();
  int64_t start = torb_return_trace_mark();
  int64_t count = (int64_t)TORB_RETURN_TRACE_ENTRIES + 10;
  for (int64_t index = 0; index < count; index++) {
    torb_return_trace_record(start, torb_test_site((uint32_t)index));
  }
  TORB_CHECK(torb_return_trace_chain_start(trace) == start + count - (int64_t)TORB_RETURN_TRACE_ENTRIES);
  torb_return_trace_rewind(start);
}

void torb_register_trace_tests(void) {
  TORB_ADD(test_a_chain_of_three);
  TORB_ADD(test_a_success_rewinds);
  TORB_ADD(test_a_handled_failure_is_not_in_the_next_chain);
  TORB_ADD(test_a_rewind_never_goes_forward);
  TORB_ADD(test_a_chain_longer_than_the_ring);
}
