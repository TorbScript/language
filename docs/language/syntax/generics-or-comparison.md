---
title: Angle brackets or comparison
summary: A `<` after a name starts a type argument list only if what follows parses as types up to a matching `>` that is itself followed by a token a comparison could not have.
kind: reference
status: stable
order: 7
keywords:
  - type arguments
  - less than
  - non-associative
  - ambiguity
source:
  - CONCEPT.md#lexical-structure
---

`load<Config>(path)` and `a < b` both start with a name and a `<`. The parser tells them apart without knowing what
any of the names mean: it tries to read a type argument list, and falls back to "less than" the moment that reading
does not work out.

## Example

```trb check
fn identity<Value>(value: Value): Value {
  value
}

const a = 1
const b = 2

print identity<Int>(5)
print(a < b)
```

## Syntax

```text
name<Type, Type>               a type argument list, when the reading below succeeds
name < value                   "less than", otherwise
```

## Rules

1. **After a name, `<` is tried as the start of a type argument list first.** The parser reads what follows as one or
   more types, separated by commas, up to a `>`.

2. **The attempt only succeeds if the token right after the matching `>` could not continue a comparison.** That
   token has to be one of `(`, `)`, `]`, `{`, `}`, `:`, `,`, `.`, `?`, `==`, `!=`, or the end of the line. Anything
   else - most importantly another comparison operator - means the `<` was "less than" all along, and the parser
   backs out and re-reads the whole thing as an expression.

3. **A comparison never loses a valid expression to this reading**, because comparisons do not chain: `a < b > c` is
   never a legal comparison in the first place, so the generic reading cannot be mistaken for one that would have
   parsed.

   ```trb error
   const a = 1
   const b = 2
   const c = 3
   print a < b > c
   // error: Comparisons do not chain. Use `&&`: `a < b && b < c`
   ```

4. **In a type position there is no ambiguity to resolve.** After `:`, `with`, `where`, or inside another type's angle
   brackets, `<` always starts a type argument list, because "less than" is not an expression a type position could
   contain.

5. **The decision is made without looking up what a name refers to.** `identity<Int>` reads as a type argument list
   because `Int` parses as a type, whether or not `identity` turns out to be generic; a wrong reading here is a type
   error at the call, not a different parse.

## What this is not

**Whitespace is not part of the decision.** `identity<Int>(5)` and `identity <Int> (5)` are the same call; the
choice between a type argument list and "less than" comes only from what follows the `<`, never from where the
spaces are.

```trb check
fn identity<Value>(value: Value): Value {
  value
}

print identity <Int> (5)
```

**A missing closing `>` is not read as an open comparison, either: it falls back to "less than" the moment the type
reading cannot finish**, which is what makes `total < Int` below an ordinary comparison rather than a stalled
attempt at a type argument list.

```trb error
const total = 1
const flag = total < Int
print flag
// error: Expected `Int64`, found `() => Int64`
```

## Related

- [Naming](naming.md) - why a type parameter is written out (`Value`), not abbreviated (`T`).
- [Declaring a type](../types/declaring-a-type.md) - where the generic parameters of a type are declared.
- [Syntax cheat sheet](cheat-sheet.md) - every declaration and expression form on one page.
