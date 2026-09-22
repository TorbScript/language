/*
 * console.c - `print`, `printError` and `readLine`.
 *
 * `print(...values: Show)` joins the shown parts with one space and appends one `\n`. The join lives here, so the two
 * back ends cannot disagree about it and the conformance suite compares one format.
 *
 * The bytes are written as they are - to a pipe or a file that is the whole story, and it is what the conformance
 * suite compares. A live Windows **console** is a third case: it does not read the bytes as UTF-8 but through its own
 * code page, the same mismatch `docs/BACKEND.md` describes for `fopen` and a narrow path, and `SetConsoleOutputCP`
 * cannot fix it here because it is a global setting of the console window that outlives this process. So where the
 * standard handle behind a stream is a live console (`GetConsoleMode` on it succeeds - checked once per stream and
 * cached, because the answer cannot change while the process runs), the text is converted to UTF-16 and handed to
 * `WriteConsoleW` instead, in chunks that never split a surrogate pair. A pipe or a file never takes that path, so its
 * bytes stay exactly what they are. Reading a line follows the same split: a console is read with `ReadConsoleW`
 * and converted back, everything else with `fgetc`.
 *
 * `torb_write_line` is the same dispatch for the two reports the runtime writes itself - a panic and the line of a
 * test - which are rendered as plain bytes and not as a `torb_text`, so that a panic still works when the heap is
 * exhausted. Its console path converts on the **stack** for the same reason, and a line too long for that buffer falls
 * back to the raw bytes exactly as invalid UTF-8 does.
 */

#include "torb.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

/* ============================================================================================ Windows ========== */

#if defined(_WIN32)

/**
 * `WriteConsoleW` of some Windows console hosts refuses or truncates one call over about 64 KiB of UTF-16. A few
 * thousand units stays comfortably under that in every case and is still one call for almost every line a program
 * prints, so this is not tuned any tighter than that.
 */
#define TORB_CONSOLE_CHUNK_LIMIT 4096u

/**
 * The longest line `torb_write_line` converts for a console, in UTF-16 units. A panic renders into a fixed buffer of
 * 2048 bytes and a line of the test report is a name and a message, so nothing the runtime writes itself comes near
 * this; a line that does is written as its raw bytes instead, which is what a pipe gets anyway.
 */
#define TORB_CONSOLE_LINE_LIMIT 4096u

/**
 * How many of the first `length` units of `text` may be handed to one `WriteConsoleW` call without exceeding `limit`
 * and without splitting a surrogate pair - a low surrogate must never be separated from the high surrogate in front
 * of it, which one call at the boundary would otherwise do. Pure and total for every `length` and every `limit` of at
 * least one, which is what lets `runtime/tests/console_test.c` exercise the boundary without opening a console at
 * all: nothing above `console.c` calls this.
 */
size_t torb_console_chunk_length(const wchar_t *text, size_t length, size_t limit) {
  size_t chunk = limit == 0u ? 1u : limit;
  if (chunk > length) {
    chunk = length;
  }
  if (chunk > 0u && chunk < length && text[chunk - 1u] >= 0xD800u && text[chunk - 1u] <= 0xDBFFu) {
    /* The unit right at the cut is a high surrogate: keep it with the low surrogate one further, which is what makes
       the pair one character instead of two broken halves in two different calls. */
    chunk -= 1u;
    if (chunk == 0u) {
      /* `limit` itself lands inside the one pair there is - no cut respects both rules at once, so the pair wins and
         this call is one unit over `limit`. `length` is at least 2 here, because a lone high surrogate at the very
         end (`chunk == length`) never reaches this branch. */
      chunk = 2u;
    }
  }
  return chunk;
}

/**
 * Whether the standard handle named by `id` (`STD_OUTPUT_HANDLE`, `STD_ERROR_HANDLE` or `STD_INPUT_HANDLE`) is a live
 * console rather than a pipe or a file, decided once per handle and cached: what a standard stream is redirected to
 * is fixed for the life of the process, so asking `GetConsoleMode` again on every `print` would only repeat the same
 * answer. `*out_handle` is always set, so a caller never calls `GetStdHandle` a second time either.
 */
