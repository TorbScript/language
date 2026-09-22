/*
 * file.c - `std/fs`: `readText`, `writeText`, `exists`, `isDirectory`, `list` (sorted), `absolutePath`, and the open
 * handle (`File.open`/`readAll`/`close`) that lets a program read a file without holding it in memory as one
 * `readText`.
 *
 * Every function answers false on failure and puts the message of the `IoError` into `*error` (owned). The `IoError`
 * record and the `Result` around it are layouts of the program, so the lowering builds them - a runtime function
 * never constructs a type of the language.
 *
 * `readText` and `File.readAll` both validate UTF-8 and fail when the bytes are not: a `String` is always valid
 * UTF-8, so a file that is not is an `IoError` and never a replacement character (decided gap 7).
 *
 * Directory listings are sorted by bytes, always. The fixpoint test of the compiler depends on it.
 */

#include "torb.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/** A NUL-terminated copy of a path. Owned; free with `torb_raw_free(buffer, *capacity)`. */
static char *torb_path_bytes(torb_text path, size_t *capacity) {
  *capacity = (size_t)path.length + 1u;
  {
    char *buffer = (char *)torb_raw_allocate(*capacity);
    if (path.length > 0u) {
      memcpy(buffer, path.storage->data + path.offset, (size_t)path.length);
    }
    buffer[path.length] = '\0';
    return buffer;
  }
}

/** `<path>: <message>`, which is what `IoError.show()` prints. Result owned. */
static torb_text torb_io_error(torb_text path, const char *message) {
  torb_text parts[3];
  torb_text result;
  parts[0] = path;
  parts[1] = torb_text_from_cstring(": ");
  parts[2] = torb_text_from_cstring(message);
  result = torb_text_concat(parts, 3u);
  torb_text_release(parts[1]);
  torb_text_release(parts[2]);
  return result;
}

bool torb_file_read_text(torb_text path, torb_text *out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  const char *message = NULL;
  uint8_t *bytes = NULL;
  size_t length = 0u;
  size_t bad_offset = 0u;
  bool read = torb_platform_read_file(name, &bytes, &length, &message);
  torb_raw_free(name, capacity);
  if (!read) {
    *error = torb_io_error(path, message);
    return false;
  }
  if (!torb_text_try_from_bytes(bytes, length, out, &bad_offset)) {
    char detail[96];
    snprintf(detail, sizeof detail, "the byte at offset %lu is not valid UTF-8", (unsigned long)bad_offset);
    *error = torb_io_error(path, detail);
    torb_raw_free(bytes, length);
    return false;
  }
  torb_raw_free(bytes, length);
  return true;
}

bool torb_file_write_text(torb_text path, torb_text text, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  const char *message = NULL;
  const uint8_t *bytes = text.length == 0u ? NULL : text.storage->data + text.offset;
  bool written = torb_platform_write_file(name, bytes, (size_t)text.length, &message);
  torb_raw_free(name, capacity);
  if (!written) {
    *error = torb_io_error(path, message);
    return false;
  }
  return true;
}

/**
 * `File.createDirectory`: the directory and every directory above it that is missing, and nothing where one is there.
 *
 * `torb build` has to create the directory it writes the C and the binary into, which is the reason this exists at all -
 * a caller that only wants a place to write should not have to ask first, so "it is already there" is success.
 */
bool torb_file_create_directory(torb_text path, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  const char *message = NULL;
  bool created = torb_platform_create_directory(name, &message);
  torb_raw_free(name, capacity);
  if (!created) {
    *error = torb_io_error(path, message);
    return false;
  }
  return true;
}

bool torb_file_exists(torb_text path) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  torb_path_kind kind = torb_platform_path_kind(name);
  torb_raw_free(name, capacity);
  return kind != TORB_PATH_MISSING;
}

bool torb_file_is_directory(torb_text path) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  torb_path_kind kind = torb_platform_path_kind(name);
  torb_raw_free(name, capacity);
  return kind == TORB_PATH_DIRECTORY;
}

static int32_t torb_compare_text_elements(const void *first, const void *second, void *context) {
  (void)context;
  return torb_text_compare(*(const torb_text *)first, *(const torb_text *)second);
}

