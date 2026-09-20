---
title: Type parameters
summary: A type parameter is declared in angle brackets after the name of a fn, type, trait or extend, and its name is written out like a type, never a single letter.
kind: reference
status: stable
order: 10
keywords:
  - generic
  - type parameter
  - angle brackets
  - turbofish
source:
  - CONCEPT.md#traits
  - examples/tour/src/02-functions.trb
---

A type parameter stands for a type that is filled in at each use: at each call for a `fn`, at each construction for a
`type`, at each implementation for a `trait`. It is declared once, in angle brackets, and used by name in the
signature or the body that follows.

## Example

```trb check
fn identity<Value>(value: Value): Value {
  value
}

fn empty<Item>(): List<Item> {
  []
}

const number = identity 5
const strings = empty<String>()
print "{number} {strings}"

type Pair<Left, Right> {
  left: Left
  right: Right
}

trait Convert<Target> {
  fn convert(self): Target
}

const pair = Pair 1, "one"
print pair
```

## Syntax

```text
fn <name><Params>(<parameters>): <Type> { ... }
type <Name><Params> { ... }
trait <Name><Params> { ... }
extend<Params> <Name><Args> [with <Trait>] [where <bounds>] { ... }

<Params> ::= <Param> ("," <Param>)*
<Param>  ::= <Name> [: <bound>] ["=" <default>]
```

A call or a construction that writes the arguments out uses the same angle brackets: `empty<String>()`,
`Pair<Int, String>(1, "one")`.

## Rules

1. **A type parameter is declared in angle brackets directly after the name**, before the parameter list of a `fn`,
   the body of a `type` or `trait`, or the target of an `extend`. `fn identity<Value>`, `type Pair<Left, Right>`.

2. **A type parameter's name is written out and capitalized like a type: `Item`, `Key`, `Value`, `Output`, `Failure` -
   never a single letter.** `List<Item>` and `Map<Key, Value>` read as what they hold; `List<T>` does not say
   anything a reader has to guess less about. The compiler accepts a single letter; the convention is enforced by
   the linter, not by the type checker.

3. **`extend` declares its own type parameters**, separate from the type it targets. `extend<Item> List<Item> with
   Show where Item: Show` introduces `Item` for this `extend` alone; a second `extend` of `List` introduces its own.

   ```trb check
   extend<Item> List<Item> where Item: Compare {
     fn largest(self): Item? {
       get(0).map { first => fold first { a, b => a.max b } }
     }
   }

   print([3, 1, 2].largest())
   ```

4. **A `type` or `trait` parameter can have a default; a `fn` parameter cannot declare one at all.**
   `trait Add<Other = Self, Output = Self>` means `with Add` is `Add<Self, Self>`, and nobody writes it out. A `fn`'s
   type arguments always come from the call, so a default on one would never be consulted, and the checker rejects the
   declaration itself:

   ```trb check
   fn empty<Item>(): List<Item> {
     []
   }

   const y: List<Bool> = empty()
   ```

   ```trb error
   fn empty<Item = String>(): List<Item> {
     []
   }

   const y: List<Bool> = empty()
   // error: A type parameter of a `fn` has no default
   ```

5. **`Self` is never declared as a type parameter.** Inside a `trait` or a `type`, `Self` already names the
   implementing type; a trait can use it as an ordinary type, including as an argument to another trait
   (`Accumulator<Item, Self>`).

## What this is not

**A type parameter is not a value, and a value is not a type parameter.** A parameter declared without `const` fills
in a type; filling in a number needs a `const` parameter instead, and the two cannot stand in for each other.

```trb check
type Buffer<const Size: Int> {
  data: Array<Int, Size>
}
```

```trb error
type Buffer<Size> {
  data: Array<Int, Size>
}
// error: `Size` is a type, and a value belongs here
```

## Related

- [Bounds](bounds.md) - restricting what a type parameter can be filled in with.
- [Inference](inference.md) - when a type argument can be left out.
- [Traits](../traits/traits.md) - `with Add<Self, Self>` and where a trait's own parameters come from.
- [No higher-kinded types](no-higher-kinded-types.md) - what a type parameter cannot be.
