# Uniform Resource Identifiers

**Status: proposed** — there is no `std/uri` yet; the probes of this document were run against the checker only.

**A URI is a value, and a text that is not one is refused at the door.** That is the whole design of `std/uri`: one
capsule for RFC 3986, normalized at construction, so that `==`, `hash()` and `compare()` are over the canonical form
without anybody asking for it — and so that a signature that takes a `Uri` cannot be handed a typo.

```text
                       ┌── scheme()      ─────  "https", lower case. None for a relative reference
                       ├── authority()   ─────  userInfo?, host (lower case), port?
       Uri ────────────┼── path()        ─────  always there, dot segments gone where it is absolute
   (a capsule:         ├── query()       ─────  one opaque string, RFC 3986 section 3.4
    six private        └── fragment()    ─────  everything after the first #
    fields)

  a String LITERAL ──→ parsed by the compiler ──→ a Uri, or a build error at the literal
  a String VALUE   ──→ Uri.tryFrom(text)?     ──→ a Uri, or a UriError that names the text
  a Path           ──→ Uri.tryFrom(path)?     ──→ a file: URI, and Path.tryFrom(uri)? is the way back

  a Uri ──→ show() ──→ the display form: postgres://user:***@host/db
        └─→ text() ──→ all of it:        postgres://user:secret@host/db, and Encode writes this one
```

