/*
 * environment_test.c - `Environment.get` for a present and an absent variable.
 *
 * The present one is set through `torb_platform_set_environment_variable` (platform.c), not through the C library
 * directly, so the test stays behind the same `#ifdef _WIN32` boundary as everything else that touches the OS.
 */

#include "harness.h"

TORB_TEST(a_variable_that_is_set_is_found) {
  torb_text name = torb_text_from_cstring("TORB_RUNTIME_TEST_VARIABLE");
  torb_text out = torb_text_empty();
  TORB_CHECK(torb_platform_set_environment_variable("TORB_RUNTIME_TEST_VARIABLE", "hello"));
  TORB_CHECK(torb_environment_get(name, &out));
  TORB_CHECK_TEXT(out, "hello");
  torb_text_release(out);
  torb_text_release(name);
}

TORB_TEST(a_variable_that_is_not_set_is_absent) {
  torb_text name = torb_text_from_cstring("TORB_RUNTIME_TEST_VARIABLE_THAT_IS_NEVER_SET");
  torb_text out = torb_text_empty();
  TORB_CHECK(!torb_environment_get(name, &out));
  torb_text_release(name);
}

void torb_register_environment_tests(void) {
  TORB_ADD(a_variable_that_is_set_is_found);
  TORB_ADD(a_variable_that_is_not_set_is_absent);
}
