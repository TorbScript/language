---
title: std/network
summary: Name resolution and TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - over the address values of std/ip, which it re-exports.
kind: package
status: stable
order: 175
keywords:
  - std/network
  - TCP
  - socket
  - IpAddress
  - SocketAddress
  - TcpListener
  - TcpStream
  - resolve
  - NetworkError
source:
  - std/network/src/lib.trb
  - std/network/src/error.trb
  - std/network/src/tcp.trb
  - docs/design/NETWORK.md
---

`std/network` is the network below HTTP: name resolution and TCP, over the address values of [std/ip](ip.md). Everything that waits for the
network answers a `Task` and can be cancelled; a timeout is [`within`](task.md). It runs on the IO core of the runtime -
an IO completion port on Windows, epoll on Linux, kqueue on macOS and FreeBSD - which wakes a task when its bytes
arrive, so a thousand connections wait on one thread (see [docs/design/NETWORK.md](../design/NETWORK.md)). It needs the
network capability inside a sandboxed script, and is not in the prelude.

## Import

```trb fragment
use TcpListener, TcpStream, SocketAddress, IpAddress, NetworkError, resolve from "std/network"
```

```trb check
use TcpListener, TcpStream, SocketAddress, IpAddress, NetworkError from "std/network"

/** Echoes one connection back to itself, and answers how many bytes it echoed. */
fn echoOnce(listener: TcpListener): Task<Result<Int, NetworkError>> {
  const connection = listener.accept().await()?
  var echoed = 0
  while const Some(chunk) = connection.receive().await()? {
    echoed = echoed + chunk.length()
    connection.send(chunk).await()?
  }
  connection.shutdown()?
  Ok echoed
}
```

## Declarations

### Addresses

```trb fragment
public use AddressError, Ipv4Address, Ipv6Address, IpAddress, SocketAddress from "std/ip"
```

The address values are [std/ip](ip.md)'s, re-exported here: a package that only names an address imports `std/ip` and
does not claim the network, and a program that connects imports them from here as it always did. Both names are one
type.

### `resolve`

```trb fragment
public fn resolve(host: String): Task<Result<List<IpAddress>, NetworkError>>
```

The addresses a host name stands for. A literal address answers itself and `localhost` answers the two loopback
addresses without asking anybody, so a program that talks to itself needs no resolver; every other name goes to the
system's resolver, on a thread of the runtime.

### TcpListener

```trb fragment
public shared type TcpListener with Close {
  static fn listen(address: SocketAddress, backlog: Int = 128): Result<TcpListener, NetworkError>
  fn localAddress(): SocketAddress
  fn accept(): Task<Result<TcpStream, NetworkError>>
  fn stop()
}
```

A socket that listens. `listen` is synchronous - binding never waits for the network - and port 0 asks the system for
a free port, which `localAddress()` then says. An IPv6 listener hears IPv6 only, on every system alike. `accept` waits
for the next connection; an accept that is cancelled leaves the connection for the next one. `stop()` closes the socket
before the listener is released, and an `accept` that waits then fails with `isClosed()` - which is how a server stops
accepting while it is shut down. Otherwise the socket closes when the last reference to the listener goes.

### TcpStream

```trb fragment
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

One connection. `receive` answers as soon as any bytes arrived, and `None` once the peer ended its half; nothing is read
before somebody asks, so a slow reader slows the peer down. `send` finishes once the operating system took the bytes.
`shutdown` ends this half: the peer reads the end of the stream and may still answer. `connectTo` resolves the host and
tries its addresses in turn. `TCP_NODELAY` is on. `source()` and `sink()` are the two directions as a
`Source<Bytes, NetworkError>` and a `Sink<Bytes, NetworkError>` (see [std/stream](stream.md)); the socket closes when the
last of the stream, its source and its sink is gone.

### NetworkError

```trb fragment
public type NetworkError with Show, Error {
  fn isConnectionRefused(): Bool
  fn isConnectionReset(): Bool
  fn isTimedOut(): Bool
  fn isAddressInUse(): Bool
  fn isHostNotFound(): Bool
  fn isUnreachable(): Bool
  fn isClosed(): Bool
  fn isTlsFailure(): Bool
  fn isCertificateRejected(): Bool
  fn isRetryable(): Bool
}
```

What went wrong, as one wrapper type: a new kind of failure is never a breaking change. The kind is a question, and
`show()` is the operating system's own words. A cancelled wait is not one of its kinds: cancelling a task ends it, and
`result()` of the task says `Cancelled`.
`isTlsFailure()` and `isCertificateRejected()` are the failures of [std/tls](tls.md), which reports through this type
too.

## Related

- [std/ip](ip.md) - the address values this package connects to.
- [std/tls](tls.md) - TLS over a `TcpStream`.
- [std/http](http.md) - HTTP/1.1 over these streams.
- [std/stream](stream.md) - `Source`, `Sink` and `Bytes`.
- [std/task](task.md) - `within`, the timeout of every network task, and `cancel`.
- [The standard library](index.md) - the other packages.
