---
title: std/http
summary: HTTP/1.1, client and server, over std/network - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
kind: package
status: stable
order: 180
keywords:
  - std/http
  - HTTP
  - Request
  - Response
  - Body
  - Server
  - Headers
  - Status
  - Method
source:
  - std/http/src/lib.trb
  - std/http/src/message.trb
  - std/http/src/error.trb
  - std/http/src/client.trb
  - std/http/src/server.trb
  - std/http/src/parse.trb
  - std/http/src/write.trb
  - std/http/src/connection.trb
  - docs/design/NETWORK.md
---

`std/http` is HTTP/1.1 in TorbScript over [std/network](network.md): a client, a server, and the messages both speak.
Every body is a stream (see [std/stream](stream.md)): `Body` is a `Source<Bytes, HttpError>`, so a body of any size is
read chunk by chunk, and the convenience that covers the common case (`body.text()`, `body.json<User>()`) sits on top of
it with a limit. It needs the network capability inside a sandboxed script, and is not in the prelude. `https` waits for
TLS (docs/design/NETWORK.md section 5).

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

fn fetchUser(id: Int): Task<Result<User, HttpError>> {
  var response = http.get("http://example.test/users/{id}").await()?
  response.json<User>().await()
}

fn hello(request: Request): Task<Result<Response, HttpError>> {
  if request.path() == "/hello" {
    return Ok Response.text("hello")
  }
  Ok Response.of(Status.notFound)
}

fn serveForever(): Task<Result<Void, HttpError>> {
  const server = Server.listen(SocketAddress(IpAddress.loopback, 8080), hello)?
  server.serve().await()
}
```

## Declarations

### `get`, `post`, `send`

```trb fragment
public fn get(url: String, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn post(url: String, body: Body, headers: Headers = Headers()): Task<Result<Response, HttpError>>
public fn send(method: Method, url: String, headers: Headers = Headers(), body: Body = Body.empty()): Task<Result<Response, HttpError>>
```

The client. A request opens a connection to the host of the URL, writes the request - with `Content-Length` where the
body's length is known, in chunks where it is not - and answers once the head of the response arrived; its body is read
from the connection as the program pulls it, and the connection closes with the response. A status that is not a
success is a `Response` too, and no redirect is followed. A timeout is `within`: `http.get(url).within(10.seconds())`.

### Server

```trb fragment
public type Handler = (request: Request) => Task<Result<Response, HttpError>>

public shared type Server with Close {
  static fn listen(address: SocketAddress, handler: Handler, limits: ServerLimits = ServerLimits()): Result<Server, HttpError>
  fn localAddress(): SocketAddress
  fn serve(): Task<Result<Void, HttpError>>
  fn serveOne(): Task<Result<Void, HttpError>>
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
of routes. `serve()` accepts until its task is cancelled, and each connection is a task of its own that serves its
requests one after another (a persistent connection, and pipelined requests in order). A handler that fails is answered
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
  headers: Headers = Headers()
  var body: Body
  remote: SocketAddress? = None
  version: String = "HTTP/1.1"
  fn path(): String
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
(`/users/7?details=true`); `path()` and `query()` take it apart.

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
- [std/stream](stream.md) - `Source`, which `Body` is, and `Sink`.
- [std/json](json.md) - `Json().decode`, which `Response.json` and `Body.json` call.
- [The standard library](index.md) - the other packages.

