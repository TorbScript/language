/*
 * process_test.c - `Process.run`: a child process run to its end.
 *
 * The program it runs is the one every machine has and whose output is fixed: the shell's `echo` on POSIX and
 * `cmd /c echo` on Windows. What is asserted is the three things a caller can observe - the exit code, the output, and
 * that a program which cannot be started at all is a failure and not an exit code.
 *
 * `Process.arguments` and `Process.exit` are not testable in process: the first is what `main` handed over and the
 * second ends the process. The conformance suite covers both through `tests/conformance/`.
 */

#include "harness.h"

static torb_list argument_list(const char *first, const char *second) {
  torb_list arguments = torb_list_new(&torb_element_text);
  if (first != NULL) {
    torb_text one = torb_text_from_cstring(first);
    torb_list_add(&arguments, &one);
  }
  if (second != NULL) {
    torb_text other = torb_text_from_cstring(second);
    torb_list_add(&arguments, &other);
  }
  return arguments;
}

TORB_TEST(a_program_that_ran_answers_its_code_and_its_output) {
#if defined(_WIN32)
  torb_text command = torb_text_from_cstring("cmd");
  torb_list arguments = argument_list("/c", "echo torb");
#else
  torb_text command = torb_text_from_cstring("/bin/sh");
  torb_list arguments = argument_list("-c", "echo torb");
#endif
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

  TORB_CHECK_INTEGER(torb_process_run(command, arguments, &output, &failure), 0);
  {
    torb_text needle = torb_text_from_cstring("torb");
    TORB_CHECK(torb_text_contains(output, needle));
    torb_text_release(needle);
  }

  torb_text_release(output);
  torb_list_release(arguments);
  torb_text_release(command);
  torb_text_release(failure);
}

/** A program that ran and failed is an **exit code** and not a failure of `run`: that is the whole point of it. */
TORB_TEST(a_program_that_failed_is_an_exit_code_and_not_an_error) {
#if defined(_WIN32)
  torb_text command = torb_text_from_cstring("cmd");
  torb_list arguments = argument_list("/c", "exit 3");
#else
  torb_text command = torb_text_from_cstring("/bin/sh");
  torb_list arguments = argument_list("-c", "exit 3");
#endif
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

  TORB_CHECK_INTEGER(torb_process_run(command, arguments, &output, &failure), 3);

  torb_text_release(output);
  torb_list_release(arguments);
  torb_text_release(command);
  torb_text_release(failure);
}

/**
 * A command that is nowhere.
 *
 * On Windows this is a **failure** of `run` (`-1` plus a reason), because `CreateProcess` is what looks the program up
 * and it says so - which is the difference `findCompiler` reads and what the interpreter answers for the same call. On
 * POSIX `popen` starts a **shell**, which exists, and the shell reports the missing program with a code of its own; that
 * half becomes a failure too when 7.3 makes `Process.start` shell free there as well.
 */
TORB_TEST(a_command_that_is_nowhere_cannot_be_started) {
  torb_text command = torb_text_from_cstring("torb-no-such-program-anywhere");
  torb_list arguments = argument_list(NULL, NULL);
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

#if defined(_WIN32)
  TORB_CHECK_INTEGER(torb_process_run(command, arguments, &output, &failure), -1);
  TORB_CHECK(torb_text_byte_length(failure) > 0u);
#else
  TORB_CHECK(torb_process_run(command, arguments, &output, &failure) != 0);
#endif

  torb_text_release(output);
  torb_list_release(arguments);
  torb_text_release(command);
  torb_text_release(failure);
}

/**
 * A command found on **PATH**, with exactly one argument. This is the shape `torb build` looks for a C compiler with
 * (`gcc --version`), and the shape that was broken while this went through `cmd`: the command line `"gcc" "--version"`
 * arrived as one command *named* `gcc" "--version`, so the compiled compiler reported "no C compiler found" while the
 * interpreter found gcc on the same PATH. Two arguments happened to work, which is why the test above never caught it.
 */
TORB_TEST(a_command_on_the_path_with_one_argument_runs) {
#if defined(_WIN32)
  /* `where` is in System32 on every Windows, and it is named without an extension, like `gcc` */
  torb_text command = torb_text_from_cstring("where");
  torb_list arguments = argument_list("cmd", NULL);
#else
  torb_text command = torb_text_from_cstring("uname");
  torb_list arguments = argument_list("-s", NULL);
#endif
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

  TORB_CHECK_INTEGER(torb_process_run(command, arguments, &output, &failure), 0);
  TORB_CHECK(torb_text_byte_length(output) > 0u);

  torb_text_release(output);
  torb_list_release(arguments);
  torb_text_release(command);
  torb_text_release(failure);
}

/** An argument with a space in it survives the command line, which is what the quoting in the platform layer is for. */
TORB_TEST(an_argument_with_a_space_arrives_as_one_argument) {
#if defined(_WIN32)
  torb_text command = torb_text_from_cstring("cmd");
  torb_list arguments = argument_list("/c", "echo one two");
#else
  torb_text command = torb_text_from_cstring("/bin/sh");
  torb_list arguments = argument_list("-c", "echo one two");
#endif
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

  TORB_CHECK_INTEGER(torb_process_run(command, arguments, &output, &failure), 0);
  {
    torb_text needle = torb_text_from_cstring("one two");
    TORB_CHECK(torb_text_contains(output, needle));
    torb_text_release(needle);
  }

  torb_text_release(output);
  torb_list_release(arguments);
  torb_text_release(command);
  torb_text_release(failure);
}

void torb_register_process_tests(void) {
  TORB_ADD(a_program_that_ran_answers_its_code_and_its_output);
  TORB_ADD(a_program_that_failed_is_an_exit_code_and_not_an_error);
  TORB_ADD(a_command_that_is_nowhere_cannot_be_started);
  TORB_ADD(a_command_on_the_path_with_one_argument_runs);
  TORB_ADD(an_argument_with_a_space_arrives_as_one_argument);
}
