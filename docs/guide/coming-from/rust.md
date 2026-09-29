---
title: Coming from Rust
summary: The 15 things that map directly from Rust, the 5 that will surprise you, and what Rust has that TorbScript deliberately does not.
kind: contrast
status: stable
order: 10
keywords:
  - Rust
  - borrow
  - Err
  - impl
  - match
---

Rust is the closest relative here: traits instead of inheritance, exhaustive `match`, `Option` and `Result` instead of
`null` and exceptions, no garbage collector. That closeness is the trap - the syntax is close enough that a Rust habit
often parses and compiles into something that means the wrong thing.

## At a glance

| Rust | TorbScript | Why |
|------|------------|-----|
| `let x = 1` / `let mut x = 1` | `const x = 1` / `var x = 1` | `const` freezes the whole value, not just the name |
| `&T`, `&mut T` | a copy, or a `var` parameter for one call | references are second-class; there are no lifetimes |
| `Ok(v)` / `Err(e)` | `Ok value` / `Fail problem` | `Error` is the trait's name, so the case is `Fail` |
| `Shape::Circle(r)` | `Shape.Circle(r)` or `.Circle(r)` | `.` for everything |
| `impl Trait for Type { }` | `extend Type with Trait { }` | `with` is the only word for "implements" |
| `fn area(&self)` / `fn grow(&mut self)` | `fn area()` / `var fn grow()` | the receiver is never a parameter |
| `fn new() -> Self` | `static fn of(): Self` | `static` marks it, not the shape of the signature |
| `struct`, `enum` | `type` | one keyword for all data |
| `#[derive(Clone, PartialEq, Hash, Debug)]` | nothing to write | `Equals`, `Hash`, `Show`, `copy` are generated |
| `Vec<T>`, `HashMap<K, V>` | `List<Item>`, `Map<Key, Value>` | the type is a trait, the implementation is a name |
| `T + U` as a bound | `T & U` | `&` intersects traits |
| `.iter().map(\|x\| x * 2).collect()` | `.map({ _ * 2 }).toList()` | one lazy pipeline, no `iter()` step |
| `?` converting with `From` | `?` converting with `From` | identical |
| `loop { ... }`, `break value` | `loop { ... }`, no `break value` | same word, smaller feature |
| `expr;` | `expr` | there are no semicolons |
| `f(a, b)` | `f a, b` | a call drops its parentheses wherever the grammar allows it |

## What changes in your code

A method never lists its receiver, and a function call is a command wherever it can be:

```trb run
type Counter {
  var value: Int = 0

  var fn increment() {
    value = value + 1
  }
}

var counter = Counter()
counter.increment()
print counter.value
// prints 1
```

Where Rust writes `fn increment(&mut self)` and calls it through `&mut counter`, TorbScript writes `var fn increment()`
and requires `counter` itself to be a `var` binding - the mutability travels with the binding and the method, never
with a borrow you take out separately.

## What TorbScript does not have

No lifetimes, no `unsafe`, no macros (`macro_rules!` or procedural), no `#[derive]` or attributes in general, no
`impl Trait` / `dyn Trait` distinction (a trait is just a type; dispatch is not observable), and no non-exhaustive
enums - a public type with cases is a promise. Integer overflow panics except on `UInt64`, which alone has
`addedWrapping` and `multipliedWrapping`.

## Habits to unlearn

- **Reaching for `&` or `&mut` on a parameter.** Take a copy, or write `var` on the parameter for one call - there is no
  borrow that outlives the call.
- **`Vec`, `HashMap`, `Box<dyn Trait>` in a signature.** Write the trait: `List<Item>`, `Map<Key, Value>`, `Trait`.
- **A glob import of an enum's variants.** There is none; `use Shape.Circle from "./shape"` names one case at a time.
- **Semicolons at the end of a line, out of reflex.** A statement ends at the end of its line; a semicolon is a parse
  error.
- **Expecting `?` to work inside a method that answers something other than `Result` or `Option`.** It leaves the
  *enclosing function*, exactly as in Rust, but there is no `Try` trait to implement for a type of your own.

## Related

- [Coming from Rust](../../explanation/coming-from-rust.md) - the full essay, with every difference and its reason.
- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- [Why values instead of references](../../explanation/why-values-instead-of-references.md) - the argument behind the
  ownership difference.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language, at a glance.
