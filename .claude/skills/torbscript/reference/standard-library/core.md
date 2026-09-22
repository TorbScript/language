---
title: std/core
summary: "The bottom of the standard library: Option, Result, Error, the operator and conversion traits, and the control structures that are functions."
kind: package
status: stable
order: 10
keywords:
  - std/core
  - Option
  - Result
  - Error
  - Array
  - operators
  - using
source:
  - std/core/src/lib.trb
---

`std/core` is what every other package builds on: the types the language itself refers to - `Option` behind `Value?`,
`Result` behind `?`, the three ranges behind `a..b`, `Array` as the inline storage a list literal adapts to - the traits the
operators and the conversions go through, and the control flow that is an ordinary function. Everything in it is
re-exported by the prelude, so a file rarely imports it by name.

A data structure is not in here: `List`, `Map` and `Set` are written on top of a storage primitive and live in
[std/collections](collections.md). Nothing in `std/core` knows about text or numbers - with the one exception that
`String` is unavoidable in a signature (`Show.show`, `panic`). That import points at `std/text`, which is why the packages of the standard library are
cyclic, and cycles between modules are allowed because nothing runs when a module is imported.

## Import

```trb fragment
use Option, Result, Error from "std/core"
use Option.Some, Option.None, Result.Ok, Result.Fail from "std/core"
```

Every name below is already in scope through the prelude, so an import is only needed in a file whose project names
another prelude, or to make the dependency explicit.

## Declarations

### Option

```trb fragment
public type Option<Value> {
  case Some(value: Value)
  case None
}
```

A value that may be absent. `Value?` is sugar for `Option<Value>`. `map`, `flatMap`, `filter` and `forEach` mean the same
as everywhere else and run immediately, because an Option is a value and not a pipeline. `orElse` is `OrElse`, the trait
`??` goes through,
`okOr` turns it into a `Result`, `toList()` brings it into the world of pipelines, and `expect(message)` panics where
absence is a bug.

### Result

```trb fragment
public type Result<Value, Failure> {
  case Ok(value: Value)
  case Fail(error: Failure)
}
```

The result of an operation that can fail. The postfix `?` unwraps `Ok` or returns the `Fail` from the surrounding
function, converting the error type through `From`. `map`, `flatMap` and `forEach` work on the `Ok` side, `mapError` on the
`Fail` side. `isOk`, `isError`, `ok`, `orElse`, `toList` and `expect` are the rest. See
[Result](../language/errors/result.md) for the rules.

### Error

```trb fragment
public trait Error with Show {
  fn cause(): Error? {
    None
  }
}
```

"Some error, hand it up." Every error type that carries this trait fits into `Result<Value, Error>`, which lets an
application pass a failure through layers that have nothing to say about it. Precise error types stay the norm for a
library, because a caller can only `match` on what a signature names. `cause()` is the chain, and its default is `None`, so
a type that has nothing to add implements the trait by writing `with Error` and nothing else.

### Void, Never and panic

`Void` is the type with exactly one value, and that value is the keyword literal `void`. A function without a result type
answers `Void`, a block that ends in a statement has the value `void`, and `Ok(void)` passes it on. `Never` is the type of
an expression that does not return, which is why `panic "..."` fits into any expression. `panic` prints
`panic: <message>` and the site to standard error and exits with **101**; nothing else runs on the way out.

### Bool

`Bool` with `Equals`, `Hash`, `Show` and `Encode`. `&&`, `||` and `!` are built in, they short-circuit, and they cannot be
overloaded, because a trait method would evaluate its argument.

### Equals, Compare, Ordering, Hash, combineHashes

`Equals` has `equals` and is `==`. `Compare` has `compare`, requires `Equals`, and is `<`, `<=`, `>` and `>=`; its default
members include `min` and `max`. `Ordering` is what `compare` answers. `Hash` has `hash`, and `combineHashes` is what a
hand-written `hash` folds with. `Float32` and `Float64` are deliberately not `Hash`, so a float can never be a `Map` key
and the `nan` key does not exist.

### From, Into, TryFrom, TryInto, Show, LiteralParseError

Conversions follow `From` and `Into`: implementing `From` provides `Into` for free through a blanket implementation.
`TryFrom` is the fallible form and provides `TryInto` the same way, and text is a source like any other
(`Int.tryFrom("42")`). One direction of each
pair is the one to implement, and an `extend` that writes `Into` or `TryInto` by hand is told which `From` or `TryFrom`
to write instead. Every type has `From<Self>`, and that conversion is the
value itself. `Show` has `show` and is what string interpolation calls; `showNested` is what a value inside another value
uses, and only `String` and `Char` override it. `LiteralParseError` is what the generated `TryFrom<String, ...>` of a
literal type answers.