bool torb_file_list(torb_text path, torb_list *out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  const char *message = NULL;
  torb_list entries = torb_list_new(&torb_element_text);
  bool listed = torb_platform_list_directory(name, &entries, &message);
  torb_raw_free(name, capacity);
  if (!listed) {
    torb_list_release(entries);
    *error = torb_io_error(path, message);
    return false;
  }
  torb_list_sort(&entries, torb_compare_text_elements, NULL);
  *out = entries;
  return true;
}

/* ------------------------------------------------------------------------------------------- absolute paths --- */

static bool torb_path_is_absolute(const char *bytes, size_t length) {
  if (length >= 1u && (bytes[0] == '/' || bytes[0] == '\\')) {
    return true;
  }
  if (length >= 2u && bytes[1] == ':'
      && ((bytes[0] >= 'A' && bytes[0] <= 'Z') || (bytes[0] >= 'a' && bytes[0] <= 'z'))) {
    return true;
  }
  return false;
}

/**
 * Text arithmetic, not a lookup: `.` and `..` are resolved against the working directory, the file does not have to
 * exist and links are not followed. Separators come out as forward slashes, on every platform, so no path of a
 * machine ever differs between them beyond the drive letter.
 */
bool torb_file_absolute_path(torb_text path, torb_text *out, torb_text *error) {
  size_t joined_capacity;
  char *joined;
  size_t joined_length = 0u;
  char *result;
  size_t result_capacity;
  size_t result_length = 0u;
  uint32_t *starts;
  size_t start_capacity;
  size_t depth = 0u;
  size_t position;
  size_t root_length = 0u;

  if (torb_path_is_absolute(path.length == 0u ? "" : (const char *)(path.storage->data + path.offset),
                            (size_t)path.length)) {
    joined_capacity = (size_t)path.length + 1u;
    joined = (char *)torb_raw_allocate(joined_capacity);
    memcpy(joined, path.storage->data + path.offset, (size_t)path.length);
    joined_length = (size_t)path.length;
  } else {
    size_t working_length = 0u;
    char *working = torb_platform_working_directory(&working_length);
    if (working == NULL) {
      *error = torb_io_error(path, "the working directory could not be read");
      return false;
    }
    joined_capacity = working_length + (size_t)path.length + 2u;
    joined = (char *)torb_raw_allocate(joined_capacity);
    memcpy(joined, working, working_length);
    joined_length = working_length;
    torb_raw_free(working, working_length + 1u);
    joined[joined_length] = '/';
    joined_length += 1u;
    if (path.length > 0u) {
      memcpy(joined + joined_length, path.storage->data + path.offset, (size_t)path.length);
      joined_length += (size_t)path.length;
    }
  }
  for (position = 0u; position < joined_length; position += 1u) {
    if (joined[position] == '\\') {
      joined[position] = '/';
    }
  }
  /* An extended-length path (`\\?\C:\x`, `\\?\UNC\server\share`) loses that prefix here: it is the form a *call* of
     the operating system takes and never a form a path is shown in, and one can arrive from anything that canonicalized
     a path on Windows. `UNC\` takes the place of the two separators of a share, so putting them back is what undoes it. */
  if (joined_length >= 4u && memcmp(joined, "//?/", 4u) == 0) {
    size_t skipped = 4u;
    if (joined_length >= 8u && memcmp(joined + 4u, "UNC/", 4u) == 0) {
      skipped = 6u;
      joined[6] = '/';
      joined[7] = '/';
    }
    joined_length -= skipped;
    memmove(joined, joined + skipped, joined_length);
  }

  result_capacity = joined_length + 2u;
  result = (char *)torb_raw_allocate(result_capacity);
  start_capacity = (joined_length + 2u) * sizeof(uint32_t);
  starts = (uint32_t *)torb_raw_allocate(start_capacity);

  position = 0u;
  if (joined_length >= 2u && joined[1] == ':') {
    result[0] = joined[0] >= 'a' && joined[0] <= 'z' ? (char)(joined[0] - ('a' - 'A')) : joined[0];
    result[1] = ':';
    result[2] = '/';
    result_length = 3u;
    position = joined[2] == '/' ? 3u : 2u;
  } else {
    result[0] = '/';
    result_length = 1u;
    while (position < joined_length && joined[position] == '/') {
      position += 1u;
    }
  }
  root_length = result_length;

  while (position < joined_length) {
    size_t start = position;
    size_t length;
    while (position < joined_length && joined[position] != '/') {
      position += 1u;
    }
    length = position - start;
    while (position < joined_length && joined[position] == '/') {
      position += 1u;
    }
    if (length == 0u || (length == 1u && joined[start] == '.')) {
      continue;
    }
    if (length == 2u && joined[start] == '.' && joined[start + 1u] == '.') {
      if (depth > 0u) {
        depth -= 1u;
        result_length = (size_t)starts[depth];
        if (result_length > root_length) {
          result_length -= 1u; /* Drop the separator this component was written after. */
        }
      }
      continue;
    }
    if (result_length > root_length) {
      result[result_length] = '/';
      result_length += 1u;
    }
    starts[depth] = (uint32_t)result_length;
    depth += 1u;
    memcpy(result + result_length, joined + start, length);
    result_length += length;
  }

  *out = torb_text_from_bytes((const uint8_t *)result, result_length, torb_location_unknown);
  torb_raw_free(starts, start_capacity);
  torb_raw_free(result, result_capacity);
  torb_raw_free(joined, joined_capacity);
  return true;
}

