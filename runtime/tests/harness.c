/*
 * harness.c - the runner: registration, the report, the leak check, the panic hook that longjmps.
 *
 * Every run works in a directory of its own below the system's temporary one - its temporary directory and its working
 * directory both - so the fixed file names of the tests never meet those of another run: a second checkout's gates, or
 * the same suite started twice at once.
 */

#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "harness.h"

#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <process.h>
#  include <windows.h>
static void torb_test_set_variable(const char *name, const char *value) {
  /* Both copies: the one of the C library getenv reads, and the process's the runtime reads */
  (void)_putenv_s(name, value);
  (void)SetEnvironmentVariableA(name, value);
}
static int torb_test_make_directory(const char *path) {
  return _mkdir(path);
}
static int torb_test_change_directory(const char *path) {
  return _chdir(path);
}
static int torb_test_remove_directory(const char *path) {
  return _rmdir(path);
}
static long torb_test_process_id(void) {
  return (long)_getpid();
}
#else
#  include <sys/stat.h>
#  include <unistd.h>
static void torb_test_set_variable(const char *name, const char *value) {
  (void)setenv(name, value, 1);
}
static int torb_test_make_directory(const char *path) {
  return mkdir(path, 0700);
}
static int torb_test_change_directory(const char *path) {
  return chdir(path);
}
static int torb_test_remove_directory(const char *path) {
  return rmdir(path);
}
static long torb_test_process_id(void) {
  return (long)getpid();
}
#endif

/* The directory of this run, and the working directory to go back to before it is removed. */
static char torb_test_directory[1024];
static char torb_test_parent[1024];

/* The run's own directory below the temporary one, made the temporary and the working directory of the run. */
static void torb_test_isolate(void) {
  const char *names[3] = { "TMPDIR", "TEMP", "TMP" };
  const char *base = NULL;
  size_t index;
  for (index = 0u; index < 3u && base == NULL; index += 1u) {
    base = getenv(names[index]);
  }
  /* Git Bash exports TMPDIR as a path of its own (/tmp), which no Windows function reads: TEMP is the real one there */
#if defined(_WIN32)
  if (getenv("TEMP") != NULL) {
    base = getenv("TEMP");
  }
#endif
  if (base == NULL) {
    return;
  }
  snprintf(torb_test_parent, sizeof torb_test_parent, "%s", base);
  snprintf(torb_test_directory, sizeof torb_test_directory, "%s/torb-runtime-tests-%ld-%ld", base,
           torb_test_process_id(), (long)time(NULL));
  if (torb_test_make_directory(torb_test_directory) != 0 || torb_test_change_directory(torb_test_directory) != 0) {
    torb_test_directory[0] = '\0';
    return;
  }
  for (index = 0u; index < 3u; index += 1u) {
    torb_test_set_variable(names[index], torb_test_directory);
  }
}

/* The run's directory removed where the tests left it empty; a file a failing test left keeps it, to be looked at. */
static void torb_test_leave(void) {
  if (torb_test_directory[0] == '\0') {
    return;
  }
  (void)torb_test_change_directory(torb_test_parent);
  (void)torb_test_remove_directory(torb_test_directory);
}

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
/* The test that runs, for a panic that ends the whole run: the report says where it happened. */
static const char *volatile torb_current_name = NULL;

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

/*
 * The hook of every other moment: a panic nobody expected ends the run as it would without a hook, and says first which
 * test it happened in - the one line that turns a rare crash of the whole suite into something to look at.
 */
static void torb_test_report_panic(const char *message) {
  (void)message;
  fflush(stdout);
  fprintf(stderr, "the panic below ended the run in the test %s\n",
          torb_current_name != NULL ? torb_current_name : "(none, between tests)");
  fflush(stderr);
}

void torb_install_panic_hook(void) {
  torb_set_panic_hook(torb_test_panic_hook);
}

void torb_remove_panic_hook(void) {
  torb_set_panic_hook(torb_test_report_panic);
}

int main(void) {
  size_t index;
  /* `TORB_TEST_TRACE` names every test on standard error before it runs: which one a crash of the whole run was in */
  bool tracing = getenv("TORB_TEST_TRACE") != NULL;
  torb_test_isolate();
  torb_set_panic_hook(torb_test_report_panic);
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
  torb_register_tls_tests();
  torb_register_trace_tests();

  for (index = 0u; index < torb_test_count; index += 1u) {
    size_t before = torb_live_block_count();
    size_t after;
    torb_current_failed = false;
    torb_current_ignores_leaks = false;
    if (tracing) {
      fprintf(stderr, "running %s\n", torb_tests[index].name);
      fflush(stderr);
    }
    torb_current_name = torb_tests[index].name;
    torb_tests[index].body();
    torb_current_name = NULL;
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
  torb_test_leave();
  return torb_failures == 0u ? 0 : 1;
}
