/*
 * torb_io.h - the IO core: what `runtime/io.c` and the three pollers of `runtime/os/` share (docs/design/NETWORK.md
 * section 2). The generated C never includes it: the natives of `std/network` are declared in `torb.h`, and everything
 * here can change without the compiler knowing.
 *
 * # The shape
 *
 * A socket is a **record** (`torb_io_socket`): the descriptor of the system, a count of its own, a lock, and what was
 * received and not taken yet. A program holds it through a **handle**, an `Int64` of the table in `io.c`, and never
 * through a pointer.
 *
 * A read, a write, an accept, a connect and a name resolution are each an **operation** (`torb_io_operation`): a record
 * that owns the buffer the kernel writes into or reads from, holds its socket, and has a waiter - the task of the
 * runtime that `io.c` made for it. Its count is two while it is in flight, the task's and the kernel's, and whoever lets
 * go last frees it: so a task that is cancelled while it waits stops at once, and the kernel may still write into the
 * operation's buffer until it lets go (section 3 of the record).
 *
 * `io.c` owns the handles, the operations, the tasks and the resolver threads; a poller owns the IO thread and every
 * call of the system. The poller of a system is the file of that system in `runtime/os/`, one `#if` from its first line
 * to its last: IOCP and Winsock in `windows.c`, epoll in `linux.c`, kqueue in `bsd.c`, and the POSIX calls the last two
 * share in `posix.c`.
 *
 * # Threads
 *
 * The **IO thread** waits on the poller and completes operations; it runs no TorbScript, allocates nothing counted (every
 * record here is `malloc`ed) and wakes a task only through `torb_task_io_done`. The **resolver threads** run
 * `getaddrinfo`, at most four, started on the first name that is not a literal. A **worker** makes operations, submits
 * them, and finishes them after its task was woken.
 */

#ifndef TORB_IO_H
#define TORB_IO_H

#include "torb_pool.h"

/* ------------------------------------------------------------------------------------------- failures --- */

/**
 * The kind of a failure, packed with the system's code into a negative answer: `-(kind << 32 | code)`. `std/network`
 * unpacks it into a `NetworkError`; the numbers are that package's too, so they never change.
 */
typedef enum torb_io_failure {
  TORB_IO_OTHER = 1,
  TORB_IO_REFUSED = 2,
  TORB_IO_RESET = 3,
  TORB_IO_ABORTED = 4,
  TORB_IO_TIMED_OUT = 5,
  TORB_IO_ADDRESS_IN_USE = 6,
  TORB_IO_ADDRESS_NOT_AVAILABLE = 7,
  TORB_IO_HOST_NOT_FOUND = 8,
  TORB_IO_UNREACHABLE = 9,
  TORB_IO_CLOSED = 10,
  TORB_IO_INVALID = 11
} torb_io_failure;

/** A negative answer out of a kind and the system's code (0 where there is none). */
static inline int64_t torb_io_failed(torb_io_failure kind, uint32_t code) {
  return -(int64_t)(((uint64_t)kind << 32) | (uint64_t)code);
}

/* ------------------------------------------------------------------------------------------- addresses --- */

/** An IP address and a port, as the natives pass them: the family is 4 or 6, the bytes in network order. */
typedef struct torb_io_address {
  int32_t family;
  uint16_t port;
  uint8_t bytes[16];
} torb_io_address;

/* ------------------------------------------------------------------------------------------- records --- */

typedef enum torb_io_record_kind {
  TORB_IO_STREAM = 1,
  TORB_IO_LISTENER = 2,
  TORB_IO_RESOLUTION = 3,
  /** A UDP socket: bound, maybe connected to one peer, its received datagrams queued whole. */
  TORB_IO_DATAGRAM = 4,
  /**
   * A pipe of a child process, on a system whose poller takes one (epoll and kqueue): read and written like a stream,
   * with `read` and `write` instead of `recv` and `send`. runtime/stream.c holds its handle.
   */
  TORB_IO_PIPE = 5
} torb_io_record_kind;

typedef struct torb_io_operation torb_io_operation;
typedef struct torb_io_socket torb_io_socket;

/** A datagram that arrived and no receive took yet: where it came from, and its bytes right after the record. */
typedef struct torb_io_datagram {
  struct torb_io_datagram *next;
  torb_io_address from;
  size_t length;
  uint8_t bytes[];
} torb_io_datagram;

