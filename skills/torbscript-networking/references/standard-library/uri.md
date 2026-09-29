---
title: std/uri
summary: Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
kind: package
status: stable
order: 129
keywords:
  - std/uri
  - Uri
  - UriReference
  - UriError
  - Urn
  - UriTemplate
  - TemplateValue
  - TemplateCase
  - TemplateRoutes
  - route
  - RFC 6570
  - IRI
  - Authority
  - Host
  - resolved
  - queryParameters
  - formEncoded
  - formDecoded
  - Schemes
source:
  - std/uri/src/lib.trb
  - std/uri/src/uri.trb
  - std/uri/src/authority.trb
  - std/uri/src/urn.trb
  - std/uri/src/form.trb
  - std/uri/src/template.trb
  - std/uri/src/route.trb
  - std/uri/src/schemes.trb
  - docs/design/URI.md
---

`std/uri` is Uniform Resource Identifiers as values. A `Uri` is read from text once, with `Uri.tryFrom(text)?`, and
normalized on the way in - the scheme and the host lower case, escapes upper case and decoded where they name an
unreserved character, dot segments gone, every character a URI cannot hold percent-encoded as UTF-8 - so `==`,
`hash()` and `compare()` are over the canonical form without anybody asking for it. A text that is no URI is refused at
the door with a `UriError` that names it - and a literal where a `Uri` is expected, `open("https://…")`, is read by
the compiler, so a typo in one is an error at that line. Nothing in the package opens anything: an IP literal host is an address of
[std/ip](ip.md), and resolving a name or connecting is [std/network](network.md)'s. `Uri` and `UriError` are in the
prelude; everything else is an import. The design, and why it reads RFC 3986 rather than the WHATWG standard, is
docs/design/URI.md.

## Import

```trb fragment
use Uri, UriReference, UriError, Urn, UriTemplate, TemplateValue, TemplateValues, Authority, Host from "std/uri"
use TemplateVariable, TemplateCase, TemplateRoute, TemplateRoutes, route from "std/uri"
use formEncoded, formDecoded, percentDecoded from "std/uri"
use Schemes from "std/uri"
```

```trb check
use UriReference from "std/uri"

fn linkTarget(page: String, link: String): Result<Uri, UriError> {
  const base = Uri.tryFrom(page)?
  const reference = UriReference.tryFrom(link)?
  reference.resolved against: base
}

print linkTarget("https://example.test/guide/start.html", "../reference/index.html#top")
```

## Declarations

### Uri

```trb fragment
public type Uri with Show, Equals, Hash, Compare {
  fn scheme(): String
  fn authority(): Authority?
  fn host(): Host?
  fn port(): Int?
  fn defaultPort(): Int?
  fn socketAddress(): SocketAddress?
  fn path(): String
  fn segments(): List<String>
  fn query(): String?
  fn fragment(): String?
  fn isUrl(): Bool
  fn isUrn(): Bool
  fn queryParameters(): Map<String, List<String>>
  fn queryParameter(name: String): String?
  fn withQueryParameters(parameters: Map<String, List<String>>): Uri
  fn withFragment(fragment: String?): Uri
  fn joined(relative: String): Uri
  fn normalized(): Uri
  fn relativeTo(base: Uri): UriReference
  fn text(): String
  fn iriText(): String
  fn show(): String
}
extend Uri with TryFrom<String, UriError>
extend Uri with TryFrom<Path, UriError>
extend Uri with From<Urn>
```

A URI per RFC 3986 section 3: it always has a scheme. What construction does not decide, because it is a guess about
a scheme rather than a fact about URIs, is `normalized()`'s: the default port of `http`, `https`, `ws`, `wss` and `ftp`
dropped and an empty path under an authority made `/` - so `http://example.test:80` and `http://example.test/` are two
values until both are normalized. `port()` is the port that is written; `defaultPort()` the scheme's, and
`socketAddress()` answers where to connect without a resolver when the host is an IP literal.

