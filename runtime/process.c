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
#  include <fcntl.h> /* _O_BINARY */
#  include <io.h>    /* _setmode, _fileno */
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
 *
 * **`\n` is `\n` everywhere.** On Windows the C runtime opens `stdout` and `stderr` in *text* mode, which turns every
 * `\n` a program writes into `\r\n` on the way into a pipe or a file - so the same program would produce different
 * bytes on two platforms, and the conformance suite would be comparing the platform rather than the program. Both
 * streams go into binary mode here instead. The console path of `console.c` is untouched by this: `WriteConsoleW`
 * takes UTF-16 straight to the console handle and never sees a stream mode at all, and the console host's own
 * processed-output mode is what turns a `\n` into a new line there.
 */
void torb_process_start(int argument_count, char **argument_values) {
  const char *given = getenv("TORB_REPORT_LEAKS");
#if defined(_WIN32)
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  torb_argument_count = argument_count;
  torb_argument_values = argument_values;
  torb_reports_leaks = given != NULL && given[0] == '1' && given[1] == '\0';
  /* Before anything of the program runs and before any thread starts: `TORB_MEMORY_LIMIT` or the dev default */
  torb_memory_limit_start();
  torb_set_stack_limit();
}

/** What `torb_process_exit` runs before the leak report: the release of the entry cells, or nothing. */
static void (*torb_exit_release)(void) = NULL;

void torb_process_on_exit(void (*release)(void)) {
  torb_exit_release = release;
}

void torb_process_finish(void) {
  if (torb_reports_leaks) {
    torb_report_leaks();
  }
}

/**
 * `Process.arguments()`: a fresh `List<String>`, without the program's own name.
 *
 * The platform layer is asked first, because on Windows the `argv` of `main` is **not** UTF-8 - the C runtime builds it
 * from the wide command line through the code page of the machine, which loses every character that code page has no
 * byte for. Where a platform has no source of its own (POSIX, where `argv` is bytes and a UTF-8 `String` is bytes),
 * `argv` is what there is.
 */
torb_list torb_process_arguments(void) {
  torb_list arguments = torb_list_new(&torb_element_text);
  int index;
  if (torb_platform_arguments(&arguments)) {
    return arguments;
  }
  for (index = 1; index < torb_argument_count; index += 1) {
    torb_text argument = torb_text_from_cstring(torb_argument_values[index]);
    torb_list_add(&arguments, &argument);
  }
  return arguments;
}

const char *torb_process_option(const char *name) {
  int index;
  for (index = 1; index + 1 < torb_argument_count; index += 1) {
    if (strcmp(torb_argument_values[index], name) == 0) {
      return torb_argument_values[index + 1];
    }
  }
  return NULL;
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
 * `Process.runBlocking(program, arguments)`: the arguments out of the list, the platform layer, and the two output
 * streams as two texts - standard error in `*failure`, which only says why nothing ran where the result is -1. `input`
 * `NULL` hands the child this program's own standard input, and otherwise its bytes are all the child reads.
 *
 * The list holds `torb_text`, which is not NUL terminated - so every argument is copied into a C string for the call
 * and freed right after. Nothing here interprets an argument; the quoting the command line needs is the platform
 * layer's, and it is the only thing that touches them.
 */
static int64_t torb_process_collect(
  torb_text command,
  torb_list arguments,
  const torb_text *input,
  torb_text *output,
  torb_text *failure
) {
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
  uint8_t *errors = NULL;
  size_t errorsLength = 0u;
  size_t errorsCapacity = 0u;
  bool ran;
  /* The bytes of the input, where there is one: an empty text may have no storage, and is no bytes at all */
  const uint8_t *fed = input == NULL ? NULL
                       : input->length == 0u ? (const uint8_t *)""
                                             : (const uint8_t *)(input->storage->data + input->offset);
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
    fed,
    input == NULL ? 0u : (size_t)input->length,
    &code,
    &bytes,
    &length,
    &bytesCapacity,
    &errors,
    &errorsLength,
    &errorsCapacity,
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
    torb_text written = torb_text_empty();
    const bool decoded = torb_text_try_from_bytes(bytes, length, output, &bad);
    const bool errorsDecoded = decoded && torb_text_try_from_bytes(errors, errorsLength, &written, &bad);
    torb_raw_free(bytes, bytesCapacity);
    torb_raw_free(errors, errorsCapacity);
    if (!decoded) {
      *failure = torb_text_from_cstring("the output of the program is not valid UTF-8");
      return -1;
    }
    if (!errorsDecoded) {
      torb_text_release(*output);
      *output = torb_text_empty();
      *failure = torb_text_from_cstring("the standard error of the program is not valid UTF-8");
      return -1;
    }
    *failure = written;
  }
  return code;
}

int64_t torb_process_run(torb_text command, torb_list arguments, torb_text *output, torb_text *failure) {
  return torb_process_collect(command, arguments, NULL, output, failure);
}

/** `Process.run(program, arguments, input:)`: the same, with `input` as all the child reads. */
int64_t torb_process_run_feeding(
  torb_text command,
  torb_list arguments,
  torb_text input,
  torb_text *output,
  torb_text *failure
) {
  return torb_process_collect(command, arguments, &input, output, failure);
}

/**
 * `Process.runPassingThrough(program, arguments)`: the same arguments, the same platform layer, and no pipe - the
 * child is handed this program's own three streams.
 *
 * Everything this program has buffered is written out first. Two processes share one console from the moment the
 * child starts, so a line this one produced before the call has to be on the console before then, or it appears
 * after output the child wrote later.
 */
int64_t torb_process_run_inheriting(torb_text command, torb_list arguments, torb_text *failure) {
  const int64_t count = torb_list_length(arguments);
  int64_t code = -1;
  size_t index;
  size_t commandCapacity = 0u;
  char *name = torb_argument_bytes(command, &commandCapacity);
  char **given = count == 0 ? NULL : (char **)torb_raw_allocate((size_t)count * sizeof(char *));
  size_t *capacities = count == 0 ? NULL : (size_t *)torb_raw_allocate((size_t)count * sizeof(size_t));
  const char *message = NULL;
  bool ran;
  for (index = 0u; index < (size_t)count; index++) {
    torb_text argument = { NULL, 0u, 0u };
    (void)torb_list_get(arguments, (int64_t)index, &argument);
    capacities[index] = 0u;
    given[index] = torb_argument_bytes(argument, &capacities[index]);
    torb_text_release(argument);
  }
  fflush(NULL);
  ran = torb_platform_run_inheriting(name, (const char **)given, (size_t)count, &code, &message);
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
  return code;
}

void torb_process_exit(int64_t code) {
  /* Inside a sandboxed script the script ends, never the host that runs it (docs/design/SCRIPTS.md section 4) */
  if (torb_sandbox_active != 0) {
    torb_sandbox_exit(code);
  }
  torb_scheduler_exit(code);
  if (torb_exit_release != NULL) {
    void (*release)(void) = torb_exit_release;
    torb_exit_release = NULL;
    release();
  }
  torb_process_finish();
  fflush(stdout);
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY((int)(code & 0xFF));
}

/**
 * `Process.executablePath()`. The platform layer answers, because every operating system keeps this somewhere else
 * (`GetModuleFileNameW`, `/proc/self/exe`) and some keep it nowhere a program can read.
 */
bool torb_process_executable_path(torb_text *out) {
  char *value = NULL;
  size_t length = 0u;
  if (!torb_platform_executable_path(&value, &length)) {
    return false;
  }
  *out = torb_text_from_cstring(value);
  torb_raw_free(value, length + 1u);
  return true;
}