struct torb_io_socket {
  /** References: the table's while it has a handle, one per operation in flight, one while the poller needs it. */
  uint32_t count;
  /** A spin lock: `pending`, `accepted`, `closed`, and on POSIX `reading`, `writing` and the registration. */
  uint32_t lock;
  uint8_t kind;
  /** `torb_network_close` ran: no new operation starts, and the ones in flight fail. */
  uint8_t closed;
  int32_t family;
  /** The descriptor of the system: a `SOCKET` on Windows, a file descriptor elsewhere; -1 for none. */
  int64_t system;
  /** A stream: bytes a receive delivered that `torb_network_take_received` has not taken yet. */
  uint8_t *pending;
  size_t pending_length;
  size_t pending_capacity;
  /** A listener: connections an accept completed and no accept took yet, oldest first, through `accepted_next`. */
  torb_io_socket *accepted_first;
  torb_io_socket *accepted_last;
  torb_io_socket *accepted_next;
  /** A resolution: the addresses, `address_count` of them. */
  torb_io_address *addresses;
  size_t address_count;
  /** A datagram socket: what arrived and no receive took yet, oldest first, each `malloc`ed whole. */
  torb_io_datagram *datagram_first;
  torb_io_datagram *datagram_last;
  /** A datagram socket is connected to one peer: a send without an address goes there, and nothing else arrives. */
  uint8_t connected;
  /* ---- the readiness pollers' (epoll, kqueue) ---- */
  /**
   * The operation that waits for the socket to become readable, and the one that waits for it to become writable. A
   * listener has one waiting accept per accept loop: `reading` is the oldest, the others follow through `queued_next`.
   */
  torb_io_operation *reading;
  torb_io_operation *writing;
  /** Registered with the poller at all. */
  uint8_t registered;
  /** Calls on the descriptor in progress: a close waits for them, so a descriptor is never reused under a call. */
  uint32_t performing;
  /** Freed by the IO thread once no event it holds can name it any more (`torb_io_system_forget`). */
  torb_io_socket *retired_next;
};

typedef enum torb_io_operation_kind {
  TORB_IO_ACCEPT = 1,
  TORB_IO_CONNECT = 2,
  TORB_IO_RECEIVE = 3,
  TORB_IO_SEND = 4,
  TORB_IO_RESOLVE = 5,
  /** A datagram in, whole, and where it came from in `address`; the buffer holds the largest datagram there is. */
  TORB_IO_RECEIVE_DATAGRAM = 6,
  /** A datagram out, whole, to `address` - or to the connected peer where its family is 0. */
  TORB_IO_SEND_DATAGRAM = 7
} torb_io_operation_kind;

struct torb_io_operation {
  /** The system's part: an `OVERLAPPED` on Windows, which has to come first to be found from a completion. */
  _Alignas(16) uint8_t system[64];
  /** What task.c reads: the lock, whether it completed, and the task that waits for it. */
  torb_io_waiting waiting;
  /** 2 while in flight (the task's and the kernel's), 1 before it was submitted and after either let go. */
  uint32_t count;
  uint8_t kind;
  /** The socket it works on (a reference), or the listener of an accept; `NULL` for a resolution. */
  torb_io_socket *socket;
  /** A receive: where the bytes go, `capacity` of them. A send: what goes, `length` bytes from `offset`. */
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  size_t offset;
  /** The answer: zero or more, or a packed failure. Written once, by whoever completes it. */
  int64_t result;
  /** An accept: the socket the connection arrives on (a reference). */
  torb_io_socket *accepted;
  /** A connect and a datagram sent: where to. A datagram received: where it came from. */
  torb_io_address address;
  /** A datagram received on IOCP: the system's form of its sender, which the kernel writes after the call returned. */
  _Alignas(8) uint8_t peer[128];
  int32_t peer_length;
  /** A resolution: the host, NUL terminated, and what it resolved to. */
  char *host;
  torb_io_address *addresses;
  size_t address_count;
  /** The queue it waits in: the resolver threads' for a resolution, a listener's accepts on epoll and kqueue. */
  torb_io_operation *queued_next;
  uint8_t in_queue;
  /**
   * Its waiter asked for it to be cancelled (IOCP). An accept whose connection was gone before it finished is made
   * again for the next connection, but not once this is set - and a cancel that came while the new `AcceptEx` was being
   * made is repeated after it. Written and read in the one total order of the sequentially consistent operations.
   */
  uint32_t cancelling;
};

/* ------------------------------------------------------------------------------ what io.c gives the pollers --- */

/**
 * The operation is done, with `result` (zero or more, or a packed failure): what the kernel delivered is moved to where
 * it belongs (received bytes to the socket, an accepted connection to the listener), the waiter is woken, and the
 * kernel's reference is dropped. From any thread, exactly once per submitted operation.
 */
void torb_io_complete(torb_io_operation *operation, int64_t result);

/** One reference to the operation less; the last one frees it. */
void torb_io_operation_release(torb_io_operation *operation);

/** A new record of `kind` over `system` (-1 for none), count 1. */
torb_io_socket *torb_io_socket_new(torb_io_record_kind kind, int32_t family, int64_t system);
void torb_io_socket_retain(torb_io_socket *socket);
/** One reference less; the last one closes the descriptor where it is still open and forgets the record. */
void torb_io_socket_release(torb_io_socket *socket);
/** Frees the record now: what `torb_io_system_forget` does, at once or on the IO thread. */
void torb_io_socket_free(torb_io_socket *socket);

/** How many operations are alive, for the stop of the core and the tests. */
int64_t torb_io_operations_alive(void);

/* ----------------------------------------------------------------- what io.c gives the stream layer (stream.c) --- */

