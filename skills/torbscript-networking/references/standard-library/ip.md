---
title: std/ip
summary: IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
kind: package
status: stable
order: 172
keywords:
  - std/ip
  - IpAddress
  - Ipv4Address
  - Ipv6Address
  - SocketAddress
  - AddressError
  - RFC 5952
source:
  - std/ip/src/lib.trb
  - std/ip/src/address.trb
  - docs/design/URI.md
---

`std/ip` holds the addresses of the Internet Protocol as values: an IPv4 address, an IPv6 address, an address of either
version, and a socket address - an address and a port. It touches nothing: it has no natives and needs no capability,
so a package that only *names* an address - a URI whose host is an IP literal, a configuration file - does not claim the
network by importing it. [std/network](network.md) connects to these addresses and re-exports every name of this
package, so `use SocketAddress from "std/network"` and `use SocketAddress from "std/ip"` name the same type (see
docs/design/URI.md section 8a). It is not in the prelude.

## Import

```trb fragment
use IpAddress, Ipv4Address, Ipv6Address, SocketAddress, AddressError from "std/ip"
```

```trb check
use IpAddress, Ipv6Address, SocketAddress from "std/ip"

const local = SocketAddress IpAddress.loopback, 8080
print local
match Ipv6Address.tryFrom("2001:DB8:0:0:0:0:0:1") {
  Ok(address) => print address
  Fail(problem) => print problem
}
```

## Declarations

```trb fragment
public type Ipv4Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError>
public type Ipv6Address with Show, Equals, Hash, Compare, TryFrom<String, AddressError>
public type IpAddress with Show, Equals, Hash, Compare, TryFrom<String, AddressError> {
  case Version4(address: Ipv4Address)
  case Version6(address: Ipv6Address)
}
public type SocketAddress with Show, Equals, Hash, TryFrom<String, AddressError> {
  address: IpAddress
  port: Int
}
public type AddressError with Show, Error
```

Values like any other, read from text with `tryFrom` and shown as text. An IPv4 address is four decimal parts from 0 to
255; a part with a leading zero is refused, because some systems read `010` as octal. An IPv6 address shows in the RFC
5952 form: lower case, no leading zeros, the longest run of zero segments collapsed to `::`, and an IPv4-mapped
address with its IPv4 part (`::ffff:192.0.2.1`). A zone (`fe80::1%eth0`) is not part of an address and is refused.
`Ipv4Address.loopback`, `Ipv6Address.loopback`, `IpAddress.loopback` and the `unspecified` addresses are constants, and
`isLoopback()`, `isPrivate()`, `isLinkLocal()`, `isMulticast()` answer what they say. A `SocketAddress` shows as
`127.0.0.1:8080` or `[::1]:8080`. `IpAddress` orders every IPv4 address before every IPv6 address.

## Related

- [std/network](network.md) - resolving names into these addresses, and connecting to them.
- [std/uri](uri.md) - a URI whose host is an IP literal holds one of these, and answers a `SocketAddress`.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the other packages.