/* --------------------------------------------------------------------------------------------- open handles --- */

/**
 * `File.open`/`readAll`/`close`: the `torb_file` `shared type` of torb.h. `open` is read-only: nothing in `std/fs`
 * writes through an open handle, only `File.writeText` on a path.
 */

bool torb_file_open(torb_text path, torb_file **out, torb_text *error) {
  size_t capacity = 0u;
  char *name = torb_path_bytes(path, &capacity);
  const char *message = NULL;
  /* Through the platform layer, so the path is handed to the operating system in that platform's own form */
  FILE *handle = (FILE *)torb_platform_open_file(name, false, &message);
  torb_raw_free(name, capacity);
  if (handle == NULL) {
    *error = torb_io_error(path, message);
    return false;
  }
  {
    torb_file *file = (torb_file *)torb_allocate(sizeof(torb_file), TORB_BLOCK_SHARED);
    file->path = torb_text_retained(path);
    file->handle = handle;
    *out = file;
  }
  return true;
}

bool torb_file_read_all(torb_file *self, torb_text *out, torb_text *error) {
  size_t capacity = 65536u;
  size_t filled = 0u;
  uint8_t *buffer;
  size_t bad_offset = 0u;
  if (self->handle == NULL) {
    *error = torb_io_error(self->path, "the file is already closed");
    return false;
  }
  buffer = (uint8_t *)torb_raw_allocate(capacity);
  for (;;) {
    size_t read = fread(buffer + filled, 1u, capacity - filled, (FILE *)self->handle);
    filled += read;
    if (filled < capacity) {
      if (ferror((FILE *)self->handle)) {
        *error = torb_io_error(self->path, strerror(errno));
        torb_raw_free(buffer, capacity);
        return false;
      }
      break;
    }
    /* A full buffer past the limit of a text can never become one, and doubling it could wrap */
    if (capacity > (size_t)UINT32_MAX || capacity > SIZE_MAX / 2u) {
      torb_panic_text("a text longer than 4 GiB is not supported", torb_location_unknown);
    }
    {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
      memcpy(grown, buffer, filled);
      torb_raw_free(buffer, capacity);
      buffer = grown;
      capacity *= 2u;
    }
  }
  if (!torb_text_try_from_bytes(buffer, filled, out, &bad_offset)) {
    char detail[96];
    snprintf(detail, sizeof detail, "the byte at offset %lu is not valid UTF-8", (unsigned long)bad_offset);
    *error = torb_io_error(self->path, detail);
    torb_raw_free(buffer, capacity);
    return false;
  }
  torb_raw_free(buffer, capacity);
  return true;
}

void torb_file_close(torb_file *self) {
  if (self->handle != NULL) {
    fclose((FILE *)self->handle);
    self->handle = NULL;
  }
}

void torb_file_drop(void *block) {
  torb_file *file = (torb_file *)block;
  torb_file_close(file);
  torb_text_release(file->path);
}
