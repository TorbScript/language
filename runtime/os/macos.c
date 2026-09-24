/*
 * macos.c - the natives of `std/os` that only macOS has (`native type MacOs`, docs/design/OS.md section 7): the per-user
 * temporary directory now, and the Mach host statistics and `_NSGetExecutablePath` when their slices come. What macOS
 * shares with FreeBSD is in `bsd.c`, and what it shares with every POSIX system in `posix.c`.
 *
 * The whole file is one `#if defined(__APPLE__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine. No feature macro is defined, because `_CS_DARWIN_USER_TEMP_DIR` is
 * Darwin's own and strict POSIX hides it.
 */

#include "torb.h"

#if defined(__APPLE__)

#include <errno.h>
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

#endif /* __APPLE__ */
