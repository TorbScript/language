---
title: Why there are no bit operators
summary: Both `&` and `|` already mean something else, so bitwise work is a method of the Bits trait instead of a symbol, and only UInt64 gets the wrapping arithmetic a hash function needs.
kind: explanation
status: stable
order: 130
keywords:
  - Bits
  - bitwise
  - wrapping arithmetic
  - overflow
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#built-in-types
  - CONCEPT.md#traits
---

`&`, `|`, `^`, `<<` and `>>` are five tokens every C-family language spends on bit manipulation. TorbScript spends
none of them there, and this page argues for why the language is not missing bit operations, only the operators.

## The decision

**There are no bit operators.** The integer types come `with Bits` instead, a trait whose members are ordinary
methods: `bitwiseAnd`, `bitwiseOr`, `bitwiseExclusiveOr`, `bitwiseNot`, `shiftedLeft(by:)` and `shiftedRight(by:)`.

- `UInt64` additionally has `addedWrapping` and `multipliedWrapping` - the only arithmetic in the language that does
  not panic on overflow.
- A shift by a negative amount or by the width of the type or more panics, the same way an out-of-range access does
  anywhere else.

```trb check
const value: UInt8 = 0b1010_1100
const masked = value.bitwiseAnd 0b0000_1111
const shifted = value.shiftedLeft by: 2
print "{masked} {shifted}"
```

## Why

**Because `&` and `|` already mean something in this language, and a second meaning per symbol is exactly the
ambiguity operators are supposed to remove.** `&` intersects traits (`Show & Encode`) and `|` unions literal types
(`"tcp" | "udp"`); adding a bitwise reading of the same two tokens would mean the parser - and every reader - decides
which meaning applies from the types on either side, the kind of context-sensitivity the language avoids everywhere
else (see [generics-or-comparison](../language/syntax/generics-or-comparison.md) for the one place `<>` already needs
a rule like this, and why a second one was not wanted).

**Because a method needs no precedence rule and no new token.** `<<` and `>>` would need a place in the operator
precedence table relative to `+`, `-`, `==` and every other operator that already exists; `shiftedLeft(by: 2)` needs
none, because a method call already has one, fixed precedence - tighter than every operator - and nothing to argue
about.

**Because without the methods, several things a scripting language's own toolchain needs could not be written in
TorbScript at all.** A hand-written `Hash` implementation, decoding UTF-8 byte by byte, the bytecode format a build
cache hashes, and the wrapping addition a hash function needs all require bit-level and overflow-tolerant arithmetic.
Removing the operators without adding the methods would have made those things `native`-only; the `Bits` trait keeps
them writable in the language that has to write its own standard library.

**Because keeping the wrapping pair off the signed types keeps "overflow panics" true wherever `+` is written.**
Design Principle 5 and the built-in numeric types agree that overflow is a bug and panics rather
than wrapping silently; `addedWrapping`/`multipliedWrapping` exist on `UInt64` alone; because a hash function cannot be
written without wrapping arithmetic somewhere, and giving every integer type a wrapping escape hatch would have made
"overflow panics" a claim with an asterisk on every one of them instead of a fact.

### What was rejected

- **Bitwise operators reusing `&`, `|`, `^`, `<<`, `>>`.** Rejected because `&` and `|` are already spoken for and a
  new precedence table would be needed for the rest, for an operation that a method already covers with no ambiguity.
- **A wrapping arithmetic escape hatch on every integer type.** Rejected because it would weaken "overflow panics"
  everywhere instead of in the one place - `UInt64`, for hashing - that genuinely needs it.

## Consequences

**Bitwise work reads as method calls, which chain like any other method call.**

```trb check
const flags: UInt32 = 0
const withRead = flags.bitwiseOr 0b0001
const withWrite = withRead.bitwiseOr 0b0010
print withWrite.bitwiseAnd(0b0001)
```

**There is no `&`/`|` fallback for masking, even where it would read naturally**, because those tokens are already
spoken for by traits and literal types:

```trb error
fn masked(value: Int): Int {
  value & 0xFF
}
// error: Expected the end of the statement, found `&`
```

**A hash implementation is ordinary TorbScript, not a `native` body**, because `Bits` gives it everything it needs:

```trb check
type Point {
  x: Int
  y: Int

  fn hash(): Int {
    x.bitwiseExclusiveOr y.shiftedLeft(by: 1)
  }
}

print Point(x: 3, y: 5).hash()
```

## Related

- [Trait intersections](../language/traits/intersections.md) - what `&` means between two traits.
- [Literal types](../language/values-and-types/literal-types.md) - what `|` means between two literals.
- [Integers](../language/values-and-types/integers.md) - the full `Bits` method list and the overflow rules.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the bit-operator mistake, with the
  diagnostic.

