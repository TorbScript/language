/*
 * stream.c - the byte streams of a file, of the three standard streams and of a child process (docs/design/STREAMS.md
 * section 14): what `File.chunks`, `File.add` and `File.end`, `standardInput`, `standardOutput` and `standardError`,
 * `Process.start` and the members of `Child` are TorbScript over, in std/fs, std/io and std/process.
 *
 * Every operation that may wait is a task of the runtime that turns to the blocking pool first (`torb_blocking_turn`,
 * docs/design/CONCURRENCY.md section 16) and does the one call of the operating system there, so the worker it was
 * started on goes on with its other tasks. The poller of runtime/io.c takes sockets; a regular file is always ready to
 * every poller there is, a console is no handle a poller takes on Windows, and the pipes of a child are read the same
 * way on every system, which keeps the three kinds one code path. A task answers an `Int64`: the bytes a read got (0
 * at the end), the bytes a write wrote, the exit code a wait got - or a negative failure, which
 * `torb_stream_failure_text` renders.
 *
 * What a read got waits in a buffer of its stream until the reader takes it into a list (`..._take_read`), because a
 * task answers one number and the bytes belong to whoever reads. A buffer is `malloc`'s and never a block of the
 * runtime's heap: it is filled on a thread of the blocking pool and emptied on the reader's worker, and neither is a
 * value of the program.
 */

#include "torb.h"
#include "torb_natives.h"
#include "torb_pool.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------------------------- the buffers --- */

/* What a read got and nobody took yet. */
typedef struct torb_stream_pending {
  uint8_t *data;
  size_t length;
  size_t capacity;
} torb_stream_pending;

static void torb_stream_keep(torb_stream_pending *pending, const uint8_t *bytes, size_t length) {
  if (pending->length + length > pending->capacity) {
    size_t capacity = pending->capacity == 0u ? length : pending->capacity * 2u;
    uint8_t *grown;
    while (capacity < pending->length + length) {
      capacity *= 2u;
    }
    grown = (uint8_t *)realloc(pending->data, capacity);
    if (grown == NULL) {
      torb_panic_out_of_memory(capacity);
    }
    pending->data = grown;
    pending->capacity = capacity;
  }
  memcpy(pending->data + pending->length, bytes, length);
  pending->length += length;
}

/* Everything a read kept, appended to a list of bytes, and the buffer empty again. */
static void torb_stream_take(torb_stream_pending *pending, torb_list *into) {
  if (pending->length > 0u) {
    torb_list_add_plain(into, pending->data, pending->length);
  }
  pending->length = 0u;
}

static void torb_stream_forget(torb_stream_pending *pending) {
  free(pending->data);
  pending->data = NULL;
  pending->length = 0u;
  pending->capacity = 0u;
}

/* -------------------------------------------------------------------------------------------- the failures --- */

/*
 * A failure is negative: `-errno` of the C library, or a code of the platform with `TORB_STREAM_PLATFORM` added, which
 * `torb_stream_failure_text` renders in the platform's own words.
 */
#define TORB_STREAM_PLATFORM ((int64_t)1 << 40)

torb_text torb_stream_failure_text(int64_t failure) {
  char buffer[512];
  int64_t code = -failure;
  if (code >= TORB_STREAM_PLATFORM) {
    return torb_platform_failure_text(code - TORB_STREAM_PLATFORM);
  }
  snprintf(buffer, sizeof buffer, "%s", strerror((int)code));
  return torb_text_from_cstring(buffer);
}

static int64_t torb_stream_errno(void) {
  return errno == 0 ? -(int64_t)EIO : -(int64_t)errno;
}

/* ------------------------------------------------------------------------------------------------ the children --- */

/*
 * A child process started with three pipes. The table hands its handle to the program, an index plus one, and a
 * record lives while the program holds the handle or an operation still uses it: `users` counts both, so closing
 * the handle while a read still runs on the blocking pool frees nothing that read still reads.
 */
typedef struct torb_child_record {
  void *process;
  void *input;
  void *output;
  void *errors;
  torb_stream_pending pending_output;
  torb_stream_pending pending_errors;
  uint32_t users;
  uint8_t input_ended;
} torb_child_record;

static torb_mutex torb_child_lock = TORB_MUTEX_INITIALIZER;
static torb_child_record **torb_child_table = NULL;
static size_t torb_child_capacity = 0u;

