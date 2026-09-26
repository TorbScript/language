---
title: std/network
summary: Name resolution and DNS lookups, TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - and UDP datagrams, over the address values of std/ip, which it re-exports.
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
  - UDP
  - UdpSocket
  - datagram
  - resolve
  - lookup
  - Resolver
  - DNS
  - NetworkError
source:
  - std/network/src/lib.trb
  - std/network/src/error.trb
  - std/network/src/tcp.trb
  - std/network/src/udp.trb
  - std/network/src/dns.trb
  - docs/design/NETWORK.md
---

`std/network` is the network below HTTP: name resolution, DNS lookups, TCP and UDP, over the address values of [std/ip](ip.md). Everything that waits for the
network answers a `Task` and can be cancelled; a timeout is [`within`](task.md). It runs on the IO core of the runtime -
an IO completion port on Windows, epoll on Linux, kqueue on macOS and FreeBSD - which wakes a task when its bytes
arrive, so a thousand connections wait on one thread (see [docs/design/NETWORK.md](../design/NETWORK.md)). It needs the
network capability inside a sandboxed script, and is not in the prelude.

## Import

```trb fragment
use TcpListener, TcpStream, UdpSocket, SocketAddress, IpAddress, NetworkError, resolve from "std/network"
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

### `lookup` and `Resolver`

```trb fragment
public type Resolver {
  servers: List<SocketAddress> = []
  attemptMilliseconds: Int = 2000
  rounds: Int = 2
  fn lookup(name: DomainName, recordType: RecordType): Task<Result<List<Record>, NetworkError>>
  fn exchange(name: DomainName, recordType: RecordType): Task<Result<Message, NetworkError>>
}
public fn lookup(name: DomainName, recordType: RecordType): Task<Result<List<Record>, NetworkError>>
public fn systemNameServers(): Result<List<SocketAddress>, NetworkError>
public fn randomQueryIdentifier(): Int
```

A stub resolver over the messages of [std/dns](dns.md): the records of a type that a name has - `MX`, `TXT`, `SRV`,
`HTTPS`, or addresses - asked of a recursive name server. `lookup` answers the records, CNAME chains followed, an empty
list for a name without such records, and `isHostNotFound()` for a name that does not exist; `exchange` answers the
whole response. Each attempt is a UDP datagram with a random identifier from a new socket; a truncated answer is asked
again over TCP; no answer within `attemptMilliseconds`, a refusal, or `SERVFAIL`, `REFUSED` and `NOTIMP` hand the
question to the next server, for `rounds` rounds. An empty `servers` asks the ones the system is configured with -
`systemNameServers()`: the adapters' on Windows, `/etc/resolv.conf` elsewhere - and the free `lookup` is `Resolver()`'s.
There is no cache: `resolve` stays what connecting uses, with the system's cache and hosts file. DNS over TLS is
[std/tls](tls.md)'s `TlsResolver`, which draws its identifiers from `randomQueryIdentifier()` too.

```trb check
use Resolver, SocketAddress, IpAddress, NetworkError from "std/network"
use DomainName, RecordType from "std/dns"

/** The mail exchanges of `domain`, lowest preference first, as a server on this machine answers them. */
fn mailExchanges(domain: String): Task<Result<List<String>, NetworkError>> {
  const resolver = Resolver servers: [SocketAddress(IpAddress.loopback, 53)]
  const records = resolver.lookup(DomainName.tryFrom(domain)?, RecordType.Mx).await()?
  Ok records.map({ _.data.show() }).toList()
}
```

### TcpListener

```trb fragment
public shared type TcpListener with Close {
  static fn listen(address: SocketAddress, backlog: Int = 128): Result<TcpListener, NetworkError>
  fn localAddress(): SocketAddress
  fn accept(): Task<Result<TcpStream, NetworkError>>
  fn acceptor(): TcpAcceptor
  fn stop()
}

