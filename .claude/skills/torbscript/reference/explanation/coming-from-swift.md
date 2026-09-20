---
title: Coming from Swift
summary: What carries over from Swift - value types, enums with payloads, Optionals, protocols with default members - and what a Swift habit gets wrong here.
kind: contrast
status: stable
order: 30
keywords:
  - Swift
  - mutating
  - protocol
  - associatedtype
  - guard
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#traits
  - CONCEPT.md#identity-shared-type
---

Swift is the closest relative on the value-semantics side: structs copy, enums carry payloads, `Optional` and `Result`
replace `null` and exceptions in most Swift code, and protocols read like traits. The closeness hides three habits
that produce TorbScript which parses and does something else: Swift's `let` does not freeze everything it touches, its
enum cases never need an import, and its protocols can grow an associated type that TorbScript traits cannot have.

## At a glance

| Swift | TorbScript | Why |
|-------|------------|-----|
| `let x = 1` / `var x = 1` | `const x = 1` / `var x = 1` | the same two words, but `const` freezes deeper than `let` does for a class |
| `struct Point { }` | `type Point { }` | one keyword for every value type; there is no second keyword to reach for |
| `class Connection { }` | `shared type Connection { }` | identity is the exception that says so, not a second family of types |
| `mutating func increment()` | `fn increment(var self)` | the marker moves from the call site's implicit rule to the parameter itself |
| `enum Shape { case circle(Double) }` | `type Shape { case Circle(radius: Float) }` | one keyword for structs and enums; a case is `UpperCamelCase` |
| `case .circle(let radius):` | `.Circle(radius) =>` | the leading dot is the same habit; `match` replaces `switch` |
| `Optional<Value>` / `Value?` | `Option<Value>` / `Value?` | the sugar looks the same; wrapping a value into it is never implicit |
| `let x: Int? = 5` | `const x: Int? = Some(5)` | there is no implicit `Some` |
| `guard let x = opt else { return }` | `const x = opt?` | `?` is the early return itself, not a statement that needs one |
| `.success(value)` / `.failure(error)` | `Ok(value)` / `Fail(error)` | `Error` is the name of the trait, so the case is `Fail` |
| `protocol Shape { func area() -> Double }` | `trait Shape { fn area(self): Float }` | one word, `trait`, replaces `protocol` |
| `extension Square: Area { }` | `extend Square with Area { }` | `with` is the only word for "implements" |
| `protocol Hashable` | `trait Hash` | a single-method trait is named after its method |
| `willSet` / `didSet` | nothing to write | a change is already visible at the one place it happens: the verb |
| `associatedtype Item` inside a protocol | `trait Container<Item> { }` | a type parameter on the trait, never an associated type |
| `weak var` / `unowned let` | nothing to write | a cycle collector reclaims `shared type` objects |
| `if case .some(let x) = opt` | `if const Some(x) = opt` | pattern position is spelled with `const`/`var`, not `case` |
| `Codable` (`Encodable`/`Decodable`) | `Encode`/`Decode` | generated the same way; a renamed field is a hand-written pair, not `CodingKeys` |

## What changes in your code

### `let` becomes `const`, and freezes further than Swift's does

A `let` binding to a Swift `struct` already blocks every mutation, so this part feels familiar. The difference shows
up at the one place Swift has a second kind of type: a `class`. `let connection = Connection(...)` stops you from
reassigning `connection`, but `connection.send("hello")` still runs if `send` mutates a `var` property, because the
object itself was never frozen - only the reference to it.

TorbScript has one keyword for that exception, `shared type`, and `const` reaches it too. Assigning a `shared type`
never copies it - two bindings name the same object, exactly like two Swift references to one class instance:

```trb
shared type Connection {
  url: String
  private(var) sent: Int = 0

  fn send(var self, message: String) {
    sent = sent + 1
  }
}

var connection = Connection "tcp://example.test"
var same = connection          // The same object, not a copy
same.send "hello"
print connection.sent          // 1
```

A `const` binding to that same object is a read-only view: the object can still change through somebody else's `var`
path, but never through this one - which is the Swift-`let`-on-a-class behavior TorbScript keeps. What changes is
that ordinary types have no such loophole at all, because there is no `class` to fall back on when a `struct` starts
feeling inconvenient to freeze:

```trb error
shared type Connection {
  url: String
  private(var) sent: Int = 0

  fn send(var self, message: String) {
    sent = sent + 1
  }
}

const view = Connection "tcp://example.test"
view.send "nope"
// error: `send` needs a `var`
```

### Mutation is marked on the declaration, not the call

Swift writes `mutating` once, on the function, and the compiler checks every call site against whether the variable
is a `var`. TorbScript writes the same idea as `var self` on the parameter list, and the call-site check is the same
rule as for a `var` field or a `var` parameter - it is not a special case for methods:

```trb
type Counter {
  var value: Int = 0

  fn increment(var self) {
    value = value + 1
  }

  fn incremented(self): Counter {
    copy(value: value + 1)
  }
}

var counter = Counter()
counter.increment()
const next = counter.incremented()
print "{counter.value} {next.value}"
```

