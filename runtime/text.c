/*
 * text.c - `String` and `Char`: UTF-8, slices, concatenation, comparison, hashing, and `Show` for the primitives.
 *
 * A `String` is a slice of one counted `torb_bytes`: `offset` and `length`, so `text[3..]` is O(1) and shares the
 * storage, `byteLength()` is a field read, and a literal is an immortal `torb_bytes` in read-only data. There is no
 * small-string optimization in v1.
 *
 * A `String` is **always valid UTF-8** (decided gap 7). Every offset is checked: past the end, reversed, or on a
 * continuation byte each panic with the offset and the length in the message. The only ways in are literals, slices at
 * character boundaries, `String.from(Iterable<Char>)` and the functions here, which validate.
 *
 * A text is at most 4 GiB: `offset` and `length` are `uint32_t`, which keeps `torb_text` at 16 bytes.
 */

#include "torb.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------- storage --- */

static const struct {
  torb_header header;
  uint32_t capacity;
  uint8_t data[1];
} torb_empty_storage = { TORB_IMMORTAL_HEADER(TORB_BLOCK_BYTES), 0u, { 0u } };

static torb_bytes *torb_storage_of(const void *block) {
  /* The emitter's literals live in read-only data; nothing ever writes through this pointer. */
  return (torb_bytes *)(void *)(uintptr_t)block;
}

static void torb_check_text_length(size_t length) {
  if (length > (size_t)UINT32_MAX) {
    torb_panic_text("a text longer than 4 GiB is not supported", torb_location_unknown);
  }
}

torb_text torb_text_from_storage(const void *storage, uint32_t offset, uint32_t length) {
  torb_text text;
  text.storage = torb_storage_of(storage);
  text.offset = offset;
  text.length = length;
  return text;
}

torb_text torb_text_empty(void) {
  return torb_text_from_storage(&torb_empty_storage, 0u, 0u);
}

torb_text torb_text_retained(torb_text text) {
  torb_retain(text.storage);
  return text;
}

void torb_text_release(torb_text text) {
  torb_release(text.storage, NULL);
}

torb_text torb_text_allocate(uint32_t length, uint8_t **data_out) {
  torb_text text;
  torb_bytes *storage;
  if (length == 0) {
    static uint8_t nothing[1] = { 0u };
    *data_out = nothing;
    return torb_text_empty();
  }
  storage = (torb_bytes *)torb_allocate(sizeof(torb_bytes) + (size_t)length, TORB_BLOCK_BYTES);
  storage->capacity = length;
  text.storage = storage;
  text.offset = 0u;
  text.length = length;
  *data_out = storage->data;
  return text;
}

static const uint8_t *torb_text_data(torb_text text) {
  if (text.storage == NULL) {
    return (const uint8_t *)"";
  }
  return text.storage->data + text.offset;
}

int64_t torb_text_byte_length(torb_text text) {
  return (int64_t)text.length;
}

bool torb_text_is_empty(torb_text text) {
  return text.length == 0u;
}

/* ---------------------------------------------------------------------------------------------------- UTF-8 --- */

bool torb_utf8_is_continuation(uint8_t byte) {
  return (byte & 0xC0u) == 0x80u;
}

uint32_t torb_char_byte_length(torb_char character) {
  if (character > 0x10FFFFu || (character >= 0xD800u && character <= 0xDFFFu)) {
    return 0u;
  }
  if (character < 0x80u) {
    return 1u;
  }
  if (character < 0x800u) {
    return 2u;
  }
  if (character < 0x10000u) {
    return 3u;
  }
  return 4u;
}

uint32_t torb_utf8_encode(torb_char character, uint8_t out[4]) {
  uint32_t width = torb_char_byte_length(character);
  switch (width) {
    case 1u:
      out[0] = (uint8_t)character;
      break;
    case 2u:
      out[0] = (uint8_t)(0xC0u | (character >> 6));
      out[1] = (uint8_t)(0x80u | (character & 0x3Fu));
      break;
    case 3u:
      out[0] = (uint8_t)(0xE0u | (character >> 12));
      out[1] = (uint8_t)(0x80u | ((character >> 6) & 0x3Fu));
      out[2] = (uint8_t)(0x80u | (character & 0x3Fu));
      break;
    case 4u:
      out[0] = (uint8_t)(0xF0u | (character >> 18));
      out[1] = (uint8_t)(0x80u | ((character >> 12) & 0x3Fu));
      out[2] = (uint8_t)(0x80u | ((character >> 6) & 0x3Fu));
      out[3] = (uint8_t)(0x80u | (character & 0x3Fu));
      break;
    default:
      break;
  }
  return width;
}

