---
title: Type aliases
summary: "`type Name = Other` names an existing type rather than declaring a new one, and the two names are freely interchangeable - there is no separate `alias` keyword."
kind: reference
status: stable
order: 21
keywords:
  - type alias
  - transparent
  - alias
source:
  - CONCEPT.md#type-aliases
---

`type Name { ... }` declares a new type; `type Name = Other` names an existing one. There is no `alias` keyword,
because naming a type is not a different kind of declaration from making one - both start with `type`, and which one
follows tells them apart.

## Example

```trb check
type EntityId = Int
type Pair<Value> = (Value, Value)

fn process(id: EntityId): Int {
  id + 1
}

const raw: Int = 5
print process(raw)

const bounds: Pair<Int> = (1, 2)
print bounds
```

## Syntax

```text
type Name = Other                        an alias for an existing type
type Name<Params> = Other                a generic alias
public type Name = Other                 exported from its file
```

## Rules

1. **The right side of `type X =` is a type position**, the same position a `:` annotation or a `with` list uses.
   `const`/`var` have no meaning there and are not written.

2. **An alias is transparent: the alias and its target are the same type, freely interchangeable.** `process` above
   takes an `EntityId`; passing a plain `Int` where `raw` is declared type checks without a conversion.

3. **An alias can be generic.** `Pair<Value>` above stands for `(Value, Value)`, and `Pair<Int>` is `(Int, Int)`
   wherever it is used.

4. **An alias is an ordinary scoped name**: private to its file unless `public`, importable, and it can shadow a
   prelude name like any other name a file declares.

   ```trb check
   type Int = Int32

   const count: Int = 5
   print count
   ```

5. **A declared type has no target; only `type X =` has one.** `type EntityId { value: Int }` is a new type with one
   field named `value`, not an alias for `Int` - see [Distinct types](distinct-types.md) for what that form is for.

## What this is not

**An alias is not a distinct type.** Nothing stops an `Int` from being passed where an `EntityId` is expected, or the
other way around - the two names never disagree, because they name the same type.

```trb check
type EntityId = Int

fn process(id: EntityId): Int {
  id + 1
}

print process(5)
```

```trb error
type EntityId = Int

fn process(id: EntityId): Int {
  id + 1
}

print process("five")
// error: Expected `EntityId (Int64)`, found `String`
```

The error above names the alias together with its target, `EntityId (Int64)`, where the type was written as the alias:
the annotation of a binding, a parameter of a function, and a name bound with one of those. Everywhere else - a type
that a message works out rather than reads off an annotation - it is the target alone, because the checker holds the
alias for the message only and never in the type itself. The aliases of the standard library (`Int`, `Float`) are
not repeated: `Int64` is how every message spells an integer.

**Shadowing a prelude name is not rejected, even though it can be confusing.** `type Int = Int32` in rule 4 compiles;
nothing in the compiler warns that literals are still `Int64` regardless (see [Integers](integers.md)), so a file
that shadows `Int` this way is easy to misread.

## Related

- [Distinct types](distinct-types.md) - a single-field type, for when an alias should not be interchangeable.
- [Integers](integers.md) - why an integer literal's type does not follow an alias like `Int`.
- [Literal types](literal-types.md) - `type Name = "a" | "b"`, the same declaration form for a different right side.

