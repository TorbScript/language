/*
 * console_test.c - `print`/`printError` writing, without ever needing a real console.
 *
 * `runtime/console.c` only treats `stdout`/`stderr` specially when the standard handle behind them is a live console
 * (`docs/BACKEND.md`, "What the boundary to the operating system decided"). Nothing here can open one, so what is
 * tested is everything that does not require it: the raw-byte path a pipe or a file always takes, the pure UTF-16
 * chunk boundary that never splits a surrogate pair, and that invalid UTF-8 falls back instead of being handed to a
 * Windows call. `torb_write_parts`, `torb_write_parts_console` and `torb_console_chunk_length` are not `static` in
 * `console.c` for exactly this reason and are declared here rather than in `include/torb.h`, because their signatures
 * (`FILE *`, and on Windows `wchar_t *`/`HANDLE`) are not part of the portable, `<stdio.h>`-free ABI the rest of that
 * header keeps.
 */

/* `dup`, `dup2`, `fileno` and `lseek`, which the ordering test below points the standard streams away with */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
/* On macOS `_POSIX_C_SOURCE` alone hides every Darwin extension (`_SC_NPROCESSORS_ONLN`, `SO_NOSIGPIPE`,
 * `pthread_cond_timedwait_relative_np`); `_DARWIN_C_SOURCE` shows them again beside POSIX. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE
#endif

#include "harness.h"

#include <stdio.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  define TORB_TEST_DUP _dup
#  define TORB_TEST_DUP2 _dup2
#  define TORB_TEST_FILENO _fileno
#  define TORB_TEST_LSEEK _lseek
#  define TORB_TEST_CLOSE _close
#else
#  include <unistd.h>
#  define TORB_TEST_DUP dup
#  define TORB_TEST_DUP2 dup2
#  define TORB_TEST_FILENO fileno
#  define TORB_TEST_LSEEK lseek
#  define TORB_TEST_CLOSE close
#endif

/* Defined in console.c, not declared in torb.h - see the file comment above. */
void torb_write_parts(FILE *stream, const torb_text *parts, size_t count);
void torb_write_line(FILE *stream, const char *bytes, size_t length);
#if defined(_WIN32)
size_t torb_console_chunk_length(const wchar_t *text, size_t length, size_t limit);
bool torb_write_parts_console(HANDLE handle, const torb_text *parts, size_t count);
bool torb_write_line_console(HANDLE handle, const char *bytes, size_t length);
#endif

/* --------------------------------------------------------------------------------- the raw-byte path, both OSes --- */

/**
 * `torb_write_parts` against a `FILE *` that is neither `stdout` nor `stderr`: on Windows this is exactly the branch
 * the console dispatch never takes, and on POSIX it is the only branch there is - either way, the file gets the join
 * `console.c` has always produced, byte for byte, non-ASCII included.
 */
TORB_TEST(a_file_receives_the_raw_bytes_of_the_join) {
  const char *path = "torb-runtime-test-console-output.bin";
  torb_text first = torb_text_from_cstring("grüße");
  torb_text second = torb_text_from_cstring("日本");
  torb_text parts[2];
  FILE *file;
  uint8_t read_back[64];
  size_t read_length;
  const char *expected = "grüße 日本\n";
  parts[0] = first;
  parts[1] = second;

  file = fopen(path, "wb");
  TORB_CHECK(file != NULL);
  torb_write_parts(file, parts, 2u);
  fclose(file);

  file = fopen(path, "rb");
  TORB_CHECK(file != NULL);
  read_length = fread(read_back, 1u, sizeof read_back, file);
  fclose(file);
  remove(path);

  TORB_CHECK_INTEGER(read_length, strlen(expected));
  TORB_CHECK(memcmp(read_back, expected, read_length) == 0);

  torb_text_release(first);
  torb_text_release(second);
}

/**
 * `torb_write_line` against a `FILE *` that is neither `stdout` nor `stderr`: the bytes it was given and one `\n`, and
 * a `\n` the caller put inside the line stays where it is - the panic report is two lines written as one call.
 */
