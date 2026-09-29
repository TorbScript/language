---
title: std/dns
summary: Domain names with IDNA and the comparison of RFC 4343, DNS records as typed cases, and the RFC 1035 wire format with compression and EDNS0 - values with no natives and no capability, for the transports of std/network and std/http.
kind: package
status: stable
order: 173
keywords:
  - std/dns
  - DNS
  - DomainName
  - IDNA
  - Punycode
  - UTS 46
  - Message
  - Record
  - RecordData
  - RecordType
  - DnsError
  - EDNS0
  - RFC 1035
source:
  - std/dns/src/lib.trb
  - std/dns/src/name.trb
  - std/dns/src/punycode.trb
  - std/dns/src/codes.trb
  - std/dns/src/record.trb
  - std/dns/src/message.trb
  - std/dns/src/wire.trb
  - std/dns/src/error.trb
  - docs/design/DNS.md
---

`std/dns` holds the Domain Name System as values: a `DomainName` read from what a person writes (IDNA, with Unicode
written as A-labels), from the presentation format of a zone file, or from the labels of a message, and compared
without regard to the case of ASCII letters; the records as a closed set of typed cases; and a `Message` with the wire
format of RFC 1035, compressed names and the EDNS0 record. It touches nothing - no natives, no capability - so sending
a query is a transport's: `std/network` over UDP and TCP, DNS over TLS and DNS over HTTPS are later slices that move the
bytes `query` writes and `readResponse` reads (see docs/design/DNS.md). It is not in the prelude.

## Import

```trb fragment
use DomainName, DnsError, Message, Record, RecordData, RecordType, query, readResponse from "std/dns"
```

```trb check
use DomainName, Message, RecordType, query from "std/dns"

match DomainName.tryFrom("Bücher.example") {
  Ok(name) => {
    print name
    print name.unicodeText()
    const bytes = query name, RecordType.Aaaa, 4660
    match Message.tryFrom(bytes) {
      Ok(message) => print message.questions
      Fail(problem) => print problem
    }
  }
  Fail(problem) => print problem
}
```

## Declarations

### Names

```trb fragment
public type DomainName with Show, Equals, Hash, TryFrom<String, DnsError> {
  static root: DomainName
  static fn tryFrom(text: String): Result<Self, DnsError>
  static fn ofPresentation(text: String): Result<Self, DnsError>
  static fn ofLabels(labels: List<Bytes>): Result<Self, DnsError>
  fn labels(): List<Bytes>
  fn labelCount(): Int
  fn isRoot(): Bool
  fn isHostName(): Bool
  fn parent(): Self?
  fn isSubdomain(of: DomainName): Bool
  fn wireLength(): Int
  fn show(): String
  fn unicodeText(): String
}
```

A name is its labels, most specific first, each 1 to 63 bytes, at most 255 bytes on the wire. `tryFrom` reads a host
name as a person writes it: the lookup processing of UTS #46 as far as the tables of std/text (skill `torbscript-standard-library`: `references/standard-library/text.md`) reach, then
Punycode (RFC 3492), so `Bücher.example` is `xn--bcher-kva.example`; every label is letters, digits and hyphens, and
one trailing dot changes nothing. `ofPresentation` reads the presentation format of a zone file - any byte, `\.` and
`\DDD` escapes, no IDNA - for names such as `_sip._tcp.example.test`, and `ofLabels` takes the labels of a message.
`==` and `hash()` ignore the case of ASCII letters (RFC 4343), and `show()` keeps the case the name arrived in.
`unicodeText()` shows the A-labels as U-labels for a person to read. What the IDNA processing does not cover - the
mapping table outside Latin-1, NFC, the contextual and bidirectional rules - is listed in
docs/design/DNS.md section 3.

### Records

