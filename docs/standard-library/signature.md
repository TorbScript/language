---
title: std/signature
summary: Ed25519 of RFC 8032 in TorbScript - a private key from a 32-byte seed signs, a public key verifies, and keys and signatures are capsules that read and write themselves as bytes and hexadecimal.
kind: package
status: stable
order: 188
keywords:
  - std/signature
  - Ed25519
  - Ed25519PrivateKey
  - Ed25519PublicKey
  - Ed25519Signature
  - SignatureError
  - EdDSA
  - RFC 8032
  - digital signature
source:
  - std/signature/src/lib.trb
  - std/signature/src/ed25519.trb
  - std/signature/src/error.trb
  - std/signature/src/point.trb
  - std/signature/src/field.trb
  - std/signature/src/scalar.trb
---

`std/signature` makes and checks digital signatures. Its one scheme is Ed25519 (RFC 8032 section 5.1): a private key
made from 32 bytes of randomness signs a message, and anybody who has the public key can check that the signature was
made by that key over exactly that message. The package registry signs every record of its index with it
([RELEASE.md](../design/RELEASE.md) section 7.4), so that a mirror can hold back a release but cannot change one. It is
ordinary TorbScript with no native, over `Int` arithmetic and [std/digest](digest.md)'s `Sha512`, so both back ends
compute the same bits.

## Import

```trb fragment
use Ed25519PrivateKey, Ed25519PublicKey, Ed25519Signature, SignatureError from "std/signature"
```

```trb check
use Ed25519PrivateKey from "std/signature"

const seed: List<UInt8> = List.filled 32, 1
const key = Ed25519PrivateKey.fromSeed(seed).expect("32 bytes")
const message = "a release record".bytes().toList()
const signature = key.signature message
print key.publicKey().verifies(message, signature)
```

## Declarations

### `Ed25519PrivateKey`

```trb fragment
public type Ed25519PrivateKey with Show, Encode {
  static fn fromSeed(seed: Bytes): Result<Ed25519PrivateKey, SignatureError>
  static fn fromHex(text: String): Result<Ed25519PrivateKey, SignatureError>
  fn seed(): List<UInt8>
  fn seedHex(): String
  fn publicKey(): Ed25519PublicKey
  fn signature(message: Bytes): Ed25519Signature
}
```

A private key is the 32-byte seed RFC 8032 calls the private key. `fromSeed` derives everything else from it once - the
secret scalar, the prefix the nonce of a signature is hashed from, and the public key - which costs about as much as one
signature; `fromHex` reads the seed as 64 hexadecimal digits, the way it comes out of an environment variable.
`signature` signs a message. Signing is deterministic: the same key and message always give the same signature, and it
needs no randomness of its own.

The package makes no randomness: where the seed comes from is the caller's, and it has to be 32 bytes of the system's
source of randomness. Its `show()` and its `Encode` write `Ed25519PrivateKey(public key <hex>)` and never the seed, so a
key that lands in a log line or an encoded configuration does not leak; `seed` and `seedHex` are the ways to the secret,
for the one place that keeps it.

### `Ed25519PublicKey`

```trb fragment
public type Ed25519PublicKey with Show, Equals, Hash, TryFrom<String, SignatureError> {
  static fn fromBytes(bytes: Bytes): Result<Ed25519PublicKey, SignatureError>
  static fn tryFrom(value: String): Result<Ed25519PublicKey, SignatureError>
  fn bytes(): List<UInt8>
  fn hex(): String
  fn verifies(message: Bytes, signature: Ed25519Signature): Bool
}
```

A public key is 32 bytes that encode a point of the curve. Both factories check that they do, canonically - a y below
p, an x that exists, no negative zero - so a key that exists is one `verifies` can use. As text it is 64 lowercase
hexadecimal digits: `hex()`, `show()` and `String.from(key)` write them, `tryFrom` reads them in either case, and the
same pair is how a key is encoded and decoded. Two keys are equal when their bytes are.

`verifies` answers whether a signature is this key's signature of a message: whether S B - k A encodes as R, the
equation without the cofactor that ref10, libsodium and OpenSSL check.

```trb check
use Ed25519PrivateKey, Ed25519PublicKey from "std/signature"

const key = Ed25519PrivateKey.fromSeed(List.filled(32, 2)).expect("32 bytes")
const published = String.from key.publicKey()
const reader = Ed25519PublicKey.tryFrom(published).expect("the key just written")
const message = "index/acme/http.trb".bytes().toList()
print reader.verifies(message, key.signature(message))
print reader.verifies("another message".bytes().toList(), key.signature(message))
```