/*
 * The pipes of a child process (`Process.start`), where the system's poller takes them: on Linux, macOS and FreeBSD a
 * pipe is a descriptor epoll and kqueue watch like a socket, so a read that waits holds no thread and a cancelled one
 * stops at once. On Windows the pipes of a child are anonymous pipes without overlapped IO, which no completion port
 * takes, and the stream layer reads them on the blocking pool as before. A task of a pipe answers what the stream
 * layer's tasks answer: a count, 0 at the end, or minus the system's code (`EBADF` for a pipe that was closed).
 */

/**
 * The pipe `descriptor` of a child, made non-blocking and a record of the poller: its handle, which the stop of the core
 * closes with every other, or 0 where the poller takes no pipes or refused this one - the caller keeps the descriptor
 * then. A pipe it answers a handle for is the core's: closing the handle closes the descriptor.
 */
int64_t torb_io_pipe_adopt(int64_t descriptor);
/** A task that reads at most `maximum` bytes of the pipe, as soon as any arrived, into the pipe's own buffer. */
torb_task *torb_io_pipe_read(int64_t pipe, int64_t maximum);
/** What the reads of the pipe got and nobody took yet, appended to a list of bytes. */
void torb_io_pipe_take_read(int64_t pipe, torb_list *into);
/** A task that writes all the bytes of `bytes` from `from` on into the pipe, and answers their count. */
torb_task *torb_io_pipe_write(int64_t pipe, torb_list bytes, int64_t from);
/** Closes the pipe: an operation that waits on it answers `EBADF`. A handle that is closed already is nothing. */
void torb_io_pipe_close(int64_t pipe);

/* ------------------------------------------------------------------------------ what a poller gives io.c --- */

/**
 * Starts the poller and the IO thread; the first socket asks for it, and the stop of the pool undoes it. False where the
 * system refused, with a packed failure in `*failure`.
 */
bool torb_io_system_start(int64_t *failure);
/** Wakes the IO thread and joins it. Nothing is in flight any more when it is called. */
void torb_io_system_stop(void);

/**
 * A socket of `family` - a TCP stream, or with `datagram` a UDP socket - non-blocking and known to the poller. The
 * descriptor, or a packed failure.
 */
int64_t torb_io_system_socket(int32_t family, bool datagram);
/** Binds `socket` to `address` and listens with `backlog`. 0, or a packed failure. */
int64_t torb_io_system_listen(torb_io_socket *socket, const torb_io_address *address, int64_t backlog);
/** Binds the datagram socket `socket` to `address`. 0, or a packed failure. */
int64_t torb_io_system_bind(torb_io_socket *socket, const torb_io_address *address);
/**
 * Connects the datagram socket `socket` to `address`: a send without an address goes there, and only its datagrams
 * arrive. Never waits. 0, or a packed failure.
 */
int64_t torb_io_system_connect_datagram(torb_io_socket *socket, const torb_io_address *address);
/**
 * Starts `operation`, which holds its two references. Its completion comes through `torb_io_complete`, from the IO thread
 * or from this call itself where the system answered at once.
 */
void torb_io_system_submit(torb_io_operation *operation);
/** The waiter of `operation` was cancelled: the kernel is asked to give it up. Its completion still comes. */
void torb_io_system_cancel(torb_io_operation *operation);
/** No more sending on `socket` (`shutdown` for writing). 0, or a packed failure. */
int64_t torb_io_system_shutdown(torb_io_socket *socket);
/**
 * Readies the pipe `descriptor` of a child for the poller: non-blocking, so a read or a write that would wait is
 * registered like one of a socket. 0, or a packed failure - always on Windows, whose completion port takes no anonymous
 * pipe.
 */
int64_t torb_io_system_pipe(int64_t descriptor);
/** Closes the descriptor. Every operation in flight on it completes, failed. Called once, with the record alive. */
void torb_io_system_close(torb_io_socket *socket);
/** The record's last reference went: freed at once, or once the IO thread holds no event that could name it. */
void torb_io_system_forget(torb_io_socket *socket);
/** The address `socket` is bound to, or with `peer` the one it is connected to. 0, or a packed failure. */
int64_t torb_io_system_address(torb_io_socket *socket, bool peer, torb_io_address *out);
/**
 * Resolves `host` (NUL terminated), blocking: on a resolver thread. The count of addresses in `*out` (`malloc`ed, freed
 * by the caller), or a packed failure.
 */
int64_t torb_io_system_resolve(const char *host, torb_io_address **out);
/** The system's words for its `code`, into `buffer` of `size` bytes, NUL terminated. */
void torb_io_system_error_text(uint32_t code, char *buffer, size_t size);
/** The kind of the system's error `code`. */
torb_io_failure torb_io_system_failure_kind(uint32_t code);
/**
 * The name servers the system is configured with, at most `capacity` of them into `out`, on port 53: the servers of the
 * adapters that are up on Windows, the `nameserver` lines of `/etc/resolv.conf` elsewhere - and there the local machine
 * where the file names none, as resolv.conf(5) says. Blocking, but short. The count, or a packed failure.
 */
int64_t torb_io_system_name_servers(torb_io_address *out, size_t capacity);

#endif /* TORB_IO_H */
