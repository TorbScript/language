/*
 * posix_io.c - the socket calls of the IO core that Linux, macOS and FreeBSD make alike, the reads and writes of the
 * pipes of a child (which the pollers watch like sockets), and the logic both readiness pollers share
 * (docs/design/NETWORK.md section 2, runtime/include/torb_posix_io.h). The pollers - epoll in `epoll.c`,
 * kqueue in `kqueue.c` - decide *when* a call is made; this file makes it. The natives of `std/os` that every POSIX
 * system shares are `posix.c`'s.
 *
 * The whole file is one `#if`: it is compiled on every machine and is empty on Windows, so the build compiles every
 * file of `runtime/os/` without choosing, and no function has an `#ifdef` inside it - except the two spellings of
 * "no `SIGPIPE`", which are one macro each at the top.
 *
 * **Every socket and every pipe of the core is non-blocking**, and a call that would block answers `TORB_IO_POSIX_AGAIN`:
 * the poller registers the operation and makes the call again once the socket is ready. Readiness is level-triggered, so a socket that became
 * ready between the call and the registration is reported at once.
 */

/* POSIX 2008, which a strict `-std=c11` hides. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
/* On macOS `_POSIX_C_SOURCE` alone hides every Darwin extension (`_SC_NPROCESSORS_ONLN`, `SO_NOSIGPIPE`,
 * `pthread_cond_timedwait_relative_np`); `_DARWIN_C_SOURCE` shows them again beside POSIX. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE
#endif

#include "torb.h"

#if defined(__unix__) || defined(__APPLE__)

#include "torb_io.h"
#include "torb_posix_io.h"

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* A write to a socket whose peer is gone fails with `EPIPE` instead of killing the process: a flag per call on Linux
   and FreeBSD, an option per socket on macOS. */
#if defined(MSG_NOSIGNAL)
#  define TORB_POSIX_SEND_FLAGS MSG_NOSIGNAL
#else
#  define TORB_POSIX_SEND_FLAGS 0
#endif
#if defined(SO_NOSIGPIPE)
#  define TORB_POSIX_NO_SIGPIPE_OPTION SO_NOSIGPIPE
#else
#  define TORB_POSIX_NO_SIGPIPE_OPTION 0
#endif

/* Two more answers of `getaddrinfo` that say a name has no address: a name server that knows the name and has no A or
   AAAA record for it, and none of the family asked for. musl, macOS and FreeBSD name them where they answer them;
   glibc names them only for `_GNU_SOURCE`, which this file does not ask for, and answers them all the same - with the
   values below. Where a system has neither, "no such name" stands in, which the resolver compares with anyway. */
#if defined(EAI_NODATA)
#  define TORB_POSIX_NO_ADDRESS EAI_NODATA
#elif defined(__GLIBC__)
#  define TORB_POSIX_NO_ADDRESS (-5)
#else
#  define TORB_POSIX_NO_ADDRESS EAI_NONAME
#endif
#if defined(EAI_ADDRFAMILY)
#  define TORB_POSIX_NO_FAMILY EAI_ADDRFAMILY
#elif defined(__GLIBC__)
#  define TORB_POSIX_NO_FAMILY (-9)
#else
#  define TORB_POSIX_NO_FAMILY EAI_NONAME
#endif

/* Linux passes the errors already pending on a connection through the `accept` that takes it, and names them in
   accept(2): those of TCP/IP. BSD and macOS report a connection that is gone before its accept as `ECONNABORTED` alone. */
#if defined(__linux__)
#  define TORB_POSIX_PENDING_NETWORK_ERROR(code)                                                                   \
    ((code) == ENETDOWN || (code) == ENOPROTOOPT || (code) == EHOSTDOWN || (code) == ENONET                        \
     || (code) == EHOSTUNREACH || (code) == EOPNOTSUPP || (code) == ENETUNREACH)
#else
#  define TORB_POSIX_PENDING_NETWORK_ERROR(code) false
#endif

/* ------------------------------------------------------------------------------------------------- failures --- */