public type TcpAcceptor {
  fn accept(): Task<Result<TcpStream, NetworkError>>
}
```

A socket that listens. `listen` is synchronous - binding never waits for the network - and port 0 asks the system for
a free port, which `localAddress()` then says. An IPv6 listener hears IPv6 only, on every system alike. `accept` waits
for the next connection; an accept that is cancelled leaves the connection for the next one. `stop()` closes the socket
before the listener is released, and an `accept` that waits then fails with `isClosed()` - which is how a server stops
accepting while it is shut down. Otherwise the socket closes when the last reference to the listener goes.
`acceptor()` hands out the right to accept as a value - the socket's handle and nothing else - which a task may take to
another worker: several accept loops on one listening socket, each connection going to one of them, are how a server
serves on every core. The listener still owns the socket, and once it stops every acceptor's `accept` fails with
`isClosed()`.

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

### UdpSocket

```trb fragment
public shared type UdpSocket with Close {
  static fn bind(address: SocketAddress): Result<UdpSocket, NetworkError>
  fn localAddress(): SocketAddress
  fn connect(address: SocketAddress): Result<Void, NetworkError>
  fn peerAddress(): SocketAddress?
  fn send(bytes: Bytes, to: SocketAddress): Task<Result<Void, NetworkError>>
  fn sendToPeer(bytes: Bytes): Task<Result<Void, NetworkError>>
  fn receive(): Task<Result<(Bytes, SocketAddress), NetworkError>>
}
```

A UDP socket: datagrams, each one whole or not at all, with the address it came from. A datagram is not a stream, so
the socket is no `Source`: `receive` answers the next datagram and its sender - a datagram without bytes is one too -
and `send` hands one datagram to the operating system, which says nothing of whether it arrives. `bind` is synchronous,
port 0 asks for a free port, and an IPv6 socket binds IPv6 only. `connect` names one peer without sending anything:
from then on only the peer's datagrams arrive, `sendToPeer` sends to it, `send` accepts no other address, and a peer
whose port is closed makes the next `receive` fail with `isConnectionRefused()` - on every system alike, where an
unconnected socket never hears of such a refusal. A datagram that arrives while nobody receives waits in the socket, and
a cancelled `receive` leaves it to the next one; a timeout is `within`, which is how a protocol over UDP asks again.

```trb check
use UdpSocket, SocketAddress, IpAddress, NetworkError from "std/network"

/** Answers one datagram with its bytes reversed, to whoever sent it. */
fn answerOnce(socket: UdpSocket): Task<Result<Void, NetworkError>> {
  const (bytes, sender) = socket.receive().await()?
  socket.send(bytes.reversed(), to: sender).await()
}
```

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
  fn isNameServerFailure(): Bool
  fn isRetryable(): Bool
}
```

What went wrong, as one wrapper type: a new kind of failure is never a breaking change. The kind is a question, and
`show()` is the operating system's own words. A cancelled wait is not one of its kinds: cancelling a task ends it, and
`result()` of the task says `Cancelled`.
`isTlsFailure()` and `isCertificateRejected()` are the failures of [std/tls](tls.md), which reports through this type
too. `isNameServerFailure()` is a lookup's: a name server answered with a failure or with no DNS at all, or the system
names none. A `DnsError` - a name that is no domain name - converts into a `NetworkError`, so `DomainName.tryFrom(text)?`
works in a function that fails with one.

## Related

- [std/ip](ip.md) - the address values this package connects to.
- [std/tls](tls.md) - TLS over a `TcpStream`.
- [std/dns](dns.md) - the messages of the Domain Name System, which a program sends over a `UdpSocket`.
- [std/http](http.md) - HTTP/1.1 over these streams.
- [std/stream](stream.md) - `Source`, `Sink` and `Bytes`.
- [std/task](task.md) - `within`, the timeout of every network task, and `cancel`.
- [The standard library](index.md) - the other packages.
