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
 * Running out of memory prints the same way and leaves with `TORB_OUT_OF_MEMORY_EXIT_CODE` (102) instead, past every
 * recovery point.
 */

#include "torb.h"
#include "torb_pool.h"

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

#define TORB_PANIC_BUFFER_SIZE 4096
#define TORB_MESSAGE_BUFFER_SIZE 1024
/** The frames of the `dev` profile, rendered below the site. */
#define TORB_FRAMES_BUFFER_SIZE 2048
/** A recursion that ran out of stack has thousands of frames, and the innermost ones are what says where. */
#define TORB_FRAMES_SHOWN 24u

#if defined(TORB_FRAMES)
_Thread_local torb_frame *torb_frame_innermost = NULL;
#endif

/**
 * How much of the stack the check leaves over: the panic renders into two buffers of a few kilobytes and writes through
 * `stdio` or `WriteConsoleW`, and a function without the check - a leaf, a function of the runtime, a call of the
 * operating system - may still run below the last one that has it. A tenth of the smallest main thread stack a C
 * toolchain gives (1 MiB with MSVC, 2 MiB with MinGW, 8 MiB on Linux and macOS).
 */
#define TORB_STACK_RESERVE ((uintptr_t)128u * 1024u)

uintptr_t torb_stack_reserve = 0u;
#if !(defined(_WIN64) && (defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)))
#  if defined(_MSC_VER)
__declspec(thread) uintptr_t torb_thread_stack_floor = 0u;
#  else
_Thread_local uintptr_t torb_thread_stack_floor = 0u;
#  endif
#endif
static torb_panic_hook torb_hook = NULL;
static torb_debug_panic_hook torb_debug_hook = NULL;
/** Set by the first thread that panics: a second panic on another worker waits for the first to end the process. */
static uint32_t torb_panicking = 0u;

void torb_set_panic_hook(torb_panic_hook hook) {
  torb_hook = hook;
}

void torb_set_debug_panic_hook(torb_debug_panic_hook hook) {
  torb_debug_hook = hook;
}

/* The recovery point is the running thread's: a panic on a worker must never jump into a frame of the main thread. */
torb_recovery *torb_begin_recovery(torb_recovery *point) {
  torb_worker *worker = torb_worker_current();
  torb_recovery *previous = worker->recovery;
  if (point != NULL) {
    point->frames[0] = '\0';
#if defined(TORB_FRAMES)
    point->innermost = torb_frame_innermost;
#else
    point->innermost = NULL;
#endif
  }
  worker->recovery = point;
  return previous;
}

/**
 * The frames of the `dev` profile below a panic's site (`torb_frame` in `torb.h`), one line each and innermost first:
 * `  in <function>`, and `, at <site>` behind every frame but the innermost, whose line is the site itself. The first
 * `TORB_FRAMES_SHOWN`, then how many more there are. Renders the empty text in every other build.
 */
static void torb_render_frames(char *buffer, size_t size) {
  buffer[0] = '\0';
#if defined(TORB_FRAMES)
  {
    const torb_frame *frame;
    size_t filled = 0u;
    unsigned shown = 0u;
    unsigned long long more = 0u;
    for (frame = torb_frame_innermost; frame != NULL; frame = frame->caller) {
      const char *separator = filled == 0u ? "" : "\n";
      int written;
      if (shown == TORB_FRAMES_SHOWN) {
        more += 1u;
        continue;
      }
      if (frame != torb_frame_innermost && frame->site.path != NULL) {
        written = snprintf(buffer + filled, size - filled, "%s  in %s, at %s:%u:%u", separator, frame->function,
                           frame->site.path, frame->site.line, frame->site.column);
      } else {
        written = snprintf(buffer + filled, size - filled, "%s  in %s", separator, frame->function);
      }
      if (written < 0 || (size_t)written >= size - filled) {
        /* The line did not fit: it is cut off where it was, and counted with the rest */
        buffer[filled] = '\0';
        shown = TORB_FRAMES_SHOWN;
        more += 1u;
        continue;
      }
      filled += (size_t)written;
      shown += 1u;
    }
    if (more > 0u) {
      (void)snprintf(buffer + filled, size - filled, "\n  ... and %llu frames more", more);
    }
  }
#else
  (void)size;
#endif
}

void torb_end_recovery(torb_recovery *previous) {
  torb_worker_current()->recovery = previous;
}

/**
 * Renders the whole message, hands it to the hook if a test build installed one, and otherwise writes it to stderr
 * and leaves with `code`: 101, or `TORB_OUT_OF_MEMORY_EXIT_CODE`. Does not return either way: if a hook returns
 * anyway, the process still leaves.
 */
