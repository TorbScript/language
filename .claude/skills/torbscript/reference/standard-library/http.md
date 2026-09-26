---
title: std/http
summary: HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
kind: package
status: stable
order: 180
keywords:
  - std/http
  - HTTP
  - HTTPS
  - Request
  - Response
  - Body
  - Server
  - Client
  - connection pool
  - redirect
  - Headers
  - Status
  - Method
source:
  - std/http/src/lib.trb
  - std/http/src/message.trb
  - std/http/src/error.trb
  - std/http/src/client.trb
  - std/http/src/pool.trb
  - std/http/src/server.trb
  - std/http/src/parse.trb
  - std/http/src/write.trb
  - std/http/src/connection.trb
  - std/http/src/transport.trb
  - docs/design/NETWORK.md
  - docs/design/URI.md
---

`std/http` is HTTP/1.1 in TorbScript over [std/network](network.md), and HTTPS over [std/tls](tls.md): a client, a
server, and the messages both speak.
Every body is a stream (see [std/stream](stream.md)): `Body` is a `Source<Bytes, HttpError>`, so a body of any size is
read chunk by chunk, and the convenience that covers the common case (`body.text()`, `body.json<User>()`) sits on top of
it with a limit. It needs the network capability inside a sandboxed script, and is not in the prelude.

## Import

```trb fragment
use * as http from "std/http"
use Server, Request, Response, Status, Method, Headers, Body, HttpError from "std/http"
```

```trb check
use * as http from "std/http"
use Server, Request, Response, Status, HttpError from "std/http"
use SocketAddress, IpAddress from "std/network"

type User {
  id: Int
  name: String
}

fn fetchUser(api: Uri, id: Int): Task<Result<User, HttpError>> {
  var response = http.get(api.joined("users/{id}")).await()?
  response.json<User>().await()
}

fn hello(request: Request): Task<Result<Response, HttpError>> {
  if request.path() == "/hello" {
    return Ok Response.text("hello")
  }
  Ok Response.of(Status.notFound)
}

fn serveForever(): Task<Result<Void, HttpError>> {
  var server = Server.listen(SocketAddress(IpAddress.loopback, 8080), hello)?
  server.serve().await()
}
```

## Declarations

### `get`, `post`, `send`

```trb fragment
public fn get(url: Uri, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn post(url: Uri, body: Body, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn send(
  method: Method,
  url: Uri,
  headers: Headers = Headers(),
  body: Body = Body.empty(),
  tls: TlsSettings = TlsSettings(),
): Task<Result<Response, HttpError>>
```

The client. What it reaches is a [`Uri`](uri.md), read from text once where the text enters the program
(`Uri.tryFrom(text)?`) or built from one (`api.joined("users/{id}")`, which encodes each segment and never climbs above
`api`) - there is no URL string to get wrong at the call. A request opens a connection to the host of the URI - straight
to an IP literal, through the resolver for a name - writes the request - with `Content-Length` where the
body's length is known, in chunks where it is not - and answers once the head of the response arrived; its body is read
from the connection as the program pulls it, and the connection closes with the response. A status that is not a
success is a `Response` too, and no redirect is followed. A timeout is `within`: `http.get(url).within(10.seconds())`.
A URI this client does not reach - a scheme other than `http` and `https`, an empty host, user information (RFC 9110
section 4.2) - is an `HttpError` that is `unsupported`; the fragment is never sent. An `https` URI is the same request
over [TLS](tls.md), on port 443 unless the URI names another: the server's
certificate is checked for the host the way the platform checks it, or against the roots of `tls` where `send` names
some, and a certificate that is refused is an `HttpError` whose `cause()` is the `NetworkError` that
`isCertificateRejected()`.

### Client

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

