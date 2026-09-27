---
title: Coming from TypeScript
summary: What carries over from TypeScript - literal unions, structural-looking optional chaining, declarative generics - and the four places TypeScript's type-level programming has no counterpart at all.
kind: contrast
status: stable
order: 50
keywords:
  - TypeScript
  - structural typing
  - any
  - unknown
  - conditional types
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#type-aliases
  - CONCEPT.md#types-values-and-reflection
---

TypeScript and TorbScript share `"tcp" | "udp"` almost keystroke for keystroke, and `?.`/`??` read the same in both.
That resemblance stops at the type checker's door: TypeScript types are shapes a value can happen to fit, and
TorbScript types are names a declaration gives itself. A model that has read a great deal of TypeScript writes
unions of real types, reaches for `any` when a shape is inconvenient to spell out, and expects a type parameter to be
computed from another type - none of which exists here.

## At a glance

| TypeScript | TorbScript | Why |
|------------|------------|-----|
| `interface Point { x: number; y: number }` | `type Point { x: Int; y: Int }` | fields alone do not make two types the same one |
| `type Status = "online" \| "offline"` | `type Status = "online" \| "offline"` | looks identical; only literals of one base type combine with `\|` |
| `string \| number` | a `type` with cases | there are no unions of types, only unions of literals |
| `any` | nothing to write | there is no dynamically typed value anywhere in the language |
| `unknown` | `JsonValue`, or a trait type such as `Show & Encode` | nothing is untyped; a document with no fixed shape is an ordinary ADT instead |
| `null` / `undefined` | `None` (`Value?` is `Option<Value>`) | one representation of absence, and it is a real generic type |
| `x?.y` | `x?.y` | looks the same; here it is `Option.map`/`flatMap`, not a check bolted onto every type |
| `x ?? y` | `x ?? y` | the same spelling; here it is the trait `OrElse`, and the right side has the wrapped `Value`'s type |
| `readonly x: number` | `x: Int` (no `var`) | a field is already `const` unless marked `var`; there is no separate modifier |
| `private _count` behind `get count()` | `protected var count: Int = 0` | write-protected: read everywhere, written only in the file of its type - `protected` never means "visible to subclasses", there is no inheritance |
| `Readonly<T>` | `const` on the binding | `readonly` freezes reassignment of one field; `const` freezes everything reachable through the binding |
| `T extends U ? A : B` | not expressible | generics are declarative; a type parameter is never computed from another type |
| `{ [K in keyof T]: ... }` | not expressible | types are not values, so nothing iterates over a type's keys |
| `get x() { return this._x }` | `fn x(): Int { ... }`, called `x()` | there are no computed properties; the parentheses say a method ran |
| `static of(...)` in a class | `static fn of(...)` | the same word, and it is the only way such a member is reached |
| a method that writes `this.x` | `var fn` | mutation is written on the declaration, and the caller needs a `var` path |
| `enum Color { Red, Green }` | `type Color { case Red; case Green }` | a case can carry data; a TypeScript enum member cannot |
| `interface Shape { area(): number }`, fit structurally | `trait Shape { fn area(): Float }`, given with `with`/`extend` | a type states which traits it has; nothing satisfies one by accident |
| `function identity<T>(x: T): T` | `fn identity<Value>(value: Value): Value` | the same inference model, without conditional or mapped types layered on top |
| `String.prototype.shout = ...` | `extend String { ... }`, named with `use String.shout from "acme/text"` | a member is added at compile time, and the file that uses a foreign one names it |
| `while (true)`, `for (;;)` | `loop { ... }` | the endless loop has a word of its own, and `while true` is an error |

## What changes in your code

### Structural typing becomes nominal typing

TypeScript accepts a value anywhere its shape fits, whether or not the two sides ever named each other. TorbScript
checks the name a declaration gave itself: two types with identical fields are still two types, and a function that
asks for one does not accept the other just because the fields line up.

```trb check
type Point {
  x: Int
  y: Int
}

type Vector {
  x: Int
  y: Int
}

fn length(from: Point): Int {
  from.x + from.y
}

print length(Point(x: 1, y: 2))
```

```trb error
type Point {
  x: Int
  y: Int
}

type Vector {
  x: Int
  y: Int
}

fn length(from: Point): Int {
  from.x + from.y
}

print length(Vector(x: 1, y: 2))
// error: Expected `Point`, found `Vector`
```

The one place the language does accept a value in place of a name it does not carry is a trait: `Point` and `Vector`
can both implement `Show`, and a function that asks for `Show` takes either one, because that coercion is spelled out
at the type's declaration (`with Show`) rather than inferred from what the type happens to contain. See
[Traits](../language/traits/traits.md).

### There are no unions of types, only unions of literals

`string | number` has no TorbScript equivalent. What survived from the same corner of TypeScript is narrower on
purpose: a union of literal values of one base type, told apart by value rather than by a type of their own.

```trb check
type Status = "online" | "offline" | "away"

var status: Status = "online"
status = "away"

const label = match status {
  "online" => "here"
  "offline" => "gone"
  "away" => "back soon"
}

print "{status} {label}"
```

Reaching for `|` between two real types is not a smaller version of that - it does not compile at all, because
nothing at runtime could tell the two apart:

```trb error
type Width = Int | String
// error: Only literals can be combined with `|` (`"online" | "offline"`). There are no unions of types: declare a type with cases
```

A `type` with cases is the tool once the values are not literals of the same base type, or once behavior has to
travel with them. See [Literal types](../language/values-and-types/literal-types.md).

### No `any`, and no `null` or `undefined` to fall back on

