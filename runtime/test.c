/*
 * test.c - `test` and `group` of `std/test`.
 *
 *     group "Vector2" {
 *       test "adds component-wise" { ... }
 *     }
 *
 * writes one line per test to standard output, `  ok      Vector2 > adds component-wise` or `  FAILED  ` plus the
 * message and the site.
 *
 * Both are functions of the runtime, because the runtime can call a closure: a closure value's `code` points at a thunk
 * with an erased environment, so a cast and a call are all that is needed. What is not ordinary is the **recovery
 * point** a test needs - a body that panics has to be reported and the next test has to run - and that is
 * `torb_begin_recovery` in `panic.c`, which makes a panic land here instead of leaving the process.
 *
 * **A recovered panic runs nothing on the way out**, exactly as an ordinary panic runs nothing, so a run with a failed
 * test leaks what the aborted frames held and the leak gate does not apply to one.
 *
 * `torb test` over a directory is **one binary for every test file**, so the counts of the run and the summary line
 * live here as well: `torb_test_file` writes the name of the file whose tests come next and `torb_test_finish` writes
 * the blank line, `N passed, M failed (K files)`, and the exit code. A test file that fails does not stop the file
 * after it - that is what the recovery point is for - so the counts are what say whether the run was green.
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

/**
 * What the summary line counts. They live here and not in the driver for the same reason the format of one line does:
 * one binary holds every test file of a run, and the two implementations of `torb test` print one summary.
 */
static int64_t torb_test_passed = 0;
static int64_t torb_test_failed = 0;
static int64_t torb_test_files = 0;

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

/**
 * One line of the report, through the same path `print` goes through (`torb_write_line_out` in `console.c`): a name or
 * a message with a non-ASCII character in it reads correctly on a live Windows console, and a pipe keeps exactly the
 * bytes `torb test` compares between the two implementations.
 */
static void torb_test_line(const char *line) {
  torb_write_line_out(line, strlen(line));
}

/** The same, with `prefix` in front of `body`, which is the one shape every line of this report has. */
static void torb_test_line_of(const char *prefix, const char *body, size_t length) {
  char line[TORB_TEST_NAME_SIZE + 64];
  size_t filled = 0u;
  torb_test_append(line, sizeof line, &filled, prefix, strlen(prefix));
  torb_test_append(line, sizeof line, &filled, body, length);
  torb_write_line_out(line, filled);
}

/** The message of a failure, with every line after the first indented to the column the first one starts in. */
static void torb_test_print_indented(const char *message) {
  const char *line = message;
  for (;;) {
    const char *end = strchr(line, '\n');
    if (end == NULL) {
      torb_test_line_of("          ", line, strlen(line));
      return;
    }
    torb_test_line_of("          ", line, (size_t)(end - line));
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
    torb_test_passed += 1;
    torb_test_line_of("  ok      ", full, strlen(full));
    return;
  }
  torb_end_recovery(previous);
  torb_test_failed += 1;
  torb_test_line_of("  FAILED  ", full, strlen(full));
  torb_test_print_indented(point.message);
  if (point.at.path != NULL) {
    char site[TORB_TEST_NAME_SIZE];
    snprintf(site, sizeof site, "          at %s:%u:%u", point.at.path, point.at.line, point.at.column);
    torb_test_line(site);
  }
  fflush(stdout);
}

void torb_test_file(const char *path, size_t length) {
  torb_test_files += 1;
  torb_write_line_out(path, length);
}

int torb_test_finish(void) {
  char summary[128];
  snprintf(summary, sizeof summary, "\n%lld passed, %lld failed (%lld files)", (long long)torb_test_passed,
           (long long)torb_test_failed, (long long)torb_test_files);
  torb_write_line_out(summary, strlen(summary));
  fflush(stdout);
  return torb_test_failed == 0 ? 0 : 1;
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
