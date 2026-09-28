# Cases with Fixed Values and Flags

**Status: built (2026-09-28)** — decided on 2026-09-19. Both slices of section 4 are in: `case Read = 1` in the parser,
the rules of section 1 in the checker (`compiler/src/semantics/checker/case-value.trb`), `RawValue` generated for such a
type in both back ends, and `Flags<Case>` in `std/collections` with the power-of-two rule and its encoding as the
mask. Section 5 records how its two open points were decided, and section 6 what is left.

**A bit mask is two small pieces, not a construct of its own.** What C enums, protocol codes, database columns and bit
masks need is, first, a case that stands for a fixed number, and second, a set of such cases that is stored as one
integer.

## 1. Cases with a fixed value

A type whose cases all have no fields may give each of them a constant - all of them or none:

```trb check
type Permission {
  case Read = 1
  case Write = 2
  case Execute = 4
}

print Permission.Write.rawValue()
print Permission.fromRawValue(4)
```

Such a type gets two generated members: `fn rawValue(): Int`, the number of the case, and
`static fn fromRawValue(value: Int): Permission?`, the case of a number or `None`. That is useful for every kind of
interoperability and not only for flags: a foreign signature can take such a type directly, a database column or a
protocol code reads into it, and a `match` over it stays exhaustive.

**Built as the generated implementation of a trait**, `RawValue` of `std/core`, which the prelude exports: the two
members are its members, the checker derives it where it is used (`derive.trb`) for a type whose cases all carry a
number, and the lowering generates the two bodies (`ir/lower/derive.trb`) - `rawValue` a switch over the tag,
`fromRawValue` a switch over the number itself. A trait and not two free-standing generated members, because slice 2
needs a bound: `Flags<Case: RawValue>`, and so does any function of a program that takes such a type.

The rules the checker holds, each with a message at the case:

- all cases have a value or none has, and a case with fields never has one;
- two cases never have the same value;
- the values are integer literals, in every form an integer literal has (`-1`, `0x10`).

## 2. `Flags<Case>`

`Flags<Case>` is a value type of `std/collections` over one mask, with the vocabulary a `Set` already has
([COLLECTIONS.md](COLLECTIONS.md) section 6b): `contains`, `insert` and `inserted`, `insertAll`, `remove` and `removed`,
`removeAll`, `clear`, `Flags.union`, `Flags.intersection` and `Flags.difference` as `Set` has them, `length` and
`isEmpty`, and iteration over the cases that are set. It is written on top of the bit operators of `std/number` and
`rawValue`/`fromRawValue`, so it needs no new operator and no new native.

```trb check
use Flags from "std/collections"

type Permission {
  case Read = 1
  case Write = 2
  case Execute = 4
}

var access: Flags<Permission> = [.Read, .Write]
access.insert(.Execute)
if access.contains(.Write) {
  print "may write"
}
const mask = access.bits()
print mask
print Flags<Permission>.fromBits(5)
```

- **`bits()`** is the mask for a foreign function, a file or the network (`7` above), a `UInt64`.
- **`fromBits(mask)`** fails with a `FlagsError` when a bit belongs to no case.
- **Every value must be a power of two** for a type used as `Flags<Case>`, and the checker reports the first case that
  is not one where `Flags<Case>` is written (`requireFlagBits`).
- A list literal adapts to `Flags<Case>` as it adapts to every type that can be built from an `Iterate<Item>`: it comes
  `with From<Iterate<Case>>`.
- **It iterates in the order of the bits**, the lowest first - which is why it is not a `Set`, whose every
  implementation iterates in insertion order.

## 3. Interoperability

Outside the program the value is its number. `Flags` is a capsule - its mask is a `private` field - whose one
conversion pair is `TryFrom<UInt64, FlagsError>` and `UInt64` with `From<Flags<Case>>`, so `Encode` writes the mask as
a number and `Decode` reads one back through `tryFrom`, refusing a bit that stands for no case
([data or capsule](../language/types/data-or-capsule.md), rule 5). The list of the case names as an option of the
format is not built (section 6). In the C back end the mask is a field of a one-field struct; in PHP it will be an `int`,
in JavaScript a `number` (the bit operators of JavaScript are exact up to 32 flags; above that the mask is two halves).

## 4. Slices

| # | Slice | Needs | State |
|---|---|---|---|
| 1 | Cases with a fixed value: the parser, the checker's rules of section 1, `rawValue`/`fromRawValue` generated, both back ends | nothing | built 2026-09-28 |
| 2 | `Flags<Case>` in `std/collections`, the power-of-two rule in the checker, `Encode` and `Decode` | 1 | built 2026-09-28, the names of the cases as an encoding left |

## 5. The two open points, decided

| Question | Decision | Why |
|---|---|---|
| Do the names `rawValue` and `fromRawValue` survive the naming rule? | **Yes.** | Both are full words; `rawValue` is a noun for the value a case stands for, and `fromRawValue` is the `static fn fromX` factory the standard library writes everywhere else (`fromBytes`, `fromHex`, `fromSeed`, `fromUnixNanoseconds`). |
| May a case value be any integer type (`case Read: UInt8 = 1`), or always `Int`? | **Always `Int`.** | One type for every such type is what lets `RawValue` be one trait and `Flags` one implementation; the conversion to a narrower type is the ordinary `UInt8.tryFrom(value.rawValue())`, and a typed value can be added later without changing what `case Read = 1` means. |

## 6. Open

- **The names of the cases as an encoding of `Flags`** (`["Read", "Write"]` instead of `3`), as an option of the format:
  it needs a way for an `Encode` implementation to ask its format for an option, which `std/encoding` does not have.
- **The power-of-two rule reaches every written `Flags<Case>`**, and not a `Flags` whose type is only inferred
  (`Flags.of(Permission.All)`); such a value stores the bits of the number as they are.