/**
 * Decodes one character. Answers its byte width, or zero when the bytes at `position` are not a valid, shortest-form,
 * non-surrogate UTF-8 sequence.
 */
static uint32_t torb_utf8_decode(const uint8_t *bytes, size_t length, size_t position, torb_char *character) {
  uint8_t first;
  uint32_t value;
  uint32_t width;
  uint32_t index;
  if (position >= length) {
    return 0u;
  }
  first = bytes[position];
  if (first < 0x80u) {
    *character = first;
    return 1u;
  }
  if ((first & 0xE0u) == 0xC0u) {
    width = 2u;
    value = (uint32_t)(first & 0x1Fu);
  } else if ((first & 0xF0u) == 0xE0u) {
    width = 3u;
    value = (uint32_t)(first & 0x0Fu);
  } else if ((first & 0xF8u) == 0xF0u) {
    width = 4u;
    value = (uint32_t)(first & 0x07u);
  } else {
    return 0u;
  }
  if (position + width > length) {
    return 0u;
  }
  for (index = 1u; index < width; index += 1u) {
    uint8_t byte = bytes[position + index];
    if (!torb_utf8_is_continuation(byte)) {
      return 0u;
    }
    value = (value << 6) | (uint32_t)(byte & 0x3Fu);
  }
  if (torb_char_byte_length(value) != width) {
    /* An overlong form, a surrogate, or a value above 0x10FFFF. */
    return 0u;
  }
  *character = value;
  return width;
}

bool torb_utf8_validate(const uint8_t *bytes, size_t length, size_t *bad_offset) {
  size_t position = 0u;
  while (position < length) {
    torb_char character = 0u;
    uint32_t width = torb_utf8_decode(bytes, length, position, &character);
    if (width == 0u) {
      *bad_offset = position;
      return false;
    }
    position += width;
  }
  return true;
}

bool torb_text_next_char(torb_text text, uint32_t *offset, torb_char *character) {
  const uint8_t *bytes = torb_text_data(text);
  uint32_t width;
  if (*offset >= text.length) {
    return false;
  }
  width = torb_utf8_decode(bytes, (size_t)text.length, (size_t)*offset, character);
  if (width == 0u) {
    /* Unreachable: a `String` is always valid UTF-8. Reported rather than silently skipped. */
    torb_panic_invalid_utf8((int64_t)*offset, torb_location_unknown);
  }
  *offset += width;
  return true;
}

torb_text torb_text_from_bytes(const uint8_t *bytes, size_t length, torb_location at) {
  size_t bad_offset = 0u;
  torb_text text;
  uint8_t *data;
  torb_check_text_length(length);
  if (!torb_utf8_validate(bytes, length, &bad_offset)) {
    torb_panic_invalid_utf8((int64_t)bad_offset, at);
  }
  text = torb_text_allocate((uint32_t)length, &data);
  if (length > 0u) {
    memcpy(data, bytes, length);
  }
  return text;
}

bool torb_text_try_from_bytes(const uint8_t *bytes, size_t length, torb_text *out, size_t *bad_offset) {
  uint8_t *data;
  torb_check_text_length(length);
  if (!torb_utf8_validate(bytes, length, bad_offset)) {
    return false;
  }
  *out = torb_text_allocate((uint32_t)length, &data);
  if (length > 0u) {
    memcpy(data, bytes, length);
  }
  return true;
}

torb_text torb_text_from_cstring(const char *text) {
  return torb_text_from_bytes((const uint8_t *)text, strlen(text), torb_location_unknown);
}

/* ------------------------------------------------------------------------------- building, slicing, comparing --- */

