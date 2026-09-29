---
title: std/digest
summary: Sha256, Sha512, Sha1 and Md5 and the Digest they answer, Hmac (RFC 2104) and PBKDF2 (RFC 8018) over any of them - fed at once or in pieces, shown as lowercase hexadecimal.
kind: package
status: stable
order: 185
keywords:
  - std/digest
  - Sha256
  - Sha512
  - Sha1
  - Md5
  - Digest
  - Hasher
  - Hmac
  - HmacSha256
  - HmacSha1
  - HmacMd5
  - pbkdf2
  - SHA-256
  - SHA-512
  - SHA-1
  - MD5
  - HMAC
  - PBKDF2
  - hash
source:
  - std/digest/src/lib.trb
  - std/digest/src/sha512.trb
  - std/digest/src/sha1.trb
  - std/digest/src/md5.trb
  - std/digest/src/hmac.trb
  - std/digest/src/pbkdf2.trb
---

`std/digest` computes message digests: SHA-256, which is what the package manager pins a package by (the tree hash of
[project.lock.trb](../tooling/project-lock-trb.md)) and what a download is checked against, and SHA-512, which is what
Ed25519 hashes with ([std/signature](signature.md)). SHA-1 and MD5 are here too, for the protocols that still ask for
them - `mysql_native_password`, Postgres's `md5` authentication, SMTP's `CRAM-MD5` - and never for anything new. `Hmac`
(RFC 2104) works over any of the four, and `Hmac.pbkdf2` (RFC 8018) stretches a password through it: what SCRAM
authenticates a password with. It is ordinary TorbScript with no native, so the same bytes give the same digest in the
VM and in a native binary.

## Import

```trb fragment
use Sha256, Sha512, Sha1, Md5, Digest from "std/digest"
use Hmac, HmacSha256, HmacSha1, HmacMd5 from "std/digest"
```

```trb check
use Sha256 from "std/digest"

print Sha256.of("abc".bytes()).hex()
```

## Declarations

### `Sha256`

```trb fragment
public type Sha256 {
  static fn of(bytes: Bytes): Digest
  var fn add(bytes: Bytes)
  fn finished(): Digest
}
```

`Sha256.of(bytes)` is the digest of everything at once. For input that arrives in pieces, `Sha256()` starts a hasher,
`add` feeds the next bytes, and `finished` answers the digest of everything added so far **without changing the
hasher**, so a hasher can be read, fed more, and read again. Pieces of any size give the same digest as the whole.

```trb check
use Sha256 from "std/digest"

var hasher = Sha256()
hasher.add "ab".bytes()
hasher.add "c".bytes()
print(hasher.finished() == Sha256.of("abc".bytes()))
```

### `Sha512`

```trb fragment
public type Sha512 {
  static fn of(bytes: Bytes): Digest
  var fn add(bytes: Bytes)
  fn finished(): Digest
}
```

The same shape as `Sha256`, over 128-byte blocks, answering a digest of 64 bytes. Its words are `UInt64`s, summed with
`addedWrapping`, the one arithmetic of the language that wraps.

```trb check
use Sha512 from "std/digest"

print Sha512.of("abc".bytes()).hex()
```

### `Sha1`

```trb fragment
public type Sha1 {
  static fn of(bytes: Bytes): Digest
  var fn add(bytes: Bytes)
  fn finished(): Digest
}
```

SHA-1 (FIPS 180-4 / RFC 3174): the same shape as `Sha256`, over 64-byte blocks, answering a 20-byte digest.
`mysql_native_password`, MongoDB's SCRAM-SHA-1 and the WebSocket handshake's accept key still ask for it, but nothing
new should choose it over `Sha256` - collisions are practical (SHAttered, 2017).

```trb check
use Sha1 from "std/digest"

print Sha1.of("abc".bytes()).hex()
```

### `Md5`

```trb fragment
public type Md5 {
  static fn of(bytes: Bytes): Digest
  var fn add(bytes: Bytes)
  fn finished(): Digest
}
```

MD5 (RFC 1321), the same shape again, over 64-byte blocks little endian throughout, answering a 16-byte digest.
**Legacy: never for security.** Collisions are cheap to produce and a chosen-prefix collision is practical - it
survives only where a protocol demands its exact bytes: Postgres's `md5` authentication, S3's `Content-MD5` header.

