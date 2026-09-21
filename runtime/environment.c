/*
 * environment.c - `std/environment`: `Environment.get`.
 *
 * Capability filtering for a sandboxed script (`environment "APP_*"`) is a front-end concern of milestone 7.4; here
 * every variable `getenv` can see is visible, which is what a native, non-sandboxed program expects.
 */

#include "torb.h"

#include <string.h>

/** A NUL-terminated copy of `name`. Owned; free with `torb_raw_free(buffer, *capacity)`. */
static char *torb_environment_name_bytes(torb_text name, size_t *capacity) {
  *capacity = (size_t)name.length + 1u;
  {
    char *buffer = (char *)torb_raw_allocate(*capacity);
    if (name.length > 0u) {
      memcpy(buffer, name.storage->data + name.offset, (size_t)name.length);
    }
    buffer[name.length] = '\0';
    return buffer;
  }
}

/**
 * The value goes through the platform layer and not through `getenv`, because on Windows the narrow environment is the
 * code page of the machine: a variable whose value holds a non-ASCII character (a `TEMP` under a user called `grüße`)
 * would not be UTF-8 at all, and a `String` always is.
 */
bool torb_environment_get(torb_text name, torb_text *out) {
  size_t capacity = 0u;
  char *buffer = torb_environment_name_bytes(name, &capacity);
  char *value = NULL;
  size_t length = 0u;
  const bool found = torb_platform_environment_variable(buffer, &value, &length);
  torb_raw_free(buffer, capacity);
  if (!found) {
    return false;
  }
  *out = torb_text_from_cstring(value);
  torb_raw_free(value, length + 1u);
  return true;
}
