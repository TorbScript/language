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
#include "torb_pool.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#endif

/*
 * One line at a time. A line is several writes (the parts, the spaces, the `\n`), and two workers that print at once
 * must not interleave inside one, so every line is written under this lock. Recursive by hand, like the lock of
 * memory.c, because a panic while this thread holds it (an allocation of the console path that fails) writes its report
 * through the same path.
 */
static torb_mutex torb_console_mutex = TORB_MUTEX_INITIALIZER;
static uint32_t torb_console_owner = 0u;
static unsigned torb_console_depth = 0u;

static void torb_console_lock(void) {
  uint32_t self = torb_worker_current()->index + 1u;
  if (torb_atomic_peek_u32(&torb_console_owner) == self) {
    torb_console_depth += 1u;
    return;
  }
  torb_mutex_lock(&torb_console_mutex);
  torb_atomic_store_u32(&torb_console_owner, self);
  torb_console_depth = 1u;
}

static void torb_console_unlock(void) {
  torb_console_depth -= 1u;
  if (torb_console_depth == 0u) {
    torb_atomic_store_u32(&torb_console_owner, 0u);
    torb_mutex_unlock(&torb_console_mutex);
  }
}

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
  torb_console_lock();
#if defined(_WIN32)
  if (stream == stdout || stream == stderr) {
    HANDLE handle;
    if (torb_std_is_console(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, &handle)) {
      fflush(stream);
      if (torb_write_parts_console(handle, parts, count)) {
        torb_console_unlock();
        return;
      }
    }
  }
#endif
  torb_write_parts(stream, parts, count);
  torb_console_unlock();
}

/**
 * One line of raw UTF-8 bytes, through the same dispatch `print` goes through: `WriteConsoleW` where the stream is a
 * live console, the bytes themselves everywhere else. This is what the runtime's own two reports use - the panic
 * report of `panic.c` and the lines of `test.c` - so that a message with a non-ASCII character in it reads correctly
 * on a console while a pipe keeps exactly the bytes the conformance suite compares. `bytes` may hold `\n` of its own:
 * the whole line goes out in one call, and the one `\n` this appends is the end of it.
 */
void torb_write_line(FILE *stream, const char *bytes, size_t length) {
  torb_console_lock();
#if defined(_WIN32)
  if (stream == stdout || stream == stderr) {
    HANDLE handle;
    if (torb_std_is_console(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, &handle)) {
      fflush(stream);
      if (torb_write_line_console(handle, bytes, length)) {
        torb_console_unlock();
        return;
      }
    }
  }
#endif
  if (length > 0u && bytes != NULL) {
    fwrite(bytes, 1u, length, stream);
  }
  fputc('\n', stream);
  torb_console_unlock();
}

/* The cache of `torb_std_is_console` is filled before a second thread exists, so every later look-up only reads it. */
void torb_console_prepare(void) {
#if defined(_WIN32)
  HANDLE handle;
  (void)torb_std_is_console(STD_OUTPUT_HANDLE, &handle);
  (void)torb_std_is_console(STD_ERROR_HANDLE, &handle);
  (void)torb_std_is_console(STD_INPUT_HANDLE, &handle);
#endif
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

/*
 * The byte streams of std/io (runtime/stream.c), which a thread of the blocking pool reads and writes. Standard input
 * as bytes as they come: a console on Windows is read as UTF-16 and handed on as its UTF-8, as `readLine` reads it,
 * everything else through the system's own read, which answers what arrived without waiting for more.
 */
int64_t torb_read_standard_bytes(uint8_t *buffer, size_t maximum) {
#if defined(_WIN32)
  HANDLE handle;
  if (torb_std_is_console(STD_INPUT_HANDLE, &handle)) {
    wchar_t wide[1024];
    DWORD read = 0u;
    size_t wanted = maximum / 3u;
    int bytes;
    if (wanted > 1024u) {
      wanted = 1024u;
    }
    if (wanted == 0u) {
      wanted = 1u;
    }
    if (!ReadConsoleW(handle, wide, (DWORD)wanted, &read, NULL)) {
      return -(int64_t)GetLastError();
    }
    /* Ctrl+Z at the start of a line is the console's end of the input */
    if (read == 0u || wide[0] == 0x1A) {
      return 0;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0u, wide, (int)read, (char *)buffer, (int)maximum, NULL, NULL);
    return bytes <= 0 ? 0 : (int64_t)bytes;
  }
  {
    DWORD read = 0u;
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
      return 0;
    }
    if (!ReadFile(handle, buffer, (DWORD)(maximum > 0x7FFFFFFFu ? 0x7FFFFFFFu : maximum), &read, NULL)) {
      const DWORD failure = GetLastError();
      if (failure == ERROR_BROKEN_PIPE || failure == ERROR_HANDLE_EOF) {
        return 0;
      }
      return -(int64_t)failure;
    }
    return (int64_t)read;
  }
#else
  ssize_t got;
  do {
    got = read(STDIN_FILENO, buffer, maximum);
  } while (got < 0 && errno == EINTR);
  return got < 0 ? -(int64_t)errno : (int64_t)got;
#endif
}