static int64_t torb_child_register(torb_child_record *record) {
  size_t index;
  torb_mutex_lock(&torb_child_lock);
  for (index = 0u; index < torb_child_capacity; index += 1u) {
    if (torb_child_table[index] == NULL) {
      break;
    }
  }
  if (index == torb_child_capacity) {
    size_t capacity = torb_child_capacity == 0u ? 8u : torb_child_capacity * 2u;
    torb_child_record **grown = (torb_child_record **)realloc(torb_child_table, capacity * sizeof *grown);
    if (grown == NULL) {
      torb_mutex_unlock(&torb_child_lock);
      torb_panic_out_of_memory(capacity * sizeof *grown);
    }
    memset(grown + torb_child_capacity, 0, (capacity - torb_child_capacity) * sizeof *grown);
    torb_child_table = grown;
    torb_child_capacity = capacity;
  }
  torb_child_table[index] = record;
  torb_mutex_unlock(&torb_child_lock);
  return (int64_t)index + 1;
}

/* The record of a handle, with one more user; `NULL` for a handle that is closed or never was. */
static torb_child_record *torb_child_use(int64_t handle) {
  torb_child_record *record = NULL;
  torb_mutex_lock(&torb_child_lock);
  if (handle > 0 && (size_t)handle <= torb_child_capacity) {
    record = torb_child_table[handle - 1];
    if (record != NULL) {
      record->users += 1u;
    }
  }
  torb_mutex_unlock(&torb_child_lock);
  return record;
}

/* One user less; the last one closes what is left and frees the record. */
static void torb_child_done(torb_child_record *record) {
  bool last;
  torb_mutex_lock(&torb_child_lock);
  record->users -= 1u;
  last = record->users == 0u;
  torb_mutex_unlock(&torb_child_lock);
  if (!last) {
    return;
  }
  if (record->input != NULL) {
    torb_platform_pipe_close(record->input);
  }
  if (record->output != NULL) {
    torb_platform_pipe_close(record->output);
  }
  if (record->errors != NULL) {
    torb_platform_pipe_close(record->errors);
  }
  if (record->process != NULL) {
    torb_platform_child_forget(record->process);
  }
  torb_stream_forget(&record->pending_output);
  torb_stream_forget(&record->pending_errors);
  free(record);
}

/* ------------------------------------------------------------------------------------ the tasks that block --- */

typedef enum torb_stream_kind {
  TORB_STREAM_FILE_READ = 0,
  TORB_STREAM_FILE_WRITE = 1,
  TORB_STREAM_FILE_FLUSH = 2,
  TORB_STREAM_STANDARD_READ = 3,
  TORB_STREAM_STANDARD_WRITE = 4,
  TORB_STREAM_CHILD_READ = 5,
  TORB_STREAM_CHILD_WRITE = 6,
  TORB_STREAM_CHILD_WAIT = 7
} torb_stream_kind;

typedef struct torb_stream_frame {
  int32_t kind;
  /* 1 standard output or a child's output, 2 standard error or a child's errors */
  int32_t which;
  /* The `FILE *` of a file, or the record of a child (one user of it) */
  void *target;
  /* Where a read's bytes go */
  torb_stream_pending *pending;
  /* A write's own copy of its bytes, `malloc`'s */
  uint8_t *bytes;
  size_t length;
  size_t maximum;
  /* The turn to the blocking pool, while the task waits for it */
  torb_task *turn;
  /* Whether the task was started by a program of the VM, whose blocks the kernel counts apart (below) */
  uint32_t counted;
} torb_stream_frame;

/* What standard input's reads got. The one reader of standard input reads it. */
static torb_stream_pending torb_standard_pending = { NULL, 0u, 0u };

static void torb_stream_free_frame(torb_stream_frame *frame) {
  free(frame->bytes);
  frame->bytes = NULL;
  if (frame->kind >= (int32_t)TORB_STREAM_CHILD_READ && frame->target != NULL) {
    torb_child_done((torb_child_record *)frame->target);
    frame->target = NULL;
  }
}

