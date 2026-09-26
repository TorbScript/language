# Networking, TLS and HTTP

**Status: partly implemented** — decided on 2026-09-23, the owner's three questions answered on 2026-09-24 (section
14). Slices 1 to 6 are in (section 11): the IO core of the runtime (`runtime/io.c`, IOCP in `runtime/os/iocp.c`,
epoll in `runtime/os/epoll.c`, kqueue in `runtime/os/kqueue.c`) with TCP, UDP and name resolution, `std/network` over
it, an HTTP/1.1 server and client in TorbScript in `std/http`, and TLS 1.2 and 1.3 over mbedTLS 3.6 in `std/tls`, with
`https` in the client and the server. The Windows half is built and tested on every commit; the POSIX halves were
written against their manuals, checked for syntax and types against stub headers, and are built for the first time by
the Linux and macOS jobs of CI. UDP (slice 5) was built on Windows and its runtime tests run on Linux in a container as
well; kqueue has not run it. The client's connection pool (slice 7) and serving on every worker (slice 8) are in, and
so is the cancellation of a handler whose client went away (slice 13). The sandbox's network grant, HTTP/2 and HTTP/3
are later slices, each with its reason below.

This is the record of how a TorbScript program talks to another machine: which packages there are and what each one
owns, how a socket meets the task scheduler of `docs/design/CONCURRENCY.md`, which TLS implementation the language
ships and why, what an HTTP message is, and what a sandboxed script may do with the network. It closes CONCURRENCY's
slice G (the IO poller) for sockets.

```text
   std/http        Request, Response, Headers, Status, Method, Body       client: get, post, send
                   HTTP/1.1 parser and writer, in TorbScript              server: Server.listen(address, handler)
        │                                     │
   std/tls         (slice 6) TlsStream over a TcpStream, the same Source/Sink shape
        │                                     │
   std/network     IpAddress, SocketAddress, resolve       TcpListener.accept    TcpStream.source / .sink
        │                                     │
   runtime/io.c    handles, operations, tasks of the runtime, the resolver threads          one IO thread:
   runtime/os/     iocp.c: IOCP + Winsock        epoll.c           kqueue.c                 the poller
```