**`show()` hides a password and `text()` does not.** `postgres://ada:hunter2@db/orders` shows as
`postgres://ada:***@db/orders`, which is what interpolation, `print` and every failure message reach for; `text()` is the
whole text a request, a file or an encoder needs, and `Encode` writes it. `iriText()` is the IRI the URI stands for
(RFC 3987): escapes of UTF-8 characters outside ASCII decoded, so `https://example.test/%C3%A4` reads as
`https://example.test/ä`, and `Uri.tryFrom(uri.iriText())` is the same URI again. An IRI is read by `tryFrom` like any
URI. A host outside ASCII is stored as its A-labels - `https://münchen.test/` is `https://xn--mnchen-3ya.test/` - through
the IDNA of [std/dns](dns.md), and `iriText()` shows the U-labels again; a host IDNA refuses is `InvalidHost`, and
`NonAsciiHost` is a host of percent escapes that are not UTF-8.

`segments()` is the path split at its `/`, still percent-encoded, without the empty segment in front of an absolute
path (`/a/b/` has `a`, `b` and `""`); `percentDecoded` decodes one. `joined(relative)` builds a URI below this one: each
segment of `relative` percent-encoded, and a `..` never climbs above the receiver, so `base.joined(input)` stays below
`base`. `query()` is one opaque string, as RFC 3986 leaves it, and `queryParameters()` reads it as a form.

`Uri` is a capsule (skill `torbscript-language`: `references/language/types/data-or-capsule.md`): `tryFrom` is the one way in and `text()` the way out, and
the two are the conversion pair `Encode` and `Decode` go through, so a URI in a JSON document is its text.

### UriReference

```trb fragment
public type UriReference with Show, Equals, Hash, Compare {
  fn scheme(): String?
  fn isAbsolute(): Bool
  fn isRelative(): Bool
  fn uri(): Uri?
  fn resolved(against: Uri): Uri
}
extend UriReference with TryFrom<String, UriError>
extend UriReference with TryFrom<Path, UriError>
extend UriReference with From<Uri>
```

A URI reference per RFC 3986 section 4.1: a URI, or a relative reference - `../a`, `/b?c`, `//host/d`, `#top` - that
means something only against a base. It has the reading members of `Uri`, with the scheme an `Option`, and
`resolved(against:)` is RFC 3986 section 5.2: a reference resolved against a `Uri` is a `Uri`, and cannot fail. The dot
segments of a relative path are kept until then, because they mean something only once a base is known.
`uri.relativeTo(base)` is the way back: the shortest reference that resolves into `uri`.

### Authority and Host

```trb fragment
public type Authority with Show, Equals, Hash {
  userInfo: String?
  host: Host
  port: Int?

  fn text(): String
  fn socketAddress(defaultPort: Int? = None): SocketAddress?
}
public type Host with Show, Equals, Hash {
  case Name(name: String)
  case Address(address: IpAddress)
  case Future(text: String)

  fn domainName(): DomainName?
}
```

The `[userInfo "@"] host [":" port]` of a URI, as data. `mailto:ada@example.test` has no authority, and `file:///x` has
one whose host is `Name("")`. A host is an IP literal, an IPv4 address or a registered name, and the first of RFC 3986's
rules that fits decides: `127.0.0.1` and `[::1]` are `Address`, written back in RFC 5952's form, and `01.2.3.4` is a
`Name`. A zone identifier (`[fe80::1%25eth0]`) is refused, because RFC 9844 took it out of the URI grammar.
`domainName()` answers a registered name as the `DomainName` of [std/dns](dns.md) a resolver is asked for, and `None`
for an address and for a name the DNS cannot carry - a percent escape, a sub-delimiter, a label longer than 63 bytes.

### Urn

```trb fragment
public type Urn with Show, Equals, Hash, Compare {
  fn uri(): Uri
  fn namespace(): String
  fn specific(): String
  fn resolution(): String?
  fn query(): String?
  fn fragment(): String?
  fn assignedName(): Urn
}
extend Urn with TryFrom<Uri, UriError>
```