The client that keeps its connections: `get`, `post` and `send` are the free functions', over a pool of kept-alive
connections per origin (scheme, host and port), at most `maximumConnections` of them open to one origin - a request
beyond waits until a response gives one back. A connection goes back to the pool once its response's body was read to
its end, and is closed where the response is released before that. A kept-alive connection the server closed while it
waited is noticed when a request finds it closed before any byte of a response, and a request without a body is sent
again over a new one. `timeout` limits each request up to the head of its response, redirects included; the body is the
program's to limit with `within`.

Redirects follow `redirects`: a `Client` follows up to ten to the same host (`http` to `https` included), because a
program that named a host gets answers from that host; `AnyHost` follows them anywhere and then drops `Authorization`,
`Cookie` and `Proxy-Authorization` where the host changes; `Never` answers the redirect itself, as the free functions
always do. No policy follows `https` to `http`, or a `307`/`308` that would have to send a streamed body again; `303` -
and `301`/`302` after a `POST` - becomes a `GET` without a body. More redirects than the limit is an `HttpError`
(`tooManyRedirects`). A `Client` is an object: it stays with the task that made it, so the requests of one client run on
that task's worker.

```trb check
use Client, HttpError from "std/http"

/** Two requests to one API over one kept-alive connection. */
fn twoUsers(api: Uri): Task<Result<(String, String), HttpError>> {
  const client = Client maximumConnections: 2
  var first = client.get(api.joined("users/1")).await()?
  const one = first.body.text().await()?
  var second = client.get(api.joined("users/2")).await()?
  Ok((one, second.body.text().await()?))
}
```

### Server

```trb fragment
public type Handler = (request: Request) => Task<Result<Response, HttpError>>

public shared type Server with Close {
  static fn listen(
    address: SocketAddress,
    handler: Handler,
    limits: ServerLimits = ServerLimits(),
    tls: ServerIdentity? = None,
  ): Result<Server, HttpError>
  fn localAddress(): SocketAddress
  var fn serve(): Task<Result<Void, HttpError>>
  var fn serveOne(): Task<Result<Void, HttpError>>
  var fn shutdown(grace: Duration = 10.seconds()): Task<Void>
  var fn close()
}

public type ServerLimits {
  headBytes: Int = 65536
  headFields: Int = 100
  headMilliseconds: Int = 10000
  idleMilliseconds: Int = 60000
  drainBytes: Int = 65536
}
```

The server. A handler is a function from a request to a task of a response - a web framework is one of these, built out
of routes. `serve()` accepts until the server is shut down or closed - with an accept loop on every worker, all of
them on the one listening socket, so connections spread over the cores - and each connection is a task of its own, on
the worker whose loop accepted it, that serves its requests one after another (a persistent connection, and pipelined
requests in order). A handler that holds an object keeps every loop on the serving task's worker, and a server with
`tls` runs one loop. `shutdown(grace)`
stops accepting, closes the connections that wait for their next request, lets the requests in progress finish - each
answered with `Connection: close` - and cancels what still runs after `grace`; then `serve()` answers `Ok`. `close()`,
which the release of the server runs, stops at once and cancels every connection. With `tls`, every connection is
HTTPS: the TLS handshake comes first and has to finish within `headMilliseconds`. The server writes no `Server` field
of its own, so it does not tell a scanner what it is; a handler that wants one sets it. A handler that fails is answered
`500` without telling the client why. A request the parser refuses is answered with the status its failure names -
`400` for a malformed message, `408` for a head that took longer than `headMilliseconds`, `431` for one over
`headBytes` or `headFields`, `501` for a transfer coding that is not `chunked`, `505` for another version - and the
connection is closed. The parser refuses everything request smuggling lives on: a `Content-Length` beside a
`Transfer-Encoding`, two lengths, a bare CR or LF, a folded line, a missing `Host`.

### Messages

