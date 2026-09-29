---
name: torbscript-networking
description: "Writes networked TorbScript: HTTP and HTTPS clients and servers with `std/http`, TCP and UDP with `std/network`, TLS, DNS, IP addresses and URIs. Use it when TorbScript code serves or calls HTTP, opens a socket, resolves a name, or builds or parses a URL."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs."
---

<!-- carry: standard-library/http.md standard-library/network.md standard-library/tls.md standard-library/dns.md -->
<!-- carry: standard-library/ip.md standard-library/uri.md -->

# Networking in TorbScript

`std/http` and `std/network` are not in the prelude, so a file that touches the network says so with its imports. An
HTTP call answers a `Task`, and every body and every socket is a stream: the `torbscript-concurrency` skill has
`.await()`, `Source` and `Sink`, and the `torbscript` skill the rest of the language. Paths are relative to the
directory of this file.

## Which page answers what

- Calling an HTTP or HTTPS API, or serving one - [std/http](references/standard-library/http.md): `get`, `post` and
  `send`, `Server` and its handlers, `Request`, `Response`, `Headers`, `Status` and `Body`.
- A TCP listener or stream, UDP datagrams, resolving a name - [std/network](references/standard-library/network.md).
- TLS over a TCP stream, as a client or a server - [std/tls](references/standard-library/tls.md).
- Building, parsing or normalizing a URL, a URI template, form encoding - [std/uri](references/standard-library/uri.md).
- IPv4, IPv6 and socket addresses as values - [std/ip](references/standard-library/ip.md).
- Domain names, DNS records and their wire format - [std/dns](references/standard-library/dns.md).

## Pages

<!-- pages -->
