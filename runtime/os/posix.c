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

#include "torb.h"

#if defined(__unix__) || defined(__APPLE__)

#include <unistd.h>

int64_t torb_os_posix_effective_user_identifier(void) {
  return (int64_t)geteuid();
}

#endif /* __unix__ || __APPLE__ */
