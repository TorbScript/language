/*
 * browser.c - the poller of the IO core in a web browser, where the playground's `torb` runs as WebAssembly compiled by
 * emscripten (docs/design/RELEASE.md section 6, `playground/build.sh`). A page has no sockets and no pipes of a child,
 * so the poller never opens: every operation of `std/network` fails with "not supported" before it waits for anything.
 * A program the playground runs cannot reach one anyway - an import of `std/network` or `std/process` is refused
 * there - and this file is what lets the compiler's own C, which reaches them, link.
 *
 * The whole file is one `#if defined(__EMSCRIPTEN__)`, like every file of `runtime/os/`: it is compiled on every
 * machine and is empty everywhere else.
 */

/* POSIX 2008, which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "torb.h"

#if defined(__EMSCRIPTEN__)

#include "torb_posix_io.h"

#include <errno.h>
#include <stdint.h>

bool torb_io_poller_open(int64_t *failure) {
  *failure = torb_io_posix_failed(ENOTSUP);
  return false;
}

void torb_io_poller_close(void) {
}

int64_t torb_io_poller_arm(torb_io_socket *socket) {
  (void)socket;
  return torb_io_posix_failed(ENOTSUP);
}

void torb_io_poller_remove(torb_io_socket *socket) {
  (void)socket;
}

bool torb_io_poller_wait(void) {
  return false;
}

void torb_io_poller_wake(void) {
}

#endif /* __EMSCRIPTEN__ */
