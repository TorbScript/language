/*
 * console.c - `print`, `printError` and `readLine`.
 *
 * `print(...values: Show)` joins the shown parts with one space and appends one `\n`. The join lives here, so the two
 * back ends cannot disagree about it and the conformance suite compares one format.
 *
 * The bytes are written as they are. A `String` is always valid UTF-8, so no encoding happens on the way out - on
 * Windows the console code page decides how it looks, which is the terminal's business, not the runtime's.
 */

#include "torb.h"

#include <stdio.h>
#include <string.h>

static void torb_write_parts(FILE *stream, const torb_text *parts, size_t count) {
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

void torb_print(torb_text text) {
  torb_write_parts(stdout, &text, 1u);
}

void torb_print_error(torb_text text) {
  torb_write_parts(stderr, &text, 1u);
  fflush(stderr);
}

void torb_print_parts(const torb_text *parts, size_t count) {
  torb_write_parts(stdout, parts, count);
}

void torb_print_error_parts(const torb_text *parts, size_t count) {
  torb_write_parts(stderr, parts, count);
  fflush(stderr);
}

bool torb_read_line(torb_text *out) {
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
      uint8_t *grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
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