torb_text torb_text_concat(const torb_text *parts, size_t count) {
  size_t total = 0u;
  size_t index;
  uint8_t *data;
  torb_text result;
  for (index = 0u; index < count; index += 1u) {
    total += (size_t)parts[index].length;
  }
  torb_check_text_length(total);
  if (total == 0u) {
    return torb_text_empty();
  }
  result = torb_text_allocate((uint32_t)total, &data);
  for (index = 0u; index < count; index += 1u) {
    uint32_t length = parts[index].length;
    if (length > 0u) {
      memcpy(data, torb_text_data(parts[index]), (size_t)length);
      data += length;
    }
  }
  return result;
}

torb_text torb_text_add(torb_text first, torb_text second) {
  torb_text parts[2];
  parts[0] = first;
  parts[1] = second;
  return torb_text_concat(parts, 2u);
}

torb_text torb_text_slice(torb_text text, int64_t from, int64_t to, torb_location at) {
  const uint8_t *bytes = torb_text_data(text);
  torb_text result;
  if (from < 0) {
    torb_panic_offset_past_end(from, (int64_t)text.length, at);
  }
  if (to < 0 || to > (int64_t)text.length) {
    torb_panic_offset_past_end(to, (int64_t)text.length, at);
  }
  if (from > to) {
    torb_panic_range_reversed(from, to, at);
  }
  if (from < (int64_t)text.length && torb_utf8_is_continuation(bytes[from])) {
    torb_panic_offset_inside_character(from, (int64_t)text.length, at);
  }
  if (to < (int64_t)text.length && torb_utf8_is_continuation(bytes[to])) {
    torb_panic_offset_inside_character(to, (int64_t)text.length, at);
  }
  result.storage = text.storage;
  result.offset = text.offset + (uint32_t)from;
  result.length = (uint32_t)(to - from);
  torb_retain(result.storage);
  return result;
}

torb_text torb_text_compact(torb_text text) {
  uint8_t *data;
  torb_text result = torb_text_allocate(text.length, &data);
  if (text.length > 0u) {
    memcpy(data, torb_text_data(text), (size_t)text.length);
  }
  return result;
}

bool torb_text_equal(torb_text first, torb_text second) {
  if (first.length != second.length) {
    return false;
  }
  if (first.length == 0u) {
    return true;
  }
  return memcmp(torb_text_data(first), torb_text_data(second), (size_t)first.length) == 0;
}

int32_t torb_text_compare(torb_text first, torb_text second) {
  uint32_t shared = first.length < second.length ? first.length : second.length;
  int comparison = 0;
  if (shared > 0u) {
    comparison = memcmp(torb_text_data(first), torb_text_data(second), (size_t)shared);
  }
  if (comparison < 0) {
    return -1;
  }
  if (comparison > 0) {
    return 1;
  }
  if (first.length < second.length) {
    return -1;
  }
  if (first.length > second.length) {
    return 1;
  }
  return 0;
}

uint64_t torb_text_hash(torb_text text) {
  return torb_hash_bytes(torb_text_data(text), (size_t)text.length);
}

/** The byte offset of `part` in `text` at or after `start`, or -1. */
static int64_t torb_find(torb_text text, torb_text part, uint32_t start) {
  const uint8_t *bytes = torb_text_data(text);
  const uint8_t *needle = torb_text_data(part);
  uint32_t position;
  if (part.length == 0u) {
    return start <= text.length ? (int64_t)start : -1;
  }
  if (part.length > text.length) {
    return -1;
  }
  for (position = start; position + part.length <= text.length; position += 1u) {
    if (memcmp(bytes + position, needle, (size_t)part.length) == 0) {
      return (int64_t)position;
    }
  }
  return -1;
}

bool torb_text_contains(torb_text text, torb_text part) {
  return torb_find(text, part, 0u) >= 0;
}

bool torb_text_starts_with(torb_text text, torb_text prefix) {
  if (prefix.length > text.length) {
    return false;
  }
  return memcmp(torb_text_data(text), torb_text_data(prefix), (size_t)prefix.length) == 0;
}

bool torb_text_ends_with(torb_text text, torb_text suffix) {
  if (suffix.length > text.length) {
    return false;
  }
  return memcmp(torb_text_data(text) + (text.length - suffix.length), torb_text_data(suffix),
                (size_t)suffix.length) == 0;
}

bool torb_text_index_of(torb_text text, torb_text part, int64_t *out) {
  int64_t found = torb_find(text, part, 0u);
  if (found < 0) {
    return false;
  }
  *out = found;
  return true;
}