```trb fragment
public type RecordData with Show, Equals, Hash {
  case A(address: Ipv4Address)
  case Aaaa(address: Ipv6Address)
  case Cname(target: DomainName)
  case Mx(preference: Int, exchange: DomainName)
  case Txt(strings: List<Bytes>)
  case Srv(priority: Int, weight: Int, port: Int, target: DomainName)
  case Ns(server: DomainName)
  case Soa(
    primaryServer: DomainName,
    responsibleMailbox: DomainName,
    serial: Int,
    refresh: Int,
    retry: Int,
    expire: Int,
    minimum: Int,
  )
  case Ptr(target: DomainName)
  case Caa(flags: Int, tag: String, value: Bytes)
  case Svcb(priority: Int, target: DomainName, parameters: List<ServiceParameter>)
  case Https(priority: Int, target: DomainName, parameters: List<ServiceParameter>)
  case Unknown(recordType: RecordType, data: Bytes)
}
public type ServiceParameter with Show, Equals, Hash
public type Record with Show, Equals, Hash {
  name: DomainName
  recordClass: RecordClass = RecordClass.Internet
  timeToLive: Int
  data: RecordData
}
public type RecordType with Show, Equals, Hash
public type RecordClass with Show, Equals, Hash
```

The data of a record is typed by its case - addresses of [std/ip](ip.md), names, numbers in the range of their field -
and a type this package does not model is `Unknown` with its bytes. `ServiceParameter` types the keys of RFC 9460
(`Alpn`, `Port`, `Ipv4Hint`, `Ipv6Hint`, ...) and keeps the rest as `Other(key, value)`. `RecordType`, `RecordClass`,
`Operation` and `ResponseCode` are closed sets with `Other(code)` that compare by their number, so
`RecordType.Other(28) == RecordType.Aaaa`. `show()` writes the presentation format of a zone file:
`example.test. 300 IN A 192.0.2.1`.

### Messages

```trb fragment
public type Message with Show, Equals, Hash, TryFrom<Bytes, DnsError> {
  identifier: Int = 0
  kind: MessageKind = MessageKind.Query
  operation: Operation = Operation.Query
  authoritative: Bool = false
  truncated: Bool = false
  recursionDesired: Bool = false
  recursionAvailable: Bool = false
  authenticated: Bool = false
  checkingDisabled: Bool = false
  responseCode: ResponseCode = ResponseCode.NoError
  questions: List<Question> = []
  answers: List<Record> = []
  authority: List<Record> = []
  additional: List<Record> = []
  edns: Edns? = None
  static fn tryFrom(bytes: Bytes): Result<Self, DnsError>
  fn encoded(): Result<Bytes, DnsError>
  fn answersFor(name: DomainName, recordType: RecordType): List<Record>
  fn addresses(of: DomainName): List<IpAddress>
}
public type MessageKind with Show, Equals, Hash {
  case Query
  case Response
}
public type Question with Show, Equals, Hash
public type Edns with Show, Equals, Hash
public type EdnsOption with Show, Equals, Hash
public type Operation with Show, Equals, Hash
public type ResponseCode with Show, Equals, Hash
public fn query(
  name: DomainName,
  recordType: RecordType,
  identifier: Int,
  recursionDesired: Bool = true,
  dnssecOk: Bool = false,
): Bytes
public fn readResponse(bytes: Bytes, query: Bytes): Result<Message, DnsError>
public type DnsError with Show, Error
```

`Message.tryFrom` reads the bytes of a message, following compression pointers with a guard against loops and pointers
outside the message; the EDNS0 record becomes `edns`, and its extended bits join `responseCode`. Bytes from the network
cannot make it panic: every problem is a `DnsError` that says where. `encoded()` compresses every name RFC 1035 allows
to compress and refuses a value its field cannot carry. `query` answers the bytes of a standard query with EDNS0
(1232 bytes of UDP payload offered), and `readResponse` reads a response and checks it against the query's bytes -
identifier, operation, question. `answersFor` follows the CNAME chain of the answer section, and `addresses(of:)` is
the same for `A` and `AAAA`. The AD bit is read into `authenticated`; DNSSEC validation is not part of the package.

## Related

- [std/ip](ip.md) - the addresses an `A` and an `AAAA` record hold.
- [std/uri](uri.md) - `Host.domainName()` answers the registered name of a URI as a `DomainName`.
- [std/network](network.md) - `resolve` asks the system's resolver today; `lookup` over UDP and TCP will send these messages.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the other packages.

