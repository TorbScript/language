/*
 * browser.c - the IO core in a web browser, where the playground's `torb` runs as WebAssembly compiled by emscripten
 * (docs/design/RELEASE.md section 6, `playground/build.sh`): the poller, which never opens, and standard input fed by
 * the page, which is how the language server of the playground hears its client.
 *
 * **The poller.** A page has no sockets and no pipes of a child, so every operation of `std/network` fails with "not
 * supported" before it waits for anything. A program the playground runs cannot reach one anyway - an import of
 * `std/network` or `std/process` is refused there - and this file is what lets the compiler's own C, which reaches
 * them, link.
 *
 * **Standard input from the page.** Nothing in a browser may block: a message reaches a worker only once its thread is
 * back in the worker's event loop. So standard input follows the rule of a non-blocking descriptor. The page gives
 * emscripten a `stdin` callback that answers the next byte, `undefined` where it has none yet - a read then fails with
 * `EAGAIN` - or `null` at the end. A read of `standardInput()` that meets `EAGAIN` (runtime/stream.c) waits here, as an
 * operation of the IO core; and where the scheduler would sleep while it waits, it returns to the page instead
 * (task.c): the first time out of `main`, whose stack it gives up, with the program alive. The page then hands in bytes
 * and calls `torb_browser_resume(1)`, or calls `torb_browser_resume(0)` once the time it was told has passed, and the
 * scheduler goes on where it stopped until it waits for the page again.
 *
 * `torb lsp` in the playground's language worker is the same server as everywhere, over the same standard input and
 * output: this is its transport, and nothing of the server knows it runs in a page. A page whose `stdin` answers `null`
 * at once - a run of the playground - sees what it always saw, standard input at its end.
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

#include "torb_pool.h"
#include "torb_posix_io.h"

#include <emscripten/em_macros.h>
#include <emscripten/eventloop.h>
#include <errno.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------------------------- the poller --- */

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

/* ----------------------------------------------------------------------------- standard input from the page --- */

/* The one wait for the page: a read of standard input that found nothing waits here until the page hands in more. */
static torb_io_waiting torb_browser_input;

bool torb_browser_input_waited(void) {
  return torb_browser_input.waiter != NULL;
}

bool torb_browser_is_input(const torb_io_waiting *waiting) {
  return waiting == &torb_browser_input;
}

torb_wait torb_browser_wait_input(torb_task *self) {
  /* Every wait is a new one: what the page handed in before was read already, or the read would not wait */
  torb_browser_input.done = 0u;
  return torb_task_wait_io(self, &torb_browser_input);
}

void torb_browser_unwind(void) {
  emscripten_unwind_to_js_event_loop();
}

/**
 * The page's call, after it handed in standard input (`input` 1) or once the time the last call answered has passed
 * (`input` 0): the program runs until it waits for the page again, and the answer is the milliseconds until its first
 * timer is due - when the page calls again with 0 - or -1 where only more input can wake it. A program that ends ends
 * the runtime as `exit` does, which emscripten reports to the page by throwing its `ExitStatus`.
 */
EMSCRIPTEN_KEEPALIVE int32_t torb_browser_resume(int32_t input) {
  int64_t span = -1;
  torb_task *until = NULL;
  if (input != 0 && torb_browser_input.waiter != NULL) {
    torb_task_io_done(&torb_browser_input);
  }
  if (!torb_scheduler_resume(&span, &until)) {
    /* What `main` would have done after its scheduler, whose stack the first return to the page took */
    torb_process_exit(until != NULL ? torb_task_end_main(until) : 0);
  }
  if (span < 0) {
    return -1;
  }
  span = (span + 999999) / 1000000;
  return span > INT32_MAX ? INT32_MAX : (int32_t)span;
}

#endif /* __EMSCRIPTEN__ */
