---
title: What a model trained on other languages gets wrong
summary: The mistakes a language model makes in TorbScript because it has read Rust, Swift, Kotlin and TypeScript, each with the wrong line, the right line and the diagnostic.
kind: explanation
status: stable
order: 15
skill: mistakes
keywords:
  - mistakes
  - diagnostics
  - common errors
  - LLM
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#formatter-canon
---

> **Not built natively yet.** An `Array` filled from a literal (mistake 19) is not built by the native back end yet, so
> `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the language's.

A model writing TorbScript does not fail from ignorance. It fails because TorbScript looks like four languages it knows
well and differs from all of them in the same few places. These are those places, in the order they go wrong, with the
right line first and the wrong one after it.

## The decision

Read this list before writing TorbScript, and check your work against it afterwards. Twenty mistakes cover nearly
everything: the call form, a bare case, `Err` instead of `Fail`, semicolons, `let`, taking a copy out of a collection,
string length, bit operators, casts, an implicit `Some`, a trait name ending in `-able`, a `match` with a `default`, a
`MAX_SIZE` constant, a name that is not ASCII, an arm binding nothing reads, a `use` without names, `while true`, an
extension member the file never names, an overload, and `Type.parse(text)`.

## Why

Three things make these mistakes likely rather than random.

**The surface is familiar.** `match`, `Option`, `Result`, `trait`, `fn`, `?` and exhaustiveness all come from Rust and
Swift, so a model that has seen those languages produces plausible TorbScript immediately - and plausible is exactly what
is dangerous.

**The differences are small and local.** `Err` against `Fail`, `Shape.Circle` against `Circle`, `print x` against
`print(x)`. A small difference does not trigger a rethink; it gets smoothed over by the prior.

**A prohibition does not remove a prior.** Telling a model not to write semicolons is weaker than showing it the line
without one next to the line with one. Every item below is therefore a pair, and the correct line comes first.

## Consequences

### 1. A call is written as a command

```trb
const numbers = [1, 2, 3]
print "hello"
print numbers.map({ _ * 2 }).toList()
```

```trb skip a parenthesized call in command position parses, so only prose can say it is wrong here
print("hello")
```

Both parse. The second is wrong because `torb canon` rewrites it and `torb canon --check` fails until it is rewritten. A
call is a command in command position - the start of a statement, the right of `=`, after `return`, after `=>` - when the
callee is a path, it has at least one argument whose first token is not `(`, `[`, `-`, `!` or `.`, no argument has an
operator at its top level, and the arguments are on one line.

### 2. A case is never bare unless the file imports it

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

const shape = Circle(2.0)
// error: Cannot find `Circle` here
```

`Some`, `None`, `Ok` and `Fail` are bare because the prelude imports them from `Option` and `Result`. Import a case of your
own with `use Shape.Circle`, and it is bare from then on.

### 3. The failure case is `Fail`, not `Err`

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

The *field* is still called `error`, and so are `isError` and `mapError`, which are about the error the case carries.

### 4. There are no semicolons

```trb
const answer = 42
print answer
```

```trb error
const answer = 42;
// error: There are no semicolons. A statement ends at the end of its line
```

A statement ends at the end of its line, and two statements never share a line.

### 5. The keyword is `const`, not `let`

```trb
const answer = 42
var counter = 0
counter = counter + 1
print "{answer} {counter}"
```

```trb error
let answer = 42
// error: Cannot find `let` here
// error: This is a temporary, and an assignment writes it
```

`const` was chosen over `val` because a page of `val` with a `var` in the middle is easy to misread. There is no `let`,
and `var` is the only mutable form - there is no `let mut`.

### 6. Taking an element out of a collection takes a copy

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
// prints 1 0
```

This is the copy trap: the second program prints `1 0`, because values are never aliased and `counters[0]` in a binding
is a copy. Reach through the path instead: `counters[0].increment()`, `world.entities[id].health = 5`,
`items.update(index) { ... }`. A change that is never read afterwards is a compile error. The second program reads the
copy in its last line, so the compiler reports nothing there: the mistake survives whenever the copy is used.

### 7. A `String` has no `length()` and no `text[i]`

```trb
const text = "Grüße"
print text.byteLength()
print text.chars().count()
```

```trb error
const text = "Grüße"
print text.length()
// error: `String` has no member `length`
```

"Length" and "the i-th character" have three different answers - bytes, code points, and what a reader sees - and two of
them are slow, so the names say what is counted. Positions come from searching (`indexOf`) and are byte offsets;
`text[3..]` slices at an offset and panics if the offset is inside a character.

### 8. There are no bit operators

```trb fragment
const masked = value.bitwiseAnd(0xFF)
const shifted = value.shiftedLeft(by: 2)
```

```trb error
fn masked(value: Int): Int {
  value & 0xFF
}
// error: Expected the end of the statement, found `&`
```

`&`, `|`, `^`, `<<` and `>>` are not operators of the language: `&` intersects traits and `|` unions literal types. The
integer types come `with Bits`, whose methods are `bitwiseAnd`, `bitwiseOr`, `bitwiseExclusiveOr`, `bitwiseNot`,
`shiftedLeft(by:)` and `shiftedRight(by:)`. `UInt64` additionally has `addedWrapping` and `multipliedWrapping`, the only
arithmetic that does not panic on overflow.

### 9. There are no casts and no implicit numeric conversions

```trb
const answer = 42
const precise = Float.from(answer) * 1.5
print precise
```

```trb error
const answer = 42
const precise: Float = answer
// error: Expected `Float64`, found `Int64`
```

Conversions go through `From` and `Into`: `Float.from(someInt)`, `value.into()` where the expected type says the target,
`Int32.tryFrom(value)` where the conversion can fail. A *literal* does adapt to the expected type
(`const ratio: Float = 1`), which is a different rule.

### 10. There is no implicit `Some` and no implicit unwrap

```trb
fn find(values: List<Int>, wanted: Int): Int? {
  for value in values {
    if value == wanted {
      return Some value
    }
  }
  None
}

print find([1, 2], 2)
```

```trb error
fn find(values: List<Int>, wanted: Int): Int? {
  for value in values {
    if value == wanted {
      return value
    }
  }
  None
}
// error: Expected `Option<Int64>`, found `Int64`
```

A value never wraps itself into an `Option`. `None` is the one value in the language that takes its type from what is
expected of it.

### 11. A single-method trait is named after its method

```trb
type Money with Equals, Hash {
  cents: Int

  fn equals(other: Money): Bool {
    cents == other.cents
  }

  fn hash(): Int {
    cents
  }
}

print Money(1).equals(Money(1))
```

```trb error
type Money with Hashable, Equatable {
  cents: Int
}
// error: Unknown type `Hashable`
// error: Unknown type `Equatable`
```

`Hash`, `Equals`, `Compare`, `Show`, `Add`, `Close`, `Length`, `From`, `Iterate`. No `-able` and no `-ible`. A trait
that is mainly used as a *type* is a noun instead: `Iterator`, `Accumulator`, `Source`, `Sink`.

### 12. A `match` has no `default` and no fallthrough

```trb
fn describe(value: Int): String {
  match value {
    0 => "zero"
    1 | 2 | 3 => "small"
    _ => "large"
  }
}

print describe(7)
```

```trb error
fn describe(value: Bool): String {
  match value {
    true => "yes"
  }
}
// error: `match` does not handle `false`
```

`_` is the wildcard. Every arm is one arm, a `match` is an expression, it must be exhaustive, and an arm that can never be
reached is an error. A `default` arm parses: `default` starts with a lowercase letter, so it is a **binding** that
matches everything. It does not compile, because nothing reads it (mistake 15), but the message is about the binding
and not about a keyword that does not exist.

### 13. A constant is `maxSize`, never `MAX_SIZE`

```trb
const maxSize = 1024

print maxSize
```

```trb error
const MAX_SIZE = 1024
// error: A constant starts with a lowercase letter: TorbScript has no `MAX_SIZE` spelling, write `maxSize`
```

There is no MACRO_CASE anywhere in this language, at module level or inside a type. How a name is spelled is a rule the
checker reports at the declaration: `A` to `Z` starts a type, a trait, a case, a type parameter and a type alias, and
everything else starts with a lowercase letter or `_`. See [Naming](../language/syntax/naming.md).

### 14. A name is ASCII, and text is not

```trb
/** Grüßt zurück. 👋 */
const greeting = "Grüße 👋"

print greeting
```

```trb error
const größe = 1
// error: A name is written in ASCII letters, digits and `_`
```

An identifier is `[A-Za-z_][A-Za-z0-9_]*`. A string, a character literal, a comment and a doc comment may contain
anything Unicode has - so a German doc comment is ordinary, and a German name is not.

### 15. An arm binding that nothing reads is an error

```trb
fn describe(value: Int?): String {
  match value {
    Some(number) => "there is {number}"
    None => "nothing"
  }
}

print describe(Some(1))
```

```trb error
fn describe(value: Int?): String {
  match value {
    Some(number) => "something"
    None => "nothing"
  }
}
print describe(Some(1))
// error: `number` is never read: write `_`, or `_number` to keep the name
```

This is the habit from Rust, where `Some(_)` and `Some(x)` are both fine and only one of them warns. Here the arm of a
`match`, an `if const`/`if var` and a `while const` are the positions where a lowercase name is a *choice*: it always
binds and never compares, so `limit =>` matches every value instead of comparing with the constant `limit`. Write `_`
where nothing needs the value and `_reason` where the name is the documentation. An unused `const`, `for` binding or
parameter is not part of the rule.

### 16. A `use` names what it imports

```trb
use String.shout from "./text-extensions"

print "hello".shout()
```

```trb error
use "std/text"
// error: A `use` names what it imports
```

Nothing runs when a module is imported, so a path on its own would bring in nothing at all. Every `use` carries names: a
declaration by its name, a case or a member of a type by its path, and `as` for a local name of this file's choosing.

### 17. An endless loop is `loop`, and a `break` carries no value

```trb check
var count = 0
loop {
  count = count + 1
  if count == 3 {
    break
  }
}
print count
```

```trb error
var count = 0
while true {
  count = count + 1
  print count
}
// error: A loop that never ends is written `loop`
```

`loop { ... }` has the type `Never` while no `break` targets it - so nothing after it is reached, and a function whose
body is one needs no other result - and `Void` once one does. There is no `break value`: what Rust carries out of a loop
is a `var` written before it. `continue` works as in a `while`, and `while false` is left alone.

### 18. A member another package adds is named in this file

```trb check
use Int64.megabytes from "std/sandbox"

print 64.megabytes()
```

```trb error
print 64.megabytes()
// error: `std/sandbox` adds `megabytes` to `Int64`, and this file does not name it
```

A member belongs to the type everywhere when the package of the *type* attached it, and so does an `extend` this package
wrote itself. Everything else the file names, by the path of the member, which is the form a case import takes; `as`
renames it, and two members of one name for one type stay an error at the use. What a trait puts on a type it does not
own needs the trait as a name of the file instead of the member (`use Slug from "acme/slug"`), which is why the
operators, `for`, interpolation, `?`, `??` and `into()` need no import: their traits are in the prelude.

### 19. A name means one declaration, and an Array is built from a literal

Two habits that both come from a language with overloading: a second `fn` of the same name with other parameter types,
and a factory whose number of arguments is the size of what it builds.

```trb check
type Circle {
  radius: Float
}

type Square {
  side: Float
}

trait Draw<Shape> {
  fn draw(shape: Shape): String
}

type Canvas {
  scale: Float
}

extend Canvas with Draw<Circle> {
  fn draw(shape: Circle): String {
    "circle of {shape.radius * scale}"
  }
}

const corners: Array<Int, 4> = [1, 2, 3, 4]
print "{Canvas(2.0).draw(Circle(1.0))} {corners}"
```

```trb error
type Circle {
  radius: Float
}

type Square {
  side: Float
}

type Canvas {
  fn draw(shape: Circle): String {
    "circle"
  }

  fn draw(shape: Square): String {
    "square"
  }
}
// error: `draw` is already declared in `Canvas`
```

There is no overloading by parameter type and no uniform function call syntax: an operation belongs to its receiver (a
method, or an `extend`) or to a trait with a parameter, one `extend` per instantiation, and a second arity is a default
parameter. An `Array` comes from a list literal whose items are counted against `Size`, from `Array.filled(value)` or
`Array.generated { index => ... }`, which take `Size` from the expected type, or from `Array.from(items)`, which counts
at run time and answers an `Option`. See [where are my overloads](where-are-my-overloads.md) and
[Arrays and const parameters](../language/values-and-types/arrays.md).

### 20. There is no `parse` on a type: text is a source like any other

Rust has `str::parse` and a `FromStr` trait beside `From`, so a model reaches for `Type.parse(text)`. TorbScript has one
fallible conversion and text is one of its sources.

```trb check
type Port {
  number: Int
}

extend Port with TryFrom<String, String> {
  static fn tryFrom(text: String): Result<Port, String> {
    const number = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
    if number < 1 || number > 65535 {
      return Fail "{number} is not a port"
    }
    Ok Port(number)
  }
}

print Port.tryFrom("8080")
print Port.tryFrom("nope")
```

```trb error
print Int64.parse("42")
// error: `Int64` has no member `parse`
```

`Type.tryFrom(text)` is the call, and `tryInto()` is the same conversion in a chain: under a `?` the annotation says
the target alone (`const port: Int = text.tryInto()?`) and the failure follows from the one `TryFrom` the target has
for a `String`. A function named `parse` belongs to a **format** - `Json.parse(text)` reads a document - never to a
value. See [Conversions](../language/types/conversions.md) and
[Parse text into a type](../how-to/parse-text-into-a-type.md).

### 21. A method does not list `self`, and `static` says what belongs to the type

Rust writes `&self`, Python writes `self`, Swift infers it - so a model writes a receiver into the parameter list.
Here a member says what it is with two words in front of `fn`, and the parameter list is what the caller writes.

```trb check
type Rectangle {
  width: Int
  height: Int
  var scale: Int = 1

  fn area(): Int {
    width * height * scale
  }

  var fn grow(by: Int) {
    scale = scale + by
  }

  static fn square(size: Int): Self {
    Self size, size, 1
  }
}

var rectangle = Rectangle.square 3
rectangle.grow 2
print rectangle.area()
```

```trb error
type Rectangle {
  width: Int
  height: Int

  fn area(self): Int {
    width * height
  }
}
// error: A method does not list `self`
```

`&mut self` becomes `var fn`, and a member written without a receiver becomes `static fn` - forgetting the `static`
makes it a method, and the call through the type name then says so. `self` is still an expression inside the body,
and a function *type* still names it: `(self: Point) => Int` and `(var self: Config) => Void` are what a
[receiver closure](../language/configuration/receiver-closures.md) is. See
[Methods and `static fn`s](../language/types/methods.md).

### The rest, in one table

| Do not write | Write | Why |
|--------------|-------|-----|
| `let mut count = 0`, `mutating func` | `var count = 0`, `var fn` | `var` on the binding or in front of the `fn` is all there is |
| a borrow, `&mut value` | a copy, or a `var` parameter | a `var` parameter lends the caller's value for the duration of one call |
| `null`, `nil`, `undefined` | `None` | absence is an `Option<Value>` |
| `throw`, `try`, `catch` | `return Fail problem`, `?` | there are no exceptions |
| `class`, `interface`, `enum`, `struct` | `type`, `trait` | one keyword for data, one for capability |
| `impl Trait for Type` | `extend Type with Trait` | `with` is the only word for it |
| `extend Mine with Into<Foreign>` | `extend Foreign with From<Mine>` | `Into` comes from a blanket over `From` |
| a second `fn` of the same name | a trait with a parameter, or a default parameter | a name means one declaration |
| `Array.of(1, 2, 3)` | `const a: Array<Int, 3> = [1, 2, 3]` | only a literal counts its items |
| `#[derive(...)]`, `@Annotation` | nothing | there are no annotations; what can be generated is |
| `1 ?? 0` | an `Option` or a `Result` on the left | `??` is the trait `OrElse` of those two |
| `list[i]` for a possibly missing index | `list.get(i)` | `list[i]` panics out of bounds |
| `a.iter().map(...)` | `a.map(...)` | there is one pipeline and no `iter()` step |
| `list.add(x)`, `list.added(x)`, `list.addAll(xs)` | `list.append(x)`, `list.appended(x)`, `list.appendAll(xs)` | each kind has its own verb, and a list appends |
| `set.add(x)`, `set.addAll(xs)` | `set.insert(x)`, `set.insertAll(xs)` | a set inserts; there is no shared `add` |
| `stack.add(x)`, `stack.remove()`, `queue.add(x)`, `queue.remove()` | `stack.push(x)`, `stack.pop()`, `queue.enqueue(x)`, `queue.dequeue()`, `peek()` | the words everybody knows for each structure |
| `Iterable<Item>`, `items.iterator()` | `Iterate<Item>`, `items.iterate()` | a single-method trait is named like its method |
| `Collection<Item>` as a bound | `Iterate<Item> & Length`, or the kind itself | there is no `Collection` trait |
| `assert sum == 3` | `assert(sum == 3)` | an operator at the top level of an argument needs parentheses |
| `Ok Some x` | `Ok Some(x)` | commands do not nest |
| `print list.map { _ * 2 }` | `print list.map({ _ * 2 })` | the `{` would belong to the outer command |
| `fn f() -> Int` | `fn f(): Int` | the result type follows a colon |
| `T`, `K`, `V`, `E` | `Item`, `Key`, `Value`, `Failure` | type parameters are written out |
| `abs`, `sqrt`, `Expr` | `absolute`, `squareRoot`, `Expression` | names are written out |
| a `for` loop that mutates the element | `items[index].field = value` | the loop variable is a `const` copy |
| a getter like `getName()` | the field `name`, or a method `name()` | there are no properties and no `get` prefix |
| `fn area(self)`, `fn grow(&mut self)` | `fn area()`, `var fn grow()` | a method does not list its receiver |
| `fn of(): Self` inside a type body | `static fn of(): Self` | without `static` it is a method |
| `const origin = Point(0, 0)` in a type body | `static origin = Point(0, 0)` | `const` in a type body is a field |
| `type point`, `fn Distance` | `type Point`, `fn distance` | the first letter of a name is a rule, not a convention |
| `Config(host)` for a type of three fields | `Config(host, ...)` | a pattern that does not name every field ends in `...` |
| `Person("Ada")` where `nickname: String?` | `Person("Ada", nickname: None)` | an optional field without a default is required |

### How to check yourself

Do not trust the list. Run the compiler, from the repository root:

```console
torb check <path>
torb canon --check <path>
```

`check` answers `no problems` or points at the line. `canon --check` reports every file that is not in the formatter canon,
which is where mistake 1 shows up. See [Verify your work](../tooling/verifying-your-work.md).

## Related

- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language in one place.
- [Command calls](../language/syntax/command-calls.md) - the canon behind mistake 1.
- [Where are my overloads](where-are-my-overloads.md) - the argument behind mistake 19.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - the rules behind mistakes 2, 12 and 15.
- [Naming](../language/syntax/naming.md) - the rules behind mistakes 13 and 14.
- [Result](../language/errors/result.md) - the rules behind mistake 3.
- [use](../language/modules-and-packages/use.md) - the rules behind mistakes 16 and 18.
- [Loops](../language/execution/loops.md) - the rule behind mistake 17.
- [extend](../language/traits/extend.md) - where a member of a foreign type is visible, and where it is named.
- [Coming from Rust](coming-from-rust.md) - the same ground for one language in detail.
- [Verify your work](../tooling/verifying-your-work.md) - the commands that decide.

