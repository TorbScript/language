/*
 * panic.c - what a panic prints, what it runs, and the exit code (decided gap 9).
 *
 *     panic: <message>
 *       at src/file.trb:12:5
 *
 * to stderr, exit code 101, and nothing else runs: no release, no `Close`, no destructor. A panic is a bug, and
 * running more code in a broken program is how bugs get worse.
 *
 * The message is rendered into a fixed buffer, never allocated, so a panic still works when the heap is exhausted.
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
/* `exit` runs the atexit handlers, of which the runtime registers none, so this is observationally the same. */
#  define TORB_EXIT_IMMEDIATELY(code) exit(code)
#endif

const torb_location torb_location_unknown = { NULL, 0, 0 };

#define TORB_PANIC_BUFFER_SIZE 2048
#define TORB_MESSAGE_BUFFER_SIZE 1024

uint32_t torb_frame_limit = 100000;
static uint32_t torb_frame_count = 0;
static torb_panic_hook torb_hook = NULL;

void torb_set_panic_hook(torb_panic_hook hook) {
  torb_hook = hook;
}

/**
 * Renders the whole message, hands it to the hook if a test build installed one, and otherwise writes it to stderr
 * and leaves with 101. Does not return either way: if a hook returns anyway, the process still leaves.
 */
static TORB_NORETURN void torb_finish_panic(const char *message, torb_location at) {
  char buffer[TORB_PANIC_BUFFER_SIZE];
  if (at.path != NULL) {
    snprintf(buffer, sizeof buffer, "panic: %s\n  at %s:%u:%u", message, at.path, at.line, at.column);
  } else {
    snprintf(buffer, sizeof buffer, "panic: %s", message);
  }
  torb_frame_count = 0;
  if (torb_hook != NULL) {
    torb_hook(buffer);
  }
  fprintf(stderr, "%s\n", buffer);
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY(TORB_PANIC_EXIT_CODE);
}

void torb_panic(torb_text message, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  size_t length = (size_t)message.length;
  if (length > sizeof text - 1) {
    length = sizeof text - 1;
  }
  if (length > 0 && message.storage != NULL) {
    memcpy(text, message.storage->data + message.offset, length);
  }
  text[length] = '\0';
  torb_finish_panic(text, at);
}

void torb_panic_text(const char *message, torb_location at) {
  torb_finish_panic(message, at);
}

void torb_panic_overflow(const char *operation, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "arithmetic overflow in `%s`", operation);
  torb_finish_panic(text, at);
}

void torb_panic_division_by_zero(const char *operation, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "division by zero in `%s`", operation);
  torb_finish_panic(text, at);
}

void torb_panic_shift_amount(int64_t amount, int64_t width, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "shift by %lld, which is not between 0 and %lld", (long long)amount,
           (long long)(width - 1));
  torb_finish_panic(text, at);
}

void torb_panic_index_out_of_bounds(int64_t index, int64_t length, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "index %lld is out of bounds for a length of %lld", (long long)index,
           (long long)length);
  torb_finish_panic(text, at);
}

void torb_panic_range_reversed(int64_t from, int64_t to, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "the range %lld..%lld starts after it ends", (long long)from, (long long)to);
  torb_finish_panic(text, at);
}

void torb_panic_offset_past_end(int64_t offset, int64_t length, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "the offset %lld is past the end of a text of %lld bytes", (long long)offset,
           (long long)length);
  torb_finish_panic(text, at);
}

void torb_panic_offset_inside_character(int64_t offset, int64_t length, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "the offset %lld is inside of a character of a text of %lld bytes", (long long)offset,
           (long long)length);
  torb_finish_panic(text, at);
}

void torb_panic_invalid_utf8(int64_t offset, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "the byte at offset %lld is not valid UTF-8", (long long)offset);
  torb_finish_panic(text, at);
}

void torb_panic_out_of_memory(size_t size) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "out of memory: %lu bytes could not be allocated", (unsigned long)size);
  torb_finish_panic(text, torb_location_unknown);
}

void torb_panic_stack_overflow(torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "stack overflow: more than %lu frames", (unsigned long)torb_frame_limit);
  torb_finish_panic(text, at);
}

void torb_enter_frame(torb_location at) {
  torb_frame_count += 1;
  if (torb_frame_count > torb_frame_limit) {
    torb_panic_stack_overflow(at);
  }
}

void torb_leave_frame(void) {
  if (torb_frame_count > 0) {
    torb_frame_count -= 1;
  }
}
