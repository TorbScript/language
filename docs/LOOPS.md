# Loops as Expressions

**Status: proposed** — `for`, `while` and `loop` are statements only today; none of the slices of section 11 has
started.

`if` and `match` are statements and expressions; `for`, `while` and `loop` are statements only. This is the
specification of what it would mean for the three loops to produce a value as well: what the value is, when the body
runs, what `break`, `continue`, `return` and `?` mean inside one, and which of the two shapes the language can afford.
The whole design turns on one question, and section 2 is that question.

```text
  for user in users {            statement position            expression position
    if ... { continue }       ──────────────────────────    ──────────────────────────
    user.name                    runs, produces nothing        runs, produces a List<String>
  }                              `break`, `continue`,          the same body, the same
                                 `return`, `?`, outer `var`    control flow, the value kept
                                 all mean what they say        instead of discarded
```

- **[1. What is being asked](#1-what-is-being-asked)** — the three meanings the loop would carry
- **[2. Eager, lazy, or following the source](#2-eager-lazy-or-following-the-source)** — the question everything else depends on
- **[3. Where this exists already](#3-where-this-exists-already)** — thirteen designs, and the three lessons they agree on
- **[4. The recommendation](#4-the-recommendation)** — a loop expression runs where it stands
- **[5. The rules](#5-the-rules)** — nine of them, each with a probe
- **[6. What eager costs](#6-what-eager-costs)** — said out loud, including what it takes from the sketch
- **[7. Two ways to write one thing](#7-two-ways-to-write-one-thing)** — where the boundary is, and why it is not a synonym
- **[8. What a lazy variant would need](#8-what-a-lazy-variant-would-need)** — the state machine, and the three things it costs
- **[9. Generators and coroutines](#9-generators-and-coroutines)** — `yield`, `Source.produce`, and values in as well as out
- **[10. What the language and the IR must provide](#10-what-the-language-and-the-ir-must-provide)**
- **[11. Slices](#11-slices)**
- **[12. Open, for the owner](#12-open-for-the-owner)**

Every snippet below was run against the checker, in `tests/language/` so that `std` resolves, and the ones marked
**type checks today** were also run. A snippet in the proposed syntax is marked **needs gap N** and names the gap of
section 10 that is in its way.

---

## 1. What is being asked

```trb fragment
const names = for user in users {
  if user.name.startsWith("a") {
    continue                       // filter
  }
  user.name                        // the body's value is the element
}
const withoutB = names.filter { !_.startsWith("b") }
print withoutB.toList()

fn mapUsers(users: List<User>): Iterable<MappedUser> {
  for user in users {
    MappedUser(name: user.name, age: user.age)
  }
}
```

Three meanings, and they are the whole of the idea:

| In the body | What it means for the sequence |
|---|---|
| the body's value | the element — what `map` does |
| `continue` | no element for this round — what `filter` does |
| `break` | the sequence ends here — what `takeWhile` does |

Nothing about those three says **when the body runs**, and that is the only thing the two candidate designs disagree
about. Everything else — the element, the filter, the end, the nesting, `break value`, the diagnostics — follows from
the answer.

## 2. Eager, lazy, or following the source

### The three candidates

**Lazy.** The loop expression answers a sequence that runs the body when somebody pulls from it. The body becomes a
state machine, split at the end of each round, with the live locals in a frame — the transformation
[BACKEND](BACKEND.md) 5.3 builds for tasks. This is what the language has no word for today, and it is what
[CONCEPT](../CONCEPT.md)'s open `yield` question is about.

**Eager.** The loop expression runs where it stands, exactly as the loop statement does, and its value is the elements
it produced. The body is the same body, in the same function, with the same control flow.

**Following the source.** Scala's answer: `for (x <- xs) yield f(x)` is strict over a `List` and lazy over a
`LazyList`, an `Iterator` or a `View`.

### Following the source is not transplantable, and the reason is precise

Scala's `for`/`yield` is not a construct with an evaluation strategy of its own — it **desugars to the subject's own**
`map`, `flatMap` and `withFilter`. Its strictness is `List.map`'s strictness, and Scala's collection library has two
families of `map`: `List.map` answers a `List` and `LazyList.map` answers a `LazyList`.

TorbScript has one family, and its `map` is lazy:

```trb fragment
fn map<Output>(transform: (value: Item) => Output): Iterable<Output> {
  Mapped self, transform
}
```

`[1, 2, 3].map({ _ * 2 })` is a `Mapped`, not a `List` — verified, and it is the design of
`std/iteration/src/stages.trb`. So the identical desugaring applied here gives **lazy for every subject**, including a
list. There is no strict `map` for the strictness to follow.

The alternative reading — a rule keyed on the static type of the subject, `List` eager and a trait-typed `Iterable`
lazy — is worse than the trap it is meant to avoid. It would make

```trb fragment
const a = for user in users { user.name }              // eager, `users` is a `List<User>`
const b = for user in users.filter(active) { user.name }   // lazy, the filter answers an `Iterable<User>`
```

two different programs that differ in one word inside the head, and the word that decides is a type the reader has to
infer. An evaluation strategy the reader cannot see in the syntax is the objection, and this candidate moves it from a
signature to a subexpression. **Rejected.**

### Eager against lazy, on the four hinges

| | Eager | Lazy |
|---|---|---|
| **`var sum = 0; for … { sum = sum + x; … }`** | works, because the body runs where it stands | forbidden, or silently operating on a copy — see section 8 |
| **`return` and `?` in the body** | work, and mean what they mean in a loop statement | cannot work: the body outlives the function that wrote it |
| **The same body in two positions** | one meaning: it runs, and the value is kept or discarded | two meanings: `fn f(): Iterable<X>` defers the body, `fn f()` runs it |
| **An endless or very large source** | cannot be walked; `.map`/`.filter`/`take` is the spelling | walks it, and fuses into one pass |

The first three are the objections. The fourth is what lazy buys, and it is real: a loop expression over
`file.lines()` would materialise the file, and over `(0..)` it would not end at all.

**The fourth already has a spelling, and the first three do not.** `(0..).map({ _ * _ }).take(5).toList()` **type
checks today** and answers `[0, 1, 4, 9, 16]`; a closure that wants `break`, a `?` or an outer `var` has nothing:

```trb fragment
texts.map({ text: String =>
  if text.isEmpty() {
    break
  }
  size(text)?
})
```

```text
error: `break` is only allowed inside of a `for`, a `while` or a `loop`
  --> probe.trb:17:7
   |
17 |       break
   |       ^^^^^
   = A closure is a function, not a block: `break` cannot leave one, and `do { }` is not a loop either
```

So the two candidates are not symmetric. Lazy adds a second way to write what the pipeline already writes and takes
away three things the loop statement has; eager adds what the pipeline cannot write and takes away nothing from the
loop statement. Section 4 takes eager, section 6 says what that costs, and section 8 keeps the lazy design written
down so the decision stays informed.

## 3. Where this exists already

Thirteen designs, with the four columns that matter. "Element" is how the value of one round is named; "control flow"
is whether `break`, `continue` and `return` work inside.

| | Eager or lazy | Filtering | Element | Control flow | One syntax or two |
|---|---|---|---|---|---|
| **Scala** `for (x <- xs) yield e` | the subject's: strict on `List`, lazy on `LazyList`/`Iterator`/`View` | `if` guard in the head, desugared to `withFilter` | `yield e` | none: `break` is a library exception, `return` inside is a non-local return that Scala 3 deprecated | two — `for`/`yield` and the method chain it desugars to |
| **Python** `[e for x in xs]` | eager | `if` clause in the head | the leading expression | none | two — `[…]` eager, `(…)` lazy, one bracket apart |
| **Python** `def f(): … yield e` | lazy, one-shot | an `if` that does not yield | `yield e` | all three: it is a function body with real loops, and `return` ends the generator | — |
| **Rust** iterator adapters | lazy | `.filter(…)` | `.map(…)` | none inside a closure | `for` stays a statement on purpose |
| **Rust** `gen { … }` (unstable) | lazy | an `if` that does not yield | `yield e` | `break`/`continue` inside the block's own loops; `return` ends the iteration; `?` is the open part of the design | two — adapters and `gen` |
| **Kotlin** `sequence { … }` | lazy | an `if` that does not yield | `yield(e)`, `yieldAll(…)` | `break`/`continue` in the block's own loops, `return@sequence` | two — `sequence { }` and `Sequence`'s adapters. `for` is a statement; `if`, `when` and `try` are expressions |
| **C#** LINQ query syntax | lazy, like the method syntax | `where` | `select` | none | two for one thing — and see below |
| **Haskell** `[e | x <- xs, p x]` | lazy, like everything | a guard in the head | the head expression | none | two — comprehensions and `do` notation over the list monad |
| **Elixir** `for x <- xs, p.(x), do: e` | **eager**, and `into:`/`reduce:` choose the target | a bare boolean in the head | the `do:` body's value | none — the language has none | two vocabularies: `for` eager, the `Stream` module lazy |
| **Julia** `[e for x in xs if p(x)]` | eager | `if` in the head | the leading expression | none | two — `[…]` eager, `(…)` lazy |
| **Racket** `for/list`, `for/stream`, `for/fold`, `for` | **the form name says**: `for/list` eager, `for/stream` lazy, `for/fold` accumulates, `for` is the side-effect loop | `#:when` | the body's value | `#:break`, `#:final` | one family, and the keyword carries the answer |
| **Ruby** `xs.map { … }`, `xs.lazy.map { … }` | eager on an `Array`, lazy after `.lazy` | `select` | the block's value | **all three work** — `break` leaves the method that yielded, `next` is continue, `return` is a non-local return | one syntax, laziness opt-in |
| **Zig, Rust** `loop { break v }`, labelled blocks | — | — | — | — | the loop expression exists and means **one final value**, not a sequence |
| **F#** `[ for x in xs -> e ]`, `seq { … }` | the bracket says: `[…]` eager, `seq { }` lazy | `when`/`if` | `yield e`, `->` as its shorthand, `yield!` to flatten | `return` is not a thing in F# | two delimiters, one grammar |

**Three lessons, and all thirteen agree on them.**

**1. No language makes one syntax mean both.** Every language that has an eager and a lazy comprehension spells the
difference in the syntax: Python and Julia in the bracket, F# in the delimiter, Racket in the form name, Elixir in the
module. Scala is the apparent exception and is not one — its answer is the subject's `map`, which section 2 shows is
not available here. The objection "the same body means lazy or not depending on the signature" is therefore not a
detail of one design; it is the thing every existing design went out of its way to avoid.

**2. A loop that *is* an expression means one value, not a sequence.** Rust's `loop { break x }` and Zig's
`break :label value` are the only loop expressions in production languages, and both answer the loop's one final
result. "A loop is a sequence" exists only in comprehension grammars — `for x <- xs yield e`, `[e for x in xs]` —
which are a different form with a different head. Making `for` a sequence expression is therefore a real choice and it
permanently spends `break value` on something else. Section 5, rule 6.

**3. Control flow inside works exactly where the body is not a closure.** Python's generator, Kotlin's `sequence`,
Rust's `gen` and Ruby's block all let `break`/`continue`/`return` through, and every one of them pays for it: Python
and Rust by making `return` mean "end the sequence", Kotlin by needing a labelled `return@sequence`, Ruby by making
blocks a construct that is not a closure. Where the body is an ordinary closure — Scala, C#, Haskell, Elixir, Julia,
the Rust adapters — none of the three works at all. **An eager loop expression is the one shape where the body is
neither: it is the block it looks like, in the function it is written in.**

**What happened to C#'s two syntaxes**, since it is the case the language's own "one obvious way" argument points at:
query syntax cannot express most of what LINQ has — no `ToList`, no `Skip`, no `Any`, no `First` — so any query past
the simplest mixes `from … select` with `.ToList()` anyway, and the method syntax became the one people write. Two
syntaxes for one thing did not settle into a boundary; one of them won and the other stayed a thing that has to be
taught. Section 7 is why the proposal here is not that case.

## 4. The recommendation

**A `for`, `while` or `loop` in expression position runs where it stands and answers a `List<Body>`.** In statement
position it is the loop of today, unchanged.

The owner's two snippets work as written:

```trb fragment
const names = for user in users {
  if user.name.startsWith("a") {
    continue
  }
  user.name
}
const withoutB = names.filter({ !_.startsWith("b") })
print withoutB.toList()
```

**needs gap 1.** `names` is a `List<String>`; `filter` is the ordinary lazy stage over it, and `toList()` pulls it.

```trb fragment
fn mapUsers(users: List<User>): Iterable<MappedUser> {
  for user in users {
    MappedUser(name: user.name, age: user.age)
  }
}
```

**needs gap 1.** A `List<MappedUser>` satisfies an `Iterable<MappedUser>` result without a conversion — the
hand-written form of exactly this **type checks today** and runs.

### The lowering is the loop that exists, plus the list that exists

```trb fragment
const names = for user in users { … }

// becomes

var result: List<String> = []
for user in users {
  …
  result.add …
}
result
```

`continue` reaches the loop's own continue, `break` its own break, `return` leaves the function, `?` leaves the
function with a `Fail`, and a `var` the body changes is the `var` it names. **Nothing in the IR, in the lowering of
tasks, in either back end or in the runtime changes.** The whole of the design below is the parser and the checker,
and section 10 is four numbered gaps rather than fourteen.

The hand-written form of every snippet in this section **type checks today and runs natively**:

```trb check
type User {
  name: String
  age: Int
}

fn names(users: List<User>): List<String> {
  var result: List<String> = []
  for user in users {
    if user.name.startsWith("a") {
      continue
    }
    result.add user.name
  }
  result
}

const users = [User("ada", 36), User("alan", 41), User("grace", 45)]
const withoutB = names(users).filter({ !_.startsWith("b") })
print withoutB.toList()
```

```console
$ torb run probe.trb
["grace"]
```

## 5. The rules

### Rule 1 — position decides, exactly as it does for `if`

A loop in **expression position** — the initialiser of a binding, an argument, the operand of a `return`, the value of
a block that is used — has the type `List<Body>` and produces elements. A loop in **statement position** is the
statement of today: it runs, it produces nothing, and its type is `Void` (or `Never`, for a `loop` without a `break`).

This is the mechanism `if` and `match` already use: `checkBlock` knows whether a block's value is used
([TYPECHECKER](TYPECHECKER.md) 5.4), and the expected type says what is wanted. The parser is the part that has to
change, because a loop keyword in expression position does not parse:

```trb fragment
const names = for user in users {
  user.name
}
```

```text
error: Expected an expression, found a keyword
 --> probe.trb:8:15
  |
8 | const names = for user in users {
  |               ^^^
```

**The two readings never overlap**, which is what makes the rule safe rather than merely defined. The body of a loop
*statement* already refuses a trailing value:

```trb error
const users = ["ada", "alan"]

for user in users {
  user
}
// error: This value is not used
```

So a body that ends in a value is an error in statement position today and becomes the element in expression position,
and a body that ends in a statement is fine in statement position and is rule 2's error in expression position. No
program changes meaning.

### Rule 2 — the body's value is the element, and a body that ends in a statement has none

The element type is the type of the body's last statement when that is an expression statement. When it is not, the
loop expression has no element type, and that is an error rather than a `List<Void>`:

```text
error: This loop produces no elements: its body ends in a statement
  --> probe.trb:3:15
   |
 3 | const names = for user in users {
   |               ^^^
   = The value of the last expression of the body is the element. Write one, or use the loop as a statement
```

**`if` without an `else` as the last expression of the body is an error, and it is one today.** The tempting reading —
"an element, or nothing" — is not available, because an `if` without an `else` does not carry a value anywhere in the
language:

```trb error
const condition = 3 > 2

const chosen = if condition {
  1
}
print chosen
// error: This value is not used
```

The branch is checked as a statement, so the value is rejected before any question about loops arises. Giving one
construct a rule that makes `if c { x }` mean `x`-or-skip would be a second meaning for a form that has one meaning
everywhere else, and the language already has a word for "skip": `continue`. So the guard goes to the front, where a
reader looks for it:

```trb fragment
for user in users {
  if user.name.startsWith("a") {
    continue
  }
  user.name
}
```

or `else { continue }` says it in place. The diagnostic inside a loop expression body names both.

### Rule 3 — `return`, `?`, `panic`, `break` and `continue` mean what they mean in a loop statement

This is the rule that is not a rule: the body runs in the function it is written in, so nothing about it is special.

```trb check
type TooLong with Show, Error {
  text: String

  fn show(): String {
    "`{text}` is too long"
  }
}

fn size(text: String): Result<Int, TooLong> {
  const length = text.chars().count()
  if length > 4 {
    return Fail(TooLong(text))
  }
  Ok length
}

fn checked(texts: List<String>): Result<List<Int>, TooLong> {
  var result: List<Int> = []
  for text in texts {
    result.add(size(text)?)
  }
  Ok result
}

print checked(["ab", "cd"])
print checked(["ab", "toolong"])
```

**type checks today** and answers `Ok([2, 2])` and `` Fail(`toolong` is too long) ``. With gap 1 the same function is

```trb fragment
fn checked(texts: List<String>): Result<List<Int>, TooLong> {
  Ok(for text in texts {
    size(text)?
  })
}
```

`return` leaves the function and the partial list goes with it, which is what a `return` out of a loop statement does
to a `var` it was filling. `break` ends the sequence and the elements collected so far are the value — the one place
`break` means something the statement form has no use for. `panic` is unchanged.

**The closure spelling cannot do this, and it does not say so.** `?` inside a closure type checks and, when it fires,
ends the program:

```trb check
type TooLong with Show, Error {
  text: String

  fn show(): String {
    "`{text}` is too long"
  }
}

fn size(text: String): Result<Int, TooLong> {
  if text.chars().count() > 4 {
    return Fail(TooLong(text))
  }
  Ok(text.chars().count())
}

fn direct(text: String): Result<Int, TooLong> {
  const step = { value: String => size(value)? }
  Ok(step(text))
}

print direct("ab")
```

```console
$ torb run probe.trb                 # with direct("toolong")
error: `toolong` is too long
Ok(2)
$ echo $?
1
```

The `Fail` becomes neither the closure's value nor `direct`'s failure: it ends the process with exit 1 and no
diagnostic anywhere. That is gap 5, it is older than this document, and it is the sharpest evidence for rule 3 — the
place a `?` belongs is a body the compiler knows is a block.

### Rule 4 — the body may change an outer `var`

```trb check
fn halves(start: Int): List<Int> {
  var remaining = start
  var result: List<Int> = []
  while remaining > 1 {
    remaining = remaining / 2
    result.add remaining
  }
  result
}

print halves(40)
```

**type checks today** and answers `[20, 10, 5, 2, 1]`; with gap 1 the `var result` goes away and the `while` is the
value. The head reads the `var` and the body writes it, which a lazy form could not allow at all — section 8 says why.

So `var sum = 0; for x in xs { sum = sum + x }` keeps working, and it keeps working **in both positions**: it is the
loop of today when its value is discarded, and a loop whose value is a list of partial sums when it is not. There is no
capture analysis, no `spawn` rule, and no binding that means one thing above a loop and another inside it.

### Rule 5 — evaluation order holds no surprise

A `print` in the body prints where the loop is written. There is no moment at which a sequence "is pulled" and no
program in which the order of two side effects depends on whether somebody later called `toList()`. The catch CONCEPT
names for pipelines — "a stage with side effects does nothing until it is pulled" — is the pipeline's, and the loop
expression is the construct that does not have it.

### Rule 6 — there is no `break value`

`break` ends the sequence. A `break` that instead made the loop answer one final value would give one keyword two
opposite meanings, decided by whether a token follows it, and the language refuses the syntax today:

```trb error
var found = 0
loop {
  found = found + 1
  break found
}
print found
// error: Expected the end of the statement, found a name
```

Section 3's second lesson is the argument: Rust and Zig spend their loop expression on exactly this, and a language
cannot have both readings. A loop that computes one value is `find`, `fold` or `first` over the sequence the loop
already answers, and those are three names that say which one is meant. [Loops](language/execution/loops.md) rule 4
stands unchanged.

### Rule 7 — nesting nests, and does not flatten

```trb check
fn grid(rows: List<Int>, columns: List<Int>): List<List<(Int, Int)>> {
  var outer: List<List<(Int, Int)>> = []
  for row in rows {
    var inner: List<(Int, Int)> = []
    for column in columns {
      inner.add((row, column))
    }
    outer.add inner
  }
  outer
}

print grid([1, 2], [10, 20])
```

**type checks today** and answers `[[(1, 10), (1, 20)], [(2, 10), (2, 20)]]`. An inner loop expression is an element
like any other value, so two nested loops give a list of lists and `flatMap({ _ })` is the flattening, written where
it happens.

Comprehension grammars flatten because their **head** holds several generators — `for x <- xs; y <- ys`, `[e for x in
xs for y in ys]` — and the desugaring is `flatMap` over all but the last. TorbScript's `for` head binds one pattern
over one subject, so there is no multi-generator head to desugar and nothing to flatten. Adding one would be a second
head grammar, which is the comprehension syntax this design is deliberately not.

### Rule 8 — a `loop` in expression position needs a `break`

An endless eager sequence is a program that does not end. The rule that catches it exists:
[Loops](language/execution/loops.md) rule 2 — a `loop` without a `break` that targets it has the type `Never`. What
does **not** exist is the error, because `Never` satisfies every expected type:

```trb check
fn endless(): List<Int> {
  loop {
    print "on and on"
  }
}
```

**type checks today**, which is correct for a statement (the function never returns) and is exactly the program rule 8
has to refuse in expression position. So the rule is written out rather than inherited: a `loop` used as an expression
whose body has no `break` that targets it is an error naming `break`, `Source.produce` and the pipeline.

`while` needs no such rule: `while true` is already an error that says to write `loop`, so a `while` expression cannot
be endless by its syntax. It can still be endless by a condition that never goes false — the same non-termination a
`while` statement has today, with memory spent instead of only time. Section 6.

### Rule 9 — the answer is a `List`, and the pipeline after it is the ordinary pipeline

`List<Body>`, not `Iterable<Body>`. The loop has already run, so the value carries what it knows: `length()`,
`[index]`, `Show`, `Equals`, and a `List` where a function wants an `Iterable`. Answering `Iterable` would hide that
the elements are already in memory, which is the thing the eager/lazy question is about — a type that says "a
sequence, do not count on how" would be a promise the construct does not keep in either direction.

There is no `into:` and no collector in the head. `.to<Set<String>>()` and `collect(groupingBy(…))` are how a sequence
becomes something other than a list, they work on the answer, and they are the vocabulary that already exists.

## 6. What eager costs

**1. A loop expression cannot walk an endless or very large source.** `for n in (0..) { n * n }` does not end, and
`for line in hugeFile.lines() { … }` holds the file. The spelling for both is the pipeline, it exists, and it is what
the loop statement is for when the result is not wanted at all. What is lost is the ability to write those with loop
syntax, and section 12 asks whether that is wanted enough to add a second spelling later.

**2. One materialisation more than a fused pipeline.** `for x in xs { f x }.filter(p).toList()` builds a list, then
walks it; `xs.map({ f _ }).filter(p).toList()` builds one. For the owner's example that is one list of names. Where it
matters the pipeline is the answer, and rule 9's `List` is what makes the cost visible in the type instead of hidden
behind an `Iterable`.

**3. `loop { … }` is not the infinite generator of the sketch.** This is the one thing eager takes away from what was
asked. Fibonacci as a loop expression is not available; the shapes that are: `Source.produce`, or a `type` pair of
twelve lines that **type checks today and runs**:

```trb check
use Iterable, Iterator from "std/iteration"

type Fibonacci with Iterable<Int> {
  previous: Int
  current: Int

  fn iterator(): Iterator<Int> {
    FibonacciCursor(previous: previous, current: current)
  }
}

type FibonacciCursor with Iterator<Int> {
  var previous: Int
  var current: Int

  var fn next(): Int? {
    const answer = previous
    const following = previous + current
    previous = current
    current = following
    Some answer
  }
}

const numbers = Fibonacci(previous: 0, current: 1)
print numbers.take(10).toList()
print numbers.take(3).toList()
```

`[0, 1, 1, 2, 3, 5, 8, 13, 21, 34]` and `[0, 1, 1]` — and the second line is section 8's argument in one probe.

**4. Neither of CONCEPT's open questions closes.** `yield` stays open, and `for` over a `Source` stays open: an eager
loop over a source would still need an `await` and a `?` in its head, which is the reason
[STREAMS](STREAMS.md) section 2 gives. Both bullets point here, and section 9 says what would settle them.

**5. Laziness later needs a second spelling.** Changing an eager `for` expression into a lazy one afterwards would
change what every program using it means, so a lazy form would have to be a different construct — which is what every
language in section 3 has, and what the language already half has in the pipeline.

## 7. Two ways to write one thing

`xs.map({ f _ }).toList()` and `for x in xs { f x }` produce the same list from the same input. That is two spellings,
and "one obvious way" is a rule of this language, so the boundary has to be stated rather than left to taste.

**If the body is a closure, write a closure.** A closure is an expression with parameters that answers a value; a loop
body is a block in a function. The moment the body needs something only a block has — a statement before the value,
`break`, `continue`, `return`, `?`, a change to a `var` of the surrounding scope — the closure spelling stops being
available, and section 2's probes are what "stops being available" means: `break` is a diagnostic, `?` is a program
that exits 1.

That is a boundary a compiler can point at, and it is the one Kotlin, Scala and Rust arrive at by attrition: a `map`
whose body grows past one expression degrades into a loop with `result.add`, in every one of them, and the loop it
degrades into is exactly the code the lowering of section 4 writes. The proposal is to let that code be written the
way it reads.

**It is not C#'s case**, which section 3 ends with. Query syntax and method syntax are two spellings of the *same*
evaluation, overlapping everywhere, and one of them can express less. Here the two spellings differ in the thing that
matters most about a sequence — `map` is lazy and the loop expression is eager — so the choice is a decision about the
program, not about style, and a reader who knows which one is written knows when the body runs. The vocabulary is
untouched: `map`, `filter`, `flatMap`, `take` remain the only lazy stages and they remain the only way to spell one.

## 8. What a lazy variant would need

This section is the lazy design written down, so the recommendation is a choice and not an omission.

### The two implementations, and what each costs

**A. Desugar to the pipeline.** `for x in xs { body }` becomes `xs.map({ x => body })`, `continue` becomes a
`filterMap` that answers `None`, `break` becomes a `takeWhile` in front, and a nested loop becomes `flatMap`. This is
Scala's desugaring exactly, it needs nothing from the compiler but a rewrite in the lowering, and it fails rule 3
completely: the body is a closure, so `break` is a diagnostic, `return` returns from the closure, `?` is gap 5, and a
`var` it changes is the shared box of [Closures](language/functions/closures.md) rule 7 — which makes the answer
non-re-iterable, below.

**B. Generate a state machine.** The body is split at its suspension point, the live locals move into a frame, and the
frame becomes an `Iterator`. The generated shape **type checks today and runs**:

```trb check
use Iterable, Iterator from "std/iteration"

type User {
  name: String
  age: Int
}

type MappedUser {
  name: String
  age: Int
}

/** What `for user in users { … }` would answer. */
type MapUsers with Iterable<MappedUser> {
  users: List<User>

  fn iterator(): Iterator<MappedUser> {
    MapUsersCursor users.iterator()
  }
}

/** The body as a resume function: one state, because the only suspension point is the end of the body. */
type MapUsersCursor with Iterator<MappedUser> {
  var source: Iterator<User>

  var fn next(): MappedUser? {
    while const Some(user) = source.next() {
      if user.name.startsWith("a") {
        continue
      }
      return Some(MappedUser(name: user.name, age: user.age))
    }
    None
  }
}

const mapped = MapUsers([User("ada", 36), User("alan", 41), User("grace", 45)])
print mapped.map({ _.name }).toList()
```

`["grace"]`. Two generated types per loop expression: one value type holding the captures and implementing `Iterable`,
one holding the live locals and implementing `Iterator`.

### Is it literally the transformation tasks need

**Almost, and the "almost" is the interesting part.** [BACKEND](BACKEND.md) 5.3's four steps are: split every block at
a suspension point, move the locals that live across a split into a frame, turn the function into `resume(state)` with
a `Switch` in the entry block, and allocate the frame at the start. A loop expression needs all four. Three parameters
differ:

| | A task | A loop expression |
|---|---|---|
| Where the suspension points are | every `await()` | the end of the body |
| What a resumption brings in | the awaited value | nothing |
| What a suspension carries out | nothing — the awaited task is registered | the element |
| What the frame is | a `Boxed` record owned by the `Task`, a shared object | the fields of a generated **value** type, one per cursor |
| What `resume` answers | `Poll<Value>` | `Item?` |

So the IR node `Suspend(state, awaited)` would become one node with an optional incoming operand and an optional
outgoing one, and everything above it is shared. **The important asymmetry:** a `for` over an `Iterable` has exactly
one suspension point, so its state machine has **one state** and degenerates into the cursor above — no `Switch`, no
state field. The machine earns its keep only where something lives across a round: a nested loop expression, where the
inner cursor is a field and the state distinguishes "start an inner" from "pull from the inner", which **type checks
today** as a two-state `next()` over `state`, `row`, `rows` and `inner`; and a `loop`/`while` expression, whose own
locals are the frame.

An **asynchronous** loop expression — a body containing `await()` — has both kinds of suspension point and its value is
a `Source<Body, Failure>` ([STREAMS](STREAMS.md) section 2), whose `next()` answers
`Task<Result<Item?, Failure>>`. That is the one place `?` in a lazy body would have a meaning: the failure channel
exists, so a `Fail` ends the sequence. It is also where `for` over a `Source` would become writable without a keyword
combination, because the head's own pull failure goes into the same channel and the `?` is not hidden — it is the
`Failure` of the loop's type, written at the signature. That is the strongest argument the lazy design has and it is
the one thing the eager recommendation gives up.

### `Iterable` or `Iterator`, and the answer value semantics makes possible

If a loop expression were lazy, its type would be `Iterable<Body>` and not `Iterator<Body>`, and the reason is "one
vocabulary": `Iterator` has exactly one member, so `.filter` after the loop — the owner's own second line — would not
resolve on one. `Iterable` carries the whole pipeline.

`Iterable` promises "a fresh cursor, positioned before the first one", so an `Iterable` loop expression has to be
**re-iterable**, which Python's generators and Rust's `gen` blocks are not. It can be, and the reason is the language
rather than cleverness: **every capture is a value, so a copy is what the language does anyway.** The `Fibonacci`
probe of section 6 takes `take(10)` and then `take(3)` and answers `[0, 1, 1]` the second time, because the cursor's
state is in the cursor and the `Iterable` holds only the starting values.

**And that is exactly why lazy cannot allow rule 4.** A `var` the body changes would have to be either the shared box
of a closure — and then a second `iterator()` continues where the first stopped, and the `Iterable` contract is broken
— or a copy taken when the loop expression is evaluated, and then `print total` after the loop prints the value from
before it. Both are surprises with no good diagnostic, and choosing "copy" would mean the same `{ … }` captures a
`var` by box in a closure and by copy in a loop body, which is the kind of context-dependent rule the whole objection
is about.

### The three rules lazy would force, for the record

1. **`return` in the body of a used loop expression is an error**, with a note: the body runs when the sequence is
   pulled, and the function it would return from may be gone.
2. **`?` is an error in a synchronous loop expression** and legal in an asynchronous one, because only the second has
   a failure channel. Two spellings of one character, decided by whether the body contains an `await()`.
3. **The body may not change a `var` of the surrounding scope**, for the re-iteration reason above.

Those three are the objections, and they are not incidental: each one falls out of "the body runs later", which is the
definition of lazy.

## 9. Generators and coroutines

**A bidirectional coroutine — values in as well as out, Lua's and Python's `send` — is not wanted, and the reason is
that the language already has both halves with types on them.** A task is a coroutine whose resumption brings a value
*in*: `await()` is the suspension and the awaited result is what arrives. A generator is a coroutine whose suspension
carries a value *out*. `send` is the two at once, and it costs a type with two parameters (`Coroutine<In, Out>`), a
calling convention where `resume(value)` both delivers and receives, and a reader who has to work out which side is
running. What `send` is reached for — a producer that is steered by its consumer — is a `Channel`: it has a type on
each end, `capacity: 0` is a rendezvous, and backpressure is the `await` on `add` rather than a protocol. **The
position is that "values in through `await`, values out through the sequence, and a `Channel` when both" is the whole
story**, and nothing in this document needs more.

**The generator story stays `Source.produce` and 7.3.** `Source.produce { sink => … }` runs its body as a task and
hands items through a channel, and at `capacity: 0` that *is* lock-step generation ([STREAMS](STREAMS.md) section 8).
Where the state is small, the twelve-line `type` pair of section 6 is shorter than the machinery and needs nothing.

**What would settle CONCEPT's `yield` question**, in one sentence, so the bullet has an answer to wait for: a case
where `Source.produce` is genuinely awkward — a producer whose state is large enough that a `type` pair is noise and
whose control flow is branchy enough that `pulling` cannot hold it. Section 8 is the design it would get, the
transformation is 7.3's with three parameters changed, and the eager recommendation does not stand in its way: a lazy
form would be a second construct, and section 12 asks which spelling it should have.

## 10. What the language and the IR must provide

In the order it hurts, each with the smallest fix. Gaps 1 to 4 are the recommendation; 5 is older than it; 6 and 7 are
optional.

**1. A loop in expression position parses.** `for`, `while` and `loop` become primary expressions, so
`const x = for …` and `Ok(for …)` parse. *Smallest fix:* three keyword cases in the expression parser, answering the
statement nodes that already exist with a flag for the position. It is the one grammar change in this document.

**2. The checker types a loop expression.** The element type is the type of the body's last statement when that is an
expression statement, and the loop's type is `List<Element>`; a body that ends in a statement is rule 2's error; a
`loop` in expression position without a `break` that targets it is rule 8's error. The statement path is unchanged.
*Smallest fix:* one branch in `checkBlock`'s expression-statement case — the discarded-value rule of
[TYPECHECKER](TYPECHECKER.md) 5.3 must not fire on the body's trailing expression when the loop's value is used, which
is the same condition `if` already carries.

**3. The lowering builds the list.** A temporary `List<Element>`, an `add` where the body's value is produced,
`continue` and `break` unchanged, the temporary as the loop's value. *Smallest fix:* it is a source-level rewrite in
the lowering; there is **no IR node of its own, no back-end change and no runtime change**, which is the whole practical
argument for the recommendation.

**4. The temporary is pre-sized where the subject knows its length.** A `for` over something carrying `Length` can
start the list at that capacity, which `ArrayList.withCapacity` already provides. *Smallest fix:* one condition in
gap 3's rewrite. It is an optimisation and not a rule, so it can land after the rest.

**5. `?` inside a closure.** It type checks and, when it fires, ends the process with exit 1 and no diagnostic — probe
in rule 3. This is not a cost of this document and it is not caused by it, but rule 3's argument rests on the closure
spelling being unable to do what the block does, and "unable" has to be a diagnostic rather than a crash. *Smallest
fix:* `?` in a closure whose result type is not a `Result` is an error naming the enclosing function, the same shape
[CONCURRENCY](CONCURRENCY.md) gap 1 gave `await()`.

**6. A second, lazy spelling** — only if section 12's question is answered yes. Everything section 8 lists:
the state-machine transformation generalised from [BACKEND](BACKEND.md) 5.3 (one IR node with an optional incoming and
an optional outgoing operand), two generated types per loop expression, the three rules of section 8, and the
asynchronous case answering a `Source`. It shares its whole transformation with 7.3, so it is cheapest **after** 7.3
and costs 7.3 nothing to wait for.

**7. `for` over a `Source`.** Only reachable through gap 6, and only inside a lazy loop expression whose own type
carries the failure — section 8. Nothing in gaps 1 to 4 brings it closer or moves it further away.

## 11. Slices

**None of this needs 7.3, the VM, or a back end.** That is the difference between the recommendation and the
alternative, and it decides the order.

- **Slice A — the grammar** (gap 1). `for`, `while` and `loop` parse in expression position and answer the existing
  nodes. Gate: every `.trb` of the repository still parses byte-identically, and each of the three keywords in each of
  four expression positions (binding, argument, `return` operand, block value) has a parse test.
- **Slice B — the checker** (gap 2). The element type, rule 2's error, rule 8's error, and the discarded-value rule
  stepping aside for the body's trailing expression. Gate: a `trb error` block per diagnostic; `check .` green over
  the repository, because no existing program is in expression position; and the two probes of rule 1 keep their
  current diagnostics in statement position.
- **Slice C — the lowering** (gap 3). The temporary list. Gate: for each of the nine rules a conformance program under
  `tests/conformance/` whose output is byte-identical to the hand-written loop it replaces, run in every back end —
  including `break` ending the sequence, `return` discarding it, `?` leaving the function, and a body that changes an
  outer `var`.
- **Slice D — pre-sizing** (gap 4), and the documentation: [Loops](language/execution/loops.md) rules 4 and 7 change,
  [Iterating](language/collections-and-iteration/iterating.md) gains the loop expression beside the pipeline, and
  [Pipelines](language/collections-and-iteration/pipelines.md) gains section 7's boundary. Gate: `docs check docs`,
  and the allocation counter showing one allocation per loop expression over a sized subject.

**Slice E — a lazy spelling** (gaps 6 and 7) is after 7.3 and only on a yes to section 12's first question. It shares
the state-machine transformation with tasks, so building it before 7.3 would build it twice.

`?` in a closure (gap 5) belongs to whoever owns the checker's error-conversion path and rides with no slice here; it
is named because rule 3 leans on it.

## 12. Open, for the owner

Everything technical above is decided and the reason stands next to it. These are the questions where the answer is
taste or direction.

1. **Eager, against the lazy sketch.** Section 2 is the argument and section 6 is the bill: `var`, `return`, `?` and
   evaluation order all work, `loop { … }` is not the infinite generator, a loop expression cannot walk an
   endless source, and neither of CONCEPT's open questions closes. The alternative is section 8, which costs three
   rules that are each an objection. **This is the question the rest depend on.**
2. **Whether a lazy spelling is wanted later, and what it is called.** Section 3 shows every language spells the
   difference: Python in the bracket, F# in the delimiter, Racket in the form name. The candidates here are `loop` as
   the one lazy form (which makes the trap of objection 2 smaller but real), a marked head, or nothing at all — the
   pipeline and `Source.produce`, which is where the language stands. The recommendation is nothing at all until a
   case turns up that `Source.produce` writes badly, which is also what CONCEPT's `yield` bullet waits for.
3. **`List<Body>` as the answer, against `Iterable<Body>`.** Rule 9 argues for `List`: the elements are in memory and
   the type should say so. The cost is that a function answering `Iterable` and one answering `List` are two different
   promises, and a body that wants to change from an eager loop to a pipeline changes its signature.
4. **`loop` in expression position at all.** With rule 8 it is "repeat until `break`, collecting", which is a real
   shape (read until a sentinel) and is not what was asked for. Leaving it out would give `for` and `while` the
   expression form and keep `loop` the statement it is. Uniformity says include it; the sketch's `loop` says the word
   is spoken for.
5. **Whether `for` over a `Map` in expression position should answer a `List<(Key, Value)>` or be steered by the
   expected type** (`const ages: Map<String, Int> = for … { (name, age) }`). Rule 9 says `List` and then `.to()`;
   Elixir's `into:` is the other tradition. The recommendation is `List` plus `.to()`, because the conversion has a
   name and the head stays one binding over one subject.
6. **The word in the diagnostics.** Rule 2's error says "this loop produces no elements". The alternatives are to name
   the construct ("a loop expression") or the position ("a loop whose value is used"). The second is the most accurate
   and the longest, and it is the one a reader who hit the error by accident needs.