/* ------------------------------------------------------------------------------------------- transformations --- */

/**
 * Case mapping and character classification are ASCII plus the letters of Latin-1 for now. Full Unicode tables are
 * milestone 8; the compiler's own identifiers and keywords are ASCII, and `runtime/README.md` records the gap.
 */
static bool torb_is_ascii_space(uint8_t byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
}

torb_text torb_text_trim(torb_text text) {
  const uint8_t *bytes = torb_text_data(text);
  uint32_t start = 0u;
  uint32_t end = text.length;
  torb_text result;
  while (start < end && torb_is_ascii_space(bytes[start])) {
    start += 1u;
  }
  while (end > start && torb_is_ascii_space(bytes[end - 1u])) {
    end -= 1u;
  }
  result.storage = text.storage;
  result.offset = text.offset + start;
  result.length = end - start;
  torb_retain(result.storage);
  return result;
}

static torb_text torb_text_mapped_case(torb_text text, bool upper) {
  uint8_t *data;
  torb_text result = torb_text_allocate(text.length, &data);
  const uint8_t *bytes = torb_text_data(text);
  uint32_t index;
  for (index = 0u; index < text.length; index += 1u) {
    uint8_t byte = bytes[index];
    if (upper && byte >= 'a' && byte <= 'z') {
      data[index] = (uint8_t)(byte - ('a' - 'A'));
    } else if (!upper && byte >= 'A' && byte <= 'Z') {
      data[index] = (uint8_t)(byte + ('a' - 'A'));
    } else {
      data[index] = byte;
    }
  }
  return result;
}

torb_text torb_text_to_upper_case(torb_text text) {
  return torb_text_mapped_case(text, true);
}

torb_text torb_text_to_lower_case(torb_text text) {
  return torb_text_mapped_case(text, false);
}

torb_text torb_text_replace(torb_text text, torb_text part, torb_text replacement) {
  uint32_t position = 0u;
  size_t total = 0u;
  uint32_t occurrences = 0u;
  uint8_t *data;
  torb_text result;
  if (part.length == 0u) {
    return torb_text_retained(text);
  }
  while (position + part.length <= text.length) {
    if (memcmp(torb_text_data(text) + position, torb_text_data(part), (size_t)part.length) == 0) {
      occurrences += 1u;
      position += part.length;
    } else {
      position += 1u;
    }
  }
  if (occurrences == 0u) {
    return torb_text_retained(text);
  }
  total = (size_t)text.length + (size_t)occurrences * (size_t)replacement.length
          - (size_t)occurrences * (size_t)part.length;
  torb_check_text_length(total);
  result = torb_text_allocate((uint32_t)total, &data);
  position = 0u;
  while (position < text.length) {
    if (position + part.length <= text.length
        && memcmp(torb_text_data(text) + position, torb_text_data(part), (size_t)part.length) == 0) {
      if (replacement.length > 0u) {
        memcpy(data, torb_text_data(replacement), (size_t)replacement.length);
        data += replacement.length;
      }
      position += part.length;
    } else {
      *data = torb_text_data(text)[position];
      data += 1;
      position += 1u;
    }
  }
  return result;
}

torb_text torb_text_repeat(torb_text text, int64_t times, torb_location at) {
  size_t total;
  uint8_t *data;
  torb_text result;
  int64_t index;
  if (times < 0) {
    torb_panic_index_out_of_bounds(times, 0, at);
  }
  total = (size_t)text.length * (size_t)times;
  torb_check_text_length(total);
  if (total == 0u) {
    return torb_text_empty();
  }
  result = torb_text_allocate((uint32_t)total, &data);
  for (index = 0; index < times; index += 1) {
    memcpy(data, torb_text_data(text), (size_t)text.length);
    data += text.length;
  }
  return result;
}

