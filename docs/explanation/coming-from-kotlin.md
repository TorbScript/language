---
title: Coming from Kotlin
summary: What carries over from Kotlin - when expressions, extension functions, a nullable-looking ? - and the three places nullability, data classes and DSL receivers work on a different mechanism underneath.
kind: contrast
status: stable
order: 40
keywords:
  - Kotlin
  - nullable
  - data class
  - DSLMarker
  - sealed class
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#configuration-dsl
  - CONCEPT.md#error-handling
---

Kotlin and TorbScript read alike on the surface: `when` looks like `match`, an extension function looks like
`extend`, and `?` after a type looks exactly like Kotlin's own nullable marker. The surface hides that `T?` is a
compiler-tracked modifier on `T` in Kotlin and an ordinary generic type here, that `val` freezes less than it looks
like it does, and that a Kotlin habit for building DSLs solves a problem TorbScript's single implicit receiver never
has in the first place.

## At a glance

| Kotlin | TorbScript | Why |
|--------|------------|-----|
| `val x = 1` / `var x = 1` | `const x = 1` / `var x = 1` | `val` freezes the binding only; `const` freezes what it reaches too |
| `val list = mutableListOf(1)` | `var list = [1]` | Kotlin needs a second type, `MutableList`, for what a `var` binding already says here |
| `String?` | `String?` (`Option<String>`) | the sugar matches; underneath, one is a compiler-tracked modifier and the other an ordinary generic type |
| `x ?: fallback` | `x ?? fallback` | the same idea, spelled as the trait `OrElse`, whose fallback is `lazy` |
| `x?.length` | `x?.byteLength()` | `?.` is literally `Option.map`/`flatMap` here, not a null check the compiler special-cases per type |
| `x!!` | nothing to write | there is no force-unwrap operator; `expect(message)` panics with a message you chose |
| `data class Point(val x: Int, val y: Int)` | `type Point { x: Int; y: Int }` | every `type` gets `Equals`, `Hash`, `Show` and `copy`, not only ones marked `data` |
| `sealed class Shape` plus subclasses | `type Shape { case Circle(radius: Float) }` | one type, cases instead of a subclass per variant |
| `when (shape) { is Circle -> ... }` | `match shape { .Circle(radius) => ... }` | one keyword, and the compiler proves every arm is covered |
| `fun Int.double() = this * 2` | `extend Int { fn double(): Int { self * 2 } }` | `extend` always names the type; there is no bare receiver on a lone function |
| `import acme.text.shout` | `use String.shout from "acme/text"` | the import carries the type the member hangs on |
| `while (true) { ... }` | `loop { ... }` | the endless loop has a word of its own, and `while true` is an error |
| `String.() -> Unit` | `(self: Receiver) => Void` | the receiver is a named, typed parameter, not a distinct function-type syntax |
| `@DslMarker` | nothing to write | only the innermost receiver is ever implicit |
| `interface Shape { fun area(): Double }` | `trait Shape { fn area(): Float }` | one word, `trait`; a default method works the same way |
| a `fun` in a class that writes to a `var` field | `var fn` | mutation is on the declaration, so a reader sees it without the body |
| `companion object { fun of() }` | `static fn of()` | one word instead of a nested object, and it is reached as `Type.of()` |
| `Comparable<T>` | `Compare` | a single-method trait is named after its method |
| `object Registry { }` | a `shared type` plus one instance you hold | identity is written down, not a keyword that hides an allocation |
| `Result<T>` (wraps `Throwable`) | `Result<Value, Failure>` | the failure type is named at the signature, never fixed to one base type |

## What changes in your code

### Nullability is a type, not a modifier the compiler tracks for you

Kotlin's `?` changes the type itself: `String` and `String?` are related by subtyping, and the compiler narrows
`String?` to `String` after a null check (a smart cast) without anything happening at runtime. TorbScript's `Value?`
is sugar for an ordinary generic type, `Option<Value>`, and nothing about it is compiler magic: there is no
subtyping between `Value` and `Value?`, and no narrowing - you take the value out with `?.`, `??`, `expect`, or a
`match`:

