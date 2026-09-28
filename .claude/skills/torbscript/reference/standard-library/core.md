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
  - OperatingSystem
  - Architecture
  - ByteOrder
  - Predicate
  - Transform
source:
  - std/core/src/lib.trb
  - std/core/src/target.trb
---

`std/core` is what every other package builds on: the types the language itself refers to - `Option` behind `Value?`,
`Result` behind `?`, the three ranges behind `a..b`, `Array` as the inline storage a list literal adapts to - the traits the
operators and the conversions go through, and the control flow that is an ordinary function. Everything in it is
re-exported by the prelude, so a file rarely imports it by name - except the three target types, which a file that
branches on where it runs imports on purpose.

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

### RawValue

```trb fragment
public trait RawValue {
  fn rawValue(): Int
  static fn fromRawValue(value: Int): Self?
}
```

The number a case stands for and back, generated for every type whose cases all carry one (`case Read = 1`,
[Cases that stand for numbers](../language/types/case-values.md)): `rawValue()` answers the number of the case,
`fromRawValue` the case of a number or `None`. It is in the prelude, so it can be a bound without an import, which is
what `Flags<Case: RawValue>` of `std/collections` is.

### The operator traits

`Add`, `Subtract`, `Multiply`, `Divide`, `Remainder`, `Power`, `Negate`, `OrElse`, `Index`, `MutableIndex`,
`Slice`, `MutableSlice`. Each one has a type parameter list with defaults, which is why `with Add` means
`Add<Self, Self>`. `a ** b` is `Power.power`, whose first parameter is the `Exponent` (an integer is raised by an
`Int`, a float by a float or by an `Int`). `a[i]`
is `Index.at`, `a[i] = v` is `MutableIndex.set`, `a[from..to]` is `Slice.slice`, `a[from..to] = v` is
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
  with Iterate<Item>, Length, MutableIndex<Int, Item>
{
  static fn filled(value: Item): Array<Item, Size>
  static fn generated(produce: (index: Int) => Item): Array<Item, Size>
  static fn from(items: Iterate<Item>): Array<Item, Size>?
  var fn set(index: Int, value: Item)
  var fn fill(value: Item)
  fn mapped<Output>(transform: Transform<Item, Output>): Array<Output, Size>
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

### `do`, `unless`, `retry` and `Close`

Control structures that are ordinary functions, because only a construct that binds names or jumps is built in, and
the destructor trait.

```trb fragment
public fn do<Value>(body: () => Value): Value
public fn unless(condition: Bool, body: () => Void)
public fn retry<Value, Failure>(times: Int, action: () => Result<Value, Failure>): Result<Value, Failure>
public shared trait Close { var fn close() }
```

`Close` is the destructor: only a `shared type` implements it, and no program calls `close()` - the last release of
the object runs it. `using name = value` is the binding that pins that release to the end of a block; it is part of
the grammar, not a function of `std/core` (see [Destructors](../language/execution/destructors.md), which also says
what is built of it today).

### OperatingSystem, Architecture and ByteOrder

What the program is compiled for, each with one `static current`: a compile-time constant that `torb build --target`
sets, the machine `torb` runs on by default. A `match` on one of them keeps only the arm the value selects, while every
arm is type checked on every machine ([Compile-time branches](../language/execution/compile-time-branches.md)). They
are not in the prelude: the import is the statement that a file branches on its target.

```trb fragment
public type OperatingSystem with Show, Equals, Hash {
  case Windows
  case Linux
  case MacOs
  case FreeBsd
  case Browser

  static current: OperatingSystem
  fn isPosix(): Bool
}

public type Architecture with Show, Equals, Hash {
  case X64
  case Arm64
  case Wasm64

  static current: Architecture
}

public type ByteOrder with Show, Equals, Hash {
  case LittleEndian
  case BigEndian

  static current: ByteOrder
}
```

```trb run
use OperatingSystem from "std/core"

fn separator(): String {
  match OperatingSystem.current {
    .Windows => ";"
    .Linux | .MacOs | .FreeBsd | .Browser => ":"
  }
}

print(OperatingSystem.Linux.isPosix() && separator().byteLength() == 1)
// prints true
```

`show()` answers the name as the vendor writes it (`macOS`, `FreeBSD`), and `isPosix()` is the question for a branch
that should include an operating system added later. `Browser` on `Wasm64` is the target of the playground, whose
`torb` runs as WebAssembly in a web page ([the playground](../tooling/the-playground.md)); `isPosix()` is `false`
there.

### Predicate, Action and Transform

The three closure shapes a signature takes most, as aliases: a question about one value, an effect on one value, and
the conversion of one value into another. An alias is transparent, so any closure of the shape fits (see
[Predicate, Action and Transform](function-types.md)).

```trb fragment
public type Predicate<Value> = (value: Value) => Bool
public type Action<Value> = (value: Value) => Void
public type Transform<Input, Output> = (value: Input) => Output
```

## Related

- [Compile-time branches](../language/execution/compile-time-branches.md) - which arm of a `match` on the target is
  compiled.
- [Result](../language/errors/result.md) - the rules of `Result` and `?`.
- [Bindings](../language/values-and-types/bindings.md) - what `const` and `var` decide.
- [Traits](../language/traits/traits.md) - how the operator traits are implemented.
- [Predicate, Action and Transform](function-types.md) - the rules of the three closure aliases.
- [The standard library](index.md) - the other packages.