static bool torb_std_is_console(DWORD id, HANDLE *out_handle) {
  static DWORD known_ids[3];
  static HANDLE known_handles[3];
  static bool known_is_console[3];
  static size_t known_count = 0u;
  size_t index;
  for (index = 0u; index < known_count; index += 1u) {
    if (known_ids[index] == id) {
      *out_handle = known_handles[index];
      return known_is_console[index];
    }
  }
  {
    DWORD mode;
    HANDLE handle = GetStdHandle(id);
    bool is_console = handle != NULL && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0;
    known_ids[known_count] = id;
    known_handles[known_count] = handle;
    known_is_console[known_count] = is_console;
    known_count += 1u;
    *out_handle = handle;
    return is_console;
  }
}

/**
 * `parts` joined by one space and a trailing `\n`, converted to UTF-16 in one piece and written to the console behind
 * `handle` in chunks of `torb_console_chunk_length`. False where the join is not valid UTF-8 - which nothing above
 * this file rules out for text `torb_text_from_cstring` built - or holds a byte `torb_platform_wide` cannot see past
 * (an embedded NUL: a `String` may legally hold one, `torb_platform_wide` is NUL terminated because every other
 * caller of it hands it a path, which never does); the caller then falls back to the raw bytes, exactly the path a
 * pipe or a file always takes. `parts` borrowed. Not `static`: `console_test.c` calls it directly with a handle that
 * is never touched (invalid UTF-8 returns false before any Windows call reads it) to prove the fallback without
 * opening a console.
 */
bool torb_write_parts_console(HANDLE handle, const torb_text *parts, size_t count) {
  size_t total = 1u; /* the trailing '\n' */
  size_t filled = 0u;
  char *joined;
  size_t wide_capacity = 0u;
  wchar_t *wide;
  size_t wide_length;
  size_t written = 0u;
  size_t index;
  for (index = 0u; index < count; index += 1u) {
    total += (index > 0u ? 1u : 0u) + (size_t)parts[index].length;
  }
  joined = (char *)torb_raw_allocate(total + 1u);
  for (index = 0u; index < count; index += 1u) {
    if (index > 0u) {
      joined[filled] = ' ';
      filled += 1u;
    }
    if (parts[index].length > 0u && parts[index].storage != NULL) {
      memcpy(joined + filled, parts[index].storage->data + parts[index].offset, (size_t)parts[index].length);
      filled += (size_t)parts[index].length;
    }
  }
  joined[filled] = '\n';
  filled += 1u;
  joined[filled] = '\0';
  wide = torb_platform_wide(joined, &wide_capacity);
  torb_raw_free(joined, total + 1u);
  if (wide == NULL) {
    return false;
  }
  wide_length = wcslen(wide);
  while (written < wide_length) {
    size_t chunk = torb_console_chunk_length(wide + written, wide_length - written, TORB_CONSOLE_CHUNK_LIMIT);
    WriteConsoleW(handle, wide + written, (DWORD)chunk, NULL, NULL);
    written += chunk;
  }
  torb_raw_free(wide, wide_capacity);
  return true;
}

/**
 * `bytes` plus one `\n` to the console behind `handle`, converted on the stack so that nothing allocates: the panic
 * path renders its message into a fixed buffer for exactly that reason, and a report that allocates is a report that
 * cannot be written when the heap is gone. False where the line does not fit the buffer or is not valid UTF-8, and
 * the caller then writes the raw bytes - the path a pipe or a file always takes. Not `static`:
 * `runtime/tests/console_test.c` reaches the fallback through it without opening a console.
 */
bool torb_write_line_console(HANDLE handle, const char *bytes, size_t length) {
  wchar_t wide[TORB_CONSOLE_LINE_LIMIT];
  int units = 0;
  size_t total;
  size_t written = 0u;
  if (length + 1u >= TORB_CONSOLE_LINE_LIMIT) {
    return false;
  }
  if (length > 0u) {
    units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, (int)length, wide,
                                (int)(TORB_CONSOLE_LINE_LIMIT - 1u));
    if (units <= 0) {
      return false;
    }
  }
  wide[units] = L'\n';
  total = (size_t)units + 1u;
  while (written < total) {
    size_t chunk = torb_console_chunk_length(wide + written, total - written, TORB_CONSOLE_CHUNK_LIMIT);
    WriteConsoleW(handle, wide + written, (DWORD)chunk, NULL, NULL);
    written += chunk;
  }
  return true;
}

