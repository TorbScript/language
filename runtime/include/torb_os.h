/*
 * torb_os.h - the natives of `std/os`, one family per file of `runtime/os/` (docs/design/OS.md section 7). Included from
 * torb.h; nothing includes it directly.
 *
 * Every prototype is declared on every machine, whichever system the file that defines it is compiled for. A function
 * of `runtime/os/windows.c` exists only in a Windows build, but its signature is compared with the manifest's
 * (`torb_natives.h`, by `tests/natives_header_test.c`) on Linux as well, so the two halves cannot drift apart unseen.
 *
 * A call of one of these on a system it does not exist on never reaches the C compiler: the manifest names the systems
 * of every row (`availableOn`), and the compiler reports the call as an error of the program for that target
 * (`torb check --every-target` on any machine).
 *
 * The names are `torb_os_<family>_<name>`: the family is the file, the name the member of its `native type` in
 * `std/os`.
 */

#ifndef TORB_OS_H
#define TORB_OS_H

#include <stdint.h>

/* ---------------------------------------------------------------------------------------- windows.c, on Windows --- */

/** Milliseconds since the system started, from `GetTickCount64`: what `System.uptime` is made of. */
int64_t torb_os_windows_tick_count(void);

/* ------------------------------------------------------------------ posix.c, on Linux, macOS and FreeBSD --- */

/** The effective user id of the process, from `geteuid`. */
int64_t torb_os_posix_effective_user_identifier(void);

#endif /* TORB_OS_H */