torb_list torb_text_split(torb_text text, torb_text separator) {
  torb_list parts = torb_list_new(&torb_element_text);
  uint32_t start = 0u;
  if (separator.length == 0u) {
    /* Every character on its own, which is what a separator of nothing can only mean. */
    uint32_t offset = 0u;
    while (offset < text.length) {
      torb_char character = 0u;
      uint32_t previous = offset;
      (void)torb_text_next_char(text, &offset, &character);
      {
        torb_text piece = torb_text_slice(text, (int64_t)previous, (int64_t)offset, torb_location_unknown);
        torb_list_add(&parts, &piece);
      }
    }
    return parts;
  }
  for (;;) {
    int64_t found = torb_find(text, separator, start);
    if (found < 0) {
      torb_text piece = torb_text_slice(text, (int64_t)start, (int64_t)text.length, torb_location_unknown);
      torb_list_add(&parts, &piece);
      break;
    }
    {
      torb_text piece = torb_text_slice(text, (int64_t)start, found, torb_location_unknown);
      torb_list_add(&parts, &piece);
    }
    start = (uint32_t)found + separator.length;
  }
  return parts;
}

/* ------------------------------------------------------------------------------------------------ Char --- */

bool torb_char_is_digit(torb_char character) {
  return character >= '0' && character <= '9';
}

bool torb_char_is_letter(torb_char character) {
  if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
    return true;
  }
  if (character < 0x80u) {
    return false;
  }
  /* An approximation above ASCII: the punctuation and the symbols of Latin-1 are not letters, everything else is. */
  if (character <= 0xBFu) {
    return character == 0xAAu || character == 0xB5u || character == 0xBAu;
  }
  return character != 0xD7u && character != 0xF7u;
}

bool torb_char_is_whitespace(torb_char character) {
  if (character < 0x80u) {
    return torb_is_ascii_space((uint8_t)character);
  }
  return character == 0x85u || character == 0xA0u || character == 0x1680u
         || (character >= 0x2000u && character <= 0x200Au) || character == 0x2028u || character == 0x2029u
         || character == 0x202Fu || character == 0x205Fu || character == 0x3000u;
}

torb_char torb_char_to_upper_case(torb_char character) {
  if (character >= 'a' && character <= 'z') {
    return character - ('a' - 'A');
  }
  return character;
}

torb_char torb_char_to_lower_case(torb_char character) {
  if (character >= 'A' && character <= 'Z') {
    return character + ('a' - 'A');
  }
  return character;
}

int64_t torb_char_byte_length_of(torb_char character) {
  return (int64_t)torb_char_byte_length(character);
}

bool torb_char_try_from_i64(int64_t value, torb_char *out) {
  if (value < 0 || value > 0x10FFFF) {
    return false;
  }
  if (value >= 0xD800 && value <= 0xDFFF) {
    return false;
  }
  *out = (torb_char)value;
  return true;
}

/* --------------------------------------------------------------------------------------------- Show --- */

static torb_text torb_text_of_ascii(const char *bytes, size_t length) {
  uint8_t *data;
  torb_text result = torb_text_allocate((uint32_t)length, &data);
  if (length > 0u) {
    memcpy(data, bytes, length);
  }
  return result;
}

torb_text torb_show_bool(bool value) {
  return torb_text_of_ascii(value ? "true" : "false", value ? 4u : 5u);
}

torb_text torb_show_void(void) {
  return torb_text_of_ascii("()", 2u);
}

torb_text torb_show_i64(int64_t value) {
  char buffer[24];
  int written = snprintf(buffer, sizeof buffer, "%lld", (long long)value);
  return torb_text_of_ascii(buffer, (size_t)written);
}

torb_text torb_show_u64(uint64_t value) {
  char buffer[24];
  int written = snprintf(buffer, sizeof buffer, "%llu", (unsigned long long)value);
  return torb_text_of_ascii(buffer, (size_t)written);
}

torb_text torb_show_char(torb_char character) {
  uint8_t encoded[4];
  uint32_t width = torb_utf8_encode(character, encoded);
  uint8_t *data;
  torb_text result = torb_text_allocate(width, &data);
  memcpy(data, encoded, (size_t)width);
  return result;
}