/*
 * Bytes to standard output or standard error, under the lock of every line and through the dispatch `print` takes: a
 * live Windows console gets them as UTF-16 where they are valid UTF-8, everything else the bytes themselves, flushed at
 * once, because a stream of std/io is not buffered - `buffered(capacity:)` says where it is.
 */
bool torb_write_standard_bytes(bool error, const uint8_t *bytes, size_t length) {
  FILE *stream = error ? stderr : stdout;
  bool written = true;
  torb_console_lock();
#if defined(_WIN32)
  {
    HANDLE handle;
    if (length > 0u && torb_std_is_console(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE, &handle)) {
      int units;
      fflush(stream);
      units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)bytes, (int)length, NULL, 0);
      if (units > 0) {
        wchar_t *wide = (wchar_t *)malloc((size_t)units * sizeof(wchar_t));
        if (wide != NULL) {
          size_t done = 0u;
          (void)MultiByteToWideChar(CP_UTF8, 0u, (const char *)bytes, (int)length, wide, units);
          while (done < (size_t)units) {
            size_t chunk = torb_console_chunk_length(wide + done, (size_t)units - done, TORB_CONSOLE_CHUNK_LIMIT);
            WriteConsoleW(handle, wide + done, (DWORD)chunk, NULL, NULL);
            done += chunk;
          }
          free(wide);
          torb_console_unlock();
          return true;
        }
      }
    }
  }
#endif
  if (length > 0u && fwrite(bytes, 1u, length, stream) < length) {
    written = false;
  }
  if (fflush(stream) != 0) {
    written = false;
  }
  torb_console_unlock();
  return written;
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
      /* `byte == EOF` here is either the real end of the input or `fgetc` interrupted by the Ctrl+C handler below
         (`EINTR`, once `torb_install_interrupt_handler` is installed): either way `stdin`'s error indicator is
         cleared, so a genuine interruption does not leave every read after it failing too. `torb_take_interrupt`
         is how the caller tells the two apart. */
      clearerr(stdin);
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

/* =================================================================================== the terminal and Ctrl+C === */

/**
 * `isTerminal()`: whether standard output is a live console rather than a pipe or a file - the same question
 * `torb_std_is_console` answers for `print` on Windows, and `isatty` everywhere else. Decided fresh every call: the
 * answer cannot change while the process runs, but nothing above this file calls it often enough for that to matter,
 * and `torb_std_is_console` already caches its own half of the answer.
 */
#if defined(_WIN32)
bool torb_is_terminal(void) {
  HANDLE handle;
  return torb_std_is_console(STD_OUTPUT_HANDLE, &handle);
}
#else
bool torb_is_terminal(void) {
  return isatty(fileno(stdout)) != 0;
}
#endif

/**
 * `printErrorRaw(text: String)`: `text` to standard error exactly as given - no join, no trailing `\n`. The prompt of
 * `torb repl` (docs/design/REPL.md section 2) writes with this, because a prompt is not a line of its own: the line
 * it starts is finished by what the terminal echoes after it, not by this call. Goes through the same dispatch
 * `print` does - a live Windows console gets `WriteConsoleW`, everything else gets the raw bytes - and flushes
 * standard error itself, because a prompt has to be on the screen before the blocking read that follows it runs.
 */