/* The one call of the operating system, on a thread of the blocking pool. */
static int64_t torb_stream_perform(torb_stream_frame *frame) {
  switch ((torb_stream_kind)frame->kind) {
    case TORB_STREAM_FILE_READ: {
      uint8_t *buffer = (uint8_t *)malloc(frame->maximum == 0u ? 1u : frame->maximum);
      size_t read;
      if (buffer == NULL) {
        torb_panic_out_of_memory(frame->maximum);
      }
      errno = 0;
      read = fread(buffer, 1u, frame->maximum, (FILE *)frame->target);
      if (read == 0u && ferror((FILE *)frame->target)) {
        int64_t failure = torb_stream_errno();
        free(buffer);
        return failure;
      }
      torb_stream_keep(frame->pending, buffer, read);
      free(buffer);
      return (int64_t)read;
    }
    case TORB_STREAM_FILE_WRITE: {
      size_t written;
      errno = 0;
      written = fwrite(frame->bytes, 1u, frame->length, (FILE *)frame->target);
      if (written < frame->length) {
        return torb_stream_errno();
      }
      return (int64_t)written;
    }
    case TORB_STREAM_FILE_FLUSH:
      errno = 0;
      return fflush((FILE *)frame->target) == 0 ? 0 : torb_stream_errno();
    case TORB_STREAM_STANDARD_READ: {
      uint8_t *buffer = (uint8_t *)malloc(frame->maximum == 0u ? 1u : frame->maximum);
      int64_t read;
      if (buffer == NULL) {
        torb_panic_out_of_memory(frame->maximum);
      }
      read = torb_read_standard_bytes(buffer, frame->maximum);
      if (read > 0) {
        torb_stream_keep(frame->pending, buffer, (size_t)read);
      }
      free(buffer);
      return read < 0 ? read - TORB_STREAM_PLATFORM : read;
    }
    case TORB_STREAM_STANDARD_WRITE:
      return torb_write_standard_bytes(frame->which == 2, frame->bytes, frame->length) ? (int64_t)frame->length
                                                                                       : -(int64_t)EIO;
    case TORB_STREAM_CHILD_READ: {
      torb_child_record *record = (torb_child_record *)frame->target;
      void *pipe = frame->which == 2 ? record->errors : record->output;
      uint8_t *buffer = (uint8_t *)malloc(frame->maximum == 0u ? 1u : frame->maximum);
      int64_t read;
      if (buffer == NULL) {
        torb_panic_out_of_memory(frame->maximum);
      }
      read = pipe == NULL ? 0 : torb_platform_pipe_read(pipe, buffer, frame->maximum);
      if (read > 0) {
        torb_stream_keep(frame->which == 2 ? &record->pending_errors : &record->pending_output, buffer, (size_t)read);
      }
      free(buffer);
      return read < 0 ? read - TORB_STREAM_PLATFORM : read;
    }
    case TORB_STREAM_CHILD_WRITE: {
      torb_child_record *record = (torb_child_record *)frame->target;
      int64_t written;
      if (record->input == NULL || record->input_ended != 0u) {
        return -(int64_t)EPIPE;
      }
      written = torb_platform_pipe_write(record->input, frame->bytes, frame->length);
      return written < 0 ? written - TORB_STREAM_PLATFORM : written;
    }
    case TORB_STREAM_CHILD_WAIT: {
      torb_child_record *record = (torb_child_record *)frame->target;
      int64_t code = 0;
      int64_t failure = 0;
      if (!torb_platform_child_wait(record->process, &code, &failure)) {
        return -failure - TORB_STREAM_PLATFORM;
      }
      return code;
    }
  }
  return -(int64_t)EINVAL;
}

/*
 * The machine of every such task: the turn to the blocking pool, the operation there, the answer. A task cancelled
 * before its operation ran stops without it; one whose operation runs is not interrupted, and finishes it.
 */
static torb_poll torb_stream_resume(torb_task *task) {
  torb_stream_frame *frame = (torb_stream_frame *)torb_task_frame(task);
  /*
   * A task of a program of the VM counts as the program's on whatever thread runs it, as the VM's own tasks do
   * (runtime/machine.c, `torb_machine_resume`): the thread of the pool that resumes it is the program's from here on,
   * so the turn it allocates, the task block its scheduler frees and the result it writes are the program's blocks.
   */
  if (frame->counted != 0u) {
    (void)torb_count_in_machine(1u);
  }
  if (task->state == 0u) {
    if (torb_task_cancelled(task)) {
      torb_stream_free_frame(frame);
      return TORB_POLL_STOPPED;
    }
    frame->turn = torb_blocking_turn();
    task->state = 1u;
    if (torb_task_await(task, frame->turn) == TORB_WAIT_SUSPENDED) {
      return TORB_POLL_SUSPENDED;
    }
  }
  torb_task_release(frame->turn);
  frame->turn = NULL;
  if (torb_task_cancelled(task)) {
    torb_stream_free_frame(frame);
    return TORB_POLL_STOPPED;
  }
  (void)torb_task_outcome(task);
  *(int64_t *)torb_task_result_slot(task) = torb_stream_perform(frame);
  torb_stream_free_frame(frame);
  return TORB_POLL_FINISHED;
}

/*
 * A task over a frame, started portable: its frame holds nothing counted - a handle of the platform, a buffer of
 * `malloc`'s - so it may turn to the blocking pool, and an idle worker may take it before it first runs.
 */