/**
 * `readLine` where standard input is a live console: `fgetc` would read the console's own code page, the input side
 * of the same mismatch `print` has on the way out, so the line is read as UTF-16 with `ReadConsoleW` and converted
 * back with `torb_platform_utf8`. The console's line mode only hands back a whole line once Enter is pressed and
 * leaves whatever did not fit in the buffer queued for the next call, which is what growing the buffer and calling
 * again relies on. Ctrl+Z at the very start of a line is the console's own end of file and reads zero characters -
 * the same case `fgetc` answers EOF for.
 */
static bool torb_read_line_console(HANDLE handle, torb_text *out) {
  size_t capacity = 128u;
  size_t length = 0u;
  wchar_t *buffer = (wchar_t *)torb_raw_allocate(capacity * sizeof(wchar_t));
  bool any = false;
  bool found_newline = false;
  char *utf8;
  size_t utf8_length = 0u;
  while (!found_newline) {
    DWORD read = 0u;
    /* One unit stays free for the terminator that is written after the loop */
    if (length + 1u >= capacity) {
      wchar_t *grown;
      /* A line past the limit of a text can never become one, and doubling the buffer could wrap */
      if (capacity > (size_t)UINT32_MAX || capacity > SIZE_MAX / 2u / sizeof(wchar_t)) {
        torb_panic_text("a text longer than 4 GiB is not supported", torb_location_unknown);
      }
      grown = (wchar_t *)torb_raw_allocate(capacity * 2u * sizeof(wchar_t));
      memcpy(grown, buffer, capacity * sizeof(wchar_t));
      torb_raw_free(buffer, capacity * sizeof(wchar_t));
      buffer = grown;
      capacity *= 2u;
    }
    if (!ReadConsoleW(handle, buffer + length, (DWORD)(capacity - length - 1u), &read, NULL) || read == 0u) {
      break;
    }
    any = true;
    {
      size_t scanned;
      for (scanned = 0u; scanned < (size_t)read; scanned += 1u) {
        if (buffer[length + scanned] == L'\n') {
          length += scanned + 1u;
          found_newline = true;
          break;
        }
      }
      if (!found_newline) {
        length += (size_t)read;
      }
    }
  }
  if (length > 0u && buffer[length - 1u] == L'\n') {
    length -= 1u;
  }
  if (length > 0u && buffer[length - 1u] == L'\r') {
    length -= 1u;
  }
  if (!any) {
    torb_raw_free(buffer, capacity * sizeof(wchar_t));
    return false;
  }
  buffer[length] = L'\0';
  utf8 = torb_platform_utf8(buffer, &utf8_length);
  torb_raw_free(buffer, capacity * sizeof(wchar_t));
  if (utf8 == NULL) {
    torb_panic_text("the console input is not valid Unicode", torb_location_unknown);
  }
  /* `torb_platform_utf8` only ever answers well formed UTF-8, so this cannot fail - the same pattern
     `torb_platform_list_directory` uses after the same conversion. */
  *out = torb_text_from_cstring(utf8);
  torb_raw_free(utf8, utf8_length + 1u);
  return true;
}

#endif

/* ================================================================================ the same on both platforms === */

/** `parts` joined by one space and a trailing `\n`, written as the raw UTF-8 bytes they are. The only path on POSIX,
 *  and the path a pipe, a file or invalid UTF-8 always takes on Windows too - a byte-for-byte copy of what this file
 *  wrote before a console ever needed different treatment. `parts` borrowed. Not `static`: `console_test.c` calls it
 *  directly against a `FILE *` of its own (never `stdout` or `stderr`) to prove a pipe or a file still gets exactly
 *  these bytes, without needing a real console to prove the dispatch around it is not taken. */
void torb_write_parts(FILE *stream, const torb_text *parts, size_t count) {
  size_t index;
  for (index = 0u; index < count; index += 1u) {
    if (index > 0u) {
      fputc(' ', stream);
    }
    if (parts[index].length > 0u && parts[index].storage != NULL) {
      fwrite(parts[index].storage->data + parts[index].offset, 1u, (size_t)parts[index].length, stream);
    }
  }
  fputc('\n', stream);
}

/** Dispatches to the console path when `stream` is `stdout` or `stderr` and that standard handle is a live console,
 *  flushing the stream first so anything already buffered for it keeps appearing before this call; the raw bytes of
 *  `torb_write_parts` otherwise, and again where the console path answers that the text was not convertible. */
