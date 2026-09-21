/*
 * test.c - `test` and `group` of `std/test`.
 *
 *     group "Vector2" {
 *       test "adds component-wise" { ... }
 *     }
 *
 * writes one line per test to standard output, `  ok      Vector2 > adds component-wise` or `  FAILED  ` plus the
 * message and the site, which is the format the interpreter of stage 0 prints to the byte: `torb test` compares the two
 * back ends by exactly these lines.
 *
 * Both are functions of the runtime, because the runtime can call a closure: a closure value's `code` points at a thunk
 * with an erased environment, so a cast and a call are all that is needed. What is not ordinary is the **recovery
 * point** a test needs - a body that panics has to be reported and the next test has to run - and that is
 * `torb_begin_recovery` in `panic.c`, which makes a panic land here instead of leaving the process.
 *
 * **A recovered panic runs nothing on the way out**, exactly as an ordinary panic runs nothing, so a run with a failed
 * test leaks what the aborted frames held and the leak gate does not apply to one.
 */

#include "torb.h"

#include <stdio.h>
#include <string.h>

/** How deeply groups may nest. A suite that needs more than this is not organized, it is nested. */
#define TORB_TEST_GROUP_DEPTH 32
#define TORB_TEST_NAME_SIZE 1024

/** The names of the groups that are open, outermost first: what goes in front of a test's own name. */
static torb_text torb_test_groups[TORB_TEST_GROUP_DEPTH];
static uint32_t torb_test_depth = 0;

/** Appends the bytes of a text to `buffer`, which stays NUL terminated whatever it does not fit. */
static void torb_test_append(char *buffer, size_t size, size_t *length, const char *bytes, size_t count) {
  size_t room = size - 1 - *length;
  if (count > room) {
    count = room;
  }
  if (count > 0 && bytes != NULL) {
    memcpy(buffer + *length, bytes, count);
    *length += count;
  }
  buffer[*length] = '\0';
}

static void torb_test_append_text(char *buffer, size_t size, size_t *length, torb_text text) {
  const char *bytes = text.storage == NULL ? NULL : (const char *)text.storage->data + text.offset;
  torb_test_append(buffer, size, length, bytes, (size_t)text.length);
}

/** `Outer > Inner > what the test is called`, which is the name one line of the report names. */
static void torb_test_full_name(char *buffer, size_t size, torb_text name) {
  size_t length = 0;
  uint32_t index;
  buffer[0] = '\0';
  for (index = 0; index < torb_test_depth; index += 1) {
    torb_test_append_text(buffer, size, &length, torb_test_groups[index]);
    torb_test_append(buffer, size, &length, " > ", 3);
  }
  torb_test_append_text(buffer, size, &length, name);
}

/** The message of a failure, with every line after the first indented to the column the first one starts in. */
static void torb_test_print_indented(const char *message) {
  const char *line = message;
  for (;;) {
    const char *end = strchr(line, '\n');
    if (end == NULL) {
      printf("          %s\n", line);
      return;
    }
    printf("          %.*s\n", (int)(end - line), line);
    line = end + 1;
  }
}

void torb_test_case(torb_text name, torb_closure body) {
  char full[TORB_TEST_NAME_SIZE];
  torb_recovery point;
  torb_recovery *previous;
  torb_test_full_name(full, sizeof full, name);
  previous = torb_begin_recovery(&point);
  if (setjmp(point.destination) == 0) {
    ((void (*)(torb_environment *))body.code)(body.environment);
    torb_end_recovery(previous);
    printf("  ok      %s\n", full);
    return;
  }
  torb_end_recovery(previous);
  printf("  FAILED  %s\n", full);
  torb_test_print_indented(point.message);
  if (point.at.path != NULL) {
    printf("          at %s:%u:%u\n", point.at.path, point.at.line, point.at.column);
  }
  fflush(stdout);
}

void torb_test_group(torb_text name, torb_closure body) {
  if (torb_test_depth >= TORB_TEST_GROUP_DEPTH) {
    torb_panic_text("a group inside more than 32 groups", torb_location_unknown);
  }
  torb_test_groups[torb_test_depth] = name;
  torb_test_depth += 1;
  ((void (*)(torb_environment *))body.code)(body.environment);
  torb_test_depth -= 1;
}