static torb_task *torb_stream_task(const torb_stream_frame *made) {
  torb_task *task = torb_task_new(torb_stream_resume, sizeof(torb_stream_frame), &torb_element_int64);
  unsigned counted = torb_count_in_machine(0u);
  (void)torb_count_in_machine(counted);
  memcpy(torb_task_frame(task), made, sizeof *made);
  ((torb_stream_frame *)torb_task_frame(task))->counted = counted;
  torb_task_start_portable(task);
  return task;
}

/* A task that answers `answer` without an operation: a closed file, a child whose handle is gone. */
static torb_poll torb_stream_answered_resume(torb_task *task) {
  *(int64_t *)torb_task_result_slot(task) = *(const int64_t *)torb_task_frame(task);
  return TORB_POLL_FINISHED;
}

static torb_task *torb_stream_answered(int64_t answer) {
  torb_task *task = torb_task_new(torb_stream_answered_resume, sizeof(int64_t), &torb_element_int64);
  *(int64_t *)torb_task_frame(task) = answer;
  torb_task_start(task);
  return task;
}

/* A write's own copy of the bytes of `bytes` from `from` on, so the task never reads a block of the program. */
static bool torb_stream_copy(torb_stream_frame *frame, torb_list bytes, int64_t from) {
  int64_t length = torb_list_length(bytes);
  if (from < 0 || from > length || (length > 0 && torb_list_element(bytes)->size != 1u)) {
    return false;
  }
  frame->length = (size_t)(length - from);
  frame->bytes = (uint8_t *)malloc(frame->length == 0u ? 1u : frame->length);
  if (frame->bytes == NULL) {
    torb_panic_out_of_memory(frame->length);
  }
  if (frame->length > 0u) {
    memcpy(frame->bytes, torb_list_at(bytes, from, torb_location_unknown), frame->length);
  }
  return true;
}

/* ------------------------------------------------------------------------------------------------- files --- */

/* The reads of a file are kept beside it, in the one reader's buffer (torb.h, `torb_file.pending`), made at the first. */
static torb_stream_pending *torb_file_pending(torb_file *file) {
  if (file->pending == NULL) {
    file->pending = calloc(1u, sizeof(torb_stream_pending));
    if (file->pending == NULL) {
      torb_panic_out_of_memory(sizeof(torb_stream_pending));
    }
  }
  return (torb_stream_pending *)file->pending;
}

void torb_file_forget_pending(torb_file *file) {
  if (file->pending != NULL) {
    torb_stream_forget((torb_stream_pending *)file->pending);
    free(file->pending);
    file->pending = NULL;
  }
}

