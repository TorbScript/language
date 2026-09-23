/*
 * sandbox_test.c - the second lock of a sandboxed script (sandbox.c, docs/design/SCRIPTS.md section 4): the patterns of
 * the environment, a path read against the base and held against the roots, and the allocation budget. A refusal is a
 * panic here, because no kernel operation has set up the recovery point the VM would catch it with.
 */

#include "harness.h"

#include <string.h>

static char *torb_resolved(const char *path, bool writes, size_t *capacity) {
  torb_text text = torb_text_from_cstring(path);
  char *resolved = torb_sandbox_path(text, writes, capacity);
  torb_text_release(text);
  return resolved;
}

static void torb_open(const char *grant) {
  torb_sandbox_open(grant, strlen(grant));
}

TORB_TEST(without_a_sandbox_every_variable_and_every_path_is_allowed) {
  size_t capacity = 0u;
  char *resolved = torb_resolved("../anywhere", true, &capacity);
  TORB_CHECK(!torb_sandbox_is_open());
  TORB_CHECK(torb_sandbox_allows_variable("PATH"));
  TORB_CHECK(strcmp(resolved, "../anywhere") == 0);
  torb_raw_free(resolved, capacity);
}

TORB_TEST(a_pattern_is_a_name_or_a_prefix_and_a_star) {
  torb_open("base\t/srv/app\nvariable\tAPP_*\nvariable\tHOME");
  TORB_CHECK(torb_sandbox_allows_variable("APP_PORT"));
  TORB_CHECK(torb_sandbox_allows_variable("HOME"));
  TORB_CHECK(!torb_sandbox_allows_variable("HOMEPATH"));
  TORB_CHECK(!torb_sandbox_allows_variable("PATH"));
  torb_sandbox_close();
  TORB_CHECK(!torb_sandbox_is_open());
}

TORB_TEST(a_relative_path_is_read_against_the_base_and_normalized) {
  size_t capacity = 0u;
  char *resolved;
  torb_open("base\t/srv/app\nread\t/srv/app");
  resolved = torb_resolved("config/../settings.txt", false, &capacity);
  TORB_CHECK(strcmp(resolved, "/srv/app/settings.txt") == 0);
  torb_raw_free(resolved, capacity);
  torb_sandbox_close();
}

TORB_TEST(a_path_outside_of_every_root_stops_the_script) {
  size_t capacity = 0u;
  torb_open("base\t/srv/app\nread\t/srv/app");
  /* The text of the path is the frame's, and a panic releases nothing on the way out */
  TORB_IGNORE_LEAKS();
  TORB_EXPECT_PANIC(torb_resolved("../etc/passwd", false, &capacity));
  TORB_CHECK_PANIC_CONTAINS("The script may not read `../etc/passwd`: it is not inside `/srv/app`");
  torb_sandbox_close();
}

TORB_TEST(a_root_to_read_is_no_root_to_write) {
  size_t capacity = 0u;
  torb_open("base\t/srv/app\nread\t/srv/app");
  TORB_IGNORE_LEAKS();
  TORB_EXPECT_PANIC(torb_resolved("out.txt", true, &capacity));
  TORB_CHECK_PANIC_CONTAINS("The script may not write `out.txt`: no directory is granted for writing");
  torb_sandbox_close();
}

TORB_TEST(an_allocation_past_the_limit_stops_the_script_where_a_recovery_point_waits) {
  void *first;
  torb_open("base\t/srv/app\nmemory\t100");
  /* Without the guard of a kernel operation an allocation only counts */
  first = torb_allocate(64, TORB_BLOCK_RECORD);
  torb_sandbox_enter_guard();
  TORB_EXPECT_PANIC(torb_allocate(64, TORB_BLOCK_RECORD));
  TORB_CHECK_PANIC_CONTAINS("The script allocated more than 100 bytes");
  torb_sandbox_leave_guard();
  torb_sandbox_close();
  torb_release(first, NULL);
}

void torb_register_sandbox_tests(void) {
  TORB_ADD(without_a_sandbox_every_variable_and_every_path_is_allowed);
  TORB_ADD(a_pattern_is_a_name_or_a_prefix_and_a_star);
  TORB_ADD(a_relative_path_is_read_against_the_base_and_normalized);
  TORB_ADD(a_path_outside_of_every_root_stops_the_script);
  TORB_ADD(a_root_to_read_is_no_root_to_write);
  TORB_ADD(an_allocation_past_the_limit_stops_the_script_where_a_recovery_point_waits);
}
