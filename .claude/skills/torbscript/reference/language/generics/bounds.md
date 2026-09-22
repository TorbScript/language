---
title: Bounds
summary: A bound restricts a type parameter to types that implement one or more traits, written inline or after where, and a member can carry a bound of its own that is not a requirement on every implementor.
kind: reference
status: stable
order: 20
keywords:
  - bound
  - where
  - inline bound
  - constraint
source:
  - CONCEPT.md#traits
  - std/iteration/src/iteration.trb
---

A bound says which traits a type parameter's type has to implement. Without one, nothing is known about the type
except that it is one - not even `==` is available on it.

## Example

```trb check
fn largest<Item: Compare>(values: List<Item>): Item? {
  values.get(0).map { first => values.fold first { a, b => a.max b } }
}

fn printSorted<Item>(items: List<Item>) where Item: Compare & Show {
  const ordered = items.sorted { _ }
  for item in ordered {
    print item
  }
}

printSorted([3, 1, 2])
print largest([3, 1, 2])
```

## Syntax

```text
fn <name><Item: <Trait>>(...)                 inline, one parameter
fn <name><Item>(...) where Item: <Trait>      after where, same meaning
fn <name><A, B>(...) where A: <Trait>, B: <Trait> & <Trait>   several parameters, `&` combines traits
fn <name>(...): <Type> where Item: <Trait>                    a member's own bound
```

## Rules

1. **A bound is written inline, right after the parameter's name (`<Item: Compare>`), or after `where`
   (`where Item: Compare`).** The two forms mean the same thing; `where` is what several parameters or several
   traits on one parameter read best with.

2. **Several traits on one parameter combine with `&`.** `where Item: Compare & Show` is the same `&` that
   intersects traits in a type position; see [Trait intersections](../traits/intersections.md).

3. **`where` can carry the bounds of more than one parameter, separated by `,`.**

   ```trb fragment
   fn zip<Left, Right>(lefts: List<Left>, rights: List<Right>): List<(Left, Right)>
     where Left: Show, Right: Show
   ```

4. **A member of a trait can carry a `where` clause of its own, and then it is not a requirement on every
   implementor - it exists only where the clause holds.** `Iterate<Item>` declares
   `fn toSet(): Set<Item> where Item: Hash`, so every `Iterate` has the method, but calling it is only legal
   where `Item` happens to be `Hash`:

   ```trb check
   const numbers: List<Int> = [3, 1, 2]
   print numbers.toSet()
   ```

   ```trb error
   type Task {
     action: () => Void
   }

   const tasks: List<Task> = []
   const unique = tasks.toSet()
   // error: `Task` does not implement `Hash`
   ```

5. **A bound is checked at the call, not inside the declaration that asks for it.** `largest<Item: Compare>` type
   checks its own body against exactly the members `Compare` promises, and a call that cannot supply a `Compare`
   type is rejected at the call.

   ```trb check
   fn largest<Item: Compare>(values: List<Item>): Item? {
     values.get(0).map { first => values.fold first { a, b => a.max b } }
   }

   print largest([3, 1, 2])
   ```

   ```trb error
   fn largest<Item: Compare>(values: List<Item>): Item? {
     values.get(0).map { first => values.fold first { a, b => a.max b } }
   }

   type Task {
     action: () => Void
   }

   const tasks: List<Task> = []
   print largest(tasks)
   // error: `Task` does not implement `Compare`
   ```

## What this is not

**A bound is not a cast, and it does not narrow what a value already is.** Inside `largest<Item: Compare>`, `item`
stays an `Item` - the bound only adds the members of `Compare` to what can be called on it, it does not turn `item`
into a `Compare`-typed value. Nothing about `largest` changes when it is called with an `Int` instead of some other
`Compare` type, and nothing inside it can tell the two apart.

```trb check
fn describe<Item: Show>(value: Item): String {
  "the value is {value}"
}

print describe(5)
```

```trb error
trait Loud {
  fn shout(): String
}

type Silent {
  loud: Loud
}

type Robot with Loud {
  fn shout(): String {
    "BEEP"
  }
}

fn describe<Item: Show>(value: Item): String {
  "the value is {value}"
}

print describe(Silent(Robot()))
// error: `Silent` does not implement `Show`
```

## Related

- [Type parameters](type-parameters.md) - where a parameter is declared, before it is bounded.
- [Trait intersections](../traits/intersections.md) - the `&` a bound and a type position share.
- [Traits as types](../traits/trait-types.md) - a bound compared to a trait used as a parameter's own type.
- [Inference](inference.md) - what a bound does and does not help the compiler infer.

