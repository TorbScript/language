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

void torb_process_exit(int64_t code) {
  torb_process_finish();
  fflush(stdout);
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY((int)(code & 0xFF));
}
