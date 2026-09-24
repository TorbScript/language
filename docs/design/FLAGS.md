# Cases with Fixed Values and Flags

**Status: planned** — decided on 2026-09-19, nothing of it is built: a case cannot carry a value today, and there is no
`Flags` type in `std`. This record is a stub that keeps the decision; the full design comes before the first slice.

**A bit mask is two small pieces, not a construct of its own.** The language has no bit operators (`std/number`'s
`Bits` names them as methods), and it does not get any for this. What C enums, protocol codes, database columns and
bit masks need is, first, a case that stands for a fixed number, and second, a set of such cases that is stored as one
integer.

## 1. Cases with a fixed value

A type whose cases all have no fields may give each of them a constant - all of them or none:

```trb fragment
type Permission {
  case Read = 1
  case Write = 2
  case Execute = 4
}
```

Such a type gets two generated members: `fn rawValue(): Int`, the number of the case, and
`static fn fromRawValue(value: Int): Permission?`, the case of a number or `None`. That is useful for every kind of
interoperability and not only for flags: a foreign signature can take such a type directly, a database column or a
protocol code reads into it, and a `match` over it stays exhaustive.

The rules the checker has to hold:

- all cases have a value or none has, and a case with fields never has one;
- two cases never have the same value;
- the values are integer literals.

## 2. `Flags<Case>`

`Flags<Case>` is a value type of `std/collections` over one `UInt64` mask, with the vocabulary a `Set` already has
([COLLECTIONS.md](COLLECTIONS.md) section 6b): `contains`, `insert` and `inserted`, `remove` and `removed`,
`Flags.union`, `Flags.intersection` and `Flags.difference` as `Set` has them, `isEmpty`, and iteration over the cases
that are set. It is written on top of the `Bits` methods of
`std/number`, so it needs no new operator and no new native.

```trb fragment
var access: Flags<Permission> = [.Read, .Write]
access.insert .Execute
if access.contains(.Write) {
  print "may write"
}
const mask = access.bits()
const parsed = Flags<Permission>.fromBits(5)?
```

- **`bits()`** is the mask for a foreign function, a file or the network (`7` above).
- **`fromBits(mask)`** fails when a bit belongs to no case.
- **Every value must be a power of two** for a type used as `Flags<Case>`, and the checker reports a case that is not
  one at the case.
- A list literal adapts to `Flags<Case>` as it adapts to every type that can be built from an `Iterate<Item>`.

## 3. Interoperability

Outside the program the type is exactly its number: in the C back end a `uint64_t` without a wrapper, in PHP an `int`,
in JavaScript a `number` (the bit operators of JavaScript are exact up to 32 flags; above that the mask is two halves).
Through `Encode` it is either the number or the list of the case names, as an option of the format.

## 4. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | Cases with a fixed value: the parser, the checker's rules of section 1, `rawValue`/`fromRawValue` generated, both back ends | nothing |
| 2 | `Flags<Case>` in `std/collections`, the power-of-two rule in the checker, `Encode` and `Decode` | 1 |

## 5. Open

- The names `rawValue` and `fromRawValue` follow Swift. Whether they survive the naming rule of the language (a full
  word, a noun for a value) is decided with slice 1.
- Whether a case value may be any integer type (`case Read: UInt8 = 1`), or always `Int`.
