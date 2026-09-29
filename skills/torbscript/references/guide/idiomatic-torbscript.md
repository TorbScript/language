---
title: Idiomatic TorbScript
summary: The habits that make code read like the standard library - whole-word names, values before shared types, a checked door for every rule a value must keep, using for resources, and the formatter's layout.
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
source:
  - CONCEPT.md#design-principles
  - CONCEPT.md#lexical-structure
  - CONCEPT.md#formatter-canon
---

Code can compile and still not read like TorbScript. These are the habits the standard library follows that the guide
has not shown yet. Each is one rule and one example; the link after it has the details.

## Goal

At the end of this page your code reads like the standard library: the names, the types and the resources in the one
form the language picked for them.

## Names are whole words {#names}

**Write `Expression`, `squareRoot` and `Item`, never `Expr`, `sqrt` or `T`.** A name is `camelCase`, and only a
type, a trait or a case starts with an uppercase letter: `maxSize`, never `MAX_SIZE`. Abbreviations that are the
name already stay: `Json`, `Http`, `min`, `max`.

```trb run
fn firstOrDefault<Item>(items: List<Item>, fallback: Item): Item {
  items.first() ?? fallback
}

print firstOrDefault([3, 4], 0)
// prints 3
```

A trait with one method is named after the method, `Hash` or `Show`: nothing ends in `-able`. A `Bool` field is an
adjective, `enabled`, not `isEnabled`; a method may ask, `isEmpty()`. See Naming (skill `torbscript-language`: `references/language/syntax/naming.md`).

## A type is a value unless it needs an identity {#values}

**Write `type` by default. Write `shared type` only for a thing that everybody holding it must see as the same one: a
connection, a file, a window.**

```trb run
type Point {
  var x: Int
  var y: Int
}

var start = Point 0, 0
var moved = start
moved.x = 5
print "{start.x} {moved.x}"
// prints 0 5
```

A value is never shared by accident, so a change happens only where it is written. See
Shared types (skill `torbscript-language`: `references/language/types/shared-types.md`).

## A rule a value must keep has one door {#capsules}

**When every value of a type must keep a rule, make the field `private` and give the type a `static fn` that checks
the rule.** The private field closes the constructor, so the check cannot be walked around.

```trb run
type Percent {
  private value: Int

  static fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Self value
  }

  fn percent(): Int {
    value
  }
}

print Percent.tryFrom(120)
print Percent.tryFrom(40).map({ _.percent() })
// prints Fail("120 is not between 0 and 100")
// prints Ok(40)
```

The reference calls such a type a capsule: Data or capsule (skill `torbscript-language`: `references/language/types/data-or-capsule.md`).

## The success is the value {#wrapping}

**A function that returns an `Option` or a `Result` ends in the plain value, not in `Some(value)` or `Ok(value)`.**
Write `None` and `Fail`; the value is wrapped for you.

```trb run
fn find(names: List<String>, wanted: String): Int? {
  for index in 0..names.length() {
    if names[index] == wanted {
      return index
    }
  }
  None
}

print find(["ada", "alan"], "alan")
// prints Some(1)
```

`torb lint --rule redundant-wrap` finds the rest, and `--fix` removes them. See
Conversions (skill `torbscript-language`: `references/language/types/conversions.md`).

## A resource is bound with using {#resources}

**A file, a socket or a lock is a `shared type` with `Close`, bound with `using`.** Nothing calls `close()` by hand:
it runs once, at the end of the block that holds the last reference.

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

See Destructors (skill `torbscript-language`: `references/language/execution/destructors.md`).

## The formatter decides the layout {#format}

**Where the language allows two spellings, `torb format` picks one.** Run it before you commit, and let `--check`
fail a build that is not in the layout.

```console
$ torb format src
$ torb format --check src
```

See [torb format](../tooling/torb-format.md).

## More habits {#more-habits}

Each of these is a rule of the reference page it links:

- A field default is a constant; compute anything else in a `static fn` -
  Construction (skill `torbscript-language`: `references/language/types/construction.md`).
- A closure that reads or writes a `var` is handed to a call that runs it, never stored or returned -
  Closures (skill `torbscript-language`: `references/language/functions/closures.md`).
- A parameter that takes a closure says what it is for: `Predicate<Item>`, `Action<Item>`, `Transform<Item, Output>` -
  Predicate, Action and Transform (skill `torbscript-standard-library`: `references/standard-library/function-types.md`).
- `From` is written by hand, `Into` comes with it, and there are no casts -
  Conversions (skill `torbscript-language`: `references/language/types/conversions.md`).
- `await()` returns the value, and a cancelled task stops whoever waits for it -
  Tasks (skill `torbscript-concurrency`: `references/language/concurrency-and-streams/tasks.md`).
- The habits from Rust, Swift, Kotlin and TypeScript that do not compile, with the message for each -
  [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md).

## Next

- The language reference (skill `torbscript-language`: `references/language/index.md`) - the exact rule behind every habit here.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language on one page.
- Why the language is like this (skill `torbscript-language`: `references/explanation/index.md`) - the reasons behind these habits.

