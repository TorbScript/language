/*
 * process.c - the program's arguments and `Process.exit`.
 *
 * The generated `main` calls `torb_process_start` before anything else; `Process.arguments()` builds a fresh
 * `List<String>` from what it saw. The program's own name is not in it.
 */

#include "torb.h"

#include <stdio.h>
#include <stdlib.h>

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

void torb_process_start(int argument_count, char **argument_values) {
  torb_argument_count = argument_count;
  torb_argument_values = argument_values;
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

void torb_process_exit(int64_t code) {
  fflush(stdout);
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY((int)(code & 0xFF));
}