- **[1. The packages](#1-the-packages)** — `std/network`, `std/tls`, `std/http`, and why three
- **[2. The IO core](#2-the-io-core)** — the poller, the IO thread, operations, and CONCURRENCY slice G reconciled
- **[3. Cancelling, timeouts and backpressure](#3-cancelling-timeouts-and-backpressure)**
- **[4. `std/network`](#4-stdnetwork)** — addresses, resolution, TCP and UDP
- **[5. TLS](#5-tls)** — the implementation, weighed
- **[6. HTTP messages](#6-http-messages)** — `Method`, `Status`, `Headers`, `Request`, `Response`, `Body`
- **[7. The client](#7-the-client)**
- **[8. The server](#8-the-server)** — handlers, connections, keep-alive, limits
- **[9. Parsing safety](#9-parsing-safety)** — smuggling, limits, and every refusal
- **[10. Capabilities](#10-capabilities)** — the sandbox's network grant, and a package's summary from its imports
- **[11. Slices](#11-slices)**
- **[12. HTTP/2, HTTP/3 and compression](#12-http2-http3-and-compression)** — why later
- **[13. What `docs/design/WEB.md` asked for](#13-what-docsdesignwebmd-asked-for)**
- **[14. Owner questions, answered](#14-owner-questions-answered)**

---

## 1. The packages

**Decision: three packages, one per protocol layer — `std/network`, `std/tls`, `std/http` — and none of them in the
prelude.**

| Package | Owns | Depends on |
|---|---|---|
| `std/network` | `Ipv4Address`, `Ipv6Address`, `IpAddress`, `SocketAddress`, `resolve`, `TcpListener`, `TcpStream`, `UdpSocket`, `NetworkError` | `std/stream`, `std/task` |
| `std/tls` (slice 6) | `TlsStream`, `TlsSettings`, the trust store | `std/network` |
| `std/http` | `Method`, `Status`, `Headers`, `Request`, `Response`, `Body`, `HttpError`, the HTTP/1.1 parser and writer, the client (`get`, `post`, `send`, `Client`), the server (`Server`) | `std/network`, `std/json`, `std/stream` |

- **The name is `std/network`, not `std/net`.** Names are full words in this language (a type is `Workers`, not
  `Wrkrs`; a case is `Version4`, not `V4`), and `docs/design/OS.md` section 5.5 already put the address types into a
  package of this name. The address types are proposed there and built here, with that section's shapes.
- **Three packages and not one**, because each layer has users that do not want the next: a game server speaks its own
  protocol over TCP or UDP and never parses a header; a database driver needs TLS and never HTTP; a web framework needs
  all three. A package is also the unit a sandbox grants (section 10), and "may open sockets" is a smaller grant than
  "may run an HTTP server".
- **Not in the prelude.** The network is a capability, and an import is where a file says it spends one — the same
  argument that keeps `std/fs` out.
- **A URL is `std/uri`'s**, which does not exist yet (`docs/design/URI.md`). Until it does, `std/http` reads the three
  parts it needs — scheme, authority, path and query — itself, in one private function that `std/uri` replaces
  without a change to any signature: every URL parameter is a `String` today and becomes `Into<Uri>` then.

### How the layers map onto streams

Every byte that crosses the network is a `Bytes` chunk of `std/stream`, and every end is a `Source` or a `Sink`:

| End | Is a | Pulled or pushed by |
|---|---|---|
| `TcpStream.source()` | `Source<Bytes, NetworkError>` | the reader; one receive per `next()`, nothing read ahead |
| `TcpStream.sink()` | `Sink<Bytes, NetworkError>` | the writer; `add` finishes once the operating system took the bytes, `end()` is a half close |
| `Body` of a request or a response | `Source<Bytes, HttpError>` | the handler, or the client that reads the response |
| the body a handler answers | a `Body`, which the server pulls into the connection | the server |

**A connection is not itself a `Source` and a `Sink`**, because both traits have a `mapFailure` of a different
signature, and one type cannot have two members of one name. So a `TcpStream` hands out its two ends as
`source()` and `sink()`, the shape `Channel` has, and both ends hold the stream: the socket closes when the last of the
three is released.

## 2. The IO core

CONCURRENCY section 7 decided the mechanism per platform and slice G is where it was to be built. This section is
that build, and one of its decisions departs from section 7 on purpose.

| Platform | Poller | File |
|---|---|---|
| Windows | an IO completion port, sockets through Winsock with `AcceptEx`, `ConnectEx`, `WSARecv`, `WSASend`, and `WSARecvFrom`, `WSASendTo` for datagrams | `runtime/os/iocp.c` |
| Linux | epoll, one-shot registrations; the calls themselves are POSIX (`posix_io.c`) | `runtime/os/epoll.c` |
| macOS, FreeBSD | kqueue, `EV_ONESHOT` filters; the calls are `posix_io.c`'s as well | `runtime/os/kqueue.c` |
| every platform | handles, operations, the tasks of the runtime, the resolver threads, error texts | `runtime/io.c` |

**Every platform file is one `#if` from its first line to its last** (OS.md section 7), so the build compiles all of
them everywhere, and `runtime/include/torb_io.h` is the interface between `io.c` and the three pollers. It is never
included by generated C, like `torb_pool.h`. **A poller is a file of its own, named after its mechanism**, beside the
family files of `std/os` (`windows.c`, `linux.c`, `bsd.c`, `posix.c`): those hold the natives of one family, and a
poller is no native - it is the IO core's, and `kqueue.c` serves two families at once.

### One IO thread, not a poller per worker

**Decision: the process has one IO thread, started the first time a socket is opened, that waits on the poller and
wakes the task whose operation completed through the scheduler's ordinary cross-worker wake (`torb_post`).**
CONCURRENCY section 7 said "each worker owns its own poller"; this is the departure, and it is a slice rather than a
reversal.

| | One IO thread (built) | A poller per worker (section 7) |
|---|---|---|
| The worker's sleep | unchanged: one condition wait, the same on every platform | becomes a wait on the poller: `GetQueuedCompletionStatusEx`, `epoll_wait`, `kevent` with the timer as its timeout, and every wake of a worker (`torb_post`, a steal, a stop) becomes a post to that poller - three rewrites of the one loop the pool's correctness rests on |
| A completion | one `torb_post` to the task's worker: a lock, a queue link, a signal | resumes on the thread that waits already, no hand-off |
| What changes in `task.c` | a sixth kind of waiting (`TORB_WAITING_IO`), its cancellation, its count in `busy` | the idle loop, the wake of a worker and the stop of the pool on three platforms |
| A socket used from two workers | nothing special: the operation carries its task, the IO thread posts it where it belongs | a socket is registered with one worker's poller; a second worker's operation on it needs a hand-off |
| Measured cost | one extra context switch per completion | none |

A completion already had to be able to reach any worker: a task that awaits a socket read may be on worker 3 while
the read was issued by a runtime task of worker 3 - but a `Channel` fed by that read wakes a task on worker 1. So the
cross-worker wake is on the path either way, and the poller per worker saves exactly the one switch between the IO
thread and the worker. **That is the later speed slice (slice 9), behind the same `torb_io.h`**, once a benchmark says
the switch matters; nothing above the runtime can tell the two apart.

**The IO thread runs no TorbScript and allocates nothing counted.** It touches operations (`malloc`ed, with an atomic
count of their own), socket records (the same), and the waiting fields of a task under the operation's lock, and it
wakes with `torb_post`. That is section 7's "a pool thread has no heap" kept literally, and it is why the IO thread
needs no worker of its own and appears in no block counter.

### An operation, and the task that waits for it

A socket read, write, accept or connect is an **operation**: a record of the runtime that owns the kernel's buffer,
holds its socket, and has a waiter.

```text
  TorbScript:   const count = networkReceive(handle, 65536).await()      a Task<Int64> of the runtime
                        │
  io.c:         torb_network_receive ─► a task of the runtime (like `sleep`), frame = { operation }
                        │ first resume: submit the operation, then torb_task_wait_io(task, operation)
  os/*.c:       WSARecv on the socket (IOCP)  |  recv, or register EPOLLIN/EVFILT_READ and recv on readiness
                        │
  IO thread:    the completion arrives ─► torb_io_complete(operation): result in, waiter out, torb_post(waiter)
                        │
  io.c:         the task resumes, moves the bytes into the socket's pending buffer, answers the count
  TorbScript:   takeReceived(handle, var into) moves them into an ArrayList<UInt8>
```

- **The runtime never builds a `Result`**, a rule of every native (BACKEND 5.R1). An operation answers an `Int64`:
  zero or more is the answer (a count, a new handle), a negative number is a failure whose kind and operating-system
  code are packed into it, and `std/network` builds the `NetworkError`. The text of a failure is one more native
  (`networkErrorText`), so the operating system's own words reach the message.
- **Data crosses in two steps**: the task answers the count, and a synchronous native copies the bytes into an
  `ArrayList<UInt8>` the caller holds with `var`. A task of the runtime cannot answer a list, because the descriptor of
  `ArrayList<UInt8>` is the program's, and a `var` parameter cannot be written by a task after the call returned
  (STREAMS section 13, point 1).
- **A handle is an `Int64`**, an index into a table of the runtime with a generation in the high bits, and not a
  pointer and not a runtime kind of the IR. So `std/network` is TorbScript over natives of plain numbers, the IR and
  both back ends learn nothing new, and a handle used after its close is a clean `NetworkError` ("the socket is
  closed") rather than memory that was freed. `TcpStream` and `TcpListener` are ordinary `shared type`s with `Close`
  that hold one.
- **Winsock is loaded on first use** (`LoadLibraryW` and `GetProcAddress`), so a program that never opens a socket
  never loads `ws2_32.dll`, and the build needs no library flag: the seed that builds the compiler links unchanged, and
  so does every program. The POSIX socket calls are libc.

### Name resolution

**`resolve(host)` runs `getaddrinfo` (`GetAddrInfoW` on Windows) on a resolver thread of `io.c`**, of which there are
at most four, started on the first name that is not a literal address. A literal - `127.0.0.1`, `::1`, `[::1]` - never
reaches a thread: `std/network` parses it and answers at once, and `localhost` answers `127.0.0.1` and `::1` without
asking the system, as RFC 6761 section 6.3 allows, so the tests never depend on a resolver.

These threads are section 7's blocking pool as the IO interface uses it - C only, no heap, a result written into the
operation - and not `offload`'s pool, which runs TorbScript and has heaps. An asynchronous resolver (`GetAddrInfoExW`
with an overlapped, `getaddrinfo_a`, a DNS client of our own) is a speed change behind the same native and waits for a
reason.

**`lookup(name, type)` is that DNS client, beside `resolve` and not under it** ([DNS.md](DNS.md) section 7, "Lookups,
as built"): the records of any type over UDP, TCP where a response was truncated, and TLS in `std/tls`, asked of the
system's name servers or of the program's own. `resolve` keeps `getaddrinfo`, because connecting wants the system's
answer - its hosts file, its cache, its search domains - and a stub resolver of our own would answer differently.

## 3. Cancelling, timeouts and backpressure

### Cancelling a wait

CONCURRENCY section 7 made the frame of a task that waits on a completion port wait for the completion, because the
kernel writes into a buffer the frame owns. **Here the buffer belongs to the operation, not to the frame**, so every
mechanism releases the frame at the next check, and only the operation waits for the kernel:

| Waiting on | What `cancel()` does |
|---|---|
| IOCP | the waiter is taken out of the operation under its lock and queued, so the task stops at once; `CancelIoEx` asks the kernel to give the operation up, and the operation - its buffer, its socket - is freed by the IO thread when the completion arrives, cancelled or not |
| epoll, kqueue | the waiter is taken out and queued; the operation is taken off its socket under the socket's lock, so a readiness that arrives later finds nothing to do |
| the resolver | the waiter is taken out and queued; the thread finishes its `getaddrinfo`, and the operation is freed there |

The count of an operation is two while it is in flight - the task's and the kernel's (or the IO thread's) - and
whoever lets go last frees it. That is the whole difference from section 7's table, and it is what lets the table say
"at once" in every row: **a cancelled network task frees its worker and its frame immediately, and the runtime's own
record at the next honest moment**.

A socket that is closed while an operation waits on it fails that operation, so a task that reads from a connection
somebody else closed is woken with a failure and never waits forever. Whatever the system called it - `WSAECONNABORTED`,
an aborted overlapped call, `EBADF` - the IO core answers "closed" for a socket that was closed (`isClosed()`), which is
what an accept loop takes as the end of listening (section 4, slice 8).

### Timeouts

**There is no timeout parameter on a socket operation.** `task.within(limit)` is the timeout of every task
(CONCURRENCY section 8), and a network task is a task: `stream.source().next().within(5.seconds())` cancels the read
when the limit passes, and the cancellation is the row above. The HTTP server's limits (section 8) are `within`s.
`SO_RCVTIMEO` would be a second mechanism with a second failure, and on a non-blocking socket it does nothing anyway.

### Backpressure

- **Reading is a pull.** One receive is in flight per socket at most, of at most the size the reader asked for, and it
  is only issued when `next()` is called. Nothing reads ahead, so a slow reader is a full receive window and the peer
  slows down - TCP's own backpressure, untouched.
- **Writing waits for the kernel.** `add` finishes when the send completed, which on a full send buffer is when the
  peer read; a writer that awaits each `add` is paced by the peer. The runtime copies a chunk into the operation once
  and holds no other buffer.
- **Accepting is the backlog.** A listener has one accept in flight per waiting `accept()`; the kernel's backlog is
  the queue, and the backlog is a parameter of `listen`.

## 4. `std/network`

```trb fragment
public type Ipv4Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError>
public type Ipv6Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError>
public type IpAddress with Show, Equals, Hash, Compare, TryFrom<String, AddressError> {
  case Version4(address: Ipv4Address)
  case Version6(address: Ipv6Address)
}
public type SocketAddress with Show, Equals, Hash, TryFrom<String, AddressError> {
  address: IpAddress
  port: Int
}

public fn resolve(host: String): Task<Result<List<IpAddress>, NetworkError>>

public shared type TcpListener with Close {
  static fn listen(address: SocketAddress, backlog: Int = 128): Result<TcpListener, NetworkError>
  fn localAddress(): SocketAddress
  fn accept(): Task<Result<TcpStream, NetworkError>>
}

public shared type TcpStream with Close {
  static fn connect(address: SocketAddress): Task<Result<TcpStream, NetworkError>>
  static fn connectTo(host: String, port: Int): Task<Result<TcpStream, NetworkError>>
  fn localAddress(): Result<SocketAddress, NetworkError>
  fn remoteAddress(): Result<SocketAddress, NetworkError>
  fn receive(maximum: Int = 65536): Task<Result<Bytes?, NetworkError>>
  fn send(bytes: Bytes): Task<Result<Void, NetworkError>>
  fn shutdown(): Result<Void, NetworkError>
  fn source(chunk: Int = 65536): TcpSource
  fn sink(): TcpSink
}
```

- **The address types are OS.md section 5.5's**, with its case names and its RFC 5952 `show()`. They are values: an
  `Ipv4Address` is a `UInt32`, an `Ipv6Address` two `UInt64`s, and they cross into the runtime as numbers, never as
  text, so the C side parses nothing. `SocketAddress` shows as `127.0.0.1:8080` and `[::1]:8080`.
- **`listen` is synchronous and `accept` is a task**, because binding a port never waits for the network and accepting
  does. `connect` is a task; `connectTo(host, port)` resolves and tries each address in the order the resolver
  answered until one connects (no Happy Eyeballs racing in slice 1; RFC 8305 is a later refinement of this one
  function).
- **`NetworkError` is one wrapper type**, like `HttpError`: a private kind (`ConnectionRefused`, `ConnectionReset`,
  `ConnectionAborted`, `TimedOut`, `AddressInUse`, `AddressNotAvailable`, `HostNotFound`, `Unreachable`, `Closed`,
  `Other`), static constructors, `is…` predicates and the operating system's message. A new kind is never a breaking
  change. It has no `Cancelled` and no `From<Cancelled>`: `await()` passes a cancellation on instead of answering it
  (CONCURRENCY.md section 8, "The cascade"), so an IO line is `stream.receive().await()?` and a cancelled read stops
  the task that waited, with its scopes, rather than becoming a failure somebody has to handle.
- **`TcpSource` reads at most 64 KiB per `next()`** by default (`source(chunk:)` says otherwise) and ends with `None`
  when the peer shut its side down. **`TcpSink.end()` is a half close** (`shutdown(SEND)`): the peer reads the end of
  the stream and may still answer, which is what an HTTP/1.0 client without a length needs. `close()` on either end
  does nothing of its own; the stream closes the socket when the last of the three is released.
- **`TCP_NODELAY` is on for every stream.** Nagle's algorithm helps a program that writes one byte at a time and hurts
  every request-response protocol, and a program here writes chunks. A setter is a later addition when a program
  needs it off.
- **UDP is `UdpSocket`** (slice 5): `bind(address)`, `send(bytes, to:)`, `receive(): Task<Result<(Bytes,
  SocketAddress), NetworkError>>`, and for a socket with one peer `connect(address)`, `peerAddress()` and
  `sendToPeer(bytes)`. A datagram is not a stream, so it is not a `Source`; the operations are TCP's shape.

### UDP, as built (slice 5)

- **A datagram crosses whole.** A receive answers the length of the next datagram, and the datagram waits in a queue of
  its socket - bytes and sender together - until `networkTakeDatagram` takes the oldest, so datagram borders survive the
  two steps every read of the IO core has (section 2). A datagram that arrives for a cancelled receive stays queued for
  the next one, as received TCP bytes do. The buffer of a receive is the largest datagram there is (64 KiB), so nothing
  is ever cut short, and `receive()` takes no maximum.
- **The calls**: `WSARecvFrom` and `WSASendTo` on the completion port, the sender written into the operation beside
  the bytes; `recvfrom` and `sendto` on readiness under epoll and kqueue, through the same `posix_io.c` that makes the
  TCP calls. The pollers learned nothing.
- **`send(bytes, to:)` and `sendToPeer(bytes)`**, two names, because a parameter has no overloads and an `Option` is not
  filled in by itself: `send(bytes, to: Some(peer))` would be the price of one name. A connected socket refuses `send`
  to an address other than its peer, because the systems disagree about it (Linux sends, BSD answers `EISCONN`).
- **"Port unreachable" is a refusal on a connected socket and nothing on an unconnected one**, on every system: Windows
  reports it on any UDP socket as `WSAECONNRESET` on the next receive, so the IO core turns that off
  (`SIO_UDP_CONNRESET`) until the socket is connected, and answers a reset of a datagram socket as `isConnectionRefused()`,
  the word Linux uses. A resolver that asks a server whose port is closed hears it at once instead of waiting out its
  timeout.
- **No `SO_REUSEADDR` on a datagram socket**: on POSIX it lets a second socket bind the same port, which Windows does not,
  so a port bound twice is `isAddressInUse()` everywhere. Broadcast, multicast and the socket options (TTL, buffer sizes)
  are later additions, each when a program needs it.
- **Verified**: the runtime tests (`runtime/tests/io_test.c`: datagrams both ways, empty ones, a connected socket and
  its filter, the refusal, a cancelled receive, a close that wakes a receive) run on Windows and on Linux (gcc 16, in a
  container); the conformance program `network-datagrams` runs natively and in the VM on Windows. kqueue has not run any
  of it: macOS and FreeBSD have the same `recvfrom`/`sendto` calls and the same readiness filter, and CI is to run them.

### Serving on more than one worker

A `TcpListener` is a `shared type`, confined to the task that made it (CONCURRENCY section 1), and so is every
`TcpStream` it accepts: a connection is served on the worker of the task that accepted it. **Serving on every core is
several accept loops on one listening socket**, which every operating system supports (several `AcceptEx` in flight,
several threads in `accept`): the listener hands out a plain `Int64` handle through a private function of
`std/http`'s server, each loop is spawned with it, and the socket is closed once the last loop is done. It is slice 8,
because the first question the server has to answer is whether it is correct.

**As built (slice 8):**

- **`TcpListener.acceptor()` answers a `TcpAcceptor`**, a public value of `std/network` rather than a private function
  of the server: the handle and nothing else, with `accept()`. It does not own the socket - the listener does, and
  stopping or releasing it fails every accept of every acceptor with `isClosed()`. Public, because any server of a
  protocol of its own wants several loops on one socket, not only HTTP's.
- **`Server.serve()` starts one accept loop per worker** (`Workers.count()`), each a task whose frame holds the
  acceptor, the handler and the five limits as numbers - a frame an idle worker takes before its first run
  (CONCURRENCY section 16). The limits cross one number each because `ServerLimits`, at 40 bytes, is a block of the
  worker's heap, and a frame holding a block stays where it was made; the acceptor holds no address for the same reason.
  A handler closure crosses where its environment may (`torb_closure_may_move`, or its copy while the pool has several
  workers); one that holds an object keeps every loop on the serving task's worker - correct, and as parallel as it can be.
- **A connection is served where its loop accepted it**: the loop's children, pinned by the `TcpStream` they hold.
  Each loop keeps its own list of connections and its own "closing" flag, so no state is shared between workers and
  nothing needs a lock.
- **`shutdown(grace)` stops the listener**; each loop's accept fails with a closed socket, and the loop then closes its
  idle connections, lets its busy ones finish with `Connection: close`, and answers `Ok` once they did. The server waits
  for every loop within `grace` and cancels what is left, which cancels their connections with them. **`close()`** stops
  the listener and cancels every loop. `serve()` answers the first failure of a loop, or `Ok`.
- **A server with TLS runs one loop**, because its `ServerIdentity` is an object and a loop that holds it cannot move;
  handing the identity's handle across is the refinement when a benchmark asks for it.
- **Verified** by `tests/conformance/http-workers.trb`, twelve requests at once and a graceful shutdown, with four workers
  and with one (the `.workers` file), natively and in the VM; the emitted C starts the loop with
  `torb_task_start_portable` where the handler may cross. Which worker runs which loop is the scheduler's, and not
  printed.

## 5. TLS

The question is what implements the protocol, and four answers were weighed.

| | System libraries (Schannel, Network.framework, OpenSSL) | A vendored C library (mbedTLS 3) | BearSSL | TLS in TorbScript over a C crypto core |
|---|---|---|---|---|
| Security fixes | the operating system ships them | we re-vendor on every advisory | the same, and upstream has been quiet since 2018 | we write and audit the protocol |
| TLS 1.3 | Schannel from Windows 11 / Server 2022 only; SecureTransport never (deprecated); OpenSSL 1.1.1+ | yes | **no** | what we write |
| Implementations to keep | three, each with its own API, state machine and error model | one | one | one |
| Certificate store | native, including enterprise roots and policies | not included: must come from the platform | not included | not included |
| Portability | Network.framework needs blocks, which C11 does not have; OpenSSL's ABI changed twice (1.0, 1.1, 3) and is missing in minimal containers | any C99 compiler, no dependency | any C compiler | wherever TorbScript runs, the VM included |
| Binary size | nothing | about 300-500 KiB, only in a program that uses TLS | about 100 KiB | the program's own |
| "The runtime is C11" | a C11 caller of three foreign APIs | vendored C99 code, compiled with its own warning flags | the same | TorbScript and a small C core |
| A sandboxed script | no difference: TLS adds no capability of its own (section 10) | | | |

**Decision: mbedTLS 3 for the protocol, vendored under `runtime/vendor/mbedtls` and compiled only into a program that
reaches a native of `std/tls`; certificate verification by the platform's verifier where it has one; the platform's
roots everywhere.** This is the shape Go and rustls arrived at from opposite ends - one protocol implementation for
every platform, the platform's trust decisions where the platform has them:

| Platform | Roots | Chain verification |
|---|---|---|
| Windows | the machine's, through crypt32 (loaded on first use, like Winsock) | `CertGetCertificateChain` and `CertVerifyCertificateChainPolicy(SSL)`: enterprise roots and policy as the machine has them |
| macOS | the keychain's anchors | `SecTrustEvaluateWithError` - *as built, the bundle macOS ships at `/etc/ssl/cert.pem`, verified by mbedTLS; the Security framework is the step after slice 6* |
| Linux, FreeBSD | the first bundle that exists of the well-known paths (`/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/cert.pem`, `/usr/local/share/certs/ca-root-nss.crt`), `SSL_CERT_FILE` overriding | mbedTLS's own, against those roots |

- **Why not the system libraries**: three implementations of one protocol behind one TorbScript API is three sets of
  behaviour for the conformance suite to pin, and two of the three fail a constraint outright - Network.framework is
  not callable from C11, and Schannel lacks TLS 1.3 on every Windows 10. What the system libraries do best - trust -
  is kept, by asking the platform's verifier.
- **Why not BearSSL**, which is smaller and constant-time by construction: no TLS 1.3, and no release in years. A
  library we re-vendor on advisories has to have advisories to re-vendor.
- **Why not TorbScript yet**: TLS 1.3 plus X.509 path validation is the most security-critical code a standard library
  has, and it needs constant-time arithmetic the language cannot promise today. **The interface of `std/tls` does not
  depend on the choice**, so the protocol can move into TorbScript over a C crypto core later - the rustls-over-ring
  shape - without a program noticing.
- **The build learns one thing**: a part of the runtime that is compiled only where a native needs it. That is slice
  6's, and it also keeps the vendored library's warnings out of `-Werror`.
- **The API is the stream's**: `TlsStream.connect(stream, serverName, settings)` and `TlsStream.accept(stream, identity)`
  answer a `TlsStream` with `receive`, `send`, `source()` and `sink()` like a `TcpStream`'s, and `std/http` reads and
  writes either through one `Transport`. `https` URLs and `Server.listen(..., tls: identity)` are the two places an HTTP
  program sees it.

### TLS, as built (slice 6)

- **mbedTLS 3.6.7, the long-term branch**, vendored whole under `runtime/vendor/mbedtls` (`include/`, `library/`, the
  license, a `VERSION`), not the 4.x line, whose split into TF-PSA-Crypto is a build of its own and whose support window
  is shorter than 3.6's. The changes to its default configuration are one file, `runtime/tls/torb_mbedtls_user.h`: no
  sockets, no timers and no key storage of mbedTLS's own, randomness from the runtime, IP names in certificates read by
  mbedTLS's parser, and a trusted-certificate callback. Everything else - TLS 1.2 and 1.3, the ciphers and curves - is
  mbedTLS's default, so an advisory is a re-vendoring and nothing else.
- **A state machine over memory.** A session reads and writes two buffers of the glue (`runtime/tls/tls.c`), and the
  TorbScript side receives from the `TcpStream`, feeds, and sends what the session produced. So every wait is a wait
  of `std/network` - cancelled, timed out and paced as a TCP wait is - and the IO core learned nothing.
- **Linked where it is reached.** The rows of the manifest name their part (`NativeEntry.part`, `"tls"`); the driver
  adds `runtime/tls/` and mbedTLS to a program whose C calls a `torb_tls_` function, and defines `TORB_WITH_TLS`, under
  which the VM's thunks for those rows exist - elsewhere they panic, so every other program links as before. mbedTLS is
  compiled once per checkout, C compiler, version and configuration into `build/vendor/` (half a minute with gcc), in a
  directory per build that is read only once a marker says it finished, so two builds at once never share a file.
- **One lock around every call of mbedTLS.** PSA's key store and generator are global in mbedTLS and not safe for two
  threads without `MBEDTLS_THREADING_C`; the calls never wait, so one mutex is correct and costs only parallel
  handshakes. `MBEDTLS_THREADING_ALT` over the runtime's mutexes is the speed change when a benchmark asks for it.
- **Randomness** is the platform's: `BCryptGenRandom` loaded on first use on Windows, `/dev/urandom` elsewhere, as
  mbedTLS's hardware source; PSA's generator is seeded from it.
- **Trust on Windows is the platform's, in the handshake.** mbedTLS gets no roots (a callback answers none) and a
  verification callback that, at depth 0, hands the whole presented chain to `CertGetCertificateChain` with the server
  name in the SSL policy. So a rejected certificate stops the handshake before a byte of data, exactly as mbedTLS's own
  verdict would. Revocation is not checked: a check goes to the network and waits, and neither Go nor rustls does it by
  default.
- **No switch turns verification off.** A program that talks to a server with a private root names that root
  (`TlsSettings(trusted: [pem])`); that is what the tests do with a root of their own.
- **A failure says who refused**: `isCertificateRejected()` for a chain the platform or mbedTLS did not trust for the
  name, `isTlsFailure()` for everything else of TLS, with the platform's or mbedTLS's words.

## 6. HTTP messages

```trb fragment
public type Method with Show, Equals, Hash {
  case Get
  case Head
  case Post
  // Put, Delete, Patch, Options, Trace, Connect, Query (RFC 10008), Search (RFC 5323)
  case Other(name: String)
  static fn of(name: String): Result<Method, HttpError>
  fn name(): String
}

public type Status with Show, Equals, Hash, Compare {
  static ok = Status(200)
  static notFound = Status(404)
  // and the other registered codes
  static fn of(code: Int): Result<Status, HttpError>
  fn code(): Int
  fn reason(): String
  fn isSuccess(): Bool
}

public type Headers with Show, Equals {
  fn get(name: String): String?
  fn all(name: String): List<String>
  fn contains(name: String): Bool
  var fn set(name: String, value: String)
  var fn add(name: String, value: String)
  var fn remove(name: String)
  fn entries(): List<(name: String, value: String)>
}

public shared type Request {
  method: Method
  target: String
  headers: Headers
  var body: Body
}

public shared type Response {
  status: Status
  headers: Headers
  var body: Body
}
```

- **`Method` is a type with a case per registered method and `Other(name)` for the rest**, so a router matches on it
  (WEB.md's request, section 13). The set is open by the RFC - a WebDAV server meets `PROPFIND` - which is what
  `Other` is for, and `Method.of(name)` is the one way in from text: it answers the case for a registered name and
  `Other` only for the rest, and refuses a name that is not a token. **`Query` and `Search` are cases too** (the owner,
  2026-09-26): `QUERY` is RFC 10008 (June 2026), a query in the request's content, safe and idempotent, whose response is
  cacheable with the content in the key; `SEARCH` is WebDAV's (RFC 5323), in the IANA registry, safe and idempotent with
  a body. `isSafe()`, `isIdempotent()` and `carriesContent()` say what a method promises: the client announces the
  content of a method that carries some even where it is empty, and the pool sends a request again over a fresh
  connection only for an idempotent method without a body. There is no cache in `std/http`, so QUERY's cache key is
  not this package's yet.
- **`Status` is a capsule over its number, with the common codes as constants** (`Status.notFound`): 599 is a valid
  status nobody registered, a router does not match on it, and `of` refuses a number outside 100-599.
- **`Headers` keeps every field in the order it arrived**, names compared case-insensitively and stored as written.
  Order matters for fields that may repeat (`Set-Cookie`), and a map would lose both the order and the repetition.
  `get` answers the first value, `all` every value, `set` replaces all of one name.
- **`Request` and `Response` are `shared type`s**, as `Response` already was: each owns a `Body`, a stream that is
  read once, and a copy would promise a second read. `target` is the request target as sent (`/users/7?x=1`); the
  server adds `remoteAddress`, the client's `send` takes the URL beside the request.
- **`Body` stays what `std/http` had**: a `Source<Bytes, HttpError>` with `bytes()`, `text()`, `json<Value>()` and a
  limit on each. What is new is that a body knows its length where it has one (`Body.from(text)`, a `Content-Length`),
  so the writer can say `Content-Length` instead of chunking.

## 7. The client

```trb fragment
public fn get(url: String, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn post(url: String, body: Body, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn send(
  method: Method,
  url: String,
  headers: Headers = Headers(),
  body: Body = Body.empty(),
): Task<Result<Response, HttpError>>
```

- **The one-liners come first**, as `std/http` decided before (`fetch` and Bun's lesson): `http.get(url).await()?`
  then `response.body.text().await()?`. Streaming is the same `Body`: a response of any size is read chunk by chunk.
- **The free functions open one connection per request** and send `Connection: close`. A pool of kept-alive
  connections per origin is a `Client` (slice 7) whose `get` is the free function's, so a program that needs the pool
  changes one line (section 7, "The client's pool, as built").
- **The free functions follow no redirect, and no status is a failure.** A `404` is a `Response` whose `status` says
  so; a program that wants a failure writes `response.status.isSuccess()`. A redirect policy is a `Client` setting,
  because a client that follows `Location` across origins sends the request somewhere its author did not name.
- **`https` is the same request over TLS** since slice 6 (section 5), on port 443 by default; `send(..., tls:)` takes the
  `TlsSettings` of a program that trusts a root of its own. Any scheme but `http` and `https` is an `HttpError`.
- **A timeout is `within`**, as for every task: `http.get(url).within(10.seconds())`.

### The client's pool, as built (slice 7)

```trb fragment
public shared type Client {
  maximumConnections: Int = 6
  timeout: Duration? = None
  redirects: RedirectPolicy = RedirectPolicy.SameHost(10)
  tls: TlsSettings = TlsSettings()
  fn get(url: Uri, headers: Headers = Headers()): Task<Result<Response, HttpError>>
  fn post(url: Uri, body: Body, headers: Headers = Headers()): Task<Result<Response, HttpError>>
  fn send(method: Method, url: Uri, headers: Headers = Headers(), body: Body = Body.empty()): Task<Result<Response, HttpError>>
}
public type RedirectPolicy {
  case Never
  case SameHost(limit: Int)
  case AnyHost(limit: Int)
}
```

- **A `Client` is a `shared type` with its settings as fields**, built with its constructor - `Client(maximumConnections:
  4, timeout: Some(10.seconds()))` - and its pool made by the first request, because a field's default is a constant and
  an object is none. It is an object, so it stays with the task that made it (CONCURRENCY section 1): every request of
  one client runs on that task's worker, and the pool needs no lock - tasks of one worker change it only between two
  waits. That is the answer to the confinement question slice 3 left open, and it is the simple one: a connection task
  with a channel per request (the shape HTTP/2 needs, section 12) is not needed for HTTP/1.1, where a connection carries
  one request at a time anyway.
- **The pool is per origin** (scheme, host in lower case, port), because a connection is reusable exactly for its
  origin. **`maximumConnections` counts per origin** and defaults to 6, what browsers open to one origin and what servers
  expect of one client. A request beyond it waits, in order, on a channel of its own, and a slot is handed to it by
  dropping that channel's writing end - so giving a slot back starts no task and waits for nothing, which matters
  because it happens in the release of a response, possibly inside a task that is being cancelled (a task started there
  would be cancelled with it, and the slot lost). A request cancelled while it waits takes no slot, or passes on the one
  it was handed; the lease of a request that fails or is cancelled anywhere gives its slot back when it is released.
  Idle connections are taken newest first.
- **A connection goes back when its response's body was read to its end**, on a connection that may carry another
  request: HTTP/1.1 without `Connection: close`, a body framed by a length or chunks (not by the end of the connection),
  and nothing buffered beyond it. **A response released before its end closes its connection** (the body's `close()`,
  which the release runs): reading the rest of a body nobody wants only to reuse the connection can cost more than a new
  one, and a slow or endless body would hold the slot. Either way the slot is free again.
- **A stale connection is sent once more.** A server may close a kept-alive connection while it is idle; a request that
  finds its reused connection closed before any byte of a response - the write fails, or the head reads the end of the
  stream - is sent again over a new connection where its method is idempotent (RFC 9110 section 9.2.2) and its body
  empty, as Go's client does. A body that was a stream is gone, and the failure is the answer: the retry of a request
  with a body is a later refinement that needs a body that can be read twice.
- **`timeout` is `None` by default**, as for the free functions: a timeout is `within`. Where it is set it limits each
  request up to the head of its response, redirects included, and answers `HttpError.timeout()`.
- **The redirect policy: `SameHost(10)` for a `Client`, `Never` for the free functions** (the owner's rule of this
  section for the free functions; the `Client` default decided with slice 7). Same host rather than same origin, because
  the most common redirect is `http` to `https` of one host, which a same-origin rule would refuse; same host rather than
  any host, because a program that named a host is then answered by that host and its credentials go nowhere else - the
  concern above. `AnyHost(limit)` follows everywhere and drops `Authorization`, `Cookie` and `Proxy-Authorization` where
  the host changes, as browsers and Go do. No policy follows `https` to `http`. `303`, and `301` or `302` after a `POST`,
  become a `GET` without a body (and without `Content-Type`), as browsers do; `307` and `308` keep the method and the
  body, and are answered as they are where the body was a stream that cannot be sent again. What is left of a redirect's
  own body is read, up to 64 KiB, so its connection goes back to the pool. More than the limit is
  `HttpError.tooManyRedirects(limit)`.
- **The free `get`, `post` and `send` stay one connection per request** and do not delegate to a default client: a
  default client would be one object for every task of the program, which confinement forbids, and a module constant
  cannot be an object. They share the request writer, the head reader and the body framing with the `Client`
  (`client.trb`), so the two cannot drift.
- **Verified** by `tests/conformance/http-client-pool.trb` over loopback, natively and in the VM: two requests of one
  client arrive over one connection (the server sees one remote port), the free functions open one each, a response
  released unread gives its connection up, one connection allowed makes the second request wait for the first body, and
  every rule of the redirect policy above.

## 8. The server

```trb fragment
public shared type Server with Close {
  static fn listen(
    address: SocketAddress,
    handler: (request: Request) => Task<Result<Response, HttpError>>,
    limits: ServerLimits = ServerLimits(),
    tls: ServerIdentity? = None,
  ): Result<Server, HttpError>
  fn localAddress(): SocketAddress
  var fn serve(): Task<Result<Void, HttpError>>
  var fn serveOne(): Task<Result<Void, HttpError>>
  var fn shutdown(grace: Duration = 10.seconds()): Task<Void>
  var fn close()
}
```

- **The server is a value, closed or shut down gracefully** (the owner's decision, section 14). `shutdown(grace)`
  stops accepting at once, closes every connection that waits for its next request, lets every request in progress
  finish and answers it with `Connection: close`, and cancels what still runs once `grace` has passed. `close()` - what
  the release of the server runs - stops at once: the listener closes and every connection's task is cancelled. There
  is no free `serve` function.
- **No `Server` field** is written (the owner's decision): a product name on the wire tells an attacker what to try
  first. A handler that wants one sets it.

- **A handler is a function from a request to a task of a response.** It is the smallest contract that is still
  asynchronous and still a value: a web framework (the next round's) is a function of this type built out of routes,
  and middleware is a function from one handler to another. A handler that fails answers `500 Internal Server Error`
  and the failure is not sent to the client, whose business it is not.
- **`serve()` accepts until the server is shut down or closed; each connection is a task**, started by a task function
  with the stream as its argument, so it is pinned to the worker that accepted it (section 4). `serveOne()` accepts and
  serves exactly one connection, which is what a test wants.
- **A client that goes away cancels its handler** (slice 13). While the handler of a request without a body runs, the
  connection task reads the connection beside it (`watchPeer`): the end of the stream or a failed read means the client
  is gone, and the handler's task is cancelled - with every task it started, and every `using` of it closed - and the
  connection ends without an answer. Bytes that arrive instead are the client's next request (pipelining), kept in the
  buffer for their turn, and the watch ends there. Once the handler answered, the watch is cancelled and waited for; a
  receive that is cancelled loses nothing (section 3), so the next request reads what arrived. **A request with a body
  is not watched**: the handler reads the body from the same connection, and two readers of one stream cannot share it;
  a client that goes away while its body is read makes that read fail, which is the handler's to see. Watching after the
  body was read to its end is the refinement. **A client that half-closes** after its request - ends its writing half
  and waits for the answer, which a raw HTTP/1.0 tool may do - reads as gone too, as it does in Go's server; HTTP/1.1
  clients do not.
- **A connection serves requests one after another** (HTTP/1.1 persistent connections): the next request is read only
  after the response was written, which also serves a pipelining client correctly, in order. The connection ends after
  a response to `Connection: close`, after an HTTP/1.0 request without `keep-alive`, after an error the parser found,
  and after a handler that did not read its request body to the end where the rest is longer than 64 KiB - reading a
  large unread body only to find the next request is a denial of service, and closing is what every server does.
- **A response body is written with `Content-Length` where its length is known and chunked where it is not**, and a
  `HEAD` request gets the head alone. The server writes `Date` and `Server` only where the handler did not.

**The limits, each with its reason:**

| Limit | Default | Why that number |
|---|---|---|
| `headBytes` | 64 KiB | the request line and every field together; nginx's default is 8 KiB per line and 32 KiB total, and a cookie-heavy browser needs more than 8 |
| `headFields` | 100 | the count Go and Node refuse above |
| `headMilliseconds` | 10 s | the time from the first byte of a request to the end of its head: slowloris |
| `idleMilliseconds` | 60 s | a kept-alive connection waiting for the first byte of its next request |
| `drainBytes` | 64 KiB | what of a request's body the handler left unread is read and dropped up to this before the next request; more closes the connection |

The times are milliseconds and not `Duration`s because a field's default is a constant and a `Duration` is none (the
reason `SandboxCapabilities` counts milliseconds too). A body has no limit of the server's: the convenience readers
have `Body.defaultLimit`, and a handler that streams decides for itself.

A request that breaks a limit is answered `431` (headers) or `408` (time) where the connection can still carry an
answer, and the connection is closed.

## 9. Parsing safety

**The parser is strict, and every refusal is a status and a closed connection.** Request smuggling lives in the space
between two parsers that disagree, so the rule is to accept nothing a stricter parser downstream could read otherwise
(RFC 9112 sections 6 and 11.2):

| Input | Answer |
|---|---|
| `Transfer-Encoding` and `Content-Length` in one request | `400`, closed: the classic smuggling vector; RFC 9112 6.1 allows either, and refusing is the choice that cannot be misread |
| two `Content-Length` fields, or one with a list or a sign | `400` |
| a `Transfer-Encoding` other than exactly `chunked` | `501`: gzip or a list is not built, and guessing is how smuggling starts |
| a bare `CR` or `LF` in the head, or a line that ends in `LF` alone | `400`: every line ends in `CRLF` |
| whitespace between a field name and its colon | `400` (RFC 9112 5.1: MUST) |
| a continuation line (obsolete line folding) | `400` |
| a field name that is not a token, a value with a control character | `400` |
| no `Host` in HTTP/1.1, or two | `400` (RFC 9112 3.2) |
| a version other than `HTTP/1.0` or `HTTP/1.1` | `505` |
| a method that is not a token | `400` |
| a chunk size of more than 15 hex digits, a chunk extension longer than 4 KiB | `400` |
| the head past `headBytes`, more than `headFields` fields | `431` |

The client reads responses with the same parser and refuses the same things, because a response split by a lenient
client is the mirror attack (response queue poisoning). A response to `HEAD`, a `1xx`, `204` and `304` have no body
whatever their headers say. `100 Continue` is skipped by the client; the server does not send it in slice 4 (a client
that sends `Expect: 100-continue` waits a second and then sends the body, which every client does).

## 10. Capabilities

**The network is a capability of the sandbox, with two grants: `connect` names hosts and ports a script may reach,
`listen` the ports it may bind.** SCRIPTS.md section 4's table gets this row in place of "its natives are milestone 8":

| Capability | How it is granted | The first lock (import) | The second lock (runtime) |
|---|---|---|---|
| The network | `network connect: "api.example.test:443", listen: "127.0.0.1:8080"` | `std/network` (or `std/http`, `std/tls`) in `modules` | `resolve` of a host no `connect` pattern names, a `connect` to an address that was neither granted nor resolved from a granted host, a `listen` on an ungranted address: each stops the script |

- **A pattern is `host:port`, and either side may be `*`**: `"*.example.test:443"`, `"10.0.0.5:*"`. A host pattern is
  matched at `resolve`, and the addresses a granted host resolved to become connectable for that port; a literal
  address is matched at `connect`. So a script that was granted `api.example.test:443` cannot reach `10.0.0.1` by
  writing it, nor `api.example.test:22`.
- **TLS and HTTP add no capability.** An HTTPS request is a resolve and a connect; the trust store is read by the
  runtime, which is the host's, like the clock the timer reads.
- **The default is none**, as for every capability: a script without `network` that imports `std/network` does not
  load.
- **Nothing is enforced before a script can run a task**: scripts run in the VM (SCRIPTS.md section 1), and the VM
  runs no task yet. The grant words and the runtime's check are slice 10 and land together with the VM's tasks, so
  that the check is tested by a script that tries.

### A package's capability summary

**The capabilities a package can use are a function of its imports, and the compiler computes them.** A `native` is
only allowed in `std` (`checker/declaration.trb`), so a package reaches the network, the file system, processes or the
environment only through a module of `std` that does. Each such module carries its capability; a package's summary is
the union over every module it imports, transitively through its dependencies:

| Module of `std` | Capability |
|---|---|
| `std/network`, `std/tls`, `std/http` | network |
| `std/fs` | files |
| `std/process` | processes |
| `std/environment`, `std/os/environment` | environment |
| `std/time` (the clock), `std/os/…` | the per-module grants of OS.md section 8 |

`torb check --capabilities <package>` prints it, and the registry shows it beside every version, so "this JSON
library can open sockets" is visible before anybody installs it. A version that gains a capability its predecessor did
not have is flagged by the registry. It is slice 11, with the registry.

## 11. Slices

| # | Scope | State |
|---|---|---|
| 1 | This record | **Done** |
| 2 | The IO core: `runtime/io.c`, `torb_io.h`, the IO thread and `TORB_WAITING_IO` in `task.c`, IOCP + Winsock (loaded on first use), epoll, kqueue; TCP listen, accept, connect, receive, send, shutdown, close, addresses; the resolver threads; `runtime/tests/io_test.c` | **Done** on Windows; POSIX written, not compiled |
| 3 | `std/network`: the address types, `resolve`, `TcpListener`, `TcpStream` with its source and sink, `NetworkError`; conformance programs over loopback | **Done** |
| 4 | `std/http`: messages, the HTTP/1.1 parser and writer, the server and the client (one connection per request); parser tests; conformance over loopback | **Done** |
| 5 | UDP: `UdpSocket` with bind, send, receive, connect and its peer, over IOCP, epoll and kqueue (section 4, "UDP, as built") | **Done** on Windows and Linux; kqueue written, not run |
| 6 | TLS: mbedTLS vendored, compiled only where it is reached, the platform verifiers, `std/tls`, `https` in the client and the server | **Done** (Windows' verifier; macOS verifies against its bundle until the Security framework step) |
| 7 | The client's connection pool and redirect policy (`Client`, section 7) | **Done** |
| 8 | Serving on every worker: several accept loops on one listening socket (section 4, "As built") | **Done** |
| 9 | A poller per worker, if a benchmark asks for it | Open |
| 10 | The sandbox's network grant, with the VM's tasks | Open |
| 11 | A package's capability summary, with the registry | Open |
| 12 | HTTP/2, then HTTP/3 (section 12) | Open |
| 13 | A disconnected client cancels its handler (section 13, WEB.md's fourth request) | **Done** for requests without a body |
| 14 | `101 Switching Protocols` and the duplex connection of the live UI (section 13, fifth request) | Open |

## 12. HTTP/2, HTTP/3 and compression

- **HTTP/2 is after TLS, and after the client's pool.** Browsers speak it only over TLS with ALPN, so without slice 6
  there is no peer for it; and it multiplexes many requests over one connection, which is a connection task with a
  channel per stream - the shape slice 7 builds for the pool. HPACK and the flow-control windows are TorbScript over
  that, and the messages of section 6 do not change.
- **HTTP/3 is QUIC**: a transport of its own over UDP, with TLS 1.3 inside it and congestion control, recovery and
  connection migration. It needs slice 5 and a TLS that exposes its handshake to QUIC, which mbedTLS does not. It is the
  last slice here and may be a vendored QUIC stack rather than TorbScript; the decision waits until HTTP/2 is used.
- **Compression** is a pair of `Stage`s (`gzip`, `deflate`, STREAMS section 14), so `Content-Encoding` is a stage
  on the body, in either direction. It needs an inflate and a deflate, which do not exist in the repository yet; a
  request with `Transfer-Encoding: gzip` is `501` until then (section 9).

## 13. What `docs/design/WEB.md` asked for

WEB.md was written in parallel and assumes one thing of this record - a handler is a function from a `Request` to a
task of a `Response` - and asks six more in its section 9. The answers, each with its reason:

| # | Request | Answer |
|---|---|---|
| 1 | `Request` a value except its body, the method a type with cases | **The method: accepted.** `Method` is a type with a case per registered method and `Other(name)` for the rest; `Method.of(name)` is the one way in from text and never answers `Other` for a registered name, so `match request.method { .Get => ... }` works and two spellings of one method cannot exist. **The request: kept a `shared type`**, because it owns its body, a stream read once, and a copy would promise a second read. Everything else in it is a value (`Method`, the target, `Headers`, the peer's `SocketAddress`), so a middleware builds a new `Request` from the old one's parts and hands the same body on, which is all a `copy` would have done |
| 2 | The body a `Source<Bytes, _>` read with a limit | **Accepted, as built**: `Body` is a `Source<Bytes, HttpError>`, and every reader that holds a whole body takes a limit. Forms, JSON and queries are decoded in `std/web` |
| 3 | A response body may be a stream | **Accepted, as built**: a body without a known length is sent in chunks while it is produced |
| 4 | A disconnected client cancels the handler's task | **Accepted, slice 13, built** (section 8, "A client that goes away"): a read of the connection beside the running handler - the end of the stream or a reset cancels the handler's task, and bytes that arrive are kept for the next request. For a request without a body; one with a body is watched by its own body reads |
| 5 | A duplex connection for the live UI | **Accepted, slice 14**: a handler answers `101 Switching Protocols` with an upgrade closure, and the server hands it the connection's `source()` and `sink()` once the head is written; WebSocket framing is a `Stage` of `std/web`, as WEB.md proposes |
| 6 | One program as several processes | **Decided: a listening socket handed to a child**, not `SO_REUSEPORT`. Only Linux balances `SO_REUSEPORT` across processes (FreeBSD needs `SO_REUSEPORT_LB`, Windows has neither), so it is not one behaviour on every platform; inheriting a socket (`WSADuplicateSocketW`, a descriptor without `FD_CLOEXEC`) is. It rides with the supervisor WEB.md section 3.8 wants, after slice 8 |

## 14. Owner questions, answered

Everything above is decided and the reason stands next to it. These three were questions of taste, answered by the owner
on 2026-09-24:

1. **`std/network` against `std/net`.** **Decided: `std/network`**, by the full-word rule.
2. **`Server.listen(address, handler)` against `http.serve(address, handler)`.** **Decided: `Server.listen` answers a
   `Server` value that can be closed and shut down gracefully; no free `serve`.** Section 8 has the shutdown.
3. **The `Server` header.** **Decided: none by default** - no fingerprinting; a handler that wants one sets it.
