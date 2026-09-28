---
title: std/digest
summary: Sha256, Sha512 and the Digest they answer - SHA-256 and SHA-512 of FIPS 180-4 in TorbScript, fed at once or in pieces, shown as lowercase hexadecimal.
kind: package
status: stable
order: 185
keywords:
  - std/digest
  - Sha256
  - Sha512
  - Digest
  - SHA-256
  - SHA-512
  - hash
source:
  - std/digest/src/lib.trb
  - std/digest/src/sha512.trb
---

`std/digest` computes message digests: SHA-256, which is what the package manager pins a package by (the tree hash of
[project.lock.trb](../tooling/project-lock-trb.md)) and what a download is checked against, and SHA-512, which is what
Ed25519 hashes with ([std/signature](signature.md)). It is ordinary TorbScript with no native, so the same bytes give
the same digest in the VM and in a native binary.

## Import

```trb fragment
use Sha256, Sha512, Digest from "std/digest"
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
in the VM. Nothing in the standard library hashes on a hot path.

**A `UInt64` literal above the largest `Int64` is not a compile-time constant of a module yet**, so SHA-512's constants
are written as their upper and lower 32 bits and joined where they are used.

## Related

- [std/signature](signature.md) - Ed25519, which hashes with `Sha512`: what a signature adds to a digest.
- [std/archive](archive.md) and [std/compression](compression.md) - the other two halves of a package archive.
- [project.lock.trb](../tooling/project-lock-trb.md) - the tree hash, which is a SHA-256 over SHA-256s.
- [The standard library](index.md) - the other packages.

