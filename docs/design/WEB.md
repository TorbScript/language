# Web: Handlers, HTML and a Live UI

**Status: decided (2026-09-24), not implemented; section 13 lists the decisions, which override the recommendations where they differ.** No `std/web`, `std/html` or `std/live` exists. The probes of
section 1 were checked, put through `canon --check` and, where they could be, built and run natively with the
`build/release/torb.exe` this worktree bootstrapped from `653af8bb`. The transport - sockets, TLS, the HTTP/1.1 server,
the IO poller - is `docs/design/NETWORK.md`, written in parallel; this record assumes only that **a handler is a
function from a `Request` to a `Task<Response>`**, and says in section 9 what it needs from that record beyond it.

**A web program is three layers of ordinary TorbScript on one function type.** Markup is written with a builder on
receiver closures, so escaping is a property of the types and not of the author's care. A handler is a function, a
route is a type with cases and dispatching is a `match`, so a new route is a list of compile errors. And an
interactive page is a value, a verb that changes it and a view that shows it - the Elm architecture - which value
semantics make cheap to snapshot, to compare and to run wherever the state is best kept.

```text
  std/live    a page is a value: `var fn update(message)`, `static fn view(model)`, parts, the wire, the client script
     │
  std/html    Html<Message>, Markup<Message>: elements as members, escaping by construction, components as `extend`
     │
  std/web     Handler, Route matching, Middleware and Next, forms, sessions, assets, WebError, WebTest
     │
  NETWORK.md  (request: Request) => Task<Response>, sockets, TLS, HTTP/1.1, the poller     ← not this record
```

