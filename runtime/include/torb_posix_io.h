/*
 * torb_posix_io.h - what the POSIX half of the IO core shares between `runtime/os/posix.c`, which makes the socket
 * calls, and the two readiness pollers that decide when to make them: epoll in `linux.c`, kqueue in `bsd.c`
 * (docs/design/NETWORK.md section 2). Included only by those three files, and empty everywhere but POSIX.
 */

#ifndef TORB_POSIX_IO_H
#define TORB_POSIX_IO_H

#if defined(__unix__) || defined(__APPLE__)

#include "torb_io.h"

#include <sys/socket.h>

/** What `torb_io_posix_perform` answers where the call would block: the poller registers the operation and waits. */
#define TORB_IO_POSIX_AGAIN INT64_MIN

/**
 * Makes the operation's call once, non-blocking: the answer (zero or more), a packed failure, or `TORB_IO_POSIX_AGAIN`.
 * A send goes on until everything is written or the socket is full; a connect starts the connection the first time and
 * asks how it ended the second.
 */
int64_t torb_io_posix_perform(torb_io_operation *operation);

/** Whether the operation waits for the socket to become writable (a send, a connect) rather than readable. */
bool torb_io_posix_wants_writable(const torb_io_operation *operation);

/** A failure out of an `errno` value. */
int64_t torb_io_posix_failed(int code);

/** The system's form of an address; answers its length. */
socklen_t torb_io_posix_address_of(const torb_io_address *address, struct sockaddr_storage *out);

/**
 * The IO thread found `socket` readable, writable or both (an error or a hang-up counts as both): the operations that
 * wait for that make their call again, and the socket is armed again for whatever still waits. posix.c's, called by
 * `torb_io_poller_wait`.
 */
void torb_io_posix_ready(torb_io_socket *socket, bool readable, bool writable);

/* -------------------------------------------------------------- what linux.c (epoll) and bsd.c (kqueue) give --- */

/** Makes the poller and what wakes it. False where the system refused, with the failure in `*failure`. */
bool torb_io_poller_open(int64_t *failure);
void torb_io_poller_close(void);
/**
 * Registers `socket`, one-shot, for the directions its operations wait for (`reading`: readable, `writing`: writable),
 * and for nothing where none waits. The socket's lock held. 0, or a packed failure.
 */
int64_t torb_io_poller_arm(torb_io_socket *socket);
/** Takes `socket` out of the poller before its descriptor is closed. */
void torb_io_poller_remove(torb_io_socket *socket);
/**
 * Waits for events, without a limit, and hands each to `torb_io_posix_ready`. False once `torb_io_poller_wake` asked the
 * IO thread to stop.
 */
bool torb_io_poller_wait(void);
/** Makes the IO thread's `torb_io_poller_wait` answer false. */
void torb_io_poller_wake(void);

#endif /* __unix__ || __APPLE__ */

#endif /* TORB_POSIX_IO_H */
