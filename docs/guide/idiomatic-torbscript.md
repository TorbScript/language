---
title: Idiomatic TorbScript
summary: The habits that make TorbScript read like TorbScript - names, mutation, calls, types, errors, closures, resources and tasks - each as one rule, one runnable example and the reason behind it.
kind: guide
status: stable
order: 130
prerequisites:
  - a-small-program.md
keywords:
  - idiom
  - style
  - conventions
  - best practices
  - canon
source:
  - CONCEPT.md#design-principles
  - CONCEPT.md#lexical-structure
  - CONCEPT.md#formatter-canon
  - CONCEPT.md#decision-log
---

A program can compile and still not read like TorbScript. This page collects the habits the standard library and the
compiler follow, one rule per section: the rule in bold, the smallest program that shows it, and one sentence of why.
Every rule links the reference page that has it in full.

The mistakes a programmer brings from Rust, Swift, Kotlin or TypeScript are not repeated here - they are on
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md), each with the wrong line
and the diagnostic. This page is the other half: what to write once the program compiles.

## Goal

At the end of this page you write TorbScript that a reader of the standard library recognizes as their own: the names,
the calls, the types and the error handling all in the one form the language picked for them.

## Contents

- [Names are written out](#names)
- [A single-method trait is named like its method](#traits)
- [A field never starts with `is`](#bool-fields)
- [A verb changes in place, a participle answers a copy](#verbs-and-participles)
- [Each collection has its own words](#collection-words)
- [A statement call is a command](#command-calls)
- [One statement per line](#one-statement-per-line)
- [A member does not list `self`](#members)
- [A type is a value unless it needs an identity](#values)
- [An invariant lives in a capsule](#capsules)
- [A field default is a constant](#field-defaults)
- [An expected failure is a `Result`](#errors)
- [A type with cases is taken apart with `match`](#match)
- [A closure is short, and trails its call](#closures)
- [A closure over a `var` does not escape](#closures-over-var)
- [A closure parameter names its shape](#closure-types)
- [A resource is bound with `using`](#resources)
- [A task answers a `Result` when it is awaited](#tasks)
- [`Into` comes from `From`](#conversions)
- [An operating system branch is a `match`](#operating-system)
- [A collection is walked with `for` or a pipeline](#loops-and-pipelines)
- [`torb canon` decides the layout](#canon)
- [Habits from other languages](#habits)

## Names are written out {#names}

**A name is a whole word: `Expression`, `absolute`, `squareRoot`, `Item` - never `Expr`, `abs`, `sqrt`, `T`.** The
exceptions are the abbreviations that already are the name (`Html`, `Json`, `Http`, `Int64`, `min`, `max`).

```trb run
fn squareRoot(value: Float): Float {
  value.squareRoot()
}

fn firstOrDefault<Item>(items: List<Item>, fallback: Item): Item {
  items.first() ?? fallback
}

print squareRoot(16.0)             // prints 4.0
print firstOrDefault([3, 4], 0)    // prints 3
```

Why: a name is read far more often than it is typed, and one spelling per word means a search finds every use. See
[Naming](../language/syntax/naming.md).

## A single-method trait is named like its method {#traits}

**A trait with one required method takes that method's name: `Hash`, `Show`, `Close`, `Iterate`, `Equals`, `From`.
Nothing ends in `-able`.** A trait that is mainly used as a type is a noun instead: `Iterator`, `Source`, `Sink`.

```trb run
trait Describe {
  fn describe(): String
}

type Planet with Describe, Equals {
  name: String

  fn describe(): String {
    "the planet {name}"
  }

  fn equals(other: Planet): Bool {
    name == other.name
  }
}

print Planet("Mars").describe()    // prints the planet Mars
```

Why: `type Planet with Describe` reads as a sentence, and the trait and the call it stands for can never drift apart.
See [Traits](../language/traits/traits.md).

## A field never starts with `is` {#bool-fields}

**A `Bool` field is an adjective or a participle: `enabled: Bool`, not `isEnabled: Bool`.** A method that answers a
`Bool` may start with `is` or `has` (`isEmpty()`), and need not - `enabled()` is fine where the type has no field of
that name, because a field and a method share one namespace.

```trb run
type Feature {
  name: String
  enabled: Bool = false
  var tags: List<String> = []

  fn isTagged(): Bool {
    !tags.isEmpty()
  }
}

const search = Feature "search"
print "{search.enabled} {search.isTagged()}"    // prints false false
```

Why: a field is data and `is` reads as a question, so the prefix tells a reader where the work is. See
[Naming](../language/syntax/naming.md), rule 11.

## A verb changes in place, a participle answers a copy {#verbs-and-participles}

**A method that changes its receiver is a verb and a `var fn`; its twin that answers a changed copy is the participle
and an ordinary `fn`.** `append`/`appended`, `sort`/`sorted`, `translate`/`translated`.

```trb run
type Counter {
  var value: Int = 0

  var fn increment() {
    value = value + 1
  }

  fn incremented(): Counter {
    copy(value: value + 1)
  }
}

var counter = Counter()
counter.increment()
const next = counter.incremented()
print "{counter.value} {next.value}"    // prints 1 2
```

Why: the name alone says whether the receiver changes, and calling the verb through a `const` makes the compiler
suggest the participle. See [Verbs and participles](../language/types/verbs-and-participles.md).

## Each collection has its own words {#collection-words}

**A list appends, a set inserts, a map sets, a stack pushes and pops, a queue enqueues and dequeues, and all of them
remove.** An empty collection is the literal `[]` - for a list, a set, a stack or a queue - and `[:]` for a map, with
the type on the binding.

```trb run
var names: List<String> = []
names.append "Ada"

var seen: Set<String> = []
seen.insert "Ada"

var scores: Map<String, Int> = [:]
scores.set "Ada", 3
scores["Grace"] = 5

var undo: Stack<String> = []
undo.push "typed"

var pending: Queue<Int> = []
pending.enqueue 1

print "{names} {seen.length()} {scores.length()}"    // prints ["Ada"] 1 2
print "{undo.pop()} {pending.dequeue()}"             // prints Some("typed") Some(1)
```

Why: there is no shared `add`, so a call says what it does without a look at the receiver's type, and `add` only ever
means `+`. See [Lists](../language/collections-and-iteration/lists.md),
[Maps and sets](../language/collections-and-iteration/maps-and-sets.md) and
[Stacks and queues](../language/collections-and-iteration/stacks-and-queues.md).

## A statement call is a command {#command-calls}

**A call at the start of a statement, after `=`, after `return` or after `=>` is written without parentheses.**
Parentheses stay where the call is nested, has no arguments, has an operator at the top level of an argument, or
stands in the head of an `if`, `for`, `while` or `match`.

```trb run
fn route(path: String, to: String) {
  print "{path} -> {to}"
}

route "/health", to: "health"    // prints /health -> health
const doubled = [1, 2].map { _ * 2 }
print(doubled.toList().length() + 1)    // prints 3
print Some(doubled.toList())            // prints Some([2, 4])
```

Why: a statement then reads like a built-in one, so `unless`, `test` and a DSL of your own look like the language
itself. See [Command calls](../language/syntax/command-calls.md).

## One statement per line {#one-statement-per-line}

**A statement ends at the end of its line. There are no semicolons, and a body of two statements is two lines.**

```trb run
fn greeting(name: String): String {
  const loud = name.toUpperCase()
  "Hello, {loud}"
}

print greeting("Ada")    // prints Hello, ADA
```

```trb error
const answer = 42;
// error: There are no semicolons. A statement ends at the end of its line
```

Why: a line is the unit a reader, a diff and an error message all point at. See
[Lexical structure](../language/syntax/lexical-structure.md).

## A member does not list `self` {#members}

**A method is `fn area(): Int`, a changing method is `var fn grow(by: Int)`, and a member of the type itself is
`static fn square(size: Int): Self`.** The receiver is never in the parameter list; `self` is still an expression in
the body.

```trb run
type Rectangle {
  width: Int
  height: Int

  fn area(): Int {
    width * height
  }

  static fn square(size: Int): Self {
    Self size, size
  }
}

print Rectangle.square(3).area()    // prints 9
```

Why: the parameter list is exactly what a caller writes, and the two words in front of `fn` say what kind of member it
is. See [Methods and `static fn`s](../language/types/methods.md).

## A type is a value unless it needs an identity {#values}

**Write `type` by default. Write `shared type` only for a thing that has an identity - a connection, a file, a
window - where everybody holding it has to see the same object.**

```trb run
type Point {
  var x: Int
  var y: Int
}

var start = Point 0, 0
var moved = start
moved.x = 5
print "{start.x} {moved.x}"    // prints 0 5
```

Why: a value is never aliased, so a change happens exactly where it is written and nowhere else. See
[Copies](../language/execution/copies.md) and [Shared types](../language/types/shared-types.md).

## An invariant lives in a capsule {#capsules}

**A type whose values must satisfy a rule has a `private` field without a default and a `static fn` factory that
checks the rule.** The private field closes the constructor, so the factory is the only way in.

```trb run
type Percent {
  private value: Int

  static fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Ok Self(value)
  }

  fn percent(): Int {
    value
  }
}

print Percent.tryFrom(120)                        // prints Fail("120 is not between 0 and 100")
print Percent.tryFrom(40).map({ _.percent() })    // prints Ok(40)
```

Why: a constructor never contains logic, so a check that runs on every value needs one door that cannot be walked
around. See [Data or capsule](../language/types/data-or-capsule.md).

## A capsule's field is named `value` {#capsule-field-naming}

**A capsule's one stored field is named `value`; several are named for the method each answers, plus `Value`, or
`Values` for a plural.** A field and a method never share a name, so the field never borrows the accessor's word.

```trb run
type Distance {
  private value: Int

  fn meters(): Int {
    value
  }
}

print Distance(5).meters()    // prints 5
```

Why: `value` cannot collide with any accessor, and `rootValue` next to `fn root()` or `componentValues` next to
`fn components()` reads at a glance which of the two is the storage. See
[Naming](../language/syntax/naming.md) and [Data or capsule](../language/types/data-or-capsule.md).

## A field default is a constant {#field-defaults}

**A field default is a literal, a constant, a constructor of constants or an empty collection literal. Anything that
has to be computed goes into a `static fn`.**

```trb run
type Buffer {
  var slots: List<Int> = []
  capacity: Int = 16

  static fn filled(capacity: Int): Self {
    Self List.filled(capacity, 0), capacity
  }
}

print Buffer().capacity                   // prints 16
print Buffer.filled(3).slots.length()     // prints 3
```

```trb error
type Buffer {
  var slots: List<Int> = List.filled 16, 0
}
// error: A field default is a constant: `List.filled` is a call - compute it in a `static fn`, or start from `[]`
```

Why: the generated constructor has no body to run code in, and building a value never fails. See
[Construction](../language/types/construction.md), rule 4.

## An expected failure is a `Result` {#errors}

**A function that can fail answers `Result<Value, Failure>`. A caller hands the failure on with `?`, replaces it with
`??`, or takes it apart with `match`.** `panic` is for a state the program considers impossible, never for bad input.

```trb run
fn port(text: String): Result<Int, String> {
  const number = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if number < 1 || number > 65535 {
    return Fail "{number} is not a port"
  }
  Ok number
}

print port("8080")             // prints Ok(8080)
print(port("http") ?? 80)      // prints 80
```

Why: a failure in the signature cannot be forgotten, and one `?` is all the ceremony handing it on costs. See
[Result](../language/errors/result.md), [The question mark operator](../language/errors/question-mark.md) and
[panic](../language/errors/panic.md).

## A type with cases is taken apart with `match` {#match}

**A `match` covers every case, and a case is written with its type or a leading dot - bare only when the file imports
it.** `Some`, `None`, `Ok` and `Fail` are bare because the prelude imports them.

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 3.0 * radius * radius
    .Rectangle(width, height) => width * height
  }
}

print area(Shape.Rectangle(2.0, 3.0))    // prints 6.0
```

Why: an exhaustive `match` turns a new case into a list of compile errors at every place that has to learn about it.
See [Cases and match](../language/pattern-matching/cases-and-match.md) and
[Why cases are never bare](../explanation/why-cases-are-never-bare.md).

## A closure is short, and trails its call {#closures}

**A one-line closure uses the implicit parameters `_`, `_2`, `_3`, and a closure that is the last argument follows the
call.** There is no currying: a function takes all its arguments at once, and a function with some of them fixed is a
closure with a placeholder.

```trb run
fn scaled(value: Int, by: Int): Int {
  value * by
}

const numbers = [1, 2, 3]
const total = numbers.fold 0 { _ + _2 }
const doubled = numbers.map({ scaled _, by: 2 })
print total                  // prints 6
print doubled.toList()       // prints [2, 4, 6]
```

Why: one closure form and one call form cover what currying, method references and lambdas cover elsewhere. See
[Closures](../language/functions/closures.md) and [Trailing closures](../language/functions/trailing-closures.md).

## A closure over a `var` does not escape {#closures-over-var}

**A closure that reads or writes a `var` binding is handed straight to a call that only runs it - `forEach`, `unless`,
a DSL block - and is never stored or returned.** To carry a value out, return it.

```trb run
fn total(numbers: List<Int>): Int {
  var sum = 0
  numbers.forEach { sum = sum + _ }
  sum
}

print total([1, 2, 3])    // prints 6
```

```trb error
fn counter(): () => Int {
  var count = 0
  {
    count = count + 1
    count
  }
}
// error: This closure captures the `var` binding `count` and may outlive it
```

Why: a `var` is the one variable the language shares, and a closure that outlived it would share it with nobody's
knowledge. See [Closures](../language/functions/closures.md), rule 8.

## A closure parameter names its shape {#closure-types}

**A parameter that takes a question about one value is a `Predicate<Item>`, one called for its effect is an
`Action<Item>`, and one that turns a value into another is a `Transform<Item, Output>`.** The three are aliases in
the prelude, so any closure of the shape fits, and the signature says what the closure is for before it says what it
looks like.

```trb run
fn countWhere(numbers: List<Int>, predicate: Predicate<Int>): Int {
  numbers.filter(predicate).count()
}

fn labels(numbers: List<Int>, transform: Transform<Int, String>): List<String> {
  numbers.map(transform).toList()
}

const numbers = [3, 8, 5]
const large = countWhere numbers { _ > 4 }
const shown = labels numbers { "#{_}" }
print large    // prints 2
print shown    // prints ["#3", "#8", "#5"]
```

Why: `filter`, `forEach` and `map` of the standard library are written this way, and a signature that reads like
theirs needs no second look. A closure without parameters stays `() => Value`, which is already as short as a name.
See [Predicate, Action and Transform](../standard-library/function-types.md).

## A resource is bound with `using` {#resources}

**A resource is a `shared type` with `Close`, bound with `using`. Nothing calls `close()`: it runs once, when the
last holder goes away at the end of its block.**

```trb run
shared type Log with Close {
  name: String

  fn write(message: String) {
    print "{name}: {message}"
  }

  var fn close() {
    print "{name} closed"
  }
}

fn work() {
  using log = Log "audit"
  log.write "started"
}

work()
// prints audit: started
// prints audit closed
```

Why: the line where a resource is released is the end of the block, so no path through the function forgets it. See
[Destructors](../language/execution/destructors.md).

## A task answers a `Result` when it is awaited {#tasks}

**A function that waits returns `Task<Value>`. `await()` answers `Result<Value, Cancelled>`, `outcome()` folds the
cancellation into the task's own `Result`, and `cancel()` asks a task to stop.**

```trb run
fn doubled(value: Int): Task<Int> {
  value * 2
}

fn sum(): Task<Result<Int, Cancelled>> {
  const first = doubled(1).await()?
  const second = doubled(2).await()?
  Ok(first + second)
}

print(sum().outcome() ?? 0)    // prints 6
```

Why: every task can be cancelled, so a wait that cannot fail would be a lie, and `?` hands a cancellation on like any
other failure. See [Tasks](../language/concurrency-and-streams/tasks.md) and
[std/task](../standard-library/task.md).

## `Into` comes from `From` {#conversions}

**Implement `From<Source>` on the target and `into()` exists for free. `Into` is never implemented by hand, and there
are no casts.**

```trb run
type Celsius {
  degrees: Float
}

type Fahrenheit {
  degrees: Float
}

extend Celsius with From<Fahrenheit> {
  static fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) * 5.0 / 9.0)
  }
}

const boiling: Celsius = Fahrenheit(212.0).into()
print boiling.degrees    // prints 100.0
```

Why: one direction written by hand means one place to change, and `?` finds the same `From` when it converts a
failure. See [Conversions](../language/types/conversions.md).

## An operating system branch is a `match` {#operating-system}

**Code that differs per operating system is a `match OperatingSystem.current` in the function where it differs.** The
compiler checks every arm on every machine and builds only the one the target takes.

> **Planned.** `OperatingSystem.current` does not exist yet; [The Operating System](../design/OS.md) is the decided
> design. The block below parses and is not type checked.

```trb
fn searchPathSeparator(): String {
  match OperatingSystem.current {
    .Windows => ";"
    .Linux | .MacOs | .FreeBsd => ":"
  }
}
```

Why: a new operating system then becomes a compile error at every place that has to learn about it, instead of a
branch hidden in C or a build file.

## A collection is walked with `for` or a pipeline {#loops-and-pipelines}

**A loop that does something per item is a `for`. A computation from one collection to another is a pipeline: lazy
stages, one per line, and one terminal operation at the end.** There is no index loop and no `iter()` step.

```trb run
const words = ["pipeline", "for", "stage", "match"]

for word in words {
  if word.byteLength() == 3 {
    print word    // prints for
  }
}

const long = words
  .filter { _.byteLength() > 4 }
  .map { _.toUpperCase() }
  .toList()
print long    // prints ["PIPELINE", "STAGE", "MATCH"]
```

Why: nothing in a pipeline runs until the terminal operation pulls, so stages compose without intermediate
collections. See [Iterating](../language/collections-and-iteration/iterating.md) and
[Pipelines](../language/collections-and-iteration/pipelines.md).

## `torb canon` decides the layout {#canon}

**Where the language allows two spellings, `torb canon` picks one, and the canon is what a file is written in.** Run it
before committing, and let `--check` fail a build that is not in it.

```console
torb canon src
torb canon --check src
```

Why: a style that is decided once is never discussed again. See [torb canon](../tooling/torb-canon.md) and
[Verify your work](../tooling/verifying-your-work.md).

## Habits from other languages {#habits}

`let`, `Err`, a bare case, `print("x")`, `Hashable`, `MAX_SIZE`, `list.add(x)`, `fn area(self)` and the rest of what
Rust, Swift, Kotlin and TypeScript teach are listed, each with the right line and the diagnostic, on
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md). The contrast pages go
through one language each: [Rust](../explanation/coming-from-rust.md), [Swift](../explanation/coming-from-swift.md),
[Kotlin](../explanation/coming-from-kotlin.md) and [TypeScript](../explanation/coming-from-typescript.md).

## Next

- [The language reference](../language/index.md) - the exact rule behind every section above.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language on one page.
- [Why the language is like this](../explanation/index.md) - the arguments behind these habits.
