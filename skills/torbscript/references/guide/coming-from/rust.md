---
title: Coming from Rust
summary: What maps directly from Rust, the five habits that will trip you up, and what Rust has that TorbScript leaves out on purpose.
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

Rust is the closest relative: traits instead of inheritance, an exhaustive `match`, `Option` and `Result` instead of
`null` and exceptions, no garbage collector. That closeness is the trap. A Rust habit often compiles here and means
something else.

## At a glance

| Rust | TorbScript | Note |
|------|------------|------|
| `let x = 1` / `let mut x = 1` | `const x = 1` / `var x = 1` | `const` freezes the whole value, not only the name |
| `&T`, `&mut T` | a copy, or a `var` parameter | no borrows and no lifetimes |
| `Ok(v)` / `Err(e)` | `Ok value` / `Fail problem` | the case is `Fail` |
| `Shape::Circle(r)` | `Shape.Circle(r)` or `.Circle(r)` | `.` for everything |
| `impl Trait for Type { }` | `extend Type with Trait { }` | `with` means "has this trait" |
| `fn area(&self)` / `fn grow(&mut self)` | `fn area()` / `var fn grow()` | the receiver is never a parameter |
| `fn new() -> Self` | `static fn of(): Self` | `static` marks a function of the type |
| `struct`, `enum` | `type` | one keyword for all data |
| `#[derive(Clone, PartialEq, Hash, Debug)]` | nothing | `==`, hashing, printing and `copy` come for free |
| `Vec<T>`, `HashMap<K, V>` | `List<Item>`, `Map<Key, Value>` | you name the kind of collection, not the implementation |
| `T: A + B` | `Item: A & B` | `&` joins traits |
| `.iter().map(\|x\| x * 2).collect()` | `.map({ _ * 2 }).toList()` | no `iter()` step |
| `?` converting with `From` | `?` converting with `From` | the same |
| `loop { ... }`, `break value` | `loop { ... }`, no `break value` | same word, smaller feature |
| `f(a, b);` | `f a, b` | no semicolons, and a statement call drops its parentheses |

## What changes in your code

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

Rust writes `fn increment(&mut self)` and calls it through `&mut counter`. TorbScript writes `var fn increment()` and
needs `counter` to be a `var`. Whether a value can change belongs to the binding and the method, never to a borrow.

## What TorbScript does not have

No lifetimes, no `unsafe`, no macros, no attributes, and no `impl Trait` versus `dyn Trait`: a trait is a type. A
public type with cases cannot be marked non-exhaustive. Integer overflow stops the program, except on `UInt64`, which
has `addedWrapping` and `multipliedWrapping`.

## Habits to unlearn

- **`&` or `&mut` on a parameter.** Take a copy, or write `var` on the parameter; no borrow outlives the call.
- **`Vec`, `HashMap` or `Box<dyn Trait>` in a signature.** Write `List<Item>`, `Map<Key, Value>` or the trait.
- **A glob import of an enum's variants.** Import one case at a time: `use Shape.Circle from "./shape"`.
- **A semicolon at the end of a line.** It is a parse error.
- **A `Try` implementation of your own.** `?` works on `Result` and `Option`, and on nothing else.

## Related

- Coming from Rust (skill `torbscript-language`: `references/explanation/coming-from-rust.md`) - the long version, with the reason for each difference.
- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- Why values instead of references (skill `torbscript-language`: `references/explanation/why-values-instead-of-references.md`) - why there are no
  borrows.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language on one page.