Calling `increment` through a `const` binding is rejected the same way Swift rejects `mutating` on a `let`, except
the message names the non-mutating twin when the standard library has one:

```trb error
type Counter {
  var value: Int = 0

  fn increment(var self) {
    value = value + 1
  }
}

const frozen = Counter()
frozen.increment()
// error: `increment` needs a `var`
```

### A case still needs the dot Swift already writes - importing is the new step

Swift pattern matching always spells a case with a leading dot (`.circle`), so this part of TorbScript needs no
relearning. What is new is that TorbScript also lets a file skip the dot completely, but only after it says so with
an import - a bare case name is never inferred from context the way Swift's implicit member expression tries every
enum in scope:

```trb
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Shape.Circle 2.0
const empty: Shape = .Empty
print "{shape} {empty}"
```

Writing the bare name without that import is not a smaller version of Swift's dot syntax, it is a name that does not
exist yet:

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Circle(2.0)
// error: Cannot find `Circle` here
```

`use Shape.Circle` at the top of the file is what makes `Circle 2.0` legal - one line, and only for that one case,
never for a whole type at once. See [Importing cases](../language/pattern-matching/importing-cases.md).

### Protocols become traits, and an associated type becomes a type parameter

A Swift `associatedtype` inside a `protocol` is why `any Shape` gets awkward the moment a member reads or returns
that associated type - the protocol needs `some` or full generics to stay usable. TorbScript traits never grow an
associated type: what Swift spells that way is an ordinary type parameter on the trait, declared exactly like one on
a `type` or a `fn`, and the trait stays a plain type with that parameter filled in:

```trb check
trait Convert<Target> {
  fn convert(self): Target
}

type Fahrenheit {
  degrees: Float
}

type Celsius with Convert<Float> {
  degrees: Float

  fn convert(self): Float {
    degrees
  }
}

print Celsius(20.0).convert()
```

`Convert<Float>` is a type on its own - a field, a parameter or a return type can name it directly, with no
existential box and no `any` keyword. See [No higher-kinded types](../language/generics/no-higher-kinded-types.md)
for the one thing a type parameter still cannot stand for: a Swift `associatedtype` is a filled-in type, never a
type constructor with a hole in it, so this gap does not reopen behind the trait.

## Habits to unlearn

These are things Swift has that TorbScript deliberately does not, and what replaces them. A comparison that only
lists what carries over would be an advertisement.

- **Force unwrap `!` and implicit optional promotion.** There is no `!` operator and no automatically unwrapped
  optional anywhere. Unwrap with `?`, `??`, `expect(message)`, or take the value apart with `match`; see
  [Option](../language/values-and-types/option.md).
- **`willSet`/`didSet` property observers.** A field has no hook that runs on assignment. Code that watched for a
  change goes into the verb that makes the change (`fn deposit(var self, amount: Int) { ... }`) instead of reacting
  to it afterwards.
- **Computed properties (`var area: Double { ... }`) and their `get`/`set` blocks.** There are no properties: a field
  is storage and a method computes, and the parentheses say which one you are looking at
  (`shape.area` is data, `shape.area()` would be the method syntax if `area` were computed). See
  [Property commands](../language/types/property-commands.md).
- **`weak` and `unowned` references.** TorbScript's cycle collector reclaims `shared type` objects the way ARC could
  not on its own, so nothing marks a reference as non-owning to break a cycle.
- **`-able` protocol names (`Hashable`, `Equatable`, `Comparable`).** A single-method trait is named after its
  method - `Hash`, `Equals`, `Compare` - which is also why the keyword is `with` and not `is` or `conforms to`.
- **`Codable` synthesis with `CodingKeys`.** `Encode` and `Decode` are generated the same way `Codable` is, but there
  are no annotations: a renamed or skipped field means writing `encode`/`decode` by hand for that type. See
  [Encode and Decode](../language/reflection/encode-and-decode.md).
- **Five access levels.** `private`, `fileprivate`, `internal`, `public` and `open` become two: unmarked, which is
  public, and `private`, which reaches every file of the same package through `extend` - there is nothing between
  file-private and open, and nothing above public.
- **Guard statements as a shape of their own.** `guard let x = opt else { return }` is exactly what `?` already does
  at the end of an expression; a function that returns `Option` or `Result` never needs a separate early-exit
  keyword. See [The question mark operator](../language/errors/question-mark.md).

## Related

- [Why values instead of references](why-values-instead-of-references.md) - the argument behind `const`/`var`
  replacing struct and class.
- [Shared types](../language/types/shared-types.md) - `shared type` in full, the one place Swift's `let`-on-a-class
  behavior survives.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - why a case is never bare unless imported.
- [Traits](../language/traits/traits.md) - `with` and `extend`, and the trait names.
- [Coming from Rust](coming-from-rust.md) - the other value-semantics relative, without classes at all.
