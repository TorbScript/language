---
title: Coming from Rust
summary: What carries over from Rust, what looks the same and is not, and what Rust has that TorbScript deliberately does not.
kind: contrast
status: stable
order: 20
keywords:
  - Rust
  - borrow
  - Err
  - impl
  - match
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#traits
---

Rust is the closest relative: traits instead of inheritance, `match` with exhaustiveness, `Option` and `Result` instead of
`null` and exceptions, and no garbage collector. That closeness is the danger. The syntax is near enough that Rust habits
produce TorbScript that parses and does not compile, or compiles and means something else.

## At a glance

| Rust | TorbScript | Why |
|------|------------|-----|
| `let x = 1` / `let mut x = 1` | `const x = 1` / `var x = 1` | `const` is deep: it freezes the value, not just the binding |
| `&T`, `&mut T` | a copy, or a `var` parameter for one call | references are second-class, so there are no lifetimes |
| `Ok(v)` / `Err(e)` | `Ok value` / `Fail problem` | `Error` is the name of the trait, so the case is `Fail` |
| `Some(x)` in a match | `Some(x)` | the prelude imports it, so it is bare here too |
| `Shape::Circle(r)` | `Shape.Circle r` | `.` for everything, and a command call |
| `Circle(r)` after `use Shape::*` | `use Shape.Circle`, then `Circle r` | there is no glob import of cases |
| `impl Trait for Type { }` | `extend Type with Trait { }` | `with` is the only word for "implements" |
| `struct`, `enum` | `type` | one keyword for all data |
| `trait Hashable` | `trait Hash` | a single-method trait is named after its method |
| `#[derive(Clone, PartialEq, Hash, Debug)]` | nothing to write | `Equals`, `Hash`, `Show` and `copy` are generated |
| `Vec<T>`, `HashMap<K, V>` | `List<Item>`, `Map<Key, Value>` | the type is a trait, the implementation is a name |
| `T`, `K`, `V`, `E` | `Item`, `Key`, `Value`, `Failure` | type parameters are written out |
| `a & b`, `a << 2` | `a.bitwiseAnd(b)`, `a.shiftedLeft(by: 2)` | there are no bit operators |
| `x as i64` | `Int64.from(x)` | there are no casts |
| `s.len()` | `text.byteLength()` or `text.chars().count()` | a `String` has no `length()` |
| `expr;` | `expr` | there are no semicolons |
| `Box<dyn Trait>` | `Trait` as a type | static or dynamic dispatch is not observable |
| `T + U` in a bound | `T & U` | `&` intersects traits; a union of literals uses another symbol |
| `.iter().map(\|x\| x * 2).collect()` | `.map({ _ * 2 }).toList()` | one collection vocabulary, no `iter()` step |
| `?` converting with `From` | `?` converting with `From` | the same, and a single-value case gets `From` generated |

## What changes in your code

### Ownership becomes copying

There is no borrow checker, because there is nothing to borrow. Assigning, passing and capturing a value is a copy; two
bindings never refer to the same value. What Rust writes as `&mut` is a `var` parameter, and it lives only for the duration
of the call:

```trb check
type Counter {
  var count: Int = 0
}

fn incrementTwice(var target: Counter) {
  target.count = target.count + 1
  target.count = target.count + 1
}

var counter = Counter()
incrementTwice counter
print counter.count
```

There is no `&` at the call site. The signature says it, and the tooling shows it. What Rust does with a mutable slice is a
`var` path to a range: `samples[0..100].sort { _ }` sorts that part of the list in place.

The trap is the mirror image of Rust's: `var first = counters[0]` compiles in Rust as a move or a clone and here takes a
**copy**, so changing it changes nothing. The program below prints `1 0`.

```trb check
type Counter {
  var count: Int = 0

  fn increment(var self) {
    count = count + 1
  }
}

var counters = [Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
```

Write `counters[0].increment()`. `CONCEPT.md` specifies a change that is never read afterwards as a compile error,
which would catch this; the compiler does not report it yet.

### `Err` becomes `Fail`

```trb
fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Fail "negative"
  }
  Ok value
}

print checked(1)
```

```trb error
fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Err("negative")
  }
  Ok value
}
// error: Cannot find `Err` here
```

`?` behaves as it does in Rust, including the `From` conversion of the error type - and a case that wraps exactly one value
of a unique type gets that `From` generated, so `extend AppError with From<ConfigError>` is never written.

### A case keeps its type, unless it is imported

Rust resolves a bare name in a pattern against the scope, which is why a misspelled variant silently becomes a binding.
Here the first letter decides: a lowercase name binds, an uppercase name never does, and an uppercase name that is not a
case in scope is an error.

```trb
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Shape.Circle 2.0
const empty: Shape = .Empty
print "{shape} {empty}"
```

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