TORB_TEST(a_file_receives_the_raw_bytes_of_a_line) {
  const char *path = "torb-runtime-test-console-line.bin";
  const char *expected = "panic: grüße\n  at src/main.trb:1:1\n";
  FILE *file;
  uint8_t read_back[64];
  size_t read_length;

  file = fopen(path, "wb");
  TORB_CHECK(file != NULL);
  torb_write_line(file, "panic: grüße\n  at src/main.trb:1:1", strlen("panic: grüße\n  at src/main.trb:1:1"));
  fclose(file);

  file = fopen(path, "rb");
  TORB_CHECK(file != NULL);
  read_length = fread(read_back, 1u, sizeof read_back, file);
  fclose(file);
  remove(path);

  TORB_CHECK_INTEGER(read_length, strlen(expected));
  TORB_CHECK(memcmp(read_back, expected, read_length) == 0);
}

/**
 * A line to standard error writes out what standard output still holds first, so the two streams of a program that
 * reports a failure arrive in the order it wrote them (`torb_print_error`). Both standard streams point at files for the
 * length of the test, and what had reached the output file when the error line went out is what is asserted: without
 * the flush, `before` is still in the buffer of `stdout` at that moment.
 */
TORB_TEST(a_line_to_standard_error_writes_standard_output_out_first) {
  const char *output_path = "torb-runtime-test-console-order-output.bin";
  const char *error_path = "torb-runtime-test-console-order-error.bin";
  FILE *captured_output = fopen(output_path, "w+b");
  FILE *captured_error = fopen(error_path, "w+b");
  torb_text line = torb_text_from_cstring("after");
  int saved_output;
  int saved_error;
  long written;
  TORB_CHECK(captured_output != NULL);
  TORB_CHECK(captured_error != NULL);
  fflush(stdout);
  fflush(stderr);
  saved_output = TORB_TEST_DUP(TORB_TEST_FILENO(stdout));
  saved_error = TORB_TEST_DUP(TORB_TEST_FILENO(stderr));
  TORB_TEST_DUP2(TORB_TEST_FILENO(captured_output), TORB_TEST_FILENO(stdout));
  TORB_TEST_DUP2(TORB_TEST_FILENO(captured_error), TORB_TEST_FILENO(stderr));

  fputs("before", stdout);
  torb_print_error(line);
  written = (long)TORB_TEST_LSEEK(TORB_TEST_FILENO(captured_output), 0, SEEK_END);

  fflush(stdout);
  fflush(stderr);
  TORB_TEST_DUP2(saved_output, TORB_TEST_FILENO(stdout));
  TORB_TEST_DUP2(saved_error, TORB_TEST_FILENO(stderr));
  TORB_TEST_CLOSE(saved_output);
  TORB_TEST_CLOSE(saved_error);
  fclose(captured_output);
  fclose(captured_error);
  remove(output_path);
  remove(error_path);
  torb_text_release(line);
  TORB_CHECK_INTEGER(written, 6);
}

/**
 * **A line is out when the call that wrote it returns** (the policy at the top of `console.c`). Standard output points
 * at a file for the length of the test - C buffers a file in blocks, exactly as it buffers the pipe of `docker logs` -
 * and the size of the file is read right after each call, before anything else flushes: every line of `print`, and
 * every line the runtime writes itself (the test report), is already in it. Without the flush at the end of a line the
 * file would still be empty.
 *
 * Where the tests run in a live Windows console, `console.c` decided once for the whole process that standard output
 * is that console and writes it with `WriteConsoleW`, past the stream this test points away - there is nothing to
 * observe then, and the test has nothing to say.
 */
