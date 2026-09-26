# The Domain Name System

**Status: slices 1 to 5 and 8 built (2026-09-26)** — the owner decided on 2026-09-25 that `std/dns` exists as a pure
package: names, records and the wire format of RFC 1035 as values, with no I/O. `std/dns` holds `DomainName` with IDNA,
the records as a closed set of typed cases, `Message` with its codec, and the `query` and `readResponse` helpers;
`std/uri` answers a `DomainName` from `Host.domainName()` and maps a non-ASCII host with it. The transports are other
packages': `lookup` over UDP and TCP with the system's name servers in `std/network`, DNS over TLS in `std/tls`
(section 7, "Lookups, as built"); DNS over HTTPS in `std/http` is a later slice.

**A DNS message is a value, and so is a name.** A transport moves bytes; everything that gives those bytes a meaning -
a name with its IDNA mapping and its comparison, a record with its typed data, a message with its compressed names and
its EDNS0 record - is plain TorbScript that touches nothing, so it is tested without a network and used by every
transport alike.

```text
   a person's text ──→ DomainName.tryFrom      ──→ A-labels, lower case: xn--bcher-kva.example
   a zone file's   ──→ DomainName.ofPresentation ─→ any byte, escapes, case kept: _sip._tcp.example.test
   a message's     ──→ DomainName.ofLabels      ──→ any byte, case kept
                         └── ==, hash() without regard to ASCII case (RFC 4343); show() and unicodeText()

   query(name, type, id) ──→ Bytes ──→ a transport ──→ Bytes ──→ readResponse(bytes, query) ──→ Message
                                        (std/network: UDP, TCP; std/tls: DoT; std/http: DoH)
   Message.tryFrom(bytes) / message.encoded()  ──  RFC 1035 section 4, compression, EDNS0 (RFC 6891)
```

## Contents

