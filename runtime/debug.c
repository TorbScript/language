/*
 * debug.c - what the debuggee of `torb debug` needs of C (docs/design/DEBUGGER.md sections 3, 11 and 16), reached
 * through the kernel operations `DebugRead`, `DebugTake`, `DebugPending`, `DebugDirectory`, `DebugVariable` and
 * `DebugContainer` of runtime/machine.c.
 *
 * **The channel.** The adapter writes one request per line to the debuggee's standard input. The lines are read here,
 * into a buffer of this file's own and not through `stdin`: a `FILE *` that read ahead would hide a request that has
 * arrived from `torb_debug_pending`, which asks the pipe itself (`torb_platform_standard_input_ready`) whether a read
 * would wait. The debuggee's answers go the other way on standard output, written by the host's own `print`.
 *
 * **Containers.** A list, a map or a set of the program is looked into by the runtime's own functions, so the debugger
 * needs no knowledge of their storage: its length, and the address of the item, or of the key and the value, at a
 * position.
 */

#include "torb.h"
#include "torb_machine.h"

#include <string.h>

/* The bytes of standard input that arrived and were not taken yet, and whether it has ended. */
static uint8_t *torb_debug_buffer = NULL;
static size_t torb_debug_length = 0u;
static size_t torb_debug_capacity = 0u;
static bool torb_debug_ended = false;

/* Where the first line of the buffer ends (its `\n`), or -1 while it is not whole. */
static int64_t torb_debug_line_end(void) {
  for (size_t index = 0u; index < torb_debug_length; index++) {
    if (torb_debug_buffer[index] == '\n') {
      return (int64_t)index;
    }
  }
  return -1;
}

/* One read of standard input into the buffer, which waits until something arrives; its end is noted. */
static void torb_debug_fill(void) {
  uint8_t chunk[4096];
  const int64_t got = torb_read_standard_bytes(chunk, sizeof chunk);
  if (got <= 0) {
    torb_debug_ended = true;
    return;
  }
  if (torb_debug_length + (size_t)got > torb_debug_capacity) {
    size_t capacity = torb_debug_capacity == 0u ? 8192u : torb_debug_capacity;
    uint8_t *grown;
    while (capacity < torb_debug_length + (size_t)got) {
      capacity *= 2u;
    }
    grown = (uint8_t *)torb_raw_allocate(capacity);
    if (torb_debug_length > 0u) {
      memcpy(grown, torb_debug_buffer, torb_debug_length);
    }
    if (torb_debug_buffer != NULL) {
      torb_raw_free(torb_debug_buffer, torb_debug_capacity);
    }
    torb_debug_buffer = grown;
    torb_debug_capacity = capacity;
  }
  memcpy(torb_debug_buffer + torb_debug_length, chunk, (size_t)got);
  torb_debug_length += (size_t)got;
}

/* The length of a line without its `\r\n` or `\n`. */
static int64_t torb_debug_content_length(int64_t end) {
  if (end > 0 && torb_debug_buffer[end - 1] == '\r') {
    return end - 1;
  }
  return end;
}

int64_t torb_debug_read(void) {
  for (;;) {
    const int64_t end = torb_debug_line_end();
    if (end >= 0) {
      return torb_debug_content_length(end);
    }
    if (torb_debug_ended) {
      return -1;
    }
    torb_debug_fill();
  }
}

int64_t torb_debug_take(int64_t *target) {
  const int64_t end = torb_debug_line_end();
  int64_t length;
  if (end < 0) {
    return 0;
  }
  length = torb_debug_content_length(end);
  for (int64_t index = 0; index < length; index++) {
    target[index] = (int64_t)torb_debug_buffer[index];
  }
  torb_debug_length -= (size_t)end + 1u;
  memmove(torb_debug_buffer, torb_debug_buffer + end + 1, torb_debug_length);
  return length;
}

int64_t torb_debug_pending(void) {
  while (torb_debug_line_end() < 0 && !torb_debug_ended) {
    if (!torb_platform_standard_input_ready()) {
      return 0;
    }
    torb_debug_fill();
  }
  return 1;
}

/* A text as a NUL-terminated copy, owned by the caller: `torb_raw_free(result, length + 1)`. */
static char *torb_debug_terminated(torb_text text) {
  char *copy = (char *)torb_raw_allocate((size_t)text.length + 1u);
  if (text.length > 0u && text.storage != NULL) {
    memcpy(copy, text.storage->data + text.offset, text.length);
  }
  copy[text.length] = '\0';
  return copy;
}

int64_t torb_debug_directory(torb_text path) {
  char *terminated = torb_debug_terminated(path);
  const bool changed = torb_platform_set_working_directory(terminated);
  torb_raw_free(terminated, (size_t)path.length + 1u);
  return changed ? 0 : -1;
}

int64_t torb_debug_variable(torb_text name, torb_text value) {
  char *terminatedName = torb_debug_terminated(name);
  char *terminatedValue = torb_debug_terminated(value);
  const bool set = torb_platform_set_environment_variable(terminatedName, terminatedValue);
  torb_raw_free(terminatedValue, (size_t)value.length + 1u);
  torb_raw_free(terminatedName, (size_t)name.length + 1u);
  return set ? 0 : -1;
}

int64_t torb_debug_container(int64_t kind, const int64_t *source, int64_t index, int64_t *target) {
  if (kind == 0) {
    torb_list list;
    memcpy(&list, source, sizeof list);
    target[0] = list.storage == NULL ? 0 : torb_list_length(list);
    if (index < 0 || index >= target[0]) {
      return 0;
    }
    target[1] = (int64_t)(intptr_t)torb_list_at(list, index, torb_location_unknown);
    return 1;
  }
  {
    torb_map map;
    uint32_t cursor = 0u;
    const void *key = NULL;
    const void *value = NULL;
    memcpy(&map, source, sizeof map);
    target[0] = map.storage == NULL ? 0 : torb_map_length(map);
    if (index < 0 || index >= target[0]) {
      return 0;
    }
    for (int64_t position = 0; position <= index; position++) {
      const bool found = kind == 1 ? torb_map_next(map, &cursor, &key, &value) : torb_set_next(map, &cursor, &key);
      if (!found) {
        return 0;
      }
    }
    target[1] = (int64_t)(intptr_t)key;
    target[2] = (int64_t)(intptr_t)value;
    return 1;
  }
}