```trb check
use Md5 from "std/digest"

print Md5.of("abc".bytes()).hex()
```

### `Hasher`

```trb fragment
public trait Hasher {
  static fn empty(): Self
  static fn of(bytes: Bytes): Digest
  var fn add(bytes: Bytes)
  fn finished(): Digest
  static fn blockSize(): Int
  static fn digestLength(): Int
}
```

The shape `Sha256`, `Sha512`, `Sha1` and `Md5` all four share, named after the cryptographic sense of "hash" - a hash
function's own equality-hashing is the unrelated `Hash` of `std/core`. `Hmac` is written once, over this trait, for
any of them.

### `Hmac`

```trb fragment
public type Hmac<H: Hasher> {
  static fn keyed(key: Bytes): Self
  var fn add(bytes: Bytes)
  fn finished(): Digest
  static fn of(key: Bytes, message: Bytes): Digest
}

public type HmacSha256 = Hmac<Sha256>
public type HmacSha1 = Hmac<Sha1>
public type HmacMd5 = Hmac<Md5>
```

HMAC (RFC 2104) over any `Hasher`: SCRAM (postgres, mongodb), S3's Signature Version 4, JWT's `HS256`, SMTP's
`CRAM-MD5`. `HmacSha256`, `HmacSha1` and `HmacMd5` name the three combinations the packages after `std` need; `Hmac`
is fed the same way as `Sha256` and the rest.

```trb check
use HmacSha256 from "std/digest"

print HmacSha256.of("key".bytes(), "message".bytes()).hex()
```

**The key is folded into the hasher once, not once a round.** `keyed` pads the key to a block, hashes the two pads into
a fresh inner and outer hasher, and keeps only those two - never the key itself. A round of `Hmac.pbkdf2` copies the
keyed `Hmac` - copying a value costs its size, not its history - so many rounds hash the key's two blocks once between
them, not once a round.

#### `Hmac.pbkdf2`

```trb fragment
extend<H: Hasher> Hmac<H> {
  static fn pbkdf2(password: Bytes, salt: Bytes, iterations: Int, length: Int): List<UInt8>
}
```

PBKDF2 (RFC 8018): `password` stretched into `length` bytes, `iterations` rounds of HMAC salted forward from one round
to the next. `HmacSha256.pbkdf2` is what SCRAM-SHA-256 authenticates a password with.

```trb check
use HmacSha256 from "std/digest"

const derived = HmacSha256.pbkdf2 "password".bytes(), "salt".bytes(), 4096, 32
print derived.length()
```

### `Digest`

```trb fragment
public type Digest with Show {
  bytes: List<UInt8>
  fn hex(): String
}
```

The bytes of a digest, most significant first: 32 of SHA-256, 64 of SHA-512. `hex()` and `show()` are the same text: two lowercase hexadecimal digits
per byte, the spelling of `sha256sum` and of a lock file. Two digests are equal when their bytes are.

## Pitfalls

**A digest is not a signature.** SHA-256 says that two byte sequences are the same; it says nothing about who wrote
them. A hash that travels next to the data it describes proves nothing an attacker who can change the one cannot also
change in the other.

**It is written for correctness, not speed.** A few megabytes take a moment in a native binary and noticeably longer
in the VM. Nothing in the standard library hashes on a hot path - except `Hmac.pbkdf2`'s rounds, which is why it keys
the hasher once and copies it, instead of re-hashing the key on every round.

**MD5 and SHA-1 are for interoperability, not security.** Neither resists a deliberate collision, so nothing signs with
them and nothing new should ask for them; they exist because a handful of protocols still do.

**A `UInt64` literal above the largest `Int64` is not a compile-time constant of a module yet**, so SHA-512's constants
are written as their upper and lower 32 bits and joined where they are used.

## Related

- [std/signature](signature.md) - Ed25519, which hashes with `Sha512`: what a signature adds to a digest.
- [std/archive](archive.md) and [std/compression](compression.md) - the other two halves of a package archive.
- [project.lock.trb](../tooling/project-lock-trb.md) - the tree hash, which is a SHA-256 over SHA-256s.
- [std/encoding](encoding.md) - Base64 and hexadecimal, for a digest or a MAC that travels as text.
- [The standard library](index.md) - the other packages.