/** Appends the escape of one character, in the spelling the language's literals use. */
static size_t torb_escape_char(torb_char character, char quote, char *out) {
  switch (character) {
    case '\n':
      out[0] = '\\';
      out[1] = 'n';
      return 2u;
    case '\r':
      out[0] = '\\';
      out[1] = 'r';
      return 2u;
    case '\t':
      out[0] = '\\';
      out[1] = 't';
      return 2u;
    case '\\':
      out[0] = '\\';
      out[1] = '\\';
      return 2u;
    default:
      break;
  }
  if (character == (torb_char)quote) {
    out[0] = '\\';
    out[1] = quote;
    return 2u;
  }
  if (character < 0x20u || character == 0x7Fu) {
    return (size_t)snprintf(out, 11u, "\\u{%x}", (unsigned)character);
  }
  return (size_t)torb_utf8_encode(character, (uint8_t *)out);
}

torb_text torb_show_char_nested(torb_char character) {
  char buffer[16];
  size_t length = 0u;
  buffer[length] = '\'';
  length += 1u;
  length += torb_escape_char(character, '\'', buffer + length);
  buffer[length] = '\'';
  length += 1u;
  return torb_text_of_ascii(buffer, length);
}

torb_text torb_text_show_nested(torb_text text) {
  size_t capacity = (size_t)text.length * 6u + 2u;
  char *buffer = (char *)torb_raw_allocate(capacity);
  size_t length = 0u;
  uint32_t offset = 0u;
  torb_text result;
  buffer[length] = '"';
  length += 1u;
  for (;;) {
    torb_char character = 0u;
    if (!torb_text_next_char(text, &offset, &character)) {
      break;
    }
    length += torb_escape_char(character, '"', buffer + length);
  }
  buffer[length] = '"';
  length += 1u;
  result = torb_text_of_ascii(buffer, length);
  torb_raw_free(buffer, capacity);
  return result;
}

/* ------------------------------------------------------------------------------------- float formatting --- */

static bool torb_same_bits(double first, double second) {
  uint64_t left, right;
  memcpy(&left, &first, sizeof left);
  memcpy(&right, &second, sizeof right);
  return left == right;
}

/**
 * The shortest decimal string that parses back to the same `Float64` (decided gap 4).
 *
 * Correctness first, speed later: `%.*e` is asked for 1 up to 17 significant digits and the first answer that
 * `strtod` turns back into the identical bit pattern wins. That is by construction the shortest round-tripping
 * decimal, and 17 digits always round-trip, so the loop always ends. A Ryu or Grisu routine would be faster and has
 * to produce exactly the same strings; this one is the reference they are tested against.
 *
 * `%e` is asked rather than `%g`, because `%g` decides between the two notations by the precision it was given, which
 * would make `100.0` come out as `1e2`. The notation is decided here instead, by the decimal exponent of the shortest
 * form: the exponent notation for an exponent below -6 or at 21 and above, the plain one in between. `.0` is appended
 * when neither a `.` nor an `e` is in the result, so a `Float` always carries a decimal point. `nan`, `inf`, `-inf`,
 * and `-0.0` prints as `-0.0`.
 */
