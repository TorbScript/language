---
title: std/http
summary: A minimal HTTP client - get, post and request answer a Task, and a response body is a stream of any size.
kind: package
status: stable
order: 180
keywords:
  - std/http
  - HTTP
  - Request
  - Response
  - Body
source:
  - std/http/src/lib.trb
---

> **Not built natively yet.** A function whose body answers a `Task` is not built by the native back end yet, so `torb
> run` refuses the examples here that use it. `torb check` accepts them, and the rules are the language's.

`std/http` is a minimal HTTP client. A body is a stream (see [std/stream](stream.md)): `Body` is a
`Source<Bytes, HttpError>`, so a response of any size can be piped into a file or read item by item, and the
convenience that covers the common case (`body.text()`, `body.json<User>()`) sits on top of it. It needs the network
capability inside a sandboxed script, and is not in the prelude.

Every request answers a `Task`, so a program that calls `get`, `post` or `request` needs `.await()` on the result -
and [std/task](task.md) is `status: planned`, because no back end gives a `Task` a value yet. The types and rules below
are the settled design and type check today; running a request end to end waits on the same milestone as `spawn`.

## Import

```trb fragment
use * as http from "std/http"
use HttpError, Request, Response, Body from "std/http"
```

```trb check
use * as http from "std/http"
use HttpError from "std/http"

type User {
  id: Int
  name: String
}

fn fetchUser(id: Int): Task<Result<User, HttpError>> {
  var response = http.get("https://example.test/users/{id}").await()?
  response.json<User>().await()
}
```

## Declarations

### `get`, `post`, `request`

```trb fragment
public native fn get(url: String, headers: Map<String, String> = [:]): Task<Result<Response, HttpError>>
public native fn post(url: String, body: Body, headers: Map<String, String> = [:]): Task<Result<Response, HttpError>>
public native fn request(request: Request): Task<Result<Response, HttpError>>
```

`get` and `post` build a `Request` for the common cases; `request` is for methods and headers they do not cover.
`post`'s body may be a stream, so an upload is not held in memory.

### Request

```trb fragment
public type Request {
  method: String
  url: String
  headers: Map<String, String> = [:]
  var body: Body = Body.empty()
}
```

One HTTP request, for `request()`. `body` is `var` because reading a body changes it: whoever reads one needs a `var`
path to it (see [Shared types](../language/types/shared-types.md)).

### Response

```trb fragment
public shared type Response {
  status: Int
  headers: Map<String, String> = [:]
  var body: Body

  var fn json<Value: Decode>(limit: Int = Body.defaultLimit): Task<Result<Value, HttpError>>
}
```

What came back. A `shared type` and not a value, because it owns one end of a stream that is read once - a copy would
promise a second read of a body that is already gone, so a `Response` that is read from sits in a `var` binding, the
same way a `File` does.

### Body

```trb fragment
public shared type Body with Source<Bytes, HttpError> {
  static defaultLimit: Int = 16777216

  var fn bytes(limit: Int = Body.defaultLimit): Task<Result<Bytes, HttpError>>
  var fn text(limit: Int = Body.defaultLimit): Task<Result<String, HttpError>>
  var fn json<Value: Decode>(limit: Int = Body.defaultLimit): Task<Result<Value, HttpError>>
  var fn lines(): Source<String, HttpError>
  static fn of(var source: Source<Bytes, HttpError>): Body
  static fn empty(): Body
  static fn jsonOf(value: Encode): Body
}
```

The bytes of a request or a response, as a stream read once. `defaultLimit` is 16 MiB - large enough for any document a
program means to hold in one piece, small enough that a response nobody expected is refused instead of filling the
machine; streaming with `of` past a `Source` is the way past it, not a bigger number. `Body.from("text")` and
`Body.from(bytes)` come from `extend Body with From<String>` and `extend Body with From<Bytes>`.

### HttpError

```trb fragment
public type HttpError with Show, Error {
  static fn timeout(): HttpError
  static fn connectionFailed(message: String): HttpError
  static fn status(code: Int, message: String): HttpError
  static fn invalidUrl(message: String): HttpError
  static fn decodeFailed(cause: JsonError): HttpError
  static fn tooLarge(limit: Int): HttpError
  static fn invalidText(cause: Utf8Error): HttpError
  fn isTimeout(): Bool
  fn isRetryable(): Bool
  fn statusCode(): Int?
  fn cause(): Error?
}
```

What went wrong making a request, as a single wrapper type - a new kind of failure is never a breaking change (see
[Cases and match](../language/pattern-matching/cases-and-match.md)). `isRetryable()` is `true` for a timeout, a
connection failure, or a `5xx` response; `statusCode()` answers the status only if the request reached the server at
all.

## Related

- [std/stream](stream.md) - `Source`, which `Body` is, and `Sink`.
- [std/json](json.md) - `Json.decode`, which `Response.json` and `Body.json` call.
- [The standard library](index.md) - the other packages.
