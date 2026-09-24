---
title: std/tls
summary: TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, and a stream like the TCP one.
kind: package
status: stable
order: 178
keywords:
  - std/tls
  - TLS
  - HTTPS
  - certificate
  - TlsStream
  - TlsSettings
  - ServerIdentity
source:
  - std/tls/src/lib.trb
  - runtime/tls/tls.c
  - docs/design/NETWORK.md
---

`std/tls` puts TLS over a [std/network](network.md) `TcpStream`: `TlsStream.connect` for a client, `TlsStream.accept`
with a `ServerIdentity` for a server, and then a stream that receives and sends like the TCP one. The protocol is
mbedTLS 3.6, TLS 1.2 and 1.3, vendored into the runtime and linked only into a program that reaches `std/tls`.
[std/http](http.md) is built on it: `https` URLs and `Server.listen(..., tls: identity)`. It needs the network
capability inside a sandboxed script, and is not in the prelude.

## Import

```trb fragment
use TlsStream, TlsSettings, ServerIdentity from "std/tls"
```

```trb check
use TcpStream, NetworkError from "std/network"
use TlsStream from "std/tls"
use textOf from "std/stream"

/** Asks a server for its front page over TLS and answers the first chunk of the answer. */
fn frontPage(host: String): Task<Result<String, NetworkError>> {
  const tcp = TcpStream.connectTo(host, 443).await()?
  var stream = TlsStream.connect(tcp, host).await()?
  stream.send("GET / HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".bytes().toList()).await()?
  const chunk = stream.receive().await()? ?? []
  Ok(textOf(chunk).ok() ?? "")
}
```

## Declarations

### TlsStream

```trb fragment
public shared type TlsStream with Close {
  static fn connect(stream: TcpStream, serverName: String, settings: TlsSettings = TlsSettings()): Task<Result<TlsStream, NetworkError>>
  static fn accept(stream: TcpStream, identity: ServerIdentity): Task<Result<TlsStream, NetworkError>>
  fn protocol(): String
  fn localAddress(): Result<SocketAddress, NetworkError>
  fn remoteAddress(): Result<SocketAddress, NetworkError>
  var fn receive(maximum: Int = 65536): Task<Result<Bytes?, NetworkError>>
  var fn send(bytes: Bytes): Task<Result<Void, NetworkError>>
  var fn shutdown(): Task<Result<Void, NetworkError>>
  fn source(chunk: Int = 65536): TlsSource
  fn sink(): TlsSink
}
```

One TLS connection. `connect` runs the handshake as a client and checks that the server's certificate is trusted for
`serverName`; `accept` runs it as a server. Both finish before a byte of data crosses, so a certificate that is refused
is refused before the program sends anything. `protocol()` is `TLSv1.3` or `TLSv1.2`. `receive` answers `None` once
the peer ended the session with `close_notify`; a peer that just drops the connection is a failure, because an attacker
can cut a connection but cannot forge its end. `shutdown` sends `close_notify` and ends the TCP half. Every wait is a
wait of the `TcpStream` under it, so a TLS read is cancelled and timed out ([`within`](task.md)) exactly as a TCP
read is. `source()` and `sink()` are the two directions as a `Source<Bytes, NetworkError>` and a
`Sink<Bytes, NetworkError>` (see [std/stream](stream.md)).

### TlsSettings

```trb fragment
public type TlsSettings {
  trusted: List<String> = []
}
```

What a client trusts. The default is the platform's judgement: on Windows the certificate chain goes to the system's
own verifier - its roots, the enterprise's roots, its policies - and elsewhere the system's bundle of roots decides.
`trusted` names PEM certificates to trust instead, and then only those: a company's private root, a test's own root.
There is no setting that turns the check off. Revocation is not checked, as in Go and rustls: a check goes to the
network and waits.

### ServerIdentity

```trb fragment
public shared type ServerIdentity with Close {
  static fn of(certificates: String, privateKey: String): Result<ServerIdentity, NetworkError>
}
```

What a server proves who it is with: its PEM certificate chain - its own certificate first, then the intermediates -
and the PEM private key of the first certificate. It is read once and shared by every connection; a key that is not the
certificate's is refused here, not at the first handshake.

### Failures

A failure of TLS is a [`NetworkError`](network.md): `isCertificateRejected()` where the peer's certificate is not
trusted for the name, `isTlsFailure()` for every other failure of the protocol, and `show()` says why in the words of
the platform's verifier or of mbedTLS.

## Related

- [std/network](network.md) - the TCP stream under TLS, and `NetworkError`.
- [std/http](http.md) - HTTPS in the client and the server.
- docs/design/NETWORK.md - section 5, why mbedTLS and the platform's verifier.
- [The standard library](index.md) - the other packages.

