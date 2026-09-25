---
title: std/uri
summary: Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, the file bridge to Path and the form codec of HTML - values that open nothing.
kind: package
status: stable
order: 129
keywords:
  - std/uri
  - Uri
  - UriReference
  - UriError
  - Urn
  - IRI
  - Authority
  - Host
  - resolved
  - queryParameters
  - formEncoded
  - formDecoded
source:
  - std/uri/src/lib.trb
  - std/uri/src/uri.trb
  - std/uri/src/authority.trb
  - std/uri/src/urn.trb
  - std/uri/src/form.trb
  - docs/design/URI.md
---

`std/uri` is Uniform Resource Identifiers as values. A `Uri` is read from text once, with `Uri.tryFrom(text)?`, and
normalized on the way in - the scheme and the host lower case, escapes upper case and decoded where they name an
unreserved character, dot segments gone, every character a URI cannot hold percent-encoded as UTF-8 - so `==`,
`hash()` and `compare()` are over the canonical form without anybody asking for it. A text that is no URI is refused at
the door with a `UriError` that names it. Nothing in the package opens anything: an IP literal host is an address of
[std/ip](ip.md), and resolving a name or connecting is [std/network](network.md)'s. `Uri` and `UriError` are in the
prelude; everything else is an import. The design, and why it reads RFC 3986 rather than the WHATWG standard, is
[docs/design/URI.md](../design/URI.md).

## Import

```trb fragment
use Uri, UriReference, UriError, Urn, Authority, Host, formEncoded, formDecoded, percentDecoded from "std/uri"
```

```trb check
use UriReference from "std/uri"

fn linkTarget(page: String, link: String): Result<Uri, UriError> {
  const base = Uri.tryFrom(page)?
  const reference = UriReference.tryFrom(link)?
  Ok reference.resolved(against: base)
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
URI, and a non-ASCII host is refused until the package has IDNA.

`segments()` is the path split at its `/`, still percent-encoded, without the empty segment in front of an absolute
path (`/a/b/` has `a`, `b` and `""`); `percentDecoded` decodes one. `joined(relative)` builds a URI below this one: each
segment of `relative` percent-encoded, and a `..` never climbs above the receiver, so `base.joined(input)` stays below
`base`. `query()` is one opaque string, as RFC 3986 leaves it, and `queryParameters()` reads it as a form.

`Uri` is a [capsule](../language/types/data-or-capsule.md): `tryFrom` is the one way in and `text()` the way out, and
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
}
```

The `[userInfo "@"] host [":" port]` of a URI, as data. `mailto:ada@example.test` has no authority, and `file:///x` has
one whose host is `Name("")`. A host is an IP literal, an IPv4 address or a registered name, and the first of RFC 3986's
rules that fits decides: `127.0.0.1` and `[::1]` are `Address`, written back in RFC 5952's form, and `01.2.3.4` is a
`Name`. A zone identifier (`[fe80::1%25eth0]`) is refused, because RFC 9844 took it out of the URI grammar.

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
}
```

Why a text is not what it was read as. A `%` that is not followed by two hexadecimal digits is refused rather than
encoded, because `100%` in a path is a typo or a text somebody forgot to encode. Every case carries the display form,
so a password never reaches a message.

## Related

- [std/ip](ip.md) - the addresses an IP literal host is.
- [std/path](path.md) - `Path`, and why it is not a URI.
- [std/http](http.md) - HTTP, one level up.
- [The standard library](index.md) - the other packages.
