---
title: std/network
summary: IP and socket addresses as values, name resolution, and TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes.
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
  - std/network/src/address.trb
  - std/network/src/error.trb
  - std/network/src/tcp.trb
  - docs/design/NETWORK.md
---

`std/network` is the network below HTTP: addresses as values, name resolution, and TCP. Everything that waits for the
network answers a `Task` and can be cancelled; a timeout is [`within`](task.md). It runs on the IO core of the runtime -
an IO completion port on Windows, epoll on Linux, kqueue on macOS and FreeBSD - which wakes a task when its bytes
arrive, so a thousand connections wait on one thread (see docs/design/NETWORK.md). It needs the
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
public type AddressError with Show, Error
```

Values like any other, read from text with `tryFrom` and shown as text. An IPv4 address is four decimal parts from 0 to
255; a part with a leading zero is refused, because some systems read `010` as octal. An IPv6 address shows in the RFC
5952 form: lower case, no leading zeros, the longest run of zero segments collapsed to `::`, and an IPv4-mapped
address with its IPv4 part (`::ffff:192.0.2.1`). A zone (`fe80::1%eth0`) is not part of an address and is refused.
`Ipv4Address.loopback`, `Ipv6Address.loopback`, `IpAddress.loopback` and the `unspecified` addresses are constants, and
`isLoopback()`, `isPrivate()`, `isLinkLocal()`, `isMulticast()` answer what they say. A `SocketAddress` shows as
`127.0.0.1:8080` or `[::1]:8080`.

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
}
```

A socket that listens. `listen` is synchronous - binding never waits for the network - and port 0 asks the system for
a free port, which `localAddress()` then says. An IPv6 listener hears IPv6 only, on every system alike. `accept` waits
for the next connection; an accept that is cancelled leaves the connection for the next one. The socket closes when
the last reference to the listener goes.

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
  fn isCancelled(): Bool
  fn isRetryable(): Bool
}
```

What went wrong, as one wrapper type: a new kind of failure is never a breaking change. The kind is a question, and
`show()` is the operating system's own words. It converts from `Cancelled`, so `?` folds a cancelled wait into it.

## Related

- [std/http](http.md) - HTTP/1.1 over these streams.
- [std/stream](stream.md) - `Source`, `Sink` and `Bytes`.
- [std/task](task.md) - `within`, the timeout of every network task, and `cancel`.
- [The standard library](index.md) - the other packages.