```trb check
type User {
  name: String
  manager: User? = None
}

fn findUser(id: Int): User? {
  if id == 1 { Some User("Ada") } else { None }
}

const managerName: String? = findUser(2)?.manager?.name
const name = findUser(1)?.name ?? "anonymous"
print "{managerName} {name}"
```

Because wrapping is never implicit, a plain value is not accepted where an `Option` is expected, even where Kotlin's
subtyping would let a non-null `String` stand in for `String?` without a second thought:

```trb error
const found: Int? = 3
// error: Expected `Option<Int64>`, found `Int64`
```

There is also no `!!`. A `?.` chain that reaches the end of an `Option` produces `None`, not a thrown
`NullPointerException`, and there is no operator that turns absence back into a crash on the spot - `expect(message)`
is the closest thing, and it names the message instead of leaving Kotlin's default one to explain itself. See
[Optional chaining](../language/errors/option-chaining.md).

### `val` freezes the binding; `const` freezes what it reaches

`val list = mutableListOf(1)` is legal Kotlin, and `list.add(2)` runs anyway: Kotlin's immutability is a property of
the *type* (`List` against `MutableList`), so a `val` of a mutable type is still mutable through every method that
type has. TorbScript has one collection trait per kind and no mutable twin - `const` and `var` are the only two
words, and they decide everything:

```trb
const answer = 42
var counter = 0
counter = counter + 1

var list = [1, 2]
list.append 3
const fixed = list

print "{answer} {counter} {list} {fixed}"
```

`fixed` is a `const` binding to the same list `list` was, made after `list` had already grown to three elements; from
here nothing about it can change, through any method:

```trb error
const fixed = [1, 2]
fixed.append 3
// error: `append` needs a `var`
```

There is no `MutableList` to reach for instead - the trait `list` and `fixed` both have is the same one, `List`, and
the binding is the whole answer to whether it can be changed. See [Bindings](../language/values-and-types/bindings.md).

### Every type gets what `data class` opts in to

A Kotlin `class` compares by reference and prints its memory address unless it is marked `data`, which then
generates `equals`, `hashCode`, `toString`, `copy` and the `componentN` functions destructuring reads on. TorbScript
generates the equivalent four - `Equals`, `Hash`, `Show`, `copy` - for every `type`, with no modifier to add, as long
as every field itself supports them:

```trb run
type Employee {
  name: String
  salary: Int = 50_000
}

const alice = Employee name: "Alice"
const raise = alice.copy(salary: alice.salary + 5_000)
print raise                   // prints Employee(name: "Alice", salary: 55000)
print(alice == alice.copy())  // prints true
```

That prints `Employee(name: "Alice", salary: 55000)` and then `true` - `copy()` with no arguments is a plain
duplicate, and `Equals` compares content the way `data class` would, without a modifier deciding whether it exists.
See [Copy and equality](../language/types/copy-and-equality.md).

### One implicit receiver removes the reason for `@DslMarker`

Kotlin's builder style stacks lambdas with receivers (`apply`, a custom DSL function, another one nested inside), and
because every enclosing receiver stays reachable by an unqualified name, a name meant for the inner block can
silently resolve against an outer one - which is what `@DslMarker` exists to forbid. TorbScript never has the
problem to begin with: exactly one receiver is implicit at a time, in a method as in a receiver closure, so nesting
one builder inside another never adds the outer receiver to what a bare name can mean in the inner one:

```trb check
type Counter {
  var value: Int = 0

  var fn add(amount: Int) {
    value = value + amount
  }
}

fn build(configure: (var self: Counter) => Void): Counter {
  var counter = Counter()
  configure counter
  counter
}

const counter = build {
  add 3
  add 4
}

print counter.value
```

