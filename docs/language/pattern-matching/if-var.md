---
title: if var
summary: if var P = place binds a pattern into the place itself, exactly like a var parameter, instead of copying the value out first.
kind: reference
status: stable
order: 60
keywords:
  - var pattern
  - mutable binding
  - var path
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - CONCEPT.md#var-paths-and-var-parameters
---

Every other pattern binds a copy: `if const Some(user) = current` gives the body a `user` that is its own value, and
changing it changes nothing `current` holds. `if var` is the one exception, for the one case where that copy is the
problem.

## Example

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

fn run() {
  var current: Counter? = Some Counter()
  if var Some(counter) = current {
    counter.increment()
  }
  print current
}

run()
```

## Syntax

```text
if var <pattern> = <place> { ... }
while var <pattern> = <place> { ... }
```

## Rules

1. **`if var P = place` binds the names of `P` into `place` itself, not into a copy of what `place` holds.** Inside
   the body, a name the pattern bound is a `var` path back to that part of `place`, exactly as a `var` parameter is a
   path back to the caller's argument.

2. **The subject has to be a `var` path.** Everything [Mutation and var paths](../types/var-paths.md) allows as the
   argument of a `var` parameter is allowed here: a `var` binding, a field of one, an index, a slice. A `const`
   binding, a temporary, or the result of a call is not a `var` path, and none of them can stand after `if var`.

3. **The body runs inside a `var` access to the matched part of `place`.** A method that is a `var fn`, such as
   `increment` above, can be called on the bound name precisely because the pattern reached it through a `var` path
   and not through a copy.

4. **A pattern that only binds names benefits from `if var`; a pattern that only reads does not need it.** `if var`
   exists so that `if const Some(iterator) = current { iterator.next() }` can change the very iterator `current`
   holds - not to make ordinary reads faster, and not to change what the pattern itself matches.

## What this is not

**`if var` is not a second syntax for the same thing `if const` does.** The two differ in exactly one respect: what a
bound name is a path to. Reading through either one looks identical; only a write through the name shows the
difference, which is why picking the wrong one is easy to miss until a write silently lands on a copy.

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

fn run() {
  var current: Counter? = Some Counter()
  if var Some(counter) = current {
    counter.increment()
  }
  print current
}

run()
```

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

fn run() {
  var current: Counter? = Some Counter()
  if const Some(counter) = current {
    var mutableCopy = counter
    mutableCopy.increment()
    print mutableCopy
  }
  print current
}

run()
```

The second program type checks: `counter` under `if const` is a `const` name, so it has to be copied into a `var`
binding before anything can call `increment` on it - and that call changes the copy, not `current`. `if var` skips the
copy instead of requiring one.

**`if var` is not a reference that outlives the `if`.** The bound name is a `var` path only for the body, the same
rule that makes a `var` parameter second-class: it cannot be stored in a field, returned, or captured by a closure
that escapes the block.

**This page describes what the checker accepts. Running a program that uses `if var` is still ahead of the toolchain**:
the native back end does not lower `if var` or `while var` yet, so `torb build` cannot turn the example above into a
binary today. `torb check` accepts it, which is what every block on this page is verified against.

## Related

- [Mutation and var paths](../types/var-paths.md) - what counts as a `var` path anywhere in the language.
- [Patterns in bindings and conditions](patterns-in-bindings.md) - `if const` and `while const`, the copying siblings.
- [Why values instead of references](../../explanation/why-values-instead-of-references.md) - why a `var` path exists
  only for the duration of a call or a block, and never longer.