- **[1. Goals](#1-goals)**
- **[2. The package boundary](#2-the-package-boundary)**
- **[3. Domain names and IDNA](#3-domain-names-and-idna)**
- **[4. Records](#4-records)**
- **[5. The wire format](#5-the-wire-format)**
- **[6. Queries and responses](#6-queries-and-responses)**
- **[7. Slices](#7-slices)**
- **[8. What this is not](#8-what-this-is-not)**
- **[9. Decisions](#9-decisions)**

## 1. Goals

1. **Names are values with the DNS's own equality.** `DomainName` compares and hashes without regard to the case of
   ASCII letters (RFC 4343), so a name is a key of a `Map` and `WWW.Example.TEST == www.example.test` without anybody
   lower casing anything - while the case a name arrived in is kept for `show()`.
2. **What a person writes becomes what the DNS asks for.** `DomainName.tryFrom("Bücher.example")` is
   `xn--bcher-kva.example`: the lookup processing of UTS #46 and Punycode (RFC 3492), as far as the tables of `std/text`
   reach, with exactly what is not covered written down in section 3.
3. **Records have typed data.** An `A` record holds an `Ipv4Address` of `std/ip`, an `MX` record a preference and a
   `DomainName`, an `HTTPS` record its service parameters as cases - and a type this package does not model is
   `Unknown(recordType, data)` rather than a failure.
4. **Bytes from the network cannot make it panic.** Decoding answers a `Result` whose failure is a `DnsError` that says
   where, for truncations, compression pointers outside the message or in a cycle, and data that does not fit its type.
5. **The transport does only the transport.** `query(name, type, id)` answers the bytes to send and
   `readResponse(bytes, query)` checks and reads what came back, so `std/network`'s later `lookup` is a socket, a
   timeout and a retry, and DNS over TLS and over HTTPS are the same two calls around another pipe.

## 2. The package boundary

**Decided by the owner (2026-09-25): `std/dns` is pure.** It has no natives and needs no capability, like `std/ip`, so
a program that parses a zone's records or validates a host name does not claim the network by importing it.

```text
   std/http   ── later: DoH (RFC 8484) as a resolver option; HTTPS/SVCB (RFC 9460) for ALPN
      │
   std/tls    ── later: DNS over TLS (RFC 7858) over a TcpStream
      │
   std/network ─ resolve(host) over getaddrinfo, as today; later lookup(name, type) over UDP and TCP
      │
   std/uri ──→ std/dns ──→ std/ip
                  └──────→ std/stream (textOf, for the ALPN identifiers)
```

**The dependencies, checked for cycles.** `std/dns` imports `std/ip` for the addresses of `A`, `AAAA` and the address
hints, and `std/stream` for `textOf`, the UTF-8 decoder `std/uri` already uses. It imports nothing that imports it:
neither `std/uri` nor `std/network` nor `std/http`. So **`std/uri` may depend on `std/dns`**, and does:
`Host.domainName()` answers the registered name as a `DomainName`, and slice 5 of [URI.md](URI.md) maps a non-ASCII
host with `DomainName.tryFrom` rather than with a package of its own. The owner's brief named a `std/text/encoding`
"as needed"; the one decoder needed is `std/stream`'s, so no new package was made.

**What that costs.** The prelude names `Uri`, so `std/uri` is type checked in every build, and now `std/dns` with it -
as `std/ip` has been since URI.md's slice 1. It is about 2500 lines with its documentation, none of it natives, and
nothing in `compiler/` calls it, so it is not part of the fixpoint.

**Where the transports go** (section 7 lists them as slices):

| Transport | Package | Standard | What it adds on top of `std/dns` |
|---|---|---|---|
| the system's resolver | `std/network` | `getaddrinfo`, `GetAddrInfoW` | nothing: `resolve(host)` stays as it is, the default for connecting |
| UDP | `std/network` | RFC 1035 section 4.2.1 | `lookup(name, type, server:)`: a random identifier, the datagram, a timeout, retries; needs `UdpSocket` (NETWORK.md slice 5) |
| TCP | `std/network` | RFC 7766 | the same over a `TcpStream` with a two-byte length before each message, when a UDP response has `truncated` set |
| DNS over TLS | `std/network` with `std/tls` | RFC 7858 | the TCP framing inside a `TlsStream` to port 853 |
| DNS over HTTPS | `std/http` | RFC 8484 | a resolver option of the client: `POST` of `application/dns-message`, identifier 0 |
| HTTPS/SVCB for ALPN | `std/http` | RFC 9460 | the client asks for the `HTTPS` record of an origin and takes its `alpn`, `port` and address hints |

## 3. Domain names and IDNA

**One type, three ways in.** A `DomainName` is its labels, most specific first, each 1 to 63 bytes, at most 255 bytes on
the wire with the length bytes and the root's zero byte (RFC 1035 section 2.3.4). A label may hold any byte (RFC 2181
section 11); only the constructors differ in what they accept.

| Constructor | Reads | Accepts |
|---|---|---|
| `DomainName.tryFrom(text)` | a host name as a person writes it | UTS #46 mapping and Punycode; then every label letters, digits and hyphens, no hyphen at either end (RFC 1123 section 2.1); lower case |
| `DomainName.ofPresentation(text)` | the presentation format of RFC 1035 section 5.1 | any byte, `\.`, `\\` and `\DDD`; ASCII only; case kept - `_sip._tcp.example.test`, `*.example.test` |
| `DomainName.ofLabels(labels)` | the labels of a message | any bytes; case kept |

`show()` is the presentation format in A-labels without the dot of the root (`.` for the root itself), with a dot or a
backslash inside of a label escaped and every byte that is not printable ASCII written `\DDD`, so `ofPresentation`
reads every shown name back. `unicodeText()` shows every valid A-label as its U-label, for a person; `show()` stays
ASCII for the reason URI.md section 4a gives - a U-label is where homographs live, and a value type has no policy for
when to show one. Equality, `hash()` and `isSubdomain(of:)` compare labels byte by byte with the ASCII letters folded
(RFC 4343); no other byte is folded.

**The IDNA processing, step by step** (UTS #46 section 4, lookup, non-transitional, `UseSTD3ASCIIRules=true`,
`CheckHyphens=false` as WHATWG uses it, `CheckBidi` and `CheckJoiners` not done - see below):

1. **Map** each code point. Covered: the three other full stops of East Asian text (U+3002, U+FF0E, U+FF61) become
   `.`; the fullwidth forms of ASCII (U+FF01 to U+FF5E) become ASCII; the code points UTS #46 ignores are removed (U+00AD
   soft hyphen, U+034F, U+180B to U+180D and U+180F, U+200B, U+2060, U+FEFF, the variation selectors U+FE00 to U+FE0F
   and U+E0100 to U+E01EF, U+1BCA0 to U+1BCA3); everything is lower cased with `Char.toLowerCase`, which is exact for
   ASCII and for Latin-1 and maps `Ÿ` to `ÿ`. `ß` stays `ß` (non-transitional).
2. **Split** at `.`. One trailing dot is allowed and changes nothing; an empty label is refused.
3. **Validate** each label. ASCII must be letters, digits and hyphens. A hyphen may not start or end a label, and
   `--` in the third and fourth position is allowed (hosts like `r3---sn-abc.example` exist, and browsers set
   `CheckHyphens=false`). Refused outside ASCII, because the package can neither map nor check them: U+0080 to U+00BF
   (C1 controls, the no-break space and the Latin-1 symbols, several of which UTS #46 maps to other characters), `×`
   and `÷`, ZWNJ and ZWJ (U+200C, U+200D - their context rule CONTEXTJ needs joining types), the bidirectional
   formatting characters (U+200E, U+200F, U+202A to U+202E, U+2066 to U+2069), U+2028 and U+2029, private use
   (U+E000 to U+F8FF and the planes 15 and 16), and noncharacters and specials (U+FDD0 to U+FDEF, U+FFF0 to U+FFFF, and
   every code point ending in FFFE or FFFF).
4. **Convert** a label with a character outside ASCII to `xn--` and its Punycode. A label that already starts with
   `xn--` has to be an A-label this package would have written: its Punycode decodes, holds a character outside ASCII,
   contains nothing step 1 would change or step 3 refuses, and encodes back to itself.
5. **Check the lengths** on the result: 63 bytes per A-label, 255 bytes for the name.

**What is not covered, exactly** - each one a table `std/text` does not have yet, and each one closed when it does:

| UTS #46 / IDNA 2008 requirement | What `std/dns` does instead | Effect |
|---|---|---|
| the full mapping table (`IdnaMappingTable.txt`): case folding outside Latin-1, compatibility mappings | lower cases with `Char.toLowerCase` only | an upper-case letter of Greek, Cyrillic, Latin Extended and the rest passes **unmapped**: `ПРИМЕР.рф` becomes the A-label of the upper-case text, not `xn--e1afmkfd`, and the lookup finds nothing. Write such names in lower case until the table exists |
| the disallowed code points of the table outside the ranges of step 3 | accepts them | a label with such a code point is encoded rather than refused |
| NFC normalization (UTS #46 section 4, step 3) | none | `bu` + U+0308 is encoded as two code points and differs from `bü`; a text from a keyboard is NFC already |
| a label may not start with a combining mark (RFC 5891 section 5.4) | not checked | needs `General_Category` |
| CONTEXTJ (RFC 5892 appendix A.1, A.2) | ZWNJ and ZWJ are refused | a name that needs a joiner - rare outside Persian and Indic scripts - cannot be written |
| CONTEXTO (RFC 5892 appendix A.3 to A.9) | not checked | the middle dot, Greek keraia, Hebrew punctuation and the Katakana middle dot pass without their context rule |
| the Bidi rule (RFC 5893) | not checked | a label mixing right-to-left and left-to-right that the rule refuses is accepted |

**Why the refusals and not a guess.** Step 3 refuses what it cannot check where the risk is a wrong name - an invisible
joiner, a bidirectional override - and accepts what it cannot map where the risk is a failed lookup. A failed lookup is
visible; a name that shows as one thing and resolves as another is not.

## 4. Records

`RecordData` is a closed set: one case per type the owner listed, with typed fields, and `Unknown` for the rest.

| Case | Type | Fields | RFC |
|---|---|---|---|
| `A(address)` | 1 | `Ipv4Address` | 1035 |
| `Aaaa(address)` | 28 | `Ipv6Address` | 3596 |
| `Cname(target)` | 5 | `DomainName` | 1035 |
| `Mx(preference, exchange)` | 15 | `Int` (16 bits), `DomainName` | 1035 |
| `Txt(strings)` | 16 | `List<Bytes>`, each at most 255 bytes | 1035 |
| `Srv(priority, weight, port, target)` | 33 | three `Int`s (16 bits), `DomainName` | 2782 |
| `Ns(server)` | 2 | `DomainName` | 1035 |
| `Soa(primaryServer, responsibleMailbox, serial, refresh, retry, expire, minimum)` | 6 | two `DomainName`s, five `Int`s (32 bits) | 1035, 2308 |
| `Ptr(target)` | 12 | `DomainName` | 1035 |
| `Caa(flags, tag, value)` | 257 | `Int` (8 bits), `String` of ASCII letters and digits, `Bytes` | 8659 |
| `Svcb(priority, target, parameters)` | 64 | `Int`, `DomainName`, `List<ServiceParameter>` | 9460 |
| `Https(priority, target, parameters)` | 65 | the same | 9460 |
| `Unknown(recordType, data)` | any other | `RecordType`, `Bytes` | 3597 |

`ServiceParameter` types the keys RFC 9460 defines - `Mandatory(keys)`, `Alpn(protocols)`, `NoDefaultAlpn`,
`Port(port)`, `Ipv4Hint(addresses)`, `EncryptedClientHello(configurations)`, `Ipv6Hint(addresses)` - and keeps every
other key as `Other(key, value)`. An ALPN identifier that is not UTF-8 keeps the whole parameter as `Other(1, ...)`, so
decoding loses nothing.

**A number is an `Int` in the range of its field**, and encoding refuses one outside it rather than truncating it. The
alternative - `UInt16` and `UInt32` fields - would make every literal in a test and every comparison a conversion,
for a check the encoder makes anyway.

**The codes are closed sets that compare by number.** `RecordType`, `RecordClass`, `Operation` and `ResponseCode` each
have a case per code the package knows and `Other(code)`, with `equals` and `hash` over the code, so
`RecordType.Other(28) == RecordType.Aaaa` and a code read from the wire matches the case a program wrote. `show()` is
the mnemonic of the tools (`AAAA`, `NXDOMAIN`) and RFC 3597's `TYPE65280` where there is none.

`Record` is the name, the class (`Internet` by default), the time to live in seconds and the data; its type is the case
of its data, so a record cannot claim one type and hold another. `show()` is a line of a zone file:
`example.test. 300 IN A 192.0.2.1`.

## 5. The wire format

**`Message`** is plain data with defaults: the identifier, QR as `kind` (`MessageKind.Query` or `Response`), the flags
AA, TC, RD, RA, AD and CD as `Bool` fields named `authoritative`, `truncated`, `recursionDesired`,
`recursionAvailable`, `authenticated` and `checkingDisabled`, the `operation`, the twelve-bit `responseCode`, the four
sections, and `edns: Edns?`.
`Message.tryFrom(bytes)` reads one (`TryFrom<Bytes, DnsError>`) and `message.encoded()` writes one.

**Names and compression** (RFC 1035 section 4.1.4):

- **Decoding follows pointers anywhere in the message**, backwards or forwards, but never to a pointer the same name
  already followed (`PointerLoop`) and never outside the message (`InvalidPointer`), and the labels together may not
  pass 255 bytes. So every name ends after at most as many steps as the message has bytes, whatever the bytes are. The
  label types `01` and `10` (the extended labels RFC 6891 retired) are refused.
- **Decoding accepts a compressed name in the data of every type**, including SRV and SVCB, which their RFCs say must
  not be compressed: being strict there refuses a message a server sent for nothing.
- **Encoding compresses** every question name, every owner name, and the names in the data of the types of RFC 1035
  (CNAME, MX, NS, PTR, SOA) - never those of SRV (RFC 2782) and SVCB and HTTPS (RFC 9460 section 2.2), nor of a type it
  does not know (RFC 3597 section 4). A pointer goes to the first occurrence of the longest suffix already written at
  an offset below 16384, compared without case. The responses of a public resolver captured for the tests encode back
  to their own bytes.

**EDNS0** (RFC 6891): the OPT record is taken out of the additional section into `Edns(payloadSize, version,
dnssecOk, options)`, its extended response code joins the four bits of the header in `responseCode`, and its owner has
to be the root. A second OPT record, or one in the answer or the authority section, is `InvalidMessage`. Encoding
writes the OPT record last and refuses a response code above 15 without one. The options stay `EdnsOption(code,
data)`: cookies (RFC 7873), padding (RFC 7830) and client subnet (RFC 7871) are bytes until something reads them.

**Strictness where RFC 1035 leaves room**:

| Situation | Decision | Why |
|---|---|---|
| bytes after the last record | refused (`InvalidMessage`) | the counts of the header are then wrong, and a transport hands over exactly one message |
| a record's data longer than its type needs | refused (`InvalidRecord`) | the bytes left over are a record nobody can read |
| a TXT record with no strings | read as `Txt([])` | RFC 1035 wants one, servers send none, and it round trips |
| a time to live with the top bit set | kept as read | RFC 2181 section 8 says to treat it as 0 - a cache's decision, and the value round trips |
| the Z bit of the header and the unused bits of the OPT record | dropped, written as 0 | they are reserved |
| the class of a question or record with mDNS's top bit | kept as `RecordClass.Other(code)` | mDNS is not this package's |

## 6. Queries and responses

```trb fragment
public fn query(
  name: DomainName,
  recordType: RecordType,
  identifier: Int,
  recursionDesired: Bool = true,
  dnssecOk: Bool = false,
): Bytes

public fn readResponse(bytes: Bytes, query: Bytes): Result<Message, DnsError>
```

`query` writes one question in class `Internet`, RD set, and an EDNS0 record that offers a UDP payload of 1232 bytes
(the DNS Flag Day of 2020: no fragmentation on any path) and sets DO where `dnssecOk` asks. It cannot fail: the name is
valid by its type and the identifier is taken modulo 65536. The transport draws the identifier at random for every
query (RFC 5452 section 9.2) and sends the bytes as they are over UDP, after a two-byte length over TCP.

`readResponse` decodes both messages and checks that the response is one (QR), that its identifier and operation are
the query's, and that its question section is the query's with the names compared without case - or empty, which a
server may send with an error code. It judges nothing else: a `NameError`, an empty answer and a truncated response
are messages, and what to do about them is the transport's (`truncated` means: ask again over TCP).

`message.answersFor(name, recordType)` follows the CNAME chain of the answer section from `name` and answers the
records of `recordType` at its end, with a guard against a chain in a circle; `message.addresses(of:)` is the same for
`A` and `AAAA`. That is what a stub resolver answers, and what `lookup` returns.

**What a transport does with them**, as `std/network`'s `Resolver` does it (section 7):

```text
   const asked = query(name, RecordType.A, randomIdentifier())
   send asked to the server over UDP; wait with a timeout
   const response = readResponse(received, asked)?     ← a spoofed or stale datagram is refused here
   if response.truncated: repeat over TCP (RFC 7766), with the two-byte length
   answer response.addresses(of: name)
```

## 7. Slices

| # | Slice | Package | Depends on | State |
|---|-------|---------|------------|-------|
| 1 | **`std/dns`.** `DomainName` with IDNA (section 3), the records (section 4), `Message` and its codec (section 5), `query` and `readResponse` (section 6); tests with captured responses | `std/dns/*` | `std/ip` | **done** |
| 2 | **`Host.domainName()`** in `std/uri` | `std/uri/src/authority.trb` | 1 | **done** |
| 3 | **`lookup(name, type)` over UDP**: random identifiers, a timeout, retries against the system's configured servers | `std/network` | NETWORK.md slice 5 (`UdpSocket`) | **done** |
| 4 | **TCP** (RFC 7766): the fallback for a truncated response, and the two-byte framing | `std/network` | 3 | **done** |
| 5 | **DNS over TLS** (RFC 7858) to port 853: `TlsResolver` | `std/tls` | 4 | **done** |
| 6 | **DNS over HTTPS** (RFC 8484) as a resolver option of the client | `std/http` | 1; NETWORK.md slice 7 (`Client`) | later |
| 7 | **HTTPS and SVCB records for ALPN** (RFC 9460): the client takes `alpn`, `port` and the address hints of an origin | `std/http` | 3 or 6; HTTP/2 (NETWORK.md slice 12) for `h2` | later |
| 8 | **Non-ASCII hosts in `Uri.tryFrom`**: URI.md slice 5 calls `DomainName.tryFrom`, and `iriText()` shows U-labels | `std/uri` | 1 | **done** |
| 9 | **The rest of UTS #46**: the mapping table, NFC, the combining-mark, CONTEXTJ, CONTEXTO and Bidi rules | `std/dns` | the Unicode tables in `std/text` | blocked |

### Lookups, as built (slices 3 to 5)

```trb fragment
public type Resolver {
  servers: List<SocketAddress> = []      // empty: the system's
  attemptMilliseconds: Int = 2000
  rounds: Int = 2
  fn lookup(name: DomainName, recordType: RecordType): Task<Result<List<Record>, NetworkError>>
  fn exchange(name: DomainName, recordType: RecordType): Task<Result<Message, NetworkError>>
}
public fn lookup(name: DomainName, recordType: RecordType): Task<Result<List<Record>, NetworkError>>
public fn systemNameServers(): Result<List<SocketAddress>, NetworkError>

public type TlsResolver {                  // std/tls
  server: SocketAddress
  serverName: String
  settings: TlsSettings = TlsSettings()
  attemptMilliseconds: Int = 5000
}
```

- **`lookup` answers records, `exchange` the message.** `lookup` is `answersFor(name, type)` of the response - CNAME
  chains followed - an empty list for a name without such records, and `isHostNotFound()` for NXDOMAIN; `exchange` is
  the whole response for a program that reads the authority section or the flags. Both take a `DomainName`, so the name
  was read - and its IDNA done - where it entered the program; `From<DnsError>` makes `DomainName.tryFrom(text)?` work in
  a function that fails with `NetworkError`.
- **`Resolver` is a value with the three settings a stub resolver has**: the servers (empty asks the system's), the
  time one attempt waits, and how many rounds over the servers a lookup makes. The free `lookup` is `Resolver()`'s.
  Times are milliseconds for the reason `ServerLimits` gives. Two seconds and two rounds are between Windows' one second
  and glibc's five; `resolv.conf`'s `options timeout:` and `attempts:` are not read, a program that wants them says so.
- **One attempt is one new UDP socket, connected to the server, with a new random identifier.** Connected, so the
  system drops a datagram from anybody else and a closed port is a refusal at once (NETWORK.md slice 5); a new socket, so
  the source port is new and random too (RFC 5452 section 9.2); a datagram that is not the response to the question -
  another query's, a stale one, a forged one - is dropped by `readResponse` and the wait goes on until the attempt's time
  passes. The identifier comes from the system's randomness (`networkRandom`: `BCryptGenRandom` on Windows,
  `/dev/urandom` elsewhere), exported as `randomQueryIdentifier()` for the other transports.
- **Truncated means TCP to the same server, in the same attempt's time** (RFC 7766), one connection per question, the
  two-byte length before each message. A connection kept for several questions (RFC 7766 section 6.2) is a later
  refinement, as is pipelining.
- **What hands the question on**: no answer within the time, a refusal, a response that is not DNS, and `SERVFAIL`,
  `REFUSED` or `NOTIMP` - each makes the next server of the list the next attempt, and the rounds start over at the first.
  `NXDOMAIN` and an empty answer are answers and end the lookup. After the last attempt the failure of that attempt is
  the lookup's: `isTimedOut()`, `isConnectionRefused()`, or `isNameServerFailure()` (a new predicate of `NetworkError`,
  for a server's failure answer and for a system configured with no server). A server that answers `FORMERR` to the
  EDNS0 record of the query is not asked again without it (RFC 6891 section 7): every resolver of this decade reads
  EDNS0, and the retry is a later refinement if one does not.
- **The system's servers are one native** (`networkNameServers`): on Windows `GetAdaptersAddresses` of iphlpapi, loaded
  on first use - not `GetNetworkParams`, which knows IPv4 only - with the servers of every adapter that is up, in order,
  once each, and without the three site-local placeholders (`fec0:0:0:ffff::1` to `::3`) Windows lists for an adapter
  that has no IPv6 server; on POSIX the `nameserver` lines of `/etc/resolv.conf`, a `%scope` dropped, and the local
  machine where the file names none, as resolv.conf(5) says. `search`, `domain` and `ndots` are not read: `lookup` takes
  a whole name and asks for it as it is.
- **No cache.** Every lookup asks. `resolve(host)` - `getaddrinfo`, with the system's cache and hosts file - stays what
  connecting uses; `lookup` is for records and for a server of the program's own choosing.
- **DNS over TLS fits the same shape and is built** (`TlsResolver` in `std/tls`, which may import `std/network` and
  `std/dns`; `std/network` cannot import `std/tls`): one server, a TCP connection to it - usually port 853 - a TLS
  handshake in which the server proves `serverName` (RFC 8310's strict profile: nothing is sent to a server that cannot),
  and the question and the answer framed as over TCP. One connection per lookup; keeping it open is the refinement TCP
  has too. `lookup` fails with `isNameServerFailure()` for `SERVFAIL`, `REFUSED` and `NOTIMP`, as there is no second
  server to hand the question to.
- **DNS over HTTPS stays `std/http`'s** (slice 6): a `POST` of `application/dns-message` through a `Client`
  (NETWORK.md slice 7, built now), when a program asks for it.
- **Verified**: `tests/conformance/network-lookup.trb` asks a name server written in the test itself, on loopback over
  UDP and TCP: an address, a CNAME chain, a truncated answer asked again over TCP, NXDOMAIN, the whole message, a closed
  port and a failing server passed to the third, a silent server passed after its time, and a list of silent servers
  timing out; `tests/conformance/dns-over-tls.trb` the same over TLS with a test root, and a server that cannot prove the
  name refused. Both run natively and in the VM; the runtime tests read the system's servers and randomness on Windows
  and Linux.

## 8. What this is not

- **Not DNSSEC validation** (the owner, 2026-09-25). The AD bit is read into `authenticated` and the DO bit can be
  set; RRSIG, DNSKEY, DS and NSEC records arrive as `Unknown`. A program that needs validated answers uses a validating
  resolver it trusts and a path to it that cannot be tampered with (DoT, DoH).
- **Not a resolver.** No cache, no recursion, no server selection: those belong to the transports of section 7, and a
  cache is a later question for `lookup`.
- **Not a zone file parser.** Records are written in the presentation format by `show()`; reading a master file (RFC
  1035 section 5, with `$ORIGIN`, `$TTL` and parentheses) is a package of its own if anyone asks.
- **Not TSIG, SIG(0) or dynamic update semantics.** `Operation.Update` and its response codes are named so that such a
  message can be read and written; what an update means is not modelled.
- **Not mDNS or DNS-SD** (RFC 6762, 6763): the unicast-response and cache-flush bits stay inside the class number.
- **Not 0x20 randomization**: `readResponse` compares names without case, as RFC 4343 says; a transport that
  randomizes case compares `show()` of the two questions itself.

## 9. Decisions

1. **Names of the API.** `DnsError` keeps the protocol name as its prefix, capitalized like `Http` and `Json` (the
   naming page, rule 9); the record types are `Aaaa`, `Cname`, `Mx`, `Txt`, `Srv`, `Soa`, `Ptr`, `Caa`, `Svcb` and
   `Https` for the same reason. Everything else is written out: `timeToLive`, `recordType`, `recordClass`,
   `responsibleMailbox`, `EncryptedClientHello`. The Bool fields are adjectives and participles (`truncated`,
   `authoritative`, `authenticated`), which is why QR is `kind: MessageKind` - a field `response: Bool` would be a
   noun; the methods `isRoot()`, `isHostName()` and `isSubdomain(of:)` keep `is` because
   the bare `root` collides with `DomainName.root` and the bare `hostName` and `subdomain` read as nouns.
2. **The message is `Message`, not `DnsMessage`.** It is imported by name from `std/dns`, and a program that meets
   another `Message` renames one with `as`.
3. **`query` answers bytes and `readResponse` takes the query's bytes.** The transport keeps exactly what it sent, so
   the check is against what went over the wire, and no second representation of the query has to be kept in sync.
4. **Two host-name rules are one type.** `tryFrom` builds host names and `ofPresentation` and `ofLabels` build any
   DNS name; `isHostName()` tells them apart. A second type would split every signature that takes "a name to ask
   for", and the DNS itself does not distinguish them.
5. **`std/uri` depends on `std/dns`, and the IDNA code is `std/dns`'s.** There was no punycode or IDNA code in
   `std/uri` to move (URI.md had planned a `std/idna`); with no cycle possible (section 2), the owner's first choice
   holds.
