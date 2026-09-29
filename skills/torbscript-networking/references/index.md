# The pages of torbscript-networking

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- standard-library

## standard-library

- `standard-library/uri.md` - **std/uri** (package): Uri and UriReference after RFC 3986, normalized at construction, with IRIs, Urn, UriTemplate, the file bridge to Path and the form codec of HTML - values that open nothing.
- `standard-library/ip.md` - **std/ip** (package): IPv4, IPv6 and socket addresses as values, read from text and shown as text, with no natives and no capability - std/network and std/uri both use them.
- `standard-library/dns.md` - **std/dns** (package): Domain names with IDNA and the comparison of RFC 4343, DNS records as typed cases, and the RFC 1035 wire format with compression and EDNS0 - values with no natives and no capability, for the transports of std/network and std/http.
- `standard-library/network.md` - **std/network** (package): Name resolution and DNS lookups, TCP - a listener, and a stream whose two directions are a Source and a Sink of Bytes - and UDP datagrams, over the address values of std/ip, which it re-exports.
- `standard-library/tls.md` - **std/tls** (package): TLS 1.2 and 1.3 over a TcpStream - a client that checks the server's certificate the way the platform does, a server with an identity, a stream like the TCP one, and DNS over TLS.
- `standard-library/http.md` - **std/http** (package): HTTP/1.1 and HTTPS, client and server, over std/network and std/tls - get, post and send answer a Task, a handler answers a Task of a Response, and every body is a stream.