- **[1. What the probes proved](#1-what-the-probes-proved)** — eight probes, and five problems of the toolchain they found
- **[2. What other systems do](#2-what-other-systems-do)** — the template side: Scala, Twirl, ScalaTags, kotlinx.html, JSX, Go, Rust, Elm, Swift
- **[3. Requests and responses](#3-requests-and-responses)** — handlers, routing, middleware, forms, sessions, assets, errors, tests
- **[4. HTML](#4-html)** — the builder, three alternatives on one page, escaping, untrusted templates
- **[5. Server-driven UI: what exists](#5-server-driven-ui-what-exists)** — ten systems in one table
- **[6. The live model](#6-the-live-model)** — state, events, parts, placements, the wire, reconnection, cost, v1
- **[7. What the language gives](#7-what-the-language-gives)**
- **[8. What the language and the toolchain lack](#8-what-the-language-and-the-toolchain-lack)** — thirteen items, each its own decision
- **[9. What this record needs from NETWORK.md](#9-what-this-record-needs-from-networkmd)**
- **[10. Slices](#10-slices)**
- **[11. What this is not](#11-what-this-is-not)**
- **[12. Owner decisions](#12-owner-decisions)** — fourteen, each with options and a recommendation

The code blocks below are in the formatter canon and parse. The ones that name `std/web`, `std/html` or `std/live`
were checked against stand-ins for those packages declared in the probe files; nothing else of them is invented.

---

## 1. What the probes proved

Eight scratch files, each checked with `torb check`, each in the canon after `torb canon`, and run with `torb run`
where the back end could build it.

| # | Probe | Result |
|---|-------|--------|
| 1 | An HTML builder over a `Markup` receiver: `html { ... }` around a heading, a `div class: "card" { ... }` with a list inside, a `for` over orders, a component as a `var fn` in `extend Markup`, text escaped by `text` | **Builds and runs natively.** Nested receiver closures, a labelled first argument in command form with a trailing closure (`a href: "/x" { ... }`), a `for` inside a block and a component added by `extend` all work today. `Ada <admin>` comes out as `Ada &lt;admin&gt;` |
| 2 | A live component: typed messages, a handler table of `Send(message)` and `WithText(transform)`, a case constructor passed as `(text: String) => Message` (`onInput: CounterMessage.Rename`), a part keyed by an input's `hash()`, `match (method, segments)` with list patterns for routing | **Builds and runs natively** once `render<Counter, CounterMessage>(counter)` is written out (problem 4 below). `["orders", id]` matches and binds, and `Int.tryFrom(id).ok().map(Page.Order)` builds the case |
| 3 | `type Markup<Message = Never>`, a static page as `Markup` with no argument | **Checks.** The default of a type parameter works where a type is written. **Does not build**: `List<Never>` instantiates `ArrayList.append__Never` and the back end refuses every slot of type `Never` (problem 2) |
| 4 | The routing, handler and form examples of section 3 against stand-in `Request`, `Response` and `WebError` | **Checks and is in the canon.** `x ?? return Fail(...)` does not parse - `return` is not an expression - so an absent row is a `match` with a `None` arm |
| 5 | The example page of section 4.2 with a stand-in `std/html`: `document title: "Orders" { ... }`, elements with a content string, a class and a block, `extend<Message> Markup<Message>` for a component | **Builds and runs natively**, output escaped |
| 6 | The live search of section 6.1 with stand-ins for `Live`, `Effect` and the builder | **Builds and runs natively**, including `found(query)` as a `Task` awaited at the top level |
| 7 | An application DSL: a `web { ... }` block with `wrap`, `assets` and `handle` | **Three findings.** A middleware as a function over a function is refused (section 3.3); inside the block a member of the receiver shadows a module function of the same name, so the closure captured `self` (section 3.1); the trait-based middleware that replaces it **checks**, and the C it generates **does not compile** (problem 3) |
| 8 | The same page as a plain interpolated `"""` string | **Runs, and prints `<td>Ada <admin></td>`.** This is today's only alternative to a builder, and it is the injection every framework of section 2 exists to prevent |

**Five problems of the toolchain the probes ran into.** Each is reported here and is not a decision.

1. **A checker panic.** Probe 2 without the explicit type arguments: after `Cannot infer Message of render`, a second
   `counter.update(renamed)` whose argument's type was never inferred panics the checker with `Key does not exist` at
   `std/core/src/option.trb:172`. The first such call does not. A diagnostic that follows another must not crash.
2. **`List<Never>` does not build.** Twenty-two internal errors of the form `slot %1 has the type Never, which has no
   values` for `ArrayList.append__Never`, `Iterate.forEach__List_Never_Never` and their neighbours. A collection of an
   uninhabited type is empty forever and every function over one is dead code; the back end has to accept it rather
   than refuse its slots (section 8, F2).
3. **Generated C that does not compile.** Probe 7: a trait method `fn around(request: Request, next: Next):
   Task<Result<Response, WebError>>` implemented by a value type, called through the trait. The witness thunk
   `W_app_RequestLog_around` calls `R_T_app_Next`, a retain function that was never emitted (`D_T_app_Next` exists).
   `torb check` says no problems.
4. **A type parameter reachable only through a bound is not inferred.** `fn render<Model: Live<Message>, Message>(model:
   Model)` called with a `Counter` that is `Live<CounterMessage>` asks for `render<Counter, CounterMessage>(...)`.
   Every generic function of `std/live` has this shape (section 8, F3).
5. **Quoted expressions do not build natively.** `input bind: model.name` with `bind: Expression<String>` checks and
   answers `nameOf(bind)`, and `torb run` refuses it with `a quoted expression is not supported by the native back end
   yet`. That keeps typed form bindings out of v1 (section 3.5).

---

## 2. What other systems do

The owner's reference is Scala, so Scala first: it tried all three shapes this record weighs, in that order.

| System | Shape | Typed | Escaping | What is worth taking |
|--------|-------|-------|----------|----------------------|
| **Scala 2 XML literals** | `<p>{name}</p>` is an expression of type `scala.xml.Elem`; the scanner has an XML mode | the tree is untyped (`NodeSeq`); names and attributes are strings | an embedded `String` becomes a text node and is escaped on output | that escaping by construction is possible even with literal syntax |
| **Scala 3** | XML literals "are still supported, but will be dropped in the near future, to be replaced with XML string interpolation" (the Scala 3 reference, *Dropped Features*); the replacement is the `xml"""..."""` interpolator of `lampepfl/xml-interpolator` | as Scala 2 | as Scala 2 | **the direction**: the language gives up special syntax for one format and keeps a general interpolation hook a library fills. The reference page gives no rationale; the reasons usually cited are that the literals tie the language to one library (`scala-xml`, moved out of the standard library in 2.11), that the scanner carries a second lexical mode for them, and that string interpolation already covers the need |
| **Twirl** (Play) | `.scala.html` files with a parameter line (`@(title: String, orders: Seq[Order])`), `@if`, `@for`, compiled by the build into Scala functions (`views.html.orders(title, orders)`) | parameters and holes are type checked | escapes by default; `@Html(raw)` bypasses | a template is a function with typed parameters; content types (`Html`, `Txt`, `Xml`) |
| **ScalaTags** | `div(cls := "card", h1("Orders"), p(name))` - plain function calls | elements and attributes are values; attribute values are checked through implicits | by construction; `raw(...)` bypasses | the builder needs no syntax, and its `Text` back end writes straight into a string builder, which is why it is fast |
| **kotlinx.html** | `html { body { div(classes = "card") { +"text" } } }` on receiver lambdas, `@DslMarker` against scope leaks | elements are functions of the receiver, attributes typed per element | by construction | **this is TorbScript's shape exactly**, and TorbScript has no scope leak to guard against: one receiver is implicit (CONCEPT, "Configuration DSL") |
| **JSX / TSX** | `<p>{name}</p>` compiled to `jsx("p", props)` calls | props typed per component through TypeScript | React escapes strings; `dangerouslySetInnerHTML` bypasses | a literal can be pure sugar over function calls - but it needs a compiler plugin, an editor mode and a formatter of its own |
| **Go `html/template`, templ** | template text parsed at run time (`html/template`) or `.templ` files compiled to Go (templ) | templ: component parameters typed | **context-aware**: the parser knows whether a hole is in text, an attribute, a URL, a script or a style, and escapes for that context; `template.HTML` is the typed trust | the contexts, and a typed marker for trusted markup |
| **Rust: maud, askama** | maud: `html! { p { (name) } }`, a procedural macro; askama: Jinja-like files compiled at build time through a derive | both checked at compile time | by construction | both need what TorbScript refuses: macros and build-time code generation |
| **Elm** | `div [ class "card" ] [ text name ]`, `onClick Increment` | `Html msg`: the type of the messages a view can send is part of the view's type | by construction (virtual DOM) | **`Html<Message>`**: events are typed values, and a view that sends no messages is `Html Never` |
| **Swift `ExpressibleByStringInterpolation`**, C# interpolated string handlers | a string literal whose expected type is `T` is handed to `T` piece by piece: `appendLiteral` for the text, `appendInterpolation` for each value | per hole | the type decides | the mechanism for section 4.3: the literal parts and the holes arrive separately, so escaping needs no parser |

**What this record takes.** The builder of kotlinx.html and ScalaTags, which TorbScript can already write (probe 1);
Elm's `Html<Message>`; Go's contexts for escaping; Twirl's "a template is a typed function"; and Swift's split of an
interpolated literal into parts as the optional second form (section 4.3). **What it does not take:** literal syntax
for one format (Scala's own verdict), template files with a grammar of their own, and macros.

---

## 3. Requests and responses

### 3.1 The handler and the application

**A handler is a function, and the framework adds a failure to it.** NETWORK.md's handler answers a `Task<Response>`.
Inside an application almost every step can fail - a form that does not decode, a row that is not there, a database
that is down - and `?` is how this language hands a failure on, so the framework's handler answers a `Result`:

```trb
type Handler = (request: Request) => Task<Result<Response, WebError>>
```

**The application is a value built by a block**, in the vocabulary `project.trb` and `examples/config-dsl` already
use, and it adapts itself to NETWORK.md's function type at the edge, where a `Fail` becomes the error page of its
status (section 3.8):

```trb
const app = web {
  wrap RequestLog()
  wrap Sessions(key: sessionKey)
  assets "/assets", files: ["./public/site.css", "./public/logo.svg"]
  handle { request => respond(request, shop).outcome() }
  errors renderError
}
```

- **`web`** is a builder of three lines (`docs/language/configuration/builders.md`), its receiver `WebApp` has the
  vocabulary `wrap`, `assets`, `handle` and `errors`, and nothing in it is registered by scanning or annotation.
- **The dependencies are captured values.** `shop` is a `const` of the program - a connection pool, a repository -
  and the closure takes it along. That is constructor wiring, which is what `docs/design/FRAMEWORK.md` decides for
  dependency injection: Spring's layers and names, wired by the compiler instead of by reflection.
- **The vocabulary of the receiver is kept small on purpose.** Probe 7: inside the block a module function `respond`
  lost against the receiver's member of the same name, and the closure captured `self` and was refused. That is
  CONCEPT's resolution order working as designed (the receiver before the module), so the receiver of `web` has four
  members and none of them is a word a program would use for its own handler.

### 3.2 Routing is a `match`

**A route is a type with cases, parsing a request is a `static fn` with a `match`, and a link is a member.** No
string pattern, no registry and no reflection:

```trb
type Route {
  case Home
  case Orders
  case Order(id: Int)
  case CreateOrder

  static fn of(request: Request): Route? {
    match (request.method, request.segments()) {
      (.Get, []) => Some Route.Home
      (.Get, ["orders"]) => Some Route.Orders
      (.Get, ["orders", id]) => Int.tryFrom(id).ok().map(Route.Order)
      (.Post, ["orders"]) => Some Route.CreateOrder
      _ => None
    }
  }

  fn path(): String {
    match self {
      .Home => "/"
      .Orders => "/orders"
      .Order(id) => "/orders/{id}"
      .CreateOrder => "/orders"
    }
  }
}

fn respond(request: Request, shop: Shop): Task<Result<Response, WebError>> {
  match Route.of(request) {
    Some(.Home) => Ok Response.redirect(Route.Orders)
    Some(.Orders) => {
      const orders = shop.orders().outcome()?
      Ok Response.html(ordersPage(orders))
    }
    Some(.Order(id)) => {
      match shop.order(id).outcome()? {
        Some(order) => Ok Response.html(orderPage(order))
        None => Fail WebError.notFound()
      }
    }
    Some(.CreateOrder) => createOrder(request, shop).outcome()
    None => Fail WebError.notFound()
  }
}
```

- **A new route is a list of compile errors.** Adding `case Invoice(id: Int)` makes `path()` and `respond`
  non-exhaustive, which is the argument CONCEPT makes for `match` everywhere, applied to URLs. Play needs a compiled `routes` file and
  a generated reverse router for the same guarantee; here it is two `match`es.
- **A link is typed.** `Route.Order(order.id).path()` cannot name a route that does not exist, and the builder of
  section 4 takes a `Link` - a trait with `fn path(): String` that `Route` implements - wherever HTML takes a URL.
- **The two directions are written twice, and a test holds them together.** `of` and `path` could disagree about
  `/orders/{id}`. `WebTest.assertRoutes(Route.Home, Route.Order(7))` asks, for each sample, that `of` of a request to
  `path()` answers the sample back. A pattern language that derives both would remove the duplication and bring back
  the string, a second grammar and a check at startup; section 12, decision D5, has both.
- **This block checks as it stands** against stand-ins for `Request`, `Response` and `WebError` (probe 4), including the
  list patterns with string literals and the case constructor `Route.Order` passed to `map`.

### 3.3 Middleware is a trait, and `Next` is a value

The obvious shape - a middleware is a function that takes the next handler as a function - is refused, and the reason
is one of the language's own rules:

```text
error: `layer` runs this function value as a task, and what it captured is not part of its type
  = A task only takes what it can see is a value. Hand in a closure written right there, whose captures are checked,
    a declared function, or a function-typed parameter
```

A composed chain is exactly a function value whose captures are not visible (CONCURRENCY.md section 12, probe 3's
rule), and every middleware answers a `Task`. So **the chain is data**: a middleware is a trait with one method, named
after it, and the rest of the chain is a value the middleware hands the request on to - Spring's `Filter` and
`FilterChain`, and Symfony's kernel events, under one word each:

```trb
trait Middleware {
  fn around(request: Request, next: Next): Task<Result<Response, WebError>>
}

type Next {
  layers: List<Middleware>
  endpoint: Handler

  fn run(request: Request): Task<Result<Response, WebError>> {
    match layers {
      [] => endpoint(request).outcome()
      [first, ...rest] => first.around(request, Next(rest, endpoint)).outcome()
    }
  }
}

type RequestLog with Middleware {
  prefix: String = "request"

  fn around(request: Request, next: Next): Task<Result<Response, WebError>> {
    const answer = next.run(request).outcome()
    print "{prefix} {request.path}"
    answer
  }
}
```

**This checks, and it is what probe 7 could not build** (problem 3 of section 1). A middleware that answers without
calling `next` is how authentication refuses a request; one that changes the request is `next.run(request.copy(...))`,
and the value semantics say that the change is visible below it and nowhere else.

### 3.4 Request and response helpers

What `std/web` adds to NETWORK.md's `Request` and `Response`, as members an `extend` puts on them:

| Member | Answers | Built on |
|--------|---------|----------|
| `request.segments()` | `List<String>`, percent-decoded, empty segments dropped | `std/uri` (URI.md) |
| `request.query<Value: Decode>()` | `Result<Value, WebError>`; a missing field takes the field's default, a malformed one is a 400 | `std/encoding` with a query-string `Decoder` |
| `request.form<Value: Decode>()` | `Task<Result<Submitted<Value>, WebError>>`, section 3.5 | the body read with a limit, then the same `Decoder` |
| `request.json<Value: Decode>()` | as `Body.json` of `std/http` today | `std/json` |
| `request.cookie(name)` | `String?` | |
| `request.session<Data>()` | section 3.6 | |
| `Response.html(page: Html<Never>)` | 200, `text/html; charset=utf-8` | `std/html` |
| `Response.json(value: Encode)` | 200, `application/json` | `std/json` |
| `Response.redirect(to: Link)` | 303 | |
| `response.withStatus(code)`, `withHeader(name, value)`, `withCookie(cookie)` | participles: each answers a changed copy | the verb/participle rule of the idiomatic guide |

**A limit on every body read**, as `std/http`'s `Body` already has one (`Body.defaultLimit`, 16 MiB): the server of a
form must not let a client decide how much memory it allocates. `request.form` takes a `limit` whose default is 1 MiB.

### 3.5 Forms and validation

**A form is a type, decoding it is validating it, and the rules live in the types of its fields.** That is
ENCODING.md applied to one more format: a record with public fields decodes field by field, and a capsule field
(`Email`) decodes through its `TryFrom`, so the check its factory exists to force runs for a form too (ENCODING.md
section 3a):

```trb
type NewOrder {
  customer: String
  email: Email
  quantity: Int = 1
}

fn createOrder(request: Request, shop: Shop): Task<Result<Response, WebError>> {
  match request.form<NewOrder>().outcome()? {
    .Valid(order) => {
      const created = shop.create(order).outcome()?
      Ok Response.redirect(Route.Order(created.id))
    }
    .Invalid(form) => Ok Response.html(newOrderPage(form)).withStatus(422)
  }
}
```

- **`Submitted<Value>` has two cases**: `Valid(value: Value)` and `Invalid(form: Form<Value>)`. A malformed body, a body
  over the limit and a missing CSRF token are a `WebError` (400, 413, 403); a form a person filled in wrongly is not an
  error of the request, it is a page to show again, so it is a case and not a `Fail`.
- **`Form<Value>` keeps what was typed**: the submitted text of every field and a problem per field, so the page can
  fill its inputs back in and say what is wrong next to each. `DecodeError.path` (ENCODING.md section 2) already names
  the field.
- **All problems at once needs a change in ENCODING.** A derived `decode` stops at its first `?`, so today the form
  would report the first bad field only. A person expects every field marked at once; that is section 8, F6.
- **Field names in the page are strings in v1.** `textField form, "email"` is checked when the page renders against
  `Describe` of `NewOrder` (a field that does not exist is a panic in the page's test, not a silent empty input). The
  typed form, `input bind: form.email` with `bind: Expression<String>` and `nameOf`, checks today and does not build
  (problem 5); it comes with F4.

### 3.6 Sessions, cookies and CSRF

- **A session is a value in a signed cookie by default**: `request.session<Cart>()` decodes it,
  `response.withSession(cart)` encodes, signs and sets it. Any `Encode & Decode` type is a session without a line of
  mapping. A value too large for a cookie goes into a `SessionStore` - a trait with `load` and `save`, an in-memory one
  for tests and one per database - and the cookie then carries only its id.
- **Signing needs HMAC-SHA256 and a random source,** which `std` does not have (section 8, F7). Encryption is not
  proposed for v1: the rule is that a secret does not go into a session value, and signing is what makes the cookie
  trustworthy.
- **CSRF is handled by construction, like escaping.** The builder's `form` element writes the token of the request it
  renders for as a hidden input, and the `Sessions` middleware refuses a state-changing request without the matching
  token (403). A form written with the builder cannot forget it, and one written by hand is a form the author took
  responsibility for.
- **Cookies are `Secure`, `HttpOnly` and `SameSite=Lax` unless a setting says otherwise.** A default that has to be
  switched on is a default most programs will not have.

### 3.7 Static assets

**Assets are resources, and RESOURCES.md already decides what a resource is.** `assets "/assets", files: [...]` takes a
`List<Resource>`, whose elements are literals the compiler resolves (RESOURCES.md section 3): a missing file is a build
error at the line that names it, the build ships every file beside the program, and nothing is served that the author
did not name.

- **Every asset gets a fingerprinted URL** - `/assets/site.3f2a9c10.css`, the hash of its bytes, computed at startup -
  and is served with `Cache-Control: immutable`. The builder's `stylesheet` and `script` elements take the `Resource`
  and write that URL, so a page never links a stale file after a deploy.
- **A directory is not a resource yet.** RESOURCES.md section 6 leaves `ResourceDirectory` open, so a site with two
  hundred images lists two hundred literals or serves a `Path` from the disk at run time (`std/fs`, the containment
  check of PATH.md section 7). Section 8, F8 asks for the directory; decision D11 is whether v1 waits for it.
- **Small assets can be embedded**: `EmbeddedBytes` for the favicon and the client script of section 6.8, so a
  single binary serves a working page.

### 3.8 Errors, and what a panic does

`WebError` is a capsule with a status, a public message and a private cause (`fn cause(): Error?`, as `HttpError` has).
It converts from the failures a handler meets, so `?` works without ceremony: `From<DecodeError>` is a 400,
`From<Cancelled>` is the client that went away (logged, never rendered), and a program adds `From<ShopError>` for its
own. The `errors` member of the application renders a page per status; the cause is logged and never shown.

**A panic in a handler ends the process.** That is CONCURRENCY.md section 8, "Panics are unchanged", and it is the
largest difference to every server framework of section 2, all of which turn an exception in a handler into a 500 and
serve the next request. The record does not change the rule; it lists the two ways forward as decision D10 and the
language change as F9.

### 3.9 Testing a handler without a socket

**A handler is a function and a request is a value, so a test calls it.**

```trb
fn checkUnknownOrder(): Task<Result<Void, WebError>> {
  const answer = app.answer(Request.get("/orders/99")).outcome()
  match answer {
    Ok(response) => assert(response.status == 404)
    Fail(problem) => assert(problem.status() == 404)
  }
  Ok void
}
```

- **`app.answer(request)`** runs the whole chain - middleware, handler, the error pages - exactly as NETWORK.md's server
  would, without a socket, a port or a thread of its own. `WebTest` adds `Request.get`, `Request.post(form:)`, a cookie
  jar across calls, `assertRoutes` of section 3.2, and a reader for the returned page (`page.text("h1")`).
- **A view is a function of a value**, so most of a page's tests need no request at all: `ordersPage([])` is an `Html`,
  and `html.text()` is the markup.
- **A `test` body cannot wait today.** `std/test` declares `test(name: String, body: () => Void)`, and `await()` is only
  allowed in a closure that is a task, so the check above is a `fn` answering a `Task` that the test file calls at its
  top level. A `test` whose body is a task is section 8, F13.

---

## 4. HTML

### 4.1 What the markup has to be

1. **Correct by construction.** No path from a `String` to markup that does not escape it. Not "escaped by default":
   there is no switch.
2. **Typed.** Attributes of an element are its parameters, with types (`href: Link`, `kind: InputKind`); an element
   that takes no children takes no block; an event is a `Message`, not a string of JavaScript.
3. **Components are ordinary declarations**, found by the checker, the LSP and `torb doc` like any function.
4. **In the canon**, formatted by `torb canon` and checked by the gates that exist.
5. **Fast enough not to matter**: rendering is appending pieces to a list and joining it once.

### 4.2 The recommendation: a builder on receiver closures

The example page - a title, a heading, a message when there is nothing, a table with one component per row, a link -
written with `std/html`. **Probe 5 builds and runs this against a stand-in of `std/html`**, with `href` a `String`
there because the stand-in has no `Link`:

```trb
use Html, Markup, document from "std/html"

fn ordersPage(orders: List<Order>): Html<Never> {
  document title: "Orders" {
    h1 "Orders"
    if orders.isEmpty() {
      p "No orders yet."
    }
    table class: "orders" {
      for order in orders {
        orderRow order
      }
    }
    a href: Route.CreateOrder, class: "button" {
      text "New order"
    }
  }
}

extend<Message> Markup<Message> {
  /** A component is a member of the builder, so it reads like an element where it is used. */
  var fn orderRow(order: Order) {
    tr {
      td "{order.number}"
      td order.customer
      td class: "amount" {
        text "{order.total} EUR"
      }
    }
  }
}
```

- **An element is a `var fn` of `Markup<Message>`** with a content string, the global attributes as labelled
  parameters with defaults, its own attributes typed, and a trailing block `(var self: Markup<Message>) => Void = {}`
  for children. `td "{order.number}"`, `td class: "amount" { ... }` and `tr { ... }` are the three shapes one
  declaration gives.
- **The element table is generated, not written by hand.** About 110 elements and their attributes come from the WHATWG
  tables; a TorbScript program in `tools/` writes `std/html/src/elements.trb`, which is checked in, the way
  `torb natives --header` writes the thunk table. Nothing about it is a build step.
- **Two attribute names are reserved words.** `type` and `for` cannot be parameter names (lexical structure, rule 6),
  so `input kind: .Email` writes `type="email"` and `label target: "email"` writes `for="email"`. `class` is not
  reserved and keeps its name.
- **Rare attributes** go through `attributes: ["itemprop": "name"]`: the names are checked against the HTML name
  grammar and the values escaped, so the escape hatch cannot inject either.
- **A component is `extend<Message> Markup<Message>`** when it works for any page, or `extend Markup<CartMessage>` when
  it sends that page's messages. It is found by the checker like every member, shows in completion after the elements,
  and cannot be misspelt.
- **Data comes from parameters and locals, never from an outer receiver.** Only the innermost receiver is implicit, so
  inside `tr { ... }` a member of the page's model is not in scope (probe 2 found this when the view was a method of
  the model: `count` is `Cannot find count here` inside the block). That is why a view is `static fn view(model:
  Self)` in section 6 and not a method: the model is a parameter, and a parameter is visible everywhere below it.
- **Rendering is one list of pieces joined once.** No tree is built for a page that is only sent; section 6 builds none
  either (section 6.3).

### 4.3 Alternative B: an interpolated literal that is `Html`

**A language rule, the one of URI.md section 9 with one more member on its list**: *an interpolated string literal
whose expected type is `Html` is built from its parts* - the literal text is markup, each hole is a value that is
escaped for the context the literal puts it in. It is Swift's `ExpressibleByStringInterpolation`, restricted to one
standard-library type, and it is exactly Scala 3's replacement for its XML literals:

```trb
fn orderRow(order: Order): Html {
  """
    <tr>
      <td>{order.number}</td>
      <td>{order.customer}</td>
      <td class="amount">{order.total} EUR</td>
    </tr>
    """
}

fn ordersPage(orders: List<Order>): Html {
  const empty: Html = if orders.isEmpty() { "<p>No orders yet.</p>" } else { Html.empty }
  """
    <!doctype html>
    <html>
      <head><title>Orders</title></head>
      <body>
        <h1>Orders</h1>
        {empty}
        <table class="orders">{orders.map(orderRow)}</table>
        <a href="{Route.CreateOrder}" class="button">New order</a>
      </body>
    </html>
    """
}
```

This parses today and means something else: probe 8 runs it as a `String` and prints `<td>Ada <admin></td>`. Under the
rule it would be:

- **Lowered, not parsed at run time.** The checker splits the literal at its holes, which it does already for every
  interpolation, and the lowering emits the static parts as static data and each hole as a call of `Render.render` of
  the hole's value: a `String` escapes, an `Html` is inserted, an `Iterate<Html>` is joined, `Option<Html>` is nothing
  or its value.
- **Checked where it is written.** The checker tokenizes the static parts with `std/html`'s tokenizer - one more
  package the compiler imports, and so one more package in the fixpoint, the cost URI.md section 9 states for `Uri` -
  and knows for each hole whether it is text, an attribute value, a URL attribute (`href`, `src`, `action`: the hole
  must be a `Link`), or inside `<script>`, `<style>` or an `on...` attribute (refused, with a note naming the builder).
  An unclosed element or an attribute without quotes around a hole is a build error at the literal.
- **The limits that come with interpolation.** A hole fits on one line (string interpolation, rule 3), so a loop is a
  call - `{orders.map(orderRow)}` - which is the style section 4.2 wants anyway. A literal `{` in markup is `\{`, which
  makes inline CSS and JavaScript painful, and both belong in resource files.
- **It is the natural form for large static prose** - a legal page, an email, a page a designer handed over - and the
  wrong one for anything interactive: an event in a literal would be `onclick="{Message.Increment}"`, a hole whose
  type depends on the attribute's name, which is the checker learning HTML one attribute at a time.

### 4.4 Alternative C: native markup literals

What JSX and Scala 2 do, as TorbScript would have to spell it. **Hypothetical syntax; it does not parse:**

```text
fn ordersPage(orders: List<Order>): Html<Never> {
  <html>
    <body>
      <h1>Orders</h1>
      {if orders.isEmpty() { <p>No orders yet.</p> } else { Html.empty }}
      <table class="orders">{orders.map(orderRow)}</table>
      <a href={Route.CreateOrder} class="button">New order</a>
    </body>
  </html>
}
```

The best version of this is JSX's: every element desugars to a call of the builder's member of that name, attributes
to labels, children to the block - so it would be exactly as typed as section 4.2, and the whole question is whether
the syntax is worth what it costs. **It is not**, for six reasons:

1. **It breaks design principle 4.** Inside an element, whitespace between tags is content; JSX has a page of rules for
   which of it survives. CONCEPT's principle is that whitespace never changes the meaning of a token sequence.
2. **`<` becomes ambiguous where the grammar has no room left.** `a < b` is a comparison and `json<User>()` a type
   argument; in command position `print <p>hi</p>` and `print < p` differ only by what follows. The command-call rule
   would gain `<` in its list of first characters that make a call parenthesized, and every such rule is a line every
   reader has to know.
3. **`{` stops being a closure.** Inside markup `{...}` is a hole; everywhere else `{` in expression position is always
   a closure (CONCEPT, "Lambdas and Closures"). The one rule the language never breaks would get its first exception.
4. **A second lexical mode**, which is what Scala 3 is removing: the lexer, the canon, `torb canon`'s layout of markup,
   the LSP's completion inside tags, the docs checker and every tool that reads `.trb` learn it, and a syntax change is
   two commits and a seed refresh (CLAUDE.md, "Seed and breaking changes").
5. **The language would know one format.** `docs/ROADMAP.md` plans `html`, `xml`, `yaml`, `toml` as packages of `std`; syntax for
   one of them makes it special in a way no other format can follow, which is Scala's `scala-xml` lesson.
6. **It buys almost nothing over the builder.** Probe 5's page is shorter as a builder than as markup, because the
   builder has no closing tags; what the literal buys is pasting HTML from elsewhere, and section 4.3 buys that too, at
   a fraction of the cost and without a second grammar.

### 4.5 Alternatives D and E: template files, and quoted expressions

**D, a template file compiled by `torb`** (Twirl, templ, askama): `orders.html.trb` with a parameter line and
`@for`/`@if`. RESOURCES.md section 7 gives it a home - a resource checked at build time against a type - but it is a
second grammar with a second parser, a second canon and a second LSP mode, for the same result as section 4.3, whose
literal can sit in a `.trb` file of its own. **Not proposed.**

**E, quoted expressions** (`std/expression`). A parameter `markup: Expression<String>` receives the tree of an
interpolated string, whose `Interpolation` node lists the literal parts and the holes separately - escaping without a
new rule, in principle. **It does not work**, for a reason the design of quotations states: the tree cannot be
executed, only the whole value can (CONCEPT, "Quoted Expressions"). A hole that is a captured variable or a field path
can be recovered from `captures()`, and a hole that is a call - `{format(order.total)}` - cannot, and `value()` is the
string already joined without escaping. It also does not build natively yet (problem 5). **Rejected.**

### 4.6 The comparison

| | A. Builder | B. `Html` literal | C. Markup literals | D. Template files |
|---|---|---|---|---|
| Language change | **none** | one literal rule on URI.md's closed list | a lexical mode, `<` and `{` rules | a second file grammar |
| Builds today | **yes** (probes 1, 5) | parses; means a `String` | no | no |
| Escaping by construction | yes | yes, per context, checked at the literal | yes if it desugars to A | yes if compiled like B |
| Attributes typed | **yes, per element** | URL holes typed, names checked by the tokenizer | yes if it desugars to A | as B |
| Events typed (`Html<Message>`) | **yes** | only with per-attribute hole types | yes if it desugars to A | as B |
| Components | `extend Markup`, a `fn` | a `fn` answering `Html` | `<OrderRow order={...} />` | files calling files |
| Canon and tooling | **existing** | existing; a markup check in the checker | all new | all new |
| Paste HTML from a designer | no | **yes** | yes | yes |
| Static/dynamic split for live diffs | no (section 6.3 diffs parts) | **yes**, known at compile time | yes | yes |
| Cost | `std/html` | `std/html` + checker + lowering + fixpoint | parser, lexer, canon, LSP, seed | a new front end |

**Recommendation: A as the one way to write markup, B as a later, separate decision, C and D not at all** (D1-D4).

### 4.7 Escaping, exactly

| Where a value goes | What happens |
|--------------------|--------------|
| text content (`p text`, `text`, a hole of B in text) | `&`, `<`, `>` escaped |
| an attribute value | `&`, `<`, `>`, `"`, `'` escaped, and the value always quoted |
| a URL attribute (`href`, `src`, `action`, `formaction`) | the parameter is a `Link` (a `Route`, a `Uri`, a fingerprinted asset). A `Uri` whose scheme is not `http`, `https`, `mailto` or relative is refused where the `Uri` is made, so `javascript:` cannot reach an attribute |
| an attribute name | only from the generated table or `attributes:`, whose names are checked against the HTML name grammar |
| an event (`onClick:`) | a `Message` value; the page gets a handler id (section 6.2), never script text |
| `<script>` and `<style>` | only `script(source: Resource)` and `style(sheet: EmbeddedText)`: files the author shipped, never a `String` |
| trusted markup | `Html.trusted(file: EmbeddedText)` - markup the author put into the binary, trusted at build time exactly as RESOURCES.md trusts it - and `Html.sanitized(markup, policy)` for user HTML, which parses and keeps an allowlist |

**There is no function from a `String` to `Html` that neither escapes nor sanitizes.** A Markdown renderer writes
through the builder, element by element, like every other producer of markup.

### 4.8 Untrusted templates: the sandbox

A shop that lets its customers edit their own email templates needs Liquid or Twig, and TorbScript has something
better: **a template is a receiver script against a `Markup<Never>`** (SCRIPTS.md). The data the template may show is
the receiver's members, its `for` and `if` are the language's, a step, time and memory limit stops a template that
loops, and it cannot escape the escaping, because it can only reach markup through the builder. `Sandbox.read` loads
one a customer wrote; `Sandbox.load` checks a shipped one at build time (RESOURCES.md section 7). This is not v1 - a
native binary that loads scripts is SCRIPTS.md slice 8 - but it needs nothing of this record beyond section 4.2.

---

## 5. Server-driven UI: what exists

| System | Where the state lives | What is diffed | Transport | Latency per interaction | Offline | First paint, SEO | Cost per connected user |
|--------|----------------------|----------------|-----------|-------------------------|---------|------------------|-------------------------|
| **React Server Components** (Next.js) | client components in the browser; server components are per request and hold none | the RSC payload, a serialized tree, reconciled by React | streamed HTTP; server actions as `POST` | local for client state, one round trip for server data | what client code caches | server-rendered, streamed | per request; the cost moves to the bundle and hydration in the browser |
| **Blazor Server** | the server: a *circuit* per tab | the render tree, diffed on the server, sent as binary render batches | SignalR (WebSocket, fallbacks) | one round trip for every event | none | prerendered, then rendered again when the circuit starts | about 250 KB per circuit for a minimal app (Microsoft's hosting docs); sticky sessions |
| **Blazor WebAssembly** | the browser: .NET in WebAssembly | the render tree, in the browser | HTTP for data | local | yes, as a PWA | needs prerendering; a download of several MB | none on the server |
| **Laravel Livewire** | **in the page**: a dehydrated snapshot with a checksum, sent back with every request | the whole component re-rendered on the server, morphed into the DOM | one HTTP `POST` per interaction | one round trip | none | full HTML first | **nothing between requests**; the snapshot travels both ways |
| **Vaadin Flow** | the server: the Java component tree of the session | property changes of server-side components, synced to web components | XHR, WebSocket for push | one round trip | none | historically a client shell | the component tree per session, commonly tens to hundreds of KB |
| **Phoenix LiveView** | the server: one BEAM process per LiveView | **only the dynamic parts**: templates are compiled into static and dynamic parts, and a change sends changed dynamics | WebSocket, long-poll fallback | one round trip; `JS` commands for client-only changes | none | a dead render, then the connected mount (two renders) | a process of a few KB plus its assigns and the rendered tree it keeps |
| **htmx** | wherever the server keeps it; no component model | nothing: the server answers HTML fragments swapped into targets | HTTP per interaction | one round trip | none | full HTML | per request |
| **Hotwire / Turbo** | the server; Stimulus for small client state | Frames swap regions, Streams apply actions, Turbo 8 morphs a refreshed page | HTTP; Streams over WebSocket or SSE | one round trip | none | full HTML | per request, plus a socket for streams |
| **SolidStart** | the browser: fine-grained signals; server functions for data | the signals update the DOM directly | HTTP RPC | local | partly | server-rendered, then hydrated | per request |
| **Qwik** | the browser, serialized into the HTML at render time: **resumability** instead of hydration | fine-grained, in the browser; handlers are loaded on first use | HTTP | local after the handler loaded | partly | server-rendered with nothing to replay | per request |

**The axes that matter for TorbScript.** State on the server (Blazor Server, LiveView, Vaadin) costs memory and a
connection per user, and in exchange keeps secrets and data on the server and ships no application code. State in the
page (Livewire, Qwik) costs bytes per interaction and requires that the state be serializable, and in exchange costs
nothing between interactions and survives a deploy. State in the browser (Blazor WebAssembly, Solid, RSC's client
components) costs a download and a second runtime, and in exchange is the only one that works offline.

**No single placement is right for every page**, and every framework of the table has picked one and bolted the others
on (Blazor's render modes are the closest to admitting it). TorbScript can do better, because the one condition that
makes all three placements work is one the language already meets: **the state is a value.** A value can be kept in a
task's frame, encoded into the page, or compiled to run in the browser - without a line of the page changing.

---

## 6. The live model

### 6.1 A page is a value, a verb and a view

**The Elm architecture, in TorbScript's own words.** The state is a `type`; what can happen is a `type` with cases; the
verb that changes the state is a `var fn`; the view is a function of the state answering `Html<Message>`. **Probe 6
builds and runs this against stand-ins:**

```trb
use Live, Effect, fragment from "std/live"
use Html from "std/html"

type SearchMessage {
  case Typed(text: String)
  case Found(products: List<Product>)
  case Cleared
}

type Search with Live<SearchMessage> {
  var query: String = ""
  var results: List<Product> = []
  var searching: Bool = false

  var fn update(message: SearchMessage): Effect<SearchMessage> {
    match message {
      .Typed(text) => {
        query = text
        searching = true
        Effect.task found(text)
      }
      .Found(products) => {
        results = products
        searching = false
        Effect.none()
      }
      .Cleared => {
        query = ""
        results = []
        Effect.none()
      }
    }
  }

  static fn view(model: Search): Html<SearchMessage> {
    fragment {
      input kind: "search", value: model.query, onInput: SearchMessage.Typed, debounce: 250.milliseconds()
      button onClick: .Cleared {
        text "Clear"
      }
      if model.searching {
        p "Searching..."
      }
      ul {
        for product in model.results {
          li "{product.name}: {product.price} EUR"
        }
      }
    }
  }
}
```

- **`update` is a `var fn` and answers no `Task`**, and that is not a choice: a `var` receiver of a *value* on a
  function that answers a `Task` is an error (`A var of a value is copy in, copy out, and the copy back happens when
  the call returns - before the task has run`, probe 3). So everything that waits is an **`Effect`**: a task the
  framework runs whose answer comes back as the next message. `Effect.task found(text)` is that; a second `Typed`
  before the first search answered cancels the first task (CONCURRENCY.md section 8: every task can be cancelled), so
  the last keystroke wins without a line of code.
- **`view` is `static` and takes the model**, because inside a block only the innermost receiver is implicit (section
  4.2): as a method its fields would be out of scope in every element block.
- **`Html<Message>` types the events.** `onClick: .Cleared` must be a `SearchMessage`, and
  `onInput: SearchMessage.Typed` must be a `(text: String) => SearchMessage` - a case constructor is one. A page without events is `Html<Never>`, so a
  static page cannot carry a handler by accident (after F2).
- **Testing it needs nothing**: `var search = Search()`, `search.update(SearchMessage.Typed("tea"))`, then assert on
  `search.results` or on `Search.view(search)`. No DOM, no socket, no mock.

### 6.2 Events: a handler table, never a string

While a view renders, each `onClick:` and `onInput:` appends its message or its function to the render's **handler
table** and writes only an index into the page: `data-on-click="3"`. The browser sends back the index and, for an
input, the text. The server looks the index up and gets the message.

- **The client cannot forge a message.** It can only name a handler the server rendered for the state it holds, and
  the only data it contributes is the event's text, which goes through the function the view named (`SearchMessage.
  Typed`) or, for a capsule, through its `TryFrom`. Livewire had to add `#[Locked]` because any public method was
  callable with any argument; this design has nothing to lock.
- **Functions stay on the server.** A handler may be a closure; it is never encoded, compared or sent, so
  `Html<Message>` needs neither `Equals` nor `Encode` of anything but the model.
- **Handler indexes are deterministic**: the same model rendered by the same build gives the same table. That is what
  lets the state leave the server (section 6.4).

### 6.3 Rendering and diffing: parts, compared by what they rendered

**The unit of change is a part**: a region of the view with a key, `part "results" { ... }`. On every event the view
renders, each part's output is hashed, and **only parts whose hash changed are sent**, as HTML; the client morphs each
into its element, keeping focus, the caret and the text of the input being typed in.

- **Compared by output, not by input.** Probe 2 keyed a part by the `hash()` of an input it declared - and the part also
  read a field it had not declared, and was stale after a rename. That is React's dependency-array bug, and it cannot
  be closed by a type: a closure's captures are not part of its type. Hashing what the part *rendered* is always
  correct and costs one render, which is the work the server does anyway.
- **Memoizing is an optimization with a stated rule.** `part "results", key: model.results { ... }` skips rendering the
  part while the key is `==` to the last one. Value semantics make the comparison sound - a `List<Product>` cannot have
  changed behind the framework's back - and cheap where the runtime compares shared storage by address first (F11).
  The rule it cannot enforce is that the block reads only its key, and the documentation says so.
- **Everything outside a part belongs to the root part.** A page with no parts is re-sent whole on every change, which
  is Livewire's model and correct; parts are how an author makes it smaller. Nested parts are sent inside their parent
  when the parent changed.
- **No virtual DOM, no tree kept.** The server holds a hash per part (eight bytes and a key), not a copy of the
  document, and the client morphs HTML it already knows how to parse.
- **The fine-grained step is B's.** With section 4.3 the compiler knows every literal's static text and its holes, which
  is LiveView's static/dynamic split: static parts sent once, then only changed holes. That is F1 and a later slice, and
  it is the reason B is kept on the list at all.

### 6.4 Where the state lives: three placements, one page

**The same `Live` type runs in three places, and where is a line of the application, not a rewrite.**

| Placement | State | Needs | Good for | v |
|-----------|-------|-------|----------|---|
| **Page** (stateless) | the model, encoded into the page and signed; every event is an HTTP `POST` carrying it | `Model: Encode & Decode` (derived for any record of values), F7 | forms, filters, search, wizards; any number of idle users; deploys; PHP hosting later | **v1** |
| **Session** | a `var` of one task per open page, on the server | a duplex connection (section 9), F5 | server push, dashboards, chat, large or secret state | v2 |
| **Browser** | the model in the browser, the same `update` and `view` compiled for it | a web target (F10) | offline, zero-latency interaction | v3 |

**Page placement, per event:** decode the snapshot and check its signature and build id; render the old model's view
without text, only to rebuild the handler table; take the handler the event names; `update`; run the effects to their
answer within a time limit, applying each message; render the new view; send the parts whose hashes differ from those
the client sent, and the new snapshot. Two renders per event and **nothing held between events** - no connection, no
session, no memory. The server is a plain `Handler` (section 3.1), so it needs nothing from NETWORK.md beyond a request
and a response.

**Session placement, per event:** the model is a local `var` of the session task. Nothing else can reach it - no other
task can see a `var` of this one - so there is no lock, no actor library and no shared state to design. The task reads
one inbox in which the socket's events and the effects' answers arrive:

```trb
fn session<Model: Live<Message>, Message>(
  initial: Model,
  var connection: LiveConnection<Message>,
): Task<Result<Void, WebError>> {
  var model = initial
  var shown = Rendered.of Model.view(model)
  connection.send(shown.everything()).outcome()?
  while const Some(input) = connection.next().outcome()? {
    const effect = model.update(shown.message(input)?)
    connection.start effect
    const next = Rendered.of Model.view(model)
    connection.send(next.changedSince(shown)).outcome()?
    shown = next
  }
  Ok void
}
```

`connection.next()` has to wait on the socket and on the effects at once. The language has no way to wait for the
first of two tasks (CONCURRENCY.md has `within` against a timer and `Task.all`, and no `select`), so either the
connection feeds both into one `Channel` - which needs the socket's reading end in a task of its own, and a socket is a
`shared type` confined to the task that made it - or the runtime gains `Task.first` (F5). The session placement waits
for that and for NETWORK.md's duplex connection.

**Browser placement** is the same `update` and `view` built for a web target, with the morph replaced by the same
function applied to the browser's own DOM. Section 8, F10 has the two routes to it.

### 6.5 The wire

**JSON, because `std/json` exists and a browser reads it natively.** The page placement over HTTP:

The page (a `GET`) is ordinary HTML, complete and indexable, with the live region marked:

```text
<main data-live="/search" data-snapshot="v1.9f2c...signature" data-build="3a71...">
  <input type="search" value="" data-on-input="0" data-debounce="250">
  <div data-part="results" data-hash="11aa02c3"> ... </div>
</main>
```

An event is a `POST` to the same URL:

```json
{
  "snapshot": "v1.9f2c...signature",
  "build": "3a71...",
  "event": { "handler": 0, "kind": "input", "text": "tea" },
  "hashes": { "": "0be5f1a2", "results": "11aa02c3" }
}
```

The answer:

```json
{
  "snapshot": "v1.77e0...signature",
  "parts": { "results": "<ul><li>Teapot: 30 EUR</li></ul>" }
}
```

- **The CSRF token rides in a header**, set by the client script from the page.
- **A different build answers `{ "reload": true }`**: handler indexes belong to the code that rendered them. The
  client reloads the URL; what the page was showing is in the URL or is lost, as in LiveView.
- **The session placement sends the same objects** as WebSocket text frames, without the snapshot, and the server may
  send a `parts` object at any time.
- **Compression is the transport's**: `Content-Encoding` on the answer, `permessage-deflate` on a socket.

### 6.6 Reconnection and deploys

- **Page placement has nothing to reconnect.** Every event is an independent request that carries its state. A server
  restart between two events is invisible; a deploy is a reload (section 6.5). Any server of the fleet can answer, so
  there is no sticky session.
- **Session placement keeps a disconnected session task for a window** (default 30 s): the client reconnects with the
  session id and the last version it applied, and the task answers with every part whose hash differs. After the window
  the task has ended, and the client falls back to the page placement's answer: a reload. A session that must survive
  a restart encodes its model into a `SessionStore` on every change, which is a line in the application and not a
  feature of the framework.
- **Multiple servers** need sticky routing for the session placement only; the page placement needs none.

### 6.7 What it costs

| | Held between events | CPU per event | Bytes per event |
|---|---|---|---|
| Blazor Server | the circuit: about 250 KB minimum | render tree diff | binary render batch |
| Phoenix LiveView | a process, its assigns, its rendered tree | changed dynamics only | the smallest: changed holes |
| Livewire | nothing | hydrate, call, render the component | the component's HTML and the snapshot |
| **TorbScript, page** | **nothing** | decode, two renders, hashes, encode | changed parts and the snapshot |
| **TorbScript, session** | the model, a hash per part, the handler table, a task frame, the socket's buffers | one render and hashes | changed parts |
| TorbScript, with F1 | as above | as above, holes compared | changed holes, as LiveView |

- **Why the session is small.** The model is the only thing of size, and a copy of a value shares its storage, so
  keeping the model costs nothing beyond the model; no tree is kept (section 6.3). A task is a record on its worker's
  heap, not a thread or a stack (CONCURRENCY.md section 2). The fixed part per session is a number slice 7 measures;
  the target is under 8 KB besides the model and the socket.
- **Why the page placement is cheap to run.** Its cost is CPU per event and nothing per user, which is the shape that
  scales with the number of *interactions* and not the number of open tabs.
- **Where the time goes** is rendering: appending pieces to a list and joining it (section 4.2), then hashing. A
  benchmark of the example page in `benchmarks/` is part of slice 1, against the plain `"""` string as the floor; the
  performance goal (docs/PERFORMANCE.md) is that the builder costs close to what that string costs.
- **Where the language pays off**: a view is a function of a value, so rendering the old model again to find a handler
  is correct by construction; `==` on a model is generated and deep; a model is `Encode` without a line of mapping;
  and a session's state cannot be touched by another task.

### 6.8 The client script

**A hand-written JavaScript file of a few kilobytes**, shipped as an `EmbeddedText` of `std/live` and served as a
fingerprinted asset: it forwards events (with `debounce`), posts or sends them, applies `parts` by morphing, keeps
the snapshot, marks elements `data-loading` while a request is in flight, and reloads on `{ "reload": true }`.

It is the one piece of this record not written in TorbScript, and it stays that way until a web target exists (F10);
then it can be TorbScript compiled for the browser. **Client-only behaviour** - a menu that opens, a class that
toggles - is a small set of commands the builder writes as data (`onClick: Client.toggle("menu")`), which the script
runs without a round trip, as LiveView's `JS` commands do. Anything more is the browser placement.

### 6.9 v1, honestly

**v1 is the page placement** (slices 6 and 7 of section 10): server-rendered pages, forms, parts re-sent by hash, an
HTTP `POST` per event, the model in the page. **It does not have**: server push, anything offline, fine-grained diffs,
client-side execution, reconnection (there is nothing to reconnect), streams of events from other users, or uploads
through a live form (a plain form posts a file). Latency is a round trip per interaction, like LiveView and Livewire;
typing is debounced, and the input keeps what the user typed while the answer is on its way.

That is less than LiveView, and it is honest about why: the session placement needs a duplex connection NETWORK.md has
not designed yet and a way to wait on two tasks the runtime does not have (F5). What v1 already has that LiveView does
not: a state that survives any restart, typed messages that cannot be forged, and a page that costs nothing while
nobody is clicking.

---

## 7. What the language gives

| Strength | Where it carries this design |
|----------|------------------------------|
| **Receiver closures and command calls** | the builder of section 4.2 and the application of section 3.1, with no syntax of their own (probes 1, 5, 7) |
| **Value semantics** | a model is snapshotted by keeping it, compared with a generated `==`, re-rendered for handler lookup; a request changed by a middleware is changed below it only; a session's model is a `var` nobody else can see |
| **Derived `Encode`, `Decode`, `Hash`, `Equals`** | forms decode into types, sessions and snapshots encode without mapping, parts hash |
| **Capsules and `TryFrom`** | validation lives in field types; a decoded `Email` cannot skip its check (ENCODING.md section 3a) |
| **Types with cases and exhaustive `match`** | routes, messages, effects: every new case is a list of compile errors |
| **`Result` and `?`** | handlers fail with `?`; `From` maps a failure to a status |
| **Tasks: stackless, cancellable, per-worker heaps** | effects cancel when superseded; a session is a record on a heap, not a thread; no lock anywhere in the framework |
| **The task rule on function values** | forced the middleware into a value chain (section 3.3), which is also the shape Spring users know |
| **Resources** | assets are checked at build time and fingerprinted (section 3.7) |
| **The sandbox** | customer-editable templates with limits (section 4.8) |
| **The VM** | the docs site's playground: the front end and the VM built for the browser run a snippet without a server (F10) |

---

## 8. What the language and the toolchain lack

Each item is its own decision; none of them is needed for slices 1 to 3 except where it says so.

| # | Feature | Needed by | Kind | Smallest form |
|---|---------|-----------|------|---------------|
| **F1** | **An interpolated literal whose expected type is `Html` is built from its parts** | alternative B (section 4.3), fine-grained diffs | language rule, checker, lowering; `std/html` joins the fixpoint | URI.md section 9's closed list gains `Html`; the checker tokenizes static parts and types holes by context |
| **F2** | **An uninhabited type argument builds**: `List<Never>` and every function over it | `Html<Never>` for static pages (probe 3) | back end | a slot of type `Never` is dead code, emitted as nothing; or monomorphize with a unit representation |
| **F3** | **A type parameter inferred through a trait bound**: `render(counter)` finds `Message` from `Counter: Live<CounterMessage>` - and the panic of problem 1 fixed | every generic function of `std/live` | checker | unify the bound's arguments after the parameter's type is known |
| **F4** | **Quoted expressions in the native back end** | typed form bindings `input bind: form.email` (section 3.5) | back end | `Expression<Value>` lowered as static tree data plus the capture list, as CONCEPT specifies |
| **F5** | **Waiting on the first of two tasks** (`Task.first`) | the session placement's loop (section 6.4) | runtime native, `std/task` | `Task.first(a, b)` answering which of the two finished, on the scheduler that already races a task against a timer for `within` |
| **F6** | **A derived `decode` that collects every field's failure** | forms (section 3.5) | ENCODING.md's derivation | `DecodeError` gains `problems: List<DecodeError>`; the derived record decode continues past a failing field and fails once at the end |
| **F7** | **`std/crypto`: SHA-256, HMAC-SHA256, a secure random source**, constant-time comparison | sessions, CSRF, snapshots (sections 3.6, 6.4) | new natives: two commits and a seed refresh | a portable C implementation of SHA-256 and HMAC in `runtime/`, `BCryptGenRandom`/`getrandom` for randomness; no TLS-grade library needed for this |
| **F8** | **`ResourceDirectory`** | assets beyond a handful of files (section 3.7) | RESOURCES.md section 6, checker | a literal naming a directory, resolved to its file list at build time |
| **F9** | **A panic that ends its task, not the process** | a server that survives one bad handler (section 3.8) | **language rule** (CONCURRENCY.md section 8 says the opposite), runtime | a recovery point per worker loop; the panicking task's frame is released as a cancelled one's, what the synchronous callees held leaks and is counted; the awaiter sees `Cancelled` or a new `Panicked` |
| **F10** | **A web target** | the browser placement (section 6.4); the docs playground | back end | route 1: the C back end and `runtime/` compiled with clang for `wasm32-wasi`, one worker, the browser's event loop as the poller - tasks are already state machines, so nothing needs a stack switch; route 2: the JavaScript back end of milestone 9 (`docs/design/JAVASCRIPT-AND-PHP.md`) |
| **F11** | **`==` on values that share storage answers by address first** | parts memoized by key, session diffs (section 6.3) | runtime and IR, no semantics | in the generated `equals` of a boxed record and in the runtime's list, map and text equality, when no `Float` is reachable (`NaN != NaN`) |
| **F12** | **A text buffer that appends in place** | rendering (section 4.2) | runtime | only if the benchmark of slice 1 shows the joined list is slower than it should be; measure first |
| **F13** | **A `test` whose body is a task** | handler tests (section 3.9), every asynchronous test | `std/test`, the runtime's recovery point, VM slice 6 | `test(name, body: () => Task<Void>)` beside today's `() => Void`, the body run to its end by the test runner |

**Not needed, and why.** A closure capturing `var self` natively: the builder hands `self` on as a `var` argument
(`children self`) and no block captures it (probes 1 and 5 build). Macros, annotations, reflection: routing is a
`match`, wiring is a constructor, forms and sessions are `Encode`/`Decode`. Overloading: one element function with
defaults covers `td "x"`, `td class: "a" { }` and `tr { }`.

**One question for the checker, not a feature.** Probe 7 found that a bare function value of invisible captures may not
cross into a task, while probe 8 found that a record carrying one may (`Next` above relies on it, and so would any
application). Whether the record path is intended or a gap in the rule of CONCURRENCY.md section 12 is for the checker
round to answer; if it is a gap, `Next` needs `endpoint` to be a trait value too, and section 3.3 does not otherwise
change.

---

## 9. What this record needs from NETWORK.md

Only the handler function type is assumed. These are requests, for that record to accept or refuse:

1. **`Request` is a value except for its body**: method, target, headers, the peer - so a middleware can `copy` it and a
   test can build one. The method is a type with cases (`.Get`, `.Post`, ...), which section 3.2 matches on.
2. **The request body is a `Source<Bytes, _>` read with a limit**, as `std/http`'s `Body` is; form, JSON and query
   decoding live in `std/web`, not in the transport.
3. **The response body may be a stream**, so a large page, a download or server-sent events can be sent while they
   are produced.
4. **A disconnected client cancels the handler's task** (CONCURRENCY.md section 8), so an effect or a slow query of a
   request nobody waits for any more stops.
5. **For the session placement: a duplex connection.** Either a `101 Switching Protocols` answer that keeps both bodies
   open as raw streams - WebSocket framing is then a `Stage` over bytes in `std/web`, not in the transport - or a
   connection type with a `source()` and a `sink()`. It does not block v1.
6. **One process may be several.** Section 3.8's answer to panics without F9 is a supervisor that restarts a worker
   process; `SO_REUSEPORT` on Linux or a listening socket handed to children is NETWORK.md's to decide.

---

## 10. Slices

| # | Slice | Needs | Risk |
|---|-------|-------|------|
| 1 | **`std/html`**: `Html<Message>`, `Markup<Message>`, the generated element table, escaping per section 4.7, `document`, `fragment`, `Html.trusted`, the example page as a test, the benchmark against a plain string | F2 (or a `Nothing` type until then, as probe 5 does) | **low**: probes 1 and 5 build it |
| 2 | **`std/web` core**: `Handler`, `WebApp` and `web { }`, `Middleware` and `Next`, `WebError`, the response helpers, `Request.segments`, `query`, `WebTest` with `answer` and `assertRoutes`; an `examples/web-shop` with the orders pages | NETWORK.md's `Request`/`Response`; problem 3 fixed | medium: the first real use of trait methods answering tasks |
| 3 | **Forms**: the form-urlencoded and multipart formats, `Submitted`, `Form`, field helpers, CSRF | F6 for all problems at once, F7 for CSRF | medium |
| 4 | **Sessions and cookies**: signed cookies, `SessionStore`, in-memory store | F7 | low once F7 exists |
| 5 | **Assets**: `assets` with `List<Resource>`, fingerprints, `stylesheet`/`script` elements | RESOURCES.md slices 1-3; F8 for directories | low |
| 6 | **`std/live`, page placement**: `Live`, `Effect`, handler tables, parts and hashes, snapshots, the `POST` protocol, `LiveTest` | F3, F7 | **highest**: the protocol and the client script |
| 7 | **The client script**, morphing, debounce, loading marks, client commands; a browser test of the example search | slice 6 | medium: the one JavaScript file |
| 8 | **Session placement**: the session task, WebSocket framing, reconnection window, broadcast topics | NETWORK.md item 5, F5 | high |
| 9 | **F1**: `Html` literals and, with them, static/dynamic diffs | owner decision D2 | high: a checker rule and a fixpoint member |
| 10 | **Browser placement** | F10, owner decision D9 | after milestone 9 |

Slices 1 and 2 need no language change at all.

---

## 11. What this is not

- **Not the transport.** Sockets, TLS, HTTP parsing, keep-alive, the poller and HTTP/2 are NETWORK.md's.
- **Not an ORM, a DI container or a migration tool.** Milestone 10 plans those with the framework (`docs/design/FRAMEWORK.md`); this record
  covers the web layer and says only that dependencies are captured values (section 3.1).
- **Not a template language.** There is no second grammar; markup is TorbScript (section 4.2), or with F1 a string
  literal the checker understands.
- **Not a virtual DOM.** Nothing keeps or diffs a document tree; parts are compared by what they rendered.
- **Not a single-page-application framework.** Client-side routing, a client state store and hydration are not
  proposed; the browser placement runs the same page model, not a second architecture.
- **Not a security boundary beyond its own guarantees.** Escaping, CSRF and unforgeable messages are by construction;
  authorization is the application's, in a middleware or in `update`.

---

## 12. Owner decisions

Each with the options and the recommendation. Everything technical not listed here is decided above.

| # | Decision | Options | Recommendation |
|---|----------|---------|----------------|
| **D1** | How markup is written | (a) the builder of section 4.2 as the one way; (b) the builder and B side by side from the start; (c) only B | **(a)**. It builds today, needs no language change, types every attribute and event, and is in the canon. B comes later if D2 says so |
| **D2** | `Html` literals (F1) | (a) never; (b) later, as slice 9, for prose and designer HTML; (c) now, before the builder | **(b)**. It is Scala 3's own answer, costs one rule on an existing list, and is the only route to LiveView-sized diffs |
| **D3** | Native markup literals (JSX, Scala 2 XML) | (a) no; (b) as sugar over the builder | **(a)**. Six costs (section 4.4), one of them a design principle, for pasting HTML that D2 also gives |
| **D4** | Template files compiled by `torb` (Twirl, templ) | (a) no; (b) as `Sandbox`-checked resources | **(a)** for v1; the sandbox of section 4.8 covers customer templates |
| **D5** | Routing | (a) a `match` over a route type, both directions written out, held together by `assertRoutes`; (b) a pattern DSL (`get "/orders/{id}"`) decoded into cases through `Describe`, checked at startup | **(a)**. Exhaustive in both directions, typed links, no string grammar; (b) can be added on top later for people coming from Spring's `@GetMapping` |
| **D6** | The component model | (a) the Elm architecture: one model per page, `update`, `view`, parts; (b) stateful components with their own state each (Blazor, LiveComponents) | **(a)**. A model is one value: one snapshot, one `==`, one encoding. (b) is nested state machines and message routing between them, which Elm users know as the thing to avoid |
| **D7** | Where live state lives in v1 | (a) in the page, one `POST` per event (Livewire's placement); (b) in a session task over WebSocket (LiveView's); (c) both at once | **(a)**. It runs on nothing but `Request => Task<Response>`, survives restarts and deploys, needs no sticky sessions, costs nothing while idle, and is the placement a PHP back end could serve from a web space. (b) follows in slice 8 |
| **D8** | Diff granularity | (a) parts compared by rendered output, morphed on the client; (b) a virtual DOM; (c) LiveView's static/dynamic split | **(a)** now, **(c)** with D2. (b) keeps a tree per session for what (a) gets with a hash |
| **D9** | Which web target first (F10) | (a) C back end to `wasm32`; (b) the JavaScript back end of milestone 9; (c) neither until asked | **(c)** for this record; when milestone 9 starts, **(a)** for the docs playground (the VM already exists, and `runtime/` is C11) and **(b)** for pages, whose DOM access and size favour JavaScript |
| **D10** | A panic in a handler | (a) keep "a panic ends the process"; ship a supervisor that restarts worker processes; (b) F9: a panic ends its task, leaks what its synchronous frames held, and the server answers 500 | **(a)** for v1. It keeps one rule for every program and costs a process restart per bug; (b) is a language change that deserves the numbers of a running server before it is made |
| **D11** | Assets before `ResourceDirectory` | (a) v1 lists files as `Resource` literals and waits for F8; (b) v1 also serves a `Path` directory from disk | **(a)**. A build error for a missing asset is worth a list; (b) is a second way that stays after F8 arrives |
| **D12** | Forms report every problem (F6) | (a) change ENCODING's derived `decode` to collect; (b) `std/web` decodes field by field itself | **(a)**. Every format gains it (a config file with three mistakes says three), and (b) cannot call a field type's decoder without reflection |
| **D13** | Package names | `std/html`, `std/web`, `std/live`; or one `std/web` with modules | three packages: `std/html` has no HTTP in it and is useful alone (emails, reports, the docs generator) |
| **D14** | The word for an interactive page | `Live` (`type Search with Live<SearchMessage>`), `Page`, `Component`, `Interactive` | `Live`: short, and it names what differs from a page that is only sent. A question of taste |

## 13. Decided

- **D1-D4: markup is written with the builder, and only with it.** No `Html` interpolated literal (D2 is declined, so F1
  is dropped), no native markup literals, no template files. One way to write markup.
- **D5: routing uses URI templates (RFC 6570), checked by the compiler.** `UriTemplate` joins the closed list of
  literal types (URI.md section 9): a string literal whose expected type is `UriTemplate` is parsed where it is written,
  and inside it `{...}` is a template expression, not interpolation. Only the reversible subset is accepted
  (`{name}`, `{?name,other}`, `{/name*}`). The type of the literal carries its variables as a labelled tuple
  (`UriTemplate<Variables>`), so `route("/orders/{id}", to: Route.Order)` checks the variable names and types against the
  parameters of the case constructor; a parameter with a default may only be an optional query variable. Both
  directions (parsing a request, producing a link) are derived from the one template, and an application refuses to
  start when a case of its `Route` type has no route. The same typed-literal mechanism is intended for named groups of
  `Regex`.
- **D6/D7: the live UI follows the Elm architecture, and its state lives in a task per session from the start**
  (placement v2 of section 6.4 becomes v1). This needs the duplex connection of NETWORK.md and `Task.first` (F5) first.
- **D8-D12: as recommended** (parts compared by the hash of what they rendered; no web target yet, WebAssembly only for
  the playground later; a panic in a handler ends the process and a supervisor restarts it; assets as `Resource`
  literals until resource directories exist; decoding collects every field failure).
- **D13/D14: `std/html`, `std/web`, `std/live`, and the trait is `Live`.**
