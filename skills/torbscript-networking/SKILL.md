---
name: torbscript-networking
description: "Writes networked TorbScript: HTTP and HTTPS clients and servers with `std/http`, TCP and UDP with `std/network`, TLS, DNS, IP addresses and URIs. Use it when TorbScript code serves or calls HTTP, opens a socket, resolves a name, or builds or parses a URL."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs."
---

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

- [std/uri](references/standard-library/uri.md) - Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
- [std/ip](references/standard-library/ip.md) - IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
- [std/dns](references/standard-library/dns.md) - Domain names with IDNA and the comparison of RFC 4343, DNS records as typed cases, and the RFC 1035 wire format with compression and EDNS0 - values with no natives and no capability, for the transports of std/network and std/http.
- [std/network](references/standard-library/network.md) - Name resolution and DNS lookups, TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - and UDP datagrams, over the address values of std/ip, which it re-exports.
- [std/tls](references/standard-library/tls.md) - TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, a stream like the TCP one, and DNS over TLS.
- [std/http](references/standard-library/http.md) - HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