- **[1. What the probes proved](#1-what-the-probes-proved)** — eight of them, checked and built in the worktree that wrote this
- **[2. What other systems do](#2-what-other-systems-do)** — nine of them, and what is taken from each
- **[3. The type](#3-the-type)** — the capsule, and why the scheme is an `Option`
- **[4. Parsing and normalization](#4-parsing-and-normalization)** — what construction decides and what `normalized()` adds
- **[5. The members](#5-the-members)** — every signature, and `UriError`
- **[6. RFC 3986, not WHATWG](#6-rfc-3986-not-whatwg)** — nine differences, and what a program does about each
- **[7. `Url` and `Urn`](#7-url-and-urn)** — one type, one question and one refinement
- **[8. `Path` and `Uri`](#8-path-and-uri)** — two conversions that are deliberately not a pair
- **[9. A literal adapts to a checked type](#9-a-literal-adapts-to-a-checked-type)** — the language rule, and who is on the list
- **[10. Who takes a `Uri`](#10-who-takes-a-uri)** — every signature, today and proposed
- **[11. Schemes choose drivers](#11-schemes-choose-drivers)** — one trait per capability, a registry that is a value, and where a secret lives
- **[12. `std/identifier`](#12-stdidentifier)** — `Uuid`, `Ulid`, the `Identifier` trait, and where a value comes from
- **[13. What the language must provide](#13-what-the-language-must-provide)** — thirteen gaps, smallest fix each
- **[14. Migration](#14-migration)** — eight slices
- **[15. What this is not](#15-what-this-is-not)**
- **[16. Open](#16-open)**

`std/uri` replaces no code, because there is none: the repository has three hand-written URI readers
(`compiler/src/documentation/links.trb:38` classifies a link target with `startsWith("http://")`,
`std/http`'s `HttpError.InvalidUrl` reports a text the native side could not take apart, and `docs/design/PROJECT.md`'s
`source "...", git: "..."` is a bare string). It is a package of its own and not part of `std/http`, because nothing in
it touches a network.

---

## 1. What the probes proved

`examples/uri-probe` is a package in the workspace: `Uri`, `Authority`, `UriError`, `Urn`, the `Path` bridge and the
driver layer of section 11, with a hand-written RFC 3986 parser. It is type checked by the self-hosted compiler and
**built and run as a native binary**; every table in this document is copied from that run. `examples/uri-scratch` and
`examples/uri-shared-scratch` held the probes that do not check or do not build, and are deleted.

```console
$ build/release/torb check examples/uri-probe
6 files, no problems
$ build/release/torb canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings --rule loops examples/uri-probe
0 of 6 files would change
$ build/release/torb run examples/uri-probe
----- parsing
  ...
```

### Probe 1 — the capsule, with `Show`, `Equals`, `Hash` and `Compare`

The type of section 3 written out, with a parser of about two hundred lines, checked and built. Section 4's table is
its output. It needed nothing the back ends do not have: six private fields, an `Option<Authority>` among them, a
hand-written `compare` over five parts, a `Map<Uri, Int>` key and a `sorted`.

```text
----- collections
  5 texts made 4 keys
  http://example.test/a seen 1
  https://example.test/a seen 2
  https://example.test/a/ seen 1
  https://example.test/b seen 1
```

`https://example.test/a` and `https://EXAMPLE.test/a` are **one key**, because the host is lower cased at
construction. `https://example.test/a` and `https://example.test/a/` are **two**, because a trailing slash is a
different resource to every HTTP server there is.

### Probe 2 — three conversions on one capsule, and which of them is a pair

`Uri` carries `TryFrom<String, UriError>`, `TryFrom<Path, UriError>` and `From<Urn>`, and `String` carries
`From<Uri>`. The capsule rule (`docs/design/ENCODING.md` section 3a) says `Decode` comes from **exactly one** conversion
pair, so the question is whether three conversions are three pairs.

```text
----- encoding
  Uri has Encode: true, Decode: true
  Urn has Encode: true, Decode: true
  Authority is data, so it has both from its fields: true
```

**They are one pair.** A pair needs `Source` to have an infallible `From<Self>`, and neither `Path` nor `Urn` has one:
`Path.tryFrom(uri)` refuses a `mailto:` and `Urn.tryFrom(uri)` refuses anything that is not a `urn:`. So `String` is
`Uri`'s only pair and `Uri` keeps its `Decode`; `Urn`'s own pair is `Uri`, so a URN in a JSON document is its URI
text. Section 8 is the design that falls out of this.

The control, in `examples/uri-scratch`: a capsule with two **real** pairs loses `Decode`, with this message.

```text
error: `Slug` does not implement `Decode`
  --> src/main.trb:68:7
   |
68 | print needsDecode<Slug>()
   |       ^^^^^^^^^^^^^^^^^^^
   = `needsDecode` asks for it
   = `Slug` is a capsule: `value` is private and has no default, so nothing outside can call the constructor
     and `Decode` comes from `Slug`'s one conversion pair instead of from its fields
   = `Slug` converts both ways with `String` and `Title`. Exactly one type may
```

### Probe 3 — a string literal where a capsule is expected

```trb
fn open(locator: Locator): String {
  locator.text()
}

print open("https://example.test/a")
```

```text
error: Expected `Locator`, found `String`
  --> src/main.trb:68:12
   |
68 | print open("https://example.test/a")
   |            ^^^^^^^^^^^^^^^^^^^^^^^^
```

**A literal adapts to nothing**, exactly as probe 1 of `docs/design/RESOURCES.md` found for a resource type — and a
`TryFrom<String, _>` on the target changes nothing about it. There is no partial mechanism to finish: section 9 is
entirely new checker work, and it is the same work `docs/design/RESOURCES.md` slice 1 is.

### Probe 4 — `Into<Uri>` as a parameter type

The signature `fn get(url: Into<Uri>)`, which is what `docs/design/PATH.md` section 5 wanted for `Path`. `Into.into(value)`
— the trait's member reached through the trait — is the one spelling that type checks, and it does not build:

```text
error: a name the back end does not lower yet is not supported by the native back end yet (at src/main.trb:41:27)
internal error: t_std_x2f_core_convert_into__...: b0: argument 0 is `Never` and %0 is `Record(Locator)`
internal error: t_std_x2f_core_convert_from__...: slot %0 has the type `Never`, which has no values
```

So `Into<Uri>` is not available, the interim signature is the concrete `Uri`, and the ergonomics come from section 9's
literal rule instead. This is the same gap `docs/design/PATH.md` section 5 measured, one milestone later and with the
lowering's internal error in addition to the checker's refusal.

### Probe 5 — a `static fn` factory reached through a bound

This one is the surprise, because `docs/design/ENCODING.md` section 13 lists it as missing.

```trb
fn byBound<Value: TryFrom<String, Failure>, Failure>(text: String): Result<Value, Failure> {
  Value.tryFrom text
}
```

```text
$ build/release/torb run examples/uri-scratch
Ok(Locator(value: "https://example.test/a"))
```

**A static trait member reached through a bound type checks and builds natively.** So the generic reader that
section 12's `Identifier` needs is buildable today, and the gap `ENCODING.md` records is narrower than the sentence
that records it — it is about `Into`'s blanket implementation over a type parameter (probe 4), not about static
dispatch through a bound as such.

### Probe 6 — the `Identifier` trait, with a `Uuid` under it

```trb
public trait Identifier with Show, Equals, Hash, Compare, TryFrom<String, IdentifierError> {
  fn bytes(): List<Int>
}
```

```text
Ok(f81d4fae-7dec-11d0-a765-00a0c91e6bf6)
version 1, lead 248, shows as f81d4fae-7dec-11d0-a765-00a0c91e6bf6
Fail(`nope` is not a UUID)
```

A trait with five supertraits, one of them generic, a capsule implementing it, and both a static member
(`Value.tryFrom`) and an instance member (`value.bytes()`) reached through the bound — checked and built.

**The first shape of the trait failed, and the failure is a design decision.** `trait Identifier<Failure>` — the
failure type as a parameter — type checks and then poisons every call site:

```text
error: Cannot infer `Failure` of `leadOf`
   = Annotate the closure parameter or the result, or write the type argument: `leadOf<Failure>(...)`
```

A type parameter that appears only inside a bound has nothing to be inferred from, so every generic function over
`Identifier` would have to be called with both arguments written out. Section 12 therefore fixes the failure type:
`std/identifier` has one `IdentifierError` and the trait names it.

### Probe 7 — a capability trait, three drivers and a registry that is a value

Section 11's design, written out and built: a `Schemes` supertrait, a `Storage` trait of five members, a
`MemoryStorage`, a `FileStorage` over `std/fs` and an `ObjectStorage` that carries a region and credentials, plus
`Storage.registry(drivers)` — a `static fn` on the trait whose result is the trait.

```text
  the registry answers to ["file", "memory", "s3"]
  write memory:/notes/first: Ok(void)
  read  memory:/notes/first: Ok("hello")
  read  webdav://host/x: Fail(no driver for `webdav://host/x`: the registry knows file, memory, s3)
```

Three things the probe settled, each of which changed the design.

1. **A trait as a type is the driver shape.** `List<Storage>` is a list of different driver types, every member is
   object safe, and `Storage.registry(…)` is reachable through the trait name. A `static fn` per driver behind a bound
   (probe 5) builds and cannot make that list; a record of a scheme and a closure builds and says less.
2. **`Map<String, Storage>` is the copy trap.** The first version bound the driver out of the map and wrote into the
   copy: the write answered `Ok(void)` and the read after it answered ``Fail(nothing is stored at `memory:/notes/first`)``,
   with no diagnostic anywhere. What builds reaches through the path instead — `Map<String, Int>` into a
   `var List<Storage>`, and `driverValues[index].write(…)`.
3. **A relative reference has no scheme, so a registry cannot dispatch on it**, and the failure says so rather than
   guessing a base.

### Probe 8 — a display form that redacts, beside a text form that does not

```text
  show postgres://ada:***@db.example.test:5432/orders?sslmode=require
  text postgres://ada:hunter2@db.example.test:5432/orders?sslmode=require
  two passwords, one display form: true
  and they are still two values: false, compare Less
```

`Uri.show()` redacts everything after the first `:` of the user information and `Uri.text()` is the recomposition
with all of it; `String.from(uri)` — the capsule's conversion pair, and therefore `Encode` — calls `text()`. The last
two lines are the reason `compare` had to be moved off `show()`: two references that differ only in their password
have one display form, and a comparison over it would have called them equal.

**Two measurements came out of writing it.** `String.from(uri)` does **not** select `From<Uri>`:

```text
error: `Uri` does not implement `Iterate<Char>`
```

— `String`'s other `From` wins the selection — and `const back: String = uri.into()` type checks and answers *"a
conversion through `From` is not supported by the native back end yet"*. So a capsule's own pair is unreachable in
written-out form when its source type is `String`, and the accessor is what a caller has. Gap 11.

## 2. What other systems do

| System | One type or two | Which spec | Relative references | Path relation | Identifiers |
|---|---|---|---|---|---|
| **Rust** (`url`) | one `Url`, **absolute only** | WHATWG | `base.join(&str)`; a relative `Url` cannot exist | `Url::from_file_path`, `to_file_path`, both fallible | `uuid`, `ulid`, separate crates |
| **Go** (`net/url`) | one `URL`, a struct with public fields | RFC 3986 | `URL.ResolveReference`, `Parse` accepts both | `url.Path` is a string, no `filepath` bridge | `github.com/google/uuid` |
| **Java** | `URI` **and** `URL` | RFC 2396 for `URI` | `URI.resolve`, `URI.relativize` | `Path.of(uri)`, `Path.toUri()` | `java.util.UUID` |
| **.NET** | one `Uri`, with `UriKind` | RFC 3986, with quirks | `new Uri(base, relative)` | `Uri.LocalPath`, `new Uri(path)` | `System.Guid` |
| **WHATWG / browsers** | one `URL` | WHATWG | `new URL(relative, base)`; a relative `URL` cannot exist | none | `crypto.randomUUID()` |
| **Node** | `URL` (WHATWG) plus the legacy `url.parse` | both | as WHATWG | `fileURLToPath`, `pathToFileURL` | `crypto.randomUUID()` |
| **Deno** | `URL`, and `import.meta.url` is the module's identity | WHATWG | as WHATWG | `Deno.readTextFile` takes a `string \| URL` | `crypto.randomUUID()` |
| **Bun** | `URL`; `Bun.file(url)` and `fetch(url)` both take one | WHATWG | as WHATWG | `Bun.fileURLToPath` | `Bun.randomUUIDv7()` |
| **Python** | `urllib.parse.ParseResult`, a named tuple | RFC 3986 | `urljoin` | `Path.as_uri()`, `url2pathname` | `uuid` |
| **Swift** | one `URL`, and it **is** the file API | RFC 3986 (rewritten in 2024) | `URL(string:relativeTo:)` | `URL` is the file path type | `Foundation.UUID` |

**What is taken.**

- **Go's and Python's shape**: a URI reference is a value with parts, relative or absolute, and resolution is a
  member. That is the only shape in the table where every member is total, which is the property `docs/design/PATH.md`
  already bought for `Path` and which section 3 keeps.
- **Bun's and Deno's reach**: a URI is what a program passes around, not a string it re-parses at every boundary.
  Section 10 is how far that goes and where it stops.
- **Rust's and .NET's one type**: there is one `Uri`, and `Url` is a question rather than a type (section 7).
- **Swift's and Node's explicit bridge**: `fileURLToPath` and `pathToFileURL` are two named, fallible functions and
  not an implicit coercion. Section 8 is the same, spelled as `TryFrom`.

**What is left.**

- **Java's `URI`/`URL` split**, which is the famous mistake: `URL` carries a protocol handler, so `URL.equals` does a
  **DNS lookup** and two URLs are equal when their hosts resolve to one address. A type whose `==` touches a network
  is not a value.
- **WHATWG's repair-as-you-parse.** A parser that turns `http:\\a\b` into `http://a/b` and a space into `%20` without
  saying so is a parser that cannot refuse, so a typo becomes a 404 at run time instead of a message at the literal.
  Section 6 says what happens to each difference.
- **.NET's `UriKind`.** A kind argument on a constructor makes the type's invariant depend on a parameter, and then
  half the members throw for half the values (`Uri.Host` on a relative `Uri` is an exception). Section 3 answers the
  same question with an `Option`, which is what `Path.root()` already does.
- **Swift's `URL` as the file API.** It makes every file operation carry a scheme that is always `file:`, and it is
  why `URL.path` and `URL.absoluteString` are two different texts that everybody confuses. `Path` stays its own type
  (section 8).
- **Rust's absolute-only `Url`.** It makes `docs check`'s job — resolve `../guide/x.md` against the page that wrote
  it — impossible in the type, so every consumer keeps a string beside the `Url`.

## 3. The type

```trb
/** The `[userInfo "@"] host [":" port]` of a URI. */
public type Authority with Show, Equals, Hash {
  /** Everything before the `@`, normalized. `None` where the authority has no `@`. */
  userInfo: String?
  /** The registered name or IP literal, lower case. `""` is a value: `file:///x` has an empty host. */
  host: String
  /** The port, where one is written out. It is not the port a scheme defaults to. */
  port: Int?
}

/** A URI reference per RFC 3986: what it names, and where. */
public type Uri with Show, Equals, Hash, Compare {
  private schemeValue: String?
  private authorityValue: Authority?
  private pathValue: String
  private queryValue: String?
  private fragmentValue: String?

  fn scheme(): String?
  fn authority(): Authority?
  fn path(): String
  fn query(): String?
  fn fragment(): String?
}
```

**`Uri` holds a URI *reference*, and whether it has a scheme is a question a member answers.** RFC 3986 calls a
reference with a scheme a URI and one without a relative reference, and both are what a program meets: `docs check`
resolves `../guide/x.md`, an HTML page carries `/a/b`, a manifest carries `https://…`. So the scheme is a `String?`
and `isAbsolute()` is `scheme().isSome()` — which is `Path.root(): Root?` and `Path.isAbsolute()` letter for letter,
and for the same reason: **every member stays total.** .NET's `UriKind` is the alternative, and it is why `Uri.Host`
throws there.

**`Authority` is a type and not three fields of `Uri`**, because its absence is different from an empty one:
`mailto:ada@example.test` has **no** authority and `file:///x` has one whose host is `""`. Two `Option`s cannot say
that; `Option<Authority>` says it once. `Authority` is ordinary data — its three fields are public, it has no
invariant of its own beyond what `Uri.tryFrom` already checked, and a caller that builds one by hand reaches it only
through `Uri`, which is normalized anyway.

**`Uri` is a capsule, so `Uri.tryFrom(text)` is the one way in.** Every field is private and without a default
(`docs/language/types/data-or-capsule.md` rule 1), so nothing outside `std/uri` can hand in a scheme with a slash in
it or a host in upper case. The way out is `text()`, and the two are the conversion pair `Encode` and `Decode` are
derived through — so a URI in a JSON document is its text, which is what every reader of such a document expects.
Probe 2 is the measurement that three conversions still leave exactly one pair.

**The way out and the way it is shown are two members, because a URI can carry a password.** `text()` is the
recomposition with every part of it and `show()` replaces everything after the first `:` of the user information with
`***`; section 11 is the argument, and the consequence for this section is that `compare` is written over `text()`
and not over the display form.

**`Equals`, `Hash` and `Compare` are over what the value holds, which is the normalized form.** There is no
`equalsIgnoringPort`, no case-insensitive mode and no flag: normalization happens once, at the door, and everything
after it is ordinary value equality. What construction does *not* normalize is section 4, and the sentence a caller
needs is short: **to compare two URIs as resources rather than as text, call `normalized()` on both** — the same
sentence `Path` writes for "whether two paths name the same file is a question for the file system".

`compare` orders by scheme, then authority, then path, then query, then fragment, each `None` before every `Some`. It
is hand written, because comparing `text()` would order `https://a/b?c` before `https://a/b/c` — a text order is not a
URI order.

## 4. Parsing and normalization

```trb
extend Uri with TryFrom<String, UriError> {
  static fn tryFrom(value: String): Result<Uri, UriError>
}
```

**`TryFrom` and not `From`**, which is the one place this type differs from `Path`. `Path.from` is infallible because
no file system is guaranteed to reject any text; RFC 3986 *does* reject text, and a `Uri` that could hold
`https//example.test` would make every member a lie. The cost is a `?` where a `String` value crosses the boundary,
and section 9 removes it for the case that matters, which is a literal.

**Construction does RFC 3986 section 6.2.2, syntax-based normalization, and nothing else.** The rule is
`docs/design/PATH.md`'s rule, applied again: **normalize what is a fact, and never guess.**

| What | Fact or guess | Where it happens |
|---|---|---|
| the scheme lower cased | a fact — RFC 3986 section 3.1 says a scheme is case-insensitive | construction |
| the host lower cased | a fact — section 3.2.2 | construction |
| `%7e` becoming `%7E` | a fact — section 6.2.2.1 | construction |
| `%7E` becoming `~` | a fact — section 6.2.2.2, `~` is unreserved | construction |
| `%2F` staying `%2F` | a fact — `/` is a delimiter and the escape is not the same character | construction |
| dot segments in an **absolute** path | a fact — section 5.2.4 is textual, and a URI has no symbolic links | construction |
| dot segments at the front of a **relative** reference | a guess — they mean something only once a base is known | never; `resolved(against:)` does it |
| a non-ASCII character in a path, query or fragment | a fact — RFC 3987 section 3.1 encodes it as UTF-8 | construction |
| a non-ASCII **host** | neither — it needs IDNA, and percent-encoding it is *wrong* | refused (section 6) |
| the port `80` under `http` | a guess about five schemes, not a fact about URIs | `normalized()` |
| an empty path under an authority becoming `/` | the same | `normalized()` |

The last two rows are the trade. `http://example.test:80/a` and `http://example.test/a` are two values, which
surprises; putting a scheme-to-port table inside a construction would mean `std/uri` deciding that *your* scheme has a
default port, and a construction that guesses is the one thing `docs/design/PATH.md` forbade. `normalized()` is one call,
and the five schemes it knows (`http` 80, `https` 443, `ws` 80, `wss` 443, `ftp` 21) are the ones IANA fixes.

**The table, copied from the probe.** `scheme | authority | path | query | fragment`, with `-` for what is absent.

| Input | Parts | Shows as |
|---|---|---|
| `https://example.test/a/b?q=1#top` | `https` \| `example.test` \| `/a/b` \| `q=1` \| `top` | `https://example.test/a/b?q=1#top` |
| `HTTP://Example.TEST:80/a/./b/../c` | `http` \| `example.test:80` \| `/a/c` \| `-` \| `-` | `http://example.test:80/a/c` |
| `https://user:secret@example.test:8443/` | `https` \| `user:***@example.test:8443` \| `/` \| `-` \| `-` | `https://user:***@example.test:8443/`; `text()` has the password |
| `file:///C:/Users/ada/notes.txt` | `file` \| `""` \| `/C:/Users/ada/notes.txt` \| `-` \| `-` | unchanged |
| `file://server/share/x` | `file` \| `server` \| `/share/x` \| `-` \| `-` | unchanged |
| `mailto:ada@example.test` | `mailto` \| `-` \| `ada@example.test` \| `-` \| `-` | unchanged |
| `urn:uuid:f81d4fae-…` | `urn` \| `-` \| `uuid:f81d4fae-…` \| `-` \| `-` | unchanged |
| `git+ssh://git@example.test/project/x.git` | `git+ssh` \| `git@example.test` \| `/project/x.git` \| `-` \| `-` | unchanged |
| `data:text/plain,hello` | `data` \| `-` \| `text/plain,hello` \| `-` \| `-` | unchanged |
| `//example.test/a` | `-` \| `example.test` \| `/a` \| `-` \| `-` | unchanged |
| `/a/b`, `../a/b`, `a/b` | `-` \| `-` \| the path \| `-` \| `-` | unchanged |
| `""` | `-` \| `-` \| `""` \| `-` \| `-` | `""` |
| `?q=1` | `-` \| `-` \| `""` \| `q=1` \| `-` | `?q=1` |
| `#top` | `-` \| `-` \| `""` \| `-` \| `top` | `#top` |
| `https://example.test/a%2Fb/%7euser/%c3%a4` | `https` \| `example.test` \| `/a%2Fb/~user/%C3%A4` \| `-` \| `-` | the normalized text |
| `https://example.test/path with spaces` | `https` \| `example.test` \| `/path%20with%20spaces` \| `-` \| `-` | the encoded text |
| `https://münchen.test/a` | refused | `` `münchen.test` is not an ASCII host `` |
| `https://example.test/a%zz` | refused | a `%` not followed by two hexadecimal digits |
| `https://example.test:http/a` | refused | `` `http` is not a port `` |

Two rows deserve a sentence. `../a/b` keeps its `..`, because a relative reference is resolved and not read. And
`""` is a value — the empty reference, which RFC 3986 section 5.4 resolves to the base itself, and which is what a
bare `#top` on a page means.

**Reference resolution is RFC 3986 section 5.3, byte for byte.** The probe runs the specification's own normal
examples against its own base:

| Reference | Against `http://a/b/c/d;p?q` |
|---|---|
| `g` | `http://a/b/c/g` |
| `./g` | `http://a/b/c/g` |
| `g/` | `http://a/b/c/g/` |
| `/g` | `http://a/g` |
| `//g` | `http://g` |
| `?y` | `http://a/b/c/d;p?y` |
| `#s` | `http://a/b/c/d;p?q#s` |
| `g?y#s` | `http://a/b/c/g?y#s` |
| `;x` | `http://a/b/c/;x` |
| `../g` | `http://a/b/g` |
| `../../g` | `http://a/g` |
| `../../../g` | `http://a/g` |
| `""` | `http://a/b/c/d;p?q` |

## 5. The members

```trb
public type Uri with Show, Equals, Hash, Compare {
  /** The scheme, lower case. `None` for a relative reference. */
  fn scheme(): String?

  /** The authority, where the reference has one. `None` and an empty host are different things. */
  fn authority(): Authority?

  /** The host of the authority, where there is one. */
  fn host(): String?

  /** The port that is written out. It is not the port the scheme defaults to: `normalized()` decides that. */
  fn port(): Int?

  /** The path. Always there, and `""` where the reference has none. */
  fn path(): String

  /** The path split at its separators, with the empty leading segment of an absolute path dropped. */
  fn segments(): List<String>

  /** Everything between the first `?` and the `#`, as RFC 3986 leaves it: one opaque string. */
  fn query(): String?

  /** Everything after the first `#`. */
  fn fragment(): String?

  /** Whether the reference has a scheme. Its opposite is [Uri.isRelative]. */
  fn isAbsolute(): Bool

  /** Whether the reference has no scheme, so that it means something only against a base. */
  fn isRelative(): Bool

  /** Whether the reference has a scheme and an authority: the question `Url` would have been a type for. */
  fn isUrl(): Bool

  /** Whether the scheme is `urn`. [Urn] is what answers the two parts of one. */
  fn isUrn(): Bool

  /** The query read as `application/x-www-form-urlencoded`, which is HTML's convention and not RFC 3986's. */
  fn queryParameters(): Map<String, List<String>>

  /** The first value of a query parameter, which is what almost every caller of [Uri.queryParameters] wants. */
  fn queryParameter(name: String): String?

  /** The same reference with a different query, built from parameters. */
  fn withQueryParameters(parameters: Map<String, List<String>>): Uri

  /** The same reference with its fragment replaced, and with none where `fragment` is `None`. */
  fn withFragment(fragment: String?): Uri

  /** `relative` below this reference's path, the way `Path.joined` works: the root of `relative` is dropped. */
  fn joined(relative: String): Uri

  /** RFC 3986 section 6.2.3: a default port dropped, an empty path under an authority becoming `/`. */
  fn normalized(): Uri

  /** This reference resolved against `base`, per RFC 3986 section 5.3. */
  fn resolved(against: Uri): Result<Uri, UriError>

  /** This URI as seen from `base`: the shortest reference `resolved` turns back into this one. */
  fn relativeTo(base: Uri): Uri?

  /** The canonical text: the recomposition of RFC 3986 section 5.3, with every part of it (section 11). */
  fn text(): String

  /** The same text with a password in the user information replaced by `***`. The display form (section 11). */
  fn show(): String

  /** Scheme, then authority, then path, then query, then fragment, over [Uri.text]. */
  fn compare(other: Uri): Ordering
}

/** What a text is refused for, and what a resolution refuses. */
public type UriError with Show, Error {
  /** The text before the first `:` is not `ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )`. */
  case InvalidScheme(text: String)
  /** A `%` that is not followed by two hexadecimal digits. */
  case InvalidEscape(text: String)
  /** A host that is not ASCII, which needs IDNA (section 6). */
  case NonAsciiHost(host: String)
  /** The text after the `:` of an authority is not a number. */
  case InvalidPort(text: String)
  /** The bytes a percent escape named are not UTF-8, so there is no `String` for them. */
  case NotText(reason: String)
  /** [Uri.resolved] was given a base with no scheme, which RFC 3986 section 5.2.1 requires. */
  case NoBase(base: String)
  /** A `Path` was asked for from a URI whose scheme is not `file` (section 8). */
  case NotAFile(uri: String)
  /** A [Urn] was asked for from a URI that is not one (section 7). */
  case NotAUrn(uri: String)
}
```

**`resolved(against:)` takes the base as its argument and not as its receiver**, so that it reads as
`reference.resolved(against: base)` — the same shape, the same word and the same argument position as
`path.resolved(inside: base)`. `base.resolve(reference)` is the other order and is what most libraries write; one
word for one thing across two packages is worth more than matching a habit.

**No member panics.** What can be absent answers an `Option`, what can be refused answers a
`Result<_, UriError>`, and everything else is total.

| Answers | Members |
|---|---|
| a value | `path`, `segments`, `isAbsolute`, `isRelative`, `isUrl`, `isUrn`, `queryParameters`, `withQueryParameters`, `withFragment`, `joined`, `normalized`, `text`, `show`, `compare` |
| `Option` | `scheme`, `authority`, `host`, `port`, `query`, `fragment`, `queryParameter`, `relativeTo` |
| `Result` | `resolved(against:)` |

**`query()` is one opaque string and `queryParameters()` is a reading of it.** RFC 3986 section 3.4 says a query is
an opaque sequence of characters; `a=1&b=2` is `application/x-www-form-urlencoded`, which is an HTML form convention
that `data:`, `git+ssh:` and half the APIs in the world do not follow. So the raw text is what the type holds and what
`text()` reproduces byte for byte, and the `Map<String, List<String>>` is a member a caller asks for. `List<String>`
and not `String` as the value, because `?tag=a&tag=b` is legal and every API that flattened it has a bug report about
it; `queryParameter(name)` is the first value, which is the ninety-percent call.

## 6. RFC 3986, not WHATWG

**Decided: RFC 3986.** The two specifications disagree, and a language has to pick one and say what happens at each
disagreement.

Three reasons, in order of weight.

1. **WHATWG URL is defined for the web and this type is not.** Its parser branches on a closed list of "special
   schemes" (`http`, `https`, `ws`, `wss`, `ftp`, `file`), and everything else takes a different path through the
   state machine — so `git+ssh://`, `urn:`, `mailto:` and `data:`, which are four of the ten rows in section 4's
   table, are second-class there. `docs/design/PROJECT.md`'s `source "...", git: "..."` is exactly such a scheme.
2. **A parser that repairs cannot refuse.** WHATWG's parser is written to make a browser's address bar work: it
   trims control characters, turns `\` into `/`, percent-encodes a space and lower cases what it feels like. Under
   section 9 a literal is checked at build time, and a checker whose parser repairs finds nothing to report.
3. **WHATWG is a living standard.** Its behaviour has changed several times since 2012. A value type whose `==`
   changes with a specification revision is not a value type, and `docs/design/PROJECT.md` section 8 already built the whole
   toolchain on "two builds of one commit agree".

**What a program meets, and what it does.**

| Input | RFC 3986 (`std/uri`) | WHATWG (Bun, Node, Deno, browsers) | What a TorbScript program does |
|---|---|---|---|
| `http://example.com/a/./b` | `/a/b` | `/a/b` | nothing; they agree |
| `http://example.com/a/../../b` | `/b` | `/b` | nothing |
| `http://example.com:80/` | port `80` kept, `normalized()` drops it | dropped at parse | calls `normalized()` before comparing |
| `http://example.com` | path is `""` | path is `/` | `normalized()` makes it `/` |
| `http:\\example.com\a` | `\` is not a delimiter: the whole tail is a path and is percent-encoded | `\` is read as `/` for a special scheme | gets a different value, and it is the honest one |
| `http://example.com/a b` | the space is encoded as `%20` | the same | nothing |
| `http://example.com/a\u0000b` | the NUL is encoded as `%00` | removed silently | gets a value where a browser drops a byte |
| `  http://example.com  ` | the spaces are part of the reference and are encoded | trimmed | trims it itself, or writes a literal |
| `http://münchen.de` | **refused** (`NonAsciiHost`) | `xn--mnchen-3ya.de` | section 13 gap 1: `std/idna` |
| `file:///C:/x` | path `/C:/x`, host `""` | the same, with drive-letter special cases | nothing |
| `HTTP://A/b` | `http://a/b` | `http://a/b` | nothing |

**Seven of eleven rows agree.** The four that differ are a backslash, a control character, surrounding whitespace and
a non-ASCII host — and in three of them the WHATWG answer is a *repair*, which means the program that wanted it can
ask for one and the program that did not is not surprised.

**The repair is a function and not a mode.** A flag on `tryFrom` would make the capsule's invariant depend on an
argument, which is the `UriKind` mistake at a smaller scale. So what a browser does before parsing is what a caller
can do before parsing:

```trb
/** The repairs the WHATWG URL parser makes before it parses: trimmed, backslashes turned, control characters dropped. */
public fn repaired(text: String): String
```

`Uri.tryFrom(repaired(userInput))?` is the address-bar case, written where a reader can see it. It is **not part of
the first version** — nothing in the repository reads an address bar — and it is named here so that nobody designs
around its absence.

## 7. `Url` and `Urn`

**Decided: `Uri` is the one type. `Url` is a question. `Urn` is a refinement, because it has two members a `Uri`
cannot have.**

**`Url` is `isUrl()`**, which answers whether the reference has a scheme and an authority. It is not a type, for the
reason `docs/design/PATH.md` section 4 gave for `RelativePath`: a `Url` capsule wrapping a `Uri` would have to carry
`scheme`, `authority`, `host`, `port`, `path`, `segments`, `query`, `queryParameters`, `fragment`, `joined`,
`normalized`, `resolved`, `relativeTo`, `show`, `compare`, `Encode` and `Decode` again, or a shared trait to carry
them — seventeen members restated to express a condition that one sentence expresses. And what it would buy is
already there: `uri.host()` answers an `Option`, and a function that needs a host reads it and says what it does when
there is none. **The refinement the type would guarantee is a guarantee one `?` already gives.**

The contrast worth naming is Java, where the split went the other way and produced the language's most quoted bug:
`URL` carries a protocol handler, so `URL.equals` resolves the host through DNS — two URLs are equal when their names
happen to point at one address, and a `HashSet<URL>` makes network calls. Every guarantee `Url` was supposed to add
came with a capability nobody asked for.

**`Urn` is a type**, and it is the one refinement that earns one. A URN's two parts — the namespace identifier and
the namespace-specific string — are not components RFC 3986 has any member for; on a `Uri` they would be
`urnNamespace(): String?` and `urnSpecific(): String?`, two members that answer `None` for every URI that is not a
URN, which is the .NET mistake at a smaller scale.

```trb
/** `urn:<namespace>:<specific>` per RFC 8141, with the namespace identifier lower case. */
public type Urn with Show, Equals, Hash, Compare {
  private uriValue: Uri
  private namespaceValue: String
  private specificValue: String

  /** The URI this name is. */
  fn uri(): Uri

  /** The namespace identifier, lower case: `uuid` in `urn:uuid:…`. */
  fn namespace(): String

  /** The namespace-specific string: everything after the second `:`. */
  fn specific(): String
}

extend Urn with TryFrom<Uri, UriError>
extend Uri with From<Urn>
```

**`Urn` holds the `Uri` it was read from**, so the way back is total and the shape is the asymmetric one of section 8:
`Uri` has an infallible `From<Urn>` and `Urn` has a fallible `TryFrom<Uri, UriError>`. That is one conversion pair
**for `Urn`**, whose `Decode` therefore goes through `Uri` and so through `Uri`'s own text, and **no** pair for `Uri`,
whose `Decode` stays with `String`. Probe 2 measured both.

`urn:uuid:` is the bridge to section 12, and it is the reason `Urn` is in `std/uri` rather than `std/identifier`:
`std/identifier` depends on `std/uri` and not the other way round, so a program that never names an identifier still
gets URNs.

## 8. `Path` and `Uri`

**Decided: `Path` stays its own type, and the two are joined by two fallible conversions that are deliberately not a
pair.**

`Path` is platform-dependent by construction — it has a `Root` with a drive letter and a UNC share in it, it splits on
both separators, and `std/fs` turns it into whatever the operating system reads. A URI has none of that: one
separator, one text form, no drive. Merging them would mean either a `Uri` that knows about drives or a `Path` that
carries a scheme, and Swift's `URL` shows what the second costs — `URL.path` and `URL.absoluteString` are two
different texts and every Swift program has a bug where the wrong one was used.

```trb
extend Uri with TryFrom<Path, UriError>
extend Path with TryFrom<Uri, UriError>
```

**Both directions are `TryFrom`, and the asymmetry is the point.**

- `Path` into `Uri` fails for exactly one input: a UNC share whose server name is not ASCII, which is the same gap
  IDNA closes (section 6). Everything else is total — a drive root becomes the first segment of an absolute path, a
  share root becomes the authority, and every component is percent-encoded as UTF-8.
- `Uri` into `Path` fails for every URI whose scheme is not `file`, for a percent escape whose bytes are not UTF-8,
  and for nothing else. A relative reference becomes a relative path.

**And because neither is a `From`, `Path` is not a second conversion pair of `Uri`.** The capsule rule needs the
source to carry an infallible `From<Self>` (`docs/design/ENCODING.md` section 3a), so two `TryFrom`s make no pair at all and
`Uri`'s `Decode` stays with `String`. Probe 2 measured it. This is a good outcome reached by a thin margin, and
section 13 gap 9 is the part that should be fixed rather than relied on: once `std/idna` lands, `Path` into `Uri`
becomes infallible, `Path` becomes a real pair, and `Uri` **silently loses `Decode`**. A rule where adding a total
conversion is a breaking change needs a way to say which pair is the `Decode` pair.

**The round trip, from the probe:**

| `Path` | `Uri` | back |
|---|---|---|
| `C:/Users/ada/notes.txt` | `file:///C:/Users/ada/notes.txt` | `C:/Users/ada/notes.txt` |
| `/usr/bin/torb` | `file:///usr/bin/torb` | `/usr/bin/torb` |
| `src/main.trb` | `src/main.trb` (a relative reference, no scheme) | `src/main.trb` |
| `//server/share/x` | `file://server/share/x` | `//server/share/x` |
| `a b/ü.txt` | `a%20b/%C3%BC.txt` | `a b/ü.txt` |
| — | `https://example.test/a` | `` Fail(`https://example.test/a` is not a `file:` URI) `` |

All five round trip exactly. The last row is the signature doing its job.

## 9. A literal adapts to a checked type

This is the language rule, and it has three users: `docs/design/RESOURCES.md`'s resource types, `TODO.md`'s `Regex`
("Regex und YAML, 2026-09-22"), and `Uri`.

**The rule.** *A string literal whose expected type is one of a compiler-known set of types is a value of that type,
parsed by the compiler where it is written. Anything that is not a literal is not.*

That is `docs/design/RESOURCES.md` section 3 with four more expected types on the list, and it is the rule CONCEPT already
has for numbers (`const ratio: Float = 1`) and for literal union types (`var status: Status = "online"`). Which
expressions count is `docs/design/RESOURCES.md` section 3's table unchanged — a label changes nothing, a list literal's
elements each count, a default parameter counts and resolves against the *declaring* file, and a `const` initialized
by a literal does **not**.

### The closed list, and why it is closed

The alternative the owner's outline names is the general rule: *a literal adapts to any type with
`TryFrom<String, _>`, checked at build time*. **Decided: the closed list.** Four arguments, the first of which is the
one that decides it.

1. **The general rule needs the checker to run the code it is checking.** `Uri.tryFrom` is a function of the program
   under compilation; evaluating it at check time is constant evaluation of user code, which is the VM and milestone
   7.x (`docs/design/RESOURCES.md` section 8 fences the same thing for a folded initializer, with six conditions). The
   closed list needs none of it, because **its members are standard-library types the compiler may simply import**:
   `compiler/` already depends on `std/fs`, `std/io`, `std/process`, `std/time` and `std/os`, and
   `use Uri from "std/uri"` in the checker is one more line. The checker then calls `Uri.tryFrom(text)` like any other
   function, on stage 0 (which interprets the checker) and in the compiled toolchain (which compiles it). **No new
   capability in either implementation.**
2. **A cycle exists in the general rule and not in the closed one.** A user type whose `tryFrom` calls a function one
   of whose parameters is that same type is a literal that has to be evaluated before it can be evaluated. The closed
   list has no cycle, because `std/uri` is compiled before the program that uses it.
3. **The general rule is silent and acts at a distance.** Adding `TryFrom<String, _>` to a type would change how every
   literal at every call site of every function that takes it is checked, from another file; removing it would
   un-check them with no diagnostic anywhere. A rule that wide has to be written at the type, and "written at the
   type" is an annotation, which CONCEPT does not have.
4. **It contradicts what `docs/design/RESOURCES.md` already decided.** That document says, in section 3: *"There is no
   `Into<Resource>`, no `From<String>` and no coercion"* — the trigger is the **expected type** and never a trait. The
   general rule would make a trait the trigger, and then a resource type would need a `TryFrom<String, _>` that means
   nothing (a resource's check is "does this file exist", not "does this text parse"). The closed list keeps one rule
   for all three users.

**The list, and what each member's build-time check is.**

| Expected type | The check at the literal | What it needs | Available |
|---|---|---|---|
| a literal union type (`"tcp" \| "udp"`) | membership in a structural set | nothing | it ships |
| `Path` | **none** — `From<String>` is infallible, so this is ergonomics only | the parameter kind | with the parameter kind |
| `Resource`, `EmbeddedBytes`, `EmbeddedText` | the file exists, against a directory listing; UTF-8 for the text one | a directory listing per directory, cached | `docs/design/RESOURCES.md` slice 1 |
| `Uri` | `Uri.tryFrom(text)` answers `Ok` | the checker imports `std/uri` | with slice 2 of section 14 |
| `Regex` | the pattern compiles | the checker imports `std/regex` | when `std/regex` exists |
| a user's own `TryFrom<String, _>` type | `Type.tryFrom(text)` answers `Ok` | **the VM**, constant evaluation, and rules 1 to 3 above answered | 7.x, if ever |

**The cost, stated plainly: every type on the list joins the fixpoint.** The compiler is written in TorbScript and
compiles itself, so `std/uri` is code stage 1 type checks, the C back end emits, and the resulting binary runs while
it compiles the next one — exactly what `docs/design/PATH.md` section 8 says about `std/path`. It has to be supported by both
back ends before the literal rule lands, and probe 1 is the evidence that it is.

### The diagnostics

Three messages, modelled on `docs/design/RESOURCES.md` section 3's three.

```text
error: `https//example.test/x` is not a URI
  --> src/main.trb:9:18
   |
 9 | const page = http.get("https//example.test/x").await()?
   |                       ^^^^^^^^^^^^^^^^^^^^^^^
   = A `Uri` literal is parsed where it is written. RFC 3986 wants `scheme ":"`, and there is no `:` before the
     first `/`
```

```text
error: A `Uri` literal is parsed where it is written, so it cannot be interpolated
  --> src/main.trb:12:18
   |
12 | const page = http.get("https://example.test/{name}").await()?
   |                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   = Build one from a base: `base.joined(name)`, or read the whole text at run time with `Uri.tryFrom(text)?`
```

```text
error: Expected `Uri`, found `String`
  --> src/main.trb:15:18
   |
15 | const page = http.get(address).await()?
   |                       ^^^^^^^
   = `address` is a `String`, and its value is only known when the program runs: `Uri.tryFrom(address)?` reads one
     and says what a bad one was
```

The second message is the one that matters most in practice, because interpolating a URL is what everybody does. The
fix it names is a member, not a warning: `base.joined(name)` percent-encodes the segment, which the interpolation
would not have.

### What a non-literal does

Nothing. A `String` value never converts, on its own or through `into()`, and the message above is what it gets. That
is the same answer `docs/design/RESOURCES.md` gives and the same answer literal union types give, and there is exactly one
rule for all of them.

## 10. Who takes a `Uri`

| Where | Today | Proposed | A `String` overload? |
|---|---|---|---|
| `http.get(url)` | `url: String` | `url: Uri` | **no** |
| `http.post(url, body, headers)` | `url: String` | `url: Uri` | **no** |
| `Request.url` | `String` | `Uri` | — |
| `HttpError.InvalidUrl(message)` | a case of the error type | **deleted** | — |
| `File.open/create/readText/writeText/list/…` | `path: String` | `path: Path` (`docs/design/PATH.md` slice 2) | **no** |
| `Sandbox.embedded/load/read` | `path: String` | `EmbeddedText` / `Resource` / `Path` (`docs/design/RESOURCES.md` section 5) | **no** |
| `Storage.read/write/list/delete/exists` | there is no such package | `uri: Uri`, and the scheme picks the driver (section 11) | **no** |
| `Connect.connect(uri)` | the same | `uri: Uri`, a connection string with its credentials in it (section 11) | **no** |
| `Resource.name` | `String` | `String`, unchanged | — |
| `source "acme/x", git: "…"` | a `String` the resolver reads at fetch time | a `Uri`, parsed when the manifest is read | — |
| `source "acme/x", archive: "…"` | the same | the same | — |
| `registry "acme", url: "…"` | the same | the same | — |
| `use X from "https://…"` | a grammar error with its own message | **unchanged** | — |
| `Environment.get("DATABASE_URL")` | `String?` | unchanged | — |
| `docs check`'s link targets | `startsWith("http://")` in `links.trb:38` | `Uri.tryFrom(target)` and `resolved(against:)` | — |

**No `String` overload anywhere, and the language could not offer one if it wanted to** — there is no overloading by
parameter type (mistake 19). The ergonomic case is covered by section 9's literal rule, and a `String` value converts
explicitly:

```trb
use * as http from "std/http"

// A literal: parsed by the compiler, and a typo is a build error
var page = http.get("https://example.test/users").await()?

// A value: one `?`, and the failure says what the text was
const address = Environment.get("SERVICE_URL") ?? "https://example.test"
var other = http.get(Uri.tryFrom(address)?).await()?
```

**`HttpError.InvalidUrl` disappears, and that is the measurable win.** `std/http` has an error case whose only job is
to report a text the native side could not take apart; with a `Uri` parameter there is no such text, so the case has
no producer. One case fewer in an error type every caller matches on is worth more than the `?` it costs.

**`std/fs` keeps `Path` and does not take a `Uri`.** A signature says what a call can do, and `File.open(uri)` would
type check for `https://example.test/x` and fail when the program runs — which is a compile error turned into a
run-time one, and in the one package whose import is the capability statement ("this file touches files"). What Bun
buys with `Bun.file(url)` is bought here by the conversion (`Path.tryFrom(uri)?`) plus the literal rule, which makes
`File.readText("./config.trb")` read exactly as it does in Bun. **What a program that is handed a scheme it does not
know in advance needs is a layer above `std/fs` rather than a wider signature in it**, and that layer is section 11.

**Network imports are not proposed.** `docs/design/PROJECT.md` section 7 decided that source files name packages and
`project.trb` says where each comes from, and section 6 reserves every `scheme:` prefix with a message that points at
the manifest. Nothing here reopens it: a `Uri` in a `source` line is the manifest side of that decision made typed,
and `use X from "https://…"` keeps its grammar error. The argument stands as it did — a URL in a `use` means the type
checker opens sockets — and a `Uri` type does not change it.

## 11. Schemes choose drivers

**Decided by the owner.** *`std/fs` and `File.open` take no `Uri`: `std/fs` is the native driver on `Path`. Above it
sits an abstraction that picks a driver by the URI's scheme and can then also reach S3, WebDAV and the rest; it
consumes `std/fs` for `file:`. And the connection data of a service — a database, a key-value cache, a message queue —
is a URI: `postgres://user:secret@host:5432/db?sslmode=require`, `redis://…`, `s3://bucket/key`, `webdav://…`, and the
scheme is what finds the driver, the protocol or the resource.*

That is the answer to taste question 1, and it is a **layer**, not a wider signature. Nothing below changes: `Path`
stays the type of the file system, `std/fs` stays the import that means "this file touches files", and `Uri` stays a
value that opens nothing.

```text
   the program            const storage = Storage.registry([FileStorage(), S3Storage.of(region, credentials)])
       │                                            │
       │                                 it names its drivers; nothing registers itself
       ▼
   std/storage      trait Storage  ─── read, exists, list, write, delete, each over a Uri
       │                 │
       │                 ├── FileStorage ──→ std/fs ──→ Path        file:
       │                 ├── MemoryStorage                          memory:
       │                 └── a package of its own                   s3:, webdav:, …
       │
   std/uri          trait Schemes ── the one member every registry dispatches on
```

### Two kinds of URI, and therefore two kinds of registry

The sketch this section tests had one registry shape. Writing the drivers out shows there are two, because the two
things a scheme is used for are not the same thing.

| | A **resource** URI | A **service** URI |
|---|---|---|
| Example | `s3://bucket/key`, `file:///etc/hosts`, `webdav://host/a/b` | `postgres://user:secret@host:5432/db`, `redis://host:6379/0` |
| What it names | one addressable thing | one endpoint, with everything behind it |
| When it is read | at **every** call | **once**, when the driver is opened |
| What the call after it carries | the URI again | a key, a statement, a topic — never a URI |
| The registry therefore answers | the capability itself | an *opener* for the capability |

So `Storage` dispatches per call and a registry over storages is itself a `Storage`. `Connection` and `Cache` are
opened once from a URI and then queried by key, so a registry over them is not a connection: it is a `Connect` whose
one member answers one. **Collapsing the two into a universal `open(uri)` is what loses this**, and it is also the
reason `sqlx::AnyPool` is unpleasant to use: it erases the typed query surface the caller came for.

### One trait per capability, and no universal opener

```trb
/** The schemes a driver answers to. It declares none of its own, which is why it lives beside `Uri`. */
public trait Schemes {
  /** The schemes this driver answers to, lower case. */
  fn schemes(): List<String>
}

/** Bytes at a reference, wherever the scheme reaches. */
public shared trait Storage with Schemes {
  /** The bytes stored at `uri`. */
  fn read(uri: Uri): Task<Result<Bytes, StorageFailure>>

  /** Whether anything is stored at `uri`. */
  fn exists(uri: Uri): Task<Result<Bool, StorageFailure>>

  /** The references directly below `uri`, sorted. */
  fn list(uri: Uri): Task<Result<List<Uri>, StorageFailure>>

  /** Stores `content` at `uri`, replacing whatever was there. */
  var fn write(uri: Uri, content: Bytes): Task<Result<Void, StorageFailure>>

  /** Removes whatever is stored at `uri`. */
  var fn delete(uri: Uri): Task<Result<Void, StorageFailure>>

  /** The bytes read as UTF-8, which is the ninety-percent call. */
  fn readText(uri: Uri): Task<Result<String, StorageFailure>> {
    const content = read(uri).outcome()?
    Ok textOf(content).mapError({ problem => StorageFailure.NotText(uri.show(), problem.show()) })?
  }

  /** One storage over several drivers, chosen by `uri.scheme()`. The answer is itself a [Storage]. */
  static fn registry(drivers: List<Storage>): Storage
}

/** What a storage refuses with. `UnknownScheme` is the registry's own, and it lists what it knows. */
public type StorageFailure with Show, Error {
  case UnknownScheme(uri: String, known: List<String>)
  case NotAddressable(uri: String, reason: String)
  case NotFound(uri: String)
  case NotText(uri: String, reason: String)
  case Refused(uri: String, reason: String)
  case Stopped
}
```

**A capability trait and not an opener.** `open(uri): Task<Result<Source<Bytes, …>, …>>` is the shape `fsspec` and
`Bun.file` have, and it answers one question — *give me the bytes* — while an object store is also a `list`, a
`delete` and an `exists`. A trait with five members says what a driver has to be able to do, and a driver that cannot
`list` says so in its `Result` instead of in its absence. The cost is that a driver has five members to write; the
`readText`/`writeText` pair is a default over `read`/`write`, so the cost is five and not seven.

**And the closed set of members is what keeps the import readable.** `use Storage from "std/storage"` means "this
file reads and writes things it does not own"; a universal `open` in a package that reaches the disk *and* the
network means nothing at all. That was the argument against the universal opener, and a capability trait keeps it
while giving the owner what he asked for.

The other two, written out for the shape rather than for `std`:

```trb
/** A connection to a service, and the only three things every driver can promise about one. */
public shared trait Connection with Close {
  /** The reference this connection was opened from, redacted by `show()` (below). */
  fn uri(): Uri

  /** A round trip, which is what a pool and a health check need. */
  fn ping(): Task<Result<Void, ConnectionFailure>>
}

/** How a scheme becomes a connection. The query surface is the driver's own typed API and is not in here. */
public shared trait Connect with Schemes {
  fn connect(uri: Uri): Task<Result<Connection, ConnectionFailure>>

  /** One opener over several, chosen by the scheme of the reference it is handed. */
  static fn registry(drivers: List<Connect>): Connect
}

/** A key-value cache. Its `Uri` was consumed by `Connect`, so its members carry keys. */
public shared trait Cache with Close {
  fn get(key: String): Task<Result<Bytes?, CacheFailure>>

  var fn set(key: String, value: Bytes, expires: Duration? = None): Task<Result<Void, CacheFailure>>

  var fn remove(key: String): Task<Result<Void, CacheFailure>>
}
```

**`Connection` says almost nothing, and that is the honest amount.** A trait that tried to carry a query surface
would have to pick a query language, a row type and a parameter binding — and the whole reason a program reaches for
`acme/postgres` rather than for a generic connection is that `Postgres` has a typed one. So there are two doors and
the document says so: **a program that queries names its driver** (`Postgres.connect(uri)`, whose result is a
`Postgres`), and **a program that only manages the lifetime uses the registry** (`Connect.registry([…]).connect(uri)`,
whose result is a `Connection`). A driver package writes both, and the second is three lines over the first.

### Registration is a value

**There is no discovery of any kind, and there cannot be.** CONCEPT has no reflection, no `eval` and no dynamic
import; a `use` brings in names and runs nothing, and top-level code is never importable, so a package cannot put
itself into a table when somebody imports it. Every mechanism the comparison below lists — a `ServiceLoader`, an
entry point, an `init()` reached by `import _ "…"` — is unavailable, and none of it is missed: **the program names its
drivers, in one expression, in the file where it decides what it can reach.**

```trb
use Storage, FileStorage from "std/storage"
use S3Storage, S3Credentials from "acme/s3"
use Environment from "std/os"

const region = Environment.get("AWS_REGION") ?? "eu-central-1"
const storage = Storage.registry([FileStorage(), S3Storage.of(region, S3Credentials.fromEnvironment()?)])

const page = storage.readText("s3://reports/2026-09/summary.md").outcome()?
```

**An unknown scheme is a `Result` and the message lists what is known.** From the probe, run as a native binary:

```text
  the registry answers to ["file", "memory", "s3"]
  write memory:/notes/first: Ok(void)
  read  memory:/notes/first: Ok("hello")
  read  s3://bucket/key: Fail(`s3://bucket/key` was refused: bucket "bucket" in "eu-central-1" as "AKIA", and this probe has no network)
  read  webdav://host/x: Fail(no driver for `webdav://host/x`: the registry knows file, memory, s3)
  read  ./notes/first: Fail(no driver for `./notes/first`: the registry knows file, memory, s3)
  read  file:///C:/…/project.trb: "name \"torbscript/torbscript\""
```

The fourth line is the one a caller learns from: `webdav:` is a scheme the *program* did not build in, and the failure
says which ones it did. The fifth is the other half of the same rule — **a relative reference has no scheme, so a
registry cannot dispatch on it.** A program that reads configuration resolves against a base first
(`reference.resolved(against: base)?`), which is exactly what section 4 says a relative reference is for.

### The driver shape that builds, measured

Three shapes were probed for "a driver as a value".

| Shape | Type checks | Builds natively | Why it is not the answer |
|---|---|---|---|
| a `static fn` per driver type, reached through a bound (`Driver.open` under `Value: Driver`) | **yes**, probe 5 | **yes** | a bound is resolved at the call, so a `List` of different driver types cannot exist — and a registry is exactly that list |
| a record of a scheme and a factory closure (`Driver(scheme, open: (uri: Uri) => …)`) | yes | yes | it works, and it says less than a trait: a closure cannot carry `list`, `delete` and `exists` without becoming five closures |
| **a trait used as a type**, with `Schemes` as its supertrait | **yes** | **yes** | this is it |

**A trait as a type is the shape, because rule 9 of `traits` makes `List<Storage>` a list one builds** and rule 10
checks object safety per call: `schemes`, `read` and the rest mention no `Self`, so every one of them is callable on a
trait-typed value, and `Storage.registry` is `static` and therefore reached through the trait name only — which is
where it is written anyway.

**And the registry's own table is where value semantics bite.** The first probe held `Map<String, Storage>` and bound
the driver out of it:

```trb
var driver = driverOf(uri)?
driver.write(uri, text)
```

```text
  write memory:/notes/first: Ok(void)
  read  memory:/notes/first: Fail(nothing is stored at `memory:/notes/first`)
```

**The write landed in a copy, and nothing in the language reported it** — the result of `write` is returned, so the
dead-change check has nothing to say. That is mistake 6 of `mistakes-models-make`, met in the one place a registry
cannot avoid it. There are two answers and the document takes both, one per era:

- **What builds today**: the drivers are a `var List<Storage>` and the table is a `Map<String, Int>` into it, so every
  member reaches through the path — `driverValues[index].write(uri, content)` — and nothing is ever bound to a local.
  The probe does this and the run above is its output.
- **What the design is**: `Storage` is a **`shared trait`** and a driver is a `shared type`, the way a `File` is. Then
  the drivers have identity, `Map<String, Storage>` aliases instead of copying, and the index table is unnecessary.
  This is forced anyway by the asynchronous form (below), and it does not build yet (gap 10).

### Where configuration lives, and the rule

A driver needs a region, an endpoint override, a timeout and credentials; a reference needs to stay something a
manifest can hold and a log can print. The rule is one sentence: **the URI names the resource, and the driver value
holds everything that is not the resource — except where the URI *is* the configuration, which is what a connection
string is.**

| What | Where it belongs | Why |
|---|---|---|
| `s3://bucket/key`, `file:///etc/hosts` | the URI, at every call | it is the name of the thing, and two programs must agree on it |
| an AWS region, an endpoint override, a retry count, a timeout | the driver value | it is a property of *this program's* access, not of the resource |
| the credentials of an object store | the driver value, read from the environment where the registry is built | so that `Show` of a reference is safe, and `grep` finds the one place a secret enters |
| `postgres://user:secret@host:5432/db?sslmode=require` | the URI, once, at `connect` | a connection string is a single configuration value by convention everywhere, and splitting it would mean re-inventing .NET's `Server=…;` |
| `?sslmode=require`, `?pool_max_conns=10` | the URI's query, read by the driver | it arrived with the connection string; the driver's `connect` is where it is interpreted |

**So `userInfo` carries a secret for the service case and never for the resource case**, and that is not an
inconsistency: a connection string is handed to the program as one value by an operator, and a resource reference is
written by the program itself. What the rule buys is that `s3://bucket/key` can be logged, compared and put in a
manifest, and `postgres://…` cannot — which is the next part.

### Secrets in a URI

**Decided: `show()` redacts and `text()` is the whole thing.** A `Uri` has two ways out, they are named so that the
unsafe one is greppable, and the display form is the default because a display form is what interpolation, `print`,
`describe` and a failing `assert` reach for.

```trb
public type Uri with Show, Equals, Hash, Compare {
  /** The canonical text, with every part of it. A password in the user information is in this text. */
  fn text(): String

  /** The canonical text, with everything after the first `:` of the user information replaced by `***`. */
  fn show(): String
}
```

From the probe, built and run:

```text
  show postgres://ada:***@db.example.test:5432/orders?sslmode=require
  text postgres://ada:hunter2@db.example.test:5432/orders?sslmode=require
  show redis://:***@cache.example.test:6379/0
  text redis://:hunter2@cache.example.test:6379/0
  show https://ada@example.test/a
  text https://ada@example.test/a
  two passwords, one display form: true
  and they are still two values: false, compare Less
```

- **A user information without a `:` is a name and is not a secret.** `https://ada@example.test/a` is unchanged;
  RFC 3986 section 3.2.1 deprecates the `user:password` form, and the `:` is exactly what marks it.
- **`Encode` writes the full form**, because `Encode` is data and not display. A configuration that round trips
  through a JSON document without its password is a configuration that stopped working, and the capsule's pair
  (`String.from(uri)`, section 3) therefore calls `text()`. The consequence is stated rather than hidden: **a `Uri`
  encoded into a log line, a trace or a crash report carries its password**, and a program that encodes one is doing
  the same thing as a program that encodes a password field, which no language redacts for it either.
- **Redaction touches neither `Equals`, `Hash` nor `Compare`.** `Equals` and `Hash` are generated over the fields, so
  they never saw a display form; `compare` is hand written and had to be moved onto `text()`, and the last two lines
  of the probe are what a `compare` over `show()` would have broken — two references that differ only in the password
  would have compared `Equal` and one of them would have vanished out of a `sorted`.
- **A failure carries the display form.** `StorageFailure.UnknownScheme(uri.show(), known)` and every other case take
  a `String` that `show()` produced, so a connection failure that reaches a log, a crash report or a user is redacted
  by construction and not by a rule somebody has to remember. That is the one place the default matters most, and it
  is why the redacting member is the one named `show`.
- **The two names are a rule for every capsule with a secret, not a special case for `Uri`**:
  `docs/language/types/data-or-capsule.md` rule 4 already says "a capsule with a secret writes its own `Show`", and
  this is what writing one looks like when the value still has to be recoverable.

### Asynchronous, per `docs/design/CONCURRENCY.md`

Every driver operation reaches the outside world, so every one of them answers a `Task<Result<…, Failure>>` and every
one of them is cancellable at its suspension points. That is CONCURRENCY section 8 applied without an exception:
`Failure: From<Cancelled>` is the floor, `StorageFailure.Stopped` is what `From<Cancelled>` produces, and a caller
writes `storage.read(uri).outcome()?` — `await()` plus the conversion the `?` would have applied anyway.

**What runs today is the synchronous form**, and the reason is `std/fs`: `File.readText`, `File.writeText`,
`File.exists` and `File.list` are synchronous natives, and only the streaming side (`chunks`, `fill`) is a `Task`. So
`FileStorage` and `MemoryStorage` — the two drivers that exist before milestone 7 — have nothing to wait for, and the
probe is synchronous throughout and builds as a native binary.

**The asynchronous form type checks and does not build, for two independent reasons**, both measured in a scratch
package that is deleted:

```text
error: `write` changes `self` and answers a `Task`, and `Storage` is not a `shared trait`
   = An ordinary trait can be implemented by a value, and a value cannot be changed by a task: write `shared trait Storage`
```

```text
error: `Task.await` is not supported by the native back end yet: the runtime does not provide it
error: a body whose result is a `Task` is not supported by the native back end yet
error: a counted field that is a `shared type` object is not supported by the native back end yet
error: making a value of this type unique is not supported by the native back end yet
```

The first message is a design input and not a defect: **a writing member that answers a `Task` forces the whole
capability onto a `shared trait`**, which forces every driver to be a `shared type`, which is why the trait is written
`shared` above. The rest is the native back end, which lowers neither a `Task` nor a user-written `shared type` — so
the asynchronous layer type checks and runs nowhere until milestone 7.3 (stage 0, which ran it when this was written,
has been deleted). `std/storage` therefore lands **after** CONCURRENCY's
cancellation slice rather than before it: a capability trait whose signatures change from `Result` to
`Task<Result<…>>` is the one change an ecosystem of third-party drivers cannot absorb, and there is no consumer
waiting.

### The package cut

**The rule: a capability trait belongs in `std` when `std` ships at least two drivers for it, one of which reaches
the outside world.** A trait with no implementation in its own package is a specification nobody has run, and a trait
with one is a specification nobody has compared.

| Package | What is in it | Why there |
|---|---|---|
| `std/uri` | `Schemes` | it is about URIs, it declares no scheme, and it opens nothing — so a database package can depend on it without depending on a storage package |
| `std/storage` | `Storage`, `StorageFailure`, `FileStorage`, `MemoryStorage`, `Storage.registry` | `file:` reaches the outside world and `memory:` is the double every test of a driver needs. It depends on `std/fs`, `std/uri`, `std/stream` and `std/task` |
| `acme/s3`, `acme/webdav`, `acme/gcs` | `type S3Storage with Storage` and the rest | coherence (rule 6 of `traits`): the package owns its own type, so `with Storage` is legal wherever the trait is visible |
| `acme/postgres`, `acme/redis` | `Postgres` with its typed query surface, `PostgresConnect with Connect` | `std` has no database driver and no network cache, so `Connection`, `Connect` and `Cache` are **not** in `std`. They are written out above so that the ecosystem converges on one spelling |

**`Cache` in `std` was considered and refused.** `std` could write a `MemoryCache` in a hundred lines over a `Map` and
an `Instant` — but a memory cache with no `redis:` sibling is a test double for nothing, which is the difference
between it and `MemoryStorage`. It is taste question 8.

### Where `Resource`, `Sandbox` and the manifest's sources sit

**Separate, and the dividing line is who owns the list of schemes.**

| | `Storage`'s registry | `Resource` / `Sandbox.load` | `source "…", git:/path:/archive:` |
|---|---|---|---|
| Who names the drivers | the program, in an expression | nobody: there is no scheme | the toolchain, in a closed list |
| When | while it runs | at build time | while the manifest is read |
| Reached by | `uri.scheme()` | a project-relative literal the compiler resolved | the label, and the value is a `Uri` (section 10) |
| Extensible by a package | **yes** | no | **no**, deliberately |

- **`Resource` is not in this pattern at all.** `docs/design/RESOURCES.md` section 6 says the stable name is project-relative
  text the build resolved: there is no `resource:` scheme and no run-time lookup by string, and that document's "Not a
  virtual file system" is untouched. A resource is a file the *author* chose at compile time; a `Storage` reference is
  one the program is handed while it runs. Merging them would put a compile-time guarantee behind a run-time table.
- **The manifest's sources use the same *idea* and must keep a closed list.** `git:`, `path:` and `archive:` are
  schemes that pick a fetcher, which is exactly this section's picture — and the fetchers may not be a value a package
  contributes, because `docs/design/PROJECT.md` section 8 requires that two builds of one commit agree. A build whose result
  depended on which fetcher the *builder* happened to have installed is the failure mode every plug-in-based build
  system has. So the toolchain validates the scheme with `Uri` and dispatches on a list it owns.
- **`Sandbox` stays on `Path`, `Resource` and `EmbeddedText`** (`docs/design/RESOURCES.md` section 5), for the reason
  `File.open` does. A script fetched through a `Storage` has no member to hand it to, which is gap 13.

### What other systems do

| System | One capability, many drivers | How a driver becomes reachable | Where configuration lives | What the URL buys |
|---|---|---|---|---|
| **JDBC** | `Connection`, `DataSource` | `ServiceLoader` over `META-INF/services` on the class path, and historically `Class.forName` | the URL's query, or a `Properties` | `jdbc:postgresql://host/db` picks the driver |
| **Go `database/sql`** | `driver.Driver` | `sql.Register(name, driver)` in the package's `init()`, triggered by `import _ "github.com/lib/pq"` | a DSN whose grammar is the driver's own | nothing: `sql.Open("postgres", dsn)` takes a *name* and a string |
| **.NET** | `DbProviderFactory` | `DbProviderFactories.RegisterFactory`, plus configuration files | `Server=…;Database=…;User Id=…;Password=…` | nothing: a connection string is not a URI |
| **Rust `sqlx`** | `Any`, `AnyPool` | cargo feature flags plus `install_default_drivers()` | the URL | `postgres://…` picks the back end — and `Any` erases the typed query API |
| **Bun** | none; one class per service | none | the client object (`new Bun.S3Client({…})`), credentials from the environment | `Bun.s3.file("s3://bucket/key")`, `Bun.sql(url)`, `Bun.file(url)` |
| **Python `fsspec`** | `AbstractFileSystem` | a process-global registry plus `importlib` entry points | `**storage_options`, untyped keyword arguments | `fsspec.open("s3://bucket/key")` — the universal opener |
| **Apache OpenDAL** | `Access`, behind one `Operator` | a `Scheme` enum in the core crate | a `HashMap<String, String>` | `Operator::via_iter(Scheme::S3, map)` |

**What is taken.**

- **The whole idea, from `fsspec` and OpenDAL**: one vocabulary over many back ends, with the scheme as the
  discriminator. `fsspec` is the closest thing to the owner's picture and it is right about the important half — that
  `s3://`, `file://` and `memory://` should be one API — and `AbstractFileSystem` is a capability with `ls`, `rm` and
  `exists` on it rather than an opener, which is section 11's trait.
- **Bun's placement of configuration**: the client holds the region and the credentials, the URL holds the resource.
  That is the rule above, and it is what makes `s3://bucket/key` a value a manifest can hold.
- **JDBC's and OpenDAL's scheme-picks-driver**, which is the owner's sentence and the reason this section exists.
- **`sqlx`'s warning rather than its design**: `Any` shows what erasing a query surface costs, which is why
  `Connection` promises `close`, `ping` and `uri` and nothing more.

**What is left.**

- **Every discovery mechanism there is.** `ServiceLoader`, entry points and `import _ "…"`-for-its-`init()` are three
  spellings of one thing: a table that fills itself from what happens to be on the path. TorbScript cannot do it
  (nothing runs on import) and would not want to: the Go form in particular makes the line that gives a program a
  capability an import with an underscore in front of it, which is the least readable place it could be.
- **`fsspec`'s global registry and its untyped options.** A process-global table means two libraries in one program
  can disagree about what `s3://` is, and `**storage_options` means a typo in a credential key is a run-time surprise.
  A registry that is a value has neither problem: it is scoped to whoever holds it, and a driver is a typed value with
  fields.
- **OpenDAL's `Scheme` enum.** A closed enum in the core crate means a service that the core does not know cannot be
  addressed, so every new back end is a change to the central package. `Schemes` answers a `List<String>` the driver
  itself names, so `acme/webdav` needs nothing from `std`.
- **.NET's connection strings.** A second ad-hoc grammar per provider, with no parser anybody shares, no comparison
  and no `Show`. The owner's decision is the opposite of this one, and it is the right way round.
- **A universal `open(uri)`.** Section 10's argument survives the decision: a function that reads a disk *or* a
  network is a function whose import says nothing. What replaces it is not three lines over `uri.scheme()` any more —
  it is a named capability with a named set of members, and the import says which capability.

## 12. `std/identifier`

**Not in the prelude.** It is a package a program names when it makes identifiers, the way `std/fs` is a package a
program names when it touches files.

```trb
/** What every identifier can do. */
public trait Identifier with Show, Equals, Hash, Compare, TryFrom<String, IdentifierError> {
  /** The identifier as its sixteen bytes, most significant first. */
  fn bytes(): List<UInt8>
}

/** A UUID per RFC 9562: sixteen bytes, shown as the canonical thirty-six lower-case characters. */
public type Uuid with Identifier {
  private value: Array<UInt8, 16>

  /** The version nibble: `4` for a random one, `7` for a time-ordered one. */
  fn version(): Int

  /** A version 4: 122 bits out of `source`. */
  static fn version4(var source: Random): Uuid

  /** A version 7: 48 bits of milliseconds since the epoch, then 74 bits out of `source`. */
  static fn version7(var source: Random, at: Instant): Uuid
}

/** A ULID: 128 bits, shown as twenty-six characters of Crockford base 32, ordered by time as text. */
public type Ulid with Identifier {
  private value: Array<UInt8, 16>

  /** The millisecond the identifier carries. */
  fn at(): Instant

  /**
   * A ULID for `at`. Where `after` is a ULID of the same millisecond, its random part is incremented instead of
   * drawn, which is what the specification calls monotonicity.
   */
  static fn generated(var source: Random, at: Instant, after: Ulid? = None): Ulid
}

/** What a text that is not an identifier is refused with. */
public type IdentifierError with Show, Error {
  /** The text that was refused. */
  text: String
  /** What was expected of it, e.g. `a UUID`. */
  expected: String
}

/** `urn:uuid:f81d4fae-…`, the bridge to `std/uri`. */
extend Uri with From<Uuid>
extend Uuid with TryFrom<Urn, IdentifierError>
```

**`Identifier` fixes its failure type, and probe 6 is why.** `trait Identifier<Failure>` type checks and then cannot
be called: a type parameter that appears only inside a bound has nothing to be inferred from, so
`leadOf(identifier)` answers ``Cannot infer `Failure` of `leadOf` `` and every generic function over the trait needs
both arguments written out. One `IdentifierError` for the package costs nothing — the two implementations refuse text
for the same two reasons, a wrong length and a character that is not in the alphabet.

**The trait's name is `Identifier` and not `Uid`.** Full words, no abbreviations; `Uid` is also ambiguous (a POSIX
user id is a `uid`). It is taste question 3.

**Generation takes its randomness and its clock as arguments, and `std/identifier` has no capability at all.**

```trb
var source = Random.seeded 42
const stable = Uuid.version7(source, at: Instant.epoch)
```

That is the whole answer to "what does a sandbox do". A sandboxed script has no clock and no randomness unless it was
handed some, and this design needs no rule for that, because there is nothing to grant: a script that has a `Random`
and an `Instant` can make identifiers and one that does not cannot, and the two arguments are visible at the call. It
also makes a test reproducible without a mock, and it keeps `std/identifier` a pure package that the prelude could
hold if it ever wanted to.

**There is no `std/random`.** `std/` has twenty-five packages and none of them is one; `TODO.md` line 1364 plans it
("seedbar und TEILBAR, ein Wert wie JAX-Keys — reproduzierbares Training"), and that shape — a splittable value, not a
global generator — is exactly what the signatures above want. `std/identifier` is blocked on it and on nothing else:
`Instant` and `Clock` are in `std/time` today.

**Recommended default: UUID version 7, not ULID.** Both are 128 bits and both are time-ordered; the difference is
what surrounds them.

| | UUIDv7 | ULID |
|---|---|---|
| Specification | RFC 9562 (2024), IETF | a README in a GitHub repository |
| Text | 36 characters, lower-case hexadecimal with dashes | 26 characters, Crockford base 32 |
| Sorts as text | no — the dashes and the version nibble break it | yes |
| Accepted by | every `uuid` column, every language's `UUID`, every log, every tracer | what you wrote yourself |
| A URN for it | `urn:uuid:` (RFC 4122) | none |
| Bits of entropy per millisecond | 74 | 80 |

**The two advantages ULID has are about its text form**, and a UUIDv7's sixteen bytes can be printed in Crockford
base 32 by anybody who wants them sorted as text. The advantage UUIDv7 has — that a Postgres `uuid` column, a Java
`UUID`, a `urn:uuid:` and every tracing header already accept it — cannot be recovered any other way. `Ulid` stays in
the package, because a program that must talk to a system built on ULIDs needs to read and write them, and it is
taste question 4.

**`urn:uuid:` is the bridge and it keeps section 8's asymmetry**: `Uri` has an infallible `From<Uuid>` and `Uuid` has
a fallible `TryFrom<Urn, IdentifierError>`, so `Uuid`'s one conversion pair stays `String` and its `Decode` is the
canonical thirty-six characters.

## 13. What the language must provide

Thirteen gaps, each measured by a probe above or by a run in this worktree, with the smallest fix that closes it.
Gaps 1 to 9 are the type; gaps 10 to 13 are the driver layer of section 11.

1. **`std/idna` does not exist, so a non-ASCII host is refused.** `Uri.tryFrom("https://münchen.test/a")` answers
   `NonAsciiHost`. *Smallest fix:* a package with Punycode (RFC 3492) and UTS #46 mapping, about three hundred lines
   plus a table. It is a slice of its own (section 14, slice 7) and it is what makes `Path` into `Uri` infallible,
   which is gap 9's trigger.
2. **A string literal adapts to nothing.** Probe 3. *Smallest fix:* the parameter kind of `docs/design/RESOURCES.md`
   slice 1, with the closed list of section 9 instead of three resource types.
3. **`Into<Uri>` as a parameter type type checks and does not build.** Probe 4, with two internal errors in the
   lowering. *Smallest fix:* the checker condition of `docs/design/PATH.md` section 5 (let a receiver whose own type is
   `Into<Target>` resolve `into` through its witness), and then a lowering for it. The interim signature is the
   concrete `Uri`, which costs nothing once gap 2 is closed. **The back-end half is closed:** both spellings of probe 4,
   `url.into()` and `Into.into(url)`, build natively, and a `Uri` handed in as itself goes through the reflexive
   `From<Uri>`, which the back end generates (`tests/conformance/conversions.trb`).
4. **A derived `encode` is not compiled by the native back end.** `Json.encode(uri)` type checks and answers *"a
   derived `encode`, whose `Encoder` is all `var self` members is not supported by the native back end yet"*. So the
   capsule's `Encode`/`Decode` exist in the checker and not in a binary. *Smallest fix:* it is the encoding redesign
   (`docs/design/ENCODING.md` section 14), and `std/uri` depends on it for `Decode` and on nothing else. **Closed:** the
   derived forms are built per (type, format) natively (`ir/lower/encoding.trb`), and a capsule's go through its pair
   (`tests/conformance/encoding-describe.trb`).
5. **`std/number` has no narrowing conversion into `UInt8`.** `Int8.tryFrom(Int64)` and `Int32.tryFrom(Int64)` exist,
   `UInt8`, `UInt16`, `UInt32`, `UInt64`, `Int16` do not — so a package that produces `Bytes` (which is
   `List<UInt8>`) cannot make one from a number. The one way around it, `UInt8.tryFrom("{value}")`, type checks and
   answers *"`torb_parse_u64`: parameter 2 of the runtime is a `uint64_t *` where the lowering passes a `uint8_t *`"*.
   *Smallest fix:* five `extend … with TryFrom<Int64, NumberRangeError>` in `std/number`, one per type that lacks
   one. The probe carries its own UTF-8 decoder over `Int` because of this. **Closed:** the five exist and build
   natively, over the runtime's checked conversions (`tests/conformance/narrowing.trb`).
6. **`Char` has no ASCII predicate.** `isLetter()` and `isDigit()` are Unicode-wide, so `'ä'.isLetter()` is `true` —
   which is wrong for a scheme, for `unreserved` and for a hexadecimal digit. Every such test in the probe is written
   as a code-point range. *Smallest fix:* `isAscii()`, `isAsciiLetter()`, `isAsciiDigit()` and `hexadecimalValue()`
   on `Char` in `std/text`.
7. **A `fn main()` in a package's entry module is not emitted.** Writing the probe's top-level code as
   `fn main() { … }` builds C that calls a function the emitter never wrote: *"implicit declaration of function
   `t_…_main`"*, and the build fails inside the C compiler rather than with a diagnostic. No example in the
   repository has one, so it is unmeasured ground. *Smallest fix:* either emit it or refuse it at the checker with a
   message that names top-level code.
8. **A type parameter that appears only inside a bound cannot be inferred.** Probe 6:
   `trait Identifier<Failure>` makes `leadOf(value)` answer ``Cannot infer `Failure` ``. *Smallest fix:* none is
   proposed — the design avoids it by fixing the failure type — but it is worth recording, because it is what decides
   the shape of every trait that carries a `TryFrom`.
9. **A capsule cannot say which of its conversion pairs is the `Decode` pair.** Section 8: the design has exactly one
   pair by a margin, and closing gap 1 makes `Path` into `Uri` infallible and so makes `Path` a second pair — at
   which point `Uri` **silently** loses `Decode`, with a message at whoever asked for it and nothing at the line that
   caused it. *Smallest fix:* a rule that the pair with `String` wins where there are several (cheap, and it is right
   in every case in the repository), or a way to name the pair.
10. **A writing member that answers a `Task` forces a `shared trait`, and a user-written `shared type` does not
    build.** The two halves are one gap because they are met together. The checker is explicit and is arguably right:

    ```text
    error: `write` changes `self` and answers a `Task`, and `Storage` is not a `shared trait`
       = An ordinary trait can be implemented by a value, and a value cannot be changed by a task: write `shared trait Storage`
    ```

    So section 11's asynchronous `Storage` is a `shared trait` and every driver is a `shared type` — and the native
    back end lowers neither ``a counted field that is a `shared type` object`` nor ``making a value of this type
    unique``, both of which it says out loud with no location. *Smallest fix:* lower a user-written `shared type`.
    Until then the driver layer exists as the synchronous form probe 7 builds, and `std/storage` cannot land.
11. **A capsule's conversion pair is unreachable when its source type is `String`.** `String.from(uri)` answers
    ``Uri` does not implement `Iterate<Char>`` because `String`'s other `From` wins the selection, and
    `const back: String = uri.into()` type checks and does not lower (gap 3). Probe 8. *Smallest fix:* prefer an
    exact `From<Source>` over one reached by coercing the argument to a trait type; it costs nothing, because the
    exact one is strictly more specific.
12. **`Task` is not compiled by the native back end at all.** *"a body whose result is a `Task` is not supported by
    the native back end yet"* and *"`Task.await` is not supported by the native back end yet: the runtime does not
    provide it"*. Every asynchronous signature in section 11 therefore type checks and runs nowhere until milestone
    7.3 (it ran on stage 0 when this was written; stage 0 has been deleted).
    *Smallest fix:* it is `docs/design/CONCURRENCY.md`'s own work and not this document's; it is recorded because the driver
    layer is the first design that is asynchronous end to end.
13. **`std/fs` has no `remove`, and `Sandbox` reads only a `Path`.** `FileStorage.delete` has no native to call, and
    a script a program fetched through a `Storage` cannot be handed to `Sandbox` without being written to a file
    first. *Smallest fix:* `File.remove(path)` and `File.rename(from, to)` in `std/fs`; the `Sandbox` half belongs to
    `docs/design/RESOURCES.md` and is named here so that it is not discovered twice.

## 14. Migration

Eight slices. Each one lands with the repository checking green, `torb test` passing, `canon --check` clean and the
conformance suite comparing the two implementations. Slices 1, 3 and 7 need nothing that does not exist, and slice 8
is the only one that waits on another document.

**`std/uri` is part of the fixpoint from slice 2**, because that is where the checker imports it (section 9). Until
then it is an ordinary package nothing in `compiler/` depends on.

| # | Slice | Files | Risk |
|---|-------|-------|------|
| 1 | **The package.** `Uri`, `Authority`, `UriError`, `Urn`, the parser, normalization, `resolved`/`relativeTo`, `Show`/`Equals`/`Hash`/`Compare`, `queryParameters`; the `Path` bridge; `std/text` gains the ASCII predicates of gap 6 and `std/number` the narrowing conversions of gap 5 | `std/uri/*`, `std/text/src/*`, `std/number/src/*`, `std/prelude/src/lib.trb` | **Low.** Probe 1 is this package, written out and built. `Decode` waits for gap 4, which is the encoding redesign, and nothing else in the slice does |
| 2 | **The literal rule.** The parameter kind in the checker, the closed list with `Path`, `Uri` and the resource types, the three diagnostics, the recorded value in the IR; `compiler/` depends on `std/uri` | `compiler/src/semantics/checker/{expression,call}.trb`, `compiler/src/ir/*`, `compiler/project.trb`, `compiler/tests/check.test.trb` | **Highest of the eight.** It is a new parameter kind, probe 3 says there is nothing to build on, and it is the slice that puts `std/uri` in the fixpoint. It is the same work as `docs/design/RESOURCES.md` slice 1 and should be one round with it |
| 3 | **`std/http`.** `get`, `post`, `request` and `Request.url` take a `Uri`; `HttpError.InvalidUrl` is deleted; the native side receives the canonical text | `std/http/src/lib.trb`, `compiler/src/backend/c/natives.trb`, `runtime/*` | **Low.** Three signatures and one error case, and every call site in the repository passes a literal |
| 4 | **The manifest.** `source "...", git:/archive:/path:` and `registry "...", url:` are read as `Uri`s when the manifest is evaluated, so a typo is a manifest error and not a fetch failure; the lock records the canonical text | `compiler/src/project/*`, `docs/design/PROJECT.md` section 7 | **Low**, and it depends on `docs/design/PROJECT.md` slices 1 to 4 having landed |
| 5 | **The documentation tooling.** `links.trb`'s four `startsWith` tests become `Uri.tryFrom(target)` and `resolved(against:)`, so an anchor, a relative link and an external link are told apart by the type | `compiler/src/documentation/links.trb` | **Medium.** The documentation gates compare generated text, so a changed classification changes output; the four cases have to answer exactly what they answer today |
| 6 | **`std/identifier`.** `Identifier`, `Uuid`, `Ulid`, `IdentifierError`, the `urn:uuid:` bridge | `std/identifier/*`, and `std/random`, which has to exist first | **Blocked** on `std/random` (`TODO.md` line 1364). Everything else in it is probe 6, which builds |
| 7 | **IDNA.** `std/idna` with Punycode and UTS #46; `Uri.tryFrom` accepts a non-ASCII host and stores its ASCII form; `repaired(text)` for the WHATWG differences of section 6; gap 9 is answered before `Path` into `Uri` becomes infallible | `std/idna/*`, `std/uri/src/*`, `compiler/src/semantics/checker/derive.trb` | **Medium.** The tables are the work, and gap 9 has to be closed in the same slice or `Uri` loses `Decode` without a diagnostic |
| 8 | **The driver layer.** `Schemes` in `std/uri`; `std/storage` with `Storage`, `StorageFailure`, `Storage.registry`, `FileStorage` over `std/fs` and `MemoryStorage`; `Uri.text` beside `Uri.show` and `compare` over `text` (that half belongs to slice 1 and is written there) | `std/uri/src/*`, `std/storage/*`, `std/fs/src/lib.trb` for gap 13's `remove` | **Blocked** on gap 10 and on `docs/design/CONCURRENCY.md`'s cancellation slice. Probe 7 builds the synchronous form; landing it before the trait is asynchronous would change every driver's signature afterwards, which is the one change an ecosystem cannot absorb |

**The prose.** A new `docs/standard-library/uri.md`, `docs/standard-library/identifier.md` and
`docs/standard-library/storage.md`; `docs/design/PATH.md`'s "Not a URL" paragraph points here; `docs/design/RESOURCES.md`'s
section 6 gains one sentence and its `Sandbox` section gains gap 13's; `docs/design/PROJECT.md` section 7's source table
says the values are `Uri`s and that the fetchers stay a closed list; CONCEPT's decision log gains one entry for the
literal rule and one for "a registry is a value"; `docs/internals/index.md` lists this document, which is done.

## 15. What this is not

- **Not a replacement for `Path`.** Section 8. A path is platform-dependent and a URI is not, and the two are joined
  by two fallible conversions rather than merged.
- **Not a fetcher.** Nothing in `std/uri` opens anything. It has no `get`, no `read` and no registry of drivers, and
  `use * as http from "std/http"` stays the import that means "this file talks to a network". What it gains from
  section 11 is `Schemes`, a trait with one member that answers a `List<String>` — a driver says which schemes it
  answers to, and `std/uri` neither knows nor asks which drivers exist.
- **Not WHATWG.** Section 6 lists every difference and what a program does about each. A program that needs a
  browser's repairs calls `repaired(text)` before parsing, where a reader can see it.
- **Not a `Url` type and not a `Uri`/`Url` split.** Section 7. Java's split is the most expensive mistake in this
  design space, and what `Url` would guarantee is what one `?` already gives.
- **Not an IRI type.** There is one type. Non-ASCII in a path, a query or a fragment is percent-encoded as UTF-8 at
  construction, which is what RFC 3987 section 3.1 says a URI for an IRI is; a non-ASCII **host** is refused until
  `std/idna` exists, because percent-encoding one is wrong rather than unsupported. A separate `Iri` type that keeps
  the Unicode form would double every signature to buy a display form, and a display form is `Show`'s job.
- **Not a URI template.** RFC 6570 (`https://example.test/users/{id}`) is a different language with its own grammar,
  and the literal rule of section 9 deliberately refuses an interpolated literal. If it is ever wanted it is a package
  of its own whose `expand` answers a `Uri`.
- **Not a scheme registry.** `std/uri` knows five default ports and the word `urn`, and nothing else. It does not know
  that `https` needs a host, that `mailto` has no authority or that `data` is base64 — those are the schemes'
  business, and a program that cares asks `uri.scheme()`. The registry of section 11 is a value in `std/storage` that
  a program builds out of drivers it named, and no part of it is global, discovered or implicit.
- **Not a universal opener.** Section 11 answers "read this, wherever it is" with a capability trait of five members
  and not with one `open(uri)`. A driver that cannot `list` says so in a `Result`, and the import of the capability
  is what tells a reader what the file can reach.
- **Not a query surface for databases.** `Connection` promises `close`, `ping` and `uri`, because that is all every
  driver can honestly promise. A program that queries names its driver and gets its typed API; `sqlx::AnyPool` is
  what the other choice looks like.
- **Not a resource name.** `docs/design/RESOURCES.md` section 6's stable name is project-relative text that the build
  resolved; it is not reached by a scheme, there is no `embedded:` or `resource:`, and there is no run-time lookup by
  string. That document's "Not a virtual file system" is unchanged.
- **Not a module specifier.** `docs/design/PROJECT.md` section 6's grammar reserves every `scheme:` prefix with a message
  that points at the manifest, and section 10 above does not reopen it.
- **Not `Encode`d as an object.** A `Uri` in a JSON document is its text, because a capsule is written as its source
  type (`docs/design/ENCODING.md` section 3a). A reader of such a document sees `"https://example.test/a"` and not five
  fields, which is what every other language's JSON does too.

## 16. Open

Everything technical above is decided. These are taste or direction, and only the owner answers them.

1. **Is a universal opener wanted? — Decided by the owner.** *`std/fs` and `File.open` take no `Uri`: `std/fs` is the
   native driver on `Path`. Above it sits an abstraction that picks a driver by the URI's scheme and can then also
   reach S3, WebDAV and the rest; it consumes `std/fs` for `file:`. And the connection data of a service — a
   database, a key-value cache, a message queue — is a URI, and the scheme is what finds the driver, the protocol or
   the resource.* Section 11 is that layer, written out and probed. What it changes against the sketch: the layer is
   a **capability trait per capability** rather than one `open(uri)`, there are **two** registry shapes because a
   resource URI and a service URI are read at different moments, and `Connection` promises only a lifecycle.
2. **Does the prelude export `Uri`?** The document assumes yes: `Uri` and `UriError`, two names on top of 146, with
   `Authority`, `Urn` and `repaired` behind `use … from "std/uri"`. That pairs with `docs/design/PATH.md` open question 2,
   which left `Path` out of the prelude — and the two belong together, because `File.readText("./config.trb")` and
   `http.get("https://…")` are the same ergonomics. The recommendation is both, which is four names.
   **Answered by the owner (2026-09-23):** yes, `Uri` and `UriError`. `std/uri` is not built yet, so the prelude exports `Path` and
   `PathError` today and the two `std/uri` names join them with the package.
3. **`Identifier`, or `Uid`?** The rule says full words and no abbreviations, and `Uid` also means a POSIX user id.
   The document uses `Identifier`.
   **Answered by the owner (2026-09-23):** `Identifier`.
4. **UUIDv7 as the default, with `Ulid` kept?** Section 12's table is the argument. The other reading is that 26
   characters that sort as text are worth more than a standard, which is the case every ULID user makes.
   **Answered by the owner (2026-09-23):** UUIDv7 is the default, and `Ulid` is kept beside it.
5. **`reference.resolved(against: base)`, or `base.resolve(reference)`?** The document uses the first, because it is
   `path.resolved(inside: base)` with one word changed. The second is what every other library writes.
   **Answered by the owner (2026-09-23):** `reference.resolved(against: base)`.
6. **Is refusing a non-ASCII host acceptable until `std/idna` exists?** The alternative is percent-encoding it, which
   produces a host no resolver accepts — a wrong value rather than an unsupported one. The document refuses, and
   slice 7 is the answer.
   **Answered by the owner (2026-09-23):** refuse a non-ASCII host until `std/idna` exists.
7. **Is a redacting `show()` the right default?** Section 11 makes `print uri` lossy for the one URI in a thousand
   that carries a password, so a value that is printed no longer round trips through its own display form — which is
   a property every other capsule in the standard library has. The other reading is that `text()` should be the
   `Show` and redaction should be a member a logger calls (`uri.redacted()`), which keeps `show()` honest and puts
   the burden on whoever logs. The document takes the redacting default, because the failure mode of the other one
   is a password in a crash report and the failure mode of this one is a `***` in a message that wanted the whole
   thing.
   **Answered by the owner (2026-09-23):** the redacting `show()` stays the default.
8. **Does `std` carry `Cache` and `Connection` before it carries a driver for either?** The document says no, by the
   rule "a capability trait belongs in `std` when `std` ships two drivers for it, one of which reaches the outside
   world" — so `std/storage` exists and `std/cache` does not. The other reading is that a trait in `std` is how an
   ecosystem converges on one spelling, and that waiting for a driver means two packages will invent two `Cache`
   traits first. Section 11 writes both traits out so that the shape is at least written down either way.
   **Answered by the owner (2026-09-23):** wait for two drivers: `std/cache` and `Connection` come with the second driver of each.
