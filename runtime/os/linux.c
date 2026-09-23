/*
 * linux.c - the natives of `std/os` that only Linux has (`native type Linux`, docs/design/OS.md section 7): reading
 * `/proc` and `/sys`, `getauxval`, the affinity mask. None is needed yet; the file is the place they go.
 *
 * The whole file is one `#if defined(__linux__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 */

#include "torb.h"

#if defined(__linux__)

#endif /* __linux__ */