static size_t torb_format_f64(double value, char *out, size_t capacity) {
  char scientific[64];
  char digits[24];
  int precision;
  int exponent = 0;
  size_t digit_count = 0u;
  bool negative = false;
  size_t length = 0u;
  if (isnan(value)) {
    snprintf(out, capacity, "nan");
    return 3u;
  }
  if (isinf(value)) {
    snprintf(out, capacity, value < 0.0 ? "-inf" : "inf");
    return value < 0.0 ? 4u : 3u;
  }
  scientific[0] = '\0';
  for (precision = 1; precision <= 17; precision += 1) {
    snprintf(scientific, sizeof scientific, "%.*e", precision - 1, value);
    if (torb_same_bits(strtod(scientific, NULL), value)) {
      break;
    }
  }
  {
    const char *reading = scientific;
    if (*reading == '-') {
      negative = true;
      reading += 1;
    }
    while (*reading != '\0' && *reading != 'e' && *reading != 'E') {
      if (*reading != '.' && digit_count < sizeof digits - 1u) {
        digits[digit_count] = *reading;
        digit_count += 1u;
      }
      reading += 1;
    }
    if (*reading == 'e' || *reading == 'E') {
      exponent = (int)strtol(reading + 1, NULL, 10);
    }
  }
  while (digit_count > 1u && digits[digit_count - 1u] == '0') {
    digit_count -= 1u;
  }
  if (negative) {
    out[length] = '-';
    length += 1u;
  }
  if (digit_count == 1u && digits[0] == '0') {
    memcpy(out + length, "0.0", 3u);
    length += 3u;
    out[length] = '\0';
    return length;
  }
  if (exponent < -6 || exponent >= 21) {
    out[length] = digits[0];
    length += 1u;
    if (digit_count > 1u) {
      out[length] = '.';
      length += 1u;
      memcpy(out + length, digits + 1, digit_count - 1u);
      length += digit_count - 1u;
    }
    out[length] = 'e';
    length += 1u;
    if (exponent < 0) {
      out[length] = '-';
      length += 1u;
    }
    length += (size_t)snprintf(out + length, capacity - length, "%d", exponent < 0 ? -exponent : exponent);
    return length;
  }
  if (exponent >= 0) {
    size_t integer_digits = (size_t)exponent + 1u;
    if (digit_count <= integer_digits) {
      memcpy(out + length, digits, digit_count);
      length += digit_count;
      memset(out + length, '0', integer_digits - digit_count);
      length += integer_digits - digit_count;
      memcpy(out + length, ".0", 2u);
      length += 2u;
    } else {
      memcpy(out + length, digits, integer_digits);
      length += integer_digits;
      out[length] = '.';
      length += 1u;
      memcpy(out + length, digits + integer_digits, digit_count - integer_digits);
      length += digit_count - integer_digits;
    }
  } else {
    size_t zeros = (size_t)(-exponent) - 1u;
    memcpy(out + length, "0.", 2u);
    length += 2u;
    memset(out + length, '0', zeros);
    length += zeros;
    memcpy(out + length, digits, digit_count);
    length += digit_count;
  }
  out[length] = '\0';
  return length;
}

torb_text torb_show_f64(double value) {
  char buffer[64];
  size_t length = torb_format_f64(value, buffer, sizeof buffer);
  return torb_text_of_ascii(buffer, length);
}

torb_text torb_show_f32(float value) {
  return torb_show_f64((double)value);
}

/* --------------------------------------------------------------------------------------------- parsing --- */

static int torb_digit_value(uint8_t byte) {
  if (byte >= '0' && byte <= '9') {
    return byte - '0';
  }
  if (byte >= 'a' && byte <= 'z') {
    return byte - 'a' + 10;
  }
  if (byte >= 'A' && byte <= 'Z') {
    return byte - 'A' + 10;
  }
  return -1;
}

bool torb_parse_i64_digits(torb_text text, int64_t radix, int64_t *out) {
  const uint8_t *bytes = torb_text_data(text);
  uint64_t result = 0u;
  uint32_t index;
  bool any = false;
  if (radix < 2 || radix > 36) {
    return false;
  }
  for (index = 0u; index < text.length; index += 1u) {
    int digit;
    if (bytes[index] == '_') {
      continue;
    }
    digit = torb_digit_value(bytes[index]);
    if (digit < 0 || (int64_t)digit >= radix) {
      return false;
    }
    if (result > (uint64_t)INT64_MAX / (uint64_t)radix) {
      return false;
    }
    result = result * (uint64_t)radix + (uint64_t)digit;
    if (result > (uint64_t)INT64_MAX) {
      return false;
    }
    any = true;
  }
  if (!any) {
    return false;
  }
  *out = (int64_t)result;
  return true;
}

bool torb_parse_i64(torb_text text, int64_t *out) {
  const uint8_t *bytes = torb_text_data(text);
  uint64_t result = 0u;
  uint32_t index = 0u;
  bool negative = false;
  bool any = false;
  if (text.length > 0u && (bytes[0] == '-' || bytes[0] == '+')) {
    negative = bytes[0] == '-';
    index = 1u;
  }
  for (; index < text.length; index += 1u) {
    int digit = torb_digit_value(bytes[index]);
    if (digit < 0 || digit > 9) {
      return false;
    }
    if (result > (uint64_t)INT64_MAX / 10u + 1u) {
      return false;
    }
    result = result * 10u + (uint64_t)digit;
    any = true;
  }
  if (!any) {
    return false;
  }
  if (negative) {
    if (result > (uint64_t)INT64_MAX + 1u) {
      return false;
    }
    *out = result == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)result;
    return true;
  }
  if (result > (uint64_t)INT64_MAX) {
    return false;
  }
  *out = (int64_t)result;
  return true;
}

