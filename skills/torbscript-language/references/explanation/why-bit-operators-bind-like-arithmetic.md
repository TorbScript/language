---
title: Why the bit operators bind like arithmetic
summary: The integer types have `&`, `|`, `^`, `~`, `<<` and `>>` on the levels Go and Swift give them, so `x & 1 == 0` is the test it looks like, and they are the members of the Bits trait the way `+` is Add.
kind: explanation
status: stable
order: 130
keywords:
  - Bits
  - bitwise
  - bit operators
  - precedence
  - wrapping arithmetic
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#built-in-types
  - CONCEPT.md#traits
---

`&`, `|`, `^`, `~`, `<<` and `>>` are the bit operators of every C-family language, and TorbScript has them too. What
it does not have is C's precedence for them, and this page argues for where they sit instead.

## The decision

**The bit operators are the members of `Bits`, on the levels Go and Swift give them.** `a & b` is `bitwiseAnd`,
`a | b` `bitwiseOr`, `a ^ b` `bitwiseExclusiveOr`, `~a` `bitwiseNot`, `a << n` `shiftedLeft(by:)` and `a >> n`
`shiftedRight(by:)` - exactly as `a + b` is `Add.add`.

- `&` binds like `*`, `|` and `^` like `+`, a shift tighter than `*` and looser than `**`, and `~` is a prefix like `-`.
- A shift of a signed type is arithmetic, one of an unsigned type logical, and the bits that leave the width are
  dropped; a shift by a negative amount or by the width of the type or more panics.
- `UInt64` additionally has `addedWrapping` and `multipliedWrapping` - the only arithmetic in the language that does
  not panic on overflow.

```trb run
const value: UInt8 = 0b1010_1100
const masked = value & 0b0000_1111
const shifted = value << 2
print "{masked} {shifted} {value & 1 == 0}"
// prints 12 176 true
```

## Why

**Because C's precedence is a trap every C programmer has stepped into.** In C, `x & 1 == 0` is `x & (1 == 0)`,
because the bit operators bind looser than the comparisons. Putting `&` on the level of `*` and `|` and `^` on the level
of `+` - the order of Go and Swift - makes the line mean what it says, and a reader never needs parentheses to find out.

**Because an operator is a trait exactly when it is a method call.** The members of `Bits` stay, as the members of
`Add` stay behind `+`: a body that is generic over `Bits` writes `a & b` and reaches the implementation of its type, and
the methods remain the spelling for a reference to one (`values.map(Int.bitwiseNot)`).

**Because the parser already reads a type, a pattern and an expression apart.** `&` intersects traits and `|` unions
literal types - in a type. `|` joins alternatives - in a pattern. In an expression they are the bit operators, and no
position is shared, so no reader decides a meaning from the types on either side. `>>` is two `>` with nothing between
them, so `List<List<Int>>` still closes two brackets.

**Because keeping the wrapping pair off the signed types keeps "overflow panics" true wherever `+` is written.** A hash
function cannot be written without wrapping arithmetic somewhere, and `UInt64` is where it lives; a wrapping escape
hatch on every integer type would have made "overflow panics" a claim with an asterisk on every one of them.

### What was rejected

- **C's precedence.** Rejected for the trap above.
- **No operators at all, only the methods.** That was the rule until 2026-09: `value.bitwiseAnd(0xFF)` needs no
  precedence, but a hash function, a UTF-8 decoder and a bytecode encoder are made of these operations, and as methods
  they read like a chain of calls rather than the formula they are.
- **`^` for a power.** A power is `**`; `^` is exclusive or, as in every language that has bit operators, and a float -
  which has no bits to combine - is told where the power is.

## Consequences

**A mask, a flag test and a hash read as the formulas they are.**

```trb check
type Point {
  x: Int
  y: Int

  fn hash(): Int {
    x ^ y << 1
  }
}

const flags: UInt32 = 0b0001 | 0b0010
print(flags & 0b0001 != 0)
print Point(x: 3, y: 5).hash()
```

**A float has no `^`, and hears that a power is `**`:**

```trb error
const wrong = 2.0 ^ 10.0
print wrong
// error: `Float64` has no `^`: it is the exclusive or of two integers
```

## Related

- [Operators are traits](../language/traits/operators.md) - every operator, its trait, and the precedence table.
- [Trait intersections](../language/traits/intersections.md) - what `&` means between two traits.
- [Literal types](../language/values-and-types/literal-types.md) - what `|` means between two literals.
- [Integers](../language/values-and-types/integers.md) - the widths, the shifts and the overflow rules.