TORB_TEST(a_printed_line_reaches_a_file_before_print_returns) {
  const char *output_path = "torb-runtime-test-console-line-output.bin";
  FILE *captured_output;
  torb_text parts[2];
  int saved_output;
  long after_print;
  long after_second;
  long after_report;
  char read_back[32];
  size_t read_length;
#if defined(_WIN32)
  if (torb_is_terminal()) {
    return;
  }
#endif
  captured_output = fopen(output_path, "w+b");
  TORB_CHECK(captured_output != NULL);
  parts[0] = torb_text_from_cstring("ready");
  parts[1] = torb_text_from_cstring("now");
  fflush(stdout);
  saved_output = TORB_TEST_DUP(TORB_TEST_FILENO(stdout));
  TORB_TEST_DUP2(TORB_TEST_FILENO(captured_output), TORB_TEST_FILENO(stdout));

  torb_print_parts(parts, 2u);
  after_print = (long)TORB_TEST_LSEEK(TORB_TEST_FILENO(captured_output), 0, SEEK_END);
  torb_print(parts[1]);
  after_second = (long)TORB_TEST_LSEEK(TORB_TEST_FILENO(captured_output), 0, SEEK_END);
  torb_write_line_out("done", 4u);
  after_report = (long)TORB_TEST_LSEEK(TORB_TEST_FILENO(captured_output), 0, SEEK_END);

  fflush(stdout);
  TORB_TEST_DUP2(saved_output, TORB_TEST_FILENO(stdout));
  TORB_TEST_CLOSE(saved_output);
  rewind(captured_output);
  read_length = fread(read_back, 1u, sizeof read_back - 1u, captured_output);
  read_back[read_length] = '\0';
  fclose(captured_output);
  remove(output_path);
  torb_text_release(parts[0]);
  torb_text_release(parts[1]);
  TORB_CHECK_INTEGER(after_print, 10);
  TORB_CHECK_INTEGER(after_second, 14);
  TORB_CHECK_INTEGER(after_report, 19);
  TORB_CHECK(strcmp(read_back, "ready now\nnow\ndone\n") == 0);
}

/*
 * torb_install_interrupt_handler/torb_take_interrupt (docs/design/REPL.md section 2 - Ctrl+C for `torb repl`).
 * Raising a real signal against this process would risk the test binary itself if anything about the harness does
 * not expect it, so what is tested is the flag's own protocol without one: it starts clear, stays clear without an
 * interrupt, and installing the handler more than once does not crash or otherwise misbehave.
 */
TORB_TEST(the_interrupt_flag_starts_and_stays_clear_without_a_signal) {
  TORB_CHECK(!torb_take_interrupt());
  TORB_CHECK(!torb_take_interrupt());
}

TORB_TEST(installing_the_interrupt_handler_more_than_once_is_harmless) {
  torb_install_interrupt_handler();
  torb_install_interrupt_handler();
  TORB_CHECK(!torb_take_interrupt());
}

/* ============================================================================================ Windows only ===== */

#if defined(_WIN32)

/** Under the limit, the whole text is one chunk - the common case, a line far short of `TORB_CONSOLE_CHUNK_LIMIT`. */
TORB_TEST(a_short_text_is_one_chunk) {
  const wchar_t text[] = L"hello";
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 5u, 10u), 5u);
}

/** At the limit with no surrogate anywhere near the cut, the chunk is exactly the limit. */
TORB_TEST(a_cut_away_from_any_surrogate_is_exactly_the_limit) {
  const wchar_t text[10] = { L'a', L'a', L'a', L'a', L'a', L'a', L'a', L'a', L'a', L'a' };
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 10u, 4u), 4u);
}

/** A limit that would land between the two halves of a surrogate pair backs off by one unit instead. */
TORB_TEST(a_cut_through_a_surrogate_pair_backs_off) {
  /* U+1F389 PARTY POPPER, `a` on either side - the emoji the platform test converts the same way. */
  const wchar_t text[] = { L'a', 0xD83Cu, 0xDF89u, L'a' };
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 4u, 2u), 1u);
  /* Two more units reach past the pair, so it is whole in this chunk and the cut moves to right after it. */
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 4u, 3u), 3u);
}

/** `limit` of exactly one landing on a high surrogate has no cut that both respects it and keeps the pair whole; the
 *  pair wins, one unit over `limit`, rather than a low surrogate ever being written on its own. */
TORB_TEST(a_limit_of_one_on_a_surrogate_still_keeps_the_pair) {
  const wchar_t text[] = { 0xD83Cu, 0xDF89u, L'b' };
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 3u, 1u), 2u);
}

/** A high surrogate as the very last unit of the whole text is not "cut" at all - there is nothing after it to keep
 *  it whole with, so the chunk still reaches the end. */
TORB_TEST(a_trailing_high_surrogate_at_the_true_end_is_not_special) {
  const wchar_t text[] = { L'a', 0xD83Cu };
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 2u, 2u), 2u);
}

/** A `limit` of zero still makes progress: it is treated as one unit, never zero units forever. */
TORB_TEST(a_limit_of_zero_still_advances) {
  const wchar_t text[] = { L'a', L'b' };
  TORB_CHECK_INTEGER(torb_console_chunk_length(text, 2u, 0u), 1u);
}

