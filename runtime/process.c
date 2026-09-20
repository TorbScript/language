/*
 * process.c - the program's arguments, `Process.exit`, and running a child process to its end.
 *
 * The generated `main` calls `torb_process_start` before anything else; `Process.arguments()` builds a fresh
 * `List<String>` from what it saw. The program's own name is not in it.
 */

#include "torb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <process.h> /* _exit */
#  define TORB_EXIT_IMMEDIATELY(code) _exit(code)
#elif defined(__unix__) || defined(__APPLE__)
#  include <unistd.h> /* _exit */
#  define TORB_EXIT_IMMEDIATELY(code) _exit(code)
#else
#  define TORB_EXIT_IMMEDIATELY(code) exit(code)
#endif

static int torb_argument_count = 0;
static char **torb_argument_values = NULL;
static bool torb_reports_leaks = false;

/**
 * `TORB_REPORT_LEAKS=1` makes the program write its live block count to stderr when it ends, whichever way it ends.
 * That is the leak gate of the conformance suite: a program that frees what it allocated reports zero, and the
 * environment decides rather than the build, so one binary answers both questions.
 *
 * A panic is not one of the two ends: it aborts without running anything (decided gap 9), so what it leaves behind is
 * not a leak - it is a program that is over.
 */
void torb_process_start(int argument_count, char **argument_values) {
  const char *given = getenv("TORB_REPORT_LEAKS");
  torb_argument_count = argument_count;
  torb_argument_values = argument_values;
  torb_reports_leaks = given != NULL && given[0] == '1' && given[1] == '\0';
}

void torb_process_finish(void) {
  if (torb_reports_leaks) {
    torb_report_leaks();
  }
}

torb_list torb_process_arguments(void) {
  torb_list arguments = torb_list_new(&torb_element_text);
  int index;
  for (index = 1; index < torb_argument_count; index += 1) {
    torb_text argument = torb_text_from_cstring(torb_argument_values[index]);
    torb_list_add(&arguments, &argument);
  }
  return arguments;
}

/** A NUL-terminated copy of a text. Owned; free with `torb_raw_free(buffer, *capacity)`. */
static char *torb_argument_bytes(torb_text text, size_t *capacity) {
  *capacity = (size_t)text.length + 1u;
  {
    char *buffer = (char *)torb_raw_allocate(*capacity);
    if (text.length > 0u) {
      memcpy(buffer, text.storage->data + text.offset, (size_t)text.length);
    }
    buffer[text.length] = 0;
    return buffer;
  }
}

/**
 * `Process.run(command, arguments)`: the arguments out of the list, the platform layer, and the output as one text.
 *
 * The list holds `torb_text`, which is not NUL terminated - so every argument is copied into a C string for the call
 * and freed right after. Nothing here interprets an argument; the quoting the command line needs is the platform
 * layer's, and it is the only thing that touches them.
 */
int64_t torb_process_run(torb_text command, torb_list arguments, torb_text *output, torb_text *failure) {
  const int64_t count = torb_list_length(arguments);
  int64_t code = -1;
  size_t index;
  size_t commandCapacity = 0u;
  char *name = torb_argument_bytes(command, &commandCapacity);
  char **given = count == 0 ? NULL : (char **)torb_raw_allocate((size_t)count * sizeof(char *));
  size_t *capacities = count == 0 ? NULL : (size_t *)torb_raw_allocate((size_t)count * sizeof(size_t));
  const char *message = NULL;
  uint8_t *bytes = NULL;
  size_t length = 0u;
  size_t bytesCapacity = 0u;
  bool ran;
  for (index = 0u; index < (size_t)count; index++) {
    torb_text argument = { NULL, 0u, 0u };
    (void)torb_list_get(arguments, (int64_t)index, &argument);
    capacities[index] = 0u;
    given[index] = torb_argument_bytes(argument, &capacities[index]);
    /* `get` hands the caller a count of the element (BACKEND 2: a read of a counted field retains), and the C string
       is a copy of the bytes - so this text is done with here. */
    torb_text_release(argument);
  }
  ran = torb_platform_run_process(
    name,
    (const char **)given,
    (size_t)count,
    &code,
    &bytes,
    &length,
    &bytesCapacity,
    &message
  );
  for (index = 0u; index < (size_t)count; index++) {
    torb_raw_free(given[index], capacities[index]);
  }
  if (count != 0) {
    torb_raw_free(given, (size_t)count * sizeof(char *));
    torb_raw_free(capacities, (size_t)count * sizeof(size_t));
  }
  torb_raw_free(name, commandCapacity);
  if (!ran) {
    *failure = torb_text_from_cstring(message == NULL ? "the program could not be started" : message);
    return -1;
  }
  /* What a program writes is not always UTF-8, and a `String` always is: what is not decodable is an `IoError` */
  {
    size_t bad = 0u;
    const bool decoded = torb_text_try_from_bytes(bytes, length, output, &bad);
    torb_raw_free(bytes, bytesCapacity);
    if (!decoded) {
      *failure = torb_text_from_cstring("the output of the program is not valid UTF-8");
      return -1;
    }
  }
  return code;
}

void torb_process_exit(int64_t code) {
  torb_process_finish();
  fflush(stdout);
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY((int)(code & 0xFF));
}