A `urn:` URI read after RFC 8141: the namespace identifier (lower case, two to thirty-two letters, digits and `-`), the
namespace-specific string, and the r-, q- and f-components. RFC 8141's equivalence ignores the last three, and it is
`first.assignedName() == second.assignedName()`; `==` compares the whole URN.

### UriTemplate

```trb fragment
public type UriTemplate<Variables> with Show, Equals, Hash, TryFrom<String, UriError> {
  static fn tryFrom(value: String): Result<UriTemplate<Variables>, UriError>
  fn variables(): List<String>
  fn templateVariables(): List<TemplateVariable>
  fn level(): Int
  fn expandedText(values: TemplateValues): Result<String, UriError>
  fn expanded(values: TemplateValues): Result<UriReference, UriError>
  fn expandedFrom(values: Variables): Result<UriReference, UriError> where Variables: Encode
  fn isMatchable(): Bool
  fn matched(reference: UriReference): TemplateValues?
  fn decoded(reference: UriReference): Variables? where Variables: Decode
}
public type TemplateValues = Map<String, TemplateValue>
public type TemplateValue with Show, Equals, Hash {
  case Text(value: String)
  case Items(values: List<String>)
  case Pairs(values: List<(String, String)>)
}
public type TemplateVariable with Show, Equals, Hash {
  name: String
  optional: Bool
  exploded: Bool
}
```

An RFC 6570 template, all four levels: `{var}`, `{+var}` and `{#var}`, the operators `.`, `/`, `;`, `?` and `&` with
several variables, and the prefix and explode modifiers (`{var:3}`, `{list*}`). `expanded` answers the URI reference
the template makes with the values in it; a variable that is not in the map is undefined and expands to nothing, as the
RFC says. The tests are the RFC's examples and the whole `uritemplate-test` suite.

`matched` reads a template backwards: the values that expand it into a reference, decoded. It is defined for the
templates `isMatchable()` accepts - simple variables and path segments with a literal between them, an exploded path
segment last in the path, and query parameters written `{?a,b}` or `{&c}`, which match in any order and ignore the
parameters they do not name. That is the subset routes are written in (see
docs/design/WEB.md section 13).

**`Variables` is the type whose fields the variables are.** A literal where a `UriTemplate<OrderPath>` is expected is
read by the compiler (the literal rule (skill `torbscript-language`: `references/language/values-and-types/checked-literals.md`)), verbatim - `{id}` is the
template's variable and never an interpolation - and a variable that is no field of `OrderPath`, or a field without a
default that is no variable, is an error at the literal. `expandedFrom` and `decoded` go through the fields, with
`Encode` and `Decode`. A template read while the program runs is a `UriTemplate<TemplateValues>`, whose variables are
whatever the map holds.

```trb check
use UriTemplate, TemplateValue, TemplateValues, UriReference from "std/uri"

fn orderOf(target: String): Result<String, UriError> {
  const template: UriTemplate<TemplateValues> = "/orders/{id}{?fields}"
  const values = template.matched(UriReference.tryFrom(target)?) ?? [:]
  match values.get("id") {
    Some(.Text(id)) => id
    _ => "no order"
  }
}

print orderOf("/orders/7?fields=total")
```

### Routes

```trb fragment
public type TemplateCase<Value> with Show, Equals, Hash {
  static fn named(name: String): TemplateCase<Value>
  fn name(): String
}
public type TemplateRoute<Value> with Show {
  template: UriTemplate<Value>
  target: TemplateCase<Value>
}
public fn route<Value>(template: UriTemplate<Value>, to: TemplateCase<Value>): TemplateRoute<Value>
public type TemplateRoutes<Value> with Show {
  static fn of(routes: List<TemplateRoute<Value>>): Result<TemplateRoutes<Value>, UriError> where Value: Describe
  fn matched(reference: UriReference): Value? where Value: Decode
  fn link(value: Value): Result<UriReference, UriError> where Value: Encode
}
```

