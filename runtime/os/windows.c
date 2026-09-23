/*
 * windows.c - the natives of `std/os` that only Windows has (`native type Windows`, docs/design/OS.md section 7).
 *
 * The whole file is one `#if defined(_WIN32)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 *
 * Every text crosses as UTF-8 and every call of the system is the wide one, as in `platform.c`.
 */

#include "torb.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int64_t torb_os_windows_tick_count(void) {
  return (int64_t)GetTickCount64();
}

#endif /* _WIN32 */