torb_io_failure torb_io_system_failure_kind(uint32_t code) {
  switch ((int)code) {
    case ECONNREFUSED:
      return TORB_IO_REFUSED;
    case ECONNRESET:
    case EPIPE:
    case ENETRESET:
      return TORB_IO_RESET;
    case ECONNABORTED:
    case ECANCELED:
      return TORB_IO_ABORTED;
    case ETIMEDOUT:
      return TORB_IO_TIMED_OUT;
    case EADDRINUSE:
      return TORB_IO_ADDRESS_IN_USE;
    case EADDRNOTAVAIL:
      return TORB_IO_ADDRESS_NOT_AVAILABLE;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
      return TORB_IO_UNREACHABLE;
    case EBADF:
    case ENOTSOCK:
      return TORB_IO_CLOSED;
    case EINVAL:
    case EAFNOSUPPORT:
      return TORB_IO_INVALID;
    default:
      return TORB_IO_OTHER;
  }
}

int64_t torb_io_posix_failed(int code) {
  return torb_io_failed(torb_io_system_failure_kind((uint32_t)code), (uint32_t)code);
}

void torb_io_system_error_text(uint32_t code, char *buffer, size_t size) {
  if (size == 0u) {
    return;
  }
  buffer[0] = '\0';
  /* The XSI `strerror_r`, which answers an `int`: `_POSIX_C_SOURCE` without `_GNU_SOURCE` selects it on glibc */
  if (strerror_r((int)code, buffer, size) != 0 || buffer[0] == '\0') {
    snprintf(buffer, size, "system error %u", (unsigned)code);
  }
}

/* ------------------------------------------------------------------------------------------------ addresses --- */

static uint16_t torb_posix_network_order(uint16_t port) {
  return (uint16_t)((port >> 8) | (port << 8));
}

socklen_t torb_io_posix_address_of(const torb_io_address *address, struct sockaddr_storage *out) {
  memset(out, 0, sizeof *out);
  if (address->family == 4) {
    struct sockaddr_in *version4 = (struct sockaddr_in *)(void *)out;
    version4->sin_family = AF_INET;
    version4->sin_port = torb_posix_network_order(address->port);
    memcpy(&version4->sin_addr, address->bytes, 4u);
    return (socklen_t)sizeof(struct sockaddr_in);
  }
  {
    struct sockaddr_in6 *version6 = (struct sockaddr_in6 *)(void *)out;
    version6->sin6_family = AF_INET6;
    version6->sin6_port = torb_posix_network_order(address->port);
    memcpy(&version6->sin6_addr, address->bytes, 16u);
    return (socklen_t)sizeof(struct sockaddr_in6);
  }
}

static bool torb_posix_address_from(const struct sockaddr *address, torb_io_address *out) {
  memset(out, 0, sizeof *out);
  if (address->sa_family == AF_INET) {
    const struct sockaddr_in *version4 = (const struct sockaddr_in *)(const void *)address;
    out->family = 4;
    out->port = torb_posix_network_order(version4->sin_port);
    memcpy(out->bytes, &version4->sin_addr, 4u);
    return true;
  }
  if (address->sa_family == AF_INET6) {
    const struct sockaddr_in6 *version6 = (const struct sockaddr_in6 *)(const void *)address;
    out->family = 6;
    out->port = torb_posix_network_order(version6->sin6_port);
    memcpy(out->bytes, &version6->sin6_addr, 16u);
    return true;
  }
  return false;
}

/* -------------------------------------------------------------------------------------------------- sockets --- */

/* A descriptor made non-blocking, closed on `exec`, and - where the system has the option - without `SIGPIPE`. */
static bool torb_posix_prepare(int descriptor) {
  int flags = fcntl(descriptor, F_GETFL, 0);
  int yes = 1;
  if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
    return false;
  }
  (void)fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  if (TORB_POSIX_NO_SIGPIPE_OPTION != 0) {
    (void)setsockopt(descriptor, SOL_SOCKET, TORB_POSIX_NO_SIGPIPE_OPTION, &yes, (socklen_t)sizeof yes);
  }
  return true;
}