```trb fragment
public shared type Request {
  method: Method
  target: String
  uri: Uri
  headers: Headers = Headers()
  var body: Body
  remote: SocketAddress? = None
  version: String = "HTTP/1.1"
  fn path(): String
  fn segments(): List<String>
  fn query(): String?
}

public shared type Response {
  status: Status
  var headers: Headers = Headers()
  var body: Body
  static fn of(status: Status): Response
  static fn text(content: String, status: Status = Status.ok): Response
  static fn jsonOf<Value: Encode>(value: Value, status: Status = Status.ok): Response
  var fn json<Value: Decode>(limit: Int = Body.defaultLimit): Task<Result<Value, HttpError>>
}
```

Both are `shared type`s, because each owns its body, a stream that is read once. A request's `target` is what was sent
(`/users/7?details=true`, `*`, or `host:443` for `CONNECT`), and `uri` is the target URI RFC 9112 section 3.3 rebuilds
from it, the `Host` field and whether the connection is TLS (`https://example.test/users/7?details=true`); a request
whose target and host make no URI is answered `400`. `path()`, `segments()` and `query()` read the URI, so a router sees
a normalized path whose dot segments are gone.

### Method, Status, Headers

```trb fragment
public type Method with Show, Equals, Hash {
  case Get
  case Head
  case Post
  case Put
  case Delete
  case Patch
  case Options
  case Trace
  case Connect
  case Other(name: String)
  static fn of(name: String): Result<Method, HttpError>
  fn name(): String
  fn isSafe(): Bool
}

public type Status with Show, Equals, Hash, Compare {
  static ok, created, noContent, notFound, internalServerError, ...
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
```

`Method` has a case per registered method, so a router matches on it, and `Other` for the rest: `Method.of(name)` is
the way in from text, and it never answers `Other` for a registered name. `Status` is a capsule over its number with the
common codes as constants, because 599 is a status too. `Headers`
keeps every field in the order it arrived, compares names without regard to case, and keeps a name that repeats
(`Set-Cookie`). A value with a line break is written with a space in its place and a name that is not a token is not
written at all, so a header built from what a client sent cannot carry a second header in. `Content-Length`,
`Transfer-Encoding` and `Connection` belong to the writer, which sets them from the body and the connection.

### Body

```trb fragment
public shared type Body with Source<Bytes, HttpError> {
  static defaultLimit: Int = 16777216
  fn length(): Int?
  var fn bytes(limit: Int = Body.defaultLimit): Task<Result<Bytes, HttpError>>
  var fn text(limit: Int = Body.defaultLimit): Task<Result<String, HttpError>>
  var fn json<Value: Decode>(limit: Int = Body.defaultLimit): Task<Result<Value, HttpError>>
  var fn lines(): Source<String, HttpError>
  static fn of(var source: Source<Bytes, HttpError>, length: Int? = None): Body
  static fn empty(): Body
  static fn jsonOf<Value: Encode>(value: Value): Body
}
```

The bytes of a message, read once. `defaultLimit` is 16 MiB: a peer that decides how much memory a program allocates is
a denial of service, and streaming is the way past it, not a bigger number. `length()` is known for a body made from
text or bytes and for one that arrived with `Content-Length`; a body of unknown length is sent in chunks.
`Body.from("text")` and `Body.from(bytes)` come from `From`.

### HttpError

```trb fragment
public type HttpError with Show, Error {
  fn isTimeout(): Bool
  fn isRetryable(): Bool
  fn statusCode(): Int?
  fn answerStatus(): Int?
  fn cause(): Error?
}
```

What went wrong, as one wrapper type: a new kind of failure is never a breaking change. `answerStatus()` is the status
a server answers a request with that failed this way while it was read; a failure of the network under a message keeps
its `NetworkError` as `cause()`.

## Related

- [std/network](network.md) - the TCP streams under HTTP.
- [std/tls](tls.md) - the TLS under HTTPS: `TlsSettings` and `ServerIdentity`.
- [std/stream](stream.md) - `Source`, which `Body` is, and `Sink`.
- [std/json](json.md) - `Json().decode`, which `Response.json` and `Body.json` call.
- [The standard library](index.md) - the other packages.