There is no `any` and no `unknown` that lets a value opt out of the type checker. A document whose shape is not
known ahead of time is `JsonValue`, an ordinary type with cases, and a value that could be many things but has to
stay checked is a trait type (`Show & Encode`) instead of a hole in the type system. See
[std/json](../standard-library/json.md).

Absence works the same way: there is no `null`, no `undefined`, and therefore no third state a value can silently
fall into. `Option<Value>` is the one representation: a value wraps into it where one is expected, and nothing ever
unwraps without saying so:

```trb check
type User {
  name: String
  manager: User? = None
}

fn findUser(id: Int): User? {
  if id == 1 { User "Ada" } else { None }
}

const managerName: String? = findUser(2)?.manager?.name
const name = findUser(1)?.name ?? "anonymous"
print "{managerName} {name}"
```

```trb error
const found: Int? = 3
const plain: Int = found
// error: Expected `Int64`, found `Option<Int64>`
```

`?.` reads exactly like TypeScript's optional chaining, but it produces an ordinary `Option` and nothing short-circuits
the rest of the expression the way `a?.b?.c` collapses straight to `undefined` in TypeScript. `??` reads the same way and
is a trait, `OrElse`: `a ?? b` is the method call `a.orElse(b)`, so `Option`, `Result` and a type of your own can have it
and an `Int` cannot. See [Optional chaining](../language/errors/option-chaining.md).

### Generics stay declarative

TypeScript's generics can compute: a conditional type branches on another type, a mapped type walks `keyof T`, and
`infer` pulls a type out of a pattern. None of that exists here, because a type is never a compile-time value - a
generic function or type is checked once, at its own declaration, against the bounds it names, the same way it would
be for one concrete type:

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
```

CONCEPT.md's own note on this trade-off names the TypeScript-shaped cost directly: were the right side of a
declaration a compile-time value, "generics become functions over types," a call like `numbers.map { _ * 2 }` could
no longer infer its result from unification, and bounds could only be checked once a generic was actually used
rather than where it was declared. Declarative generics with trait bounds are what TorbScript keeps instead, at the
price of `keyof`, mapped types and conditional types having nothing to attach to. See
[Type aliases](../language/values-and-types/type-aliases.md) and
[No higher-kinded types](../language/generics/no-higher-kinded-types.md) for the related thing a type parameter
still cannot express: a type constructor with a hole in it.

## Habits to unlearn

These are things TypeScript has that TorbScript deliberately does not, and what replaces them.

- **Type assertions (`value as Type`, `<Type>value`).** Nothing bypasses the checker. A conversion that can fail is
  `TryFrom` - from a number or from text - and it answers a `Result` instead of trusting the assertion. See
  [Conversions](../language/types/conversions.md).
- **The non-null assertion `!`.** There is no operator that turns an `Option` back into its value on the promise that
  it is not `None`. `expect(message)` does the same job and names why the absence would be a bug.
- **Utility types (`Partial<T>`, `Pick<T, K>`, `Omit<T, K>`).** None of them exist, because none of them could: a
  type is not a value here, so nothing computes a second type by adding, narrowing or removing fields of a first
  one. A type that needs fewer or optional fields than another is written out as its own declaration.
- **Index signatures (`{ [key: string]: number }`).** An object whose keys are not known ahead of time is a
  `Map<String, Value>`, an ordinary generic collection - never a second kind of object type.
- **Optional properties on an object type (`x?: number`), distinct from a nullable field (`x: number | null`).**
  There is exactly one way for a field to be absent, `Value?`, and no second spelling next to it for "the key itself
  might not be there."
- **`declare` and ambient types for an untyped JavaScript boundary.** There is no boundary that is trusted without
  being checked. A foreign function is declared against the real C ABI with `foreign`, and both back ends agree on
  what it means - see [Foreign functions](../language/extensibility/foreign-functions.md).
- **Patching a built-in through its prototype.** `String.prototype.shout = ...` reaches every string of the running
  program from wherever it ran. `extend String { ... }` adds the member at compile time, and a member another package's
  `extend` adds to a type it does not own is named by the file that calls it -
  `use String.shout from "acme/text"`, with `as` for a local name. What the package of the type itself attaches needs no
  import at all. See [extend](../language/traits/extend.md).
- **`while (true)` and `for (;;)`.** The endless loop is `loop { ... }`, and `while true` is the error "A loop that never
  ends is written `loop`". Its type is `Never` while no `break` targets it - so nothing after it is reached - and `Void`
  once one does, and a `break` carries no value.
- **Overload signatures over one implementation.** A list of `declare function` overloads in front of one body has no
  counterpart: a name means one declaration. An argument that may be one of several *types* is a trait with a
  parameter or a `type` with cases, and an optional argument is a default parameter. See
  [where are my overloads](where-are-my-overloads.md).
- **Enum members with an implicit numeric value.** A case is never secretly a number; it either carries the fields it
  declares or none at all, and it prints by name through the generated `Show`, not through an ordinal nobody wrote
  down. See [Cases and match](../language/pattern-matching/cases-and-match.md).

## Related

- [Where are my overloads](where-are-my-overloads.md) - one signature per call, and the two forms that replace an overload set.
- [Traits](../language/traits/traits.md) - what does travel between unrelated types, and how it is spelled out.
- [Option](../language/values-and-types/option.md) - `Some`/`None`, the one representation of absence.
- [Literal types](../language/values-and-types/literal-types.md) - the exact reach of `|` in this language.
- [Type parameters](../language/generics/type-parameters.md) - what a type parameter can stand for.
- [Coming from Kotlin](coming-from-kotlin.md) - the other language whose `?` looks like this one's and works
  differently underneath.

