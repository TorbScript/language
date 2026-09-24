/*
 * environment.c - `Environment` of `std/os`: `Environment.get`, and the listing `Environment.entries`.
 *
 * A native program sees every variable the platform can. A sandboxed script sees the ones a pattern of its grant
 * matches (`environment "APP_*"`, docs/design/SCRIPTS.md section 4), and every other one reads as unset: the loud lock
 * is the module, which a host that grants no environment does not grant either.
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
  bool found;
  if (!torb_sandbox_allows_variable(buffer)) {
    torb_raw_free(buffer, capacity);
    return false;
  }
  found = torb_platform_environment_variable(buffer, &value, &length);
  torb_raw_free(buffer, capacity);
  if (!found) {
    return false;
  }
  *out = torb_text_from_cstring(value);
  torb_raw_free(value, length + 1u);
  return true;
}

/**
 * `Environment.variables()` of `std/os` is built from this: the variables of the platform layer, each held against the
 * sandbox's patterns the way `torb_environment_get` holds one name.
 */
void torb_environment_entries(torb_list *names, torb_list *values) {
  torb_platform_environment_entries(names, values, torb_sandbox_allows_variable);
}