static void torb_print_to(FILE *stream, const torb_text *parts, size_t count) {
#if defined(_WIN32)
  if (stream == stdout || stream == stderr) {
    HANDLE handle;
    if (torb_std_is_console(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, &handle)) {
      fflush(stream);
      if (torb_write_parts_console(handle, parts, count)) {
        return;
      }
    }
  }
#endif
  torb_write_parts(stream, parts, count);
}

/**
 * One line of raw UTF-8 bytes, through the same dispatch `print` goes through: `WriteConsoleW` where the stream is a
 * live console, the bytes themselves everywhere else. This is what the runtime's own two reports use - the panic
 * report of `panic.c` and the lines of `test.c` - so that a message with a non-ASCII character in it reads correctly
 * on a console while a pipe keeps exactly the bytes the conformance suite compares. `bytes` may hold `\n` of its own:
 * the whole line goes out in one call, and the one `\n` this appends is the end of it.
 */
void torb_write_line(FILE *stream, const char *bytes, size_t length) {
#if defined(_WIN32)
  if (stream == stdout || stream == stderr) {
    HANDLE handle;
    if (torb_std_is_console(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, &handle)) {
      fflush(stream);
      if (torb_write_line_console(handle, bytes, length)) {
        return;
      }
    }
  }
#endif
  if (length > 0u && bytes != NULL) {
    fwrite(bytes, 1u, length, stream);
  }
  fputc('\n', stream);
}

/** The two standard streams of `torb_write_line`, which is how `torb.h` names it without naming `FILE *`. */
void torb_write_line_out(const char *bytes, size_t length) {
  torb_write_line(stdout, bytes, length);
}

void torb_write_line_error(const char *bytes, size_t length) {
  torb_write_line(stderr, bytes, length);
}

void torb_print(torb_text text) {
  torb_print_to(stdout, &text, 1u);
}

/* Standard output is buffered when it is a pipe or a file and standard error is not, so a line to stderr flushes stdout
   first: the two streams of a program that reports a failure then arrive in the order the program wrote them, the way
   they do for a panic (panic.c). */
void torb_print_error(torb_text text) {
  fflush(stdout);
  torb_print_to(stderr, &text, 1u);
  fflush(stderr);
}

void torb_print_parts(const torb_text *parts, size_t count) {
  torb_print_to(stdout, parts, count);
}

void torb_print_error_parts(const torb_text *parts, size_t count) {
  fflush(stdout);
  torb_print_to(stderr, parts, count);
  fflush(stderr);
}

bool torb_read_line(torb_text *out) {
#if defined(_WIN32)
  {
    HANDLE handle;
    if (torb_std_is_console(STD_INPUT_HANDLE, &handle)) {
      return torb_read_line_console(handle, out);
    }
  }
#endif
  {
    size_t capacity = 128u;
    size_t length = 0u;
    uint8_t *buffer = (uint8_t *)torb_raw_allocate(capacity);
    bool any = false;
    size_t bad_offset = 0u;
    for (;;) {
      int byte = fgetc(stdin);
      if (byte == EOF) {
        break;
      }
      any = true;
      if (byte == '\n') {
        break;
      }
      if (length + 1u > capacity) {
        uint8_t *grown;
        /* A line past the limit of a text can never become one, and doubling the buffer could wrap */
        if (capacity > (size_t)UINT32_MAX || capacity > SIZE_MAX / 2u) {
          torb_panic_text("a text longer than 4 GiB is not supported", torb_location_unknown);
        }
        grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
        memcpy(grown, buffer, length);
        torb_raw_free(buffer, capacity);
        buffer = grown;
        capacity *= 2u;
      }
      buffer[length] = (uint8_t)byte;
      length += 1u;
    }
    if (length > 0u && buffer[length - 1u] == '\r') {
      length -= 1u;
    }
    if (!any) {
      torb_raw_free(buffer, capacity);
      return false;
    }
    if (!torb_text_try_from_bytes(buffer, length, out, &bad_offset)) {
      torb_raw_free(buffer, capacity);
      torb_panic_invalid_utf8((int64_t)bad_offset, torb_location_unknown);
    }
    torb_raw_free(buffer, capacity);
    return true;
  }
}
