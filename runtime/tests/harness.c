/*
 * harness.c - the runner: registration, the report, the leak check, the panic hook that longjmps.
 */

#include "harness.h"

#include <stdlib.h>

#define TORB_MAXIMUM_TESTS 256

typedef struct torb_test {
  const char *name;
  torb_test_function body;
} torb_test;

static torb_test torb_tests[TORB_MAXIMUM_TESTS];
static size_t torb_test_count = 0u;
static size_t torb_failures = 0u;
static bool torb_current_failed = false;
static bool torb_current_ignores_leaks = false;

jmp_buf torb_panic_landing;
char torb_panic_message[2048];
bool torb_panic_happened = false;

void torb_register_test(const char *name, torb_test_function body) {
  if (torb_test_count == TORB_MAXIMUM_TESTS) {
    fprintf(stderr, "the harness holds at most %d tests\n", TORB_MAXIMUM_TESTS);
    exit(2);
  }
  torb_tests[torb_test_count].name = name;
  torb_tests[torb_test_count].body = body;
  torb_test_count += 1u;
}

void torb_test_failed(const char *file, int line, const char *message) {
  torb_current_failed = true;
  printf("  FAILED %s:%d: %s\n", file, line, message);
}

void torb_ignore_leaks(void) {
  torb_current_ignores_leaks = true;
}

static void torb_test_panic_hook(const char *message) {
  snprintf(torb_panic_message, sizeof torb_panic_message, "%s", message);
  torb_panic_happened = true;
  longjmp(torb_panic_landing, 1);
}

void torb_install_panic_hook(void) {
  torb_set_panic_hook(torb_test_panic_hook);
}

void torb_remove_panic_hook(void) {
  torb_set_panic_hook(NULL);
}

int main(void) {
  size_t index;
  torb_register_memory_tests();
  torb_register_number_tests();
  torb_register_text_tests();
  torb_register_list_tests();
  torb_register_map_tests();
  torb_register_file_tests();
  torb_register_clock_tests();
  torb_register_environment_tests();
  torb_register_process_tests();
  torb_register_platform_tests();
  torb_register_console_tests();
  torb_register_task_tests();
  torb_register_pool_tests();
  torb_register_sandbox_tests();
  torb_register_io_tests();

  for (index = 0u; index < torb_test_count; index += 1u) {
    size_t before = torb_live_block_count();
    size_t after;
    torb_current_failed = false;
    torb_current_ignores_leaks = false;
    torb_tests[index].body();
    after = torb_live_block_count();
    if (!torb_current_ignores_leaks && after != before) {
      printf("  FAILED %s: %lu blocks leaked\n", torb_tests[index].name,
             (unsigned long)(after - before));
      torb_current_failed = true;
    }
    if (torb_current_failed) {
      printf("fail %s\n", torb_tests[index].name);
      torb_failures += 1u;
    }
  }

  printf("%lu tests, %lu failed\n", (unsigned long)torb_test_count, (unsigned long)torb_failures);
  return torb_failures == 0u ? 0 : 1;
}