void torb_print_error_raw(torb_text text) {
  torb_console_lock();
#if defined(_WIN32)
  if (text.length > 0u) {
    HANDLE handle;
    if (torb_std_is_console(STD_ERROR_HANDLE, &handle)) {
      char *bytes = (char *)torb_raw_allocate((size_t)text.length + 1u);
      size_t wide_capacity = 0u;
      wchar_t *wide;
      memcpy(bytes, text.storage->data + text.offset, (size_t)text.length);
      bytes[text.length] = '\0';
      wide = torb_platform_wide(bytes, &wide_capacity);
      torb_raw_free(bytes, (size_t)text.length + 1u);
      if (wide != NULL) {
        size_t wide_length = wcslen(wide);
        size_t written = 0u;
        while (written < wide_length) {
          size_t chunk = torb_console_chunk_length(wide + written, wide_length - written, TORB_CONSOLE_CHUNK_LIMIT);
          WriteConsoleW(handle, wide + written, (DWORD)chunk, NULL, NULL);
          written += chunk;
        }
        torb_raw_free(wide, wide_capacity);
        fflush(stderr);
        torb_console_unlock();
        return;
      }
    }
  }
#endif
  if (text.length > 0u && text.storage != NULL) {
    fwrite(text.storage->data + text.offset, 1u, (size_t)text.length, stderr);
  }
  fflush(stderr);
  torb_console_unlock();
}

/**
 * Set by the handler below and read and cleared by `torb_take_interrupt`: a plain flag, not a counter, because a
 * second Ctrl+C before the first is taken answers the same question the first one did. The same atomic load/store
 * every other cross-thread flag of the runtime uses (`torb_pool.h`), since the handler runs on its own thread on
 * Windows and inside the signal itself on POSIX.
 */
static uint32_t torb_interrupt_flag = 0u;
static uint32_t torb_interrupt_installed = 0u;

#if defined(_WIN32)

static BOOL WINAPI torb_interrupt_console_handler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
    torb_atomic_store_u32(&torb_interrupt_flag, 1u);
    return TRUE;
  }
  return FALSE;
}

/** `installInterruptHandler()`. Idempotent: `torb repl` is the only caller, and it may run this more than once
 *  (`:reset`) without installing a second handler. */
void torb_install_interrupt_handler(void) {
  if (torb_atomic_peek_u32(&torb_interrupt_installed) != 0u) {
    return;
  }
  torb_atomic_store_u32(&torb_interrupt_installed, 1u);
  SetConsoleCtrlHandler(torb_interrupt_console_handler, TRUE);
}

#else

static void torb_interrupt_signal_handler(int signal_number) {
  (void)signal_number;
  torb_atomic_store_u32(&torb_interrupt_flag, 1u);
}

void torb_install_interrupt_handler(void) {
  struct sigaction action;
  if (torb_atomic_peek_u32(&torb_interrupt_installed) != 0u) {
    return;
  }
  torb_atomic_store_u32(&torb_interrupt_installed, 1u);
  memset(&action, 0, sizeof(action));
  action.sa_handler = torb_interrupt_signal_handler;
  /* No `SA_RESTART`: a blocking read of standard input has to return early (`EINTR`) so `torb_read_line` sees it
     instead of only the next real line. */
  sigaction(SIGINT, &action, NULL);
}

#endif

/** `interrupted()`: whether Ctrl+C arrived since the last call, which this also clears - the VM's budget
 *  (`compiler/src/vm/interpret.trb`) asks on every refill, and `torb repl`'s reader asks after every blocked
 *  read that answered nothing. */
bool torb_take_interrupt(void) {
  if (torb_atomic_peek_u32(&torb_interrupt_flag) == 0u) {
    return false;
  }
  torb_atomic_store_u32(&torb_interrupt_flag, 0u);
  return true;
}