torb_task *torb_file_read(torb_file *file, int64_t maximum) {
  torb_stream_frame frame;
  if (file->handle == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_FILE_READ;
  frame.target = file->handle;
  frame.pending = torb_file_pending(file);
  frame.maximum = maximum <= 0 ? 65536u : (size_t)maximum;
  return torb_stream_task(&frame);
}

void torb_file_take_read(torb_file *file, torb_list *into) {
  if (file->pending != NULL) {
    torb_stream_take((torb_stream_pending *)file->pending, into);
  }
}

torb_task *torb_file_write(torb_file *file, torb_list bytes, int64_t from) {
  torb_stream_frame frame;
  if (file->handle == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_FILE_WRITE;
  frame.target = file->handle;
  if (!torb_stream_copy(&frame, bytes, from)) {
    return torb_stream_answered(-(int64_t)EINVAL);
  }
  return torb_stream_task(&frame);
}

torb_task *torb_file_flush(torb_file *file) {
  torb_stream_frame frame;
  if (file->handle == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_FILE_FLUSH;
  frame.target = file->handle;
  return torb_stream_task(&frame);
}

/* --------------------------------------------------------------------------------------- standard streams --- */

torb_task *torb_standard_read(int64_t maximum) {
  torb_stream_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_STANDARD_READ;
  frame.pending = &torb_standard_pending;
  frame.maximum = maximum <= 0 ? 65536u : (size_t)maximum;
  return torb_stream_task(&frame);
}

void torb_standard_take_read(torb_list *into) {
  torb_stream_take(&torb_standard_pending, into);
}

torb_task *torb_standard_write(int64_t stream, torb_list bytes, int64_t from) {
  torb_stream_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_STANDARD_WRITE;
  frame.which = stream == 2 ? 2 : 1;
  if (!torb_stream_copy(&frame, bytes, from)) {
    return torb_stream_answered(-(int64_t)EINVAL);
  }
  return torb_stream_task(&frame);
}

/* ------------------------------------------------------------------------------------------ child processes --- */

int64_t torb_child_start(torb_text command, torb_list arguments, torb_text *failure) {
  const int64_t count = torb_list_length(arguments);
  torb_child_record record;
  torb_child_record *kept;
  const char *message = NULL;
  char *name = (char *)malloc((size_t)command.length + 1u);
  char **given = (char **)calloc(count == 0 ? 1u : (size_t)count, sizeof(char *));
  int64_t index;
  bool started;
  if (name == NULL || given == NULL) {
    torb_panic_out_of_memory((size_t)command.length + 1u);
  }
  if (command.length > 0u) {
    memcpy(name, command.storage->data + command.offset, command.length);
  }
  name[command.length] = '\0';
  for (index = 0; index < count; index += 1) {
    torb_text argument = { NULL, 0u, 0u };
    (void)torb_list_get(arguments, index, &argument);
    given[index] = (char *)malloc((size_t)argument.length + 1u);
    if (given[index] == NULL) {
      torb_panic_out_of_memory((size_t)argument.length + 1u);
    }
    if (argument.length > 0u) {
      memcpy(given[index], argument.storage->data + argument.offset, argument.length);
    }
    given[index][argument.length] = '\0';
    torb_text_release(argument);
  }
  memset(&record, 0, sizeof record);
  fflush(NULL);
  started = torb_platform_child_start(name, (const char **)given, (size_t)count, &record.process, &record.input,
                                      &record.output, &record.errors, &message);
  for (index = 0; index < count; index += 1) {
    free(given[index]);
  }
  free(given);
  free(name);
  if (!started) {
    *failure = torb_text_from_cstring(message == NULL ? "the program could not be started" : message);
    return -1;
  }
  kept = (torb_child_record *)malloc(sizeof *kept);
  if (kept == NULL) {
    torb_panic_out_of_memory(sizeof *kept);
  }
  *kept = record;
  kept->users = 1u;
  *failure = torb_text_empty();
  return torb_child_register(kept);
}

torb_task *torb_child_read(int64_t child, int64_t which, int64_t maximum) {
  torb_stream_frame frame;
  torb_child_record *record = torb_child_use(child);
  if (record == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_CHILD_READ;
  frame.which = which == 2 ? 2 : 1;
  frame.target = record;
  frame.maximum = maximum <= 0 ? 65536u : (size_t)maximum;
  return torb_stream_task(&frame);
}

void torb_child_take_read(int64_t child, int64_t which, torb_list *into) {
  torb_child_record *record = torb_child_use(child);
  if (record == NULL) {
    return;
  }
  torb_stream_take(which == 2 ? &record->pending_errors : &record->pending_output, into);
  torb_child_done(record);
}

torb_task *torb_child_write(int64_t child, torb_list bytes, int64_t from) {
  torb_stream_frame frame;
  torb_child_record *record = torb_child_use(child);
  if (record == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_CHILD_WRITE;
  frame.target = record;
  if (!torb_stream_copy(&frame, bytes, from)) {
    torb_child_done(record);
    return torb_stream_answered(-(int64_t)EINVAL);
  }
  return torb_stream_task(&frame);
}

int64_t torb_child_end_input(int64_t child) {
  torb_child_record *record = torb_child_use(child);
  void *input;
  if (record == NULL) {
    return -(int64_t)EBADF;
  }
  torb_mutex_lock(&torb_child_lock);
  input = record->input_ended == 0u ? record->input : NULL;
  record->input_ended = 1u;
  record->input = NULL;
  torb_mutex_unlock(&torb_child_lock);
  if (input != NULL) {
    torb_platform_pipe_close(input);
  }
  torb_child_done(record);
  return 0;
}

torb_task *torb_child_wait(int64_t child) {
  torb_stream_frame frame;
  torb_child_record *record = torb_child_use(child);
  if (record == NULL) {
    return torb_stream_answered(-(int64_t)EBADF);
  }
  memset(&frame, 0, sizeof frame);
  frame.kind = TORB_STREAM_CHILD_WAIT;
  frame.target = record;
  return torb_stream_task(&frame);
}

void torb_child_close(int64_t child) {
  torb_child_record *record = NULL;
  torb_mutex_lock(&torb_child_lock);
  if (child > 0 && (size_t)child <= torb_child_capacity) {
    record = torb_child_table[child - 1];
    torb_child_table[child - 1] = NULL;
  }
  torb_mutex_unlock(&torb_child_lock);
  if (record != NULL) {
    torb_child_done(record);
  }
}