Templates typed against the cases of one type: what a router is built from (docs/design/WEB.md,
decision D5). `Route.Order` where a `TemplateCase<Route>` is expected is the case `Order`, named where it is written,
and `route("/orders/{id}", to: Route.Order)` is checked by the compiler: every variable is a field of the case, every
field without a default a variable, a field with a default at most a query variable, and the template one that can be
matched backwards. `TemplateRoutes.of` checks the same when the program starts, and it is what refuses a case without a
route; `matched` reads a reference into the case of the first route that matches it, and `link` expands the route of a
value's case, so the two directions come from one template.

```trb check
use TemplateRoutes, route, UriReference from "std/uri"

type Page {
  case Home
  case Order(id: Int)
  case Search(query: String = "")
}

const routes = TemplateRoutes.of([
  route("/", to: Page.Home),
  route("/orders/{id}", to: Page.Order),
  route("/search{?query}", to: Page.Search),
])?
print routes.matched(UriReference.tryFrom("/orders/7")?)
print routes.matched(UriReference.tryFrom("/search?query=tea")?)
print routes.link(Page.Order(12))
```

### Path and file: URIs

`Uri.tryFrom(path)` writes an absolute `Path` as a `file:` URI (`C:/Users/ada` as `file:///C:/Users/ada`, a UNC share as
`file://server/share/x`), and `UriReference.tryFrom(path)` writes a relative one as a relative reference.
`Path.tryFrom(uri)` reads every form RFC 8089 knows - `file:///x`, `file:/x`, `file://localhost/x`, `file:///C|/x`,
`file:C:/x` - and refuses another scheme, a query and an escape that names a separator. None of the conversions is a
`From`, so each type keeps its own conversion pair with `String`.

### Forms and escapes

```trb fragment
public fn formDecoded(text: String): List<(String, String)>
public fn formEncoded(pairs: List<(String, String)>): String
public fn percentDecoded(text: String): Result<String, UriError>
```

`application/x-www-form-urlencoded`, byte for byte as the WHATWG URL Standard's parser and serializer: `+` is a space,
bytes that are not UTF-8 read as U+FFFD, and only ASCII letters, digits and `*-._` stay unescaped. It is the reading of
`queryParameters()` and the writing of `withQueryParameters`, and the format of an HTML form's body.

### Schemes

```trb fragment
public trait Schemes {
  fn schemes(): List<String>
}
```

The schemes a driver answers to, in lower case: the one member a registry of drivers dispatches on
(URI.md section 11). It is here rather than in std/storage (skill `torbscript-standard-library`: `references/standard-library/storage.md`) because it declares no
scheme and opens nothing, so a package of a database driver can take it without taking a storage package.

### UriError

```trb fragment
public type UriError with Show, Error {
  case InvalidScheme(text: String)
  case InvalidEscape(text: String)
  case InvalidHost(host: String, reason: String)
  case NonAsciiHost(host: String)
  case InvalidPort(text: String)
  case Relative(text: String)
  case NotText(reason: String)
  case NotAFile(uri: String, reason: String)
  case NotAUrn(uri: String, reason: String)
  case InvalidTemplate(template: String, reason: String)
  case InvalidRoutes(reason: String)
}
```

Why a text is not what it was read as. A `%` that is not followed by two hexadecimal digits is refused rather than
encoded, because `100%` in a path is a typo or a text somebody forgot to encode. Every case carries the display form,
so a password never reaches a message.

## Related

- [std/ip](ip.md) - the addresses an IP literal host is.
- std/path (skill `torbscript-standard-library`: `references/standard-library/path.md`) - `Path`, and why it is not a URI.
- [std/http](http.md) - a client that takes a `Uri`, and a server whose requests carry one.
- std/storage (skill `torbscript-standard-library`: `references/standard-library/storage.md`) - bytes at a `Uri`, with a driver chosen by its scheme.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the other packages.