### The operator traits

`Add`, `Subtract`, `Multiply`, `Divide`, `Remainder`, `Negate`, `OrElse`, `Indexed`, `MutableIndexed`, `Slice`,
`MutableSlice`. Each one has a type parameter list with defaults, which is why `with Add` means `Add<Self, Self>`. `a[i]`
is `Indexed.at`, `a[i] = v` is `MutableIndexed.set`, `a[from..to]` is `Slice.slice`, `a[from..to] = v` is
`MutableSlice.replace`, and `a ?? b` is `OrElse.orElse`, whose `fallback` is `lazy` so that it is only evaluated where
there is nothing to give back.

### Range, RangeFrom, RangeTo, Bounds

```trb fragment
public native type Range<Value> {
  start: Value
  end: Value
  inclusive: Bool = false
}

public native type RangeFrom<Value> {
  start: Value
}

public native type RangeTo<Value> {
  end: Value
  inclusive: Bool = false
}

public trait Bounds<Value: Compare> {
  fn lowest(): Value?
  fn highest(): Value?
  fn includesHighest(): Bool
  fn contains(value: Value): Bool
}
```

**Which ends a range has is its type**, chosen by the syntax: `a..b` and `a..=b` are a `Range`, `a..` a `RangeFrom`,
`..b` and `..=b` a `RangeTo`. Nothing is optional, so `for index in ..10` and `(0..).length()` are compile errors where
they are written: `Range<Int>` is `Iterate<Int>` and `Length`, `RangeFrom<Int>` is `Iterate<Int>` and endless,
`RangeTo<Int>` is neither. `Bounds<Value>` is what all three are and what `Slice.slice` takes, so every spelling works in
brackets.

### Array

```trb fragment
public native type Array<Item, const Size: Int>
  with Iterate<Item>, Length, MutableIndexed<Int, Item>
{
  static fn filled(value: Item): Array<Item, Size>
  static fn generated(produce: (index: Int) => Item): Array<Item, Size>
  static fn from(items: Iterate<Item>): Array<Item, Size>?
  var fn set(index: Int, value: Item)
  var fn fill(value: Item)
  fn mapped<Output>(transform: (value: Item) => Output): Array<Output, Size>
}
```

The inline storage primitive: a fixed number of items, and the number is part of the type (`Array<Float, 16>`). No
storage on the heap, no reference count, and copying it copies its items. `Size` is a const parameter - a literal, a
named `const` or another const parameter - and there is no arithmetic over one, so an out-of-bounds index the compiler
can work out at the call site is a compile error rather than a panic.

Every way to build one writes its size down. A list literal against an expected `Array` type goes straight into the
inline slots and its items are counted against `Size`; a `...` inside such a literal is allowed when what it spreads is
itself an `Array`, and the sizes add up. `filled` and `generated` take `Size` from the expected type, and `from` counts
at run time and answers `Array<Item, Size>?`. It is `Equals`, `Hash` and `Show` wherever `Item` is, and the first two
by index and therefore order-dependent. See [Arrays and const parameters](../language/values-and-types/arrays.md).

### Shared and isSame

`Shared<Value>` is a box that puts a value in a place several owners can hold - the ad hoc version of a `shared type`.
`isSame(first, second)` compares identity and works on a shared object only: on a value the answer would expose whether
the implementation shares storage, so the compiler rejects it.

### `do`, `unless`, `retry`, `using` and `Close`

Control structures that are ordinary functions, because only a construct that binds names or jumps is built in.

```trb fragment
public fn do<Value>(body: () => Value): Value
public fn unless(condition: Bool, body: () => Void)
public fn retry<Value, Failure>(times: Int, action: () => Result<Value, Failure>): Result<Value, Failure>
public fn using<Resource: Close, Value>(var resource: Resource, body: (var Resource) => Value): Value
public shared trait Close { var fn close() }
```

`using` takes a `var` resource and a closure that is handed the resource, calls the closure, then `close()`s the
resource and answers what the closure answered: `using Connection() { connection => connection.send "hello" }`. Passing a temporary to that `var` parameter is
allowed, because the callee is its only owner. There are no destructors, so the nesting of `using` blocks is the only
observable destruction order in the language.

## Related

- [Result](../language/errors/result.md) - the rules of `Result` and `?`.
- [Bindings](../language/values-and-types/bindings.md) - what `const` and `var` decide.
- [Traits](../language/traits/traits.md) - how the operator traits are implemented.
- [The standard library](index.md) - the other packages.

