/*
 * posix.c - the natives of `std/os` that every POSIX system shares (`native type Posix`, docs/design/OS.md section 7):
 * Linux, macOS and FreeBSD.
 *
 * The whole file is one `#if`: it is compiled on every machine and is empty on Windows, so the build compiles every
 * file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its prototypes are in `torb_os.h`
 * on every machine. A difference between the POSIX systems does not belong here but in the file of the system.
 */

/* POSIX 2008, which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
/* On macOS `_POSIX_C_SOURCE` alone hides every Darwin extension (`_SC_NPROCESSORS_ONLN`, `SO_NOSIGPIPE`,
 * `pthread_cond_timedwait_relative_np`); `_DARWIN_C_SOURCE` shows them again beside POSIX. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE
#endif

#include "torb.h"

#if defined(__unix__) || defined(__APPLE__)

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

/**
 * Replaces the text a `var` parameter holds with `value`, which is a system's answer and therefore not always UTF-8:
 * false where it is not, and then the slot keeps what it held.
 */
static bool torb_os_posix_answer(torb_text *slot, const char *value) {
  torb_text text = torb_text_empty();
  size_t bad = 0u;
  if (!torb_text_try_from_bytes((const uint8_t *)value, strlen(value), &text, &bad)) {
    return false;
  }
  torb_text_release(*slot);
  *slot = text;
  return true;
}

/** The outcome of an `errno`, with the C library's message for it in `*failure`. */
static int64_t torb_os_posix_failure(int code, torb_text *failure) {
  if (!torb_os_posix_answer(failure, strerror(code))) {
    (void)torb_os_posix_answer(failure, "the system's message is not UTF-8");
  }
  switch (code) {
    case EACCES:
    case EPERM:
      return TORB_OS_DENIED;
    case ENOENT:
      return TORB_OS_MISSING;
    case ENOSYS:
    case ENOTSUP:
      return TORB_OS_UNSUPPORTED;
    default:
      return TORB_OS_FAILED;
  }
}

/** A text of the answer that is not UTF-8: the outcome, and the reason in `*failure`. */
static int64_t torb_os_posix_not_text(const char *what, torb_text *failure) {
  (void)torb_os_posix_answer(failure, what);
  return TORB_OS_FAILED;
}

int64_t torb_os_posix_effective_user_identifier(void) {
  return (int64_t)geteuid();
}

int64_t torb_os_posix_system_names(
  torb_text *system,
  torb_text *node,
  torb_text *release,
  torb_text *version,
  torb_text *machine,
  torb_text *failure
) {
  struct utsname names;
  if (uname(&names) < 0) {
    return torb_os_posix_failure(errno, failure);
  }
  if (!torb_os_posix_answer(system, names.sysname) || !torb_os_posix_answer(node, names.nodename)
      || !torb_os_posix_answer(release, names.release) || !torb_os_posix_answer(version, names.version)
      || !torb_os_posix_answer(machine, names.machine)) {
    return torb_os_posix_not_text("a name of uname is not UTF-8", failure);
  }
  return TORB_OS_SUCCESS;
}

int64_t torb_os_posix_host_name(torb_text *name, torb_text *failure) {
  /* 255 bytes is the longest name POSIX allows a host (`HOST_NAME_MAX`, which not every system defines) */
  char buffer[257];
  memset(buffer, 0, sizeof buffer);
  if (gethostname(buffer, sizeof buffer - 1u) != 0) {
    return torb_os_posix_failure(errno, failure);
  }
  if (!torb_os_posix_answer(name, buffer)) {
    return torb_os_posix_not_text("the host name is not UTF-8", failure);
  }
  return TORB_OS_SUCCESS;
}

/** The names `Posix.configuration` knows, without their `_SC_`: the numbers differ between systems, the names not. */
static const struct {
  const char *name;
  int value;
} torb_os_posix_configurations[] = {
  { "PAGESIZE", _SC_PAGESIZE },
  { "PAGE_SIZE", _SC_PAGESIZE },
  { "CLK_TCK", _SC_CLK_TCK },
  { "OPEN_MAX", _SC_OPEN_MAX },
  { "ARG_MAX", _SC_ARG_MAX },
};

int64_t torb_os_posix_configuration(torb_text name) {
  const char *data = (const char *)name.storage->data + name.offset;
  const size_t count = sizeof torb_os_posix_configurations / sizeof torb_os_posix_configurations[0];
  size_t index;
  for (index = 0u; index < count; index += 1u) {
    const char *known = torb_os_posix_configurations[index].name;
    if (strlen(known) == (size_t)name.length && memcmp(known, data, (size_t)name.length) == 0) {
      return (int64_t)sysconf(torb_os_posix_configurations[index].value);
    }
  }
  return -1;
}

/** The GECOS field up to its first comma, which is the full name by the convention every system keeps. */
static bool torb_os_posix_answer_full_name(torb_text *slot, const char *gecos) {
  const char *comma = strchr(gecos, ',');
  const size_t length = comma == NULL ? strlen(gecos) : (size_t)(comma - gecos);
  torb_text text = torb_text_empty();
  size_t bad = 0u;
  if (!torb_text_try_from_bytes((const uint8_t *)gecos, length, &text, &bad)) {
    return false;
  }
  torb_text_release(*slot);
  *slot = text;
  return true;
}

int64_t torb_os_posix_account(
  int64_t *identifier,
  torb_text *name,
  torb_text *full_name,
  torb_text *home,
  torb_text *failure
) {
  const uid_t user = getuid();
  size_t capacity = 1024u;
  *identifier = (int64_t)user;
  for (;;) {
    char *buffer = (char *)torb_raw_allocate(capacity);
    struct passwd entry;
    struct passwd *found = NULL;
    const int code = getpwuid_r(user, &entry, buffer, capacity, &found);
    int64_t result = TORB_OS_SUCCESS;
    if (code == ERANGE && capacity < (size_t)1 << 20) {
      torb_raw_free(buffer, capacity);
      capacity *= 2u;
      continue;
    }
    if (code != 0) {
      result = torb_os_posix_failure(code, failure);
    } else if (found == NULL) {
      char message[64];
      snprintf(message, sizeof message, "there is no account entry for uid %lld", (long long)user);
      (void)torb_os_posix_answer(failure, message);
      result = TORB_OS_MISSING;
    } else if (!torb_os_posix_answer(name, entry.pw_name) || !torb_os_posix_answer(home, entry.pw_dir)
               || !torb_os_posix_answer_full_name(full_name, entry.pw_gecos == NULL ? "" : entry.pw_gecos)) {
      result = torb_os_posix_not_text("the account entry is not UTF-8", failure);
    }
    torb_raw_free(buffer, capacity);
    return result;
  }
}

#endif /* __unix__ || __APPLE__ */