fn describe(shape: Shape): String {
  match shape {
    Nome => "empty"
    _ => "something"
  }
}
// error: `Nome` is not a case in scope
```

`use Shape.Circle` imports one case by its path. There is no `use Shape.*`, because that is the one import form under
which a file would change meaning because a dependency gained a case.

### A call is written without parentheses

This is the difference a Rust programmer notices last and gets wrong most often. `Ok value`, `print "hello"`,
`return Fail problem`, `names.map Role`. Parentheses appear where the grammar needs them: a nested call, no arguments, an
operator at the top level of an argument, several lines, and the head of an `if`, `for`, `while` or `match`. It is not a
style option - `torb canon --check` reports every file that disagrees.

### `impl` becomes `with` or `extend`

```trb
trait Area {
  fn area(self): Float
}

type Square with Area {
  side: Float

  fn area(self): Float {
    side * side
  }
}

extend Square with Show {
  fn show(self): String {
    "Square({side})"
  }
}

print Square(2.0)
```

`with` at the declaration is Rust's `impl Trait for Type` written where the type is; `extend` is the same thing written
afterwards, and `extend Type { ... }` without a trait adds plain members. The orphan rule is the same: your package has to
own the type or the trait.

### Generics stay declarative

`<Item: Hash>` and `where Item: Hash & Equals` work as they do in Rust, and inference works the same way. What is missing is
associated types and anything higher-kinded: `Option`, `Result`, `Task` and `Iterable` share the names `map`, `flatMap`,
`filter`, `forEach` and `orElse` by convention, and `traverse` is a collection target
(`to<Result<List<Int>, ParseError>>()`).

## Habits to unlearn

These are the things Rust has that TorbScript does not, and what to reach for instead. This list is the honest half of the
comparison.

- **Lifetimes, borrows and `&`.** Gone. A reference exists only as a `var` parameter or `var self`, for one call, and
  cannot be stored, returned or captured by an escaping closure. If you want a long-lived shared thing, that is a
  `shared type` and it has an identity.
- **`unsafe`.** There is none for user code. `native` is the equivalent and is reserved for the standard library; `foreign`
  declares C functions and is a visible capability of a package.
- **Macros.** `macro_rules!` and procedural macros have no counterpart, and that is a decision rather than a gap: names in
  TorbScript are resolved with the help of types, and a macro would have to run before name resolution. What macros are used
  for is covered by functions with closure or `lazy` parameters (`do`, `unless`, `retry`, `using`), receiver closures for
  builders, traits for operators, and `Expression<Value>` for code that has to be *read* instead of run.
- **`#[derive(...)]` and attributes in general.** There are no annotations. `Equals`, `Hash`, `Show`, `copy`, `Encode` and
  `Decode` are generated when they can be, and everything else is written.
- **Bit operators.** `&`, `|`, `^`, `<<`, `>>` are not operators. The integer types come `with Bits`, whose methods are
  `bitwiseAnd`, `bitwiseOr`, `bitwiseExclusiveOr`, `bitwiseNot`, `shiftedLeft(by:)` and `shiftedRight(by:)`. `UInt64`
  additionally has `addedWrapping` and `multipliedWrapping`, which are the only arithmetic in the language that does not
  panic on overflow.
- **`impl Trait` in argument position and `dyn Trait`.** A trait is a type. Whether a call is dispatched statically or
  dynamically is the implementation's business and is not observable; object safety is checked per call, not per type, so
  `List<Show & Hash>` stays a legal type.
- **Non-exhaustive enums.** There is no `#[non_exhaustive]`. A public type with cases is a promise, and a library that wants
  to stay free to add cases hides the type behind a single-field wrapper and accepts `Into<...>`.
- **Turbofish everywhere.** Type arguments can be given partially from the left (`into<Set<Employee>>()`), and the
  disambiguation of `<` is purely syntactic: after a name, `<` starts a type argument list when the tokens up to the
  matching `>` form valid types and the token after `>` is one of `(`, `.`, `{`, `)`, `]`, `,`, `:` or the end of the line.
- **`Vec`, `HashMap`, `BTreeMap` in signatures.** Signatures name traits (`List<Item>`, `Map<Key, Value>`) and an
  implementation is named only where something is constructed (`HashMap()`, `ArrayList`). `ArrayList` is what `Vec` is;
  `TrieMap` is the default `Map`.
- **`.iter()`, `.into_iter()`, `.iter_mut()`.** There is one pipeline. `for x in xs` works on anything `Iterable`, the
  stages are lazy, and a terminal operation (`toList()`, `fold`, `count`, `to<Target>()`) pulls the values through. To change
  elements in place, use the path (`items[index].x = 1`) or `items.update(index) { ... }`.
- **Semicolons, and `;` as an expression terminator.** A statement ends at the end of its line. The distinction Rust makes
  with a trailing `;` is made here by the rule that an expression statement has to be `Void` or `Never` unless the call has
  a `var` receiver or a `var` argument.

## Related

- [Why values instead of references](why-values-instead-of-references.md) - the argument behind the ownership difference.
- [Command calls](../language/syntax/command-calls.md) - the canon, in full.
- [Result](../language/errors/result.md) - `Ok`, `Fail` and `?`.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - how a case is spelled.
- [Traits](../language/traits/traits.md) - `with`, `extend` and the trait names.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form in one place.
