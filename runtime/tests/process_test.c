/*
 * process_test.c - `Process.run`: a child process run to its end.
 *
 * The program it runs is the one every machine has and whose output is fixed: the shell's `echo` on POSIX and
 * `cmd /c echo` on Windows. What is asserted is the three things a caller can observe - the exit code, the output, and
 * that a program which cannot be started at all is a failure and not an exit code.
 *
 * `Process.arguments` and `Process.exit` are not testable in process: the first is what `main` handed over and the
 * second ends the process. The conformance suite covers both through `bootstrap/tests/native/`.
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
 * A command that is nowhere. `popen` starts a **shell**, which exists, and the shell then reports the missing program
 * with a code of its own - so this is an exit code and no failure of `run` either, and the message the shell wrote is in
 * the output. That is the honest answer for the one-pipe shape, and it is what `torb build` reads: a C compiler it
 * cannot start comes back as a non-zero code plus the shell's message.
 */
TORB_TEST(a_command_that_is_nowhere_answers_a_code_and_the_message_of_the_shell) {
  torb_text command = torb_text_from_cstring("torb-no-such-program-anywhere");
  torb_list arguments = argument_list(NULL, NULL);
  torb_text output = torb_text_empty();
  torb_text failure = torb_text_empty();

  TORB_CHECK(torb_process_run(command, arguments, &output, &failure) != 0);

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
  TORB_ADD(a_command_that_is_nowhere_answers_a_code_and_the_message_of_the_shell);
  TORB_ADD(an_argument_with_a_space_arrives_as_one_argument);
}
