/*
 * environment.c - `std/environment`: `Environment.get`.
 *
 * Capability filtering for a sandboxed script (`environment "APP_*"`) is a front-end concern of milestone 7.4; here
 * every variable `getenv` can see is visible, which is what a native, non-sandboxed program expects.
 */

#include "torb.h"

#include <stdlib.h>
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

bool torb_environment_get(torb_text name, torb_text *out) {
  size_t capacity = 0u;
  char *buffer = torb_environment_name_bytes(name, &capacity);
  const char *value = getenv(buffer);
  torb_raw_free(buffer, capacity);
  if (value == NULL) {
    return false;
  }
  *out = torb_text_from_cstring(value);
  return true;
}
