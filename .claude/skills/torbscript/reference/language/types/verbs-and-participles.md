---
title: Verbs and participles
summary: A verb changes its receiver in place and declares var self, and its participle answers a changed copy instead, so calling the verb through a const path names the participle in its error.
kind: reference
status: stable
order: 70
keywords:
  - verb
  - participle
  - var self
  - naming
source:
  - CONCEPT.md#lexical-structure
  - std/collections/src/list.trb
---

Two methods often come in a pair: one that changes a value in place and one that answers a changed copy instead. The
standard library names them so the difference is visible from the name alone, and the compiler leans on the same
naming when it explains why a call was rejected.

## Example

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

## Syntax

```text
fn <verb>(var self, <parameters>)                    // Changes in place, answers nothing
fn <participle>(self, <parameters>): <Type>          // Answers a changed copy, leaves self alone
```

## Rules

1. **A verb changes its receiver in place and declares `var self`.** `increment` needs a `var` path to run, exactly
   like any other method that declares `var self`.

2. **A participle answers a changed copy and declares only `self`.** `incremented` never needs a `var` path, because
   `counter` itself is untouched - `copy` already does the work of building the new value.

3. **Calling a verb through a `const` path is an error that names the participle**, when the compiler can build one
   for the method's name. This is the one message of the language that suggests the member a caller probably wanted.

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

4. **A noun never changes anything.** `union` and `intersection` read as operations, not as verbs, so neither one
   needs `var self` - a name that could be misread as a verb is a naming mistake independent of what the member
   actually does.

5. **A verb is picked so that its participle is a different word.** The standard library avoids `put`, `cut` and
   `reset` as mutator names, because none of them has a distinct participle to pair with; `set` pairs with `updated`
   instead of a participle of its own.

## What this is not

**A missing `var` is not fixed by adding `()` or a different call style.** The error is about the path the receiver
came through, not about how the call is written - `frozen.increment()` and `frozen.increment` fail for the same
reason, because `frozen` itself is the problem.

```trb
var counter = Counter()
counter.increment()
print counter.value
```

```trb error
type Counter {
  var value: Int = 0

  fn increment(var self) {
    value = value + 1
  }
}

fn tick(counter: Counter) {
  counter.increment()
}
// error: `increment` needs a `var`
```

## Related

- [Declaring a type](declaring-a-type.md) - where a verb and its participle first appear, `translate`/`translated`.
- [Mutation and var paths](var-paths.md) - what makes a path a `var` path in the first place.
- [Methods and static functions](methods.md) - `self` against no `self`, the more basic distinction.
