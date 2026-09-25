/*
 * macos.c - the natives of `std/os` that only macOS has (`native type MacOs`, docs/design/OS.md section 7): the per-user
 * temporary directory and the path of the running executable now, and the Mach host statistics when their slice comes.
 * What macOS shares with FreeBSD is in `bsd.c`, and what it shares with every POSIX system in `posix.c`.
 *
 * The whole file is one `#if defined(__APPLE__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine, except the executable path's, which the platform layer calls and
 * `torb.h` declares. No feature macro is defined, because `_CS_DARWIN_USER_TEMP_DIR` is
 * Darwin's own and strict POSIX hides it.
 */

#include "torb.h"

#if defined(__APPLE__)

#include <errno.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Replaces the text a `var` parameter holds with a copy of `value`, where `value` is UTF-8. */
static bool torb_os_macos_answer(torb_text *slot, const char *value) {
  torb_text text = torb_text_empty();
  size_t bad = 0u;
  if (!torb_text_try_from_bytes((const uint8_t *)value, strlen(value), &text, &bad)) {
    return false;
  }
  torb_text_release(*slot);
  *slot = text;
  return true;
}

int64_t torb_os_macos_user_temporary_directory(torb_text *path, torb_text *failure) {
  const size_t capacity = confstr(_CS_DARWIN_USER_TEMP_DIR, NULL, 0u);
  char *buffer;
  int64_t result = TORB_OS_SUCCESS;
  if (capacity == 0u) {
    (void)torb_os_macos_answer(failure, strerror(errno));
    return TORB_OS_FAILED;
  }
  buffer = (char *)torb_raw_allocate(capacity);
  if (confstr(_CS_DARWIN_USER_TEMP_DIR, buffer, capacity) == 0u) {
    (void)torb_os_macos_answer(failure, strerror(errno));
    result = TORB_OS_FAILED;
  } else if (!torb_os_macos_answer(path, buffer)) {
    (void)torb_os_macos_answer(failure, "the path is not UTF-8");
    result = TORB_OS_FAILED;
  }
  torb_raw_free(buffer, capacity);
  return result;
}

/**
 * `Process.executablePath()` on macOS: what `_NSGetExecutablePath` answers, which is the path the program was started
 * by and may run through a symbolic link or a `..`, with every one of them resolved by `realpath` - the same answer
 * Linux's `/proc/self/exe` gives. Owned, freed with `torb_raw_free(*value, *length + 1)`; false and nothing allocated
 * where either call fails.
 */
bool torb_os_macos_executable_path(char **value, size_t *length) {
  uint32_t capacity = 0u;
  size_t allocated;
  char *given;
  char *resolved;
  /* Asked with no room, it answers -1 and the size it needs, terminator included */
  (void)_NSGetExecutablePath(NULL, &capacity);
  if (capacity == 0u) {
    return false;
  }
  allocated = (size_t)capacity;
  given = (char *)torb_raw_allocate(allocated);
  if (_NSGetExecutablePath(given, &capacity) != 0) {
    torb_raw_free(given, allocated);
    return false;
  }
  resolved = realpath(given, NULL);
  torb_raw_free(given, allocated);
  if (resolved == NULL) {
    return false;
  }
  *length = strlen(resolved);
  *value = (char *)torb_raw_allocate(*length + 1u);
  memcpy(*value, resolved, *length + 1u);
  free(resolved);
  return true;
}

#endif /* __APPLE__ */
