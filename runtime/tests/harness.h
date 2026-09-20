/*
 * harness.h - the assert-based harness of the runtime's C tests. One executable, no dependency.
 *
 * Every test file declares its tests with TORB_TEST and registers them at the bottom in a
 * `torb_register_<area>_tests` function, which `harness.c` calls. The runner runs them in that order, reports what
 * failed, and asserts that the live block count is back to zero after every single one - which is the leak check.
 *
 * Panics are tested **in process**: `TORB_EXPECT_PANIC(expression)` installs a panic hook that `longjmp`s back out of
 * the panicking call, so the expected message can be compared and the suite goes on. That is portable (no fork, no
 * CreateProcess, identical on Windows and POSIX), it keeps everything in one binary, and it is the reason
 * `torb_set_panic_hook` exists at all. The cost is that the panicking call leaks whatever it held, so a test that
 * expects a panic after allocating says TORB_IGNORE_LEAKS().
 */

#ifndef TORB_TEST_HARNESS_H
#define TORB_TEST_HARNESS_H

#include "torb.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

typedef void (*torb_test_function)(void);

void torb_register_test(const char *name, torb_test_function body);
void torb_test_failed(const char *file, int line, const char *message);
/** A test that panicked on purpose leaked whatever the panicking call held. This says so on purpose. */
void torb_ignore_leaks(void);

#define TORB_TEST(name) static void name(void)
#define TORB_ADD(name) torb_register_test(#name, name)

void torb_register_memory_tests(void);
void torb_register_number_tests(void);
void torb_register_text_tests(void);
void torb_register_list_tests(void);
void torb_register_map_tests(void);
void torb_register_file_tests(void);
void torb_register_clock_tests(void);
void torb_register_environment_tests(void);
void torb_register_process_tests(void);

#define TORB_CHECK(condition)                                        \
  do {                                                               \
    if (!(condition)) {                                              \
      torb_test_failed(__FILE__, __LINE__, #condition);              \
      return;                                                        \
    }                                                                \
  } while (0)

#define TORB_CHECK_MESSAGE(condition, message)                       \
  do {                                                               \
    if (!(condition)) {                                              \
      torb_test_failed(__FILE__, __LINE__, (message));               \
      return;                                                        \
    }                                                                \
  } while (0)

#define TORB_CHECK_INTEGER(actual, expected)                                            \
  do {                                                                                  \
    long long torb_actual = (long long)(actual);                                        \
    long long torb_expected = (long long)(expected);                                    \
    if (torb_actual != torb_expected) {                                                 \
      char torb_message[256];                                                           \
      snprintf(torb_message, sizeof torb_message, "%s: %lld, expected %lld", #actual,   \
               torb_actual, torb_expected);                                             \
      torb_test_failed(__FILE__, __LINE__, torb_message);                               \
      return;                                                                           \
    }                                                                                   \
  } while (0)

/** Compares a `torb_text` with a C string. The text is borrowed; nothing is released. */
#define TORB_CHECK_TEXT(actual, expected)                                                             \
  do {                                                                                                \
    torb_text torb_value = (actual);                                                                  \
    const char *torb_wanted = (expected);                                                             \
    size_t torb_wanted_length = strlen(torb_wanted);                                                  \
    const char *torb_bytes = torb_value.length == 0u                                                  \
                                 ? ""                                                                 \
                                 : (const char *)(torb_value.storage->data + torb_value.offset);      \
    if ((size_t)torb_value.length != torb_wanted_length                                               \
        || (torb_wanted_length > 0u && memcmp(torb_bytes, torb_wanted, torb_wanted_length) != 0)) {    \
      char torb_message[512];                                                                         \
      snprintf(torb_message, sizeof torb_message, "%s: \"%.*s\", expected \"%s\"", #actual,           \
               (int)torb_value.length, torb_bytes, torb_wanted);                                      \
      torb_test_failed(__FILE__, __LINE__, torb_message);                                             \
      return;                                                                                         \
    }                                                                                                 \
  } while (0)

/* --- panics ------------------------------------------------------------------------------------------------- */

extern jmp_buf torb_panic_landing;
extern char torb_panic_message[2048];
extern bool torb_panic_happened;

void torb_install_panic_hook(void);
void torb_remove_panic_hook(void);

/**
 * Runs `expression` and expects it to panic. Afterwards `torb_panic_message` holds the whole message, so the caller
 * can check what it says with TORB_CHECK_PANIC_CONTAINS.
 */
#define TORB_EXPECT_PANIC(expression)                                                          \
  do {                                                                                         \
    torb_panic_happened = false;                                                               \
    torb_install_panic_hook();                                                                 \
    if (setjmp(torb_panic_landing) == 0) {                                                     \
      expression;                                                                              \
    }                                                                                          \
    torb_remove_panic_hook();                                                                  \
    if (!torb_panic_happened) {                                                                \
      torb_test_failed(__FILE__, __LINE__, "expected a panic from `" #expression "`");         \
      return;                                                                                  \
    }                                                                                          \
  } while (0)

#define TORB_CHECK_PANIC_CONTAINS(part)                                                                   \
  do {                                                                                                    \
    if (strstr(torb_panic_message, (part)) == NULL) {                                                     \
      char torb_message[2304];                                                                            \
      snprintf(torb_message, sizeof torb_message, "the panic said \"%s\", expected \"%s\" in it",          \
               torb_panic_message, (part));                                                               \
      torb_test_failed(__FILE__, __LINE__, torb_message);                                                 \
      return;                                                                                             \
    }                                                                                                     \
  } while (0)

#define TORB_IGNORE_LEAKS() torb_ignore_leaks()

#endif /* TORB_TEST_HARNESS_H */