static TORB_NORETURN void torb_end_with_panic(const char *message, torb_location at, int code) {
  char buffer[TORB_PANIC_BUFFER_SIZE];
  char frames[TORB_FRAMES_BUFFER_SIZE];
  torb_worker *worker = torb_worker_current();
  /* A debugger stops the program where it panics, with every frame still in place; then the panic goes on */
  if (torb_debug_hook != NULL && code == TORB_PANIC_EXIT_CODE) {
    torb_debug_panic_hook hook = torb_debug_hook;
    torb_debug_hook = NULL;
    hook(message, at);
    torb_debug_hook = hook;
  }
  /*
   * A test runner that set up a recovery point catches the panic instead: the message and the site go into the point
   * and the jump lands in the frame that owns it. The point is taken away first, so a panic *while* a failure is being
   * reported is an ordinary panic and never a jump into a frame that has already been left.
   *
   * Running out of memory is never caught: a recovered panic frees nothing, so the next test would start at the limit.
   */
  if (worker->recovery != NULL && code == TORB_PANIC_EXIT_CODE) {
    torb_recovery *point = worker->recovery;
    worker->recovery = NULL;
    snprintf(point->message, sizeof point->message, "%s", message);
    point->at = at;
    torb_render_frames(point->frames, sizeof point->frames);
#if defined(TORB_FRAMES)
    /* The jump skips the cleanup of every frame between here and the point */
    torb_frame_innermost = point->innermost;
#endif
    longjmp(point->destination, 1);
  }
  torb_render_frames(frames, sizeof frames);
  if (at.path != NULL) {
    snprintf(buffer, sizeof buffer, "panic: %s\n  at %s:%u:%u", message, at.path, at.line, at.column);
  } else {
    snprintf(buffer, sizeof buffer, "panic: %s", message);
  }
  if (frames[0] != '\0') {
    size_t length = strlen(buffer);
    snprintf(buffer + length, sizeof buffer - length, "\n%s", frames);
  }
  if (torb_hook != NULL) {
    torb_hook(buffer);
  }
  /*
   * One panic ends the process, and only one is printed: a second worker that panics at the same time waits here for the
   * first to leave, instead of interleaving its message with the first one's.
   */
  if (torb_atomic_exchange_u32(&torb_panicking, 1u) != 0u) {
    for (;;) {
      torb_platform_sleep(1000000000LL);
    }
  }
  /*
   * Everything the program printed comes first. Standard output is buffered when it is a pipe or a file, standard
   * error is not, so without this the two streams of a program that panics arrive in the wrong order: the panic on
   * top and the output of the lines that ran before it underneath.
   */
  fflush(stdout);
  /* The same path `print` takes: a console sees the message as the text it is, a pipe sees exactly these bytes */
  torb_write_line_error(buffer, strlen(buffer));
  fflush(stderr);
  TORB_EXIT_IMMEDIATELY(code);
}

static TORB_NORETURN void torb_finish_panic(const char *message, torb_location at) {
  torb_end_with_panic(message, at, TORB_PANIC_EXIT_CODE);
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

void torb_panic_negative_exponent(int64_t exponent, torb_location at) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  snprintf(text, sizeof text, "negative exponent in `**`: an integer raised to %lld is not a whole number",
           (long long)exponent);
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

/* Names the memory limit where one is in force (memory.c), and leaves with its own exit code */
void torb_panic_out_of_memory(size_t size) {
  char text[TORB_MESSAGE_BUFFER_SIZE];
  torb_memory_describe_exhaustion(text, sizeof text, size);
  torb_end_with_panic(text, torb_location_unknown, TORB_OUT_OF_MEMORY_EXIT_CODE);
}

void torb_panic_stack_overflow(torb_location at) {
  torb_finish_panic("stack overflow: the recursion is deeper than the stack of the thread", at);
}

/*
 * The check is on from here: the reserve is the process's, and the bottom of the stack is the thread's - read out of
 * the TEB on 64-bit Windows for every thread alike, and set here for the main thread everywhere else (a worker sets
 * its own when it starts, `torb_set_thread_stack_floor`).
 */
void torb_set_stack_limit(void) {
  uintptr_t low = 0u;
  if (torb_platform_stack_low(&low)) {
    torb_set_thread_stack_floor(low);
    torb_stack_reserve = TORB_STACK_RESERVE;
  }
}

void torb_set_thread_stack_floor(uintptr_t floor) {
#if !(defined(_WIN64) && (defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)))
  torb_thread_stack_floor = floor;
#else
  (void)floor;
#endif
}