int64_t torb_io_system_socket(int32_t family, bool datagram) {
  int descriptor = socket(family == 6 ? AF_INET6 : AF_INET, datagram ? SOCK_DGRAM : SOCK_STREAM, 0);
  if (descriptor < 0) {
    return torb_io_posix_failed(errno);
  }
  if (!torb_posix_prepare(descriptor)) {
    int code = errno;
    (void)close(descriptor);
    return torb_io_posix_failed(code);
  }
  return (int64_t)descriptor;
}

int64_t torb_io_system_listen(torb_io_socket *socket, const torb_io_address *address, int64_t backlog) {
  int descriptor = (int)socket->system;
  struct sockaddr_storage system;
  socklen_t length = torb_io_posix_address_of(address, &system);
  int yes = 1;
  /* A server that restarts binds its port again while the old connections are in TIME_WAIT */
  (void)setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof yes);
  if (address->family == 6) {
    /* An IPv6 listener hears IPv6 only, on every system alike */
    (void)setsockopt(descriptor, IPPROTO_IPV6, IPV6_V6ONLY, &yes, (socklen_t)sizeof yes);
  }
  if (bind(descriptor, (const struct sockaddr *)(const void *)&system, length) != 0) {
    return torb_io_posix_failed(errno);
  }
  if (listen(descriptor, (int)backlog) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

int64_t torb_io_system_bind(torb_io_socket *socket, const torb_io_address *address) {
  int descriptor = (int)socket->system;
  struct sockaddr_storage system;
  socklen_t length = torb_io_posix_address_of(address, &system);
  int yes = 1;
  /* No SO_REUSEADDR: on a datagram socket it lets a second socket take the port, which Windows does not */
  if (address->family == 6) {
    (void)setsockopt(descriptor, IPPROTO_IPV6, IPV6_V6ONLY, &yes, (socklen_t)sizeof yes);
  }
  if (bind(descriptor, (const struct sockaddr *)(const void *)&system, length) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

int64_t torb_io_system_connect_datagram(torb_io_socket *socket, const torb_io_address *address) {
  struct sockaddr_storage system;
  socklen_t length = torb_io_posix_address_of(address, &system);
  /* A datagram socket connects at once: nothing goes over the network */
  while (connect((int)socket->system, (const struct sockaddr *)(const void *)&system, length) != 0) {
    if (errno != EINTR) {
      return torb_io_posix_failed(errno);
    }
  }
  return 0;
}

int64_t torb_io_system_pipe(int64_t descriptor) {
  int flags = fcntl((int)descriptor, F_GETFL, 0);
  if (flags < 0 || fcntl((int)descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

int64_t torb_io_system_shutdown(torb_io_socket *socket) {
  if (shutdown((int)socket->system, SHUT_WR) != 0) {
    return torb_io_posix_failed(errno);
  }
  return 0;
}

int64_t torb_io_system_address(torb_io_socket *socket, bool peer, torb_io_address *out) {
  struct sockaddr_storage system;
  socklen_t length = (socklen_t)sizeof system;
  int answer = peer ? getpeername((int)socket->system, (struct sockaddr *)(void *)&system, &length)
                    : getsockname((int)socket->system, (struct sockaddr *)(void *)&system, &length);
  if (answer != 0) {
    return torb_io_posix_failed(errno);
  }
  if (!torb_posix_address_from((const struct sockaddr *)(const void *)&system, out)) {
    return torb_io_failed(TORB_IO_INVALID, 0u);
  }
  return 0;
}

int64_t torb_io_system_resolve(const char *host, torb_io_address **out) {
  struct addrinfo hints;
  struct addrinfo *found = NULL;
  struct addrinfo *entry;
  size_t count = 0u;
  size_t capacity = 0u;
  int code;
  *out = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  code = getaddrinfo(host, NULL, &hints, &found);
  if (code != 0) {
    /* The EAI codes are not errno values and have no text of `strerror`: the kind says it */
    if (code == EAI_SYSTEM) {
      return torb_io_posix_failed(errno);
    }
    /* A name the name server has no address for is a name that was not found, not a failure of the network */
    if (code == EAI_NONAME || code == TORB_POSIX_NO_ADDRESS || code == TORB_POSIX_NO_FAMILY || code == EAI_AGAIN
        || code == EAI_FAIL) {
      return torb_io_failed(TORB_IO_HOST_NOT_FOUND, 0u);
    }
    return torb_io_failed(TORB_IO_OTHER, 0u);
  }
  for (entry = found; entry != NULL; entry = entry->ai_next) {
    torb_io_address address;
    size_t index;
    bool seen = false;
    if (entry->ai_addr == NULL || !torb_posix_address_from(entry->ai_addr, &address)) {
      continue;
    }
    address.port = 0u;
    for (index = 0u; index < count; index += 1u) {
      if ((*out)[index].family == address.family && memcmp((*out)[index].bytes, address.bytes, 16u) == 0) {
        seen = true;
      }
    }
    if (seen) {
      continue;
    }
    if (count == capacity) {
      size_t grown = capacity == 0u ? 4u : capacity * 2u;
      torb_io_address *larger = (torb_io_address *)realloc(*out, grown * sizeof(torb_io_address));
      if (larger == NULL) {
        break;
      }
      *out = larger;
      capacity = grown;
    }
    (*out)[count] = address;
    count += 1u;
  }
  freeaddrinfo(found);
  if (count == 0u) {
    return torb_io_failed(TORB_IO_HOST_NOT_FOUND, 0u);
  }
  return (int64_t)count;
}

/* ------------------------------------------------------------------------- the name servers, and randomness --- */

/* One address of a `nameserver` line, without a `%scope`: false where it is none. */
static bool torb_posix_name_server(const char *text, torb_io_address *out) {
  char address[64];
  size_t length = 0u;
  memset(out, 0, sizeof *out);
  while (text[length] != '\0' && text[length] != '%' && text[length] != ' ' && text[length] != '\t'
         && text[length] != '\r' && text[length] != '\n' && length < sizeof address - 1u) {
    address[length] = text[length];
    length += 1u;
  }
  address[length] = '\0';
  out->port = 53u;
  if (inet_pton(AF_INET, address, out->bytes) == 1) {
    out->family = 4;
    return true;
  }
  if (inet_pton(AF_INET6, address, out->bytes) == 1) {
    out->family = 6;
    return true;
  }
  return false;
}

int64_t torb_io_system_name_servers(torb_io_address *out, size_t capacity) {
  FILE *file = fopen("/etc/resolv.conf", "r");
  char line[512];
  size_t count = 0u;
  if (file != NULL) {
    while (fgets(line, (int)sizeof line, file) != NULL) {
      const char *rest = line;
      torb_io_address address;
      if (strncmp(rest, "nameserver", 10u) != 0 || (rest[10] != ' ' && rest[10] != '\t')) {
        continue;
      }
      rest += 10;
      while (*rest == ' ' || *rest == '\t') {
        rest += 1;
      }
      if (torb_posix_name_server(rest, &address) && count < capacity) {
        out[count] = address;
        count += 1u;
      }
    }
    (void)fclose(file);
  }
  /* resolv.conf(5): without a `nameserver` line, the name server of the local machine */
  if (count == 0u && capacity > 0u) {
    memset(&out[0], 0, sizeof out[0]);
    out[0].family = 4;
    out[0].port = 53u;
    out[0].bytes[0] = 127u;
    out[0].bytes[3] = 1u;
    count = 1u;
  }
  return (int64_t)count;
}

bool torb_io_system_random(uint8_t *out, size_t size) {
  int descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  size_t done = 0u;
  if (descriptor < 0) {
    return false;
  }
  while (done < size) {
    ssize_t read_now = read(descriptor, out + done, size - done);
    if (read_now < 0 && errno == EINTR) {
      continue;
    }
    if (read_now <= 0) {
      break;
    }
    done += (size_t)read_now;
  }
  (void)close(descriptor);
  return done == size;
}

/* ---------------------------------------------------------------------------------------- the calls themselves --- */

bool torb_io_posix_wants_writable(const torb_io_operation *operation) {
  return operation->kind == (uint8_t)TORB_IO_SEND || operation->kind == (uint8_t)TORB_IO_CONNECT
         || operation->kind == (uint8_t)TORB_IO_SEND_DATAGRAM;
}

/*
 * Whether a failed `accept` failed for the one connection it was about to take and not for the listener: the peer gave
 * up before the accept (`ECONNABORTED`, and `EPROTO` where a system says it that way), or on Linux an error of TCP/IP
 * that was pending on the connection. accept(2) says to treat those like `EAGAIN`, so the accept takes the next
 * connection, or waits for one, instead of ending a server's accept loop with a failure that was never its own.
 */
static bool torb_posix_accept_failed_connection(int code) {
  return code == ECONNABORTED || code == EPROTO || TORB_POSIX_PENDING_NETWORK_ERROR(code);
}

/* A connection an accept took: a record of its own, non-blocking, without Nagle. */
static int64_t torb_posix_accepted(torb_io_operation *operation, int descriptor) {
  int yes = 1;
  if (!torb_posix_prepare(descriptor)) {
    int code = errno;
    (void)close(descriptor);
    return torb_io_posix_failed(code);
  }
  (void)setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &yes, (socklen_t)sizeof yes);
  operation->accepted = torb_io_socket_new(TORB_IO_STREAM, operation->socket->family, (int64_t)descriptor);
  return 0;
}

int64_t torb_io_posix_perform(torb_io_operation *operation) {
  int descriptor = (int)operation->socket->system;
  /* A pipe of a child is read and written like a stream, with the calls of a file: `recv` and `send` want a socket */
  bool of_pipe = operation->socket->kind == (uint8_t)TORB_IO_PIPE;
  for (;;) {
    switch ((torb_io_operation_kind)operation->kind) {
      case TORB_IO_RECEIVE: {
        ssize_t received = of_pipe ? read(descriptor, operation->buffer, operation->capacity)
                                   : recv(descriptor, operation->buffer, operation->capacity, 0);
        if (received >= 0) {
          return (int64_t)received;
        }
        break;
      }
      case TORB_IO_SEND: {
        /* A pipe whose reader is gone fails with `EPIPE`: the first child ignored `SIGPIPE` for the process */
        ssize_t sent = of_pipe ? write(descriptor, operation->buffer + operation->offset,
                                       operation->length - operation->offset)
                               : send(descriptor, operation->buffer + operation->offset,
                                      operation->length - operation->offset, TORB_POSIX_SEND_FLAGS);
        if (sent >= 0) {
          operation->offset += (size_t)sent;
          if (operation->offset >= operation->length) {
            return (int64_t)operation->length;
          }
          continue;
        }
        break;
      }
      case TORB_IO_RECEIVE_DATAGRAM: {
        struct sockaddr_storage sender;
        socklen_t length = (socklen_t)sizeof sender;
        ssize_t received = recvfrom(descriptor, operation->buffer, operation->capacity, 0,
                                    (struct sockaddr *)(void *)&sender, &length);
        if (received >= 0) {
          if (!torb_posix_address_from((const struct sockaddr *)(const void *)&sender, &operation->address)) {
            memset(&operation->address, 0, sizeof operation->address);
          }
          return (int64_t)received;
        }
        break;
      }
      case TORB_IO_SEND_DATAGRAM: {
        ssize_t sent;
        /* A datagram goes whole or not at all: there is no rest to send later */
        if (operation->address.family == 0) {
          sent = send(descriptor, operation->buffer, operation->length, TORB_POSIX_SEND_FLAGS);
        } else {
          struct sockaddr_storage remote;
          socklen_t remote_length = torb_io_posix_address_of(&operation->address, &remote);
          sent = sendto(descriptor, operation->buffer, operation->length, TORB_POSIX_SEND_FLAGS,
                        (const struct sockaddr *)(const void *)&remote, remote_length);
        }
        if (sent >= 0) {
          return (int64_t)sent;
        }
        break;
      }
      case TORB_IO_ACCEPT: {
        int accepted = accept(descriptor, NULL, NULL);
        if (accepted >= 0) {
          return torb_posix_accepted(operation, accepted);
        }
        /* A failure of the one connection that was pending is not this listener's failure: take the next */
        if (torb_posix_accept_failed_connection(errno)) {
          continue;
        }
        break;
      }
      case TORB_IO_CONNECT: {
        int failure = 0;
        socklen_t length = (socklen_t)sizeof failure;
        int yes = 1;
        /* The first call starts the connection; the call after "writable" asks how it ended */
        if (operation->offset == 0u) {
          struct sockaddr_storage remote;
          socklen_t remote_length = torb_io_posix_address_of(&operation->address, &remote);
          operation->offset = 1u;
          if (connect(descriptor, (const struct sockaddr *)(const void *)&remote, remote_length) == 0) {
            (void)setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &yes, (socklen_t)sizeof yes);
            return 0;
          }
          if (errno == EINPROGRESS || errno == EINTR) {
            return TORB_IO_POSIX_AGAIN;
          }
          return torb_io_posix_failed(errno);
        }
        if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &failure, &length) != 0) {
          return torb_io_posix_failed(errno);
        }
        if (failure != 0) {
          return torb_io_posix_failed(failure);
        }
        (void)setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &yes, (socklen_t)sizeof yes);
        return 0;
      }
      case TORB_IO_RESOLVE:
      default:
        return torb_io_failed(TORB_IO_INVALID, 0u);
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return TORB_IO_POSIX_AGAIN;
    }
    return torb_io_posix_failed(errno);
  }
}

/* ------------------------------------------------------------------------ the readiness poller, either of them --- */

/*
 * What epoll and kqueue have in common: a stream has at most one operation waiting to read (a receive) and one waiting
 * to write (a send, a connect), in `reading` and `writing` under the socket's lock. A listener has as many accepts
 * waiting as there are accept loops on it (docs/design/NETWORK.md section 4): `reading` is the oldest, and the others
 * follow it through their `queued_next`. A submission makes the call at once, on the worker, and registers the
 * operation only where it would block; the IO thread makes the call again when the socket is ready - for every reader
 * that waits, the oldest first, until one would block. Registrations are one-shot, so the IO thread arms the socket
 * again for whatever still waits after every event.
 *
 * A record the IO thread might still find in an event it holds is not freed at once: `torb_io_system_forget` puts it
 * on the retired list, and the IO thread frees what was retired before a wait once that wait's events are handled.
 */

static torb_thread torb_posix_io_thread;
static uint32_t torb_posix_io_running = 0u;
static torb_mutex torb_posix_retired_lock = TORB_MUTEX_INITIALIZER;
static torb_io_socket *torb_posix_retired = NULL;

/* The call, where the socket is not closed; a close waits until it returned, so the descriptor is not reused under it. */
static int64_t torb_posix_perform_guarded(torb_io_socket *socket, torb_io_operation *operation) {
  int64_t answer;
  torb_spin_lock(&socket->lock);
  if (socket->closed != 0u) {
    torb_spin_unlock(&socket->lock);
    return torb_io_failed(TORB_IO_CLOSED, 0u);
  }
  socket->performing += 1u;
  torb_spin_unlock(&socket->lock);
  answer = torb_io_posix_perform(operation);
  torb_spin_lock(&socket->lock);
  socket->performing -= 1u;
  torb_spin_unlock(&socket->lock);
  return answer;
}

/* Queues `operation` behind the readers that wait on `socket`: a stream's one receive, or one more accept of a
   listener. The socket's lock held. */
static void torb_posix_add_reader(torb_io_socket *socket, torb_io_operation *operation) {
  torb_io_operation **last = &socket->reading;
  while (*last != NULL) {
    last = &(*last)->queued_next;
  }
  operation->queued_next = NULL;
  *last = operation;
}

/* Takes `operation` out of the readers that wait on `socket`, where it is one of them. The socket's lock held. */
static bool torb_posix_remove_reader(torb_io_socket *socket, torb_io_operation *operation) {
  torb_io_operation **link = &socket->reading;
  while (*link != NULL) {
    if (*link == operation) {
      *link = operation->queued_next;
      operation->queued_next = NULL;
      return true;
    }
    link = &(*link)->queued_next;
  }
  return false;
}

/* The oldest reader that waits on `socket`, taken out, or `NULL`. */
static torb_io_operation *torb_posix_take_reader(torb_io_socket *socket) {
  torb_io_operation *operation;
  torb_spin_lock(&socket->lock);
  operation = socket->reading;
  if (operation != NULL) {
    socket->reading = operation->queued_next;
    operation->queued_next = NULL;
  }
  torb_spin_unlock(&socket->lock);
  return operation;
}

/* Frees every record on `list`. */
static void torb_posix_free_retired(torb_io_socket *list) {
  while (list != NULL) {
    torb_io_socket *next = list->retired_next;
    torb_io_socket_free(list);
    list = next;
  }
}

static void torb_posix_io_main(void *argument) {
  bool going = true;
  (void)argument;
  while (going) {
    torb_io_socket *retired;
    torb_mutex_lock(&torb_posix_retired_lock);
    retired = torb_posix_retired;
    torb_posix_retired = NULL;
    torb_mutex_unlock(&torb_posix_retired_lock);
    going = torb_io_poller_wait();
    /* Retired before the wait: no event of it, and none after it, can name them */
    torb_posix_free_retired(retired);
  }
}

bool torb_io_system_start(int64_t *failure) {
  if (!torb_io_poller_open(failure)) {
    return false;
  }
  if (!torb_thread_start(&torb_posix_io_thread, torb_posix_io_main, NULL, (size_t)256u * 1024u)) {
    torb_io_poller_close();
    *failure = torb_io_failed(TORB_IO_OTHER, 0u);
    return false;
  }
  torb_atomic_store_u32(&torb_posix_io_running, 1u);
  return true;
}

void torb_io_system_stop(void) {
  if (torb_atomic_load_u32(&torb_posix_io_running) == 0u) {
    return;
  }
  torb_io_poller_wake();
  torb_thread_join(&torb_posix_io_thread);
  torb_atomic_store_u32(&torb_posix_io_running, 0u);
  torb_mutex_lock(&torb_posix_retired_lock);
  torb_posix_free_retired(torb_posix_retired);
  torb_posix_retired = NULL;
  torb_mutex_unlock(&torb_posix_retired_lock);
  torb_io_poller_close();
}

void torb_io_system_submit(torb_io_operation *operation) {
  torb_io_socket *socket = operation->socket;
  int64_t answer;
  int64_t armed;
  if (operation->kind == (uint8_t)TORB_IO_RESOLVE) {
    torb_io_complete(operation, torb_io_failed(TORB_IO_INVALID, 0u));
    return;
  }
  answer = torb_posix_perform_guarded(socket, operation);
  if (answer != TORB_IO_POSIX_AGAIN) {
    torb_io_complete(operation, answer);
    return;
  }
  torb_spin_lock(&socket->lock);
  if (socket->closed != 0u) {
    torb_spin_unlock(&socket->lock);
    torb_io_complete(operation, torb_io_failed(TORB_IO_CLOSED, 0u));
    return;
  }
  /* One reader and one writer at a time - a second one is a program that pulls a stream from two places - but any
     number of accepts: each is one more accept loop on the listener, as IOCP has any number of `AcceptEx` in flight */
  if (torb_io_posix_wants_writable(operation)
          ? socket->writing != NULL
          : socket->reading != NULL && operation->kind != (uint8_t)TORB_IO_ACCEPT) {
    torb_spin_unlock(&socket->lock);
    torb_io_complete(operation, torb_io_failed(TORB_IO_INVALID, 0u));
    return;
  }
  if (torb_io_posix_wants_writable(operation)) {
    socket->writing = operation;
  } else {
    torb_posix_add_reader(socket, operation);
  }
  armed = torb_io_poller_arm(socket);
  if (armed != 0) {
    if (socket->writing == operation) {
      socket->writing = NULL;
    } else {
      (void)torb_posix_remove_reader(socket, operation);
    }
  }
  torb_spin_unlock(&socket->lock);
  if (armed != 0) {
    torb_io_complete(operation, armed);
  }
}

/*
 * Makes the call of an operation the IO thread took off `socket` again, and completes it. False where it would still
 * block: then it waits again - at the front of the readers, or as the writer - or fails where the socket was closed
 * in the meantime.
 */
static bool torb_posix_retry(torb_io_socket *socket, torb_io_operation *operation, bool reader) {
  int64_t answer = torb_posix_perform_guarded(socket, operation);
  bool kept = false;
  if (answer != TORB_IO_POSIX_AGAIN) {
    torb_io_complete(operation, answer);
    return true;
  }
  /* Ready and still nothing to do (a spurious wakeup, a connection reset before its accept, or one that a worker's
     accept took first): wait again */
  torb_spin_lock(&socket->lock);
  if (socket->closed == 0u) {
    if (reader) {
      operation->queued_next = socket->reading;
      socket->reading = operation;
    } else {
      socket->writing = operation;
    }
    kept = true;
  }
  torb_spin_unlock(&socket->lock);
  if (!kept) {
    torb_io_complete(operation, torb_io_failed(TORB_IO_CLOSED, 0u));
  }
  return false;
}

void torb_io_posix_ready(torb_io_socket *socket, bool readable, bool writable) {
  /* The readers, the oldest first, until one would block: a stream's one receive, or as many of a listener's accepts
     as connections arrived */
  while (readable) {
    torb_io_operation *reader = torb_posix_take_reader(socket);
    if (reader == NULL || !torb_posix_retry(socket, reader, true)) {
      break;
    }
  }
  if (writable) {
    torb_io_operation *writer;
    torb_spin_lock(&socket->lock);
    writer = socket->writing;
    socket->writing = NULL;
    torb_spin_unlock(&socket->lock);
    if (writer != NULL) {
      (void)torb_posix_retry(socket, writer, false);
    }
  }
  torb_spin_lock(&socket->lock);
  if (socket->closed == 0u && (socket->reading != NULL || socket->writing != NULL)) {
    (void)torb_io_poller_arm(socket);
  }
  torb_spin_unlock(&socket->lock);
}

void torb_io_system_cancel(torb_io_operation *operation) {
  torb_io_socket *socket = operation->socket;
  bool found = false;
  if (socket == NULL) {
    return;
  }
  torb_spin_lock(&socket->lock);
  found = torb_posix_remove_reader(socket, operation);
  if (socket->writing == operation) {
    socket->writing = NULL;
    found = true;
  }
  if (found && socket->closed == 0u) {
    (void)torb_io_poller_arm(socket);
  }
  torb_spin_unlock(&socket->lock);
  /* Not found: the IO thread has it in hand and completes it */
  if (found) {
    torb_io_complete(operation, torb_io_failed(TORB_IO_ABORTED, 0u));
  }
}

void torb_io_system_close(torb_io_socket *socket) {
  torb_io_operation *reading;
  torb_io_operation *writing;
  torb_spin_lock(&socket->lock);
  reading = socket->reading;
  writing = socket->writing;
  socket->reading = NULL;
  socket->writing = NULL;
  /* A call on the descriptor that is in progress returns first: the descriptor must not be reused under it */
  while (socket->performing != 0u) {
    torb_spin_unlock(&socket->lock);
    torb_thread_yield();
    torb_spin_lock(&socket->lock);
  }
  if (socket->registered != 0u) {
    torb_io_poller_remove(socket);
    socket->registered = 0u;
  }
  torb_spin_unlock(&socket->lock);
  (void)close((int)socket->system);
  /* Every reader that waits: a stream's receive, or each accept loop of a listener */
  while (reading != NULL) {
    torb_io_operation *next = reading->queued_next;
    reading->queued_next = NULL;
    torb_io_complete(reading, torb_io_failed(TORB_IO_CLOSED, 0u));
    reading = next;
  }
  if (writing != NULL) {
    torb_io_complete(writing, torb_io_failed(TORB_IO_CLOSED, 0u));
  }
}

void torb_io_system_forget(torb_io_socket *socket) {
  if (torb_atomic_load_u32(&torb_posix_io_running) == 0u) {
    torb_io_socket_free(socket);
    return;
  }
  torb_mutex_lock(&torb_posix_retired_lock);
  socket->retired_next = torb_posix_retired;
  torb_posix_retired = socket;
  torb_mutex_unlock(&torb_posix_retired_lock);
}

#endif /* __unix__ || __APPLE__ */
