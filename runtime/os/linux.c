/*
 * linux.c - the natives of `std/os` that only Linux has (`native type Linux`, docs/design/OS.md section 7): reading
 * `/proc` and `/sys`. Everything read is parsed in TorbScript (`std/os/src/linux/`), where the parsers are tested on
 * every machine; this file only hands the bytes over.
 *
 * The whole file is one `#if defined(__linux__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 */

#include "torb.h"

#if defined(__linux__)

#include <errno.h>
#include <stdio.h>
#include <string.h>

/** Replaces the text a `var` parameter holds with a copy of `value`. */
static void torb_os_linux_answer(torb_text *slot, const char *value) {
  torb_text_release(*slot);
  *slot = torb_text_from_cstring(value);
}

/** The outcome of an `errno`, with the C library's message for it in `*failure`. */
static int64_t torb_os_linux_failure(int code, torb_text *failure) {
  torb_os_linux_answer(failure, strerror(code));
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

/**
 * Whether `path` is one this native reads: below `/proc/` or `/sys/` without a `..` that could leave them, or one of the
 * two places `os-release` is. Everything else is a file, and a file is `std/fs`'s and the sandbox's.
 */
static bool torb_os_linux_is_system_file(const char *path) {
  if (strcmp(path, "/etc/os-release") == 0 || strcmp(path, "/usr/lib/os-release") == 0) {
    return true;
  }
  if (strncmp(path, "/proc/", 6u) != 0 && strncmp(path, "/sys/", 5u) != 0) {
    return false;
  }
  return strstr(path, "/..") == NULL;
}

/** Until the end of the file, because a file of `/proc` reports the size 0 and a read by size would answer nothing. */
int64_t torb_os_linux_read_system_file(torb_text path, torb_text *text, torb_text *failure) {
  const size_t pathBytes = (size_t)path.length + 1u;
  char *name = (char *)torb_raw_allocate(pathBytes);
  FILE *file;
  uint8_t *buffer;
  size_t capacity = 4096u;
  size_t length = 0u;
  int64_t result = TORB_OS_SUCCESS;
  if (path.length > 0u) {
    memcpy(name, path.storage->data + path.offset, (size_t)path.length);
  }
  name[path.length] = '\0';
  if (strlen(name) != (size_t)path.length || !torb_os_linux_is_system_file(name)) {
    torb_raw_free(name, pathBytes);
    torb_os_linux_answer(failure, "only a file below /proc or /sys, or os-release, is a system file");
    return TORB_OS_DENIED;
  }
  file = fopen(name, "rb");
  torb_raw_free(name, pathBytes);
  if (file == NULL) {
    return torb_os_linux_failure(errno, failure);
  }
  buffer = (uint8_t *)torb_raw_allocate(capacity);
  for (;;) {
    const size_t got = fread(buffer + length, 1u, capacity - length, file);
    length += got;
    if (length == capacity) {
      uint8_t *grown = (uint8_t *)torb_raw_allocate(capacity * 2u);
      memcpy(grown, buffer, length);
      torb_raw_free(buffer, capacity);
      buffer = grown;
      capacity *= 2u;
      continue;
    }
    if (got == 0u) {
      break;
    }
  }
  if (ferror(file)) {
    result = torb_os_linux_failure(errno, failure);
  } else {
    torb_text decoded = torb_text_empty();
    size_t bad = 0u;
    if (torb_text_try_from_bytes(buffer, length, &decoded, &bad)) {
      torb_text_release(*text);
      *text = decoded;
    } else {
      torb_os_linux_answer(failure, "the file is not UTF-8");
      result = TORB_OS_FAILED;
    }
  }
  fclose(file);
  torb_raw_free(buffer, capacity);
  return result;
}

#endif /* __linux__ */