### `Ed25519Signature`

```trb fragment
public type Ed25519Signature with Show, TryFrom<String, SignatureError> {
  static fn fromBytes(bytes: Bytes): Result<Ed25519Signature, SignatureError>
  static fn tryFrom(value: String): Result<Ed25519Signature, SignatureError>
  fn bytes(): List<UInt8>
  fn hex(): String
}
```

A signature is 64 bytes: the encoded point R, then the scalar S. The factories refuse an S that is not below the group
order L (RFC 8032 section 5.1.7), which is what keeps a signature from having a second form that verifies as well. As
text it is 128 lowercase hexadecimal digits, read back by `tryFrom` and encoded and decoded the same way.

### `SignatureError`

```trb fragment
public type SignatureError with Show, Error {
  case WrongLength(what: String, expected: Int, found: Int)
  case NotHexadecimal(what: String, reason: String)
  case NotAPoint
  case ScalarOutOfRange
}
```

Why bytes or a text are not a key or a signature: the wrong number of bytes, text that is not hexadecimal, 32 bytes that
encode no point of the curve, or an S that is not below L. A signature that does not verify is not an error:
`verifies` answers `false`.

```trb check
use Ed25519PublicKey from "std/signature"

print Ed25519PublicKey.tryFrom("0200000000000000000000000000000000000000000000000000000000000000")
print Ed25519PublicKey.tryFrom("d75a98")
```

## Correctness and speed

**Tested** against all five test vectors of RFC 8032 section 7.1 (the empty message, one byte, two bytes, 1023 bytes,
and the SHA-512 of `abc`) and against four vectors Node's `crypto` made from random seeds and messages
(`std/signature/tests`, natively and in the VM): each seed makes its public key, each message signs to the printed
signature byte for byte, each signature verifies, and a changed message, R, S or key does not. The vectors of the RFC
were checked against Node's `crypto` - OpenSSL's Ed25519 - before they were written into the test. Once more, by hand:
256 random seeds and messages of up to 300 bytes, signed by Node, gave the same public keys and the same signatures
here, every signature verified, and each of them with one bit flipped was refused here exactly where Node refused it.

**Measured** with `benchmarks/signature.trb`, a message of the size of an index record, on the machine that built it
(Windows, gcc, x86-64, 2026-09-27):

| | native, `--release` | the VM | Node's `crypto` (OpenSSL), for comparison |
|---|---|---|---|
| a key from a seed | about 700 per second | about 23 per second | - |
| a signature | about 700 per second | about 16 per second | about 13 000 per second |
| a verification | about 1 000 per second | about 21 per second | about 5 000 per second |

A thousand index records verify in about a second natively, which is what the package manager does: `torb` is a native
program. Most of the distance to OpenSSL is TorbScript's checked arithmetic and bounds; signing is further behind
because it does not use ref10's precomputed table of multiples of the base point, and computes its nine multiples for
every signature instead.

**Constant time, and where it stops.** Signing never branches on a secret and never indexes by one, in the source:
the multiplication of the base point writes the scalar as signed digits and reads all nine multiples of the base point
for every digit, keeping the one it wants with a mask; the field arithmetic, the reduction modulo L, S = r + k s and
SHA-512 are fixed sequences of arithmetic. What that becomes in the machine is not verified: the C the compiler emits
checks every `+` and `*` for overflow with a branch that is never taken, the C compiler is free to reorganize the
masks, no measurement of the timing was made, and the VM is an interpreter nobody should call constant time. Not
constant time at all, and not meant to be: verifying (everything it reads is public), decoding a public key, the
hexadecimal of `fromHex`, and `==` on two private keys, which compares their seeds byte by byte. Nothing wipes a seed
from memory; a copy lives as long as the value does.

## Pitfalls

**A seed is not a password.** A seed made from something a person chose, or from a hash of it, can be guessed. It has
to be 32 bytes of the system's source of randomness, kept secret, and never written into a repository.

**`verifies` without the cofactor.** Implementations that check 8 S B = 8 R + 8 k A accept a few signatures made with
points of small order that this one refuses. Both are allowed by RFC 8032; a system that needs every implementation to
agree on every edge case has to pin one of the two, as ZIP 215 does for the other.

## Related

- [std/digest](digest.md) - `Sha512`, which Ed25519 hashes with, and why a digest is not a signature.
- [RELEASE.md](../design/RELEASE.md) section 7.4 - the index of the registry, whose every record is signed.
- [The standard library](index.md) - the other packages.