/** Invalid UTF-8 has no UTF-16 form (`MB_ERR_INVALID_CHARS` inside `torb_platform_wide`), so the console writer
 *  answers false before it ever reads `handle` - which is why passing one that names no real console is safe here. */
TORB_TEST(invalid_utf8_is_reported_as_not_convertible) {
  TORB_LITERAL_BYTES(bad, 1) = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 1, { 0x80u } };
  torb_text text = torb_text_from_storage(&bad, 0u, 1u);
  TORB_CHECK(!torb_write_parts_console(INVALID_HANDLE_VALUE, &text, 1u));
}

/** An empty join (no parts) is valid UTF-8 too (it is just the trailing `\n`) and converts. */
TORB_TEST(an_empty_join_is_convertible) {
  TORB_CHECK(torb_write_parts_console(INVALID_HANDLE_VALUE, NULL, 0u));
}

/**
 * A text far longer than `TORB_CONSOLE_CHUNK_LIMIT`, built from a four-byte character so the loop's chunk boundaries
 * land inside surrogate pairs somewhere along the way: the conversion and the chunk loop still run to completion and
 * report success, which `INVALID_HANDLE_VALUE` lets this test check without a console - `WriteConsoleW` fails
 * silently on it (documented Win32 behaviour for a bad handle, not a crash), and this function does not treat a
 * failed write as a reason to fall back.
 */
TORB_TEST(a_text_over_the_chunk_limit_is_still_reported_convertible) {
  torb_text emoji = torb_text_from_cstring("\xF0\x9F\x8E\x89"); /* U+1F389, one surrogate pair in UTF-16 */
  torb_text long_text = torb_text_repeat(emoji, 20000, torb_location_unknown);
  TORB_CHECK(torb_write_parts_console(INVALID_HANDLE_VALUE, &long_text, 1u));
  torb_text_release(long_text);
  torb_text_release(emoji);
}

/** A line the runtime writes itself converts on the stack, so valid UTF-8 that fits is reported convertible. */
TORB_TEST(a_line_of_raw_bytes_is_convertible) {
  const char *line = "  FAILED  grüße > 日本";
  TORB_CHECK(torb_write_line_console(INVALID_HANDLE_VALUE, line, strlen(line)));
  TORB_CHECK(torb_write_line_console(INVALID_HANDLE_VALUE, NULL, 0u));
}

/** Invalid UTF-8 has no UTF-16 form, and a line longer than the stack buffer has nowhere to go: both fall back. */
TORB_TEST(a_line_that_cannot_be_converted_falls_back) {
  char *long_line = (char *)torb_raw_allocate(9000u);
  memset(long_line, 'a', 9000u);
  TORB_CHECK(!torb_write_line_console(INVALID_HANDLE_VALUE, "\x80", 1u));
  TORB_CHECK(!torb_write_line_console(INVALID_HANDLE_VALUE, long_line, 9000u));
  torb_raw_free(long_line, 9000u);
}

#endif

void torb_register_console_tests(void) {
  TORB_ADD(a_file_receives_the_raw_bytes_of_the_join);
  TORB_ADD(a_file_receives_the_raw_bytes_of_a_line);
  TORB_ADD(a_line_to_standard_error_writes_standard_output_out_first);
  TORB_ADD(a_printed_line_reaches_a_file_before_print_returns);
  TORB_ADD(the_interrupt_flag_starts_and_stays_clear_without_a_signal);
  TORB_ADD(installing_the_interrupt_handler_more_than_once_is_harmless);
#if defined(_WIN32)
  TORB_ADD(a_short_text_is_one_chunk);
  TORB_ADD(a_cut_away_from_any_surrogate_is_exactly_the_limit);
  TORB_ADD(a_cut_through_a_surrogate_pair_backs_off);
  TORB_ADD(a_limit_of_one_on_a_surrogate_still_keeps_the_pair);
  TORB_ADD(a_trailing_high_surrogate_at_the_true_end_is_not_special);
  TORB_ADD(a_limit_of_zero_still_advances);
  TORB_ADD(invalid_utf8_is_reported_as_not_convertible);
  TORB_ADD(an_empty_join_is_convertible);
  TORB_ADD(a_text_over_the_chunk_limit_is_still_reported_convertible);
  TORB_ADD(a_line_of_raw_bytes_is_convertible);
  TORB_ADD(a_line_that_cannot_be_converted_falls_back);
#endif
}