bool torb_parse_u64(torb_text text, uint64_t *out) {
  const uint8_t *bytes = torb_text_data(text);
  uint64_t result = 0u;
  uint32_t index;
  bool any = false;
  for (index = 0u; index < text.length; index += 1u) {
    int digit = torb_digit_value(bytes[index]);
    if (digit < 0 || digit > 9) {
      return false;
    }
    if (result > UINT64_MAX / 10u) {
      return false;
    }
    result *= 10u;
    if (result > UINT64_MAX - (uint64_t)digit) {
      return false;
    }
    result += (uint64_t)digit;
    any = true;
  }
  if (!any) {
    return false;
  }
  *out = result;
  return true;
}

bool torb_parse_f64(torb_text text, double *out) {
  char buffer[512];
  char *end = NULL;
  double value;
  if (text.length == 0u || text.length >= sizeof buffer) {
    return false;
  }
  memcpy(buffer, torb_text_data(text), (size_t)text.length);
  buffer[text.length] = '\0';
  value = strtod(buffer, &end);
  if (end != buffer + text.length) {
    return false;
  }
  *out = value;
  return true;
}

bool torb_parse_bool(torb_text text, bool *out) {
  if (text.length == 4u && memcmp(torb_text_data(text), "true", 4u) == 0) {
    *out = true;
    return true;
  }
  if (text.length == 5u && memcmp(torb_text_data(text), "false", 5u) == 0) {
    *out = false;
    return true;
  }
  return false;
}

/* --------------------------------------------------------------------------------------------- hashing --- */

uint64_t torb_hash_bytes(const void *bytes, size_t length) {
  const uint8_t *data = (const uint8_t *)bytes;
  uint64_t hash = 14695981039346656037u; /* The FNV-1a-64 offset basis. A fixed seed, so a hash never varies. */
  size_t index;
  for (index = 0u; index < length; index += 1u) {
    hash ^= (uint64_t)data[index];
    hash *= 1099511628211u;
  }
  return hash;
}

uint64_t torb_hash_u64(uint64_t value) {
  uint8_t bytes[8];
  size_t index;
  for (index = 0u; index < 8u; index += 1u) {
    bytes[index] = (uint8_t)(value >> (index * 8u));
  }
  return torb_hash_bytes(bytes, 8u);
}

uint64_t torb_hash_i64(int64_t value) {
  return torb_hash_u64((uint64_t)value);
}

uint64_t torb_hash_bool(bool value) {
  return torb_hash_u64(value ? 1u : 0u);
}

uint64_t torb_hash_char(torb_char character) {
  return torb_hash_u64((uint64_t)character);
}

uint64_t torb_hash_combine(uint64_t first, uint64_t second) {
  return torb_hash_u64(first ^ (second * 1099511628211u));
}

/* -------------------------------------------------------------------------------- the runtime's descriptors --- */

static void torb_element_text_retain(void *element) {
  torb_text *text = (torb_text *)element;
  torb_retain(text->storage);
}

static void torb_element_text_release(void *element) {
  torb_text *text = (torb_text *)element;
  torb_release(text->storage, NULL);
}

static bool torb_element_text_equals(const void *first, const void *second) {
  return torb_text_equal(*(const torb_text *)first, *(const torb_text *)second);
}

static uint64_t torb_element_text_hash(const void *element) {
  return torb_text_hash(*(const torb_text *)element);
}

static bool torb_element_int64_equals(const void *first, const void *second) {
  return *(const int64_t *)first == *(const int64_t *)second;
}

static uint64_t torb_element_int64_hash(const void *element) {
  return torb_hash_i64(*(const int64_t *)element);
}

const torb_element torb_element_text = {
  (uint32_t)sizeof(torb_text), (uint32_t)TORB_ALIGN_OF(torb_text),
  torb_element_text_retain, torb_element_text_release,
  torb_element_text_equals, torb_element_text_hash
};

const torb_element torb_element_int64 = {
  (uint32_t)sizeof(int64_t), (uint32_t)TORB_ALIGN_OF(int64_t),
  NULL, NULL, torb_element_int64_equals, torb_element_int64_hash
};

const torb_element torb_element_unit = { 0u, 1u, NULL, NULL, NULL, NULL };
