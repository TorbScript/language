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

A model writing TorbScript does not fail from ignorance. It fails because TorbScript looks like four languages it knows
well and differs from all of them in the same few places. These are those places, in the order they go wrong, with the
right line first and the wrong one after it.

## The decision

Read this list before writing TorbScript, and check your work against it afterwards. Fifteen mistakes cover nearly
everything: the call form, a bare case, `Err` instead of `Fail`, semicolons, `let`, taking a copy out of a collection,
string length, bit operators, casts, an implicit `Some`, a trait name ending in `-able`, a `match` with a `default`, a
`MAX_SIZE` constant, a name that is not ASCII, and an arm binding nothing reads.

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
```

`const` was chosen over `val` because a page of `val` with a `var` in the middle is easy to misread. There is no `let`,
and `var` is the only mutable form - there is no `let mut`.

### 6. Taking an element out of a collection takes a copy

```trb check
type Counter {
  var count: Int = 0

  fn increment(var self) {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

```trb check
type Counter {
  var count: Int = 0

  fn increment(var self) {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
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

  fn equals(self, other: Money): Bool {
    cents == other.cents
  }

  fn hash(self): Int {
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
```

`Hash`, `Equals`, `Compare`, `Show`, `Add`, `Close`, `Length`, `From`. No `-able` and no `-ible`. A trait that is mainly
used as a *type* is a noun instead: `Iterable`, `Iterator`, `Collection`, `Collector`.

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
matches everything. It no longer compiles, because nothing reads it (mistake 15), but the message is about the binding
and not about a keyword that does not exist.

### 13. A constant is `maxSize`, never `MAX_SIZE`

```trb
const maxSize = 1024

print maxSize
```

```trb error
const MAX_SIZE = 1024
print MAX_SIZE
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

### The rest, in one table

| Do not write | Write | Why |
|--------------|-------|-----|
| `null`, `nil`, `undefined` | `None` | absence is an `Option<Value>` |
| `throw`, `try`, `catch` | `return Fail problem`, `?` | there are no exceptions |
| `class`, `interface`, `enum`, `struct` | `type`, `trait` | one keyword for data, one for capability |
| `impl Trait for Type` | `extend Type with Trait` | `with` is the only word for it |
| `#[derive(...)]`, `@Annotation` | nothing | there are no annotations; what can be generated is |
| `list[i]` for a possibly missing index | `list.get(i)` | `list[i]` panics out of bounds |
| `a.iter().map(...)` | `a.map(...)` | there is one pipeline and no `iter()` step |
| `assert sum == 3` | `assert(sum == 3)` | an operator at the top level of an argument needs parentheses |
| `Ok Some x` | `Ok Some(x)` | commands do not nest |
| `print list.map { _ * 2 }` | `print list.map({ _ * 2 })` | the `{` would belong to the outer command |
| `fn f() -> Int` | `fn f(): Int` | the result type follows a colon |
| `T`, `K`, `V`, `E` | `Item`, `Key`, `Value`, `Failure` | type parameters are written out |
| `abs`, `sqrt`, `Expr` | `absolute`, `squareRoot`, `Expression` | names are written out |
| a `for` loop that mutates the element | `items[index].field = value` | the loop variable is a `const` copy |
| a getter like `getName()` | the field `name`, or a method `name()` | there are no properties and no `get` prefix |
| `type point`, `fn Distance` | `type Point`, `fn distance` | the first letter of a name is a rule, not a convention |

### How to check yourself

Do not trust the list. Run the compiler, from `bootstrap/`:

```console
cargo run --release -q -- run ../compiler check <path>
cargo run --release -q -- canon --check <path>
```

`check` answers `no problems` or points at the line. `canon --check` reports every file that is not in the formatter canon,
which is where mistake 1 shows up. See [Verify your work](../tooling/verifying-your-work.md).

## Related

- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language in one place.
- [Command calls](../language/syntax/command-calls.md) - the canon behind mistake 1.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - the rules behind mistakes 2, 12 and 15.
- [Naming](../language/syntax/naming.md) - the rules behind mistakes 13 and 14.
- [Result](../language/errors/result.md) - the rules behind mistake 3.
- [Coming from Rust](coming-from-rust.md) - the same ground for one language in detail.
- [Verify your work](../tooling/verifying-your-work.md) - the commands that decide.
