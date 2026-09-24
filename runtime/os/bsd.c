/*
 * bsd.c - the natives of `std/os` that macOS and FreeBSD share (`native type Bsd`, docs/design/OS.md section 7): the
 * `sysctl` interface both inherited from BSD, read by name.
 *
 * The whole file is one `#if`: it is compiled on every machine and is empty everywhere but on macOS and FreeBSD, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine. No feature macro is defined: `sysctlbyname` is no POSIX function, and
 * both systems hide it, and the types its header needs, from a program that asks for strict POSIX.
 */

#include "torb.h"

#if defined(__APPLE__) || defined(__FreeBSD__)

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

/** Replaces the text a `var` parameter holds with a copy of `value`, where `value` is UTF-8. */
static bool torb_os_bsd_answer(torb_text *slot, const char *value, size_t length) {
  torb_text text = torb_text_empty();
  size_t bad = 0u;
  if (!torb_text_try_from_bytes((const uint8_t *)value, length, &text, &bad)) {
    return false;
  }
  torb_text_release(*slot);
  *slot = text;
  return true;
}

/** The outcome of an `errno`, with the C library's message for it in `*failure`. */
static int64_t torb_os_bsd_failure(int code, torb_text *failure) {
  const char *message = strerror(code);
  (void)torb_os_bsd_answer(failure, message, strlen(message));
  switch (code) {
    case EACCES:
    case EPERM:
      return TORB_OS_DENIED;
    case ENOENT:
      return TORB_OS_MISSING;
    default:
      return TORB_OS_FAILED;
  }
}

/** A borrowed text as a NUL-terminated name. Owned, `torb_raw_free(result, text.length + 1)`. */
static char *torb_os_bsd_name(torb_text text) {
  char *name = (char *)torb_raw_allocate((size_t)text.length + 1u);
  if (text.length > 0u) {
    memcpy(name, text.storage->data + text.offset, (size_t)text.length);
  }
  name[text.length] = '\0';
  return name;
}

int64_t torb_os_bsd_sysctl_text(torb_text name, torb_text *text, torb_text *failure) {
  char *key = torb_os_bsd_name(name);
  size_t length = 0u;
  int64_t result = TORB_OS_SUCCESS;
  if (sysctlbyname(key, NULL, &length, NULL, 0) != 0) {
    result = torb_os_bsd_failure(errno, failure);
  } else {
    const size_t capacity = length + 1u;
    char *buffer = (char *)torb_raw_allocate(capacity);
    memset(buffer, 0, capacity);
    if (sysctlbyname(key, buffer, &length, NULL, 0) != 0) {
      result = torb_os_bsd_failure(errno, failure);
    } else if (!torb_os_bsd_answer(text, buffer, strlen(buffer))) {
      static const char message[] = "the value is not UTF-8";
      (void)torb_os_bsd_answer(failure, message, sizeof message - 1u);
      result = TORB_OS_FAILED;
    }
    torb_raw_free(buffer, capacity);
  }
  torb_raw_free(key, (size_t)name.length + 1u);
  return result;
}

int64_t torb_os_bsd_sysctl_integer(torb_text name, int64_t *number, torb_text *failure) {
  char *key = torb_os_bsd_name(name);
  union {
    int8_t narrow8;
    int16_t narrow16;
    int32_t narrow32;
    int64_t wide;
  } value;
  size_t length = sizeof value;
  int64_t result = TORB_OS_SUCCESS;
  memset(&value, 0, sizeof value);
  if (sysctlbyname(key, &value, &length, NULL, 0) != 0) {
    result = torb_os_bsd_failure(errno, failure);
  } else if (length == 1u) {
    *number = (int64_t)value.narrow8;
  } else if (length == 2u) {
    *number = (int64_t)value.narrow16;
  } else if (length == 4u) {
    *number = (int64_t)value.narrow32;
  } else if (length == 8u) {
    *number = value.wide;
  } else {
    static const char message[] = "the value is no integer";
    (void)torb_os_bsd_answer(failure, message, sizeof message - 1u);
    result = TORB_OS_FAILED;
  }
  torb_raw_free(key, (size_t)name.length + 1u);
  return result;
}

int64_t torb_os_bsd_uptime(int64_t *milliseconds, torb_text *failure) {
  struct timeval boot;
  struct timeval now;
  size_t length = sizeof boot;
  if (sysctlbyname("kern.boottime", &boot, &length, NULL, 0) != 0) {
    return torb_os_bsd_failure(errno, failure);
  }
  if (gettimeofday(&now, NULL) != 0) {
    return torb_os_bsd_failure(errno, failure);
  }
  *milliseconds = ((int64_t)now.tv_sec - (int64_t)boot.tv_sec) * 1000
                  + ((int64_t)now.tv_usec - (int64_t)boot.tv_usec) / 1000;
  return TORB_OS_SUCCESS;
}

#endif /* __APPLE__ || __FreeBSD__ */
