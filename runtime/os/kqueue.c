/*
 * kqueue.c - the poller of the IO core on macOS and FreeBSD (docs/design/NETWORK.md section 2,
 * runtime/include/torb_posix_io.h). The natives of `std/os` the two share are `bsd.c`'s.
 *
 * The whole file is one `#if`: it is compiled on every machine and is empty everywhere but macOS and FreeBSD, so the
 * build compiles every file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it. No feature
 * macro is defined, as in `bsd.c`: `kqueue` is no POSIX function, and a strict POSIX request would hide it.
 *
 * **One kqueue, a one-shot filter per direction**: `EVFILT_READ` for an operation that reads, `EVFILT_WRITE` for one
 * that writes, each added with `EV_ONESHOT` where it waits and deleted where it does not. Each change is its own call,
 * because a change that fails (deleting a filter that is not there) would otherwise stop the ones after it. The stop is
 * an `EVFILT_USER` event, which both systems have.
 */

#include "torb.h"

#if defined(__APPLE__) || defined(__FreeBSD__)

#include "torb_posix_io.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static int torb_kqueue = -1;

/* The one identifier of the user event that stops the IO thread. */
#define TORB_KQUEUE_STOP 1

/* One change, alone. 0, or a packed failure. */
static int64_t torb_kqueue_change(uintptr_t identifier, short filter, unsigned short flags, unsigned int filter_flags,
                                  void *data) {
  struct kevent change;
  EV_SET(&change, identifier, filter, flags, filter_flags, 0, data);
  if (kevent(torb_kqueue, &change, 1, NULL, 0, NULL) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

bool torb_io_poller_open(int64_t *failure) {
  int64_t added;
  torb_kqueue = kqueue();
  if (torb_kqueue < 0) {
    *failure = torb_io_posix_failed(errno);
    return false;
  }
  (void)fcntl(torb_kqueue, F_SETFD, FD_CLOEXEC);
  added = torb_kqueue_change(TORB_KQUEUE_STOP, EVFILT_USER, EV_ADD | EV_CLEAR, 0u, NULL);
  if (added != 0) {
    *failure = added;
    torb_io_poller_close();
    return false;
  }
  return true;
}

void torb_io_poller_close(void) {
  if (torb_kqueue >= 0) {
    (void)close(torb_kqueue);
    torb_kqueue = -1;
  }
}

int64_t torb_io_poller_arm(torb_io_socket *socket) {
  uintptr_t descriptor = (uintptr_t)socket->system;
  int64_t answer = 0;
  if (socket->reading != NULL) {
    answer = torb_kqueue_change(descriptor, EVFILT_READ, EV_ADD | EV_ONESHOT, 0u, socket);
  } else {
    (void)torb_kqueue_change(descriptor, EVFILT_READ, EV_DELETE, 0u, NULL);
  }
  if (answer == 0 && socket->writing != NULL) {
    answer = torb_kqueue_change(descriptor, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0u, socket);
  } else if (socket->writing == NULL) {
    (void)torb_kqueue_change(descriptor, EVFILT_WRITE, EV_DELETE, 0u, NULL);
  }
  socket->registered = 1u;
  return answer;
}

void torb_io_poller_remove(torb_io_socket *socket) {
  (void)torb_kqueue_change((uintptr_t)socket->system, EVFILT_READ, EV_DELETE, 0u, NULL);
  (void)torb_kqueue_change((uintptr_t)socket->system, EVFILT_WRITE, EV_DELETE, 0u, NULL);
}

bool torb_io_poller_wait(void) {
  struct kevent events[64];
  bool going = true;
  int count = kevent(torb_kqueue, NULL, 0, events, 64, NULL);
  int index;
  if (count < 0) {
    return true;
  }
  for (index = 0; index < count; index += 1) {
    if (events[index].filter == EVFILT_USER) {
      going = false;
      continue;
    }
    if (events[index].udata == NULL) {
      continue;
    }
    torb_io_posix_ready((torb_io_socket *)events[index].udata, events[index].filter == EVFILT_READ,
                        events[index].filter == EVFILT_WRITE);
  }
  return going;
}

void torb_io_poller_wake(void) {
  (void)torb_kqueue_change(TORB_KQUEUE_STOP, EVFILT_USER, 0u, NOTE_TRIGGER, NULL);
}

#endif /* __APPLE__ || __FreeBSD__ */
