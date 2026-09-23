/*
 * macos.c - the natives of `std/os` that only macOS has (`native type MacOs`, docs/design/OS.md section 7): the Mach
 * host statistics, `_NSGetExecutablePath`, the per-user temporary directory. None is needed yet; the file is the place
 * they go.
 *
 * The whole file is one `#if defined(__APPLE__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 */

#include "torb.h"

#if defined(__APPLE__)

#endif /* __APPLE__ */
