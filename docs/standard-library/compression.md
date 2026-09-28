---
title: std/compression
summary: DEFLATE and gzip in TorbScript - inflated and gunzipped read every stream the formats allow, deflated and gzipped write deterministic output, and crc32 is the checksum gzip uses.
kind: package
status: stable
order: 186
keywords:
  - std/compression
  - gzip
  - deflate
  - inflate
  - crc32
source:
  - std/compression/src/lib.trb
  - std/compression/src/inflate.trb
  - std/compression/src/deflate.trb
  - std/compression/src/gzip.trb
---

`std/compression` reads and writes DEFLATE (RFC 1951) and the gzip members around it (RFC 1952). It is what a package
archive of the registry is compressed with, and what an HTTP body with `Content-Encoding: gzip` is. Everything is
ordinary TorbScript over `List<UInt8>`, with no native.

## Import

```trb fragment
use gzipped, gunzipped, deflated, inflated, crc32, CompressionError from "std/compression"
```

```trb check
use gzipped, gunzipped from "std/compression"

const bytes = "hello, hello, hello".bytes().toList()
print(gunzipped(gzipped(bytes)) == bytes)
```

## Declarations

### `gzipped`, `gunzipped`

```trb fragment
public fn gzipped(bytes: Bytes): Bytes
public fn gunzipped(bytes: Bytes): Result<Bytes, CompressionError>
```

`gzipped` writes one member whose header says nothing about the machine or the moment - no file name, no time, the
operating system "unknown" - so the same bytes give the same member everywhere. `gunzipped` reads every member of its
input one after the other, skips the optional header fields, and checks each member's CRC-32 and length.

### `deflated`, `inflated`

```trb fragment
public fn deflated(bytes: Bytes): Bytes
public fn inflated(bytes: Bytes): Result<Bytes, CompressionError>
```

Raw DEFLATE streams. **Inflating reads every stream the format allows**: stored blocks, the fixed codes and dynamic
Huffman codes, decoded through a table of the next ten bits of the stream; a 30 MB release tarball gunzips in about
0.6 seconds in a release binary, about twice what `gzip -d` takes. **Deflating is deterministic and simple**: one block
of the fixed codes, with a greedy search for matches along hash chains over the last 32 KiB. It compresses text to
roughly half where zlib gets to a third, and every inflater reads it.

### `crc32`

```trb fragment
public fn crc32(bytes: Bytes): UInt32
```

The CRC-32 of ISO 3309 and ITU-T V.42, the checksum of gzip, zip and PNG. `crc32("123456789")` is `0xCBF43926`.

### `CompressionError`

```trb fragment
public type CompressionError with Show, Error {
  reason: String
  offset: Int
}
```

What is wrong with the compressed input, and the byte offset where it was found: a truncated stream, a code no symbol
has, a distance back past the start of the output, a checksum that does not match.

## Pitfalls

**Nothing here limits the output.** A small gzip member can inflate to gigabytes. A caller that takes input from
outside checks the size of what it got back, as the package manager does before it writes an archive to disk.

## Related

- [std/archive](archive.md) - the `tar` a package archive is, inside the gzip.
- [std/digest](digest.md) - SHA-256.
- [The standard library](index.md) - the other packages.
