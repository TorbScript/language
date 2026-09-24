/*
 * epoll.c - the poller of the IO core on Linux (docs/design/NETWORK.md section 2, runtime/include/torb_posix_io.h). The
 * natives of `std/os` that only Linux has are `linux.c`'s.
 *
 * The whole file is one `#if defined(__linux__)`: it is compiled on every machine and is empty everywhere else, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it.
 *
 * **One epoll instance, level-triggered and one-shot**: a socket is armed for exactly the directions an operation waits
 * for, an event disarms it, and posix_io.c arms it again for what still waits. Level-triggered, because an operation is
 * armed only after its call would have blocked, and a socket that became ready in between must be reported at once.
 * The stop is an eventfd registered with no socket behind it. io_uring is the later speed change behind the same
 * interface (docs/design/CONCURRENCY.md section 7).
 */

/* POSIX 2008, which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "torb.h"

#if defined(__linux__)

#include "torb_posix_io.h"

#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static int torb_epoll = -1;
static int torb_epoll_wake = -1;

bool torb_io_poller_open(int64_t *failure) {
  struct epoll_event event;
  torb_epoll = epoll_create1(EPOLL_CLOEXEC);
  if (torb_epoll < 0) {
    *failure = torb_io_posix_failed(errno);
    return false;
  }
  torb_epoll_wake = eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK);
  if (torb_epoll_wake < 0) {
    *failure = torb_io_posix_failed(errno);
    (void)close(torb_epoll);
    torb_epoll = -1;
    return false;
  }
  event.events = EPOLLIN;
  event.data.ptr = NULL;
  if (epoll_ctl(torb_epoll, EPOLL_CTL_ADD, torb_epoll_wake, &event) != 0) {
    *failure = torb_io_posix_failed(errno);
    torb_io_poller_close();
    return false;
  }
  return true;
}

void torb_io_poller_close(void) {
  if (torb_epoll_wake >= 0) {
    (void)close(torb_epoll_wake);
    torb_epoll_wake = -1;
  }
  if (torb_epoll >= 0) {
    (void)close(torb_epoll);
    torb_epoll = -1;
  }
}

int64_t torb_io_poller_arm(torb_io_socket *socket) {
  struct epoll_event event;
  event.events = EPOLLONESHOT;
  if (socket->reading != NULL) {
    event.events |= EPOLLIN | EPOLLRDHUP;
  }
  if (socket->writing != NULL) {
    event.events |= EPOLLOUT;
  }
  event.data.ptr = socket;
  if (socket->registered == 0u) {
    if (epoll_ctl(torb_epoll, EPOLL_CTL_ADD, (int)socket->system, &event) != 0) {
      return torb_io_posix_failed(errno);
    }
    socket->registered = 1u;
    return 0;
  }
  if (epoll_ctl(torb_epoll, EPOLL_CTL_MOD, (int)socket->system, &event) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

void torb_io_poller_remove(torb_io_socket *socket) {
  struct epoll_event event;
  event.events = 0u;
  event.data.ptr = NULL;
  (void)epoll_ctl(torb_epoll, EPOLL_CTL_DEL, (int)socket->system, &event);
}

bool torb_io_poller_wait(void) {
  struct epoll_event events[64];
  bool going = true;
  int count = epoll_wait(torb_epoll, events, 64, -1);
  int index;
  if (count < 0) {
    return true;
  }
  for (index = 0; index < count; index += 1) {
    uint32_t happened = events[index].events;
    if (events[index].data.ptr == NULL) {
      uint64_t drained;
      (void)!read(torb_epoll_wake, &drained, sizeof drained);
      going = false;
      continue;
    }
    torb_io_posix_ready((torb_io_socket *)events[index].data.ptr,
                        (happened & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0u,
                        (happened & (EPOLLOUT | EPOLLHUP | EPOLLERR)) != 0u);
  }
  return going;
}

void torb_io_poller_wake(void) {
  uint64_t one = 1u;
  (void)!write(torb_epoll_wake, &one, sizeof one);
}

#endif /* __linux__ */
