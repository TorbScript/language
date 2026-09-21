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

#include "harness.h"

#include <stdio.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

/* Defined in console.c, not declared in torb.h - see the file comment above. */
void torb_write_parts(FILE *stream, const torb_text *parts, size_t count);
#if defined(_WIN32)
size_t torb_console_chunk_length(const wchar_t *text, size_t length, size_t limit);
bool torb_write_parts_console(HANDLE handle, const torb_text *parts, size_t count);
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

#endif

void torb_register_console_tests(void) {
  TORB_ADD(a_file_receives_the_raw_bytes_of_the_join);
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
#endif
}
