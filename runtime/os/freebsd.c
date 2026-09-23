/*
 * freebsd.c - the natives of `std/os` that only FreeBSD has (`native type FreeBsd`, docs/design/OS.md section 7): the
 * numeric `sysctl` of the executable path, the swap devices, `cpuset_getaffinity`. None is needed yet; the file is the
 * place they go.
 *
 * The whole file is one `#if defined(__FreeBSD__)`: it is compiled on every machine and is empty everywhere else, so
 * the build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. Its
 * prototypes are in `torb_os.h` on every machine.
 */

#include "torb.h"

#if defined(__FreeBSD__)

#endif /* __FreeBSD__ */