CONCEPT.md's design lets a nested receiver closure reach an outer one by naming its parameter
(`server { s => s.database { url "{s.host}/db" } }`), which is the one annotation-free way in; today's checker
rejects that shape as a closure that might outlive the call, a gap tracked on
[Receiver closures](../language/configuration/receiver-closures.md#rules). What both agree on already is that
`extend` never lets a function pick a bare, untyped receiver the way `fun Int.double()` does - the type comes first,
always:

```trb
extend String {
  fn shout(): String {
    "{toUpperCase()}!"
  }
}

print "hello".shout()
```

That `extend` stands in this file, so `shout` needs nothing further: a file sees what its own package declares, and what
the package of the *type* attached is part of the type everywhere. The line Kotlin writes as `import acme.text.shout`
carries the type here - `use String.shout from "acme/text"`, one member at a time, with `as` for a local name
(`use String.shout as yell from "acme/text"`) where two packages both add `shout` to `String`. A member a *trait* puts on
a foreign type (`extend String with Slug`, written where `Slug` lives) asks for the trait instead, which is why `??`,
`for`, interpolation and `into()` ask for nothing at all: their traits are in the prelude.

## Habits to unlearn

These are things Kotlin has that TorbScript deliberately does not, and what replaces them.

- **`!!` and platform types.** There is no force-unwrap operator and no third, unchecked flavor of a type from an
  untyped boundary. A value that can be absent is always `Value?`, and it is always unwrapped on purpose.
- **Smart casts (`if (x is Foo)` narrowing `x`).** Nothing narrows a binding's type after a check. Take the value
  apart with `match`, or bind into the shape you need with `if const Some(x) = ...`; see
  [Cases and match](../language/pattern-matching/cases-and-match.md).
- **`lateinit var` and two-phase initialization.** A binding needs a value the moment it is declared - there is no
  implied default and no "set it before first use, I promise." See [Bindings](../language/values-and-types/bindings.md).
- **Stacked scope functions (`apply`, `run`, `with`, `also`, `let`).** There is one receiver-closure form and one
  implicit receiver at a time; `do { ... }` is the plain "run this block now" of the standard library, with no
  implicit receiver of its own.
- **`suspend fun` and coroutines.** There is no `suspend` keyword. A function that answers `Task<Value>` may call
  `await()`, the same way a function that answers `Result` may use `?` - but `Task`, like the rest of concurrency, is
  `status: planned`: it type checks today and no back end runs it yet. See [Tasks](../language/concurrency-and-streams/tasks.md).
- **`object` singletons.** There is no keyword that hides an allocation behind a name. A `shared type` still needs
  someone to construct the one instance and hand it out; see [Shared types](../language/types/shared-types.md).
- **`while (true)` as the endless loop.** `loop { ... }` is the word for one, and `while true` is the error "A loop that
  never ends is written `loop`". Its type is `Never` while no `break` targets it - so a function whose body is one needs
  no other result - and `Void` once one does; a `break` carries no value, and `continue` reads as it does in a `while`.
- **`?:` as a built-in operator.** `a ?? b` is the method call `a.orElse(b)`, so it is the trait `OrElse`: `Option`,
  `Result` and a type of your own come with it, and a type that does not hears so at the operator. See
  [Operators](../language/traits/operators.md).
- **Overloading by parameter type, and an extension function that reads like a member of anything.** A name means one
  declaration, and an extension is an `extend` on a named type whose members a calling file imports by name. A second
  argument type is a trait with a parameter, and a second arity is a default parameter. See
  [where are my overloads](where-are-my-overloads.md).
- **An `else` branch to satisfy `when`'s exhaustiveness checker.** `match` on a `type` with cases is exhaustive by
  construction, checked against the declaration, not by falling back to a catch-all arm you added out of caution. An
  arm that can never run is an error, not dead code a linter might mention.

## Related

- [Where are my overloads](where-are-my-overloads.md) - one signature per call, and the two forms that replace an overload set.
- [Optional chaining](../language/errors/option-chaining.md) - `?.` and `??` in full.
- [Option](../language/values-and-types/option.md) - `Some`/`None`, and why nothing wraps into it implicitly.
- [Receiver closures](../language/configuration/receiver-closures.md) - the one-implicit-receiver rule in full.
- [extend](../language/traits/extend.md) - what an extension function becomes, and where its members are named.
- [Copy and equality](../language/types/copy-and-equality.md) - what is generated for every `type`.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - cases instead of a `sealed class` hierarchy.
- [Coming from Swift](coming-from-swift.md) - the other language whose enums and protocols read close to this one.
